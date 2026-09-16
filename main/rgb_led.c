#include "rgb_led.h"
#include "driver/gpio.h"

#define LED_R_GPIO GPIO_NUM_9
#define LED_G_GPIO GPIO_NUM_6
#define LED_B_GPIO GPIO_NUM_5

void rgb_led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_R_GPIO) | (1ULL << LED_G_GPIO) | (1ULL << LED_B_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    rgb_led_off();
}

void rgb_led_set(uint8_t r, uint8_t g, uint8_t b)
{
    gpio_set_level(LED_R_GPIO, r ? 1 : 0);
    gpio_set_level(LED_G_GPIO, g ? 1 : 0);
    gpio_set_level(LED_B_GPIO, b ? 1 : 0);
}

void rgb_led_off(void)   { rgb_led_set(0, 0, 0); }
void rgb_led_red(void)   { rgb_led_set(1, 0, 0); }
void rgb_led_green(void) { rgb_led_set(0, 1, 0); }
void rgb_led_blue(void)  { rgb_led_set(0, 0, 1); }