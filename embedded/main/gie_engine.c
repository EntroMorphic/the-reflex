/*
 * gie_engine.c — Geometry Intersection Engine Core
 *
 * INVARIANT: No float or double anywhere in this file. The "no floating
 * point in the mechanism path" claim depends on this. grep before commit.
 *
 * The GIE runs a ternary CfC liquid neural network continuously in
 * peripheral hardware. After boot, the CPU never computes a dot product.
 * The hidden state updates autonomously at ~430 Hz via:
 *
 *   GDMA (circular chain) -> PARLIO (loopback) -> PCNT (count)
 *   -> ISR (decode dots, blend, re-encode) -> GDMA loops back
 *
 * This file contains the engine core: all peripheral initialization,
 * ISR, TriX classification, LP core interface, ESP-NOW input encoding,
 * and homeostatic learning. The test harness (app_main + TEST 1-13)
 * is in geometry_cfc_freerun.c.
 *
 * Separated from the test harness: April 6, 2026 (audit remediation).
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * ESP-IDF: v5.4
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "esp_intr_alloc.h"
#include "soc/gdma_reg.h"
#include "soc/gdma_struct.h"
#include "soc/interrupts.h"
#include "rom/lldesc.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "reflex_vdb.h"
#include "gie_engine.h"

/* ── Register addresses ── */
#define ETM_BASE            0x60013000
#define ETM_CLK_EN_REG      (ETM_BASE + 0x1A8)
#define PCR_BASE             0x60096000
#define PCR_SOC_ETM_CONF     (PCR_BASE + 0x98)

#define GDMA_BASE            0x60080000
#define GDMA_OUT_BASE(ch)    (GDMA_BASE + 0xD0 + (ch)*0xC0)
#define GDMA_OUT_CONF0           0x00
#define GDMA_OUT_OUTFIFO_STATUS  0x08
#define GDMA_OUT_LINK            0x10
#define GDMA_OUT_STATE           0x14
#define GDMA_OUT_EOF_DES_ADDR    0x18
#define GDMA_OUT_PERI_SEL        0x30
#define GDMA_RST_BIT         (1 << 0)
#define GDMA_EOF_MODE_BIT    (1 << 3)
#define GDMA_LINK_START_BIT  (1 << 21)
#define GDMA_LINK_ADDR_MASK  0x000FFFFF
#define GDMA_PERI_PARLIO     9

#define GDMA_OUT_INT_RAW_CH(ch)  (GDMA_BASE + 0x30 + (ch) * 0x10)
#define GDMA_OUT_INT_ST_CH(ch)   (GDMA_BASE + 0x34 + (ch) * 0x10)
#define GDMA_OUT_INT_ENA_CH(ch)  (GDMA_BASE + 0x38 + (ch) * 0x10)
#define GDMA_OUT_INT_CLR_CH(ch)  (GDMA_BASE + 0x3C + (ch) * 0x10)

#define GDMA_OUT_EOF_BIT        (1 << 1)
#define GDMA_OUT_TOTAL_EOF_BIT  (1 << 3)

#define PARLIO_TX_CFG0       0x60015008
#define PARLIO_INT_ENA       0x60015014   /* bit 0=tx_fifo_rempty_ena, bit 2=tx_eof_ena */
#define PARLIO_TX_ST         0x60015010   /* bit 31 = tx_ready (1=idle) */
#define PARLIO_INT_RAW       0x60015018   /* bit 0=tx_fifo_rempty, bit 2=tx_eof */
/* PCR register: PARLIO TX clock enable (bit 18 = parl_clk_tx_en).
 * The PARLIO driver disables this clock after each transaction completes.
 * Bare-metal free-run mode must explicitly enable it and keep it enabled. */
#define PCR_PARL_CLK_TX_CONF 0x600960ac
#define PARLIO_TX_CLK_EN_BIT (1 << 18)
#define REG32(addr)  (*(volatile uint32_t*)(addr))

#define PCNT_BASE            0x60012000
#define PCNT_U0_CNT_REG      (PCNT_BASE + 0x30)
#define PCNT_U1_CNT_REG      (PCNT_BASE + 0x34)

/* ── GPIO mapping ── */
#define GPIO_X_POS   4
#define GPIO_X_NEG   5
#define GPIO_Y_POS   6
#define GPIO_Y_NEG   7

/* ── Engine-internal constants ──
 * SEP_SIZE, NUM_DUMMIES, CAPTURES_PER_LOOP are in gie_engine.h
 * (shared with the test harness). */
#define TOTAL_DESCS     (NUM_DUMMIES * 2 + NUM_NEURONS * 2)

/* Total bytes per loop — must match PARLIO tx_bytelen */
#define CHAIN_BYTES     ((NUM_DUMMIES + NUM_NEURONS) * (NEURON_BUF_SIZE + SEP_SIZE))

/* ── Static allocations (BSS) ── */
static uint8_t __attribute__((aligned(4))) neuron_bufs[NUM_NEURONS][NEURON_BUF_SIZE];
static uint8_t __attribute__((aligned(4))) sep_buf[SEP_SIZE];
static uint8_t __attribute__((aligned(4))) dummy_buf[NEURON_BUF_SIZE];
static lldesc_t __attribute__((aligned(4))) neuron_descs[TOTAL_DESCS];

/* ISR state — captures within current loop */
#define ISR_CAPTURES    (CAPTURES_PER_LOOP + 4)
static volatile int32_t isr_agree[ISR_CAPTURES];
static volatile int32_t isr_disagree[ISR_CAPTURES];
static volatile int32_t isr_count;

/* Free-running state — updated by ISR at loop boundary */
volatile int32_t loop_count;         /* total completed loops */
volatile int32_t loop_dots[NUM_NEURONS]; /* dots from last completed loop */
volatile int64_t loop_timestamp_us;  /* timestamp of last loop completion */
volatile int32_t loop_base;          /* diagnostic: baseline index */
volatile int32_t loop_isr_count;     /* diagnostic: isr_count at boundary */

/* TriX gate threshold: f fires only if |f_dot| > gate_threshold.
 * 0 = original behavior (any nonzero dot fires the gate).
 * Set to ~90 after installing TriX signatures as W_f weights. */
volatile int32_t gate_threshold = 0;
volatile int32_t gate_fires_total = 0;  /* diagnostic: count gate fires */
volatile int32_t gate_steps_total = 0;  /* diagnostic: count total neurons checked */

/* Phase 5: per-group gate bias + per-group fire counters */
volatile int8_t gie_gate_bias[TRIX_NUM_PATTERNS] = {0};
volatile int8_t gie_gate_bias_pn[CFC_HIDDEN_DIM] = {0};
volatile int32_t gie_gate_fires_per_group[TRIX_NUM_PATTERNS] = {0};

/* TriX GDMA offset mapping — group index → pattern ID.
 * Initialized to identity (g→g). HP core overwrites after enrollment. */
volatile int8_t trix_group_to_pattern[TRIX_NUM_PATTERNS] = {0, 1, 2, 3};

/* TriX ISR classification — computed at 430 Hz in hardware. */
volatile int32_t trix_enabled = 0;
volatile int32_t trix_pred = -1;         /* current pattern prediction */
volatile int32_t trix_confidence = 0;    /* dot product of winning pattern */
volatile int32_t trix_scores[TRIX_NUM_PATTERNS]; /* per-pattern max dots */
volatile int64_t trix_timestamp_us = 0;
volatile int32_t trix_count = 0;         /* total classifications */
volatile int32_t trix_valid_lc = 0;      /* loop_count of last clean classification */
/* ISR-side input re-encode: main loop sets gie_input_pending=1 after updating
 * cfc.input[]. The ISR re-encodes all_products[] and neuron_bufs[] input portion
 * during isr_loop_boundary() when PARLIO is stopped (no DMA race). Then sets
 * gie_input_pending=0. Main loop spins on this flag + waits for one clean loop. */
volatile int32_t gie_input_pending = 0;   /* 1 = ISR should re-encode */
reflex_channel_t trix_channel __attribute__((aligned(32)));

/* Reflex channel: ISR → main loop coordination.
 * The ISR signals this channel after committing a complete hidden state,
 * providing ordering guarantees and cycle-accurate timestamps. */
reflex_channel_t gie_channel __attribute__((aligned(32)));
/* Raw capture diagnostic: captures around neuron 0 */
#define DIAG_LEN 12
volatile int32_t diag_agree[DIAG_LEN];
volatile int32_t diag_disagree[DIAG_LEN];

/* Peripheral handles */
static gptimer_handle_t timer0 = NULL;
static pcnt_unit_handle_t pcnt_agree = NULL, pcnt_disagree = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;
static int bare_ch = 0;
static intr_handle_t gdma_isr_handle = NULL;

/* CfC state — typedef in gie_engine.h */
cfc_state_t cfc;
int8_t all_products[NUM_NEURONS][CFC_MAX_DIM];

/* ulp_addr() is defined in gie_engine.h as static inline. */

/* LP Core dimensions defined in gie_engine.h */

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS & PRNG
 * ══════════════════════════════════════════════════════════════════ */

int8_t IRAM_ATTR tmul(int8_t a, int8_t b) {
    /* Ternary multiply without MUL instruction.
     * {-1,0,+1} × {-1,0,+1} → {-1,0,+1}
     * Zero if either is zero; sign = XOR of signs. */
    if (a == 0 || b == 0) return 0;
    /* same sign → +1, different sign → -1 */
    return ((a ^ b) >= 0) ? (int8_t)1 : (int8_t)-1;
}

int8_t tsign(int val) {
    if (val > 0) return T_POS;
    if (val < 0) return T_NEG;
    return T_ZERO;
}

static uint32_t cfc_rng_state;

void cfc_seed(uint32_t seed) {
    cfc_rng_state = seed ? seed : 0xDEADBEEF;
}

uint32_t cfc_rand(void) {
    uint32_t x = cfc_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cfc_rng_state = x;
    return x;
}

int8_t rand_trit(int sparsity_pct) {
    uint32_t r = cfc_rand() % 100;
    if (r < (uint32_t)sparsity_pct) return T_ZERO;
    return (cfc_rand() & 1) ? T_POS : T_NEG;
}

