/**
 * reflex_cfc.h - Binary Ternary CfC: Liquid Neural Network in 3KB
 *
 * A Closed-form Continuous-time neural network that runs WITHOUT the CPU.
 * 
 * Key constraints:
 *   - Ternary weights: {-1, 0, +1} stored as bit masks
 *   - Binary activations: {0, 1}
 *   - Operations: AND, POPCOUNT, SUB, LUT - NO MULTIPLY
 *
 * Math per neuron:
 *   count_pos = popcount(input AND pos_mask)
 *   count_neg = popcount(input AND neg_mask)
 *   pre_act = count_pos - count_neg + bias
 *   output = sigmoid_lut[pre_act] > threshold
 *
 * Memory: ~3KB for 64 neurons
 * Speed: ~10-50 MHz (LUT access limited)
 * Power: μW (no CPU needed for inference)
 */

#ifndef REFLEX_CFC_H
#define REFLEX_CFC_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define CFC_NUM_NEURONS     64      // Hidden layer size
#define CFC_INPUT_BITS      64      // Input vector width
#define CFC_OUTPUT_BITS     64      // Output vector width
#define CFC_BYTES_PER_MASK  8       // 64 bits = 8 bytes

// Pre-activation range: [-64, +64] needs 7 bits + sign
// We'll use offset encoding: 0-128 maps to -64 to +64
#define CFC_PREACT_OFFSET   64
#define CFC_PREACT_MAX      128

// ============================================================
// Lookup Tables (NO MULTIPLY - just indexed reads)
// ============================================================

// Popcount LUT: popcount_lut[byte] = number of 1 bits
static const uint8_t popcount_lut[256] = {
    0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8
};

// Sigmoid LUT: maps pre-activation [-64..+64] (stored as 0..128) to [0..255]
// sigmoid(x) = 255 / (1 + exp(-x/8))  -- scaled for 8-bit output
static const uint8_t sigmoid_lut[CFC_PREACT_MAX + 1] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,
    1,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  4,  4,  5,  5,
    6,  6,  7,  8,  9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25,
    28, 30, 33, 37, 40, 44, 48, 53, 58, 63, 69, 75, 82, 89, 96,104,
   112,120,128,136,144,151,159,166,173,180,186,192,197,202,207,212,
   217,221,225,228,232,235,237,240,242,244,246,248,249,251,252,253,
   253,254,254,254,255,255,255,255,255,255,255,255,255,255,255,255,
   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
   255
};

// ============================================================
// Ternary Weight Masks
// ============================================================

/**
 * Ternary weights for one neuron:
 *   - pos_mask: bits set where weight = +1
 *   - neg_mask: bits set where weight = -1
 *   - (neither set = weight is 0)
 */
typedef struct {
    uint8_t pos_mask[CFC_BYTES_PER_MASK];   // +1 weights
    uint8_t neg_mask[CFC_BYTES_PER_MASK];   // -1 weights
} cfc_ternary_weights_t;

// ============================================================
// CfC Layer
// ============================================================

/**
 * Single CfC layer with ternary weights and binary activations
 */
typedef struct {
    // Time constant pathway (f): determines how fast to adapt
    cfc_ternary_weights_t f_weights[CFC_NUM_NEURONS];
    int8_t f_bias[CFC_NUM_NEURONS];
    
    // Target state pathway (g): determines where to go
    cfc_ternary_weights_t g_weights[CFC_NUM_NEURONS];
    int8_t g_bias[CFC_NUM_NEURONS];
    
    // Hidden state (binary, 64 bits = 8 bytes)
    uint8_t hidden[CFC_BYTES_PER_MASK];
    
    // Time since last update (for continuous-time dynamics)
    uint16_t time_delta;
    
    // Output projection (ternary weights)
    cfc_ternary_weights_t out_weights[CFC_OUTPUT_BITS];
    int8_t out_bias[CFC_OUTPUT_BITS];
    
} cfc_layer_t;

// ============================================================
// Core Operations (NO MULTIPLY)
// ============================================================

/**
 * Popcount for 64-bit vector (8 bytes)
 * Uses LUT - 8 lookups, 7 adds
 */
static inline uint8_t popcount64(const uint8_t* data) {
    return popcount_lut[data[0]] + popcount_lut[data[1]] +
           popcount_lut[data[2]] + popcount_lut[data[3]] +
           popcount_lut[data[4]] + popcount_lut[data[5]] +
           popcount_lut[data[6]] + popcount_lut[data[7]];
}

/**
 * AND two 64-bit vectors
 */
