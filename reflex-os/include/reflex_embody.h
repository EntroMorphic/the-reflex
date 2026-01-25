/**
 * reflex_embody.h - Embodied Self-Discovery for ESP32-C6
 *
 * The chip learns its own body through exploration.
 * No pre-programmed drivers. Meaning discovered, not assigned.
 *
 * Core insight: Navigation, not optimization.
 * The hardware is frozen. We learn to navigate it.
 */

#ifndef REFLEX_EMBODY_H
#define REFLEX_EMBODY_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

// Safe GPIO pins for exploration (avoid boot-critical pins)
// ESP32-C6 safe pins: 0-7, 10-21 (avoiding 8=LED, 9=BOOT, 12-13=USB)
#define NUM_EXPLORE_OUTPUTS  8
#define NUM_EXPLORE_INPUTS   8
#define NUM_ADC_INPUTS       4

// Temporal memory depth
#define MEMORY_DEPTH         16

// Relationship tracking
#define MAX_RELATIONSHIPS    32

// Exploration parameters
#define EXPLORE_TICK_MS      100    // 10 Hz exploration
#define SETTLE_TIME_US       1000   // Wait for physical effects
#define MIN_OBSERVATIONS     10     // Before crystallization
#define CORRELATION_THRESH   500    // Minimum delta to notice
#define CONFIDENCE_THRESH    8      // Observations before crystallize

// ============================================================
// Pin Mapping (discovered, but we need to know what's safe)
// ============================================================

static const uint8_t EXPLORE_OUTPUT_PINS[NUM_EXPLORE_OUTPUTS] = {
    0, 1, 2, 3, 4, 5, 6, 7  // Safe output pins
};

static const uint8_t EXPLORE_INPUT_PINS[NUM_EXPLORE_INPUTS] = {
    10, 11, 14, 15, 18, 19, 20, 21  // Safe input pins
};

static const uint8_t EXPLORE_ADC_CHANNELS[NUM_ADC_INPUTS] = {
    0, 1, 2, 3  // ADC1 channels
};

// Special pins we know about (but the system still discovers their function)
#define PIN_LED_BUILTIN      8   // Onboard LED
#define PIN_BOOT_BUTTON      9   // Boot button (external agent!)

// ============================================================
// Memory Entry - One (action, observation) pair
// ============================================================

typedef struct {
    uint8_t output_pin;              // Which pin we toggled
    uint8_t output_state;            // HIGH or LOW
    int16_t gpio_before[NUM_EXPLORE_INPUTS];   // GPIO state before
    int16_t gpio_after[NUM_EXPLORE_INPUTS];    // GPIO state after
    int16_t adc_before[NUM_ADC_INPUTS];        // ADC readings before
    int16_t adc_after[NUM_ADC_INPUTS];         // ADC readings after
    int16_t temp_before;             // Temperature before
    int16_t temp_after;              // Temperature after
    uint32_t timestamp;              // Cycle count
} memory_entry_t;

// ============================================================
// Memory Buffer - Recent trajectory
// ============================================================

typedef struct {
    memory_entry_t entries[MEMORY_DEPTH];
    uint8_t head;
    uint8_t count;
} memory_buffer_t;

static inline void memory_init(memory_buffer_t* mem) {
    memset(mem, 0, sizeof(memory_buffer_t));
}

static inline void memory_push(memory_buffer_t* mem, memory_entry_t* entry) {
    mem->entries[mem->head] = *entry;
    mem->head = (mem->head + 1) % MEMORY_DEPTH;
    if (mem->count < MEMORY_DEPTH) mem->count++;
}

static inline memory_entry_t* memory_get(memory_buffer_t* mem, uint8_t ago) {
    if (ago >= mem->count) return NULL;
    int idx = (mem->head - 1 - ago + MEMORY_DEPTH) % MEMORY_DEPTH;
    return &mem->entries[idx];
}

// ============================================================
// Forward Model - Predicts outcomes
// ============================================================

typedef struct {
    // predictions[output][state][input] = expected delta
    int16_t gpio_pred[NUM_EXPLORE_OUTPUTS][2][NUM_EXPLORE_INPUTS];
    int16_t adc_pred[NUM_EXPLORE_OUTPUTS][2][NUM_ADC_INPUTS];
    int16_t temp_pred[NUM_EXPLORE_OUTPUTS][2];

    // Confidence = number of observations
    uint8_t confidence[NUM_EXPLORE_OUTPUTS][2];
} forward_model_t;

