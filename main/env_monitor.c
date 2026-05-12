#include "env_monitor.h"
#include "bme680.h"
#include "dingtalk_notify.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "env_monitor";

/* ── state ─────────────────────────────────────────────────── */

static env_threshold_t threshold;

/* circular buffer for pressure trend (60 samples = 1 h at 1/min) */
#define HIST_LEN 60
static float  pressure_hist[HIST_LEN];
static int    hist_idx;
static int    hist_count;      /* 0..HIST_LEN, once full stays at HIST_LEN */

/* alert cooldown counters (in cycles, 1 cycle ≈ 60 s) */
#define COOLDOWN 30            /* 30 min between same-type alerts */
static int cooldown_temp_high;
static int cooldown_temp_low;
static int cooldown_humi_high;
static int cooldown_humi_low;
static int cooldown_pressure;
static int cooldown_voc;

/* sensor burn-in: skip gas alerts for first N readings */
#define BURN_IN 5
static int reading_count;

/* ── helpers ────────────────────────────────────────────────── */

static void push_pressure(float p)
{
    pressure_hist[hist_idx] = p;
    hist_idx = (hist_idx + 1) % HIST_LEN;
    if (hist_count < HIST_LEN) hist_count++;
}

/* Return pressure from N minutes ago (N must be ≤ hist_count). */
static float pressure_n_min_ago(int n)
{
    if (n >= hist_count) return 0.0f;
    int idx = (hist_idx - n + HIST_LEN) % HIST_LEN;
    return pressure_hist[idx];
}

static int cooldown_ok(int *cd)
{
    if (*cd > 0) return 0;   /* still cooling down */
    *cd = COOLDOWN;
    return 1;
}

static void tick_cooldowns(void)
{
    if (cooldown_temp_high  > 0) cooldown_temp_high--;
    if (cooldown_temp_low   > 0) cooldown_temp_low--;
    if (cooldown_humi_high  > 0) cooldown_humi_high--;
    if (cooldown_humi_low   > 0) cooldown_humi_low--;
    if (cooldown_pressure   > 0) cooldown_pressure--;
    if (cooldown_voc        > 0) cooldown_voc--;
}

/* ── alert evaluation ──────────────────────────────────────── */

static void evaluate(const bme680_data_t *d)
{
    char msg[256];
    int combo = 0;

    /* ── temperature ── */
    if (d->temperature > threshold.temp_high && cooldown_ok(&cooldown_temp_high)) {
        snprintf(msg, sizeof(msg),
                 "\xe2\x9a\xa0\xef\xb8\x8f 室内温度过高 %.1f°C（阈值 %.0f°C），建议开窗或开空调。",
                 d->temperature, threshold.temp_high);
        dingtalk_send(msg);
        combo++;
    }
    if (d->temperature < threshold.temp_low && cooldown_ok(&cooldown_temp_low)) {
        snprintf(msg, sizeof(msg),
                 "\xe2\x9d\x84\xef\xb8\x8f 室内温度过低 %.1f°C（阈值 %.0f°C），注意保暖。",
                 d->temperature, threshold.temp_low);
        dingtalk_send(msg);
        combo++;
    }

    /* ── humidity ── */
    if (d->humidity > threshold.humidity_high && cooldown_ok(&cooldown_humi_high)) {
        snprintf(msg, sizeof(msg),
                 "\xf0\x9f\x92\xa7 室内湿度过高 %.1f%%（阈值 %.0f%%），建议除湿或开窗。",
                 d->humidity, threshold.humidity_high);
        dingtalk_send(msg);
        combo++;
    }
    if (d->humidity < threshold.humidity_low && cooldown_ok(&cooldown_humi_low)) {
        snprintf(msg, sizeof(msg),
                 "\xf0\x9f\x8f\x9c\xef\xb8\x8f 室内湿度过低 %.1f%%（阈值 %.0f%%），建议加湿。",
                 d->humidity, threshold.humidity_low);
        dingtalk_send(msg);
        combo++;
    }

    /* ── pressure drop (30-min window) ── */
    if (hist_count >= 30) {
        float old_p = pressure_n_min_ago(30);
        float drop  = old_p - d->pressure;
        if (drop >= threshold.pressure_drop && cooldown_ok(&cooldown_pressure)) {
            snprintf(msg, sizeof(msg),
                     "\xe2\x98\x94 气压 30 分钟内下降 %.1f hPa（当前 %.1f hPa），可能很快下雨，记得收衣服带伞。",
                     drop, d->pressure);
            dingtalk_send(msg);
            combo++;
        }
    }

    /* ── VOC / gas resistance ── */
    if (reading_count >= BURN_IN
        && d->gas_resistance > 0.1f
        && d->gas_resistance < threshold.gas_res_low
        && cooldown_ok(&cooldown_voc)) {
        snprintf(msg, sizeof(msg),
                 "\xf0\x9f\x98\xb7 室内空气质量差（VOC %.0f kΩ，阈值 %.0f kΩ），建议开窗通风。",
                 d->gas_resistance, threshold.gas_res_low);
        dingtalk_send(msg);
        combo++;
    }

    /* ── combo summary ── */
    if (combo >= 2) {
        snprintf(msg, sizeof(msg),
                 "\xe2\x9a\xa0\xef\xb8\x8f 多项环境指标同时异常"
                 "（温度 %.1f°C，湿度 %.1f%%，气压 %.1f hPa，VOC %.0f kΩ），请检查室内环境。",
                 d->temperature, d->humidity, d->pressure, d->gas_resistance);
        dingtalk_send(msg);
    }
}

/* ── monitoring task ───────────────────────────────────────── */

static void monitor_task(void *arg)
{
    ESP_LOGI(TAG, "task started, interval 60 s");

    /* give BME680 a warm-up cycle before the first real read */
    bme680_data_t dummy;
    bme680_read(&dummy);
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (;;) {
        bme680_data_t d;
        esp_err_t ret = bme680_read(&d);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "bme680_read fail: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        reading_count++;
        push_pressure(d.pressure);
        tick_cooldowns();
        evaluate(&d);

        ESP_LOGI(TAG, "T=%.1f°C H=%.1f%% P=%.1fhPa G=%.0fkΩ",
                 d.temperature, d.humidity, d.pressure, d.gas_resistance);

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

/* ── public API ────────────────────────────────────────────── */

esp_err_t env_monitor_start(const env_threshold_t *th)
{
    if (th) {
        memcpy(&threshold, th, sizeof(env_threshold_t));
    } else {
        threshold.temp_low      = ENV_DEFAULT_TEMP_LOW;
        threshold.temp_high     = ENV_DEFAULT_TEMP_HIGH;
        threshold.humidity_low  = ENV_DEFAULT_HUMIDITY_LOW;
        threshold.humidity_high = ENV_DEFAULT_HUMIDITY_HIGH;
        threshold.pressure_drop = ENV_DEFAULT_PRESSURE_DROP;
        threshold.gas_res_low   = ENV_DEFAULT_GAS_RES_LOW;
    }

    memset(pressure_hist, 0, sizeof(pressure_hist));
    hist_idx   = 0;
    hist_count = 0;
    reading_count = 0;
    cooldown_temp_high = 0;
    cooldown_temp_low  = 0;
    cooldown_humi_high = 0;
    cooldown_humi_low  = 0;
    cooldown_pressure  = 0;
    cooldown_voc       = 0;

    BaseType_t ret = xTaskCreate(monitor_task, "env_monitor",
                                 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate fail");
        return ESP_FAIL;
    }
    return ESP_OK;
}
