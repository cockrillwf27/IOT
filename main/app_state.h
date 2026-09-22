#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "nvs_store.h"

void app_state_init(void);
void app_state_set_cmd(const char *target_tag_hex, int flash_count);
void app_state_get_cmd(char *target_hex, size_t hex_len, int *flash_count);
void app_state_set_presence(bool present, const uint8_t *uid, uint8_t uid_len);
bool app_state_target_found(void);
bool app_state_tag_present(void);
bool app_state_take_tag_change(bool *target_found);
bool app_state_uid_matches_target(const uint8_t *uid, uint8_t uid_len);
void app_state_format_uid_hex(const uint8_t *uid, uint8_t len, char *out, size_t out_len);