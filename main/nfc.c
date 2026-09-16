#include "nfc.h"
#include "rgb_led.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "pn532.h"
#include "pn532_driver_i2c.h"

static const char *TAG = "nfc";

#define I2C_SDA_GPIO    GPIO_NUM_3
#define I2C_SCL_GPIO    GPIO_NUM_4
#define I2C_POWER_GPIO  GPIO_NUM_7
#define NFC_RESET_GPIO  GPIO_NUM_NC
#define NFC_IRQ_GPIO    GPIO_NUM_NC
#define I2C_PORT        0
#define POLL_TIMEOUT_MS 250

static SemaphoreHandle_t s_lock;
static bool s_enroll;
static bool s_present;
static uint8_t s_target[UID_MAX_LEN];
static uint8_t s_target_len;
static uint8_t s_current[UID_MAX_LEN];
static uint8_t s_current_len;
static pn532_io_t s_io;

void nfc_format_uid(const uint8_t *uid, uint8_t len, char *out, size_t out_len)
{
    if (!out || out_len < 3) {
        return;
    }
    if (!uid || len == 0) {
        snprintf(out, out_len, "none");
        return;
    }
    out[0] = '\0';
    for (uint8_t i = 0; i < len && (strlen(out) + 4) < out_len; i++) {
        char tmp[4];
        snprintf(tmp, sizeof(tmp), "%s%02X", (i == 0) ? "" : " ", uid[i]);
        strlcat(out, tmp, out_len);
    }
}

void nfc_get_status(bool *enroll, bool *present,
                    uint8_t *target, uint8_t *target_len,
                    uint8_t *current, uint8_t *current_len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (enroll) {
        *enroll = s_enroll;
    }
    if (present) {
        *present = s_present;
    }
    if (target && target_len) {
        memcpy(target, s_target, s_target_len);
        *target_len = s_target_len;
    }
    if (current && current_len) {
        memcpy(current, s_current, s_current_len);
        *current_len = s_current_len;
    }
    xSemaphoreGive(s_lock);
}

void nfc_request_enroll(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_enroll = true;
    s_target_len = 0;
    memset(s_target, 0, sizeof(s_target));
    xSemaphoreGive(s_lock);
    nvs_store_target_erase();
    rgb_led_blue();
    ESP_LOGI(TAG, "Enroll mode: waiting for a new target tag");
}

static bool uids_equal(const uint8_t *a, uint8_t alen, const uint8_t *b, uint8_t blen)
{
    return alen == blen && alen > 0 && memcmp(a, b, alen) == 0;
}

static void apply_led(bool enroll, bool present, bool match)
{
    if (enroll) {
        rgb_led_blue();
    } else if (!present) {
        rgb_led_off();
    } else if (match) {
        rgb_led_green();
    } else {
        rgb_led_red();
    }
}

static void i2c_power_on(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << I2C_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(I2C_POWER_GPIO, 1);
}

static void nfc_task(void *arg)
{
    (void)arg;
    i2c_power_on();
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Init PN532 I2C SDA=%d SCL=%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    ESP_ERROR_CHECK(pn532_new_driver_i2c(I2C_SDA_GPIO, I2C_SCL_GPIO,
                                         NFC_RESET_GPIO, NFC_IRQ_GPIO,
                                         I2C_PORT, &s_io));

    esp_err_t err;
    do {
        err = pn532_init(&s_io);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PN532 init failed (%s) — check I2C switches and wiring",
                     esp_err_to_name(err));
            pn532_release(&s_io);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (err != ESP_OK);

    uint32_t version = 0;
    do {
        err = pn532_get_firmware_version(&s_io, &version);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No PN53x — retrying");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (err != ESP_OK);

    ESP_LOGI(TAG, "Found chip PN5%02X firmware %d.%d",
             (unsigned)((version >> 24) & 0xFF),
             (int)((version >> 16) & 0xFF),
             (int)((version >> 8) & 0xFF));

    pn532_set_passive_activation_retries(&s_io, 0x01);

    uint8_t uid[UID_MAX_LEN];
    uint8_t uid_len = 0;

    while (1) {
        err = pn532_read_passive_target_id(&s_io, PN532_BRTY_ISO14443A_106KBPS,
                                           uid, &uid_len, POLL_TIMEOUT_MS);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool enroll = s_enroll;
        if (err == ESP_OK && uid_len > 0 && uid_len <= UID_MAX_LEN) {
            s_present = true;
            s_current_len = uid_len;
            memcpy(s_current, uid, uid_len);

            if (enroll) {
                memcpy(s_target, uid, uid_len);
                s_target_len = uid_len;
                s_enroll = false;
                enroll = false;
                nvs_store_target_save(uid, uid_len);
                char hex[40];
                nfc_format_uid(uid, uid_len, hex, sizeof(hex));
                ESP_LOGI(TAG, "Target stored: %s", hex);
            }
        } else {
            s_present = false;
            s_current_len = 0;
        }

        bool match = s_present && uids_equal(s_current, s_current_len, s_target, s_target_len);
        bool present = s_present;
        xSemaphoreGive(s_lock);

        apply_led(enroll, present, match);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void nfc_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_target_len = 0;
    s_current_len = 0;
    s_present = false;
    if (nvs_store_target_load(s_target, &s_target_len)) {
        s_enroll = false;
        char hex[40];
        nfc_format_uid(s_target, s_target_len, hex, sizeof(hex));
        ESP_LOGI(TAG, "Loaded target UID %s", hex);
        rgb_led_off();
    } else {
        s_enroll = true;
        ESP_LOGI(TAG, "No target in NVS — enroll (LED blue)");
        rgb_led_blue();
    }
    xTaskCreate(nfc_task, "nfc", 4096, NULL, 5, NULL);
}