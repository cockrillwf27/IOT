idf_component_register(
    SRCS
        "main.c"
        "rgb_led.c"
        "nvs_store.c"
        "wifi_portal.c"
        "nfc.c"
       
       
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        nvs_flash
        lwip
        mdns
)