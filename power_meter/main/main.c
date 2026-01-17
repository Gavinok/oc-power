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

#define TAG "POWER_METER"

/* Configuration */
#define POWER_UPDATE_RATE_HZ    4       /* 4 Hz update rate (250ms) */
#define POWER_UPDATE_PERIOD_MS  (1000 / POWER_UPDATE_RATE_HZ)

/* Sine wave power simulation */
#define POWER_BASE_WATTS        200     /* Center power value */
#define POWER_AMPLITUDE_WATTS   50      /* +/- variation (150W to 250W) */
#define POWER_CYCLE_SECONDS     10      /* Full sine wave period */

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
static void power_update_task(void *param);

/* Stack callbacks */
static void on_stack_reset(int reason) {
    ESP_LOGI(TAG, "NimBLE stack reset, reason: %d", reason);
}

static void on_stack_sync(void) {
    ESP_LOGI(TAG, "NimBLE stack synced, starting advertising");
    adv_init();
}

static void nimble_host_config_init(void) {
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Store host configuration */
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");

    /* This function won't return until nimble_port_stop() is called */
    nimble_port_run();

    /* Clean up */
    vTaskDelete(NULL);
}

static void power_update_task(void *param) {
    ESP_LOGI(TAG, "Power update task started (%d Hz)", POWER_UPDATE_RATE_HZ);
    ESP_LOGI(TAG, "Power range: %d-%d W, cycle: %d sec",
             POWER_BASE_WATTS - POWER_AMPLITUDE_WATTS,
             POWER_BASE_WATTS + POWER_AMPLITUDE_WATTS,
             POWER_CYCLE_SECONDS);

    while (1) {
        /* Get current time in seconds */
        double time_sec = esp_timer_get_time() / 1000000.0;

        /* Calculate power using sine wave */
        double angle = (2.0 * M_PI * time_sec) / POWER_CYCLE_SECONDS;
        int16_t power = POWER_BASE_WATTS + (int16_t)(POWER_AMPLITUDE_WATTS * sin(angle));

        /* Send power notification if connected and subscribed */
        send_power_notification(power);

        /* Wait for next update period */
        vTaskDelay(pdMS_TO_TICKS(POWER_UPDATE_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

void app_main(void) {
    int rc = 0;
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing BLE Cycling Power Meter");

    /* Initialize NVS flash (required by BLE stack) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS flash, error: %d", ret);
        return;
    }

    /* Initialize NimBLE stack */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE stack, error: %d", ret);
        return;
    }

    /* Initialize GAP service */
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initialize GAP service, error: %d", rc);
        return;
    }

    /* Initialize Cycling Power Service */
    rc = power_service_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initialize Power Service, error: %d", rc);
        return;
    }

    /* Configure NimBLE host */
    nimble_host_config_init();

    /* Start NimBLE host task */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

    /* Start power update task */
    xTaskCreate(power_update_task, "Power Update", 2 * 1024, NULL, 5, NULL);

    ESP_LOGI(TAG, "BLE Cycling Power Meter initialized successfully");
}
