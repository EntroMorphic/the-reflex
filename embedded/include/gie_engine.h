/**
 * gie_engine.h — Geometry Intersection Engine Public Interface
 *
 * This header exposes the GIE engine, ISR, TriX classifier, LP core
 * interface, and all peripheral initialization for use by the test
 * harness (geometry_cfc_freerun.c) or any future firmware entry point.
 *
 * The engine is implemented in gie_engine.c.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#ifndef GIE_ENGINE_H
#define GIE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "reflex.h"
#include "reflex_espnow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════
 *  CONSTANTS
 * ══════════════════════════════════════════════════════════════════ */

#define CFC_INPUT_DIM   128
#define CFC_HIDDEN_DIM  32
#define CFC_CONCAT_DIM  (CFC_INPUT_DIM + CFC_HIDDEN_DIM)   /* 160 */
#define CFC_MAX_DIM     256

#define NUM_NEURONS     64   /* 32 f + 32 g pathways */
#define NEURON_BUF_SIZE 80   /* 160 trits = 80 bytes */

/* DMA chain layout — shared between engine and test harness.
 * NUM_DUMMIES + NUM_NEURONS separator captures form one loop. */
#define SEP_SIZE           64
#define NUM_DUMMIES        5
#define CAPTURES_PER_LOOP  (NUM_DUMMIES + NUM_NEURONS)

#define TRIX_NUM_PATTERNS  4
#define TRIX_NEURONS_PP    (CFC_HIDDEN_DIM / TRIX_NUM_PATTERNS)  /* 8 */

/* LP Core dimensions (must match ulp/main.S) */
#define LP_GIE_HIDDEN    32
#define LP_HIDDEN_DIM    16
#define LP_CONCAT_DIM    48  /* 32 + 16 */
#define LP_NUM_NEURONS   32  /* 16 f + 16 g */
#define LP_PACKED_WORDS  3   /* ceil(48/16) */

#define T_POS  (+1)
#define T_ZERO  (0)
#define T_NEG  (-1)

/* ══════════════════════════════════════════════════════════════════
 *  CfC STATE
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int8_t W_f[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t W_g[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t hidden[CFC_HIDDEN_DIM];
    int8_t input[CFC_INPUT_DIM];
} cfc_state_t;

/* Engine state — accessible by test harness */
extern cfc_state_t cfc;
extern int8_t all_products[NUM_NEURONS][CFC_MAX_DIM];

/* ══════════════════════════════════════════════════════════════════
 *  ISR / FREE-RUNNING STATE (volatile, updated by ISR)
 * ══════════════════════════════════════════════════════════════════ */

extern volatile int32_t loop_count;
extern volatile int32_t loop_dots[NUM_NEURONS];
extern volatile int64_t loop_timestamp_us;
extern volatile int32_t loop_base;
extern volatile int32_t loop_isr_count;

/* TriX gate threshold — writable by test harness */
extern volatile int32_t gate_threshold;
extern volatile int32_t gate_fires_total;
extern volatile int32_t gate_steps_total;

/* Phase 5: per-group gate bias (agreement-weighted kinetic attention).
 * Positive bias → lower effective threshold → fires more easily.
 * Written by test harness, read by ISR. HP BSS, not LP SRAM. */
#define BASE_GATE_BIAS      15      /* max bias magnitude at full agreement */
#define MIN_GATE_THRESHOLD  30      /* hard floor: 33% of gate_threshold=90 */
extern volatile int8_t gie_gate_bias[TRIX_NUM_PATTERNS];
extern volatile int8_t gie_gate_bias_pn[CFC_HIDDEN_DIM];  /* per-neuron bias */
extern volatile int32_t gie_gate_fires_per_group[TRIX_NUM_PATTERNS];

/* TriX GDMA offset mapping — resolves ISR group index to pattern ID.
 * The GDMA circular chain offset means ISR group g may not correspond
 * to pattern g. This table maps group → pattern. Populated by the HP
 * core after enrollment (Test 11) by matching ISR group scores against
 * CPU-computed pattern dots. The ISR applies it in the trix_pred argmax.
 * -1 = unmapped (use group index as-is). */
extern volatile int8_t trix_group_to_pattern[TRIX_NUM_PATTERNS];

/* TriX ISR classification state */
extern volatile int32_t trix_enabled;
extern volatile int32_t trix_pred;
extern volatile int32_t trix_confidence;
extern volatile int32_t trix_scores[TRIX_NUM_PATTERNS];
extern volatile int64_t trix_timestamp_us;
extern volatile int32_t trix_count;
extern volatile int32_t trix_valid_lc;
extern volatile int32_t gie_input_pending;

