#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "nvs_store.h"

void nfc_start(void);
void nfc_request_enroll(void);
void nfc_get_status(bool *enroll, bool *present,
                    uint8_t *target, uint8_t *target_len,
                    uint8_t *current, uint8_t *current_len);
void nfc_format_uid(const uint8_t *uid, uint8_t len, char *out, size_t out_len);