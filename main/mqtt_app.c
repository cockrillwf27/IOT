#include "mqtt_app.h"
#include "app_config.h"
#include "app_state.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mdns.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static char s_cmd_topic[80];
static char s_status_topic[80];

static void handle_cmd_json(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "cmd JSON parse failed");
        return;
    }
    const cJSON *flash = cJSON_GetObjectItem(root, "flashCount");
    const cJSON *tag = cJSON_GetObjectItem(root, "targetTag");
    int count = cJSON_IsNumber(flash) ? flash->valueint : 1;
    const char *hex = cJSON_IsString(tag) ? tag->valuestring : "";
    app_state_set_cmd(hex, count);
    cJSON_Delete(root);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t e = data;

    switch (id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected to broker");
        snprintf(s_cmd_topic, sizeof(s_cmd_topic), "lab3/%s/cmd", DEVICE_ID);
        snprintf(s_status_topic, sizeof(s_status_topic), "lab3/%s/status", DEVICE_ID);
        esp_mqtt_client_subscribe(s_client, s_cmd_topic, 1);
        ESP_LOGI(TAG, "subscribed %s", s_cmd_topic);

        char body[96];
        snprintf(body, sizeof(body), "{\"deviceId\":\"%s\"}", DEVICE_ID);
        esp_mqtt_client_publish(s_client, "lab3/register", body, 0, 1, 0);
        ESP_LOGI(TAG, "published lab3/register %s", body);
        break;
    }
    case MQTT_EVENT_DATA:
        if (e->topic_len && e->data_len) {
            ESP_LOGI(TAG, "rx %.*s : %.*s",
                     e->topic_len, e->topic, e->data_len, e->data);
            if (e->topic_len == (int)strlen(s_cmd_topic) &&
                strncmp(e->topic, s_cmd_topic, e->topic_len) == 0) {
                handle_cmd_json(e->data, e->data_len);
            }
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        break;
    default:
        break;
    }
}

static bool resolve_broker(char *ip, size_t ip_len)
{
    mdns_init();
    mdns_hostname_set(DEVICE_ID);
    mdns_instance_name_set("EE-419 Lab 3");

    esp_ip4_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    esp_err_t err = mdns_query_a("NEB426", 3000, &addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS lookup NEB426.local failed (%s)", esp_err_to_name(err));
        return false;
    }
    snprintf(ip, ip_len, IPSTR, IP2STR(&addr));
    ESP_LOGI(TAG, "NEB426.local -> %s", ip);
    return true;
}

void mqtt_app_publish_status(bool target_found)
{
    if (!s_client) {
        return;
    }
    char body[48];
    snprintf(body, sizeof(body), "{\"targetFound\":%s}", target_found ? "true" : "false");
    int mid = esp_mqtt_client_publish(s_client, s_status_topic, body, 0, 1, 0);
    ESP_LOGI(TAG, "status %s mid=%d", body, mid);
}

void mqtt_app_start(void)
{
    char ip[16] = {0};
    char uri[48];
    if (resolve_broker(ip, sizeof(ip))) {
        snprintf(uri, sizeof(uri), "mqtt://%s:%d", ip, MQTT_BROKER_PORT);
    } else {
        snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    }

    char lwt[96];
    snprintf(lwt, sizeof(lwt), "{\"deviceId\":\"%s\"}", DEVICE_ID);

    static char s_lwt[96];
    static char s_uri[48];
    strlcpy(s_lwt, lwt, sizeof(s_lwt));
    strlcpy(s_uri, uri, sizeof(s_uri));

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_uri,
        .credentials.client_id = DEVICE_ID,
        .session.last_will.topic = "lab3/unregister",
        .session.last_will.msg = s_lwt,
        .session.last_will.msg_len = 0,
        .session.last_will.qos = 1,
        .session.last_will.retain = 0,
        .session.keepalive = 30,
    };

    ESP_LOGI(TAG, "MQTT uri %s id %s", s_uri, DEVICE_ID);
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(s_client);
}