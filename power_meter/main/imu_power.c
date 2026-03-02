#include "imu_power.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "IMU_POWER"

/* Dot product of two 3D vectors */
static float dot3(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Normalize a 3D vector in place. Returns false if magnitude is near zero. */
static bool normalize3(float v[3]) {
  float mag = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (mag < 1e-6f)
    return false;
  v[0] /= mag;
  v[1] /= mag;
  v[2] /= mag;
  return true;
}

/* Acceleration stddev above which calibration is considered motion-corrupted */
#define CALIBRATION_STDDEV_WARN_G 0.05f

void imu_calibrate(imu_calibration_t* cal, mpu6050_handle_t mpu) {
  float sum[3] = {0, 0, 0};
  float sum_sq[3] = {0, 0, 0};
  int count = 0;

  ESP_LOGI(TAG, "Gravity calibration: hold still for 2 seconds...");

  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    mpu6050_acce_value_t acce;
    if (mpu6050_get_acce(mpu, &acce) == ESP_OK) {
      sum[0] += acce.acce_x;
      sum[1] += acce.acce_y;
      sum[2] += acce.acce_z;
      sum_sq[0] += acce.acce_x * acce.acce_x;
      sum_sq[1] += acce.acce_y * acce.acce_y;
      sum_sq[2] += acce.acce_z * acce.acce_z;
      count++;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); /* 20 Hz */
  }

  if (count == 0) {
    ESP_LOGE(TAG, "Calibration failed: no samples read");
    cal->calibrated = false;
    return;
  }

  /* Average then normalize to get a unit vector pointing "down" */
  cal->gravity[0] = sum[0] / count;
  cal->gravity[1] = sum[1] / count;
  cal->gravity[2] = sum[2] / count;

  /* Check for motion corruption: variance = E[x^2] - E[x]^2, summed over axes */
  float total_variance = 0.0f;
  for (int i = 0; i < 3; i++) {
    float mean = cal->gravity[i];
    total_variance += (sum_sq[i] / count) - (mean * mean);
  }
  float stddev = sqrtf(total_variance);
  if (stddev > CALIBRATION_STDDEV_WARN_G) {
    ESP_LOGW(TAG,
             "Calibration may be corrupted: stddev=%.3f g (threshold=%.2f g) — was device moving?",
             stddev, CALIBRATION_STDDEV_WARN_G);
  }

  ESP_LOGI(TAG, "Gravity vector before normalize: x=%.3f y=%.3f z=%.3f", cal->gravity[0],
           cal->gravity[1], cal->gravity[2]);

  if (!normalize3(cal->gravity)) {
    ESP_LOGE(TAG, "Calibration failed: gravity vector has zero magnitude");
    cal->calibrated = false;
    return;
  }

  cal->calibrated = true;
  ESP_LOGI(TAG, "Calibration done. Down unit vector: x=%.3f y=%.3f z=%.3f", cal->gravity[0],
           cal->gravity[1], cal->gravity[2]);
}

void imu_power_init(imu_power_state_t* state) {
  memset(state, 0, sizeof(*state));
  state->mass_kg = TOTAL_MASS_KG;
  state->forward[0] = FORWARD_AXIS_X;
  state->forward[1] = FORWARD_AXIS_Y;
  state->forward[2] = FORWARD_AXIS_Z;
  state->verbose = false;
}

void imu_power_update(imu_power_state_t* state,
                      const imu_calibration_t* cal,
                      const mpu6050_acce_value_t* acce,
                      stroke_phase_t stroke_phase,
                      float dt_s,
                      float* out_power_w) {
  *out_power_w = 0.0f;

  if (!cal->calibrated || dt_s <= 0.0f)
    return;

  const float* forward = state->forward;

  /* Raw reading as array for dot products */
  const float raw[3] = {acce->acce_x, acce->acce_y, acce->acce_z};

  /* Remove gravity component: a_dynamic = raw - dot(raw,down)*down */
  float gravity_component = dot3(raw, cal->gravity);
  float dynamic[3] = {
      raw[0] - gravity_component * cal->gravity[0],
      raw[1] - gravity_component * cal->gravity[1],
      raw[2] - gravity_component * cal->gravity[2],
  };

  /* Project onto forward axis to get signed forward acceleration (in g) */
  float a_forward_g = dot3(dynamic, forward);

  /* Convert g to m/s^2 */
  float a_forward_ms2 = a_forward_g * 9.81f;

  if (state->verbose) {
    static const char* const phase_names[] = {"RECOVERY", "CATCH", "PULL", "RELEASE"};
    ESP_LOGI(TAG, "acce x=%.3f y=%.3f z=%.3f | a_fwd=%.3f m/s^2 | phase=%s | dv=%.3f m/s dt=%.2f s",
             acce->acce_x, acce->acce_y, acce->acce_z, a_forward_ms2, phase_names[stroke_phase],
             state->stroke_delta_v_ms, state->stroke_dt_s);
  }

  /* Detect stroke start: RECOVERY/RELEASE -> CATCH */
  bool new_stroke =
      (stroke_phase == STROKE_PHASE_CATCH && state->prev_phase != STROKE_PHASE_CATCH &&
       state->prev_phase != STROKE_PHASE_PULL);
  if (new_stroke) {
    state->stroke_delta_v_ms = 0.0f;
    state->stroke_dt_s = 0.0f;
  }

  /* Accumulate delta-v while paddle is in water */
  if (stroke_phase == STROKE_PHASE_CATCH || stroke_phase == STROKE_PHASE_PULL) {
    state->stroke_delta_v_ms += a_forward_ms2 * dt_s;
    state->stroke_dt_s += dt_s;
  }

  /* Compute power once at PULL->RELEASE transition */
  bool stroke_ending =
      (stroke_phase == STROKE_PHASE_RELEASE && state->prev_phase != STROKE_PHASE_RELEASE);
  if (stroke_ending && state->stroke_dt_s > 0.0f) {
    float dv = state->stroke_delta_v_ms;
    state->avg_stroke_power_w = 0.5f * state->mass_kg * dv * dv / state->stroke_dt_s;
    ESP_LOGI(TAG, "Stroke: delta_v=%.3f m/s  dur=%.2f s  power=%.1f W", dv, state->stroke_dt_s,
             state->avg_stroke_power_w);
  }

  /* Drag estimation during recovery (kept for future GPS fusion) */
  if (stroke_phase == STROKE_PHASE_RECOVERY && a_forward_ms2 < 0.0f) {
    float drag_estimate = -state->mass_kg * a_forward_ms2;
    state->drag_force_n = 0.9f * state->drag_force_n + 0.1f * drag_estimate;
  }

  state->prev_phase = stroke_phase;

  /* Always output last completed stroke power */
  *out_power_w = state->avg_stroke_power_w;
}
