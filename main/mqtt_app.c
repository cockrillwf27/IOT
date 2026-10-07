#include "mqtt_app.h"
#include "app_config.h"
#include "app_state.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[] asm("_binary_device_cert_pem_end");
extern const uint8_t device_key_pem_start[] asm("_binary_device_private_key_start");
extern const uint8_t device_key_pem_end[] asm("_binary_device_private_key_end");
extern const uint8_t server_cert_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_AmazonRootCA1_pem_end");

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
        ESP_LOGI(TAG, "connected to AWS");
        snprintf(s_cmd_topic, sizeof(s_cmd_topic), "ee419_lab/%s/cmd", DEVICE_ID);
        snprintf(s_status_topic, sizeof(s_status_topic), "ee419_lab/%s/status", DEVICE_ID);
        esp_mqtt_client_subscribe(s_client, s_cmd_topic, 1);
        ESP_LOGI(TAG, "subscribed %s", s_cmd_topic);

        char body[96];
        snprintf(body, sizeof(body), "{\"deviceId\":\"%s\"}", DEVICE_ID);
        esp_mqtt_client_publish(s_client, "ee419_lab/register", body, 0, 1, 0);
        ESP_LOGI(TAG, "published ee419_lab/register %s", body);
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
    static char s_lwt[96];
    static const char *alpn_protos[] = { "x-amzn-mqtt-ca", NULL };

    snprintf(s_cmd_topic, sizeof(s_cmd_topic), "ee419_lab/%s/cmd", DEVICE_ID);
    snprintf(s_status_topic, sizeof(s_status_topic), "ee419_lab/%s/status", DEVICE_ID);
    snprintf(s_lwt, sizeof(s_lwt), "{\"deviceId\":\"%s\"}", DEVICE_ID);

    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = "mqtts://a2rgwh0228ui2o-ats.iot.us-east-2.amazonaws.com:443",
            },
            .verification = {
                .certificate = (const char *)server_cert_pem_start,
                .certificate_len = 0,
                .alpn_protos = alpn_protos,
            },
        },
        .credentials = {
            .username = NULL,
            .client_id = DEVICE_ID,
            .authentication = {
                .password = NULL,
                .certificate = (const char *)device_cert_pem_start,
                .certificate_len = 0,
                .key = (const char *)device_key_pem_start,
                .key_len = 0,
            },
        },
        .session = {
            .last_will = {
                .topic = "ee419_lab/unregister",
                .msg = s_lwt,
                .qos = 0,
                .retain = false,
            },
            .keepalive = 60,
        },
    };

    ESP_LOGI(TAG, "MQTT AWS id %s", DEVICE_ID);
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(s_client);
}