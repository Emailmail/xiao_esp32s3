#include "sntp_sync.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "sntp";

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "时间同步完成通知回调");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "正在初始化 SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "ntp.aliyun.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

bool obtain_time(void)
{
    initialize_sntp();

    int retry = 0;
    const int retry_count = 15;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "等待时间同步... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (retry >= retry_count) {
        ESP_LOGE(TAG, "时间同步超时!");
        return false;
    }

    ESP_LOGI(TAG, "时间同步成功!");
    return true;
}

void print_current_time(void)
{
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    ESP_LOGI(TAG, "当前时间: %s (UTC+8)", strftime_buf);
}
