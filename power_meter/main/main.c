/*
 * BLE Cycling Power Meter - Main Application
 *
 * Implements a BLE peripheral that advertises as a Cycling Power Sensor
 * and reports power data to connected devices (e.g., Garmin watches).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "ble_power_service.h"
#include "gap.h"
#include "wifi_log_server.h"

/* 0 = sine wave demo, 1 = IMU-based stroke detection */
#define USE_IMU_POWER 1

#if USE_IMU_POWER
#include "driver/i2c.h"
#include "imu_power.h"
#include "mpu6050.h"
#include "stroke_detector.h"
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_PORT I2C_NUM_0
#define I2C_FREQ_HZ 400000
#define IMU_SAMPLE_MS 50 /* 20 Hz IMU sampling */
#endif

/* BLE notification rate */
#define POWER_UPDATE_RATE_HZ 10
#define POWER_UPDATE_PERIOD_MS (1000 / POWER_UPDATE_RATE_HZ)

/* Sine wave demo config */
#define POWER_BASE_WATTS 200
#define POWER_AMPLITUDE_WATTS 50
#define POWER_CYCLE_SECONDS 10

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

static void nimble_host_task(void* param) {
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run();
  vTaskDelete(NULL);
}

#if USE_IMU_POWER

/* ── Inter-task message types ────────────────────────────────────────────── */

typedef enum {
  SETTING_MASS,
  SETTING_FORWARD_AXIS,
  SETTING_CATCH_G,
  SETTING_RECOVERY_G,
  SETTING_VERBOSE,
  SETTING_CALIBRATE,
  SETTING_SMOOTH_STROKES,
} settings_type_t;

typedef struct {
  settings_type_t type;
  union {
    float f;     /* MASS, CATCH_G, RECOVERY_G */
    bool b;      /* VERBOSE */
    int i;       /* SMOOTH_STROKES */
    float v3[3]; /* FORWARD_AXIS */
  };
} settings_msg_t;

typedef struct {
  float power_w;
  float stroke_rate_spm;
  uint32_t stroke_count;
} power_reading_t;

static QueueHandle_t s_settings_queue;
static QueueHandle_t s_power_queue;

static void enqueue_setting(const settings_msg_t* msg) {
  if (xQueueSend(s_settings_queue, msg, pdMS_TO_TICKS(10)) != pdTRUE) {
    ESP_LOGW(TAG, "Settings queue full — message dropped");
  }
}