/* Reflex channels */
extern reflex_channel_t trix_channel;
extern reflex_channel_t gie_channel;

/* Diagnostic captures */
#define DIAG_LEN 12
extern volatile int32_t diag_agree[DIAG_LEN];
extern volatile int32_t diag_disagree[DIAG_LEN];

/* LP core weights (HP-side copies for CPU reference) */
extern int8_t lp_W_f[LP_HIDDEN_DIM][LP_CONCAT_DIM];
extern int8_t lp_W_g[LP_HIDDEN_DIM][LP_CONCAT_DIM];

/* ESP-NOW timing state (reset by test harness between tests) */
extern int64_t espnow_last_rx_us;

/* ══════════════════════════════════════════════════════════════════
 *  ULP ADDRESS HELPER
 *
 *  Gets an opaque pointer to a ULP symbol's LP SRAM address.
 *  The ULP build system declares all symbols as `extern uint32_t`,
 *  but the actual data is larger (arrays). This helper prevents
 *  GCC's -Warray-bounds from seeing through the cast.
 * ══════════════════════════════════════════════════════════════════ */

static inline void * __attribute__((always_inline))
ulp_addr(const volatile void *sym) {
    uintptr_t addr;
    __asm__ volatile("" : "=r"(addr) : "0"(sym));
    return (void*)addr;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS
 * ══════════════════════════════════════════════════════════════════ */

int8_t tmul(int8_t a, int8_t b);
int8_t tsign(int val);

/* PRNG */
void cfc_seed(uint32_t seed);
uint32_t cfc_rand(void);
int8_t rand_trit(int sparsity_pct);

/* ══════════════════════════════════════════════════════════════════
 *  CfC INITIALIZATION & PREPARATION
 * ══════════════════════════════════════════════════════════════════ */

void cfc_init(uint32_t seed, int sparsity_pct);
void premultiply_all(void);
void encode_all_neurons(void);

/* ══════════════════════════════════════════════════════════════════
 *  PERIPHERAL INITIALIZATION
 * ══════════════════════════════════════════════════════════════════ */

void gie_init_gpio(void);
void gie_init_etm_clk(void);
void gie_init_parlio(void);
void gie_init_pcnt(void);
void gie_init_timer(void);
void gie_detect_gdma_channel(void);
void gie_init_gdma_isr(void);
int  gie_get_gdma_channel(void);
bool gie_isr_ready(void);

/* ══════════════════════════════════════════════════════════════════
 *  ENGINE CONTROL
 * ══════════════════════════════════════════════════════════════════ */

void build_circular_chain(void);
void prime_pipeline(void);
void start_freerun(void);
void stop_freerun(void);
void clear_all_pcnt(void);

/* ══════════════════════════════════════════════════════════════════
 *  ESP-NOW INPUT ENCODING
 * ══════════════════════════════════════════════════════════════════ */

int espnow_encode_input(const espnow_state_t *st);
int espnow_encode_rx_entry(const espnow_rx_entry_t *entry, int64_t *out_gap_ms);
void update_gie_input(void);

/* ══════════════════════════════════════════════════════════════════
 *  HOMEOSTATIC LEARNING
 * ══════════════════════════════════════════════════════════════════ */

int cfc_homeostatic_step(int dot_threshold, int period);

/* ══════════════════════════════════════════════════════════════════
 *  LP CORE INTERFACE
 * ══════════════════════════════════════════════════════════════════ */

void init_lp_core_weights(uint32_t seed);
void start_lp_core(void);
void feed_lp_core(void);
void cpu_lp_reference(const int8_t *gie_h, const int8_t *lp_h,
                      int *dots_f, int *dots_g);

/* ══════════════════════════════════════════════════════════════════
 *  DISPLAY HELPERS
 * ══════════════════════════════════════════════════════════════════ */

char trit_char(int8_t t);
void print_trit_vec(const char *label, const int8_t *v, int n);
int trit_hamming(const int8_t *a, const int8_t *b, int n);
int trit_energy(const int8_t *v, int n);

/* ══════════════════════════════════════════════════════════════════
 *  ENCODING (exposed for test verification)
 * ══════════════════════════════════════════════════════════════════ */

uint8_t encode_trit_pair(int t0, int t1);
void encode_neuron_buffer(uint8_t *buf, const int8_t *trits, int n_trits);

/* ══════════════════════════════════════════════════════════════════
 *  MTFP21 GAP HISTORY
 * ══════════════════════════════════════════════════════════════════ */

void gie_reset_gap_history(void);

#ifdef __cplusplus
}
#endif

#endif /* GIE_ENGINE_H */
