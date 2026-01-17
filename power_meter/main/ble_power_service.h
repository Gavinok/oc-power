#ifndef BLE_POWER_SERVICE_H
#define BLE_POWER_SERVICE_H

#include <stdint.h>
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Cycling Power Service UUIDs */
#define CYCLING_POWER_SVC_UUID          0x1818
#define CYCLING_POWER_MEASUREMENT_UUID  0x2A63
#define CYCLING_POWER_FEATURE_UUID      0x2A65
#define SENSOR_LOCATION_UUID            0x2A5D

/* Cycling Power Measurement Flags */
#define CPM_FLAG_CRANK_REV_DATA_PRESENT 0x0020

/* Sensor Location Values */
#define SENSOR_LOCATION_LEFT_CRANK      0x0D

/* Power Measurement Data Structure (8 bytes) */
typedef struct {
    uint16_t flags;                    /* Feature flags */
    int16_t  instantaneous_power;      /* Watts */
    uint16_t cumulative_crank_revs;    /* Cumulative crank revolutions */
    uint16_t last_crank_event_time;    /* 1/1024 second resolution */
} __attribute__((packed)) cycling_power_measurement_t;

/* Public function declarations */
int power_service_init(void);
void power_service_set_conn_handle(uint16_t conn_handle);
void power_service_subscribe_cb(struct ble_gap_event *event);
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void send_power_notification(int16_t power_watts);

#endif // BLE_POWER_SERVICE_H
