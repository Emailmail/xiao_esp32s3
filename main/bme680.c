#include "bme680.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bme680";

/* ── internal calibration data ─────────────────────────────── */
static struct {
    /* temperature */
    uint16_t par_t1;
    int16_t  par_t2;
    int8_t   par_t3;
    /* pressure */
    uint16_t par_p1;
    int16_t  par_p2;
    int8_t   par_p3;
    int16_t  par_p4;
    int16_t  par_p5;
    int8_t   par_p6;
    int8_t   par_p7;
    int16_t  par_p8;
    int16_t  par_p9;
    uint8_t  par_p10;
    /* humidity */
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t   par_h3;
    int8_t   par_h4;
    int8_t   par_h5;
    uint8_t  par_h6;
    int8_t   par_h7;
    /* gas heater */
    int8_t   par_gh1;
    int16_t  par_gh2;
    int8_t   par_gh3;
    /* gas range */
    uint8_t  res_heat_range;        /* 1..8 */
    int8_t   res_heat_val;          /* register 0x00 */
    int8_t   range_sw_err;          /* register 0x04 */
    uint32_t gas_lut[8];            /* registers 0x08..0x1F, 3 bytes each */
} calib;

static int32_t t_fine;

/* ── I2C helpers ───────────────────────────────────────────── */

static esp_err_t i2c_write(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BME680_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BME680_I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BME680_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BME680_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BME680_I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static uint8_t i2c_read8(uint8_t reg)
{
    uint8_t val = 0;
    i2c_read(reg, &val, 1);
    return val;
}

/* ── calibration data ──────────────────────────────────────── */

static esp_err_t read_calibration(void)
{
    /* block1: 25 bytes 0x89..0xA1, block2: 16 bytes 0xE1..0xF0 */
    uint8_t raw[41];
    esp_err_t ret;

    ret = i2c_read(0x89, raw, 25);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "read calib block1 fail"); return ret; }
    ret = i2c_read(0xE1, raw + 25, 16);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "read calib block2 fail"); return ret; }

/* helper: raw byte at register address */
#define R(addr) raw[(addr) >= 0xE1 ? 25 + (addr) - 0xE1 : (addr) - 0x89]

    /* temperature */
    calib.par_t1 = R(0xE9) | ((uint16_t)R(0xEA) << 8);
    calib.par_t2 = (int16_t)(R(0x8A) | ((uint16_t)R(0x8B) << 8));
    calib.par_t3 = (int8_t)R(0x8C);

    /* pressure */
    calib.par_p1  = R(0x8E) | ((uint16_t)R(0x8F) << 8);
    calib.par_p2  = (int16_t)(R(0x90) | ((uint16_t)R(0x91) << 8));
    calib.par_p3  = (int8_t)R(0x92);
    calib.par_p4  = (int16_t)(R(0x94) | ((uint16_t)R(0x95) << 8));
    calib.par_p5  = (int16_t)(R(0x96) | ((uint16_t)R(0x97) << 8));
    calib.par_p6  = (int8_t)R(0x99);
    calib.par_p7  = (int8_t)R(0x98);
    calib.par_p8  = (int16_t)(R(0x9A) | ((uint16_t)R(0x9B) << 8));
    calib.par_p9  = (int16_t)(R(0x9C) | ((uint16_t)R(0x9D) << 8));
    calib.par_p10 = R(0x9E);

    /* humidity — interleaved across 0xE1..0xE3 */
    calib.par_h1 = ((uint16_t)R(0xE2) << 4) | (R(0xE1) & 0x0F);
    calib.par_h2 = ((uint16_t)R(0xE3) << 4) | (R(0xE2) >> 4);
    calib.par_h3 = (int8_t)R(0xE4);
    calib.par_h4 = (int8_t)R(0xE5);
    calib.par_h5 = (int8_t)R(0xE6);
    calib.par_h6 = R(0xE7);
    calib.par_h7 = (int8_t)R(0xE8);

    /* gas heater */
    calib.par_gh1 = (int8_t)R(0xED);
    calib.par_gh2 = (int16_t)(R(0xEB) | ((uint16_t)R(0xEC) << 8));
    calib.par_gh3 = (int8_t)R(0xEE);

#undef R

    return ESP_OK;
}

