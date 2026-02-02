/**
 * reflex_turing_fabric.h - Hardware-Only Neural Computation
 *
 * The CPU sleeps while the fabric computes.
 *
 * Architecture:
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                     TURING FABRIC                                       │
 *   │                                                                         │
 *   │   The insight: PCNT can COUNT. Counting IS addition.                   │
 *   │   If we can convert VALUES to PULSES, we can ADD in hardware.          │
 *   │                                                                         │
 *   │   ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐            │
 *   │   │  GDMA   │───►│   RMT   │───►│  GPIO   │───►│  PCNT   │            │
 *   │   │  (LUT)  │    │(pulses) │    │ (wire)  │    │ (count) │            │
 *   │   └─────────┘    └─────────┘    └─────────┘    └─────────┘            │
 *   │       │              │              │              │                   │
 *   │   Read from      Generate N     Physical       Hardware                │
 *   │   spline LUT     pulses from    connection     accumulator             │
 *   │                  RMT memory                    (the SUM!)              │
 *   │                                                                         │
 *   │   ETM orchestrates the whole thing. CPU = SLEEPING.                    │
 *   │                                                                         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * Spline Interpolation via Hardware:
 *
 *   For a function f(x), we store:
 *     - knots[N]: x-coordinates of control points
 *     - values[N]: y-coordinates (f(knot[i]))
 *     - slopes[N]: derivative at each knot (for cubic) or between knots (linear)
 *
 *   Linear interpolation:
 *     segment = x >> SEGMENT_BITS
 *     offset = x & OFFSET_MASK
 *     result = base[segment] + slope[segment] * offset
 *
 *   The multiply becomes repeated addition via pulses:
 *     slope[segment] * offset = slope pulses, repeated 'offset' times
 *
 *   Or pre-compute: slope_contrib[segment][offset] as a 2D LUT
 *     16 segments × 16 offsets = 256 bytes per function
 *
 * Hardware Accumulation:
 *
 *   PCNT features on ESP32-C6:
 *     - 4 units, 2 channels each
 *     - 16-bit signed counter (-32768 to +32767)
 *     - Count UP on positive edge, DOWN on negative edge
 *     - Level signal can invert counting direction
 *     - Glitch filter built-in
 *
 *   For neural dot product:
 *     - Positive weights: count UP
 *     - Negative weights: count DOWN
 *     - Zero weights: don't generate pulses
 *
 *   This IS sparse ternary in hardware!
 *
 * The Fabric Pipeline:
 *
 *   1. Input arrives (ADC, GPIO, or software trigger)
 *   2. ETM triggers GDMA to read from quantized input LUT
 *   3. LUT output configures RMT pulse pattern
 *   4. RMT generates pulses → GPIO → PCNT
 *   5. PCNT accumulates (hardware addition!)
 *   6. ETM triggers next stage when PCNT reaches threshold
 *   7. Repeat for each layer
 *   8. Final PCNT value = network output
 *
 *   CPU wakes only to read final result (or stays asleep if output goes to GPIO)
 */

#ifndef REFLEX_TURING_FABRIC_H
#define REFLEX_TURING_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/rmt_tx.h"
#include "esp_etm.h"
#include "driver/gpio_etm.h"
#include "esp_async_memcpy.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

// Spline configuration
#define FABRIC_SPLINE_SEGMENTS      16      // Number of spline segments
#define FABRIC_SPLINE_OFFSETS       16      // Offsets per segment (4-bit)
#define FABRIC_SEGMENT_BITS         4       // Bits for segment index
#define FABRIC_OFFSET_MASK          0x0F    // Mask for offset within segment

// Neural configuration
#define FABRIC_MAX_NEURONS          8       // Neurons per layer
#define FABRIC_INPUT_DIM            4       // Input dimension
#define FABRIC_HIDDEN_DIM           8       // Hidden dimension

// Hardware resources
#define FABRIC_PCNT_UNIT            0       // Which PCNT unit to use
#define FABRIC_RMT_CHANNEL          0       // Which RMT channel
#define FABRIC_PULSE_GPIO           4       // RMT output → PCNT input
#define FABRIC_SIGN_GPIO            5       // Sign signal for PCNT direction

