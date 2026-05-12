#include "wifi_app.h"
#include "sntp_sync.h"
#include "weather.h"
#include "dingtalk_notify.h"
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
}
