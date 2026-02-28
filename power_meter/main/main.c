/*
 * BLE Cycling Power Meter - Main Application
 *
 * Implements a BLE peripheral that advertises as a Cycling Power Sensor
 * and reports power data to connected devices (e.g., Garmin watches).
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

#include "gap.h"
#include "ble_power_service.h"

/* 0 = sine wave demo, 1 = IMU-based stroke detection */
#define USE_IMU_POWER 1

#if USE_IMU_POWER
#include "driver/i2c.h"
#include "mpu6050.h"
#include "stroke_detector.h"
#include "imu_power.h"
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_PORT        I2C_NUM_0
#define I2C_FREQ_HZ     400000
#define IMU_SAMPLE_MS   50   /* 20 Hz IMU sampling */
#endif

/* BLE notification rate */
#define POWER_UPDATE_RATE_HZ    10
#define POWER_UPDATE_PERIOD_MS  (1000 / POWER_UPDATE_RATE_HZ)

/* Sine wave demo config */
#define POWER_BASE_WATTS        200
#define POWER_AMPLITUDE_WATTS   50
#define POWER_CYCLE_SECONDS     10

#define TAG "POWER_METER"

/* Library function declarations */
void ble_store_config_init(void);

static void on_stack_reset(int reason) {
    ESP_LOGI(TAG, "NimBLE stack reset, reason: %d", reason);
}

static void on_stack_sync(void) {
    ESP_LOGI(TAG, "NimBLE stack synced, starting advertising");
    adv_init();
}

static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    vTaskDelete(NULL);
}

#if USE_IMU_POWER

static void power_update_task(void *param) {
    mpu6050_handle_t mpu = (mpu6050_handle_t)param;
    stroke_state_t stroke = {0};
    imu_calibration_t cal = {0};
    imu_power_state_t power = {0};

    stroke_detector_init(&stroke);
    imu_power_init(&power, TOTAL_MASS_KG);

    /* Run gravity calibration — device must be still for ~2 seconds */
    imu_calibrate(&cal, mpu);

    int ble_notify_counter = 0;
    const int ble_notify_every = POWER_UPDATE_PERIOD_MS / IMU_SAMPLE_MS;
    int64_t last_sample_us = esp_timer_get_time();
    float last_power_w = 0.0f;

    ESP_LOGI(TAG, "IMU power task running (20Hz IMU, 10Hz BLE)");

    while (1) {
        mpu6050_acce_value_t acce;
        if (mpu6050_get_acce(mpu, &acce) == ESP_OK) {
            int64_t now = esp_timer_get_time();
            float dt_s = (now - last_sample_us) / 1e6f;
            last_sample_us = now;

            /* Compute acceleration magnitude for stroke detection */
            float mag = sqrtf(acce.acce_x * acce.acce_x +
                              acce.acce_y * acce.acce_y +
                              acce.acce_z * acce.acce_z);
            float dynamic_g = fabsf(mag - 1.0f);

            /* Update stroke detector */
            int stroke_done = stroke_detector_update(&stroke, dynamic_g, now);
            if (stroke_done) {
                /* Real stroke completed — update cadence on the watch */
                power_service_update_crank(now);
            }

            /* Update power estimator with signed forward acceleration */
            float inst_power_w = 0.0f;
            imu_power_update(&power, &cal, &acce, stroke.phase, dt_s, &inst_power_w);

            /* Hold the last non-zero power reading to send over BLE */
            if (inst_power_w > 0.0f) {
                last_power_w = inst_power_w;
            }
        }

        /* Send BLE notification at 10 Hz */
        if (++ble_notify_counter >= ble_notify_every) {
            ble_notify_counter = 0;
            /* Use stroke average power between strokes for a stable reading */
            float send_watts = (stroke.phase == STROKE_PHASE_PULL)
                               ? last_power_w
                               : power.avg_stroke_power_w;
            send_power_notification((int16_t)send_watts);
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_MS));
    }

    vTaskDelete(NULL);
}

#else

static void power_update_task(void *param) {
    ESP_LOGI(TAG, "Sine wave demo started (%d Hz)", POWER_UPDATE_RATE_HZ);

    while (1) {
        double time_sec = esp_timer_get_time() / 1000000.0;
        double angle = (2.0 * M_PI * time_sec) / POWER_CYCLE_SECONDS;
        int16_t power = POWER_BASE_WATTS + (int16_t)(POWER_AMPLITUDE_WATTS * sin(angle));
        send_power_notification(power);
        vTaskDelay(pdMS_TO_TICKS(POWER_UPDATE_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

#endif /* USE_IMU_POWER */

void app_main(void) {
    esp_err_t ret;
    int rc = 0;

    ESP_LOGI(TAG, "Initializing BLE Cycling Power Meter (USE_IMU_POWER=%d)", USE_IMU_POWER);

    /* Initialize NVS flash (required by BLE stack) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if USE_IMU_POWER
    /* Initialize I2C and MPU6050 */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_PIN,
        .scl_io_num       = I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    mpu6050_handle_t mpu = mpu6050_create(I2C_PORT, MPU6050_I2C_ADDRESS);
    if (mpu == NULL) {
        ESP_LOGE(TAG, "mpu6050_create failed — check I2C wiring");
        return;
    }
    ESP_ERROR_CHECK(mpu6050_wake_up(mpu));
    ESP_ERROR_CHECK(mpu6050_config(mpu, ACCE_FS_4G, GYRO_FS_500DPS));

    /* Configure MPU6050 digital low-pass filter (DLPF) to 10 Hz cutoff.
     * Register 0x1A (CONFIG), bits 2:0 = 5 → 10 Hz accel/gyro bandwidth.
     *
     * At our 20 Hz sampling rate, a 10 Hz cutoff means the filter "memory"
     * spans ~1/10Hz = 100ms = 2 samples. A spike must persist for ~2
     * consecutive samples before it passes through fully. Real paddle strokes
     * build over 500ms+ so they pass cleanly; single-sample vibration spikes
     * get attenuated. Stroke dynamics (~1 Hz) are well below the cutoff. */
    uint8_t dlpf_cfg = 0x05;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x1A, true);  /* CONFIG register */
    i2c_master_write_byte(cmd, dlpf_cfg, true);
    i2c_master_stop(cmd);
    esp_err_t dlpf_ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (dlpf_ret == ESP_OK) {
        ESP_LOGI(TAG, "MPU6050 DLPF set to 10 Hz");
    } else {
        ESP_LOGW(TAG, "MPU6050 DLPF config failed (non-critical)");
    }

    ESP_LOGI(TAG, "MPU6050 initialized");
#endif

    /* Initialize NimBLE stack */
    ret = nimble_port_init();
    ESP_ERROR_CHECK(ret);

    rc = gap_init();
    if (rc != 0) { ESP_LOGE(TAG, "gap_init failed: %d", rc); return; }

    rc = power_service_init();
    if (rc != 0) { ESP_LOGE(TAG, "power_service_init failed: %d", rc); return; }

    nimble_host_config_init();

    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

#if USE_IMU_POWER
    xTaskCreate(power_update_task, "Power Update", 4 * 1024, mpu, 5, NULL);
#else
    xTaskCreate(power_update_task, "Power Update", 2 * 1024, NULL, 5, NULL);
#endif

    ESP_LOGI(TAG, "BLE Cycling Power Meter running");
}
