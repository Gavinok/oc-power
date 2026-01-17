#ifndef GAP_H
#define GAP_H

/* NimBLE GAP APIs */
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

/* Defines */
#define DEVICE_NAME "ESP32 Power"

/* BLE Appearance for Cycling Power Sensor */
#define BLE_GAP_APPEARANCE_CYCLING_POWER 0x0483

/* Public function declarations */
void adv_init(void);
int gap_init(void);

#endif // GAP_H