/* ══════════════════════════════════════════════════════════════════
 *  ENCODING
 * ══════════════════════════════════════════════════════════════════ */

uint8_t IRAM_ATTR encode_trit_pair(int t0, int t1) {
    uint8_t lo = 0, hi = 0;
    if (t0 == T_POS)  lo = 0x01;
    if (t0 == T_NEG)  lo = 0x02;
    if (t1 == T_POS)  hi = 0x10;
    if (t1 == T_NEG)  hi = 0x20;
    return hi | lo;
}

void encode_neuron_buffer(uint8_t *buf, const int8_t *trits, int n_trits) {
    memset(buf, 0, NEURON_BUF_SIZE);
    for (int i = 0; i < n_trits; i += 2) {
        int t0 = trits[i];
        int t1 = (i + 1 < n_trits) ? trits[i + 1] : 0;
        buf[i / 2] = encode_trit_pair(t0, t1);
    }
}

/* ISR-safe version: only re-encode the hidden portion (indices CFC_INPUT_DIM..CFC_CONCAT_DIM)
 * Called from LEVEL3 ISR during the dummy window. */
static void IRAM_ATTR isr_reencode_hidden_portion(int neuron_idx, const int8_t *products) {
    uint8_t *buf = neuron_bufs[neuron_idx];
    /* Hidden portion starts at trit index CFC_INPUT_DIM, byte index CFC_INPUT_DIM/2 */
    int start_byte = CFC_INPUT_DIM / 2;
    for (int i = CFC_INPUT_DIM; i < CFC_CONCAT_DIM; i += 2) {
        int t0 = products[i];
        int t1 = (i + 1 < CFC_CONCAT_DIM) ? products[i + 1] : 0;
        buf[i / 2] = encode_trit_pair(t0, t1);
    }
    /* Zero-fill remaining bytes (CFC_CONCAT_DIM/2 to NEURON_BUF_SIZE) — already zero from init */
    (void)start_byte;
}

/* ══════════════════════════════════════════════════════════════════
 *  CfC INITIALIZATION & PREPARATION
 * ══════════════════════════════════════════════════════════════════ */

void cfc_init(uint32_t seed, int sparsity_pct) {
    memset(&cfc, 0, sizeof(cfc));
    cfc_seed(seed);

    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            cfc.W_f[n][i] = rand_trit(sparsity_pct);
            cfc.W_g[n][i] = rand_trit(sparsity_pct);
        }
        for (int i = CFC_CONCAT_DIM; i < CFC_MAX_DIM; i++) {
            cfc.W_f[n][i] = T_ZERO;
            cfc.W_g[n][i] = T_ZERO;
        }
    }

    for (int i = 0; i < CFC_INPUT_DIM; i++)
        cfc.input[i] = rand_trit(40);

    memset(cfc.hidden, T_ZERO, CFC_HIDDEN_DIM);
}

void premultiply_all(void) {
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        /* f-pathway: neuron n */
        for (int i = 0; i < CFC_INPUT_DIM; i++)
            all_products[n][i] = tmul(cfc.W_f[n][i], cfc.input[i]);
        for (int i = 0; i < CFC_HIDDEN_DIM; i++)
            all_products[n][CFC_INPUT_DIM + i] = tmul(cfc.W_f[n][CFC_INPUT_DIM + i], cfc.hidden[i]);

        /* g-pathway: neuron n + 32 */
        int g_idx = n + CFC_HIDDEN_DIM;
        for (int i = 0; i < CFC_INPUT_DIM; i++)
            all_products[g_idx][i] = tmul(cfc.W_g[n][i], cfc.input[i]);
        for (int i = 0; i < CFC_HIDDEN_DIM; i++)
            all_products[g_idx][CFC_INPUT_DIM + i] = tmul(cfc.W_g[n][CFC_INPUT_DIM + i], cfc.hidden[i]);
    }
}

void encode_all_neurons(void) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        encode_neuron_buffer(neuron_bufs[n], all_products[n], CFC_CONCAT_DIM);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  ISR: FREE-RUNNING LOOP BOUNDARY HANDLER
 *
 *  This is the heart of the free-running architecture. When the ISR
 *  detects that a full loop has completed (CAPTURES_PER_LOOP captures),
 *  it decodes dots, applies the CfC blend, re-premultiplies the hidden
 *  portion of all neuron buffers, re-encodes them in-place, and re-arms
 *  PARLIO for the next loop.
 *
 *  All of this happens during the dummy phase of the NEXT loop (~86us
 *  window), which is safe because GDMA is streaming all-zeros dummies
 *  that produce no PCNT edges, and the real neuron descriptors haven't
 *  been read yet.
 * ══════════════════════════════════════════════════════════════════ */

