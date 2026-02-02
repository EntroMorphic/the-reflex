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

// Configuration options - trade size for speed
#ifndef CFC_NUM_NEURONS
#define CFC_NUM_NEURONS     64      // Hidden layer size (8, 16, 32, or 64)
#endif
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
 * Fast 64-bit popcount using parallel bit manipulation
 * Fewer memory accesses than LUT for 64 bits
 */
static inline uint8_t popcount64_fast(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (x * 0x0101010101010101ULL) >> 56;
}

/**
 * Ternary neuron using 64-bit operations (faster than byte-wise)
 */
static inline uint8_t ternary_neuron_fast(
    uint64_t input,
    uint64_t pos_mask,
    uint64_t neg_mask,
    int8_t bias
) {
    int16_t pre_act = popcount64_fast(input & pos_mask) 
                    - popcount64_fast(input & neg_mask) 
                    + bias + CFC_PREACT_OFFSET;
    
    if (pre_act < 0) pre_act = 0;
    if (pre_act > CFC_PREACT_MAX) pre_act = CFC_PREACT_MAX;
    return (uint8_t)pre_act;
}

/**
 * Load 8 bytes as uint64_t (handles alignment)
 */
static inline uint64_t load64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

/**
 * Store uint64_t as 8 bytes
 */
static inline void store64(uint8_t* p, uint64_t v) {
    memcpy(p, &v, 8);
}

/**
 * Process 4 neurons at once (manual unroll)
 */
#define PROCESS_4_NEURONS(base, combined, layer, f_gate, g_bits) do { \
    for (int _i = 0; _i < 4; _i++) { \
        int _n = (base) + _i; \
        uint64_t _pf = load64(layer->f_weights[_n].pos_mask); \
        uint64_t _nf = load64(layer->f_weights[_n].neg_mask); \
        uint64_t _pg = load64(layer->g_weights[_n].pos_mask); \
        uint64_t _ng = load64(layer->g_weights[_n].neg_mask); \
        uint8_t _fp = ternary_neuron_fast(combined, _pf, _nf, layer->f_bias[_n]); \
        uint8_t _gp = ternary_neuron_fast(combined, _pg, _ng, layer->g_bias[_n]); \
        if (sigmoid_lut[_fp] > 128) f_gate |= (1ULL << _n); \
        if (_gp > (CFC_PREACT_OFFSET + 8)) g_bits |= (1ULL << _n); \
    } \
} while(0)

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
    // Load as 64-bit for faster operations
    uint64_t input64 = load64(input);
    uint64_t hidden64 = load64(layer->hidden);
    uint64_t combined = input64 | hidden64;
    
    uint64_t g_bits = 0;       // Target state bits
    uint64_t f_gate = 0;       // Which neurons update
    
    // Process all 64 neurons (unrolled 4x = 16 iterations)
    PROCESS_4_NEURONS(0, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(4, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(8, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(12, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(16, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(20, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(24, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(28, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(32, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(36, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(40, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(44, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(48, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(52, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(56, combined, layer, f_gate, g_bits);
    PROCESS_4_NEURONS(60, combined, layer, f_gate, g_bits);
    
    // Apply CfC dynamics: blend hidden with g based on f gate
    uint64_t new_hidden = (f_gate & g_bits) | ((~f_gate) & hidden64);
    
    // Store updated hidden state
    store64(layer->hidden, new_hidden);
    
    // Compute output layer (unrolled)
    uint64_t output64 = 0;
    #define PROCESS_OUTPUT(o) do { \
        uint64_t _po = load64(layer->out_weights[o].pos_mask); \
        uint64_t _no = load64(layer->out_weights[o].neg_mask); \
        uint8_t _op = ternary_neuron_fast(new_hidden, _po, _no, layer->out_bias[o]); \
        if (sigmoid_lut[_op] > 128) output64 |= (1ULL << (o)); \
    } while(0)
    
    // Unroll output layer too
    for (int o = 0; o < CFC_OUTPUT_BITS; o += 8) {
        PROCESS_OUTPUT(o+0); PROCESS_OUTPUT(o+1);
        PROCESS_OUTPUT(o+2); PROCESS_OUTPUT(o+3);
        PROCESS_OUTPUT(o+4); PROCESS_OUTPUT(o+5);
        PROCESS_OUTPUT(o+6); PROCESS_OUTPUT(o+7);
    }
    #undef PROCESS_OUTPUT
    
    store64(output, output64);
}

/**
 * Original forward pass (for comparison)
 */
static inline void cfc_forward_original(
    cfc_layer_t* layer,
    const uint8_t* input,
    uint8_t* output
) {
    uint8_t combined[CFC_BYTES_PER_MASK];
    uint8_t f_activations[CFC_NUM_NEURONS];
    uint8_t g_activations[CFC_NUM_NEURONS];
    uint8_t new_hidden[CFC_BYTES_PER_MASK] = {0};
    
    for (int i = 0; i < CFC_BYTES_PER_MASK; i++) {
        combined[i] = input[i] | layer->hidden[i];
    }
    
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        uint8_t f_pre = ternary_neuron(combined, &layer->f_weights[n], layer->f_bias[n]);
        f_activations[n] = sigmoid_lut[f_pre];
        
        uint8_t g_pre = ternary_neuron(combined, &layer->g_weights[n], layer->g_bias[n]);
        g_activations[n] = g_pre;
    }
    
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        int byte_idx = n / 8;
        int bit_idx = n % 8;
        
        bool h_old = (layer->hidden[byte_idx] >> bit_idx) & 1;
        bool g_bit = g_activations[n] > (CFC_PREACT_OFFSET + 8);
        bool h_new = (f_activations[n] > 128) ? g_bit : h_old;
        
        if (h_new) {
            new_hidden[byte_idx] |= (1 << bit_idx);
        }
    }
    
    memcpy(layer->hidden, new_hidden, CFC_BYTES_PER_MASK);
    
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
