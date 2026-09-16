#include "web_app.h"
#include "nfc.h"
#include "time_sync.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "mdns.h"

static const char *TAG = "web";
#define MDNS_HOSTNAME "cockrillwf27-esp32s3"

static void uid_or_none(const uint8_t *uid, uint8_t len, bool valid, char *out, size_t out_len)
{
    if (!valid || len == 0) {
        snprintf(out, out_len, "none");
    } else {
        nfc_format_uid(uid, len, out, out_len);
    }
}

static esp_err_t root_get(httpd_req_t *req)
{
    bool enroll = false, present = false;
    uint8_t target[UID_MAX_LEN] = {0}, current[UID_MAX_LEN] = {0};
    uint8_t tlen = 0, clen = 0;
    nfc_get_status(&enroll, &present, target, &tlen, current, &clen);

    char now[40];
    char target_s[40];
    char current_s[40];
    time_sync_now(now, sizeof(now));
    uid_or_none(target, tlen, tlen > 0, target_s, sizeof(target_s));
    uid_or_none(current, clen, present, current_s, sizeof(current_s));

    char page[1600];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta charset='utf-8'>"
             "<meta http-equiv='refresh' content='2'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>EE-419 Lab 2</title>"
             "<style>body{font-family:sans-serif;max-width:32rem;margin:2rem auto;padding:0 1rem}"
             "table{border-collapse:collapse;width:100%%}td,th{border:1px solid #ccc;padding:.5rem;text-align:left}"
             "button{padding:.6rem 1rem;font-size:1rem;margin-top:1rem}</style></head><body>"
             "<h1>EE-419 Lab 2 — NFC</h1>"
             "<table>"
             "<tr><th>Date / time</th><td>%s</td></tr>"
             "<tr><th>Target tag</th><td>%s</td></tr>"
             "<tr><th>Current tag</th><td>%s</td></tr>"
             "<tr><th>Mode</th><td>%s</td></tr>"
             "</table>"
             "<form method='POST' action='/reset'>"
             "<button type='submit'>Reset target NFC tag</button>"
             "</form>"
             "<p>LED: blue=enroll, green=match, red=other tag, off=none</p>"
             "</body></html>",
             now, target_s, current_s, enroll ? "enroll (waiting for a tag)" : "scan");

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t reset_post(httpd_req_t *req)
{
    nfc_request_enroll();
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

void web_app_start(void)
{
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("EE-419 Lab 2 ESP32-S3"));
    ESP_ERROR_CHECK(mdns_service_add("EE419-NFC", "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "mDNS http://%s.local/", MDNS_HOSTNAME);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    const httpd_uri_t reset = { .uri = "/reset", .method = HTTP_POST, .handler = reset_post };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &reset);
    ESP_LOGI(TAG, "Status page on port 80");
}