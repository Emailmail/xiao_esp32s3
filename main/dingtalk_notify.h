#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 钉钉机器人配置 ====================
// 创建钉钉群 → 群设置 → 智能群助手 → 添加机器人 → 自定义(Webhook)
// 复制 Webhook URL 中 access_token= 后面的值
#define DINGTALK_ACCESS_TOKEN  "3f2af192543038dd318742e716569579474a9ae63bd769db6cb6e75f13347d24"

// 签名密钥（可选，在机器人安全设置中选择「加签」后获取）
// 如果设为空字符串 "" 则跳过加签
#define DINGTALK_SECRET  ""

// ==================== API ====================

/**
 * @brief  通过钉钉机器人发送群消息
 * @param  content  消息内容（纯文本）
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t dingtalk_send(const char *content);

#ifdef __cplusplus
}
#endif