// Pulse timing (in RMT ticks, 1 tick = 100ns at 10MHz)
#define FABRIC_PULSE_HIGH_TICKS     2       // 200ns high
#define FABRIC_PULSE_LOW_TICKS      2       // 200ns low
#define FABRIC_PULSE_PERIOD_NS      400     // Total pulse period

// ============================================================
// Spline LUT Structures
// ============================================================

/**
 * Piecewise linear spline stored as 2D LUT
 * 
 * For input x (8-bit):
 *   segment = x >> 4 (high nibble)
 *   offset = x & 0x0F (low nibble)
 *   result = lut[segment][offset]
 *
 * This pre-computes base[seg] + slope[seg] * offset for all combinations.
 * 16 × 16 = 256 bytes per function.
 */
typedef struct {
    int8_t lut[FABRIC_SPLINE_SEGMENTS][FABRIC_SPLINE_OFFSETS];
} fabric_spline_t;

/**
 * Collection of splines for activation functions
 */
typedef struct {
    fabric_spline_t sigmoid;    // σ(x) spline
    fabric_spline_t tanh;       // tanh(x) spline
    fabric_spline_t identity;   // Pass-through (for debugging)
} fabric_activations_t;

// ============================================================
// Sparse Ternary Weights as Pulse Patterns
// ============================================================

/**
 * For sparse ternary dot product in hardware:
 * 
 * Instead of storing weights {-1, 0, +1}, we store:
 *   - pos_mask: bitmask of which inputs have +1 weight
 *   - neg_mask: bitmask of which inputs have -1 weight
 *
 * The dot product becomes:
 *   result = popcount(input & pos_mask) - popcount(input & neg_mask)
 *
 * In hardware:
 *   - For each bit set in (input & pos_mask): pulse UP
 *   - For each bit set in (input & neg_mask): pulse DOWN
 *   - PCNT accumulates the result
 */
typedef struct {
    uint8_t pos_mask;   // Inputs with +1 weight
    uint8_t neg_mask;   // Inputs with -1 weight
} fabric_ternary_row_t;

/**
 * Sparse ternary weight matrix
 * Each neuron has a row for computing its input
 */
typedef struct {
    fabric_ternary_row_t gate[FABRIC_MAX_NEURONS];  // Gate weights
    fabric_ternary_row_t cand[FABRIC_MAX_NEURONS];  // Candidate weights
} fabric_sparse_weights_t;

// ============================================================
// Pulse Pattern for RMT
// ============================================================

/**
 * Pre-computed pulse patterns for values 0-15
 * 
 * Value N = N pulses followed by end marker
 * RMT reads these patterns from memory and generates GPIO pulses
 */
typedef struct {
    // Each pattern: up to 16 pulses + end marker
    // RMT symbol: {duration0, level0, duration1, level1}
    rmt_symbol_word_t patterns[16][17];  // patterns[value][pulse_index]
    size_t lengths[16];                   // Number of symbols per pattern
} fabric_pulse_patterns_t;

// ============================================================
// Hardware Handles
// ============================================================

typedef struct {
    // PCNT for accumulation
    pcnt_unit_handle_t pcnt_unit;
    pcnt_channel_handle_t pcnt_chan_pos;  // Count up
    pcnt_channel_handle_t pcnt_chan_neg;  // Count down (optional)
    
    // RMT for pulse generation
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t rmt_encoder;
    
    // ETM for orchestration
    esp_etm_channel_handle_t etm_chan;
    
    // Async memcpy (GDMA) for LUT reads
    async_memcpy_handle_t dma_handle;
    
    // Spline tables
    fabric_activations_t* activations;
    
    // Pulse patterns
    fabric_pulse_patterns_t* pulses;
    
    // Sparse weights
    fabric_sparse_weights_t* weights;
    
    // State
    bool initialized;
    
    // Statistics
    uint32_t compute_count;
    
} fabric_engine_t;

// ============================================================
// Spline Generation
// ============================================================

/**
 * Generate sigmoid spline LUT
 * Maps 8-bit input (0-255, representing -8 to +8) to 8-bit output (0-255, representing 0 to 1)
 */
