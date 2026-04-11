/*
 * test_harness.h — Shared State and Declarations for Test Suite
 *
 * This header provides the shared state, constants, and function
 * declarations used across the split test files. The actual definitions
 * of shared state live in geometry_cfc_freerun.c (app_main).
 *
 * Created: April 8, 2026 (test harness split, audit remediation).
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "reflex_vdb.h"
#include "gie_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 *  CONSTANTS (used across Tests 11-14C)
 * ══════════════════════════════════════════════════════════════════ */

/* SEP_SIZE, NUM_DUMMIES, CAPTURES_PER_LOOP are in gie_engine.h */
#define NUM_TEMPLATES       4

#define NOVELTY_THRESHOLD   60
#define T11_DRAIN_MS        10
#define LP_MTFP_DIM         (LP_HIDDEN_DIM * 5)   /* 80: 16 neurons x 5 trits */
#define BIAS_SCALE          10  /* 10x resolution for integer bias decay */

/* ══════════════════════════════════════════════════════════════════
 *  SHARED STATE (defined in geometry_cfc_freerun.c)
 *
 *  sig[] is populated by Test 11 (enrollment) and read by Tests 12-14C.
 *  drain_buf[] is a scratch buffer used by all ESP-NOW-consuming tests.
 *  t12_* variables carry data from Test 12 to Test 13.
 * ══════════════════════════════════════════════════════════════════ */

extern int8_t sig[NUM_TEMPLATES][CFC_INPUT_DIM];
extern espnow_rx_entry_t drain_buf[32];

/* TEST 12 -> TEST 13 handoff (sign-space) */
extern int8_t  t12_mean1[LP_HIDDEN_DIM];
extern int8_t  t12_mean2[LP_HIDDEN_DIM];
extern int     t12_p1p3_result;
extern int     t12_p1p2_result;
extern int     t12_n1, t12_n2;

/* TEST 12 -> TEST 13 handoff (MTFP-space) */
extern int8_t  t12_mean1_mtfp[LP_MTFP_DIM];
extern int8_t  t12_mean2_mtfp[LP_MTFP_DIM];

/* ══════════════════════════════════════════════════════════════════
 *  MTFP DOT ENCODER — 5 trits per LP neuron
 *
 *  Encodes a raw dot product (integer, typically [-48, +48]) into
 *  5 trits: [sign, exp0, exp1, mant0, mant1].
 *
 *  Defined as static inline in the header so each translation unit
 *  gets its own copy without linker conflicts.
 * ══════════════════════════════════════════════════════════════════ */

/* Scale boundaries: |dot| ranges for each exponent level.
 * __attribute__((unused)) suppresses warnings in TUs that include
 * this header but don't call the MTFP encoder. */
__attribute__((unused))
static const int mtfp_dot_lo[] = {0,  1,  4,  9, 16, 25, 36, 49};
__attribute__((unused))
static const int mtfp_dot_hi[] = {0,  3,  8, 15, 24, 35, 48, 99};
__attribute__((unused))
static const int8_t mtfp_dot_exp0[] = {-1, -1, -1,  0,  0,  0,  1,  1};
__attribute__((unused))
static const int8_t mtfp_dot_exp1[] = {-1,  0,  1, -1,  0,  1, -1,  0};
#define MTFP_DOT_SCALES 8

__attribute__((unused))
static inline void encode_lp_dot_mtfp(int dot, int8_t *out) {
    /* Trit 0: sign */
    out[0] = (dot > 0) ? 1 : (dot < 0) ? -1 : 0;

    int mag = (dot > 0) ? dot : -dot;

    /* Find scale */
    int s = 0;
    for (int i = 0; i < MTFP_DOT_SCALES; i++) {
        if (mag >= mtfp_dot_lo[i] && mag <= mtfp_dot_hi[i]) { s = i; break; }
        if (mag < mtfp_dot_lo[i]) { s = (i > 0) ? i - 1 : 0; break; }
        s = MTFP_DOT_SCALES - 1;
    }

    /* Trits 1-2: exponent */
    out[1] = mtfp_dot_exp0[s];
    out[2] = mtfp_dot_exp1[s];

    /* Trits 3-4: mantissa (position within scale range) */
    int range = mtfp_dot_hi[s] - mtfp_dot_lo[s];
    if (range <= 0) {
        out[3] = 0;
        out[4] = 0;
    } else {
        int pos = mag - mtfp_dot_lo[s];
        /* 2 trits -> 9 states, but we use 3 levels per trit (lower/mid/upper) */
        out[3] = (pos * 3 < range) ? -1 : (pos * 3 > range * 2) ? 1 : 0;
        /* Second mantissa trit: finer position within the third */
        int sub_range = (range + 2) / 3;
        int sub_pos = pos % (sub_range > 0 ? sub_range : 1);
        out[4] = (sub_pos * 3 < sub_range) ? -1
               : (sub_pos * 3 > sub_range * 2) ? 1 : 0;
    }
}

/* Encode all 16 LP neuron dots into 80-trit MTFP vector */
__attribute__((unused))
static inline void encode_lp_mtfp(const int32_t *dots_f, int8_t *mtfp_out) {
    for (int n = 0; n < LP_HIDDEN_DIM; n++)
        encode_lp_dot_mtfp((int)dots_f[n], &mtfp_out[n * 5]);
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST FUNCTION DECLARATIONS
 *
 *  Each returns 1 (pass) or 0 (fail/skip).
 *  Tests 12-14 require Test 11 to have run first.
 * ══════════════════════════════════════════════════════════════════ */

/* test_gie_core.c — Tests 1-8 (GIE, LP core, VDB, pipeline, feedback) */
int run_test_1(void);
int run_test_2(void);
int run_test_3(void);
int run_test_4(void);
int run_test_5(void);
int run_test_6(void);
int run_test_7(void);
int run_test_8(void);

/* test_espnow.c — Tests 9-10 (ESP-NOW receive, live input) */
int run_test_9(void);
int run_test_10(void);

/* test_live_input.c — Test 11 (pattern classification + enrollment) */
int run_test_11(void);

/* test_memory.c — Tests 12-13 (memory-modulated attention, VDB necessity) */
int run_test_12(void);
int run_test_13(void);

/* test_kinetic.c — Tests 14, 14C (kinetic attention, CLS transition) */
int run_test_14(void);
int run_test_14c(void);

/* test_lp_char.c — LP characterization + dot magnitude diagnostic */
void run_lp_char(void);
void run_lp_dot_diag(void);

/* test_hebbian.c — Test 15 (Hebbian LP weight learning) */
int run_test_15(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_HARNESS_H */