static void IRAM_ATTR isr_loop_boundary(void) {
    /* ── 1. Publish raw captures around boundary for diagnostics ──
     * Copy captures [0..9] so the CPU can see exactly what PCNT
     * values look like across dummies and into early neurons. */
    int diag_len = (isr_count < DIAG_LEN) ? isr_count : DIAG_LEN;
    for (int i = 0; i < diag_len; i++) {
        diag_agree[i] = isr_agree[i];
        diag_disagree[i] = isr_disagree[i];
    }

    /* ── 2. Auto-detect base: find last pair of dummy captures with
     * zero delta (both agree and disagree unchanged). Same heuristic
     * as M8 v2, which verified 64/64 exact dot matches.
     * With phantom EOF cleared, base should be NUM_DUMMIES - 1. */
    int base = -1;
    int search_limit = NUM_DUMMIES + 2;
    if (search_limit > isr_count) search_limit = isr_count;
    /* Find last consecutive pair with zero delta */
    for (int i = 1; i < search_limit; i++) {
        int da = isr_agree[i] - isr_agree[i - 1];
        int dd = isr_disagree[i] - isr_disagree[i - 1];
        if (da == 0 && dd == 0) {
            base = i;
        }
    }
    /* Fallback: find last absolute (0,0) */
    if (base < 0) {
        for (int i = 0; i < search_limit; i++) {
            if (isr_agree[i] == 0 && isr_disagree[i] == 0) {
                base = i;
            }
        }
    }
    /* Last resort */
    if (base < 0) base = NUM_DUMMIES - 1;

    /* ── 3. Decode per-neuron dots ── */
    int dots[NUM_NEURONS];
    memset(dots, 0, sizeof(dots));
    for (int n = 0; n < NUM_NEURONS && (base + n + 1) < isr_count; n++) {
        int a = isr_agree[base + n + 1] - isr_agree[base + n];
        int d = isr_disagree[base + n + 1] - isr_disagree[base + n];
        dots[n] = a - d;
    }

    /* ── 3b. TriX classification (parallel path) ──
     * When enabled, extract per-pattern scores from the f-pathway dots.
     * Neurons 0..7 have sig[0] installed, 8..15 have sig[1], etc.
     * For each pattern, take the MAX dot among its 8 neurons.
     * Winner = argmax of the 4 pattern scores.
     *
     * The input re-encode now happens in step 5b (ISR-side, when PARLIO
     * is stopped). The dots from THIS loop reflect the PREVIOUS neuron_bufs.
     * After step 5b re-encodes, the NEXT loop will have clean data.
     * The main loop sets gie_input_pending=1 (instead of calling
     * update_gie_input), then waits for gie_input_pending==0 + 1 more loop. */
    if (trix_enabled) {
        /* ── TriX classification: clean-loop path preferred, sum fallback ──
         *
         * Clean path: all 8 neurons in each group have identical dots.
         * This gives exact per-group values. Used for trix_pred and
         * trix_channel when available.
         *
         * Sum fallback: if clean check fails (stale/mixed loop), compute
         * per-group sums and take argmax. Less precise but always available.
         * Only updates trix_pred, not trix_channel. */
        int clean = 1;
        int group_val[TRIX_NUM_PATTERNS];
        for (int p = 0; p < TRIX_NUM_PATTERNS && clean; p++) {
            int p_base = p * TRIX_NEURONS_PP;
            group_val[p] = dots[p_base];
            for (int k = 1; k < TRIX_NEURONS_PP; k++) {
                if (dots[p_base + k] != group_val[p]) {
                    clean = 0;
                    break;
                }
            }
        }
        if (clean) {
            /* Exact: uniform neurons → group_val is the dot product.
             * Apply GDMA offset mapping: group index → pattern ID. */
            int best_g = 0, best_v = group_val[0];
            for (int g = 1; g < TRIX_NUM_PATTERNS; g++) {
                if (group_val[g] > best_v) { best_v = group_val[g]; best_g = g; }
            }
            trix_pred = trix_group_to_pattern[best_g];
            trix_confidence = best_v;
            trix_timestamp_us = esp_timer_get_time();
            trix_count++;

            /* Publish to channel for detailed resolution (Test 11) */
            uint32_t packed = 0;
            for (int g = 0; g < TRIX_NUM_PATTERNS; g++) {
                trix_scores[g] = group_val[g];
                int v = group_val[g];
                if (v > 127) v = 127;
                if (v < -128) v = -128;
                packed |= ((uint32_t)(v & 0xFF)) << (g * 8);
            }
            trix_valid_lc = loop_count + 1;
            reflex_signal(&trix_channel, packed);
        } else if (trix_pred < 0) {
            /* Fallback: clean check failed but trix_pred has never been set.
             * Use group sums for an approximate classification so that
             * trix_pred is not stuck at -1. Once a clean loop sets it,
             * clean loops will keep it updated at 430 Hz. */
            int group_sum[TRIX_NUM_PATTERNS];
            for (int p = 0; p < TRIX_NUM_PATTERNS; p++) {
                int p_base = p * TRIX_NEURONS_PP;
                int s = 0;
                for (int k = 0; k < TRIX_NEURONS_PP; k++)
                    s += dots[p_base + k];
                group_sum[p] = s;
            }
            int best_g = 0, best_v = group_sum[0];
            for (int g = 1; g < TRIX_NUM_PATTERNS; g++) {
                if (group_sum[g] > best_v) { best_v = group_sum[g]; best_g = g; }
            }
            trix_pred = trix_group_to_pattern[best_g];
            trix_confidence = best_v;
            trix_timestamp_us = esp_timer_get_time();
            trix_count++;
        }
    }

    /* ── 4. Apply CfC ternary blend (with TriX gate threshold + Phase 5 bias) ── */
    int8_t h_new[CFC_HIDDEN_DIM];
    int32_t thresh = gate_threshold;  /* snapshot volatile once */
    int fires = 0;
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int f_dot = dots[n];
        int8_t f;
        if (thresh > 0) {
            /* Phase 5: per-group gate bias. Positive bias → lower effective
             * threshold → fires more easily. Bias is subtracted. */
            int group = n / TRIX_NEURONS_PP;
            int32_t eff = thresh - (int32_t)gie_gate_bias[group];
            if (eff < MIN_GATE_THRESHOLD) eff = MIN_GATE_THRESHOLD;
            f = (f_dot > eff || f_dot < -eff) ? tsign(f_dot) : T_ZERO;
        } else {
            f = tsign(f_dot);
        }
        int8_t g = tsign(dots[n + CFC_HIDDEN_DIM]);
        if (f == T_ZERO) {
            h_new[n] = cfc.hidden[n];
        } else {
            h_new[n] = tmul(f, g);
            fires++;
            gie_gate_fires_per_group[n / TRIX_NEURONS_PP]++;
        }
    }
    gate_fires_total += fires;
    gate_steps_total += CFC_HIDDEN_DIM;

    /* ── 5. Commit hidden + re-premultiply + re-encode ──
     * Phase 4 optimization: skip when gate_threshold == INT32_MAX (0x7FFFFFFF).
     * When blend is disabled (Phase 3+), no neuron ever fires, hidden never
     * changes, and the DMA buffers stay correct. Saves ~20us per loop.
     * When blend IS active (Tests 1-10, gate_threshold = 0), this block
     * executes normally regardless of fires count. */
    if (thresh < 0x7FFFFFFF) {
        /* Commit new hidden state — this is the "register" the CPU reads */
        memcpy((void*)cfc.hidden, h_new, CFC_HIDDEN_DIM);

        /* Re-premultiply hidden portion and re-encode.
         * Only the hidden portion (indices CFC_INPUT_DIM..CFC_CONCAT_DIM) changes
         * between loops. The input portion stays the same.
         * This takes ~20us — well within the 86us dummy window. */
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            /* f-pathway: neuron n */
            for (int i = 0; i < CFC_HIDDEN_DIM; i++)
                all_products[n][CFC_INPUT_DIM + i] = tmul(cfc.W_f[n][CFC_INPUT_DIM + i], cfc.hidden[i]);
            isr_reencode_hidden_portion(n, all_products[n]);

            /* g-pathway: neuron n + 32 */
            int g_idx = n + CFC_HIDDEN_DIM;
            for (int i = 0; i < CFC_HIDDEN_DIM; i++)
                all_products[g_idx][CFC_INPUT_DIM + i] = tmul(cfc.W_g[n][CFC_INPUT_DIM + i], cfc.hidden[i]);
            isr_reencode_hidden_portion(g_idx, all_products[g_idx]);
        }
    }

    /* ── 5b. ISR-side input re-encode (when flagged by main loop) ──
     * PARLIO has stopped (tx_bytelen exhausted), so the DMA is not
     * streaming neuron_bufs. Safe to re-encode the input portion.
     * This eliminates the DMA race that corrupted dots when the main
     * loop called update_gie_input() while the DMA was mid-stream. */
    if (gie_input_pending) {
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            /* f-pathway: neuron n */
            for (int ii = 0; ii < CFC_INPUT_DIM; ii++)
                all_products[n][ii] = tmul(cfc.W_f[n][ii], cfc.input[ii]);
            /* g-pathway: neuron n + 32 */
            int g_idx = n + CFC_HIDDEN_DIM;
            for (int ii = 0; ii < CFC_INPUT_DIM; ii++)
                all_products[g_idx][ii] = tmul(cfc.W_g[n][ii], cfc.input[ii]);
        }
        for (int n = 0; n < NUM_NEURONS; n++) {
            uint8_t *buf = neuron_bufs[n];
            for (int ii = 0; ii < CFC_INPUT_DIM; ii += 2) {
                int t0 = all_products[n][ii];
                int t1 = (ii + 1 < CFC_INPUT_DIM) ? all_products[n][ii + 1] : 0;
                buf[ii / 2] = encode_trit_pair(t0, t1);
            }
        }
        /* Do NOT restart the GDMA link pointer here — that kills
         * the free-running loop (same failure as full GDMA reset).
         * Instead, the main loop waits for 2 full loops after we
         * clear gie_input_pending:
         *   Loop N+1: PARLIO FIFO flush discards any stale data that
         *             GDMA pre-fetched before the re-encode. However,
         *             GDMA's internal read pointer is mid-chain, so
         *             early descriptors may still be stale in GDMA's
         *             prefetch buffer.
         *   Loop N+2: GDMA has cycled through the entire chain with
         *             the new neuron_bufs. All 64 neurons are fresh.
         *             The trix_scores from this loop are trustworthy. */
        gie_input_pending = 0;
    }

    /* ── 6. Publish results for CPU ── */
    loop_base = base;
    loop_isr_count = isr_count;
    memcpy((void*)loop_dots, dots, sizeof(dots));
    loop_timestamp_us = esp_timer_get_time();
    loop_count++;

    /* ── 6b. Signal reflex channel ──
     * The hidden state and loop_dots are committed to SRAM above.
     * The fence inside reflex_signal() guarantees the consumer sees
     * the complete state before the sequence number changes. */
    reflex_signal(&gie_channel, (uint32_t)loop_count);

    /* ── 7. Feed GIE hidden state to LP core ──
     * Copy the new hidden state to LP SRAM where the LP core's
     * geometric processor can read it. The ulp_ prefix accesses
     * the LP SRAM variable directly by address.
     * Note: ulp_ symbols are uint32_t pointers into LP SRAM. */
    /* LP SRAM update is done from main loop context (not ISR) to avoid
     * bus contention stalling the time-critical ISR. See feed_lp_core(). */
}

/* ══════════════════════════════════════════════════════════════════
 *  GDMA EOF ISR — Free-Running Version
 *
 *  Captures cumulative PCNT on each separator EOF. When a full loop
 *  completes (isr_count == CAPTURES_PER_LOOP), calls the loop boundary
 *  handler, resets isr_count, clears PCNT, and re-arms PARLIO.
 * ══════════════════════════════════════════════════════════════════ */

static void IRAM_ATTR gdma_eof_isr(void *arg) {
    uint32_t status = REG32(GDMA_OUT_INT_ST_CH(bare_ch));

    if (status & GDMA_OUT_EOF_BIT) {
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_EOF_BIT;

        /* Clock domain drain: ~5us for PCNT pipeline to settle.
         * PCNT reads GPIO through the matrix; after PARLIO outputs the
         * separator's last byte, the PCNT pipeline needs time to drain.
         * 200 volatile loops ≈ 5us at 160MHz. Increased from 100 loops
         * after gpio_func_sel() in the PCNT driver changed matrix timing. */
        for (volatile int _d = 0; _d < 200; _d++) { }

        int idx = isr_count;
        if (idx < ISR_CAPTURES) {
            isr_agree[idx] = (int16_t)(REG32(PCNT_U0_CNT_REG) & 0xFFFF);
            isr_disagree[idx] = (int16_t)(REG32(PCNT_U1_CNT_REG) & 0xFFFF);
            isr_count = idx + 1;
        }

        /* Loop boundary: full pass completed */
        if (isr_count >= CAPTURES_PER_LOOP) {
            /* Process dots, blend, re-encode — happens during dummy phase
             * of the NEXT loop (GDMA has already looped back to dummy descriptors
             * and is streaming zeros while we work here). */
            isr_loop_boundary();

            /* Reset for next loop */
            isr_count = 0;
            memset((void*)isr_agree, 0, sizeof(isr_agree));
            memset((void*)isr_disagree, 0, sizeof(isr_disagree));

            /* Re-arm PARLIO byte counter for next loop.
             * PARLIO has stopped because tx_bytelen ran out.
             * Do NOT reset PARLIO FIFO here — doing so while GDMA still
             * has an active AHB transaction to PARLIO's FIFO confuses
             * GDMA's state machine and prevents it from generating further
             * EOFs, which kills the free-running loop.
             * The NUM_DUMMIES separator window at the start of each loop
             * absorbs any FIFO residue without requiring an explicit flush. */
            /* Stop PARLIO and reprogram byte count.
             * tx_start must transition 0→1 to re-arm the byte counter;
             * simply writing a new tx_bytelen with tx_start still high
             * does not restart it. Do NOT reset FIFO here — that confuses
             * GDMA's AHB state machine and stops EOF generation. */
            uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
            tx_cfg0 &= ~(1 << 19);   /* clear tx_start */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;

            /* Clear PCNT — PARLIO has stopped (tx_bytelen exhausted), so
             * no edges are in flight and the clear is race-free.
             *
             * Triple clear per unit (6 calls total): the PCNT clear is
             * asynchronous to the PCNT sampling clock domain. A single
             * pcnt_unit_clear_count() writes the control register, but
             * the clear must propagate through the PCNT's internal
             * pipeline (edge detector → accumulator → count register)
             * before the count register reads as zero. Empirically,
             * a single clear leaves residual counts (1–3) that corrupt
             * the first neuron's dot product on the next loop. Three
             * clears with the interleaved unit ordering ensure the
             * pipeline has flushed by the time we restart PARLIO.
             * Verified: reducing to 2 clears intermittently leaves
             * residue; 3 clears produces zero residue across 13/13
             * hardware runs (March 22, 2026). */
            pcnt_unit_clear_count(pcnt_agree);
            pcnt_unit_clear_count(pcnt_disagree);
            pcnt_unit_clear_count(pcnt_agree);
            pcnt_unit_clear_count(pcnt_disagree);
            pcnt_unit_clear_count(pcnt_agree);
            pcnt_unit_clear_count(pcnt_disagree);

            /* Clear any phantom EOF interrupts that fired during FIFO
             * pre-fill. After FIFO reset, GDMA's linked-list pointer
             * kept advancing (filling the empty FIFO), potentially
             * crossing eof=1 descriptors and generating stale EOFs.
             * Clear these BEFORE restarting PARLIO so the ISR only
             * sees EOFs from actual PARLIO output.
             *
             * Note: we do NOT reset or restart the GDMA here — any
             * GDMA manipulation (RST_BIT, OUT_LINK restart) kills
             * the free-running loop on ESP32-C6. Instead, the dot
             * decoding in step 3 handles the circular chain offset
             * by decoding both pre-dummy and post-dummy captures. */
            REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_EOF_BIT;

            /* Reprogram byte count and start */
            tx_cfg0 &= ~(0xFFFF << 2);
            tx_cfg0 &= ~(1 << 18);   /* tx_gating_en off — TXD[7] gates clock, always 0 */
            tx_cfg0 |= (CHAIN_BYTES << 2);
            tx_cfg0 |= (1 << 19);   /* tx_start */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;
            /* Keep PCR clock enabled — in case PARLIO driver ISR ran and cleared it */
            REG32(PCR_PARL_CLK_TX_CONF) |= PARLIO_TX_CLK_EN_BIT;
        }
    }

    /* Clear any other pending bits */
    uint32_t others = status & ~GDMA_OUT_EOF_BIT;
    if (others) {
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = others;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  HELPERS
 * ══════════════════════════════════════════════════════════════════ */

void gie_init_etm_clk(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf = (*conf & ~(1 << 1)) | (1 << 0);
    esp_rom_delay_us(1);
    REG32(ETM_CLK_EN_REG) = 1;
    esp_rom_delay_us(1);
}

void gie_init_gpio(void) {
    for (int i = 4; i <= 7; i++) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << i),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(i, 0);
    }
}

