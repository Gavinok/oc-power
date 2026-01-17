/*
 * BLE Cycling Power Service (0x1818) implementation
 *
 * Implements the standard Bluetooth Cycling Power Service with:
 * - Cycling Power Measurement (0x2A63) - Notify
 * - Cycling Power Feature (0x2A65) - Read
 * - Sensor Location (0x2A5D) - Read
 */

#include "ble_power_service.h"

#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

#define TAG "POWER_SVC"

/* Private function declarations */
static int power_measurement_access(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg);
static int power_feature_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);
static int sensor_location_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Service and Characteristic UUIDs */
static const ble_uuid16_t cycling_power_svc_uuid =
    BLE_UUID16_INIT(CYCLING_POWER_SVC_UUID);
static const ble_uuid16_t power_measurement_chr_uuid =
    BLE_UUID16_INIT(CYCLING_POWER_MEASUREMENT_UUID);
static const ble_uuid16_t power_feature_chr_uuid =
    BLE_UUID16_INIT(CYCLING_POWER_FEATURE_UUID);
static const ble_uuid16_t sensor_location_chr_uuid =
    BLE_UUID16_INIT(SENSOR_LOCATION_UUID);

/* Characteristic value handles */
static uint16_t power_measurement_val_handle;
static uint16_t power_feature_val_handle;
static uint16_t sensor_location_val_handle;

/* Connection and subscription state */
static uint16_t power_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool power_notify_enabled = false;

/* Cycling Power Feature value:
 * Bit 3 (0x08) = Crank Revolution Data Supported
 */
static const uint8_t power_feature_value[4] = {0x08, 0x00, 0x00, 0x00};

/* Sensor Location: Left Crank (0x0D) */
static const uint8_t sensor_location_value = SENSOR_LOCATION_LEFT_CRANK;

/* Crank revolution tracking */
static uint16_t cumulative_crank_revs = 0;
static uint16_t last_crank_event_time = 0;

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* Cycling Power Service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &cycling_power_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            /* Cycling Power Measurement Characteristic */
            {
                .uuid = &power_measurement_chr_uuid.u,
                .access_cb = power_measurement_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &power_measurement_val_handle
            },
            /* Cycling Power Feature Characteristic */
            {
                .uuid = &power_feature_chr_uuid.u,
                .access_cb = power_feature_access,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &power_feature_val_handle
            },
            /* Sensor Location Characteristic */
            {
                .uuid = &sensor_location_chr_uuid.u,
                .access_cb = sensor_location_access,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &sensor_location_val_handle
            },
            { 0 } /* No more characteristics */
        }
    },
    { 0 } /* No more services */
};

/* Characteristic access callbacks */
static int power_measurement_access(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Power measurement is notify-only, no read/write access needed */
    return BLE_ATT_ERR_UNLIKELY;
}

static int power_feature_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    int rc;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ESP_LOGI(TAG, "power feature read; conn_handle=%d", conn_handle);
        rc = os_mbuf_append(ctxt->om, power_feature_value,
                           sizeof(power_feature_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int sensor_location_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    int rc;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ESP_LOGI(TAG, "sensor location read; conn_handle=%d", conn_handle);
        rc = os_mbuf_append(ctxt->om, &sensor_location_value,
                           sizeof(sensor_location_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* Public functions */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "registered characteristic %s with def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registered descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    default:
        break;
    }
}

void power_service_set_conn_handle(uint16_t conn_handle) {
    power_conn_handle = conn_handle;
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        power_notify_enabled = false;
    }
}

void power_service_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.attr_handle == power_measurement_val_handle) {
        power_notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "power measurement notifications %s",
                 power_notify_enabled ? "enabled" : "disabled");
    }
}

void send_power_notification(int16_t power_watts) {
    if (!power_notify_enabled || power_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    cycling_power_measurement_t measurement = {0};
    struct os_mbuf *om;
    int rc;

    /* Update crank revolution data (simulate 80 RPM = ~1.33 revs/sec at 4Hz update) */
    cumulative_crank_revs += 1;  /* Simplified: 1 rev per update */
    last_crank_event_time += 256; /* 1/4 second in 1/1024 sec units */

    /* Build measurement packet */
    measurement.flags = CPM_FLAG_CRANK_REV_DATA_PRESENT;
    measurement.instantaneous_power = power_watts;
    measurement.cumulative_crank_revs = cumulative_crank_revs;
    measurement.last_crank_event_time = last_crank_event_time;

    /* Allocate mbuf for notification */
    om = ble_hs_mbuf_from_flat(&measurement, sizeof(measurement));
    if (om == NULL) {
        ESP_LOGE(TAG, "failed to allocate mbuf for notification");
        return;
    }

    /* Send notification */
    rc = ble_gatts_notify_custom(power_conn_handle,
                                  power_measurement_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to send notification, error code: %d", rc);
    } else {
        ESP_LOGD(TAG, "sent power: %d W, revs: %d, time: %d",
                 power_watts, cumulative_crank_revs, last_crank_event_time);
    }
}

int power_service_init(void) {
    int rc = 0;

    /* Initialize GATT service */
    ble_svc_gatt_init();

    /* Count GATT services */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to count GATT services, error code: %d", rc);
        return rc;
    }

    /* Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to add GATT services, error code: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "Cycling Power Service initialized");
    return 0;
}
