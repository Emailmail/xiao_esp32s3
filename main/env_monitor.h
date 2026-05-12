#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── threshold configuration ───────────────────────────────── */
typedef struct {
    float temp_low;         /* °C   below → cold alert */
    float temp_high;        /* °C   above → hot alert */
    float humidity_low;     /* %RH  below → dry alert */
    float humidity_high;    /* %RH  above → damp alert */
    float pressure_drop;    /* hPa  drop within 1 h → rain-likely alert */
    float gas_res_low;      /* kΩ   below → poor air quality alert */
} env_threshold_t;

/* Default thresholds — adjust to taste */
#define ENV_DEFAULT_TEMP_LOW         10.0f
#define ENV_DEFAULT_TEMP_HIGH        33.0f
#define ENV_DEFAULT_HUMIDITY_LOW     20.0f
#define ENV_DEFAULT_HUMIDITY_HIGH    75.0f
#define ENV_DEFAULT_PRESSURE_DROP     2.0f
#define ENV_DEFAULT_GAS_RES_LOW      50.0f

/**
 * Start the environment monitoring FreeRTOS task.
 * Reads BME680 every 60 s and sends DingTalk alerts on threshold crosses.
 *
 * @param threshold  Pointer to static threshold struct (copied).
 *                   Pass NULL to use ENV_DEFAULT_* values.
 * @return ESP_OK on success.
 */
esp_err_t env_monitor_start(const env_threshold_t *threshold);

#ifdef __cplusplus
}
#endif
