#include "led_flash.h"
#include "app_state.h"
#include "rgb_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void flash_task(void *arg)
{
    (void)arg;
    while (1) {
        int count = 1;
        app_state_get_cmd(NULL, 0, &count);
        const bool found = app_state_target_found();

        for (int i = 0; i < count; i++) {
            if (found) {
                rgb_led_green();
            } else {
                rgb_led_red();
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            rgb_led_off();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        rgb_led_off();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void led_flash_start(void)
{
    xTaskCreate(flash_task, "led_flash", 2048, NULL, 4, NULL);
}