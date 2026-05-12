#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C configuration */
#define BME680_I2C_MASTER_PORT   I2C_NUM_0
#define BME680_I2C_SDA_IO        GPIO_NUM_4
#define BME680_I2C_SCL_IO        GPIO_NUM_5
#define BME680_I2C_FREQ_HZ       100000
#define BME680_I2C_ADDR          0x76       /* SDO pulled low; use 0x77 if SDO=VDDIO */

/* Sensor data */
typedef struct {
    float temperature;      /* °C */
    float humidity;         /* %RH */
    float pressure;         /* hPa */
    float gas_resistance;   /* kΩ  — lower = more VOCs */
} bme680_data_t;

/**
 * Initialize I2C bus and BME680 sensor.
 * Call once at startup.
 * Returns ESP_OK on success.
 */
esp_err_t bme680_init(void);

/**
 * Trigger a forced-mode measurement and read compensated data.
 * Blocks ~300 ms while the gas heater runs.
 * Returns ESP_OK on success.
 */
esp_err_t bme680_read(bme680_data_t *out);

#ifdef __cplusplus
}
#endif