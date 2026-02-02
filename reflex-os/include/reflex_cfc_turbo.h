/**
 * reflex_cfc_turbo.h - TURBO CfC: Bit-Parallel Neural Network
 *
 * Radical optimization: process ALL neurons simultaneously using
 * bit-parallel operations on transposed weight matrices.
 *
 * Key insight: Instead of looping over neurons, we loop over INPUT BITS.
 * Each input bit affects all neurons in parallel.
 *
 * Standard approach (64 neurons, 64 inputs):
 *   for n in neurons:        # 64 iterations
 *     for i in inputs:       # 64 bits
 *       accumulate
 *   Total: 64 * 64 = 4096 operations, but only 64 at a time
 *
 * Bit-parallel approach:
 *   for i in inputs:         # 64 iterations
 *     all_neurons += input_bit[i] ? column[i] : 0
 *   Total: 64 operations, ALL 64 neurons updated simultaneously
 *
 * Expected: 10-20x speedup. Target: ~50-100 kHz.
 */

#ifndef REFLEX_CFC_TURBO_H
#define REFLEX_CFC_TURBO_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex_cfc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Transposed Weight Layout for Bit-Parallel Processing
// ============================================================

/**
 * Instead of: weights[neuron][input_bit] = ternary
 * We store:   pos_column[input_bit] = which neurons have +1 for this input
 *             neg_column[input_bit] = which neurons have -1 for this input
 *
 * This is the TRANSPOSE of the weight matrix, stored as bit vectors.
 */
typedef struct {
    // For each of 64 input bits: which of 64 neurons have +1 weight
    uint64_t pos_columns[64];
    // For each of 64 input bits: which of 64 neurons have -1 weight
    uint64_t neg_columns[64];
    // Bias per neuron (still 64 values)
    int8_t bias[64];
} cfc_transposed_weights_t;

/**
 * Turbo CfC layer with transposed weights
 */
typedef struct {
    cfc_transposed_weights_t f_weights;  // Time constant pathway
    cfc_transposed_weights_t g_weights;  // Target state pathway
    cfc_transposed_weights_t out_weights; // Output projection
    
    uint64_t hidden;  // Hidden state (64 bits)
} cfc_turbo_layer_t;

// ============================================================
// Bit-Parallel Accumulation
// ============================================================

/**
 * Accumulate contributions from all input bits to all neurons simultaneously.
 * 
 * For each input bit that is SET:
 *   - Add +1 to all neurons in pos_column[bit]
 *   - Add -1 to all neurons in neg_column[bit]
 *
 * Returns 64 accumulators packed cleverly.
 *
 * TRICK: Instead of actual addition, we count bits using multiple passes.
 * Final activation is: popcount(pos_contributions) - popcount(neg_contributions)
 */

/**
 * Bit-parallel matmul: compute all 64 neuron activations at once
 *
 * Returns: bit vector where bit n = (activation[n] > threshold)
 */
static inline uint64_t cfc_turbo_matmul(
    uint64_t input,
    const cfc_transposed_weights_t* weights,
    uint8_t threshold_offset
) {
    // For each neuron, we need to count:
    //   pos_count = number of input bits where (input_bit=1 AND pos_weight=1)
    //   neg_count = number of input bits where (input_bit=1 AND neg_weight=1)
    //   pre_act = pos_count - neg_count + bias
    //
    // With transposed weights, for input bit i:
    //   pos_column[i] tells us which neurons have +1 weight for input i
    //   If input bit i is set, those neurons get +1
    //
    // We need to accumulate across all 64 input bits per neuron.
    // This is still O(64) for 64 neurons, but with SIMD-friendly access.
    
    // Accumulator arrays (one per neuron, but processed in parallel)
    uint8_t pos_count[64] = {0};
    uint8_t neg_count[64] = {0};
    
    // Process input bits one at a time
    uint64_t in = input;
    for (int bit = 0; bit < 64 && in; bit++) {
        if (in & 1) {
            // This input bit is set - update affected neurons
            uint64_t pos_mask = weights->pos_columns[bit];
            uint64_t neg_mask = weights->neg_columns[bit];
            
            // Increment counters for affected neurons
            // (This is the expensive part - still a loop)
            for (int n = 0; n < 64; n++) {
                if (pos_mask & (1ULL << n)) pos_count[n]++;
                if (neg_mask & (1ULL << n)) neg_count[n]++;
            }
        }
        in >>= 1;
    }
    
    // Compute final activations and threshold
    uint64_t result = 0;
    for (int n = 0; n < 64; n++) {
        int16_t pre_act = (int16_t)pos_count[n] - (int16_t)neg_count[n] + 
                          weights->bias[n] + CFC_PREACT_OFFSET;
        if (pre_act < 0) pre_act = 0;
        if (pre_act > CFC_PREACT_MAX) pre_act = CFC_PREACT_MAX;
        
        if (sigmoid_lut[pre_act] > (128 + threshold_offset)) {
            result |= (1ULL << n);
        }
    }
    
    return result;
}

