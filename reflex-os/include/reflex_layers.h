/**
 * reflex_layers.h - Layered Exploration for ESP32-C6
 *
 * Multiple observers at different time scales.
 * Disagreement between layers drives exploration.
 */

#ifndef REFLEX_LAYERS_H
#define REFLEX_LAYERS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define NUM_LAYERS      3
#define NUM_OUTPUTS     8
#define NUM_GPIO_IN     8
#define NUM_ADC_IN      4
#define NUM_INPUTS      13  // 8 GPIO + 4 ADC + 1 temp

#define MEMORY_DEPTH    16
#define TICK_MS         100  // 10 Hz

// Output pins (safe GPIOs)
static const uint8_t OUTPUT_PINS[NUM_OUTPUTS] = {
    0, 1, 2, 3, 4, 5, 6, 7
};

// Input pins
static const uint8_t INPUT_PINS[NUM_GPIO_IN] = {
    10, 11, 14, 15, 18, 19, 20, 21
};

// ADC channels
static const uint8_t ADC_CHANNELS[NUM_ADC_IN] = {
    0, 1, 2, 3
};

// Special pins
#define PIN_LED     8
#define PIN_BUTTON  9

// ============================================================
// Memory Entry
// ============================================================

typedef struct {
    uint8_t output_idx;
    uint8_t output_state;
    int16_t deltas[NUM_INPUTS];
    uint32_t tick;
} mem_entry_t;

typedef struct {
    mem_entry_t entries[MEMORY_DEPTH];
    uint8_t head;
    uint8_t count;
} mem_buffer_t;

static inline void lmem_init(mem_buffer_t* m) {
    memset(m, 0, sizeof(mem_buffer_t));
}

static inline void lmem_push(mem_buffer_t* m, uint8_t out, uint8_t state,
                            int16_t* deltas, uint32_t tick) {
    mem_entry_t* e = &m->entries[m->head];
    e->output_idx = out;
    e->output_state = state;
    e->tick = tick;
    memcpy(e->deltas, deltas, sizeof(int16_t) * NUM_INPUTS);
    m->head = (m->head + 1) % MEMORY_DEPTH;
    if (m->count < MEMORY_DEPTH) m->count++;
}

static inline mem_entry_t* lmem_get(mem_buffer_t* m, uint8_t ago) {
    if (ago >= m->count) return NULL;
    int idx = (m->head - 1 - ago + MEMORY_DEPTH) % MEMORY_DEPTH;
    return &m->entries[idx];
}

// ============================================================
// Exploration Layer
// ============================================================

typedef struct {
    // Parameters (set at init, don't change)
    float tau;          // Decay rate: 0.99=slow, 0.50=fast
    uint8_t window;     // Ticks to consider
    const char* name;   // For debugging

    // State (updated each tick)
    float ema[NUM_OUTPUTS][NUM_INPUTS];     // Smoothed deltas
    float var[NUM_OUTPUTS][NUM_INPUTS];     // Variance estimates
    float entropy[NUM_OUTPUTS];             // Exploration priority

    // Output (computed each tick)
    float scores[NUM_OUTPUTS];
} layer_t;

static inline void layer_init(layer_t* l, float tau, uint8_t window, const char* name) {
    l->tau = tau;
    l->window = window;
    l->name = name;

    memset(l->ema, 0, sizeof(l->ema));
    memset(l->var, 0, sizeof(l->var));

    // Start with uniform entropy
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        l->entropy[o] = 100.0f;
        l->scores[o] = 100.0f;
    }
}

// ============================================================
// Layered State
// ============================================================

typedef struct {
    // The three layers
    layer_t layers[NUM_LAYERS];

    // Shared memory
    mem_buffer_t memory;

    // Current readings
    int16_t gpio_in[NUM_GPIO_IN];
    int16_t adc_in[NUM_ADC_IN];
    int16_t temp_in;

    // Output states
    uint8_t output_states[NUM_OUTPUTS];

    // Decision
    float final_scores[NUM_OUTPUTS];
    uint8_t chosen_output;
    uint8_t chosen_state;

    // Metrics
    uint32_t tick;
    uint32_t agreements;     // All layers within 20% of each other
    uint32_t disagreements;  // Spread > 50% of max
    uint32_t output_counts[NUM_OUTPUTS];  // Times each output chosen

    // ADC handles
    reflex_adc_channel_t adc_handles[NUM_ADC_IN];

    // Sequence tracking (for stuck detection)
    uint8_t last_outputs[10];
    uint8_t seq_idx;

} layered_state_t;

// ============================================================
// Layer Analysis
// ============================================================

