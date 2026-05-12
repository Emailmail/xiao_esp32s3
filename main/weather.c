#include "weather.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "weather";

#define WEATHER_CITY    "Beijing"
#define WEATHER_URL     "http://wttr.in/" WEATHER_CITY "?format=j1"
#define HTTP_RX_BUF_SIZE  8192

static char g_http_rx_buf[HTTP_RX_BUF_SIZE];
static int  g_http_rx_len = 0;

static char *json_get_str(const char *json, const char *key, char *out, int out_len)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos += strlen(search);
    while (*pos == ':' || *pos == ' ' || *pos == '\t') pos++;

    if (*pos == '"') {
        pos++;
        int i = 0;
        while (*pos && *pos != '"' && i < out_len - 1) {
            out[i++] = *pos++;
        }
        out[i] = '\0';
        return out;
    }
    return NULL;
}

static void parse_weather(const char *json_str)
{
    char val[128];

    ESP_LOGI(TAG, "========== 天气信息 ==========");
    ESP_LOGI(TAG, "城市: %s", WEATHER_CITY);

    if (json_get_str(json_str, "temp_C", val, sizeof(val)))
        ESP_LOGI(TAG, "温度: %s °C", val);
    if (json_get_str(json_str, "FeelsLikeC", val, sizeof(val)))
        ESP_LOGI(TAG, "体感: %s °C", val);
    if (json_get_str(json_str, "humidity", val, sizeof(val)))
        ESP_LOGI(TAG, "湿度: %s %%", val);
    if (json_get_str(json_str, "windspeedKmph", val, sizeof(val)))
        ESP_LOGI(TAG, "风速: %s km/h", val);
    if (json_get_str(json_str, "winddir16Point", val, sizeof(val)))
        ESP_LOGI(TAG, "风向: %s", val);

    const char *desc_start = strstr(json_str, "\"weatherDesc\"");
    const char *val_start = desc_start ? strstr(desc_start, "\"value\"") : NULL;
    if (val_start) {
        val_start = strchr(val_start, ':');
        if (val_start && json_get_str(val_start, "value", val, sizeof(val)))
            ESP_LOGI(TAG, "天气: %s", val);
    }

    ESP_LOGI(TAG, "==============================");
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (g_http_rx_len + evt->data_len < HTTP_RX_BUF_SIZE) {
            memcpy(g_http_rx_buf + g_http_rx_len, evt->data, evt->data_len);
            g_http_rx_len += evt->data_len;
            g_http_rx_buf[g_http_rx_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

bool fetch_weather(void)
{
    g_http_rx_len = 0;
    memset(g_http_rx_buf, 0, sizeof(g_http_rx_buf));

    esp_http_client_config_t config = {
        .url = WEATHER_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 30000,
        .buffer_size = HTTP_RX_BUF_SIZE,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return false;
    }

    ESP_LOGI(TAG, "正在请求天气数据...");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP 状态码: %d", status);
        if (status == 200 && g_http_rx_len > 0) {
            ESP_LOGI(TAG, "收到 %d 字节数据", g_http_rx_len);
            parse_weather(g_http_rx_buf);
            esp_http_client_cleanup(client);
            return true;
        }
    } else {
        ESP_LOGE(TAG, "HTTP 请求失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return false;
}
