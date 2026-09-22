#include "nfc.h"
#include "app_state.h"
#include "mqtt_app.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

static pn532_io_t s_io;

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
            ESP_LOGW(TAG, "PN532 init failed (%s)", esp_err_to_name(err));
            pn532_release(&s_io);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (err != ESP_OK);

    uint32_t version = 0;
    do {
        err = pn532_get_firmware_version(&s_io, &version);
        if (err != ESP_OK) {
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
        const bool present = (err == ESP_OK && uid_len > 0 && uid_len <= UID_MAX_LEN);
        app_state_set_presence(present, present ? uid : NULL, present ? uid_len : 0);

        bool found = false;
        if (app_state_take_tag_change(&found)) {
            char hex[24] = {0};
            if (present) {
                app_state_format_uid_hex(uid, uid_len, hex, sizeof(hex));
            }
            ESP_LOGI(TAG, "tag change present=%d uid=%s targetFound=%d",
                     present, present ? hex : "none", found);
            mqtt_app_publish_status(found);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void nfc_start(void)
{
    xTaskCreate(nfc_task, "nfc", 4096, NULL, 5, NULL);
}