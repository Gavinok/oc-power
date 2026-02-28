#pragma once

#include <stdbool.h>
#include "mpu6050.h"
#include "stroke_detector.h"

/* Total moving mass: paddler + boat + gear (kg) */
#define TOTAL_MASS_KG 100.0f

/* Forward direction unit vector — change if sensor is remounted differently.
 * Default: positive Y axis points toward bow of boat. */
#define FORWARD_AXIS_X 0.0f
#define FORWARD_AXIS_Y 1.0f
#define FORWARD_AXIS_Z 0.0f

/* Number of samples to average during gravity calibration (~2 seconds at 20Hz) */
#define CALIBRATION_SAMPLES 40

typedef struct {
    /* Gravity vector captured at calibration (magnitude ~1g, points down) */
    float gravity[3];
    bool  calibrated;
} imu_calibration_t;

typedef struct {
    float mass_kg;

    /* Drag force estimate, smoothed during recovery (N).
     * Reserved for future GPS-fusion work; not used in power calculation yet. */
    float drag_force_n;

    /* Per-stroke velocity change: integral of a_forward over CATCH+PULL only.
     * Reset at RECOVERY→CATCH transition. Drift bounded to one stroke (~1s). */
    float stroke_delta_v_ms;

    /* Elapsed time within the active stroke (s), reset with delta_v. */
    float stroke_dt_s;

    /* Previous phase — used to detect phase transitions, not phase values. */
    stroke_phase_t prev_phase;

    /* Power of last completed stroke (W).
     * P = 0.5 * mass * delta_v^2 / stroke_dt
     * Updated once per stroke at PULL→RELEASE transition. */
    float avg_stroke_power_w;
} imu_power_state_t;

/* Run stationary gravity calibration. Hold device still for CALIBRATION_SAMPLES.
 * Logs progress to serial. Must complete before imu_power_update() is called. */
void imu_calibrate(imu_calibration_t *cal, mpu6050_handle_t mpu);

void imu_power_init(imu_power_state_t *state, float mass_kg);

/* Feed one accelerometer sample into the power estimator.
 *   acce        — raw reading from mpu6050_get_acce()
 *   stroke_phase — current phase from stroke_detector
 *   dt_s        — seconds since last call
 *   out_power_w — set to instantaneous power (W), only valid during stroke phase
 */
void imu_power_update(imu_power_state_t *state,
                      const imu_calibration_t *cal,
                      const mpu6050_acce_value_t *acce,
                      stroke_phase_t stroke_phase,
                      float dt_s,
                      float *out_power_w);
