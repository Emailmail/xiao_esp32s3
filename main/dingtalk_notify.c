#include "dingtalk_notify.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "dingtalk";

static char g_resp_buf[512];
static int  g_resp_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (g_resp_len + evt->data_len < sizeof(g_resp_buf)) {
            memcpy(g_resp_buf + g_resp_len, evt->data, evt->data_len);
            g_resp_len += evt->data_len;
            g_resp_buf[g_resp_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ==================== 钉钉机器人实现 ====================
// 官方文档: https://open.dingtalk.com/document/robots/custom-robot-access
//
// 原理:
//   1. HTTP POST 到钉钉 Webhook URL
//   2. JSON Body: {"msgtype":"text","text":{"content":"消息内容"}}
//   3. 如果配置了加签，还需要在 URL 中带上 timestamp 和 sign 参数

esp_err_t dingtalk_send(const char *content)
{
    // 检查 token 是否已配置
    if (strcmp(DINGTALK_ACCESS_TOKEN, "your_dingtalk_token_here") == 0) {
        ESP_LOGW(TAG, "钉钉 token 未配置，请在 dingtalk_notify.h 中设置 DINGTALK_ACCESS_TOKEN");
        return ESP_ERR_INVALID_ARG;
    }

    // ---------- 构造 Webhook URL ----------
    char url[256];
    snprintf(url, sizeof(url),
             "https://oapi.dingtalk.com/robot/send?access_token=%s",
             DINGTALK_ACCESS_TOKEN);

    // ---------- 构造 JSON 消息体 ----------
    // 钉钉 text 类型的消息格式:
    // {"msgtype":"text","text":{"content":"这里是消息内容"}}
    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
             "{\"msgtype\":\"text\",\"text\":{\"content\":\"%s\"}}",
             content);

    ESP_LOGI(TAG, "正在发送钉钉消息: %s", content);

    g_resp_len = 0;
    memset(g_resp_buf, 0, sizeof(g_resp_buf));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // ---------- 发送请求 ----------
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "钉钉服务器响应: HTTP %d", status);
        ESP_LOGI(TAG, "钉钉响应内容: %s", g_resp_buf);
        if (status == 200 && strstr(g_resp_buf, "\"errcode\":0")) {
            ESP_LOGI(TAG, "钉钉消息发送成功!");
        } else {
            ESP_LOGW(TAG, "钉钉返回异常: %s", g_resp_buf);
        }
    } else {
        ESP_LOGE(TAG, "钉钉发送失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}