/**
 * ULTRA-optimized: Use bit manipulation for counting
 * 
 * Key insight: We can use population count and bit operations
 * to avoid the inner neuron loop entirely.
 */
static inline uint64_t cfc_turbo_matmul_ultra(
    uint64_t input,
    const cfc_transposed_weights_t* weights,
    uint8_t threshold
) {
    // This version trades memory for speed
    // We'll compute a simplified binary output directly
    
    // Count positive contributions per neuron
    // pos_total[n] = sum over all input bits i of: input[i] AND pos_columns[i][n]
    //              = popcount(input AND transpose_row[n])
    // But we have columns, not rows...
    //
    // Actually, let's think differently.
    // For neuron n, we want: popcount(input AND original_pos_mask[n])
    // The transposed format doesn't help here directly.
    
    // Let's go back to counting but with better memory access
    uint64_t pos_hits[64];
    uint64_t neg_hits[64];
    
    // Initialize with input bits masked by each column
    for (int b = 0; b < 64; b++) {
        // If input bit b is set, pos_columns[b] neurons get +1
        uint64_t bit_set = (input >> b) & 1;
        pos_hits[b] = bit_set ? weights->pos_columns[b] : 0;
        neg_hits[b] = bit_set ? weights->neg_columns[b] : 0;
    }
    
    // Now reduce: for each neuron, count how many columns had it set
    // This is essentially a vertical popcount across 64 uint64_t values
    // Where each bit position represents one neuron
    
    // Use parallel prefix popcount across the bit positions
    // This is a well-known SIMD pattern
    
    // For each neuron n, we want: sum of bit n across pos_hits[0..63]
    // This is exactly what we need: vertical sum of 64-bit columns
    
    // Simplified: just loop and count (still faster due to memory layout)
    uint64_t result = 0;
    for (int n = 0; n < 64; n++) {
        uint8_t pos_count = 0;
        uint8_t neg_count = 0;
        uint64_t mask = 1ULL << n;
        
        for (int b = 0; b < 64; b++) {
            if (pos_hits[b] & mask) pos_count++;
            if (neg_hits[b] & mask) neg_count++;
        }
        
        int16_t pre_act = (int16_t)pos_count - (int16_t)neg_count + 
                          weights->bias[n] + CFC_PREACT_OFFSET;
        if (pre_act < 0) pre_act = 0;
        if (pre_act > CFC_PREACT_MAX) pre_act = CFC_PREACT_MAX;
        
        if (sigmoid_lut[pre_act] > threshold) {
            result |= mask;
        }
    }
    
    return result;
}

/**
 * BLAZING: Approximate bit-parallel computation
 *
 * Insight: For sparse ternary (90% zeros), most columns are nearly empty.
 * We can use a MUCH simpler approximation:
 *   - Positive neurons: OR of all pos_columns where input bit is set
 *   - Negative neurons: OR of all neg_columns where input bit is set  
 *   - Result ≈ positive AND NOT negative (with some threshold)
 *
 * This loses precision but is O(64) with NO inner loop.
 */
