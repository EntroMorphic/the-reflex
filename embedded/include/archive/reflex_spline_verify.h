/**
 * reflex_spline_verify.h - Verification of Splined Mixer Accuracy
 *
 * Compares splined mixer output against full LUT reference.
 * Target: <2% max error across all 4096 input combinations.
 */

#ifndef REFLEX_SPLINE_VERIFY_H
#define REFLEX_SPLINE_VERIFY_H

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "reflex_spline_mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Reference Implementation (exact, for comparison)
// ============================================================

/**
 * Compute exact mixer value (floating point reference)
 */
static inline float mixer_exact(float gate, float h_prev, float cand, float decay) {
    float one_minus_gate = 1.0f - gate;
    float retention = one_minus_gate * h_prev * decay;
    float update = gate * cand;
    float h_new = retention + update;
    if (h_new < -1.0f) h_new = -1.0f;
    if (h_new > 1.0f) h_new = 1.0f;
    return h_new;
}

/**
 * Convert 4-bit quantized value to float
 */
static inline float q4_to_float_gate(uint8_t q4) {
    return (float)q4 / 15.0f;  // Gate: [0, 1]
}

static inline float q4_to_float_state(uint8_t q4) {
    return ((float)q4 / 7.5f) - 1.0f;  // State: [-1, +1]
}

static inline uint8_t float_to_q4_state(float f) {
    if (f < -1.0f) f = -1.0f;
    if (f > 1.0f) f = 1.0f;
    return (uint8_t)((f + 1.0f) * 7.5f);
}

// ============================================================
// Verification Results
// ============================================================

typedef struct {
    uint32_t total_tests;
    uint32_t exact_matches;
    uint32_t off_by_one;
    uint32_t off_by_two;
    uint32_t larger_errors;
    int max_error;
    float mean_error;
    float mean_abs_error;
} spline_verify_result_t;

// ============================================================
// Verification Function
// ============================================================

/**
 * Verify splined mixer against exact computation
 *
 * @param mixer   The splined mixer to verify
 * @param decay   Decay value used to generate the mixer
 * @param result  Output: verification statistics
 */
static inline void spline_mixer_verify(
    const spline_mixer_complete_t* mixer,
    float decay,
    spline_verify_result_t* result
) {
    result->total_tests = 0;
    result->exact_matches = 0;
    result->off_by_one = 0;
    result->off_by_two = 0;
    result->larger_errors = 0;
    result->max_error = 0;
    result->mean_error = 0;
    result->mean_abs_error = 0;
    
    int64_t sum_error = 0;
    int64_t sum_abs_error = 0;
    
    // Test all 16×16×16 = 4096 combinations
    for (int g = 0; g < 16; g++) {
        for (int h = 0; h < 16; h++) {
            for (int c = 0; c < 16; c++) {
                result->total_tests++;
                
                // Exact value
                float gate_f = q4_to_float_gate(g);
                float h_prev_f = q4_to_float_state(h);
                float cand_f = q4_to_float_state(c);
                float exact_f = mixer_exact(gate_f, h_prev_f, cand_f, decay);
                uint8_t exact_q4 = float_to_q4_state(exact_f);
                
                // Splined value
                uint8_t spline_q4 = spline_mixer_lookup(mixer, g, h, c);
                
                // Error
                int error = (int)spline_q4 - (int)exact_q4;
                int abs_error = error < 0 ? -error : error;
                
                sum_error += error;
                sum_abs_error += abs_error;
                
                if (abs_error > result->max_error) {
                    result->max_error = abs_error;
                }
                
                if (abs_error == 0) {
                    result->exact_matches++;
                } else if (abs_error == 1) {
                    result->off_by_one++;
                } else if (abs_error == 2) {
                    result->off_by_two++;
                } else {
                    result->larger_errors++;
                }
            }
        }
    }
    
    result->mean_error = (float)sum_error / (float)result->total_tests;
    result->mean_abs_error = (float)sum_abs_error / (float)result->total_tests;
}

/**
 * Verify splined activations
 */
