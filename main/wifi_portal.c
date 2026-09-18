#include "wifi_portal.h"
#include "nvs_store.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "wifi";

#define WIFI_RESET_GPIO     GPIO_NUM_0
#define WIFI_RESET_HOLD_MS  3000
#define AP_SSID             "EE419-Lab1-Setup"
#define AP_CHANNEL          1
#define STA_MAX_RETRY       8
#define CONNECT_WAIT_MS     15000
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;
static volatile bool s_dns_run;
static volatile bool s_connected;
static wifi_got_ip_cb_t s_got_ip_cb;

static bool boot_button_held(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << WIFI_RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    if (gpio_get_level(WIFI_RESET_GPIO) != 0) {
        return false;
    }
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(WIFI_RESET_GPIO) == 0) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(WIFI_RESET_HOLD_MS)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static int hex_nibble(char c) //for url decode
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{   //Decodes URL-encoded string 
    size_t di = 0;
    for (size_t si = 0; si < src_len && di + 1 < dst_len; ++si) {
        if (src[si] == '+') {
            dst[di++] = ' ';
        } else if (src[si] == '%' && si + 2 < src_len) {
            int hi = hex_nibble(src[si + 1]);
            int lo = hex_nibble(src[si + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                si += 2;
            } else {
                dst[di++] = src[si];
            }
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static bool form_get_field(const char *body, const char *key, char *out, size_t out_len)
{ //Extracts a field from a URL-encoded form submission. Returns true if found, false if not.
    size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t tok_len = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', tok_len);
        if (eq) {
            size_t klen = (size_t)(eq - p);
            if (klen == key_len && strncmp(p, key, key_len) == 0) {
                url_decode(out, out_len, eq + 1, tok_len - klen - 1);
                return true;
            }
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static const char *PORTAL_HTML =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>EE-419 Wi-Fi Setup</title></head><body>"
    "<h1>EE-419 Wi-Fi setup</h1>"
    "<form method='POST' action='/save'>"
    "<label>SSID</label><input name='ssid' required maxlength='32' placeholder='NEB426'><br>"
    "<label>Password</label><input name='pass' type='password' maxlength='64'><br>"
    "<button type='submit'>Save and connect</button>"
    "</form></body></html>";

static const char *SAVED_HTML =
    "<!DOCTYPE html><html><body><h1>Saved</h1>"
    "<p>Rebooting. You can close this page.</p></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{ //Serves the Wi-Fi setup page.    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{ //Handles the form submission from the Wi-Fi setup page. Saves SSID and password to NVS and reboots.
    char buf[256];
    if (req->content_len <= 0 || req->content_len >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad form");
        return ESP_FAIL;
    }
    int n = httpd_req_recv(req, buf, req->content_len);
    if (n <= 0) {
        return ESP_FAIL;
    }
    buf[n] = '\0';
    char ssid[33] = {0};
    char pass[65] = {0};
    if (!form_get_field(buf, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }
    form_get_field(buf, "pass", pass, sizeof(pass));
    ESP_LOGI(TAG, "Portal saved SSID='%s'", ssid);
    nvs_store_wifi_save(ssid, pass);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
    return ESP_OK;
}

static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err)
{ //Redirects all unknown requests to the root page.
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, "Redirect", HTTPD_RESP_USE_STRLEN);
}

static void start_webserver(void)
{ //Starts the HTTP server and registers the URI handlers for the Wi-Fi setup page.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        return;
    }
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    const httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_handler);
}

static void dns_task(void *arg)
{ //Simple DNS server that responds to all queries with the AP IP address. This is used for the captive portal.
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    const uint8_t ap_ip[4] = {192, 168, 4, 1};
    uint8_t rx[512];
    s_dns_run = true;
    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&from, &fromlen);
        if (n < 12 || n + 16 > (int)sizeof(rx)) {
            continue;
        }
        uint8_t tx[512];
        memcpy(tx, rx, n);
        tx[2] = 0x81; tx[3] = 0x80;
        tx[4] = 0x00; tx[5] = 0x01;
        tx[6] = 0x00; tx[7] = 0x01;
        tx[8] = tx[9] = tx[10] = tx[11] = 0;
        int off = n;
        tx[off++] = 0xC0; tx[off++] = 0x0C;
        tx[off++] = 0x00; tx[off++] = 0x01;
        tx[off++] = 0x00; tx[off++] = 0x01;
        tx[off++] = 0x00; tx[off++] = 0x00; tx[off++] = 0x00; tx[off++] = 30;
        tx[off++] = 0x00; tx[off++] = 0x04;
        memcpy(&tx[off], ap_ip, 4);
        off += 4;
        sendto(sock, tx, off, 0, (struct sockaddr *)&from, fromlen);
    }
    close(sock);
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{ //Handles Wi-Fi events: connects to AP, retries on failure, and calls the user callback on successful IP acquisition.
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_num < STA_MAX_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "STA retry %d/%d", s_retry_num, STA_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&e->ip_info.ip));
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_got_ip_cb) {
            s_got_ip_cb();
        }
    }
}

static void start_captive_portal(void)
{ //Starts the Wi-Fi AP and the captive portal web server.
    ESP_LOGI(TAG, "Captive portal SSID='%s'", AP_SSID);
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(AP_SSID);
    ap.ap.channel = AP_CHANNEL;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    start_webserver();
    xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Join '%s' and open http://192.168.4.1/", AP_SSID);
}

static bool start_sta(const char *ssid, const char *pass)
{ //Starts Wi-Fi in station mode and connects to the given SSID and password. Returns true if successful, false if failed.
    ESP_LOGI(TAG, "STA SSID='%s'", ssid);
    esp_netif_create_default_wifi_sta();
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(CONNECT_WAIT_MS));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_portal_start(wifi_got_ip_cb_t on_got_ip)
{ //Starts the Wi-Fi portal. If saved credentials exist, tries to connect to them. Otherwise, starts the captive portal.
    s_got_ip_cb = on_got_ip;
    if (boot_button_held()) {
        nvs_store_wifi_erase();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
                        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
                        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    char ssid[33] = {0};
    char pass[65] = {0};
    if (nvs_store_wifi_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        if (!start_sta(ssid, pass)) {
            ESP_ERROR_CHECK(esp_wifi_stop());
            start_captive_portal();
        }
    } else {
        start_captive_portal();
    }
}

bool wifi_portal_is_connected(void)
{     //Returns true if Wi-Fi is connected to an AP, false otherwise.
    return s_connected;
}