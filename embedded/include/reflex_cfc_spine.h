/**
 * reflex_cfc_spine.h - CfC Integrated Spine: Closed-Loop Neural Control
 *
 * The liquid neural network becomes part of the reflex arc:
 *   Sensor → CfC → Decision → Actuator
 *
 * The CfC provides:
 *   - Temporal filtering (smooths noisy inputs)
 *   - Predictive anticipation (hidden state carries history)
 *   - Adaptive thresholds (learned, not hardcoded)
 *   - Anomaly detection (deviation from learned patterns)
 *
 * Architecture:
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │                        CfC SPINE                                    │
 *   │                                                                     │
 *   │   Sensor Input (64-bit)                                            │
 *   │         │                                                          │
 *   │         ▼                                                          │
 *   │   ┌───────────┐                                                    │
 *   │   │   CfC     │ ← Hidden state (64-bit, carries history)          │
 *   │   │  TURBO    │                                                    │
 *   │   │  31 kHz   │                                                    │
 *   │   └───────────┘                                                    │
 *   │         │                                                          │
 *   │         ▼                                                          │
 *   │   Output (64-bit)                                                  │
 *   │         │                                                          │
 *   │         ├──► Bit 0: Anomaly detected                              │
 *   │         ├──► Bit 1: Prediction confidence                         │
 *   │         ├──► Bit 2: Trend (increasing/decreasing)                 │
 *   │         ├──► Bits 3-7: Action code                                │
 *   │         └──► Bits 8+: Application-specific                        │
 *   │                                                                     │
 *   └─────────────────────────────────────────────────────────────────────┘
 */

#ifndef REFLEX_CFC_SPINE_H
#define REFLEX_CFC_SPINE_H

#include <stdint.h>
#include <stdbool.h>
#include "reflex.h"
#include "reflex_cfc_turbo.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Output Bit Definitions
// ============================================================

#define CFC_OUT_ANOMALY         (1ULL << 0)   // Anomaly detected
#define CFC_OUT_CONFIDENT       (1ULL << 1)   // High confidence in prediction
#define CFC_OUT_TREND_UP        (1ULL << 2)   // Trend is increasing
#define CFC_OUT_TREND_DOWN      (1ULL << 3)   // Trend is decreasing
#define CFC_OUT_SPIKE           (1ULL << 4)   // Sudden spike detected
#define CFC_OUT_FLAT            (1ULL << 5)   // Signal is flat/stable
#define CFC_OUT_OSCILLATING     (1ULL << 6)   // Oscillation detected
#define CFC_OUT_SATURATED       (1ULL << 7)   // Input is at limits

// Action codes (bits 8-15)
#define CFC_OUT_ACTION_MASK     (0xFFULL << 8)
#define CFC_OUT_ACTION_NONE     (0x00ULL << 8)
#define CFC_OUT_ACTION_BRAKE    (0x01ULL << 8)
#define CFC_OUT_ACTION_RELEASE  (0x02ULL << 8)
#define CFC_OUT_ACTION_HOLD     (0x03ULL << 8)
#define CFC_OUT_ACTION_TRACK    (0x04ULL << 8)
#define CFC_OUT_ACTION_AVOID    (0x05ULL << 8)

// ============================================================
// Spine State
// ============================================================

typedef struct {
    // The neural network
    cfc_turbo_layer_t network;
    
    // Input history for trend detection
    uint64_t prev_input;
    uint64_t prev_prev_input;
    
    // Output history for stability detection
    uint64_t prev_output;
    uint32_t stable_count;
    
    // Statistics
    uint32_t total_ticks;
    uint32_t anomaly_count;
    uint32_t spike_count;
    
    // Timing
    uint32_t last_tick_cycles;
    uint32_t avg_tick_cycles;
    
} cfc_spine_t;

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize CfC spine with random weights
 */