void gie_init_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = 20000000,
        .data_width = 2,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 4,
        .max_transfer_size = 256,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++)
        cfg.data_gpio_nums[i] = (i < 2) ? (4 + i) : -1;
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    ESP_ERROR_CHECK(parlio_tx_unit_enable(parlio));
}

void gie_init_pcnt(void) {
    pcnt_unit_config_t ucfg = { .low_limit = -32000, .high_limit = 32000 };

    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &pcnt_agree));
    {
        pcnt_chan_config_t ch = { .edge_gpio_num = GPIO_X_POS, .level_gpio_num = GPIO_Y_POS };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_agree, &ch, &agree_ch0));
        pcnt_channel_set_edge_action(agree_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(agree_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        pcnt_chan_config_t ch1 = { .edge_gpio_num = GPIO_X_NEG, .level_gpio_num = GPIO_Y_NEG };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_agree, &ch1, &agree_ch1));
        pcnt_channel_set_edge_action(agree_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(agree_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);
    }
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_agree));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_agree));

    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &pcnt_disagree));
    {
        pcnt_chan_config_t ch = { .edge_gpio_num = GPIO_X_POS, .level_gpio_num = GPIO_Y_NEG };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_disagree, &ch, &disagree_ch0));
        pcnt_channel_set_edge_action(disagree_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(disagree_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        pcnt_chan_config_t ch1 = { .edge_gpio_num = GPIO_X_NEG, .level_gpio_num = GPIO_Y_POS };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_disagree, &ch1, &disagree_ch1));
        pcnt_channel_set_edge_action(disagree_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(disagree_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);
    }
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_disagree));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_disagree));

    /* PCNT driver calls gpio_config(GPIO_MODE_INPUT) on all four GPIO pins,
     * clearing the output-enable bit in GPIO_ENABLE_REG. Re-enable outputs
     * directly via register write — do NOT use gpio_set_direction or
     * gpio_output_enable, which call esp_rom_gpio_connect_out_signal with
     * SIG_GPIO_OUT_IDX (128) and would disconnect PARLIO from GPIO 4/5
     * (GPIO_FUNC4/5_OUT_SEL_CFG would be overwritten from 47/48 to 128). */
    REG32(0x60091020) |= (1UL << GPIO_X_POS) | (1UL << GPIO_X_NEG)
                       | (1UL << GPIO_Y_POS)  | (1UL << GPIO_Y_NEG);
}

void gie_init_timer(void) {
    gptimer_config_t c = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c, &timer0));
    ESP_ERROR_CHECK(gptimer_enable(timer0));
}

void gie_detect_gdma_channel(void) {
    for (int ch = 0; ch < 3; ch++) {
        uint32_t peri = REG32(GDMA_OUT_BASE(ch) + GDMA_OUT_PERI_SEL) & 0x3F;
        if (peri == GDMA_PERI_PARLIO) {
            bare_ch = ch;
            return;
        }
    }
    bare_ch = 0;
}

void gie_init_gdma_isr(void) {
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;
    /* Only enable EOF — no TOTAL_EOF in free-running mode (chain never ends) */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = GDMA_OUT_EOF_BIT;

    int intr_source = ETS_DMA_OUT_CH0_INTR_SOURCE + bare_ch;
    esp_err_t err = esp_intr_alloc(intr_source,
                                   ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3,
                                   gdma_eof_isr, NULL, &gdma_isr_handle);
    if (err != ESP_OK) {
        printf("[GDMA ISR] FAILED (source=%d, err=%d)\n", intr_source, err);
        gdma_isr_handle = NULL;
    } else {
        printf("[GDMA ISR] OK — CH%d (source=%d), LEVEL3\n", bare_ch, intr_source);
    }
}

void clear_all_pcnt(void) {
    pcnt_unit_clear_count(pcnt_agree);
    pcnt_unit_clear_count(pcnt_disagree);
}

/* ══════════════════════════════════════════════════════════════════
 *  BUILD CIRCULAR DMA CHAIN
 *
 *  Same as M8 v2, but the last descriptor points back to the first,
 *  creating an infinite loop. GDMA follows the linked list forever.
 * ══════════════════════════════════════════════════════════════════ */

