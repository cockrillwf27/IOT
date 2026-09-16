#pragma once

#include <stddef.h>

void time_sync_start(void);
void time_sync_now(char *out, size_t out_len);