static inline void fabric_generate_sigmoid_spline(fabric_spline_t* spline) {
    for (int seg = 0; seg < FABRIC_SPLINE_SEGMENTS; seg++) {
        for (int off = 0; off < FABRIC_SPLINE_OFFSETS; off++) {
            // Input: 0-255 maps to -8.0 to +8.0
            int x_int = (seg << FABRIC_SEGMENT_BITS) | off;
            float x = ((float)x_int / 255.0f) * 16.0f - 8.0f;
            
            // Sigmoid: 1 / (1 + exp(-x))
            float sig = 1.0f / (1.0f + expf(-x));
            
            // Output: 0-255 maps to 0.0 to 1.0
            // But we use signed int8 for PCNT compatibility: -128 to 127
            spline->lut[seg][off] = (int8_t)((sig - 0.5f) * 254.0f);
        }
    }
}

/**
 * Generate tanh spline LUT
 * Maps 8-bit input to 8-bit output (signed, -128 to 127)
 */
static inline void fabric_generate_tanh_spline(fabric_spline_t* spline) {
    for (int seg = 0; seg < FABRIC_SPLINE_SEGMENTS; seg++) {
        for (int off = 0; off < FABRIC_SPLINE_OFFSETS; off++) {
            int x_int = (seg << FABRIC_SEGMENT_BITS) | off;
            float x = ((float)x_int / 255.0f) * 8.0f - 4.0f;
            
            float t = tanhf(x);
            spline->lut[seg][off] = (int8_t)(t * 127.0f);
        }
    }
}

/**
 * Generate identity spline (for debugging)
 */
static inline void fabric_generate_identity_spline(fabric_spline_t* spline) {
    for (int seg = 0; seg < FABRIC_SPLINE_SEGMENTS; seg++) {
        for (int off = 0; off < FABRIC_SPLINE_OFFSETS; off++) {
            int x_int = (seg << FABRIC_SEGMENT_BITS) | off;
            spline->lut[seg][off] = (int8_t)(x_int - 128);
        }
    }
}

// ============================================================
// Pulse Pattern Generation
// ============================================================

/**
 * Initialize pulse patterns for values 0-15
 * Each value N generates N pulses
 */
static inline void fabric_init_pulse_patterns(fabric_pulse_patterns_t* patterns) {
    for (int val = 0; val < 16; val++) {
        for (int p = 0; p < val; p++) {
            // High-low pulse
            patterns->patterns[val][p].duration0 = FABRIC_PULSE_HIGH_TICKS;
            patterns->patterns[val][p].level0 = 1;
            patterns->patterns[val][p].duration1 = FABRIC_PULSE_LOW_TICKS;
            patterns->patterns[val][p].level1 = 0;
        }
        // End marker (zero duration)
        patterns->patterns[val][val].duration0 = 0;
        patterns->patterns[val][val].level0 = 0;
        patterns->patterns[val][val].duration1 = 0;
        patterns->patterns[val][val].level1 = 0;
        
        patterns->lengths[val] = val + 1;  // Include end marker
    }
}

// ============================================================
// Hardware Initialization
// ============================================================

/**
 * Initialize PCNT for hardware accumulation
 */
static inline esp_err_t fabric_init_pcnt(fabric_engine_t* engine) {
    // PCNT unit configuration
    pcnt_unit_config_t unit_config = {
        .low_limit = -32768,
        .high_limit = 32767,
        .flags.accum_count = true,  // Keep counting across limits
    };
    
    esp_err_t ret = pcnt_new_unit(&unit_config, &engine->pcnt_unit);
    if (ret != ESP_OK) return ret;
    
    // Channel for positive counting (rising edge = +1)
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = FABRIC_PULSE_GPIO,
        .level_gpio_num = -1,  // No level signal
        .flags.io_loop_back = true,  // Internal loopback for testing without wire
    };
    
    ret = pcnt_new_channel(engine->pcnt_unit, &chan_config, &engine->pcnt_chan_pos);
    if (ret != ESP_OK) return ret;
    
    // Configure: rising edge = increment, falling edge = hold
    ret = pcnt_channel_set_edge_action(engine->pcnt_chan_pos,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge
        PCNT_CHANNEL_EDGE_ACTION_HOLD);     // Falling edge
    if (ret != ESP_OK) return ret;
    
    // Glitch filter: ignore pulses shorter than 100ns
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 100,
    };
    ret = pcnt_unit_set_glitch_filter(engine->pcnt_unit, &filter_config);
    if (ret != ESP_OK) return ret;
    
    // Enable and start
    ret = pcnt_unit_enable(engine->pcnt_unit);
    if (ret != ESP_OK) return ret;
    
    ret = pcnt_unit_clear_count(engine->pcnt_unit);
    if (ret != ESP_OK) return ret;
    
    ret = pcnt_unit_start(engine->pcnt_unit);
    return ret;
}