static void on_ws_command(const char* cmd) {
  settings_msg_t msg = {0};

  if (strcmp(cmd, "recalibrate") == 0) {
    msg.type = SETTING_CALIBRATE;
    enqueue_setting(&msg);
    ESP_LOGI(TAG, "Recalibration requested via browser");

  } else if (strcmp(cmd, "verbose:on") == 0) {
    msg.type = SETTING_VERBOSE;
    msg.b = true;
    enqueue_setting(&msg);
    wifi_log_server_set_status("!verbose:on");
    ESP_LOGI(TAG, "Verbose IMU logging ON");

  } else if (strcmp(cmd, "verbose:off") == 0) {
    msg.type = SETTING_VERBOSE;
    msg.b = false;
    enqueue_setting(&msg);
    wifi_log_server_set_status("!verbose:off");
    ESP_LOGI(TAG, "Verbose IMU logging OFF");

  } else if (strncmp(cmd, "set:mass:", 9) == 0) {
    float kg;
    if (sscanf(cmd + 9, "%f", &kg) == 1) {
      if (kg < 10.0f || kg > 500.0f) {
        ESP_LOGW(TAG, "Mass %.1f kg out of range [10, 500], clamped", kg);
        kg = kg < 10.0f ? 10.0f : 500.0f;
      }
      msg.type = SETTING_MASS;
      msg.f = kg;
      enqueue_setting(&msg);
      ESP_LOGI(TAG, "Mass set to %.1f kg", kg);
    }

  } else if (strncmp(cmd, "set:axis:", 9) == 0) {
    const char* ax = cmd + 9;
    msg.type = SETTING_FORWARD_AXIS;
    if (strcmp(ax, "+X") == 0)
      msg.v3[0] = 1.0f;
    else if (strcmp(ax, "-X") == 0)
      msg.v3[0] = -1.0f;
    else if (strcmp(ax, "+Y") == 0)
      msg.v3[1] = 1.0f;
    else if (strcmp(ax, "-Y") == 0)
      msg.v3[1] = -1.0f;
    else if (strcmp(ax, "+Z") == 0)
      msg.v3[2] = 1.0f;
    else if (strcmp(ax, "-Z") == 0)
      msg.v3[2] = -1.0f;
    else {
      ESP_LOGW(TAG, "Unknown axis: %s", ax);
      return;
    }
    enqueue_setting(&msg);
    ESP_LOGI(TAG, "Forward axis set to %s", ax);

  } else if (strncmp(cmd, "set:catch:", 10) == 0) {
    float g;
    if (sscanf(cmd + 10, "%f", &g) == 1) {
      if (g < 0.05f || g > 5.0f) {
        ESP_LOGW(TAG, "Catch threshold %.3f g out of range [0.05, 5.0], clamped", g);
        g = g < 0.05f ? 0.05f : 5.0f;
      }
      msg.type = SETTING_CATCH_G;
      msg.f = g;
      enqueue_setting(&msg);
      ESP_LOGI(TAG, "Catch threshold set to %.3f g", g);
    }

  } else if (strncmp(cmd, "set:recovery:", 13) == 0) {
    float g;
    if (sscanf(cmd + 13, "%f", &g) == 1) {
      if (g < 0.05f || g > 5.0f) {
        ESP_LOGW(TAG, "Recovery threshold %.3f g out of range [0.05, 5.0], clamped", g);
        g = g < 0.05f ? 0.05f : 5.0f;
      }
      msg.type = SETTING_RECOVERY_G;
      msg.f = g;
      enqueue_setting(&msg);
      ESP_LOGI(TAG, "Recovery threshold set to %.3f g", g);
    }

  } else if (strncmp(cmd, "set:smooth:", 11) == 0) {
    int n;
    if (sscanf(cmd + 11, "%d", &n) == 1) {
      if (n < 1)
        n = 1;
      if (n > STROKE_RATE_MAX_SMOOTH)
        n = STROKE_RATE_MAX_SMOOTH;
      msg.type = SETTING_SMOOTH_STROKES;
      msg.i = n;
      enqueue_setting(&msg);
      ESP_LOGI(TAG, "Stroke rate smoothing window set to %d strokes", n);
    }
  }
}

static void ble_notify_task(void* param) {
  power_reading_t reading = {0};
  ESP_LOGI(TAG, "BLE notify task running (1 Hz keepalive)");
  while (1) {
    /* Block up to 1s for a new stroke reading; on timeout send last known value */
    xQueueReceive(s_power_queue, &reading, pdMS_TO_TICKS(1000));
    send_power_notification((int16_t)reading.power_w);
  }
  vTaskDelete(NULL);
}

