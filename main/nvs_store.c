#include "nvs_store.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_store";

#define WIFI_NS   "wifi"
#define WIFI_SSID "ssid"
#define WIFI_PASS "pass"

#define NFC_NS    "nfc"
#define NFC_UID   "uid"
#define NFC_LEN   "uid_len"

bool nvs_store_wifi_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t sl = ssid_len, pl = pass_len;
    esp_err_t e1 = nvs_get_str(h, WIFI_SSID, ssid, &sl);
    esp_err_t e2 = nvs_get_str(h, WIFI_PASS, pass, &pl);
    nvs_close(h);
    return (e1 == ESP_OK && e2 == ESP_OK && ssid[0] != '\0');
}

esp_err_t nvs_store_wifi_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, WIFI_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, WIFI_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void nvs_store_wifi_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "Wi-Fi credentials erased");
    }
}

bool nvs_store_target_load(uint8_t *uid, uint8_t *uid_len)
{
    nvs_handle_t h;
    if (nvs_open(NFC_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    uint8_t len = 0;
    esp_err_t e1 = nvs_get_u8(h, NFC_LEN, &len);
    size_t blen = UID_MAX_LEN;
    esp_err_t e2 = nvs_get_blob(h, NFC_UID, uid, &blen);
    nvs_close(h);
    if (e1 != ESP_OK || e2 != ESP_OK || len == 0 || len > UID_MAX_LEN) {
        return false;
    }
    *uid_len = len;
    return true;
}

esp_err_t nvs_store_target_save(const uint8_t *uid, uint8_t uid_len)
{
    if (uid_len == 0 || uid_len > UID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NFC_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, NFC_LEN, uid_len);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NFC_UID, uid, uid_len);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void nvs_store_target_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(NFC_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "Target NFC UID erased");
    }
}