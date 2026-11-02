/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board_app.h"
#include "imu_service.h"

#define QMI8658_ADDR            0x6B
#define QMI8658_ADDR_ALT        0x6A
#define QMI8658_I2C_HZ          400000
#define QMI8658_TIMEOUT_MS      50
#define QMI8658_WHO_AM_I_ID     0x05

#define REG_WHO_AM_I            0x00
#define REG_CTRL1               0x02
#define REG_CTRL2               0x03
#define REG_CTRL3               0x04
#define REG_CTRL5               0x06
#define REG_CTRL7               0x08
#define REG_TEMP_L              0x33    // Temperature, then accel and gyro X, Y, Z: 14 bytes

#define CTRL1_AUTO_INCREMENT    0x40
#define CTRL2_ACCEL_4G_250HZ    0x15
#define CTRL3_GYRO_512DPS_250HZ 0x55
/* Low-pass filters on both at 3.63 % of the output rate, about 9 Hz */
#define CTRL5_LPF               0x33
#define CTRL7_ACCEL_GYRO        0x03
#define ACCEL_LSB_PER_G         8192.0f
#define GYRO_LSB_PER_DPS        64.0f

#define SAMPLE_PERIOD_MS        5
#define TASK_STACK              4096
#define TASK_PRIORITY           5
/* The gyro offset, and what 1 g reads, are measured over the first second the board rests: the gyro
 * then barely varies, whatever its offset. Later the offset follows slowly while it lies still. */
#define BIAS_START_SAMPLES      200
#define BIAS_STILL_SPREAD_DPS   1.0f
#define BIAS_TRACK_DPS          1.5f
#define BIAS_TRACK_ALPHA        0.002f
/* Mahony filter gains; the accel only corrects while it feels about 1 g, not while shaken */
#define FUSION_KP               1.5f
#define FUSION_KI               0.02f
#define FUSION_ACCEL_TOLERANCE  0.25f

#define DEG_TO_RAD              (3.14159265f / 180.0f)

static const char *TAG = "imu";

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_task_done;
static volatile bool s_stop;
static int s_users;

static portMUX_TYPE s_sample_lock = portMUX_INITIALIZER_UNLOCKED;
static imu_sample_t s_sample;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_dev, data, sizeof(data), QMI8658_TIMEOUT_MS);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, QMI8658_TIMEOUT_MS);
}

static esp_err_t open_device(void)
{
    if (s_dev) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = board_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not ready");
    uint16_t addr = QMI8658_ADDR;
    if (i2c_master_probe(bus, addr, QMI8658_TIMEOUT_MS) != ESP_OK) {
        addr = QMI8658_ADDR_ALT;
        ESP_RETURN_ON_ERROR(i2c_master_probe(bus, addr, QMI8658_TIMEOUT_MS), TAG, "QMI8658 not found");
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = QMI8658_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_dev), TAG, "Add QMI8658 failed");
    uint8_t id = 0;
    esp_err_t ret = read_regs(REG_WHO_AM_I, &id, 1);
    if (ret == ESP_OK && id != QMI8658_WHO_AM_I_ID) {
        ret = ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 at 0x%02x answers id 0x%02x", addr, id);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "QMI8658 at 0x%02x", addr);
    return ESP_OK;
}

static esp_err_t configure(bool on)
{
    if (!on) {
        return write_reg(REG_CTRL7, 0);
    }
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL1, CTRL1_AUTO_INCREMENT), TAG, "CTRL1");
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL2, CTRL2_ACCEL_4G_250HZ), TAG, "CTRL2");
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL3, CTRL3_GYRO_512DPS_250HZ), TAG, "CTRL3");
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL5, CTRL5_LPF), TAG, "CTRL5");
    return write_reg(REG_CTRL7, CTRL7_ACCEL_GYRO);
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)(p[0] | (p[1] << 8));
}

/* The sensor's axes as the screen's: x right, y up, z out of the screen. It sits turned half a turn
 * about the screen's y axis (measured: raising the right edge lowers its x, the board face up reads -1 g on z). */
static void to_screen(const float in[3], float out[3])
{
    out[0] = -in[0];
    out[1] = in[1];
    out[2] = -in[2];
}

