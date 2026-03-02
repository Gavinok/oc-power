#include <string.h>
#include "esp_log.h"
#include "stroke_detector.h"

#define TAG "STROKE"

static float s_catch_g    = STROKE_CATCH_THRESHOLD_G;
static float s_recovery_g = STROKE_RECOVERY_THRESHOLD_G;

void stroke_detector_set_catch_threshold(float catch_g)      { s_catch_g    = catch_g;    }
void stroke_detector_set_recovery_threshold(float recovery_g) { s_recovery_g = recovery_g; }

void stroke_detector_init(stroke_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->phase = STROKE_PHASE_RECOVERY;
}

int stroke_detector_update(stroke_state_t *state, float accel_g, int64_t ts_us) {
    int stroke_completed = 0;

    switch (state->phase) {

    case STROKE_PHASE_RECOVERY:
        if (accel_g > s_catch_g) {
            state->phase = STROKE_PHASE_CATCH;
            state->prev_stroke_start_us = state->stroke_start_us;
            state->stroke_start_us = ts_us;
            state->peak_accel_g = accel_g;
            ESP_LOGD(TAG, "CATCH at %.3f g", accel_g);
        }
        break;

    case STROKE_PHASE_CATCH:
        if (accel_g > state->peak_accel_g) {
            state->peak_accel_g = accel_g;
            state->phase = STROKE_PHASE_PULL;
            ESP_LOGD(TAG, "PULL peak %.3f g", accel_g);
        } else if (accel_g < s_recovery_g) {
            /* Spike too brief, treat as noise, return to recovery */
            state->phase = STROKE_PHASE_RECOVERY;
        }
        break;

    case STROKE_PHASE_PULL:
        if (accel_g > state->peak_accel_g) {
            state->peak_accel_g = accel_g;
        }
        if (accel_g < s_recovery_g) {
            state->phase = STROKE_PHASE_RELEASE;
            ESP_LOGD(TAG, "RELEASE");
        }
        break;

    case STROKE_PHASE_RELEASE: {
        int64_t duration_us = ts_us - state->stroke_start_us;

        if (accel_g < s_recovery_g) {
            /* Confirm stroke only if duration is plausible */
            if (duration_us >= STROKE_MIN_DURATION_US &&
                duration_us <= STROKE_MAX_DURATION_US) {

                state->stroke_duration_s = duration_us / 1e6f;
                if (state->prev_stroke_start_us > 0) {
                    float period_s = (state->stroke_start_us - state->prev_stroke_start_us) / 1e6f;
                    state->stroke_rate_spm = 60.0f / period_s;
                }
                state->recovery_start_us = ts_us;
                state->stroke_count++;
                stroke_completed = 1;

                ESP_LOGI(TAG, "Stroke #%d  peak=%.2fg  dur=%.2fs  rate=%.1f spm",
                         state->stroke_count, state->peak_accel_g,
                         state->stroke_duration_s, state->stroke_rate_spm);
            }
            state->phase = STROKE_PHASE_RECOVERY;
        } else if (accel_g > s_catch_g) {
            /* Acceleration picked back up, back into pull */
            state->phase = STROKE_PHASE_PULL;
        }
        break;
    }
    }

    return stroke_completed;
}
