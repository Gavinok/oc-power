#pragma once

#include <stdint.h>

typedef enum {
    STROKE_PHASE_RECOVERY,  /* Paddle out of water, boat decelerating */
    STROKE_PHASE_CATCH,     /* Paddle entry, acceleration rising */
    STROKE_PHASE_PULL,      /* Power phase, peak acceleration */
    STROKE_PHASE_RELEASE,   /* Paddle exit, acceleration falling */
} stroke_phase_t;

/* Acceleration threshold to detect stroke catch (in g) */
#define STROKE_CATCH_THRESHOLD_G     0.3f
/* Acceleration threshold to detect return to recovery (in g) */
#define STROKE_RECOVERY_THRESHOLD_G  0.1f
/* Minimum stroke duration to reject noise (microseconds) */
#define STROKE_MIN_DURATION_US       300000   /* 300ms */
/* Maximum stroke duration (sanity check) */
#define STROKE_MAX_DURATION_US       3000000  /* 3s */

typedef struct {
    stroke_phase_t phase;
    int64_t stroke_start_us;      /* Time catch began (current stroke) */
    int64_t prev_stroke_start_us; /* Time catch began (previous stroke) */
    int64_t recovery_start_us;    /* Time recovery began */
    float   peak_accel_g;         /* Peak acceleration during pull */
    float   stroke_duration_s;    /* Duration of last complete stroke */
    float   stroke_rate_spm;      /* Strokes per minute (catch-to-catch period) */
    int     stroke_count;         /* Total strokes detected */
} stroke_state_t;

void stroke_detector_init(stroke_state_t *state);

/* Update the catch and recovery thresholds (in g).
 * Defaults: STROKE_CATCH_THRESHOLD_G / STROKE_RECOVERY_THRESHOLD_G. */
void stroke_detector_set_catch_threshold(float catch_g);
void stroke_detector_set_recovery_threshold(float recovery_g);

/* Feed one acceleration sample (magnitude in g, timestamp in microseconds).
 * Returns 1 if a stroke just completed, 0 otherwise. */
int stroke_detector_update(stroke_state_t *state, float accel_magnitude_g, int64_t timestamp_us);