static inline void layer_analyze(layer_t* l, layered_state_t* state) {
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        float interest = 0.0f;

        // 1. Entropy: unexplored is interesting
        interest += l->entropy[o];

        // 2. Variance: unpredictable is interesting
        float total_var = 0.0f;
        for (int i = 0; i < NUM_INPUTS; i++) {
            total_var += l->var[o][i];
        }
        interest += sqrtf(total_var) * 0.5f;

        // 3. Recency penalty: just explored is less interesting
        for (int ago = 0; ago < l->window && ago < state->memory.count; ago++) {
            mem_entry_t* e = lmem_get(&state->memory, ago);
            if (e && e->output_idx == o) {
                // Decay penalty by age
                interest *= (0.7f + 0.3f * ((float)ago / l->window));
            }
        }

        l->scores[o] = interest;
    }
}

static inline void layer_update(layer_t* l, uint8_t output, int16_t* deltas) {
    for (int i = 0; i < NUM_INPUTS; i++) {
        float delta = (float)deltas[i];
        float old_ema = l->ema[output][i];

        // Update EMA
        float new_ema = l->tau * old_ema + (1.0f - l->tau) * delta;
        l->ema[output][i] = new_ema;

        // Update variance
        float diff = delta - new_ema;
        l->var[output][i] = l->tau * l->var[output][i] +
                           (1.0f - l->tau) * (diff * diff);
    }

    // Decay entropy for explored output
    l->entropy[output] *= 0.9f;
    if (l->entropy[output] < 10.0f) {
        l->entropy[output] = 10.0f;  // Floor
    }

    // Boost entropy for others (they become more interesting)
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        if (o != output) {
            l->entropy[o] *= 1.02f;
            if (l->entropy[o] > 500.0f) {
                l->entropy[o] = 500.0f;  // Ceiling
            }
        }
    }
}

// ============================================================
// Aggregation
// ============================================================

static inline void aggregate_scores(layered_state_t* state) {
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        float s0 = state->layers[0].scores[o];
        float s1 = state->layers[1].scores[o];
        float s2 = state->layers[2].scores[o];

        // Find min and max
        float min_s = s0;
        if (s1 < min_s) min_s = s1;
        if (s2 < min_s) min_s = s2;

        float max_s = s0;
        if (s1 > max_s) max_s = s1;
        if (s2 > max_s) max_s = s2;

        // Agreement component: geometric mean
        float agreement = cbrtf(s0 * s1 * s2);

        // Disagreement component: spread
        float spread = max_s - min_s;

        // Final score: agreement + disagreement bonus
        // High disagreement = layers see different things = interesting
        state->final_scores[o] = agreement + 0.3f * spread;
    }
}

static inline uint8_t choose_output(layered_state_t* state) {
    float max_score = state->final_scores[0];
    uint8_t best = 0;

    for (int o = 1; o < NUM_OUTPUTS; o++) {
        if (state->final_scores[o] > max_score) {
            max_score = state->final_scores[o];
            best = o;
        }
    }

    return best;
}

// ============================================================
// Metrics
// ============================================================

static inline void track_consensus(layered_state_t* state) {
    // Check each output for agreement/disagreement
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        float s0 = state->layers[0].scores[o];
        float s1 = state->layers[1].scores[o];
        float s2 = state->layers[2].scores[o];

        float max_s = fmaxf(fmaxf(s0, s1), s2);
        float min_s = fminf(fminf(s0, s1), s2);

        if (max_s > 0) {
            float spread_pct = (max_s - min_s) / max_s;
            if (spread_pct < 0.2f) {
                state->agreements++;
            } else if (spread_pct > 0.5f) {
                state->disagreements++;
            }
        }
    }
}

static inline bool check_stuck(layered_state_t* state, uint8_t output) {
    // Track last 10 outputs
    state->last_outputs[state->seq_idx] = output;
    state->seq_idx = (state->seq_idx + 1) % 10;

    // Check if all same
    if (state->tick < 10) return false;

    uint8_t first = state->last_outputs[0];
    for (int i = 1; i < 10; i++) {
        if (state->last_outputs[i] != first) return false;
    }
    return true;
}

// ============================================================
// Temperature Sensor
// ============================================================

#include "driver/temperature_sensor.h"

static temperature_sensor_handle_t temp_handle = NULL;

static inline void temp_init(void) {
    temperature_sensor_config_t conf = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_install(&conf, &temp_handle);
    temperature_sensor_enable(temp_handle);
}

static inline int16_t temp_read(void) {
    float t;
    temperature_sensor_get_celsius(temp_handle, &t);
    return (int16_t)(t * 100);  // Centidegrees
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_LAYERS_H