static inline void forward_init(forward_model_t* model) {
    memset(model, 0, sizeof(forward_model_t));
}

// Update forward model with observation
static inline void forward_update(forward_model_t* model,
                                   uint8_t output_idx, uint8_t state,
                                   int16_t* gpio_delta,
                                   int16_t* adc_delta,
                                   int16_t temp_delta) {
    // Exponential moving average
    uint8_t conf = model->confidence[output_idx][state];
    float alpha = (conf < 10) ? 0.5f : 0.1f;  // Learn fast early, slow later

    for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
        model->gpio_pred[output_idx][state][i] =
            (int16_t)((1-alpha) * model->gpio_pred[output_idx][state][i] +
                      alpha * gpio_delta[i]);
    }
    for (int i = 0; i < NUM_ADC_INPUTS; i++) {
        model->adc_pred[output_idx][state][i] =
            (int16_t)((1-alpha) * model->adc_pred[output_idx][state][i] +
                      alpha * adc_delta[i]);
    }
    model->temp_pred[output_idx][state] =
        (int16_t)((1-alpha) * model->temp_pred[output_idx][state] +
                  alpha * temp_delta);

    if (conf < 255) model->confidence[output_idx][state]++;
}

// ============================================================
// Backward Model - Credit assignment
// ============================================================

typedef struct {
    // credit[output][input] = correlation strength
    int16_t gpio_credit[NUM_EXPLORE_OUTPUTS][NUM_EXPLORE_INPUTS];
    int16_t adc_credit[NUM_EXPLORE_OUTPUTS][NUM_ADC_INPUTS];
    int16_t temp_credit[NUM_EXPLORE_OUTPUTS];
} backward_model_t;

static inline void backward_init(backward_model_t* model) {
    memset(model, 0, sizeof(backward_model_t));
}

// Assign credit for an observation to recent actions
static inline void backward_credit(backward_model_t* model,
                                    memory_buffer_t* memory,
                                    int16_t* gpio_delta,
                                    int16_t* adc_delta,
                                    int16_t temp_delta) {
    // Look at last few actions, assign credit proportionally
    for (int ago = 0; ago < memory->count && ago < 4; ago++) {
        memory_entry_t* entry = memory_get(memory, ago);
        if (!entry) continue;

        // Find output index
        int out_idx = -1;
        for (int i = 0; i < NUM_EXPLORE_OUTPUTS; i++) {
            if (EXPLORE_OUTPUT_PINS[i] == entry->output_pin) {
                out_idx = i;
                break;
            }
        }
        if (out_idx < 0) continue;

        // Credit decays with time (more recent = more credit)
        float weight = 1.0f / (ago + 1);

        for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
            model->gpio_credit[out_idx][i] += (int16_t)(weight * gpio_delta[i]);
        }
        for (int i = 0; i < NUM_ADC_INPUTS; i++) {
            model->adc_credit[out_idx][i] += (int16_t)(weight * adc_delta[i]);
        }
        model->temp_credit[out_idx] += (int16_t)(weight * temp_delta);
    }
}

// ============================================================
// Entropy Field - Exploration map
// ============================================================

typedef struct {
    // entropy[output][input] = how unexplored (high = unexplored)
    uint16_t gpio_entropy[NUM_EXPLORE_OUTPUTS][NUM_EXPLORE_INPUTS];
    uint16_t adc_entropy[NUM_EXPLORE_OUTPUTS][NUM_ADC_INPUTS];
    uint16_t output_entropy[NUM_EXPLORE_OUTPUTS];

    uint32_t total_entropy;
} explore_entropy_t;

static inline void entropy_init(explore_entropy_t* ent) {
    // Start with high entropy everywhere
    for (int o = 0; o < NUM_EXPLORE_OUTPUTS; o++) {
        ent->output_entropy[o] = 1000;
        for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
            ent->gpio_entropy[o][i] = 1000;
        }
        for (int i = 0; i < NUM_ADC_INPUTS; i++) {
            ent->adc_entropy[o][i] = 1000;
        }
    }
    ent->total_entropy = 1000 * NUM_EXPLORE_OUTPUTS *
                         (NUM_EXPLORE_INPUTS + NUM_ADC_INPUTS + 1);
}