static inline void and64(const uint8_t* a, const uint8_t* b, uint8_t* result) {
    result[0] = a[0] & b[0];
    result[1] = a[1] & b[1];
    result[2] = a[2] & b[2];
    result[3] = a[3] & b[3];
    result[4] = a[4] & b[4];
    result[5] = a[5] & b[5];
    result[6] = a[6] & b[6];
    result[7] = a[7] & b[7];
}

/**
 * Compute one ternary neuron:
 *   pre_act = popcount(input AND pos_mask) - popcount(input AND neg_mask) + bias
 *   Returns pre-activation in offset encoding [0..128]
 */
static inline uint8_t ternary_neuron(
    const uint8_t* input,
    const cfc_ternary_weights_t* weights,
    int8_t bias
) {
    uint8_t temp[CFC_BYTES_PER_MASK];
    
    // Count positive contributions
    and64(input, weights->pos_mask, temp);
    int16_t pos_count = popcount64(temp);
    
    // Count negative contributions
    and64(input, weights->neg_mask, temp);
    int16_t neg_count = popcount64(temp);
    
    // Compute pre-activation with offset encoding
    int16_t pre_act = pos_count - neg_count + bias + CFC_PREACT_OFFSET;
    
    // Clamp to valid range
    if (pre_act < 0) pre_act = 0;
    if (pre_act > CFC_PREACT_MAX) pre_act = CFC_PREACT_MAX;
    
    return (uint8_t)pre_act;
}

/**
 * Apply sigmoid and threshold to get binary output
 */
static inline bool activate(uint8_t pre_act, uint8_t threshold) {
    return sigmoid_lut[pre_act] > threshold;
}

// ============================================================
// CfC Forward Pass
// ============================================================

/**
 * CfC update equation (simplified binary version):
 *
 *   σ_f = sigmoid(f(input, hidden))     -- time constant
 *   g_out = g(input, hidden)            -- target state
 *   hidden_new = blend(hidden, g_out, σ_f)
 *
 * For binary: blend becomes probabilistic or thresholded
 * We use: hidden_new[i] = (σ_f[i] > rand) ? g_out[i] : hidden[i]
 *
 * Simpler version (deterministic):
 *   hidden_new[i] = (σ_f[i] > 128) ? g_out[i] : hidden[i]
 */
static inline void cfc_forward(
    cfc_layer_t* layer,
    const uint8_t* input,       // 64-bit input vector
    uint8_t* output             // 64-bit output vector
) {
    uint8_t combined[CFC_BYTES_PER_MASK];
    uint8_t f_activations[CFC_NUM_NEURONS];
    uint8_t g_activations[CFC_NUM_NEURONS];
    uint8_t new_hidden[CFC_BYTES_PER_MASK] = {0};
    
    // Combine input and hidden state for processing
    // combined = input XOR hidden (simple combination)
    for (int i = 0; i < CFC_BYTES_PER_MASK; i++) {
        combined[i] = input[i] | layer->hidden[i];  // OR to see both
    }
    
    // Compute f (time constant) and g (target) for each neuron
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        // f pathway: how much to update
        uint8_t f_pre = ternary_neuron(combined, &layer->f_weights[n], layer->f_bias[n]);
        f_activations[n] = sigmoid_lut[f_pre];
        
        // g pathway: target state
        uint8_t g_pre = ternary_neuron(combined, &layer->g_weights[n], layer->g_bias[n]);
        g_activations[n] = g_pre;  // Keep pre-activation for thresholding
    }
    
    // Update hidden state (CfC dynamics)
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        int byte_idx = n / 8;
        int bit_idx = n % 8;
        
        // Get current hidden bit
        bool h_old = (layer->hidden[byte_idx] >> bit_idx) & 1;
        
        // Get target state (threshold g activation)
        bool g_bit = g_activations[n] > (CFC_PREACT_OFFSET + 8);  // threshold at +8
        
        // Blend: if f_activation > 128, use g_bit, else keep h_old
        bool h_new = (f_activations[n] > 128) ? g_bit : h_old;
        
        // Set new hidden bit
        if (h_new) {
            new_hidden[byte_idx] |= (1 << bit_idx);
        }
    }
    
    // Update hidden state
    memcpy(layer->hidden, new_hidden, CFC_BYTES_PER_MASK);
    
    // Compute output
    memset(output, 0, CFC_BYTES_PER_MASK);
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        uint8_t out_pre = ternary_neuron(layer->hidden, &layer->out_weights[o], layer->out_bias[o]);
        if (sigmoid_lut[out_pre] > 128) {
            output[o / 8] |= (1 << (o % 8));
        }
    }
}

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize CfC layer with random ternary weights
 * Sparsity: ~10% non-zero (5% positive, 5% negative)
 */