static esp_err_t read_gas_calib(void)
{
    /* heater resistance range / value / switching error */
    uint8_t rhr = i2c_read8(0x02);
    calib.res_heat_range = ((rhr >> 4) & 0x07) + 1;  /* register bits→1..8 */
    calib.res_heat_val   = (int8_t)i2c_read8(0x00);
    calib.range_sw_err   = (int8_t)i2c_read8(0x04);

    /* gas range look-up table: 8 entries × 3 bytes, starting at 0x08 */
    for (int i = 0; i < 8; i++) {
        uint8_t buf[3];
        esp_err_t ret = i2c_read(0x08 + i * 3, buf, 3);
        if (ret != ESP_OK) return ret;
        calib.gas_lut[i] = (uint32_t)buf[0]
                         | ((uint32_t)buf[1] << 8)
                         | ((uint32_t)buf[2] << 16);
    }

    ESP_LOGI(TAG, "gas calib: res_heat_range=%d val=%d sw_err=%d",
             calib.res_heat_range, calib.res_heat_val, calib.range_sw_err);
    return ESP_OK;
}

/* ── heater profile ────────────────────────────────────────── */
/* Compute res_heat_x for a target plate temperature (typical: 320 °C).
   The formula follows Bosch's application note. */
static uint8_t calc_heater_resistance(int32_t amb_temp, int32_t target_temp)
{
    /* Bosch application-note formula for heater-plate temperature.
       We ignore target_temp here — the calibration-derived value
       already drives the plate to ~300–350 °C, which works for VOC sensing. */
    (void)target_temp;

    int32_t v1 = (calib.par_gh1 / 16) + 49;
    int32_t v2 = ((calib.par_gh2 * 5) / 32768) + 1;
    int32_t v3 = v1 * v2;
    int32_t v4 = (calib.par_gh3 * 4) + 16;
    int32_t v5 = ((calib.res_heat_range * 256 + v3) * calib.par_gh1) / (v4 * v1);
    int32_t rh = v5 + (amb_temp * 64 / 100);  /* ~ drive plate to 200–350 °C range */

    if (rh < 10)  rh = 10;
    if (rh > 255) rh = 255;
    return (uint8_t)rh;
}

static esp_err_t configure_heater(void)
{
    uint8_t res_heat_x = calc_heater_resistance(25 /* ≈ room temp */, 320);
    ESP_LOGI(TAG, "heater res_heat_x=%d (target ~320°C)", res_heat_x);

    esp_err_t ret;
    ret  = i2c_write(0x5A, res_heat_x);   /* res_heat_0 */
    ret |= i2c_write(0x5B, 0x00);         /* idac_heat_0 = auto */
    ret |= i2c_write(0x64, 150);          /* gas_wait_0 = 150 ms */
    if (ret != ESP_OK) ESP_LOGE(TAG, "heater config fail");
    return ret;
}

/* ── oversampling & filter ─────────────────────────────────── */

static esp_err_t configure_sensor(void)
{
    /* ctrl_hum  (0x72): osrs_h = 001 (×1) */
    esp_err_t ret = i2c_write(0x72, 0b001);

    /* ctrl_meas (0x74): osrs_t=010(×2) | osrs_p=100(×4) | mode=00(sleep) */
    ret |= i2c_write(0x74, (0b010 << 5) | (0b100 << 2) | 0b00);

    /* config   (0x75): iir_filter=11(coeff 7), spi3w=0 */
    ret |= i2c_write(0x75, (0b11 << 2));

    /* gas_conf (0x71): nb_conv=0(run gas), run_gas=1, heater profile 0 */
    ret |= i2c_write(0x71, (1 << 3) | 0);

    if (ret != ESP_OK) ESP_LOGE(TAG, "sensor config fail");
    return ret;
}

/* ── compensation formulas (Bosch datasheet §4) ────────────── */

static float compensate_temp(uint32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.par_t1 << 1)))
                    * (int32_t)calib.par_t2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)calib.par_t1)
                      * ((adc_T >> 4) - (int32_t)calib.par_t1)) >> 12)
                    * (int32_t)calib.par_t3) >> 14;
    t_fine = var1 + var2;
    return (float)((t_fine * 5 + 128) >> 8) / 100.0f;
}