static inline void cfc_spine_init(cfc_spine_t* spine, uint32_t seed) {
    cfc_turbo_init_random(&spine->network, seed);
    
    spine->prev_input = 0;
    spine->prev_prev_input = 0;
    spine->prev_output = 0;
    spine->stable_count = 0;
    
    spine->total_ticks = 0;
    spine->anomaly_count = 0;
    spine->spike_count = 0;
    
    spine->last_tick_cycles = 0;
    spine->avg_tick_cycles = 0;
}

/**
 * Initialize from pre-trained turbo layer
 */
static inline void cfc_spine_init_from_network(
    cfc_spine_t* spine,
    const cfc_turbo_layer_t* network
) {
    spine->network = *network;
    
    spine->prev_input = 0;
    spine->prev_prev_input = 0;
    spine->prev_output = 0;
    spine->stable_count = 0;
    
    spine->total_ticks = 0;
    spine->anomaly_count = 0;
    spine->spike_count = 0;
    
    spine->last_tick_cycles = 0;
    spine->avg_tick_cycles = 0;
}

// ============================================================
// Core Tick Function
// ============================================================

/**
 * Process one tick of the CfC spine
 *
 * Input encoding (example for force sensor):
 *   Bits 0-7:   Current force value (0-255)
 *   Bits 8-15:  Force derivative (signed, -128 to +127)
 *   Bits 16-23: Force integral (accumulated)
 *   Bits 24-31: Sensor status flags
 *   Bits 32-63: Application-specific
 *
 * Returns: Output bit vector (see CFC_OUT_* definitions)
 */
static inline uint64_t cfc_spine_tick(
    cfc_spine_t* spine,
    uint64_t input
) {
    uint32_t t0 = reflex_cycles();
    
    // Run CfC forward pass
    uint64_t raw_output;
    cfc_turbo_forward(&spine->network, input, &raw_output);
    
    // Post-process: Add trend detection, spike detection, etc.
    uint64_t output = raw_output;
    
    // Trend detection (based on input history)
    if (input > spine->prev_input && spine->prev_input > spine->prev_prev_input) {
        output |= CFC_OUT_TREND_UP;
    }
    if (input < spine->prev_input && spine->prev_input < spine->prev_prev_input) {
        output |= CFC_OUT_TREND_DOWN;
    }
    
    // Spike detection (large sudden change)
    int64_t delta = (int64_t)input - (int64_t)spine->prev_input;
    if (delta < 0) delta = -delta;
    if (delta > 0x0F0F0F0F0F0F0F0FULL) {  // Threshold per byte
        output |= CFC_OUT_SPIKE;
        spine->spike_count++;
    }
    
    // Stability detection
    if (output == spine->prev_output) {
        spine->stable_count++;
        if (spine->stable_count > 10) {
            output |= CFC_OUT_FLAT;
        }
    } else {
        spine->stable_count = 0;
    }
    
    // Oscillation detection (output keeps flipping)
    if ((output ^ spine->prev_output) == (spine->prev_output ^ raw_output)) {
        output |= CFC_OUT_OSCILLATING;
    }
    
    // Update anomaly counter
    if (output & CFC_OUT_ANOMALY) {
        spine->anomaly_count++;
    }
    
    // Update history
    spine->prev_prev_input = spine->prev_input;
    spine->prev_input = input;
    spine->prev_output = output;
    
    // Update timing statistics
    uint32_t cycles = reflex_cycles() - t0;
    spine->last_tick_cycles = cycles;
    spine->total_ticks++;
    
    // Exponential moving average for timing
    if (spine->avg_tick_cycles == 0) {
        spine->avg_tick_cycles = cycles;
    } else {
        spine->avg_tick_cycles = (spine->avg_tick_cycles * 15 + cycles) / 16;
    }
    
    return output;
}

// ============================================================
// Convenience Functions
// ============================================================

/**
 * Check if anomaly detected
 */
static inline bool cfc_spine_anomaly(uint64_t output) {
    return (output & CFC_OUT_ANOMALY) != 0;
}

/**
 * Get action code from output
 */
static inline uint8_t cfc_spine_action(uint64_t output) {
    return (uint8_t)((output & CFC_OUT_ACTION_MASK) >> 8);
}