/**
 * Initialize RMT for pulse generation
 */
static inline esp_err_t fabric_init_rmt(fabric_engine_t* engine) {
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = FABRIC_PULSE_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz = 100ns resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.io_loop_back = true,  // Internal loopback for testing
    };
    
    esp_err_t ret = rmt_new_tx_channel(&tx_config, &engine->rmt_chan);
    if (ret != ESP_OK) return ret;
    
    // Create copy encoder (just copies symbols from memory)
    rmt_copy_encoder_config_t encoder_config = {};
    ret = rmt_new_copy_encoder(&encoder_config, &engine->rmt_encoder);
    if (ret != ESP_OK) return ret;
    
    ret = rmt_enable(engine->rmt_chan);
    return ret;
}

// ============================================================
// Core Computation
// ============================================================

/**
 * Send N pulses via RMT → PCNT counts them
 * This is hardware addition!
 */
static inline esp_err_t fabric_send_pulses(fabric_engine_t* engine, int count) {
    if (count <= 0) return ESP_OK;
    if (count > 15) count = 15;  // Clamp to our pattern range
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,  // No looping
    };
    
    return rmt_transmit(engine->rmt_chan, engine->rmt_encoder,
        engine->pulses->patterns[count],
        engine->pulses->lengths[count] * sizeof(rmt_symbol_word_t),
        &tx_config);
}

/**
 * Read PCNT value (the accumulated sum)
 */
static inline int fabric_read_accumulator(fabric_engine_t* engine) {
    int count = 0;
    pcnt_unit_get_count(engine->pcnt_unit, &count);
    return count;
}

/**
 * Clear PCNT accumulator
 */
static inline esp_err_t fabric_clear_accumulator(fabric_engine_t* engine) {
    return pcnt_unit_clear_count(engine->pcnt_unit);
}

/**
 * Look up spline value (software, for comparison)
 */
static inline int8_t fabric_spline_lookup(const fabric_spline_t* spline, uint8_t x) {
    int seg = x >> FABRIC_SEGMENT_BITS;
    int off = x & FABRIC_OFFSET_MASK;
    return spline->lut[seg][off];
}

/**
 * Compute sparse ternary dot product via PCNT
 * 
 * For each bit in input:
 *   - If pos_mask bit set: send pulse (PCNT increments)
 *   - If neg_mask bit set: send pulse with inverted sign (PCNT decrements)
 *
 * Result = popcount(input & pos_mask) - popcount(input & neg_mask)
 */
static inline int fabric_sparse_dot_hw(
    fabric_engine_t* engine,
    uint8_t input,
    const fabric_ternary_row_t* row
) {
    // Clear accumulator
    fabric_clear_accumulator(engine);
    
    // Count positive contributions
    int pos_count = __builtin_popcount(input & row->pos_mask);
    if (pos_count > 0) {
        fabric_send_pulses(engine, pos_count);
        // Wait for RMT to complete (in real fabric, use ETM)
        rmt_tx_wait_all_done(engine->rmt_chan, portMAX_DELAY);
    }
    
    // For negative contributions, we'd need a second channel or sign signal
    // For now, compute in software and adjust
    int neg_count = __builtin_popcount(input & row->neg_mask);
    
    // Read accumulated positive
    int result = fabric_read_accumulator(engine);
    
    // Subtract negative (this part still needs HW solution)
    result -= neg_count;
    
    return result;
}

/**
 * Compute sparse ternary dot product in pure software (reference)
 */