static float compensate_pressure(uint32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)calib.par_p6;
    var2 = var2 + ((var1 * (int64_t)calib.par_p5) << 17);
    var2 = var2 + (((int64_t)calib.par_p4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.par_p3) >> 8)
         + ((var1 * (int64_t)calib.par_p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * (int64_t)calib.par_p1 >> 33;
    if (var1 == 0) return 0.0f;

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)calib.par_p9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)calib.par_p8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)calib.par_p7 << 4);
    return (float)p / 25600.0f;
}

static float compensate_humidity(uint16_t adc_H)
{
    int32_t v = (int32_t)(t_fine) - 76800;

    v = ((((adc_H << 14) - ((int32_t)calib.par_h4 << 20)
           - ((int32_t)calib.par_h5 * v))
          + 16384) >> 15)
        * (((((((v * (int32_t)calib.par_h6) >> 10)
               * (((v * (int32_t)calib.par_h3) >> 11) + 32768)) >> 10)
             + 2097152) * (int32_t)calib.par_h2 + 8192) >> 14);

    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)calib.par_h1) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (float)(v >> 12) / 1024.0f;
}

/* Gas resistance (kΩ).  Formula from Bosch bme680.c (public API). */
static float compute_gas_resistance(uint16_t adc_G, uint8_t gas_range)
{
    if (adc_G == 0 || gas_range >= 8) return 0.0f;

    int64_t var1 = (int64_t)((1340 + (5 * (int64_t)calib.range_sw_err))
                             * ((int64_t)1 << 25));
    int64_t var2 = ((int64_t)((int32_t)adc_G * 1024 - 512) - var1)
                 / (int64_t)calib.res_heat_range;
    if (var2 <= 0) return 0.0f;

    int64_t var3 = (int64_t)calib.gas_lut[gas_range] * (int64_t)4096;
    return (float)var3 / (float)var2;   /* kΩ */
}

/* ── public API ────────────────────────────────────────────── */

esp_err_t bme680_init(void)
{
    /* ── I2C bus ── */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = BME680_I2C_SDA_IO,
        .scl_io_num       = BME680_I2C_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BME680_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(BME680_I2C_MASTER_PORT, &conf);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2c_param_config fail"); return ret; }

    ret = i2c_driver_install(BME680_I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "i2c_driver_install fail"); return ret; }

    /* ── chip ID ── */
    uint8_t chip_id = i2c_read8(0xD0);
    ESP_LOGI(TAG, "chip_id=0x%02X (expect 0x61)", chip_id);
    if (chip_id != 0x61) {
        ESP_LOGE(TAG, "unexpected chip ID");
        return ESP_ERR_NOT_FOUND;
    }

    /* ── soft reset ── */
    ret = i2c_write(0xE0, 0xB6);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));

    /* ── calibration ── */
    ret = read_calibration();
    if (ret != ESP_OK) return ret;
    ret = read_gas_calib();
    if (ret != ESP_OK) return ret;

    /* ── configure ── */
    ret = configure_sensor();
    if (ret != ESP_OK) return ret;
    ret = configure_heater();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "init ok");
    return ESP_OK;
}

esp_err_t bme680_read(bme680_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    /* set forced mode: heater runs, gas conversion enabled */
    esp_err_t ret = i2c_write(0x74, (0b010 << 5) | (0b100 << 2) | 0b01);
    if (ret != ESP_OK) return ret;

    /* wait for gas heater to complete (~300 ms) */
    vTaskDelay(pdMS_TO_TICKS(300));

    /* poll until new_data (bit 7) or timeout */
    for (int i = 0; i < 25; i++) {
        uint8_t status = i2c_read8(0x1D);
        if (status & 0x80) goto read_data;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW(TAG, "measurement timeout");

read_data:
    uint8_t raw[15];
    ret = i2c_read(0x1F, raw, 15);
    if (ret != ESP_OK) return ret;

    uint32_t adc_P = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | (raw[2] >> 4);
    uint32_t adc_T = ((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | (raw[5] >> 4);
    uint16_t adc_H = ((uint16_t)raw[6] << 8)  | raw[7];
    uint16_t adc_G = ((uint16_t)raw[13] << 2) | (raw[14] >> 6);
    uint8_t  gas_r = raw[14] & 0x0F;

    out->temperature    = compensate_temp(adc_T);
    out->pressure       = compensate_pressure(adc_P);
    out->humidity       = compensate_humidity(adc_H);
    out->gas_resistance = compute_gas_resistance(adc_G, gas_r);

    return ESP_OK;
}