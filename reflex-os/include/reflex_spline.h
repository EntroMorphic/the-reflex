/**
 * reflex_spline.h - Spline Channels for ESP32-C6
 *
 * A spline channel bridges discrete signals and continuous reality.
 * You signal control points. You read smooth interpolated values.
 *
 * The channel IS the manifold connecting discrete events.
 *
 * Uses Catmull-Rom splines for smooth interpolation through all points.
 */

#ifndef REFLEX_SPLINE_H
#define REFLEX_SPLINE_H

#include <stdint.h>
#include <stdbool.h>
#include "reflex.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Spline Channel Structure
// ============================================================

#define SPLINE_HISTORY_SIZE 4  // Catmull-Rom needs 4 points

typedef struct {
    // Control points (circular buffer)
    int32_t values[SPLINE_HISTORY_SIZE];
    uint32_t times[SPLINE_HISTORY_SIZE];   // Timestamps in cycles
    uint8_t head;                           // Next write position
    uint8_t count;                          // Number of valid points

    // Standard reflex channel for signaling
    reflex_channel_t base;
} reflex_spline_channel_t;

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize a spline channel
 */
static inline void spline_init(reflex_spline_channel_t* sp) {
    sp->head = 0;
    sp->count = 0;
    sp->base.sequence = 0;
    sp->base.value = 0;
    sp->base.timestamp = 0;
    sp->base.flags = 0;

    for (int i = 0; i < SPLINE_HISTORY_SIZE; i++) {
        sp->values[i] = 0;
        sp->times[i] = 0;
    }
}

/**
 * Initialize with a starting value (fills history)
 */
static inline void spline_init_at(reflex_spline_channel_t* sp, int32_t value) {
    uint32_t now = reflex_cycles();
    sp->head = 0;
    sp->count = SPLINE_HISTORY_SIZE;

    for (int i = 0; i < SPLINE_HISTORY_SIZE; i++) {
        sp->values[i] = value;
        sp->times[i] = now - (SPLINE_HISTORY_SIZE - 1 - i) * 1000;  // Fake history
    }

    sp->base.sequence = 0;
    sp->base.value = value;
    sp->base.timestamp = now;
    sp->base.flags = 0;
}

// ============================================================
// Signal: Add Control Point
// ============================================================

/**
 * Signal a new control point on the spline
 * The spline will smoothly pass through this point
 */
static inline void spline_signal(reflex_spline_channel_t* sp, int32_t value) {
    uint32_t now = reflex_cycles();

    // Add to circular buffer
    sp->values[sp->head] = value;
    sp->times[sp->head] = now;
    sp->head = (sp->head + 1) % SPLINE_HISTORY_SIZE;
    if (sp->count < SPLINE_HISTORY_SIZE) sp->count++;

    // Also update base channel
    reflex_signal(&sp->base, (uint32_t)value);
}

// ============================================================
// Catmull-Rom Interpolation
// ============================================================

/**
 * Catmull-Rom cubic interpolation
 * t: normalized parameter [0, 1] between p1 and p2
 * Returns interpolated value
 *
 * Uses integer math with fixed-point for speed.
 * Precision: 10 fractional bits (1024 = 1.0)
 */
static inline int32_t catmull_rom(int32_t p0, int32_t p1, int32_t p2, int32_t p3,
                                   uint32_t t_fixed) {
    // t is in fixed point: 0-1024 represents 0.0-1.0
    // Catmull-Rom formula:
    // q(t) = 0.5 * ((2*p1) +
    //               (-p0 + p2) * t +
    //               (2*p0 - 5*p1 + 4*p2 - p3) * t^2 +
    //               (-p0 + 3*p1 - 3*p2 + p3) * t^3)

    int64_t t = t_fixed;
    int64_t t2 = (t * t) >> 10;
    int64_t t3 = (t2 * t) >> 10;

    int64_t a0 = 2 * p1;
    int64_t a1 = (-p0 + p2);
    int64_t a2 = (2*p0 - 5*p1 + 4*p2 - p3);
    int64_t a3 = (-p0 + 3*p1 - 3*p2 + p3);

    int64_t result = a0 * 1024 + a1 * t + a2 * t2 + a3 * t3;
    return (int32_t)(result >> 11);  // Divide by 2048 (2 * 1024)
}

// ============================================================
// Read: Interpolate at Current Time
// ============================================================