static inline uint64_t cfc_turbo_matmul_blazing(
    uint64_t input,
    const cfc_transposed_weights_t* weights
) {
    uint64_t any_positive = 0;  // Neurons that got ANY positive signal
    uint64_t any_negative = 0;  // Neurons that got ANY negative signal
    
    // Single pass through input bits
    uint64_t in = input;
    const uint64_t* pos_ptr = weights->pos_columns;
    const uint64_t* neg_ptr = weights->neg_columns;
    
    // Unrolled for speed
    #define PROCESS_BIT(b) \
        if (in & (1ULL << (b))) { \
            any_positive |= pos_ptr[b]; \
            any_negative |= neg_ptr[b]; \
        }
    
    PROCESS_BIT(0);  PROCESS_BIT(1);  PROCESS_BIT(2);  PROCESS_BIT(3);
    PROCESS_BIT(4);  PROCESS_BIT(5);  PROCESS_BIT(6);  PROCESS_BIT(7);
    PROCESS_BIT(8);  PROCESS_BIT(9);  PROCESS_BIT(10); PROCESS_BIT(11);
    PROCESS_BIT(12); PROCESS_BIT(13); PROCESS_BIT(14); PROCESS_BIT(15);
    PROCESS_BIT(16); PROCESS_BIT(17); PROCESS_BIT(18); PROCESS_BIT(19);
    PROCESS_BIT(20); PROCESS_BIT(21); PROCESS_BIT(22); PROCESS_BIT(23);
    PROCESS_BIT(24); PROCESS_BIT(25); PROCESS_BIT(26); PROCESS_BIT(27);
    PROCESS_BIT(28); PROCESS_BIT(29); PROCESS_BIT(30); PROCESS_BIT(31);
    PROCESS_BIT(32); PROCESS_BIT(33); PROCESS_BIT(34); PROCESS_BIT(35);
    PROCESS_BIT(36); PROCESS_BIT(37); PROCESS_BIT(38); PROCESS_BIT(39);
    PROCESS_BIT(40); PROCESS_BIT(41); PROCESS_BIT(42); PROCESS_BIT(43);
    PROCESS_BIT(44); PROCESS_BIT(45); PROCESS_BIT(46); PROCESS_BIT(47);
    PROCESS_BIT(48); PROCESS_BIT(49); PROCESS_BIT(50); PROCESS_BIT(51);
    PROCESS_BIT(52); PROCESS_BIT(53); PROCESS_BIT(54); PROCESS_BIT(55);
    PROCESS_BIT(56); PROCESS_BIT(57); PROCESS_BIT(58); PROCESS_BIT(59);
    PROCESS_BIT(60); PROCESS_BIT(61); PROCESS_BIT(62); PROCESS_BIT(63);
    
    #undef PROCESS_BIT
    
    // Simple decision: positive wins unless negative also present
    // Neurons with positive and no negative -> 1
    // Neurons with negative -> 0 (conservative)
    // Neurons with both -> use positive (aggressive)
    return any_positive & (~any_negative);
}

/**
 * FASTEST: Loop version of blazing for compiler optimization
 */
static inline uint64_t cfc_turbo_matmul_fastest(
    uint64_t input,
    const cfc_transposed_weights_t* weights
) {
    uint64_t any_positive = 0;
    uint64_t any_negative = 0;
    
    // Let compiler vectorize this
    for (int b = 0; b < 64; b++) {
        uint64_t bit_set = ((input >> b) & 1) ? ~0ULL : 0ULL;
        any_positive |= weights->pos_columns[b] & bit_set;
        any_negative |= weights->neg_columns[b] & bit_set;
    }
    
    return any_positive & (~any_negative);
}

// ============================================================
// Turbo Forward Pass
// ============================================================

/**
 * Ultra-fast forward pass using bit-parallel operations
 */
static inline void cfc_turbo_forward(
    cfc_turbo_layer_t* layer,
    uint64_t input,
    uint64_t* output
) {
    uint64_t combined = input | layer->hidden;
    
    // F pathway: which neurons should update
    uint64_t f_gate = cfc_turbo_matmul_fastest(combined, &layer->f_weights);
    
    // G pathway: target state for each neuron
    uint64_t g_bits = cfc_turbo_matmul_fastest(combined, &layer->g_weights);
    
    // CfC dynamics: blend hidden with g based on f gate
    layer->hidden = (f_gate & g_bits) | ((~f_gate) & layer->hidden);
    
    // Output projection
    *output = cfc_turbo_matmul_fastest(layer->hidden, &layer->out_weights);
}

/**
 * Forward pass with precise (non-approximate) matmul
 */
static inline void cfc_turbo_forward_precise(
    cfc_turbo_layer_t* layer,
    uint64_t input,
    uint64_t* output
) {
    uint64_t combined = input | layer->hidden;
    
    // F pathway
    uint64_t f_gate = cfc_turbo_matmul(combined, &layer->f_weights, 0);
    
    // G pathway  
    uint64_t g_bits = cfc_turbo_matmul(combined, &layer->g_weights, -56);  // threshold at +8
    
    // CfC dynamics
    layer->hidden = (f_gate & g_bits) | ((~f_gate) & layer->hidden);
    
    // Output projection
    *output = cfc_turbo_matmul(layer->hidden, &layer->out_weights, 0);
}

