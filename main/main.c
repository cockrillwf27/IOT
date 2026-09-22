#include "esp_log.h"
#include "nvs_flash.h"
#include "rgb_led.h"
#include "wifi_portal.h"
#include "nfc.h"
#include "app_state.h"
#include "led_flash.h"
#include "mqtt_app.h"

static const char *TAG = "lab3";

static void on_got_ip(void)
{
    mqtt_app_start();
}

void app_main(void)
{
    ESP_LOGI(TAG, "EE-419 Lab 3 starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    app_state_init();
    rgb_led_init();
    led_flash_start();
    nfc_start();
    wifi_portal_start(on_got_ip);
}