/**
 * Read the interpolated value at the current time
 *
 * If we're between control points, returns smooth interpolation.
 * If we're past the last point, extrapolates (or holds last value).
 */
static inline int32_t spline_read(reflex_spline_channel_t* sp) {
    uint32_t now = reflex_cycles();

    // Need at least 2 points for interpolation
    if (sp->count < 2) {
        return sp->count > 0 ? sp->values[(sp->head + SPLINE_HISTORY_SIZE - 1) % SPLINE_HISTORY_SIZE] : 0;
    }

    // Get the 4 most recent points in order
    int32_t p[4];
    uint32_t t[4];
    for (int i = 0; i < SPLINE_HISTORY_SIZE; i++) {
        int idx = (sp->head + i) % SPLINE_HISTORY_SIZE;
        p[i] = sp->values[idx];
        t[i] = sp->times[idx];
    }

    // If less than 4 points, duplicate endpoints
    if (sp->count < 4) {
        if (sp->count == 2) {
            p[0] = p[2]; t[0] = t[2] - (t[3] - t[2]);
            p[1] = p[2]; t[1] = t[2];
        } else if (sp->count == 3) {
            p[0] = p[1]; t[0] = t[1] - (t[2] - t[1]);
        }
    }

    // Find which segment we're in
    // We interpolate between p[1] and p[2] (using p[0] and p[3] for curvature)
    uint32_t t1 = t[2];  // Second-to-last point
    uint32_t t2 = t[3];  // Last point

    // If now is before t1, hold at p[1]
    if (now <= t1) {
        return p[2];
    }

    // If now is after t2, extrapolate or hold
    if (now >= t2) {
        // Hold at last value (safer than extrapolation)
        return p[3];
    }

    // Interpolate between t1 and t2
    uint32_t dt = t2 - t1;
    if (dt == 0) return p[2];

    uint32_t t_norm = ((now - t1) * 1024) / dt;  // Fixed-point [0, 1024]
    if (t_norm > 1024) t_norm = 1024;

    return catmull_rom(p[1], p[2], p[3], p[3], t_norm);  // Duplicate p3 for endpoint
}

/**
 * Read interpolated value at a specific time offset from now
 * offset_cycles: negative for past, positive for future
 */
static inline int32_t spline_read_at(reflex_spline_channel_t* sp, int32_t offset_cycles) {
    // Temporarily adjust head time, read, restore
    // For simplicity, just read at current time for now
    // Full implementation would need time parameter in spline_read
    (void)offset_cycles;
    return spline_read(sp);
}

// ============================================================
// Velocity: Rate of Change
// ============================================================

/**
 * Get the current velocity (derivative of spline)
 * Returns change in value per 1000 cycles
 */
static inline int32_t spline_velocity(reflex_spline_channel_t* sp) {
    if (sp->count < 2) return 0;

    // Get two most recent points
    int idx1 = (sp->head + SPLINE_HISTORY_SIZE - 2) % SPLINE_HISTORY_SIZE;
    int idx2 = (sp->head + SPLINE_HISTORY_SIZE - 1) % SPLINE_HISTORY_SIZE;

    int32_t dv = sp->values[idx2] - sp->values[idx1];
    uint32_t dt = sp->times[idx2] - sp->times[idx1];

    if (dt == 0) return 0;

    // Scale to per-1000-cycles
    return (dv * 1000) / (int32_t)dt;
}

// ============================================================
// Prediction: Extrapolate Future
// ============================================================

/**
 * Predict value at a future time
 * future_cycles: how far ahead to predict
 *
 * Uses velocity extrapolation (linear for now)
 */
static inline int32_t spline_predict(reflex_spline_channel_t* sp, uint32_t future_cycles) {
    int32_t current = spline_read(sp);
    int32_t vel = spline_velocity(sp);

    // Linear extrapolation: value + velocity * time
    return current + (vel * (int32_t)future_cycles) / 1000;
}

// ============================================================
// Convenience: Float API (for non-hot-path usage)
// ============================================================

/**
 * Signal with float value (scaled to fixed point internally)
 * scale: multiplier to convert float to int32 (e.g., 1000 for 3 decimal places)
 */
static inline void spline_signal_f(reflex_spline_channel_t* sp, float value, int32_t scale) {
    spline_signal(sp, (int32_t)(value * scale));
}

/**
 * Read as float
 */
static inline float spline_read_f(reflex_spline_channel_t* sp, int32_t scale) {
    return (float)spline_read(sp) / (float)scale;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_SPLINE_H