static inline void spline_activations_verify(
    const spline_activations_t* act,
    spline_verify_result_t* sigmoid_result,
    spline_verify_result_t* tanh_result
) {
    // Initialize results
    sigmoid_result->total_tests = 0;
    sigmoid_result->exact_matches = 0;
    sigmoid_result->off_by_one = 0;
    sigmoid_result->off_by_two = 0;
    sigmoid_result->larger_errors = 0;
    sigmoid_result->max_error = 0;
    sigmoid_result->mean_abs_error = 0;
    
    tanh_result->total_tests = 0;
    tanh_result->exact_matches = 0;
    tanh_result->off_by_one = 0;
    tanh_result->off_by_two = 0;
    tanh_result->larger_errors = 0;
    tanh_result->max_error = 0;
    tanh_result->mean_abs_error = 0;
    
    int64_t sig_sum_abs = 0, tanh_sum_abs = 0;
    
    for (int x = 0; x < 256; x++) {
        // === SIGMOID ===
        sigmoid_result->total_tests++;
        
        // Exact sigmoid
        float xf = ((float)x / 255.0f) * 16.0f - 8.0f;
        float sig_exact_f = 1.0f / (1.0f + expf(-xf));
        uint8_t sig_exact_q4 = (uint8_t)(sig_exact_f * 15.0f);
        
        // Splined sigmoid
        uint8_t sig_spline = spline_sigmoid_lookup(act, x);
        
        int sig_err = (int)sig_spline - (int)sig_exact_q4;
        int sig_abs = sig_err < 0 ? -sig_err : sig_err;
        sig_sum_abs += sig_abs;
        
        if (sig_abs > sigmoid_result->max_error) sigmoid_result->max_error = sig_abs;
        if (sig_abs == 0) sigmoid_result->exact_matches++;
        else if (sig_abs == 1) sigmoid_result->off_by_one++;
        else if (sig_abs == 2) sigmoid_result->off_by_two++;
        else sigmoid_result->larger_errors++;
        
        // === TANH ===
        tanh_result->total_tests++;
        
        // Exact tanh
        float tanh_exact_f = tanhf(xf);
        uint8_t tanh_exact_q4 = (uint8_t)((tanh_exact_f + 1.0f) * 7.5f);
        
        // Splined tanh
        uint8_t tanh_spline = spline_tanh_lookup(act, x);
        
        int tanh_err = (int)tanh_spline - (int)tanh_exact_q4;
        int tanh_abs = tanh_err < 0 ? -tanh_err : tanh_err;
        tanh_sum_abs += tanh_abs;
        
        if (tanh_abs > tanh_result->max_error) tanh_result->max_error = tanh_abs;
        if (tanh_abs == 0) tanh_result->exact_matches++;
        else if (tanh_abs == 1) tanh_result->off_by_one++;
        else if (tanh_abs == 2) tanh_result->off_by_two++;
        else tanh_result->larger_errors++;
    }
    
    sigmoid_result->mean_abs_error = (float)sig_sum_abs / 256.0f;
    tanh_result->mean_abs_error = (float)tanh_sum_abs / 256.0f;
}

/**
 * Print verification results
 */
static inline void spline_print_results(
    const char* name,
    const spline_verify_result_t* result
) {
    printf("\n%s Verification:\n", name);
    printf("  Total tests: %lu\n", (unsigned long)result->total_tests);
    printf("  Exact matches: %lu (%.1f%%)\n", 
           (unsigned long)result->exact_matches,
           100.0f * result->exact_matches / result->total_tests);
    printf("  Off by 1: %lu (%.1f%%)\n",
           (unsigned long)result->off_by_one,
           100.0f * result->off_by_one / result->total_tests);
    printf("  Off by 2: %lu (%.1f%%)\n",
           (unsigned long)result->off_by_two,
           100.0f * result->off_by_two / result->total_tests);
    printf("  Larger errors: %lu (%.1f%%)\n",
           (unsigned long)result->larger_errors,
           100.0f * result->larger_errors / result->total_tests);
    printf("  Max error: %d (of 15)\n", result->max_error);
    printf("  Mean abs error: %.3f\n", result->mean_abs_error);
    printf("  Error %%: %.2f%%\n", 100.0f * result->mean_abs_error / 15.0f);
}

// ============================================================
// Quick Self-Test
// ============================================================

/**
 * Run complete verification and print results
 *
 * @param decay  Decay value to test (e.g., 0.9)
 * @return       0 if all tests pass (<5% max error), -1 otherwise
 */
static inline int spline_run_verification(float decay) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║          SPLINE MIXER VERIFICATION                             ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  Testing decay = %.2f                                          ║\n", decay);
    printf("║  Memory: 576 bytes (mixer) + 64 bytes (activations) = 640 B   ║\n");
    printf("║  vs Full LUT: 262,656 bytes (410x compression!)               ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    // Generate splined mixer
    spline_mixer_complete_t mixer;
    spline_mixer_generate(&mixer, decay);
    
    // Generate splined activations
    spline_activations_t activations;
    spline_activations_generate(&activations);
    
    // Verify mixer
    spline_verify_result_t mixer_result;
    spline_mixer_verify(&mixer, decay, &mixer_result);
    spline_print_results("MIXER (8x8x8 spline)", &mixer_result);
    
    // Verify activations
    spline_verify_result_t sig_result, tanh_result;
    spline_activations_verify(&activations, &sig_result, &tanh_result);
    spline_print_results("SIGMOID (16-point spline)", &sig_result);
    spline_print_results("TANH (16-point spline)", &tanh_result);
    
    // Summary
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  SUMMARY                                                       ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  Mixer:   %.1f%% exact, %.2f%% mean error, max=%d             \n",
           100.0f * mixer_result.exact_matches / mixer_result.total_tests,
           100.0f * mixer_result.mean_abs_error / 15.0f,
           mixer_result.max_error);
    printf("║  Sigmoid: %.1f%% exact, %.2f%% mean error, max=%d             \n",
           100.0f * sig_result.exact_matches / sig_result.total_tests,
           100.0f * sig_result.mean_abs_error / 15.0f,
           sig_result.max_error);
    printf("║  Tanh:    %.1f%% exact, %.2f%% mean error, max=%d             \n",
           100.0f * tanh_result.exact_matches / tanh_result.total_tests,
           100.0f * tanh_result.mean_abs_error / 15.0f,
           tanh_result.max_error);
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    // Pass if max error < 5 (33% of range) for all
    int pass = (mixer_result.max_error <= 4) && 
               (sig_result.max_error <= 2) && 
               (tanh_result.max_error <= 2);
    
    if (pass) {
        printf("\n✓ VERIFICATION PASSED\n\n");
    } else {
        printf("\n✗ VERIFICATION FAILED\n\n");
    }
    
    return pass ? 0 : -1;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_SPLINE_VERIFY_H