// Reduce entropy after exploring an output
static inline void entropy_explored(explore_entropy_t* ent, uint8_t output_idx) {
    if (ent->output_entropy[output_idx] > 100) {
        ent->output_entropy[output_idx] -= 100;
        ent->total_entropy -= 100;
    }
}

// Find highest entropy output (most unexplored)
static inline uint8_t entropy_choose_output(explore_entropy_t* ent) {
    uint16_t max_ent = 0;
    uint8_t best = 0;
    for (int o = 0; o < NUM_EXPLORE_OUTPUTS; o++) {
        if (ent->output_entropy[o] > max_ent) {
            max_ent = ent->output_entropy[o];
            best = o;
        }
    }
    return best;
}

// ============================================================
// Discovered Relationship
// ============================================================

typedef enum {
    REL_GPIO_TO_GPIO,
    REL_GPIO_TO_ADC,
    REL_GPIO_TO_TEMP,
    REL_EXTERNAL_AGENT  // Input changed without our action!
} relationship_type_t;

typedef struct {
    relationship_type_t type;
    uint8_t output_pin;          // Which output
    uint8_t input_channel;       // Which input (GPIO index, ADC channel, or 0 for temp)
    int16_t effect_high;         // Effect when output HIGH
    int16_t effect_low;          // Effect when output LOW
    uint16_t confidence;         // How sure
    uint32_t discovered_tick;    // When discovered
    uint32_t last_verified;      // Last confirmation
    bool active;                 // Still valid?
} relationship_t;

// ============================================================
// Main Embodied State
// ============================================================

typedef struct {
    // Models
    forward_model_t forward;
    backward_model_t backward;
    memory_buffer_t memory;
    explore_entropy_t entropy;

    // Discovered knowledge
    relationship_t relationships[MAX_RELATIONSHIPS];
    uint8_t num_relationships;

    // Current state
    uint8_t current_output_idx;
    uint8_t current_output_state;
    int16_t baseline_gpio[NUM_EXPLORE_INPUTS];
    int16_t baseline_adc[NUM_ADC_INPUTS];
    int16_t baseline_temp;

    // Button state (external agent detection)
    bool last_button_state;
    uint32_t button_changes;

    // Statistics
    uint32_t tick;
    uint32_t actions_taken;
    uint32_t predictions_made;
    uint32_t prediction_errors;

    // ADC handles
    reflex_adc_channel_t adc_channels[NUM_ADC_INPUTS];

} embodied_state_t;

// ============================================================
// Core Functions (implemented in embody_main.c)
// ============================================================

/**
 * Initialize the embodied system.
 */
void embody_init(embodied_state_t* state);

/**
 * Run one exploration tick.
 * Returns true if a new relationship was discovered.
 */
bool embody_tick(embodied_state_t* state);

/**
 * Read all inputs (GPIO, ADC, temperature).
 */
void embody_read_all(embodied_state_t* state,
                     int16_t* gpio_out,
                     int16_t* adc_out,
                     int16_t* temp_out);

/**
 * Check for crystallization (new relationships).
 */
bool embody_check_crystallize(embodied_state_t* state);

/**
 * Check for external agent (button press).
 */
bool embody_check_external_agent(embodied_state_t* state);

/**
 * Print discovered relationships.
 */
void embody_print_relationships(embodied_state_t* state);

/**
 * Print current state for debugging.
 */
void embody_print_state(embodied_state_t* state);

// ============================================================
// Internal Temperature Reading
// ============================================================

// ESP32-C6 internal temperature sensor
#include "driver/temperature_sensor.h"

static temperature_sensor_handle_t temp_sensor = NULL;

static inline void embody_temp_init(void) {
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_install(&temp_config, &temp_sensor);
    temperature_sensor_enable(temp_sensor);
}

static inline int16_t embody_temp_read(void) {
    float temp;
    temperature_sensor_get_celsius(temp_sensor, &temp);
    return (int16_t)(temp * 100);  // Centidegrees for precision
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_EMBODY_H