static void normalize(float *v, int n)
{
    float sum = 0;
    for (int i = 0; i < n; i++) {
        sum += v[i] * v[i];
    }
    if (sum > 0) {
        const float inv = 1.0f / sqrtf(sum);
        for (int i = 0; i < n; i++) {
            v[i] *= inv;
        }
    }
}

/* Up in the sensor frame as the orientation q sees it */
static void up_from_quat(const float q[4], float up[3])
{
    up[0] = 2.0f * (q[1] * q[3] - q[0] * q[2]);
    up[1] = 2.0f * (q[0] * q[1] + q[2] * q[3]);
    up[2] = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];
}

/* The orientation with no turn about up that puts up where the accel feels it */
static void quat_from_up(const float a[3], float q[4])
{
    const float roll = atan2f(a[1], a[2]);
    const float pitch = atan2f(-a[0], sqrtf(a[1] * a[1] + a[2] * a[2]));
    const float cr = cosf(roll / 2), sr = sinf(roll / 2), cp = cosf(pitch / 2), sp = sinf(pitch / 2);
    q[0] = cr * cp;
    q[1] = sr * cp;
    q[2] = cr * sp;
    q[3] = -sr * sp;
}

/* One step of the Mahony filter: the gyro turns q, the accel pulls its up back towards gravity */
static void fuse(float q[4], float integral[3], const float accel[3], const float gyro_dps[3], float dt)
{
    float g[3] = {gyro_dps[0] * DEG_TO_RAD, gyro_dps[1] * DEG_TO_RAD, gyro_dps[2] * DEG_TO_RAD};
    const float norm = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (norm > 0 && fabsf(norm - 1.0f) < FUSION_ACCEL_TOLERANCE) {
        const float a[3] = {accel[0] / norm, accel[1] / norm, accel[2] / norm};
        float v[3];
        up_from_quat(q, v);
        const float e[3] = {a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]};
        for (int i = 0; i < 3; i++) {
            integral[i] += FUSION_KI * e[i] * dt;
            g[i] += FUSION_KP * e[i] + integral[i];
        }
    }
    const float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
    const float h = 0.5f * dt;
    q[0] += h * (-q1 * g[0] - q2 * g[1] - q3 * g[2]);
    q[1] += h * (q0 * g[0] + q2 * g[2] - q3 * g[1]);
    q[2] += h * (q0 * g[1] - q1 * g[2] + q3 * g[0]);
    q[3] += h * (q0 * g[2] + q1 * g[1] - q2 * g[0]);
    normalize(q, 4);
}

