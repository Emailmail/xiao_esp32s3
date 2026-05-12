#include "wifi_app.h"
#include "sntp_sync.h"
#include "weather.h"
#include "dingtalk_notify.h"
#include "bme680.h"
#include "env_monitor.h"
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    wifi_init_sta();

    if (obtain_time()) {
        print_current_time();
    } else {
        ESP_LOGE(TAG, "无法获取网络时间");
    }

    if (!fetch_weather()) {
        ESP_LOGE(TAG, "获取天气失败");
    }

    dingtalk_send("[esp] 大家好！我是麻字节！");

    /* ── BME680 + environment monitor ── */
    if (bme680_init() == ESP_OK) {
        env_monitor_start(NULL);  /* use default thresholds */
        ESP_LOGI(TAG, "环境监测已启动");
    } else {
        ESP_LOGW(TAG, "BME680 未连接，环境监测跳过");
    }
}
