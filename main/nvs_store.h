#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define UID_MAX_LEN 10

bool nvs_store_wifi_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_store_wifi_save(const char *ssid, const char *pass);
void nvs_store_wifi_erase(void);

bool nvs_store_target_load(uint8_t *uid, uint8_t *uid_len);
esp_err_t nvs_store_target_save(const uint8_t *uid, uint8_t uid_len);
void nvs_store_target_erase(void);