static void power_update_task(void* param) {
  mpu6050_handle_t mpu = (mpu6050_handle_t)param;
  stroke_state_t stroke = {0};
  imu_calibration_t cal = {0};
  imu_power_state_t power = {0};

  stroke_detector_init(&stroke);
  imu_power_init(&power);
  imu_calibrate(&cal, mpu);

  int64_t last_sample_us = esp_timer_get_time();
  ESP_LOGI(TAG, "IMU power task running at %d Hz", 1000 / IMU_SAMPLE_MS);

  while (1) {
    /* Drain settings queue before processing the next sample */
    settings_msg_t msg;
    while (xQueueReceive(s_settings_queue, &msg, 0) == pdTRUE) {
      switch (msg.type) {
        case SETTING_MASS:
          power.mass_kg = msg.f;
          break;
        case SETTING_FORWARD_AXIS:
          power.forward[0] = msg.v3[0];
          power.forward[1] = msg.v3[1];
          power.forward[2] = msg.v3[2];
          break;
        case SETTING_CATCH_G:
          stroke.catch_g = msg.f;
          break;
        case SETTING_RECOVERY_G:
          stroke.recovery_g = msg.f;
          break;
        case SETTING_VERBOSE:
          power.verbose = msg.b;
          break;
        case SETTING_CALIBRATE:
          ESP_LOGI(TAG, "Recalibrating: hold device still...");
          imu_calibrate(&cal, mpu);
          break;
        case SETTING_SMOOTH_STROKES:
          stroke.smooth_strokes = msg.i;
          break;
      }
    }

    mpu6050_acce_value_t acce;
    if (mpu6050_get_acce(mpu, &acce) == ESP_OK) {
      int64_t now = esp_timer_get_time();
      float dt_s = (now - last_sample_us) / 1e6f;
      last_sample_us = now;

      float mag =
          sqrtf(acce.acce_x * acce.acce_x + acce.acce_y * acce.acce_y + acce.acce_z * acce.acce_z);
      float dynamic_g = fabsf(mag - 1.0f);

      int stroke_done = stroke_detector_update(&stroke, dynamic_g, now);
      if (stroke_done) {
        power_service_update_crank(now);
      }

      float out_w = 0.0f;
      imu_power_update(&power, &cal, &acce, stroke.phase, dt_s, &out_w);

      if (stroke_done) {
        power_reading_t reading = {
            .power_w = power.avg_stroke_power_w,
            .stroke_rate_spm = stroke.stroke_rate_spm,
            .stroke_count = (uint32_t)stroke.stroke_count,
        };
        xQueueOverwrite(s_power_queue, &reading);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_MS));
  }

  vTaskDelete(NULL);
}

#else

static void power_update_task(void* param) {
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

  wifi_log_server_start("PowerMeter", "");

#if USE_IMU_POWER
  s_settings_queue = xQueueCreate(8, sizeof(settings_msg_t));
  s_power_queue = xQueueCreate(1, sizeof(power_reading_t));
  wifi_log_server_set_command_cb(on_ws_command);
  /* Initialize I2C and MPU6050 */
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_FREQ_HZ,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

  mpu6050_handle_t mpu = mpu6050_create(I2C_PORT, MPU6050_I2C_ADDRESS);
  if (mpu == NULL) {
    ESP_LOGE(TAG, "mpu6050_create failed: check I2C wiring");
    return;
  }
  ESP_ERROR_CHECK(mpu6050_wake_up(mpu));
  ESP_ERROR_CHECK(mpu6050_config(mpu, ACCE_FS_4G, GYRO_FS_500DPS));

  /* Configure MPU6050 digital low-pass filter (DLPF) to 10 Hz cutoff.
   * Register 0x1A (CONFIG), bits 2:0 = 5 gives 10 Hz accel/gyro bandwidth.
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
  i2c_master_write_byte(cmd, 0x1A, true); /* CONFIG register */
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
  if (rc != 0) {
    ESP_LOGE(TAG, "gap_init failed: %d", rc);
    return;
  }

  rc = power_service_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "power_service_init failed: %d", rc);
    return;
  }

  nimble_host_config_init();

  xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

#if USE_IMU_POWER
  xTaskCreate(power_update_task, "Power Update", 4 * 1024, mpu, 5, NULL);
  xTaskCreate(ble_notify_task, "BLE Notify", 4 * 1024, NULL, 5, NULL);
#else
  xTaskCreate(power_update_task, "Power Update", 2 * 1024, NULL, 5, NULL);
#endif

  ESP_LOGI(TAG, "BLE Cycling Power Meter running");
}