static inline void cfc_init_random(cfc_layer_t* layer, uint32_t seed) {
    // Simple xorshift PRNG
    uint32_t state = seed;
    #define RAND() (state ^= state << 13, state ^= state >> 17, state ^= state << 5, state)
    
    // Initialize f weights (sparse ternary)
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        memset(layer->f_weights[n].pos_mask, 0, CFC_BYTES_PER_MASK);
        memset(layer->f_weights[n].neg_mask, 0, CFC_BYTES_PER_MASK);
        
        // ~5% positive, ~5% negative
        for (int i = 0; i < CFC_INPUT_BITS; i++) {
            uint32_t r = RAND() % 100;
            if (r < 5) {
                layer->f_weights[n].pos_mask[i/8] |= (1 << (i%8));
            } else if (r < 10) {
                layer->f_weights[n].neg_mask[i/8] |= (1 << (i%8));
            }
        }
        layer->f_bias[n] = (int8_t)((RAND() % 17) - 8);  // [-8, +8]
    }
    
    // Initialize g weights (sparse ternary)
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        memset(layer->g_weights[n].pos_mask, 0, CFC_BYTES_PER_MASK);
        memset(layer->g_weights[n].neg_mask, 0, CFC_BYTES_PER_MASK);
        
        for (int i = 0; i < CFC_INPUT_BITS; i++) {
            uint32_t r = RAND() % 100;
            if (r < 5) {
                layer->g_weights[n].pos_mask[i/8] |= (1 << (i%8));
            } else if (r < 10) {
                layer->g_weights[n].neg_mask[i/8] |= (1 << (i%8));
            }
        }
        layer->g_bias[n] = (int8_t)((RAND() % 17) - 8);
    }
    
    // Initialize output weights
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        memset(layer->out_weights[o].pos_mask, 0, CFC_BYTES_PER_MASK);
        memset(layer->out_weights[o].neg_mask, 0, CFC_BYTES_PER_MASK);
        
        for (int i = 0; i < CFC_NUM_NEURONS; i++) {
            uint32_t r = RAND() % 100;
            if (r < 10) {
                layer->out_weights[o].pos_mask[i/8] |= (1 << (i%8));
            } else if (r < 20) {
                layer->out_weights[o].neg_mask[i/8] |= (1 << (i%8));
            }
        }
        layer->out_bias[o] = (int8_t)((RAND() % 17) - 8);
    }
    
    // Clear hidden state
    memset(layer->hidden, 0, CFC_BYTES_PER_MASK);
    layer->time_delta = 0;
    
    #undef RAND
}

/**
 * Initialize with zeros
 */
static inline void cfc_init_zeros(cfc_layer_t* layer) {
    memset(layer, 0, sizeof(cfc_layer_t));
}

// ============================================================
// Memory Size Calculation
// ============================================================

/**
 * Calculate total memory footprint
 */
static inline uint32_t cfc_memory_size(void) {
    uint32_t size = 0;
    
    // f weights: 64 neurons × (8 + 8) bytes masks + 64 biases
    size += CFC_NUM_NEURONS * (2 * CFC_BYTES_PER_MASK + 1);
    
    // g weights: same
    size += CFC_NUM_NEURONS * (2 * CFC_BYTES_PER_MASK + 1);
    
    // output weights: 64 outputs × (8 + 8) bytes masks + 64 biases
    size += CFC_OUTPUT_BITS * (2 * CFC_BYTES_PER_MASK + 1);
    
    // hidden state
    size += CFC_BYTES_PER_MASK;
    
    // time delta
    size += 2;
    
    // LUTs are const, not counted in instance
    
    return size;
}

// ============================================================
// Statistics
// ============================================================

typedef struct {
    uint32_t total_bytes;
    uint32_t weight_bytes;
    uint32_t state_bytes;
    uint32_t num_neurons;
    uint32_t sparsity_percent;
} cfc_stats_t;

static inline cfc_stats_t cfc_get_stats(const cfc_layer_t* layer) {
    cfc_stats_t stats;
    stats.total_bytes = cfc_memory_size();
    stats.weight_bytes = stats.total_bytes - CFC_BYTES_PER_MASK - 2;
    stats.state_bytes = CFC_BYTES_PER_MASK + 2;
    stats.num_neurons = CFC_NUM_NEURONS;
    
    // Count non-zero weights to compute sparsity
    uint32_t total_weights = CFC_NUM_NEURONS * CFC_INPUT_BITS * 2;  // f and g
    uint32_t nonzero = 0;
    
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        nonzero += popcount64(layer->f_weights[n].pos_mask);
        nonzero += popcount64(layer->f_weights[n].neg_mask);
        nonzero += popcount64(layer->g_weights[n].pos_mask);
        nonzero += popcount64(layer->g_weights[n].neg_mask);
    }
    
    stats.sparsity_percent = 100 - (nonzero * 100 / total_weights);
    
    return stats;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_CFC_H
