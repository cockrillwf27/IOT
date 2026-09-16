#include "time_sync.h"

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "time";

void time_sync_start(void)
{
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (America/New_York)");
}

void time_sync_now(char *out, size_t out_len)
{
    time_t now = 0;
    time(&now);
    if (now < 1700000000) {
        snprintf(out, out_len, "time not synced");
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    strftime(out, out_len, "%Y-%m-%d %H:%M:%S %Z", &t);
}