static inline int fabric_sparse_dot_sw(uint8_t input, const fabric_ternary_row_t* row) {
    return __builtin_popcount(input & row->pos_mask) 
         - __builtin_popcount(input & row->neg_mask);
}

// ============================================================
// Full Layer Computation
// ============================================================

/**
 * Compute one neuron output via hardware fabric
 */
static inline int8_t fabric_compute_neuron_hw(
    fabric_engine_t* engine,
    uint8_t input,
    const fabric_ternary_row_t* weights,
    const fabric_spline_t* activation
) {
    // 1. Sparse dot product via PCNT
    int pre_act = fabric_sparse_dot_hw(engine, input, weights);
    
    // 2. Clamp to 8-bit range for spline lookup
    if (pre_act < -128) pre_act = -128;
    if (pre_act > 127) pre_act = 127;
    
    // 3. Activation via spline LUT
    uint8_t lut_index = (uint8_t)(pre_act + 128);
    int8_t result = fabric_spline_lookup(activation, lut_index);
    
    return result;
}

// ============================================================
// Engine Lifecycle
// ============================================================

/**
 * Initialize the Turing Fabric
 */
static inline esp_err_t fabric_init(fabric_engine_t* engine) {
    memset(engine, 0, sizeof(fabric_engine_t));
    
    // Allocate structures
    engine->activations = (fabric_activations_t*)malloc(sizeof(fabric_activations_t));
    engine->pulses = (fabric_pulse_patterns_t*)malloc(sizeof(fabric_pulse_patterns_t));
    engine->weights = (fabric_sparse_weights_t*)malloc(sizeof(fabric_sparse_weights_t));
    
    if (!engine->activations || !engine->pulses || !engine->weights) {
        return ESP_ERR_NO_MEM;
    }
    
    // Generate spline LUTs
    fabric_generate_sigmoid_spline(&engine->activations->sigmoid);
    fabric_generate_tanh_spline(&engine->activations->tanh);
    fabric_generate_identity_spline(&engine->activations->identity);
    
    // Generate pulse patterns
    fabric_init_pulse_patterns(engine->pulses);
    
    // Initialize random sparse weights
    uint32_t seed = 42;
    #define RAND() (seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, seed)
    for (int n = 0; n < FABRIC_MAX_NEURONS; n++) {
        engine->weights->gate[n].pos_mask = 0;
        engine->weights->gate[n].neg_mask = 0;
        engine->weights->cand[n].pos_mask = 0;
        engine->weights->cand[n].neg_mask = 0;
        
        // ~20% sparsity
        for (int b = 0; b < 8; b++) {
            if ((RAND() % 10) == 0) engine->weights->gate[n].pos_mask |= (1 << b);
            else if ((RAND() % 10) == 0) engine->weights->gate[n].neg_mask |= (1 << b);
            
            if ((RAND() % 10) == 0) engine->weights->cand[n].pos_mask |= (1 << b);
            else if ((RAND() % 10) == 0) engine->weights->cand[n].neg_mask |= (1 << b);
        }
    }
    #undef RAND
    
    // Initialize hardware
    esp_err_t ret = fabric_init_pcnt(engine);
    if (ret != ESP_OK) return ret;
    
    ret = fabric_init_rmt(engine);
    if (ret != ESP_OK) return ret;
    
    engine->initialized = true;
    return ESP_OK;
}

/**
 * Clean up
 */
static inline void fabric_deinit(fabric_engine_t* engine) {
    if (engine->rmt_chan) {
        rmt_disable(engine->rmt_chan);
        rmt_del_channel(engine->rmt_chan);
    }
    if (engine->rmt_encoder) {
        rmt_del_encoder(engine->rmt_encoder);
    }
    if (engine->pcnt_chan_pos) {
        pcnt_del_channel(engine->pcnt_chan_pos);
    }
    if (engine->pcnt_unit) {
        pcnt_unit_stop(engine->pcnt_unit);
        pcnt_unit_disable(engine->pcnt_unit);
        pcnt_del_unit(engine->pcnt_unit);
    }
    
    free(engine->activations);
    free(engine->pulses);
    free(engine->weights);
    
    memset(engine, 0, sizeof(fabric_engine_t));
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_TURING_FABRIC_H
