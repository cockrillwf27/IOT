#include "app_state.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "state";

static SemaphoreHandle_t s_lock;
static char s_target_hex[24];
static int s_flash_count = 1;
static bool s_present;
static bool s_target_found;
static uint8_t s_uid[UID_MAX_LEN];
static uint8_t s_uid_len;
static bool s_tag_changed;

static void normalize_hex(char *dst, size_t dst_len, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < dst_len; i++) {
        if (isxdigit((unsigned char)src[i])) {
            dst[j++] = (char)toupper((unsigned char)src[i]);
        }
    }
    dst[j] = '\0';
}

void app_state_format_uid_hex(const uint8_t *uid, uint8_t len, char *out, size_t out_len)
{
    out[0] = '\0';
    if (!uid || !len) {
        return;
    }
    for (uint8_t i = 0; i < len && (strlen(out) + 3) < out_len; i++) {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", uid[i]);
        strlcat(out, tmp, out_len);
    }
}

bool app_state_uid_matches_target(const uint8_t *uid, uint8_t uid_len)
{
    char got[24];
    char want[24];
    app_state_format_uid_hex(uid, uid_len, got, sizeof(got));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strncpy(want, s_target_hex, sizeof(want) - 1);
    want[sizeof(want) - 1] = '\0';
    xSemaphoreGive(s_lock);
    if (want[0] == '\0' || got[0] == '\0') {
        return false;
    }
    if (strncmp(got, want, strlen(want)) == 0) {
        return true;
    }
    return strcmp(got, want) == 0;
}

void app_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_target_hex[0] = '\0';
    s_flash_count = 1;
}

void app_state_set_cmd(const char *target_tag_hex, int flash_count)
{
    if (flash_count < 1) {
        flash_count = 1;
    }
    if (flash_count > 10) {
        flash_count = 10;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    normalize_hex(s_target_hex, sizeof(s_target_hex), target_tag_hex);
    s_flash_count = flash_count;
    char got[24];
    app_state_format_uid_hex(s_uid, s_uid_len, got, sizeof(got));
    s_target_found = s_present && s_target_hex[0] &&
                     (strncmp(got, s_target_hex, strlen(s_target_hex)) == 0);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "cmd target=%s flashCount=%d", s_target_hex, flash_count);
}

void app_state_get_cmd(char *target_hex, size_t hex_len, int *flash_count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (target_hex && hex_len) {
        strlcpy(target_hex, s_target_hex, hex_len);
    }
    if (flash_count) {
        *flash_count = s_flash_count;
    }
    xSemaphoreGive(s_lock);
}

void app_state_set_presence(bool present, const uint8_t *uid, uint8_t uid_len)
{
    char got[24] = {0};
    if (present && uid && uid_len) {
        app_state_format_uid_hex(uid, uid_len, got, sizeof(got));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool uid_changed = (present != s_present) ||
                       (present && (uid_len != s_uid_len ||
                                    memcmp(s_uid, uid, uid_len) != 0));
    s_present = present;
    if (present && uid && uid_len <= UID_MAX_LEN) {
        memcpy(s_uid, uid, uid_len);
        s_uid_len = uid_len;
    } else {
        s_uid_len = 0;
    }
    s_target_found = present && s_target_hex[0] &&
                     (strncmp(got, s_target_hex, strlen(s_target_hex)) == 0);
    if (uid_changed) {
        s_tag_changed = true;
    }
    xSemaphoreGive(s_lock);
}

bool app_state_target_found(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool v = s_target_found;
    xSemaphoreGive(s_lock);
    return v;
}

bool app_state_tag_present(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool v = s_present;
    xSemaphoreGive(s_lock);
    return v;
}

bool app_state_take_tag_change(bool *target_found)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ch = s_tag_changed;
    if (ch) {
        s_tag_changed = false;
    }
    if (target_found) {
        *target_found = s_target_found;
    }
    xSemaphoreGive(s_lock);
    return ch;
}