void build_circular_chain(void) {
    memset(sep_buf, 0, SEP_SIZE);
    memset(dummy_buf, 0, NEURON_BUF_SIZE);

    int d = 0;

    /* Dummy neurons */
    for (int dum = 0; dum < NUM_DUMMIES; dum++) {
        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = NEURON_BUF_SIZE;
        neuron_descs[d].length = NEURON_BUF_SIZE;
        neuron_descs[d].buf = dummy_buf;
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 0;
        neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        d++;

        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = SEP_SIZE;
        neuron_descs[d].length = SEP_SIZE;
        neuron_descs[d].buf = sep_buf;
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 1;
        neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        d++;
    }

    /* Real neurons */
    for (int n = 0; n < NUM_NEURONS; n++) {
        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = NEURON_BUF_SIZE;
        neuron_descs[d].length = NEURON_BUF_SIZE;
        neuron_descs[d].buf = neuron_bufs[n];
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 0;
        neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        d++;

        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = SEP_SIZE;
        neuron_descs[d].length = SEP_SIZE;
        neuron_descs[d].buf = sep_buf;
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 1;
        if (n < NUM_NEURONS - 1) {
            neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        } else {
            /* CIRCULAR: last descriptor points back to first */
            neuron_descs[d].empty = (uint32_t)&neuron_descs[0];
        }
        d++;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  SETUP & START FREE-RUNNING ENGINE
 * ══════════════════════════════════════════════════════════════════ */

static void setup_gdma_freerun(void) {
    uint32_t base = GDMA_OUT_BASE(bare_ch);

    /* Reset PARLIO FIFO */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 |= (1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(10);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(10);

    /* Reset GDMA channel */
    REG32(base + GDMA_OUT_CONF0) = GDMA_RST_BIT;
    esp_rom_delay_us(10);
    REG32(base + GDMA_OUT_CONF0) = 0;
    esp_rom_delay_us(10);

    /* Clear interrupts */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    /* out_eof_mode=0: EOF fires when data is read from SRAM (no PARLIO handshake).
     * out_eof_mode=1 (bit 3) requires PARLIO to pop from its DMA FIFO before firing,
     * but that handshake is not asserted in bare-metal mode → ISR never fires. */
    REG32(base + GDMA_OUT_CONF0) = 0;
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    /* Start GDMA outlink — points to first descriptor of circular chain */
    uint32_t addr = ((uint32_t)&neuron_descs[0]) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    /* Configure PARLIO byte count for one loop */
    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);
    tx_cfg0 &= ~(1 << 19);   /* clear tx_start */
    tx_cfg0 &= ~(1 << 18);   /* clear tx_gating_en — TXD[7] is always 0 in our encoding,
                                * which would permanently gate the clock off */
    tx_cfg0 |= (CHAIN_BYTES << 2);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

void start_freerun(void) {
    /* Reset ISR state */
    isr_count = 0;
    loop_count = 0;
    loop_timestamp_us = 0;
    memset((void*)isr_agree, 0, sizeof(isr_agree));
    memset((void*)isr_disagree, 0, sizeof(isr_disagree));
    memset((void*)loop_dots, 0, sizeof(loop_dots));
    memset(&gie_channel, 0, sizeof(gie_channel));
    memset(&trix_channel, 0, sizeof(trix_channel));
    trix_pred = -1;
    trix_confidence = 0;
    trix_count = 0;
    trix_timestamp_us = 0;
    memset((void*)trix_scores, 0, sizeof(trix_scores));

    /* Drive GPIOs */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Triple-clear PCNT */
    clear_all_pcnt();
    esp_rom_delay_us(10);
    clear_all_pcnt();
    esp_rom_delay_us(10);
    clear_all_pcnt();
    esp_rom_delay_us(50);

    /* Set Y_POS high for PCNT level gating */
    gpio_set_level(GPIO_Y_POS, 1);
    esp_rom_delay_us(50);

    /* Setup GDMA + PARLIO — must happen before enabling GDMA interrupts.
     * After stop_freerun() the GDMA chain is reset (idle). If we enabled
     * interrupts before setup_gdma_freerun(), a stale pending bit or a
     * GDMA chain running from a previous loop could fire the ISR while we
     * are mid-reset, causing a deadlock or corrupt state on second call. */
    setup_gdma_freerun();

    /* Enable interrupts only after GDMA is fully reconfigured and idle */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = GDMA_OUT_EOF_BIT;
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* Re-connect PARLIO TX to GPIO 4/5 in the GPIO matrix.
     * Some init path (ESP-NOW, WiFi, or repeated gpio_config calls) resets
     * GPIO_FUNC4/5_OUT_SEL_CFG to SIG_GPIO_OUT_IDX (128), disconnecting
     * PARLIO from the physical pins. Do this unconditionally here so the
     * engine always starts with PARLIO wired to the GPIO matrix.
     * PARL_TX_DATA0_IDX=47, PARL_TX_DATA1_IDX=48 (from gpio_sig_map.h). */
    esp_rom_gpio_connect_out_signal(GPIO_X_POS, 47, false, false);
    esp_rom_gpio_connect_out_signal(GPIO_X_NEG, 48, false, false);

    /* Disable PARLIO driver interrupt so its ISR doesn't disable our clock.
     * The driver's TX-done ISR calls parlio_ll_tx_enable_clock(false) after
     * each transaction, which would kill the free-running clock. */
    REG32(PARLIO_INT_ENA) = 0;

    /* Fire PARLIO — the engine is now free-running */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 |= (1 << 19);  /* tx_start */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;

    /* Enable PARLIO TX output clock via PCR register.
     * The PARLIO driver (parlio_ll_tx_enable_clock) disables this after
     * prime_pipeline() completes. Without it, PARLIO FIFO never drains,
     * GDMA stalls on descriptor 0, and no EOFs ever fire. */
    REG32(PCR_PARL_CLK_TX_CONF) |= PARLIO_TX_CLK_EN_BIT;

    /* ── DIAGNOSTIC: read PCNT 500us after PARLIO fires ── */
    esp_rom_delay_us(500);
    int diag_agree0    = (int)(int16_t)(REG32(PCNT_U0_CNT_REG) & 0xFFFF);
    int diag_disagree0 = (int)(int16_t)(REG32(PCNT_U1_CNT_REG) & 0xFFFF);
    uint32_t gpio_ena  = REG32(0x60091020);  /* GPIO_ENABLE_REG */
    uint32_t parlio_cfg = REG32(PARLIO_TX_CFG0);
    /* GPIO_FUNC4/5_OUT_SEL_CFG: 47=PARL_TX_DATA0, 48=PARL_TX_DATA1, 128=SIG_GPIO_OUT */
    uint32_t out_sel4  = REG32(0x60091564) & 0xFF;
    uint32_t out_sel5  = REG32(0x60091568) & 0xFF;
    /* GPIO_FUNCn_IN_SEL_CFG: signal 101=PCNT_SIG_CH0_IN0, 103=PCNT_CTRL_CH0_IN0
     * DR_REG_GPIO_BASE + 0x154 + 4*signal_idx
     * bits[5:0]=func_sel(GPIO#), bit[7]=sig_in_sel(1=matrix,0=iomux) */
    uint32_t in_sel101 = REG32(0x60091154 + 4*101);  /* agree ch0 edge: GPIO4 */
    uint32_t in_sel103 = REG32(0x60091154 + 4*103);  /* agree ch0 level: GPIO6 */
    /* GPIO_IN_REG: raw input state of all GPIOs (bit N = GPIO N level) */
    uint32_t gpio_in   = REG32(0x6009103C);

    /* CPU-pulse test: temporarily drive GPIO4 manually to verify PCNT routing */
    pcnt_unit_clear_count(pcnt_agree);
    pcnt_unit_clear_count(pcnt_disagree);
    esp_rom_gpio_connect_out_signal(GPIO_X_POS, 128, false, false); /* simple GPIO latch */
    gpio_set_level(GPIO_X_POS, 0);
    for (int _p = 0; _p < 20; _p++) {
        gpio_set_level(GPIO_X_POS, 1);
        gpio_set_level(GPIO_X_POS, 0);
    }
    int cpu_agree = (int)(int16_t)(REG32(PCNT_U0_CNT_REG) & 0xFFFF);
    /* Reconnect PARLIO */
    esp_rom_gpio_connect_out_signal(GPIO_X_POS, 47, false, false);

    /* GDMA channel 0 state registers for deep diagnostics */
    uint32_t gdma_base = GDMA_OUT_BASE(bare_ch);
    uint32_t gdma_int_raw  = REG32(GDMA_OUT_INT_RAW_CH(bare_ch));
    uint32_t gdma_int_st   = REG32(GDMA_OUT_INT_ST_CH(bare_ch));
    uint32_t gdma_int_ena  = REG32(GDMA_OUT_INT_ENA_CH(bare_ch));
    uint32_t gdma_conf0    = REG32(gdma_base + GDMA_OUT_CONF0);
    uint32_t gdma_link     = REG32(gdma_base + GDMA_OUT_LINK);
    uint32_t gdma_fifo     = REG32(gdma_base + GDMA_OUT_OUTFIFO_STATUS);
    uint32_t gdma_state    = REG32(gdma_base + GDMA_OUT_STATE);
    uint32_t gdma_eof_addr = REG32(gdma_base + GDMA_OUT_EOF_DES_ADDR);
    uint32_t gdma_peri     = REG32(gdma_base + GDMA_OUT_PERI_SEL);

    printf("[DIAG] 500us: agree=%d disagree=%d out_sel4=%lu out_sel5=%lu in101=0x%lx in103=0x%lx gpio_in=0x%lx cpu_agree=%d loop=%d\n",
           diag_agree0, diag_disagree0,
           (unsigned long)out_sel4, (unsigned long)out_sel5,
           (unsigned long)in_sel101, (unsigned long)in_sel103,
           (unsigned long)gpio_in, cpu_agree, (int)loop_count);
    printf("[DIAG] gdma: raw=0x%lx st=0x%lx ena=0x%lx conf0=0x%lx link=0x%lx(park=%lu) peri=%lu\n",
           (unsigned long)gdma_int_raw, (unsigned long)gdma_int_st,
           (unsigned long)gdma_int_ena,
           (unsigned long)gdma_conf0, (unsigned long)gdma_link,
           (unsigned long)((gdma_link >> 23) & 1),
           (unsigned long)(gdma_peri & 0x3F));
    uint32_t parlio_st      = REG32(PARLIO_TX_ST);
    uint32_t parlio_int_raw = REG32(PARLIO_INT_RAW);
    printf("[DIAG] gdma2: fifo=0x%lx state=0x%lx(dscr_st=%lu out_st=%lu) eof_addr=0x%lx\n",
           (unsigned long)gdma_fifo,
           (unsigned long)gdma_state,
           (unsigned long)((gdma_state >> 18) & 0x3),   /* out_dscr_state [19:18] */
           (unsigned long)((gdma_state >> 20) & 0x7),   /* out_state [22:20] */
           (unsigned long)gdma_eof_addr);
    printf("[DIAG] parlio: cfg=0x%lx(tx_start=%lu bytelen=%lu) st=0x%lx(tx_ready=%lu) int_raw=0x%lx(eof=%lu rempty=%lu)\n",
           (unsigned long)parlio_cfg,
           (unsigned long)((parlio_cfg >> 19) & 1),
           (unsigned long)((parlio_cfg >> 2) & 0xFFFF),
           (unsigned long)parlio_st,
           (unsigned long)((parlio_st >> 31) & 1),
           (unsigned long)parlio_int_raw,
           (unsigned long)((parlio_int_raw >> 2) & 1),
           (unsigned long)((parlio_int_raw >> 0) & 1));
    (void)gpio_ena;
}

void stop_freerun(void) {
    /* Disable TriX ISR classification */
    trix_enabled = 0;

    /* Disable interrupts first to prevent ISR from firing during teardown */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    /* Stop PARLIO TX */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(1 << 19);   /* clear tx_start */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(5);
    /* Reset PARLIO FIFO — empties it, unblocking any GDMA AHB stall */
    tx_cfg0 |= (1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(10);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(10);

    /* Reset PARLIO TX core state machine via PCR (parl_tx_rst_en = bit 19).
     * stop_freerun() clears tx_start mid-transaction, leaving the PARLIO TX
     * state machine in a partial state. The FIFO reset above clears the data
     * path but not the control state machine. Without this core reset, the
     * next start_freerun() inherits corrupted TX state: PARLIO stops accepting
     * data from the AHB FIFO after the GDMA pre-fill, causing a permanent stall.
     * prime_pipeline() avoids this by running a complete TX (byte count exhausted
     * → clean idle), so TEST 1 works without this reset. All subsequent tests need it. */
    REG32(PCR_PARL_CLK_TX_CONF) |= (1 << 19);   /* assert parl_tx_rst_en */
    esp_rom_delay_us(5);
    REG32(PCR_PARL_CLK_TX_CONF) &= ~(1 << 19);  /* release reset */
    esp_rom_delay_us(5);

    /* Stop GDMA outlink — prevents the circular chain from running after stop.
     * If we skip this, the PARLIO FIFO reset above unblocks a stalled GDMA and
     * the chain resumes. On the next start_freerun(), enabling GDMA interrupts
     * would immediately fire the ISR while GDMA is mid-chain, racing with the
     * GDMA reset in setup_gdma_freerun() and causing a hang.
     * Delay 20us before reset to let any in-flight AHB transaction complete
     * (GDMA was just unblocked by the FIFO reset above). */
    esp_rom_delay_us(20);
    uint32_t base = GDMA_OUT_BASE(bare_ch);
    REG32(base + GDMA_OUT_CONF0) = GDMA_RST_BIT;
    esp_rom_delay_us(10);
    REG32(base + GDMA_OUT_CONF0) = 0;

    /* Disable PCR PARLIO TX clock */
    REG32(PCR_PARL_CLK_TX_CONF) &= ~PARLIO_TX_CLK_EN_BIT;

    /* Drive GPIOs low */
    for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(100);
}

/* ══════════════════════════════════════════════════════════════════
 *  DISPLAY HELPERS
 * ══════════════════════════════════════════════════════════════════ */

char trit_char(int8_t t) {
    if (t > 0) return '+';
    if (t < 0) return '-';
    return '0';
}

void print_trit_vec(const char *label, const int8_t *v, int n) {
    printf("  %s: [", label);
    for (int i = 0; i < n; i++) printf("%c", trit_char(v[i]));
    printf("]\n");
}

int trit_hamming(const int8_t *a, const int8_t *b, int n) {
    int d = 0;
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) d++;
    return d;
}

int trit_energy(const int8_t *v, int n) {
    int e = 0;
    for (int i = 0; i < n; i++)
        if (v[i] != T_ZERO) e++;
    return e;
}

/* ══════════════════════════════════════════════════════════════════
 *  ESP-NOW → TERNARY INPUT ENCODING
 *
 *  Encode live ESP-NOW data into the 128-trit GIE input vector.
 *  This replaces the static random input with real wireless data:
 *
 *  Trit layout (128 trits total):
 *    [0..15]    RSSI thermometer: 16 trits, maps [-80, -20] dBm
 *               Each trit goes +1 when RSSI exceeds its threshold.
 *               Below range = all -1, above = all +1.
 *    [16..23]   Pattern ID one-hot: 4 patterns × 2 trits each.
 *               Active pattern gets (+1, +1), others get (-1, -1).
 *    [24..87]   Payload bytes: 8 bytes × 8 bits = 64 trits.
 *               Each bit → +1 if set, -1 if clear.
 *    [88..102]  MTFP21 gap history: 5 gaps × 3 trits (2 exp + 1 mantissa).
 *               Encodes temporal pattern, not instantaneous value.
 *    [103]      Gap variance flag: +1 bursty, -1 steady, 0 moderate.
 *    [104..119] Sequence features: 16 trits encoding sequence % 16
 *               and sequence / 16 patterns.
 *    [120..127] Reserved: zeros (future expansion).
 *
 *  The re-encode is safe to call from the main loop while the ISR
 *  runs: the ISR only touches indices [CFC_INPUT_DIM..CFC_CONCAT_DIM]
 *  (the hidden portion), while this function touches [0..CFC_INPUT_DIM-1].
 * ══════════════════════════════════════════════════════════════════ */

/* Track last receive time for inter-packet timing */
int64_t espnow_last_rx_us = 0;

/* ── MTFP21 gap history (5 most recent inter-packet gaps) ── */
static int16_t gap_history[5] = {0};
static int     gap_history_idx = 0;

void gie_reset_gap_history(void) {
    memset(gap_history, 0, sizeof(gap_history));
    gap_history_idx = 0;
}

/* MTFP21 scale tables (file-scope, shared by encoder and variance) */
static const int16_t mtfp_scale_lo[] = {0,  25,  75, 150, 250, 400, 600, 1000};
static const int16_t mtfp_scale_hi[] = {24, 74, 149, 249, 399, 599, 999, 9999};
static const int8_t  mtfp_exp0[]     = {-1, -1, -1,   0,   0,   0,   1,    1};
static const int8_t  mtfp_exp1[]     = {-1,  0,  1,  -1,   0,   1,  -1,    0};

/* MTFP21 encoder: gap_ms → 3 trits [exp0, exp1, mantissa].
 * Scale boundaries tuned to sender gap clusters with 25ms margins. */
static void encode_mtfp21_gap(int gap_ms, int8_t *out) {

    int g = (gap_ms < 0) ? 0 : (gap_ms > 9999) ? 9999 : gap_ms;
    int s = 7;
    for (int i = 0; i < 8; i++) {
        if (g >= mtfp_scale_lo[i] && g <= mtfp_scale_hi[i]) { s = i; break; }
    }

    out[0] = mtfp_exp0[s];
    out[1] = mtfp_exp1[s];

    int range = mtfp_scale_hi[s] - mtfp_scale_lo[s];
    if (range <= 0) {
        out[2] = 0;
    } else {
        int pos = g - mtfp_scale_lo[s];
        out[2] = (pos * 3 < range) ? -1 : (pos * 3 > range * 2) ? 1 : 0;
    }
}

/* Encode 5-gap MTFP21 history + variance flag into trits [88..103] */
static void encode_gap_history(int gap_ms, int8_t *new_input) {
    gap_history[gap_history_idx] = (int16_t)gap_ms;
    gap_history_idx = (gap_history_idx + 1) % 5;

    /* Encode 5 gaps oldest-first */
    for (int g = 0; g < 5; g++) {
        int hi = (gap_history_idx + g) % 5;
        encode_mtfp21_gap(gap_history[hi], &new_input[88 + g * 3]);
    }

    /* Trit [103]: gap variance flag.
     * +1 if gaps span 3+ scales (bursty), -1 if <=1 scale (steady). */
    int min_s = 7, max_s = 0;
    for (int g = 0; g < 5; g++) {
        int hi = (gap_history_idx + g) % 5;
        int gv = gap_history[hi];
        int sv = 7;
        for (int i = 0; i < 8; i++) {
            if (gv >= mtfp_scale_lo[i] && gv <= mtfp_scale_hi[i]) { sv = i; break; }
        }
        if (sv < min_s) min_s = sv;
        if (sv > max_s) max_s = sv;
    }
    new_input[103] = (max_s - min_s >= 3) ? T_POS
                   : (max_s - min_s <= 1) ? T_NEG : T_ZERO;
}

/**
 * Encode ESP-NOW state into cfc.input[128].
 * Returns 1 if input changed, 0 if unchanged.
 *
 * Legacy version — uses esp_timer_get_time() for inter-packet gap.
 * Prefer espnow_encode_rx_entry() for stream processing.
 */
int espnow_encode_input(const espnow_state_t *st) {
    int8_t new_input[CFC_INPUT_DIM];
    memset(new_input, 0, sizeof(new_input));

    /* ── [0..15] RSSI thermometer ──
     * 16 thresholds from -80 to -20 dBm, step = 3.75 ≈ 4 dBm.
     * Each trit: +1 if RSSI >= threshold, -1 if below.
     *
     * NOTE: Dead zone (RSSI_MARGIN=2) was tested on silicon (April 8, 2026)
     * and caused P0/P1/P2 LP hidden collapse in Test 12. Reverted to binary.
     * Dead zone needs RSSI_MARGIN=1 or per-threshold tuning. See §4A in
     * docs/REMEDIATION_PLAN_APR08.md. */
    for (int i = 0; i < 16; i++) {
        int threshold = -80 + i * 4;  /* -80, -76, -72, ..., -20 */
        new_input[i] = (st->rssi >= threshold) ? T_POS : T_NEG;
    }

    /* ── [16..23] Pattern ID one-hot (4 patterns × 2 trits) ──
     * Pattern p → trits [16 + p*2] and [16 + p*2 + 1] are (+1,+1).
     * All others are (-1,-1). */
    for (int p = 0; p < 4; p++) {
        int idx = 16 + p * 2;
        if (st->pattern_id == p) {
            new_input[idx]     = T_POS;
            new_input[idx + 1] = T_POS;
        } else {
            new_input[idx]     = T_NEG;
            new_input[idx + 1] = T_NEG;
        }
    }

    /* ── [24..87] Payload bits → trits ──
     * 8 bytes × 8 bits = 64 trits.
     * bit=1 → +1, bit=0 → -1. Full information, no zeros. */
    for (int b = 0; b < 8; b++) {
        uint8_t byte = st->payload[b];
        for (int bit = 0; bit < 8; bit++) {
            new_input[24 + b * 8 + bit] = (byte & (1 << bit)) ? T_POS : T_NEG;
        }
    }

    /* ── [88..103] MTFP21 gap history (5 × 3 trits + variance flag) ──
     * Replaces 16-trit thermometer. Encodes last 5 inter-packet gaps
     * as MTFP21 (2 exponent + 1 mantissa per gap). Trit [103] is a
     * gap variance flag (+1 bursty, -1 steady). See MTFP21_TIMING_ENCODING.md. */
    int64_t now_us = esp_timer_get_time();
    int64_t gap_ms = 0;
    if (espnow_last_rx_us > 0) {
        gap_ms = (now_us - espnow_last_rx_us) / 1000;
        if (gap_ms < 0) gap_ms = 0;
        if (gap_ms > 9999) gap_ms = 9999;
    }
    espnow_last_rx_us = now_us;

    encode_gap_history((int)gap_ms, new_input);

    /* ── [104..127] Zeroed — sequence + reserved ──
     * Sequence counter is monotonic and not pattern-specific.
     * Previously encoded as binary thermometer + bit extraction.
     * Already masked in classification signatures (commit 5735119).
     * Now silenced at the source: zero trits contribute nothing to
     * the GIE dot product (tmul(0, x) = 0), eliminating ~661K
     * unnecessary AND+popcount operations per second. */
    /* (Already zeroed by memset at top of function) */

    /* Check if input actually changed */
    if (memcmp(new_input, cfc.input, CFC_INPUT_DIM) == 0) return 0;

    /* Commit new input */
    memcpy(cfc.input, new_input, CFC_INPUT_DIM);
    return 1;
}

/**
 * Encode a ring buffer entry into cfc.input[128].
 * Uses the entry's real arrival timestamp for inter-packet gap.
 * Returns 1 if input changed, 0 if unchanged.
 * Also returns the inter-packet gap in *out_gap_ms (if non-NULL).
 */
int espnow_encode_rx_entry(const espnow_rx_entry_t *entry,
                                   int64_t *out_gap_ms) {
    int8_t new_input[CFC_INPUT_DIM];
    memset(new_input, 0, sizeof(new_input));

    const espnow_packet_t *pkt = &entry->pkt;

    /* [0..15] RSSI thermometer */
    for (int i = 0; i < 16; i++) {
        int threshold = -80 + i * 4;
        new_input[i] = (entry->rssi >= threshold) ? T_POS : T_NEG;
    }

    /* [16..23] Pattern ID one-hot */
    for (int p = 0; p < 4; p++) {
        int idx = 16 + p * 2;
        if (pkt->pattern_id == p) {
            new_input[idx]     = T_POS;
            new_input[idx + 1] = T_POS;
        } else {
            new_input[idx]     = T_NEG;
            new_input[idx + 1] = T_NEG;
        }
    }

    /* [24..87] Payload bits → trits */
    for (int b = 0; b < 8; b++) {
        uint8_t byte = pkt->payload[b];
        for (int bit = 0; bit < 8; bit++) {
            new_input[24 + b * 8 + bit] = (byte & (1 << bit)) ? T_POS : T_NEG;
        }
    }

    /* [88..103] Inter-packet timing — from REAL arrival timestamps */
    int64_t gap_ms = 0;
    if (espnow_last_rx_us > 0) {
        gap_ms = (entry->rx_timestamp_us - espnow_last_rx_us) / 1000;
        if (gap_ms < 0) gap_ms = 0;
        if (gap_ms > 9999) gap_ms = 9999;
    }
    espnow_last_rx_us = entry->rx_timestamp_us;
    if (out_gap_ms) *out_gap_ms = gap_ms;

    encode_gap_history((int)gap_ms, new_input);

    /* [104..119] Sequence features */
    uint32_t seq_lo = pkt->sequence & 0x0F;
    for (int i = 0; i < 8; i++) {
        int idx = 104 + i;
        if (i < 4) {
            new_input[idx] = (seq_lo > (uint32_t)(i * 4)) ? T_POS : T_NEG;
        } else {
            uint32_t seq_hi = (pkt->sequence >> 4) & 0x0F;
            new_input[idx] = (seq_hi > (uint32_t)((i - 4) * 4)) ? T_POS : T_NEG;
        }
    }
    for (int i = 0; i < 8; i++) {
        uint32_t bit = (pkt->sequence >> i) & 1;
        new_input[112 + i] = bit ? T_POS : T_NEG;
    }

    /* [120..127] Reserved — zeroed */

    if (memcmp(new_input, cfc.input, CFC_INPUT_DIM) == 0) return 0;
    memcpy(cfc.input, new_input, CFC_INPUT_DIM);
    return 1;
}

/**
 * Re-premultiply and re-encode the INPUT portion of all neuron buffers.
 * Called from main loop when cfc.input[] changes.
 *
 * SAFETY: The ISR only re-encodes hidden portion (indices CFC_INPUT_DIM..CFC_CONCAT_DIM).
 * This function only touches indices 0..CFC_INPUT_DIM-1. No overlap, no lock needed.
 * However, we must ensure the ISR doesn't read a half-updated buffer.
 * The neuron buffer is 80 bytes. Input portion = bytes [0..63].
 * The ISR reads the complete buffer via GDMA, but GDMA reads from SRAM autonomously —
 * we're racing against GDMA, not the ISR. A single neuron takes ~4us to stream.
 * We update 64 neurons × 64 bytes = 4KB. At 160MHz, memcpy of 64 bytes takes ~0.4us.
 * Worst case: GDMA reads a buffer while we're mid-write. This would produce a
 * garbage dot for ONE neuron in ONE loop iteration — a single-bit transient
 * that the ternary tsign() maps to {-1,0,+1}. Acceptable noise.
 */
void update_gie_input(void) {
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        /* f-pathway: neuron n */
        for (int i = 0; i < CFC_INPUT_DIM; i++)
            all_products[n][i] = tmul(cfc.W_f[n][i], cfc.input[i]);

        /* g-pathway: neuron n + 32 */
        int g_idx = n + CFC_HIDDEN_DIM;
        for (int i = 0; i < CFC_INPUT_DIM; i++)
            all_products[g_idx][i] = tmul(cfc.W_g[n][i], cfc.input[i]);
    }

    /* Re-encode input portion of all neuron buffers.
     * Input portion = bytes [0..CFC_INPUT_DIM/2 - 1] = bytes [0..63]. */
    for (int n = 0; n < NUM_NEURONS; n++) {
        uint8_t *buf = neuron_bufs[n];
        for (int i = 0; i < CFC_INPUT_DIM; i += 2) {
            int t0 = all_products[n][i];
            int t1 = (i + 1 < CFC_INPUT_DIM) ? all_products[n][i + 1] : 0;
            buf[i / 2] = encode_trit_pair(t0, t1);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  HOMEOSTATIC TERNARY LEARNING
 *
 *  The gate weights (W_f) learn to produce f=0 (HOLD) for familiar
 *  input patterns. During warmup, the CfC sees the environment's
 *  typical patterns. For each neuron where |f_dot| is large (gate
 *  is firing), we zero out the weight that contributes most to the
 *  dot product. This reduces |f_dot| for that input, pushing the
 *  gate toward HOLD.
 *
 *  After warmup, familiar patterns produce f≈0 (hold), and novel
 *  patterns produce |f|>0 (update/invert) — the reflex.
 *
 *  W_g (candidate weights) are NOT adapted. They provide the
 *  diverse candidate values that the gate selects from.
 *
 *  Safety: Called from main loop while GIE runs. Weight changes
 *  cause a transient in one neuron for one GIE loop — acceptable
 *  noise, same as update_gie_input().
 * ══════════════════════════════════════════════════════════════════ */

/**
 * Apply one step of homeostatic learning to W_f.
 *
 * The goal: tune W_f so that dot(W_f[n], concat) ≈ 0 for familiar
 * inputs (gate holds). When a novel input arrives, |dot| > 0 and
 * the gate fires — that's the reflex.
 *
 * Rule: For each neuron where |f_dot| > threshold, FLIP one weight
 * that's contributing to |f_dot|. Flipping W_f[n][i] to its negative
 * changes the dot product by -2 for this input (contribution goes
 * from +1 to -1 or vice versa). This is a balanced operation —
 * the total number of non-zero weights stays constant.
 *
 * Rate limiting: Only modify each neuron once per `period` GIE loops.
 *
 * Returns number of weight modifications made.
 */
static uint32_t homeo_last_loop[CFC_HIDDEN_DIM] = {0};

int cfc_homeostatic_step(int dot_threshold, int period) {
    int mods = 0;
    uint32_t current_loop = (uint32_t)loop_count;

    /* Build the current concat vector: [input | hidden] */
    int8_t concat[CFC_CONCAT_DIM];
    memcpy(concat, cfc.input, CFC_INPUT_DIM);
    memcpy(concat + CFC_INPUT_DIM, (void*)cfc.hidden, CFC_HIDDEN_DIM);

    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        /* Rate limit: skip if we modified this neuron recently */
        if ((current_loop - homeo_last_loop[n]) < (uint32_t)period)
            continue;

        int f_dot = loop_dots[n];

        /* Gate near zero → already at homeostasis for this input. Skip. */
        if (f_dot >= -dot_threshold && f_dot <= dot_threshold)
            continue;

        /* Find a weight that contributes to |f_dot| and FLIP it.
         * Pick pseudo-randomly among contributing weights. */
        int best_i = -1;

        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            if (cfc.W_f[n][i] == T_ZERO || concat[i] == T_ZERO)
                continue;
            int contrib = tmul(cfc.W_f[n][i], concat[i]);  /* +1 or -1 */
            /* contrib has the same sign as f_dot means this weight helped fire */
            if ((f_dot > 0 && contrib > 0) || (f_dot < 0 && contrib < 0)) {
                if (best_i < 0 || (cfc_rand() % 3) == 0) {
                    best_i = i;
                }
            }
        }

        if (best_i >= 0) {
            /* FLIP the weight: +1 → -1 or -1 → +1.
             * This changes the dot product by ±2 for this input,
             * pushing it toward zero. Weight count stays constant. */
            cfc.W_f[n][best_i] = -cfc.W_f[n][best_i];
            homeo_last_loop[n] = current_loop;
            mods++;

            /* Re-premultiply this neuron's buffer (f-pathway only) */
            for (int i = 0; i < CFC_CONCAT_DIM; i++)
                all_products[n][i] = tmul(cfc.W_f[n][i], concat[i]);

            /* Re-encode the full neuron buffer */
            for (int i = 0; i < CFC_CONCAT_DIM; i += 2) {
                int t0 = all_products[n][i];
                int t1 = (i + 1 < CFC_CONCAT_DIM) ? all_products[n][i + 1] : 0;
                neuron_bufs[n][i / 2] = encode_trit_pair(t0, t1);
            }
        }
    }

    return mods;
}

/* ══════════════════════════════════════════════════════════════════
 *  PIPELINE PRIME
 * ══════════════════════════════════════════════════════════════════ */

void prime_pipeline(void) {
    printf("[PRIME] Warming pipeline...\n");
    int8_t zeros[CFC_MAX_DIM];
    memset(zeros, 0, sizeof(zeros));
    encode_neuron_buffer(neuron_bufs[0], zeros, 128);

    memset(&neuron_descs[0], 0, sizeof(lldesc_t));
    neuron_descs[0].size = NEURON_BUF_SIZE;
    neuron_descs[0].length = NEURON_BUF_SIZE;
    neuron_descs[0].buf = neuron_bufs[0];
    neuron_descs[0].owner = 1;
    neuron_descs[0].eof = 1;

    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;

    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    clear_all_pcnt();

    uint32_t base_reg = GDMA_OUT_BASE(bare_ch);
    /* Reset PARLIO FIFO */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 |= (1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(10);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(10);

    /* Reset + start GDMA — out_eof_mode=0 (no PARLIO handshake required) */
    REG32(base_reg + GDMA_OUT_CONF0) = GDMA_RST_BIT;
    esp_rom_delay_us(10);
    REG32(base_reg + GDMA_OUT_CONF0) = 0;
    REG32(base_reg + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;
    uint32_t addr = ((uint32_t)&neuron_descs[0]) & GDMA_LINK_ADDR_MASK;
    REG32(base_reg + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);
    tx_cfg0 |= (NEURON_BUF_SIZE << 2);
    tx_cfg0 |= (1 << 18);
    tx_cfg0 |= (1 << 19);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;

    esp_rom_delay_us(1500);

    /* Stop */
    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(1 << 19);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    tx_cfg0 |= (1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(5);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;

    for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    printf("[PRIME] Done.\n\n");
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════
 *  LP CORE INITIALIZATION
 *
 *  Pack ternary weights into the LP core's geometric format and
 *  load the LP binary into LP SRAM.
 * ══════════════════════════════════════════════════════════════════ */

/* Pack a trit vector into (pos_mask, neg_mask) for LP core.
 * 16 trits per 32-bit word. */
void pack_trits_for_lp(const int8_t *trits, int n_trits,
                               volatile uint32_t *pos, volatile uint32_t *neg,
                               int n_words) {
    for (int w = 0; w < n_words; w++) {
        uint32_t p = 0, n = 0;
        for (int b = 0; b < 16 && (w * 16 + b) < n_trits; b++) {
            int8_t t = trits[w * 16 + b];
            if (t > 0) p |= (1u << b);
            if (t < 0) n |= (1u << b);
        }
        pos[w] = p;
        neg[w] = n;
    }
}

/* LP CfC weights — separate from the GIE weights.
 * The LP core has its own network that takes GIE hidden state
 * as input and produces a higher-level decision. */
int8_t lp_W_f[LP_HIDDEN_DIM][LP_CONCAT_DIM];
int8_t lp_W_g[LP_HIDDEN_DIM][LP_CONCAT_DIM];

void init_lp_core_weights(uint32_t seed) {
    /* Generate LP-local weights using same PRNG */
    cfc_seed(seed);

    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        for (int i = 0; i < LP_CONCAT_DIM; i++) {
            lp_W_f[n][i] = rand_trit(40);
            lp_W_g[n][i] = rand_trit(40);
        }
    }

    /* Pack into LP SRAM in geometric format.
     * ULP build system exports all symbols as flat uint32_t.
     * The LP core's lp_W_f_pos[16][3] is accessed via byte offset
     * from &ulp_lp_W_f_pos. Each neuron's 3 words = 12 bytes. */
    volatile uint32_t *wf_pos = (volatile uint32_t *)ulp_addr(&ulp_lp_W_f_pos);
    volatile uint32_t *wf_neg = (volatile uint32_t *)ulp_addr(&ulp_lp_W_f_neg);
    volatile uint32_t *wg_pos = (volatile uint32_t *)ulp_addr(&ulp_lp_W_g_pos);
    volatile uint32_t *wg_neg = (volatile uint32_t *)ulp_addr(&ulp_lp_W_g_neg);

    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        pack_trits_for_lp(lp_W_f[n], LP_CONCAT_DIM,
                          &wf_pos[n * LP_PACKED_WORDS],
                          &wf_neg[n * LP_PACKED_WORDS],
                          LP_PACKED_WORDS);
        pack_trits_for_lp(lp_W_g[n], LP_CONCAT_DIM,
                          &wg_pos[n * LP_PACKED_WORDS],
                          &wg_neg[n * LP_PACKED_WORDS],
                          LP_PACKED_WORDS);
    }

    /* Initialize LP hidden state to zero */
    memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
    ulp_lp_step_count = 0;
    ulp_lp_decision = 0;
    ulp_lp_command = 0;
}

void start_lp_core(void) {
    /* Binary is already loaded by caller — do NOT reload here,
     * as that would zero BSS and wipe out the packed weights. */
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = 10000,  /* 10ms = ~100 Hz */
    };

    esp_err_t err = ulp_lp_core_run(&cfg);
    if (err != ESP_OK) {
        printf("[LP] FAILED to start: %d\n", err);
        return;
    }
    printf("[LP] Geometric processor running (100 Hz, 16 MHz RISC-V)\n");
}

/* Feed GIE hidden state to LP core — called from main loop context,
 * NOT from ISR (LP SRAM bus contention stalls the ISR). */
void feed_lp_core(void) {
    memcpy(ulp_addr(&ulp_gie_hidden), (void*)cfc.hidden, CFC_HIDDEN_DIM);
    ulp_lp_command = 1;
}

/* CPU reference: compute LP CfC dot products for verification */
void cpu_lp_reference(const int8_t *gie_h, const int8_t *lp_h,
                              int *dots_f, int *dots_g) {
    int8_t concat[LP_CONCAT_DIM];
    memcpy(concat, gie_h, LP_GIE_HIDDEN);
    memcpy(concat + LP_GIE_HIDDEN, lp_h, LP_HIDDEN_DIM);

    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        int sum_f = 0, sum_g = 0;
        for (int i = 0; i < LP_CONCAT_DIM; i++) {
            sum_f += tmul(lp_W_f[n][i], concat[i]);
            sum_g += tmul(lp_W_g[n][i], concat[i]);
        }
        dots_f[n] = sum_f;
        dots_g[n] = sum_g;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  LP HEBBIAN WEIGHT UPDATE (Pillar 3: Self-Organizing Representation)
 *
 *  Applies a ternary Hebbian rule to LP core f-pathway weights based
 *  on VDB mismatch. For each LP neuron where the current lp_hidden
 *  disagrees with the VDB best match's LP portion, flip one W_f weight
 *  that contributed to the current f_dot direction.
 *
 *  This is the CLS consolidation path: the VDB (hippocampus) trains
 *  the LP weights (neocortex) through retrieval under stable conditions.
 *
 *  IMPORTANT: Only LP weights are updated. GIE W_f is NOT touched.
 *  The structural wall (W_f hidden = 0, 100% TriX accuracy) stays intact.
 *  Learning improves the temporal context extraction (LP), which improves
 *  gate bias quality (Phase 5), which improves GIE selectivity — without
 *  ever modifying the classifier.
 *
 *  Called from the HP core's feedback loop when gating conditions are met:
 *  (1) retrieval stability (same top-1 for K consecutive CMD 5 calls)
 *  (2) TriX agreement (classifier and retrieval agree on pattern)
 *  (3) rate limiting (one call per N wake cycles)
 *
 *  Returns: number of weight flips applied (0..LP_HIDDEN_DIM).
 * ══════════════════════════════════════════════════════════════════ */

int lp_hebbian_step(void) {
    /* ── 1. Read the VDB best match ID from LP SRAM ── */
    int source_id = (int)ulp_fb_source_id;
    if (source_id < 0 || source_id >= VDB_MAX_NODES || source_id == 0xFF)
        return 0;

    /* ── 2. Read the best match node's LP hidden portion (trits 32..47) ──
     * Node layout: 6 words (3 pos + 3 neg) + 8 bytes graph metadata = 32 bytes.
     * Word 2 of pos_mask = bits 0..15 = trits 32..47 = the LP portion. */
    volatile uint32_t *nodes = (volatile uint32_t *)ulp_addr(&ulp_vdb_nodes);
    int node_word_off = source_id * 8;  /* 32 bytes / 4 = 8 words per node */
    uint32_t target_pos = nodes[node_word_off + 2];  /* pos_mask word 2 */
    uint32_t target_neg = nodes[node_word_off + 5];  /* neg_mask word 2 (offset 12B+8B=20B = word 5) */

    /* Decode target LP hidden (16 trits) */
    int8_t target[LP_HIDDEN_DIM];
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        int p = (target_pos >> i) & 1;
        int n = (target_neg >> i) & 1;
        target[i] = p ? T_POS : (n ? T_NEG : T_ZERO);
    }

    /* ── 3. Read current LP hidden from LP SRAM ── */
    int8_t lp_h[LP_HIDDEN_DIM];
    memcpy(lp_h, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);

    /* ── 4. Read f-pathway dots from LP SRAM ── */
    int32_t dots_f[LP_HIDDEN_DIM];
    memcpy(dots_f, ulp_addr(&ulp_lp_dots_f), LP_HIDDEN_DIM * sizeof(int32_t));

    /* ── 5. Build the concat vector [gie_hidden | lp_hidden] ── */
    int8_t concat[LP_CONCAT_DIM];
    memcpy(concat, ulp_addr(&ulp_gie_hidden), LP_GIE_HIDDEN);
    memcpy(concat + LP_GIE_HIDDEN, lp_h, LP_HIDDEN_DIM);

    /* ── 6. Hebbian update: for each neuron with error, flip one W_f weight ── */
    int flips = 0;

    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        /* Skip if target and current agree (no error) */
        if (target[n] == lp_h[n]) continue;
        /* Skip if target is zero (memory has no opinion for this trit) */
        if (target[n] == T_ZERO) continue;

        int f_dot = dots_f[n];

        /* Find a W_f weight that contributed to the current f_dot direction
         * and flip it. Same logic as cfc_homeostatic_step(): pick pseudo-
         * randomly among contributing weights.
         *
         * If f_dot > 0: flip a weight with positive contribution (push f_dot down)
         * If f_dot < 0: flip a weight with negative contribution (push f_dot up)
         * If f_dot == 0: the gate held when it should have fired — flip any
         *   non-zero contributing weight to increase |f_dot|. */
        int best_i = -1;

        for (int i = 0; i < LP_CONCAT_DIM; i++) {
            if (lp_W_f[n][i] == T_ZERO || concat[i] == T_ZERO)
                continue;
            int contrib = tmul(lp_W_f[n][i], concat[i]);

            if (f_dot > 0 && contrib > 0) {
                if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
            } else if (f_dot < 0 && contrib < 0) {
                if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
            } else if (f_dot == 0) {
                /* Gate is holding — we want it to fire. Pick any weight
                 * to perturb f_dot away from zero. */
                if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
            }
        }

        if (best_i >= 0) {
            lp_W_f[n][best_i] = -lp_W_f[n][best_i];
            flips++;
        }
    }

    /* ── 7. Repack updated LP W_f weights to LP SRAM ── */
    if (flips > 0) {
        volatile uint32_t *wf_pos = (volatile uint32_t *)ulp_addr(&ulp_lp_W_f_pos);
        volatile uint32_t *wf_neg = (volatile uint32_t *)ulp_addr(&ulp_lp_W_f_neg);
        for (int n = 0; n < LP_HIDDEN_DIM; n++) {
            pack_trits_for_lp(lp_W_f[n], LP_CONCAT_DIM,
                              &wf_pos[n * LP_PACKED_WORDS],
                              &wf_neg[n * LP_PACKED_WORDS],
                              LP_PACKED_WORDS);
        }
    }

    return flips;
}

/* ══════════════════════════════════════════════════════════════════
 *  ACCESSORS (for test harness)
 * ══════════════════════════════════════════════════════════════════ */

int gie_get_gdma_channel(void) {
    return bare_ch;
}

bool gie_isr_ready(void) {
    return gdma_isr_handle != NULL;
}