/**
 * Check if output suggests emergency stop
 */
static inline bool cfc_spine_emergency(uint64_t output) {
    return (output & (CFC_OUT_ANOMALY | CFC_OUT_SPIKE)) != 0;
}

/**
 * Get spine statistics
 */
typedef struct {
    uint32_t total_ticks;
    uint32_t anomaly_count;
    uint32_t spike_count;
    float anomaly_rate;
    uint32_t avg_tick_ns;
    uint32_t last_tick_ns;
} cfc_spine_stats_t;

static inline cfc_spine_stats_t cfc_spine_get_stats(const cfc_spine_t* spine) {
    cfc_spine_stats_t stats;
    stats.total_ticks = spine->total_ticks;
    stats.anomaly_count = spine->anomaly_count;
    stats.spike_count = spine->spike_count;
    stats.anomaly_rate = spine->total_ticks > 0 
        ? (float)spine->anomaly_count / (float)spine->total_ticks 
        : 0.0f;
    stats.avg_tick_ns = reflex_cycles_to_ns(spine->avg_tick_cycles);
    stats.last_tick_ns = reflex_cycles_to_ns(spine->last_tick_cycles);
    return stats;
}

// ============================================================
// Sensor Input Encoders
// ============================================================

/**
 * Encode force sensor reading into CfC input format
 */
static inline uint64_t cfc_encode_force(
    uint8_t force_value,
    int8_t force_derivative,
    uint8_t force_integral,
    uint8_t sensor_flags
) {
    return (uint64_t)force_value |
           ((uint64_t)(uint8_t)force_derivative << 8) |
           ((uint64_t)force_integral << 16) |
           ((uint64_t)sensor_flags << 24);
}

/**
 * Encode IMU reading into CfC input format
 */
static inline uint64_t cfc_encode_imu(
    int8_t accel_x, int8_t accel_y, int8_t accel_z,
    int8_t gyro_x, int8_t gyro_y, int8_t gyro_z,
    uint8_t status, uint8_t flags
) {
    return (uint64_t)(uint8_t)accel_x |
           ((uint64_t)(uint8_t)accel_y << 8) |
           ((uint64_t)(uint8_t)accel_z << 16) |
           ((uint64_t)(uint8_t)gyro_x << 24) |
           ((uint64_t)(uint8_t)gyro_y << 32) |
           ((uint64_t)(uint8_t)gyro_z << 40) |
           ((uint64_t)status << 48) |
           ((uint64_t)flags << 56);
}

/**
 * Encode generic 8-channel sensor array
 */
static inline uint64_t cfc_encode_array(const uint8_t values[8]) {
    return (uint64_t)values[0] |
           ((uint64_t)values[1] << 8) |
           ((uint64_t)values[2] << 16) |
           ((uint64_t)values[3] << 24) |
           ((uint64_t)values[4] << 32) |
           ((uint64_t)values[5] << 40) |
           ((uint64_t)values[6] << 48) |
           ((uint64_t)values[7] << 56);
}

// ============================================================
// Output Decoders
// ============================================================

/**
 * Decode output to motor command
 */
typedef struct {
    bool emergency_stop;
    bool anomaly;
    bool confident;
    uint8_t action;
    int8_t trend;  // -1 = down, 0 = stable, +1 = up
} cfc_motor_command_t;

static inline cfc_motor_command_t cfc_decode_motor(uint64_t output) {
    cfc_motor_command_t cmd;
    cmd.emergency_stop = (output & (CFC_OUT_ANOMALY | CFC_OUT_SPIKE)) != 0;
    cmd.anomaly = (output & CFC_OUT_ANOMALY) != 0;
    cmd.confident = (output & CFC_OUT_CONFIDENT) != 0;
    cmd.action = cfc_spine_action(output);
    
    if (output & CFC_OUT_TREND_UP) {
        cmd.trend = 1;
    } else if (output & CFC_OUT_TREND_DOWN) {
        cmd.trend = -1;
    } else {
        cmd.trend = 0;
    }
    
    return cmd;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_CFC_SPINE_H
