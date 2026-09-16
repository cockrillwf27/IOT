#include "esp_log.h"
#include "nvs_flash.h"
#include "rgb_led.h"
#include "wifi_portal.h"
#include "nfc.h"
#include "time_sync.h"
#include "web_app.h"

static const char *TAG = "lab2";

static void on_got_ip(void)
{
    time_sync_start();
    web_app_start();
}

void app_main(void)
{
    ESP_LOGI(TAG, "EE-419 Lab 2 starting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    rgb_led_init();
    nfc_start();
    wifi_portal_start(on_got_ip);
}