static void imu_task(void *arg)
{
    (void)arg;
    float q[4] = {1, 0, 0, 0};
    float integral[3] = {0};
    float bias[3] = {0};
    float bias_sum[3] = {0};
    float bias_sq[3] = {0};
    float g_sum = 0;
    float g_ref = 1.0f;     // What 1 g reads; this part reads a few percent high
    int bias_samples = 0;
    bool bias_measuring = true;
    bool have_attitude = false;
    uint32_t count = 0;
    int64_t last_us = esp_timer_get_time();
    TickType_t wake = xTaskGetTickCount();

    while (!s_stop) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        uint8_t raw[14];
        if (read_regs(REG_TEMP_L, raw, sizeof(raw)) != ESP_OK) {
            continue;
        }
        const int64_t now = esp_timer_get_time();
        const float dt = (now - last_us) / 1e6f;
        last_us = now;

        const float accel_raw[3] = {le16(&raw[2]) / ACCEL_LSB_PER_G, le16(&raw[4]) / ACCEL_LSB_PER_G,
                                    le16(&raw[6]) / ACCEL_LSB_PER_G};
        const float gyro_raw[3] = {le16(&raw[8]) / GYRO_LSB_PER_DPS, le16(&raw[10]) / GYRO_LSB_PER_DPS,
                                   le16(&raw[12]) / GYRO_LSB_PER_DPS};
        float accel_g[3];
        float gyro_all[3];
        to_screen(accel_raw, accel_g);
        to_screen(gyro_raw, gyro_all);

        float accel_norm = sqrtf(accel_g[0] * accel_g[0] + accel_g[1] * accel_g[1] + accel_g[2] * accel_g[2]);
        if (bias_measuring) {
            for (int i = 0; i < 3; i++) {
                bias_sum[i] += gyro_all[i];
                bias_sq[i] += gyro_all[i] * gyro_all[i];
            }
            g_sum += accel_norm;
            if (++bias_samples >= BIAS_START_SAMPLES) {
                bool still = true;
                for (int i = 0; i < 3; i++) {
                    const float mean = bias_sum[i] / bias_samples;
                    still = still && bias_sq[i] / bias_samples - mean * mean < BIAS_STILL_SPREAD_DPS * BIAS_STILL_SPREAD_DPS;
                }
                if (still) {
                    for (int i = 0; i < 3; i++) {
                        bias[i] = bias_sum[i] / bias_samples;
                    }
                    g_ref = g_sum / bias_samples;
                    bias_measuring = false;
                    ESP_LOGI(TAG, "Gyro offset %.2f %.2f %.2f dps, 1 g reads %.3f", bias[0], bias[1], bias[2], g_ref);
                }
                // Moved while measuring: start over, and fuse without an offset meanwhile
                bias_samples = 0;
                g_sum = 0;
                memset(bias_sum, 0, sizeof(bias_sum));
                memset(bias_sq, 0, sizeof(bias_sq));
            }
        }
        const float accel[3] = {accel_g[0] / g_ref, accel_g[1] / g_ref, accel_g[2] / g_ref};
        accel_norm /= g_ref;
        float gyro[3] = {gyro_all[0] - bias[0], gyro_all[1] - bias[1], gyro_all[2] - bias[2]};
        if (!bias_measuring && fabsf(accel_norm - 1.0f) < 0.05f && fabsf(gyro[0]) < BIAS_TRACK_DPS &&
                fabsf(gyro[1]) < BIAS_TRACK_DPS && fabsf(gyro[2]) < BIAS_TRACK_DPS) {
            // Lying still: let the offset follow temperature drift
            for (int i = 0; i < 3; i++) {
                bias[i] += BIAS_TRACK_ALPHA * gyro[i];
            }
        }

        if (!have_attitude && accel_norm > 0.5f) {
            quat_from_up(accel, q);
            have_attitude = true;
        } else if (have_attitude && dt > 0 && dt < 0.1f) {
            fuse(q, integral, accel, gyro, dt);
        }
        float up[3];
        up_from_quat(q, up);

        taskENTER_CRITICAL(&s_sample_lock);
        memcpy(s_sample.accel, accel, sizeof(accel));
        memcpy(s_sample.gyro, gyro, sizeof(gyro));
        memcpy(s_sample.up, up, sizeof(up));
        memcpy(s_sample.quat, q, sizeof(q));
        s_sample.temperature = le16(&raw[0]) / 256.0f;
        s_sample.count = ++count;
        taskEXIT_CRITICAL(&s_sample_lock);
    }
    xSemaphoreGive(s_task_done);
    vTaskDelete(NULL);
}

esp_err_t imu_service_start(void)
{
    if (!s_lock) {
        s_task_done = xSemaphoreCreateBinary();
        SemaphoreHandle_t lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(lock && s_task_done, ESP_ERR_NO_MEM, TAG, "No memory");
        s_lock = lock;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t ret = ESP_OK;
    if (s_users == 0) {
        ret = open_device();
        if (ret == ESP_OK) {
            ret = configure(true);
        }
        if (ret == ESP_OK) {
            taskENTER_CRITICAL(&s_sample_lock);
            s_sample.count = 0;
            taskEXIT_CRITICAL(&s_sample_lock);
            s_stop = false;
            if (xTaskCreate(imu_task, "imu", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
                s_task = NULL;
                configure(false);
                ret = ESP_ERR_NO_MEM;
            }
        }
    }
    if (ret == ESP_OK) {
        s_users++;
    }
    xSemaphoreGive(s_lock);
    return ret;
}

void imu_service_stop(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_users > 0 && --s_users == 0 && s_task) {
        s_stop = true;
        // The task reads the bus at most once more before it leaves
        xSemaphoreTake(s_task_done, portMAX_DELAY);
        s_task = NULL;
        configure(false);
    }
    xSemaphoreGive(s_lock);
}

esp_err_t imu_service_get(imu_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample, ESP_ERR_INVALID_ARG, TAG, "Invalid sample");
    taskENTER_CRITICAL(&s_sample_lock);
    *sample = s_sample;
    taskEXIT_CRITICAL(&s_sample_lock);
    return sample->count ? ESP_OK : ESP_ERR_INVALID_STATE;
}