// ============================================================
// Initialization
// ============================================================

/**
 * Transpose standard weights to column format
 */
static inline void cfc_turbo_transpose_weights(
    cfc_transposed_weights_t* out,
    const cfc_ternary_weights_t* in,
    const int8_t* bias,
    int num_neurons
) {
    // Clear output
    memset(out->pos_columns, 0, sizeof(out->pos_columns));
    memset(out->neg_columns, 0, sizeof(out->neg_columns));
    
    // Transpose: for each neuron n and input bit b,
    // if in[n].pos_mask has bit b set, set bit n in out->pos_columns[b]
    for (int n = 0; n < num_neurons && n < 64; n++) {
        uint64_t pos_mask, neg_mask;
        memcpy(&pos_mask, in[n].pos_mask, 8);
        memcpy(&neg_mask, in[n].neg_mask, 8);
        
        for (int b = 0; b < 64; b++) {
            if (pos_mask & (1ULL << b)) {
                out->pos_columns[b] |= (1ULL << n);
            }
            if (neg_mask & (1ULL << b)) {
                out->neg_columns[b] |= (1ULL << n);
            }
        }
        
        out->bias[n] = bias[n];
    }
}

/**
 * Initialize turbo layer from standard CfC layer
 */
static inline void cfc_turbo_init_from_layer(
    cfc_turbo_layer_t* turbo,
    const cfc_layer_t* layer
) {
    cfc_turbo_transpose_weights(&turbo->f_weights, layer->f_weights, layer->f_bias, CFC_NUM_NEURONS);
    cfc_turbo_transpose_weights(&turbo->g_weights, layer->g_weights, layer->g_bias, CFC_NUM_NEURONS);
    cfc_turbo_transpose_weights(&turbo->out_weights, layer->out_weights, layer->out_bias, CFC_OUTPUT_BITS);
    
    memcpy(&turbo->hidden, layer->hidden, 8);
}

/**
 * Initialize with random weights (transposed format)
 */
static inline void cfc_turbo_init_random(cfc_turbo_layer_t* layer, uint32_t seed) {
    uint32_t state = seed;
    #define RAND() (state ^= state << 13, state ^= state >> 17, state ^= state << 5, state)
    
    // Initialize transposed weights directly
    // ~5% positive, ~5% negative per column
    
    // F weights
    for (int b = 0; b < 64; b++) {
        layer->f_weights.pos_columns[b] = 0;
        layer->f_weights.neg_columns[b] = 0;
        for (int n = 0; n < 64; n++) {
            uint32_t r = RAND() % 100;
            if (r < 5) {
                layer->f_weights.pos_columns[b] |= (1ULL << n);
            } else if (r < 10) {
                layer->f_weights.neg_columns[b] |= (1ULL << n);
            }
        }
    }
    for (int n = 0; n < 64; n++) {
        layer->f_weights.bias[n] = (int8_t)((RAND() % 17) - 8);
    }
    
    // G weights
    for (int b = 0; b < 64; b++) {
        layer->g_weights.pos_columns[b] = 0;
        layer->g_weights.neg_columns[b] = 0;
        for (int n = 0; n < 64; n++) {
            uint32_t r = RAND() % 100;
            if (r < 5) {
                layer->g_weights.pos_columns[b] |= (1ULL << n);
            } else if (r < 10) {
                layer->g_weights.neg_columns[b] |= (1ULL << n);
            }
        }
    }
    for (int n = 0; n < 64; n++) {
        layer->g_weights.bias[n] = (int8_t)((RAND() % 17) - 8);
    }
    
    // Output weights
    for (int b = 0; b < 64; b++) {
        layer->out_weights.pos_columns[b] = 0;
        layer->out_weights.neg_columns[b] = 0;
        for (int n = 0; n < 64; n++) {
            uint32_t r = RAND() % 100;
            if (r < 10) {
                layer->out_weights.pos_columns[b] |= (1ULL << n);
            } else if (r < 20) {
                layer->out_weights.neg_columns[b] |= (1ULL << n);
            }
        }
    }
    for (int n = 0; n < 64; n++) {
        layer->out_weights.bias[n] = (int8_t)((RAND() % 17) - 8);
    }
    
    layer->hidden = 0;
    
    #undef RAND
}

/**
 * Get memory statistics
 */
static inline uint32_t cfc_turbo_memory_size(void) {
    return sizeof(cfc_turbo_layer_t);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_CFC_TURBO_H
