#pragma once

#include <stdbool.h>

typedef void (*wifi_got_ip_cb_t)(void);

void wifi_portal_start(wifi_got_ip_cb_t on_got_ip);
bool wifi_portal_is_connected(void);