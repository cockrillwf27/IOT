#pragma once
#include <stdint.h>
void rgb_led_init(void);
void rgb_led_set(uint8_t r, uint8_t g, uint8_t b);
void rgb_led_off(void);
void rgb_led_red(void);
void rgb_led_green(void);
void rgb_led_blue(void);