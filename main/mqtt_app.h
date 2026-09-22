#pragma once

#include <stdbool.h>

void mqtt_app_start(void);
void mqtt_app_publish_status(bool target_found);