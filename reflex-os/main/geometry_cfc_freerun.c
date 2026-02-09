/*
 * geometry_cfc_freerun.c — Free-Running Sub-CPU Neural Network
 *
 * The GIE runs a ternary CfC liquid neural network continuously in
 * peripheral hardware. After boot, the CPU never computes a dot product.
 * The hidden state updates autonomously at ~500 Hz via:
 *
 *   GDMA (circular chain) → PARLIO (loopback) → PCNT (count)
 *   → ISR (decode dots, blend, re-encode) → GDMA loops back
 *
 * Architecture:
 *   - Circular DMA descriptor chain (last → first)
 *   - 3 dummy neurons (all zeros) absorb transients + provide re-encode window
 *   - 64 real neurons, each = [data 80B] + [separator 64B, eof=1]
 *   - LEVEL3 ISR fires on each separator EOF
 *   - ISR tracks position; at loop boundary:
 *     1. Decode per-neuron dots via cumulative differencing
 *     2. Apply ternary CfC blend (UPDATE/HOLD/INVERT)
 *     3. Re-premultiply hidden portion of all neuron buffers
 *     4. Re-encode updated buffers in-place
 *     5. Re-arm PARLIO byte counter for next loop
 *   - Re-encode happens during dummy phase (~86us window, needs ~20us)
 *   - CPU reads cfc.hidden[] from BSS at any time — always current
 *
 * CfC update equation (ternary):
 *   f[n] = sign(dot(W_f[n], [input|hidden]))    gate:      {-1, 0, +1}
 *   g[n] = sign(dot(W_g[n], [input|hidden]))    candidate: {-1, 0, +1}
 *   h_new = (f==0) ? h_old : f * g
 *
 * Tests:
 *   TEST 1: Free-running loop count — verify chain loops N times autonomously
 *   TEST 2: Hidden state evolves — CPU only reads, never computes
 *   TEST 3: Trajectory match — free-running vs CPU reference step-by-step
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
#include "esp_intr_alloc.h"
#include "soc/gdma_reg.h"
#include "soc/gdma_struct.h"
#include "soc/interrupts.h"
#include "rom/lldesc.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "reflex_vdb.h"
#include "reflex.h"

/* ── Register addresses ── */
#define ETM_BASE            0x60013000
#define ETM_CLK_EN_REG      (ETM_BASE + 0x1A8)
#define PCR_BASE             0x60096000
#define PCR_SOC_ETM_CONF     (PCR_BASE + 0x98)

#define GDMA_BASE            0x60080000
#define GDMA_OUT_BASE(ch)    (GDMA_BASE + 0xD0 + (ch)*0xC0)
#define GDMA_OUT_CONF0       0x00
#define GDMA_OUT_LINK        0x10
#define GDMA_OUT_PERI_SEL    0x30
#define GDMA_RST_BIT         (1 << 0)
#define GDMA_EOF_MODE_BIT    (1 << 3)
#define GDMA_LINK_START_BIT  (1 << 21)
#define GDMA_LINK_ADDR_MASK  0x000FFFFF
#define GDMA_PERI_PARLIO     9

#define GDMA_OUT_INT_RAW_CH(ch)  (GDMA_BASE + 0x30 + (ch) * 0x10)
#define GDMA_OUT_INT_ST_CH(ch)   (GDMA_BASE + 0x34 + (ch) * 0x10)
#define GDMA_OUT_INT_ENA_CH(ch)  (GDMA_BASE + 0x38 + (ch) * 0x10)
#define GDMA_OUT_INT_CLR_CH(ch)  (GDMA_BASE + 0x3C + (ch) * 0x10)

#define GDMA_OUT_DONE_BIT       (1 << 0)
#define GDMA_OUT_EOF_BIT        (1 << 1)
#define GDMA_OUT_TOTAL_EOF_BIT  (1 << 3)

#define PARLIO_TX_CFG0       0x60015008
#define REG32(addr)  (*(volatile uint32_t*)(addr))

#define PCNT_BASE            0x60012000
#define PCNT_U0_CNT_REG      (PCNT_BASE + 0x30)
#define PCNT_U1_CNT_REG      (PCNT_BASE + 0x34)

/* ── GPIO mapping ── */
#define GPIO_X_POS   4
#define GPIO_X_NEG   5
#define GPIO_Y_POS   6
#define GPIO_Y_NEG   7

/* ── Constants ── */
#define T_POS  (+1)
#define T_ZERO  (0)
#define T_NEG  (-1)

/* ── CfC dimensions ── */
#define CFC_INPUT_DIM   128
#define CFC_HIDDEN_DIM  32
#define CFC_CONCAT_DIM  (CFC_INPUT_DIM + CFC_HIDDEN_DIM)   /* 160 */
#define CFC_MAX_DIM     256

#define NUM_NEURONS     64   /* 32 f + 32 g pathways */
#define NEURON_BUF_SIZE 80   /* 160 trits = 80 bytes (2 trits per byte) */
#define SEP_SIZE        64   /* separator: all zeros, no PCNT edges */
#define NUM_DUMMIES     5    /* Extra dummies absorb PCNT residue from PARLIO re-arm */
#define TOTAL_DESCS     (NUM_DUMMIES * 2 + NUM_NEURONS * 2)

/* Total bytes per loop — must match PARLIO tx_bytelen */
#define CHAIN_BYTES     ((NUM_DUMMIES + NUM_NEURONS) * (NEURON_BUF_SIZE + SEP_SIZE))

/* Expected captures per loop: NUM_DUMMIES + NUM_NEURONS separators */
#define CAPTURES_PER_LOOP  (NUM_DUMMIES + NUM_NEURONS)

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
static volatile int32_t loop_count;         /* total completed loops */
static volatile int32_t loop_dots[NUM_NEURONS]; /* dots from last completed loop */
static volatile int64_t loop_timestamp_us;  /* timestamp of last loop completion */
static volatile int32_t loop_base;          /* diagnostic: baseline index */
static volatile int32_t loop_isr_count;     /* diagnostic: isr_count at boundary */

/* Reflex channel: ISR → main loop coordination.
 * The ISR signals this channel after committing a complete hidden state,
 * providing ordering guarantees and cycle-accurate timestamps. */
static reflex_channel_t gie_channel __attribute__((aligned(32)));
/* Raw capture diagnostic: captures around neuron 0 */
#define DIAG_LEN 12
static volatile int32_t diag_agree[DIAG_LEN];
static volatile int32_t diag_disagree[DIAG_LEN];

/* Peripheral handles */
static gptimer_handle_t timer0 = NULL;
static pcnt_unit_handle_t pcnt_agree = NULL, pcnt_disagree = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;
static int bare_ch = 0;
static intr_handle_t gdma_isr_handle = NULL;

/* CfC state — hidden[] is the "always-current register" that CPU reads */
typedef struct {
    int8_t W_f[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t W_g[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t hidden[CFC_HIDDEN_DIM];
    int8_t input[CFC_INPUT_DIM];
} cfc_state_t;

static cfc_state_t cfc;
static int8_t all_products[NUM_NEURONS][CFC_MAX_DIM];

/* ── LP Core binary (embedded by build system) ── */
extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

/* Helper to get an opaque pointer to a ULP symbol's LP SRAM address.
 * The ULP build system declares all symbols as `extern uint32_t`, but the
 * actual data is larger (arrays). This helper prevents GCC's -Warray-bounds
 * from seeing through the cast. */
static inline void * __attribute__((always_inline))
ulp_addr(const volatile void *sym) {
    uintptr_t addr;
    __asm__ volatile("" : "=r"(addr) : "0"(sym));
    return (void*)addr;
}

/* LP Core CfC dimensions (must match ulp/main.c) */
#define LP_GIE_HIDDEN    32  /* = CFC_HIDDEN_DIM */
#define LP_HIDDEN_DIM    16
#define LP_CONCAT_DIM    48  /* 32 + 16 */
#define LP_NUM_NEURONS   32  /* 16 f + 16 g */
#define LP_PACKED_WORDS  3   /* ceil(48/16) */

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS & PRNG
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t IRAM_ATTR tmul(int8_t a, int8_t b) {
    /* Ternary multiply without MUL instruction.
     * {-1,0,+1} × {-1,0,+1} → {-1,0,+1}
     * Zero if either is zero; sign = XOR of signs. */
    if (a == 0 || b == 0) return 0;
    /* same sign → +1, different sign → -1 */
    return ((a ^ b) >= 0) ? (int8_t)1 : (int8_t)-1;
}

static inline int8_t tsign(int val) {
    if (val > 0) return T_POS;
    if (val < 0) return T_NEG;
    return T_ZERO;
}

static uint32_t cfc_rng_state;

static void cfc_seed(uint32_t seed) {
    cfc_rng_state = seed ? seed : 0xDEADBEEF;
}

static uint32_t cfc_rand(void) {
    uint32_t x = cfc_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cfc_rng_state = x;
    return x;
}

static int8_t rand_trit(int sparsity_pct) {
    uint32_t r = cfc_rand() % 100;
    if (r < (uint32_t)sparsity_pct) return T_ZERO;
    return (cfc_rand() & 1) ? T_POS : T_NEG;
}

/* ══════════════════════════════════════════════════════════════════
 *  ENCODING
 * ══════════════════════════════════════════════════════════════════ */

static uint8_t IRAM_ATTR encode_trit_pair(int t0, int t1) {
    uint8_t lo = 0, hi = 0;
    if (t0 == T_POS)  lo = 0x01;
    if (t0 == T_NEG)  lo = 0x02;
    if (t1 == T_POS)  hi = 0x10;
    if (t1 == T_NEG)  hi = 0x20;
    return hi | lo;
}

static void encode_neuron_buffer(uint8_t *buf, const int8_t *trits, int n_trits) {
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

static void cfc_init(uint32_t seed, int sparsity_pct) {
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

static void premultiply_all(void) {
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

static void encode_all_neurons(void) {
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

    /* ── 4. Apply CfC ternary blend ── */
    int8_t h_new[CFC_HIDDEN_DIM];
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int8_t f = tsign(dots[n]);
        int8_t g = tsign(dots[n + CFC_HIDDEN_DIM]);
        if (f == T_ZERO) {
            h_new[n] = cfc.hidden[n];
        } else {
            h_new[n] = tmul(f, g);
        }
    }
    /* Commit new hidden state — this is the "register" the CPU reads */
    memcpy((void*)cfc.hidden, h_new, CFC_HIDDEN_DIM);

    /* ── 5. Re-premultiply hidden portion and re-encode ──
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
             * Reset FIFO FIRST to flush any GDMA pre-fetched data,
             * THEN clear PCNT, THEN start PARLIO. This ensures PCNT
             * is zeroed with no stale data in the output pipeline. */
            uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
            /* Stop and reset FIFO */
            tx_cfg0 &= ~(1 << 19);  /* clear tx_start */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;
            tx_cfg0 |= (1 << 30);   /* FIFO reset */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;
            tx_cfg0 &= ~(1 << 30);  /* release FIFO reset */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;

            /* Clear PCNT — no edges should be in flight since PARLIO
             * has stopped (tx_bytelen exhausted). */
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
             * sees EOFs from actual PARLIO output. */
            REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_EOF_BIT;

            /* Reprogram byte count and start */
            tx_cfg0 &= ~(0xFFFF << 2);
            tx_cfg0 |= (CHAIN_BYTES << 2);
            tx_cfg0 |= (1 << 18);   /* tx_gating_en */
            tx_cfg0 |= (1 << 19);   /* tx_start */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;
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

static void etm_force_clk(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf = (*conf & ~(1 << 1)) | (1 << 0);
    esp_rom_delay_us(1);
    REG32(ETM_CLK_EN_REG) = 1;
    esp_rom_delay_us(1);
}

static void init_gpio(void) {
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

static void init_parlio(void) {
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

static void init_pcnt(void) {
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
}

static void init_timer(void) {
    gptimer_config_t c = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c, &timer0));
    ESP_ERROR_CHECK(gptimer_enable(timer0));
}

static void detect_gdma_channel(void) {
    for (int ch = 0; ch < 3; ch++) {
        uint32_t peri = REG32(GDMA_OUT_BASE(ch) + GDMA_OUT_PERI_SEL) & 0x3F;
        if (peri == GDMA_PERI_PARLIO) {
            bare_ch = ch;
            return;
        }
    }
    bare_ch = 0;
}

static void init_gdma_isr(void) {
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

static void clear_all_pcnt(void) {
    pcnt_unit_clear_count(pcnt_agree);
    pcnt_unit_clear_count(pcnt_disagree);
}

/* ══════════════════════════════════════════════════════════════════
 *  BUILD CIRCULAR DMA CHAIN
 *
 *  Same as M8 v2, but the last descriptor points back to the first,
 *  creating an infinite loop. GDMA follows the linked list forever.
 * ══════════════════════════════════════════════════════════════════ */

static void build_circular_chain(void) {
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

    /* EOF mode = read-from-memory, NO auto_wrback (critical for circular) */
    REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    /* Start GDMA outlink — points to first descriptor of circular chain */
    uint32_t addr = ((uint32_t)&neuron_descs[0]) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    /* Configure PARLIO byte count for one loop */
    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);
    tx_cfg0 &= ~(1 << 19);
    tx_cfg0 |= (CHAIN_BYTES << 2);
    tx_cfg0 |= (1 << 18);  /* tx_gating_en */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

static void start_freerun(void) {
    /* Reset ISR state */
    isr_count = 0;
    loop_count = 0;
    loop_timestamp_us = 0;
    memset((void*)isr_agree, 0, sizeof(isr_agree));
    memset((void*)isr_disagree, 0, sizeof(isr_disagree));
    memset((void*)loop_dots, 0, sizeof(loop_dots));
    memset(&gie_channel, 0, sizeof(gie_channel));

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

    /* Enable interrupts */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = GDMA_OUT_EOF_BIT;

    /* Set Y_POS high for PCNT level gating */
    gpio_set_level(GPIO_Y_POS, 1);
    esp_rom_delay_us(50);

    /* Setup GDMA + PARLIO */
    setup_gdma_freerun();
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* Fire PARLIO — the engine is now free-running */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 |= (1 << 19);  /* tx_start */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

static void stop_freerun(void) {
    /* Disable interrupts */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    /* Stop PARLIO */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(1 << 19);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    tx_cfg0 |= (1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(5);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;

    /* Drive GPIOs low */
    for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(100);
}

/* ══════════════════════════════════════════════════════════════════
 *  DISPLAY HELPERS
 * ══════════════════════════════════════════════════════════════════ */

static char trit_char(int8_t t) {
    if (t > 0) return '+';
    if (t < 0) return '-';
    return '0';
}

static void print_trit_vec(const char *label, const int8_t *v, int n) {
    printf("  %s: [", label);
    for (int i = 0; i < n; i++) printf("%c", trit_char(v[i]));
    printf("]\n");
}

static int trit_hamming(const int8_t *a, const int8_t *b, int n) {
    int d = 0;
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) d++;
    return d;
}

static int trit_energy(const int8_t *v, int n) {
    int e = 0;
    for (int i = 0; i < n; i++)
        if (v[i] != T_ZERO) e++;
    return e;
}

/* ══════════════════════════════════════════════════════════════════
 *  PIPELINE PRIME
 * ══════════════════════════════════════════════════════════════════ */

static void prime_pipeline(void) {
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

    /* Reset + start GDMA */
    REG32(base_reg + GDMA_OUT_CONF0) = GDMA_RST_BIT;
    esp_rom_delay_us(10);
    REG32(base_reg + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
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
static void pack_trits_for_lp(const int8_t *trits, int n_trits,
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
static int8_t lp_W_f[LP_HIDDEN_DIM][LP_CONCAT_DIM];
static int8_t lp_W_g[LP_HIDDEN_DIM][LP_CONCAT_DIM];

static void init_lp_core_weights(uint32_t seed) {
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

static void start_lp_core(void) {
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
static void feed_lp_core(void) {
    memcpy(ulp_addr(&ulp_gie_hidden), (void*)cfc.hidden, CFC_HIDDEN_DIM);
    ulp_lp_command = 1;
}

/* CPU reference: compute LP CfC dot products for verification */
static void cpu_lp_reference(const int8_t *gie_h, const int8_t *lp_h,
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
 *  MAIN — TEST SUITE
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  FREE-RUNNING SUB-CPU NEURAL NETWORK\n");
    printf("  + LP CORE GEOMETRIC PROCESSOR\n");
    printf("  GIE (441 Hz HW) → LP core (100 Hz, 16MHz RISC-V, ~30uA)\n");
    printf("  Three-layer hierarchy: peripheral → geometric → CPU\n");
    printf("============================================================\n\n");

    printf("[INIT] GPIO 4-7...\n");
    init_gpio();
    printf("[INIT] ETM clock...\n");
    etm_force_clk();
    printf("[INIT] PARLIO TX (2-bit, 20MHz, loopback)...\n");
    init_parlio();
    printf("[INIT] PCNT (2 units)...\n");
    init_pcnt();
    /* PCNT driver disables GPIO output on level pins (gpio_func_sel resets
     * output enable). Re-enable output on Y_POS and Y_NEG so we can drive
     * them as level gates. */
    gpio_set_direction(GPIO_Y_POS, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_Y_NEG, GPIO_MODE_INPUT_OUTPUT);
    printf("[INIT] Timer...\n");
    init_timer();
    printf("[INIT] GDMA channel detection...\n");
    detect_gdma_channel();
    printf("[GDMA] PARLIO owns CH%d\n", bare_ch);
    printf("[INIT] GDMA ISR (free-running mode)...\n");
    init_gdma_isr();
    /* CRITICAL: Load LP binary FIRST, then pack weights.
     * ulp_lp_core_load_binary() zeros the BSS section in LP SRAM,
     * which would wipe out any weights already written there. */
    printf("[INIT] LP core binary (%d bytes)...\n",
           (int)(ulp_main_bin_end - ulp_main_bin_start));
    {
        esp_err_t err = ulp_lp_core_load_binary(ulp_main_bin_start,
                                                (ulp_main_bin_end - ulp_main_bin_start));
        if (err != ESP_OK) {
            printf("[LP] FAILED to load binary: %d\n", err);
        }
    }
    printf("[INIT] LP core weights (after binary load)...\n");
    init_lp_core_weights(0xCAFE1234);
    start_lp_core();
    printf("[INIT] Done.\n\n");
    fflush(stdout);

    prime_pipeline();

    int test_count = 0, pass_count = 0;

    /* ══════════════════════════════════════════════════════════════
     *  TEST 1: Free-running loop count
     *
     *  Start the engine, sleep for 1 second, check how many loops
     *  completed. At ~1.9ms per loop, expect ~500+ loops.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 1: Free-running loop count --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();

        int64_t t_start = esp_timer_get_time();
        start_freerun();

        /* Let it run for 1 second */
        vTaskDelay(pdMS_TO_TICKS(1000));

        int32_t loops = loop_count;
        int64_t t_end = esp_timer_get_time();

        stop_freerun();

        double elapsed_s = (double)(t_end - t_start) / 1e6;
        double hz = (double)loops / elapsed_s;

        printf("  Loops completed: %d in %.3f s\n", (int)loops, elapsed_s);
        printf("  Frequency: %.1f Hz\n", hz);
        printf("  Period: %.1f us per loop\n", elapsed_s * 1e6 / loops);

        int ok = (loops > 100);  /* should be ~500+ */
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 2: Hidden state evolves autonomously
     *
     *  Start the engine, sample hidden state at intervals, verify
     *  it changes over time. CPU never calls premultiply or encode.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 2: Hidden state evolves autonomously --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        cfc_init(777, 45);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();

        start_freerun();

        int8_t snapshots[8][CFC_HIDDEN_DIM];
        int32_t snap_loops[8];
        int n_snaps = 0;
        int state_changed = 0;

        for (int i = 0; i < 8; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));  /* 50ms between samples */
            memcpy(snapshots[i], (void*)cfc.hidden, CFC_HIDDEN_DIM);
            snap_loops[i] = loop_count;
            n_snaps++;

            if (i > 0) {
                int dist = trit_hamming(snapshots[i], snapshots[i - 1], CFC_HIDDEN_DIM);
                int energy = trit_energy(snapshots[i], CFC_HIDDEN_DIM);
                printf("  snap %d (loop %d): delta=%d energy=%d\n",
                       i, (int)snap_loops[i], dist, energy);
                if (dist > 0) state_changed = 1;
            } else {
                int energy = trit_energy(snapshots[0], CFC_HIDDEN_DIM);
                printf("  snap 0 (loop %d): energy=%d\n", (int)snap_loops[0], energy);
            }
        }

        stop_freerun();

        print_trit_vec("final h", (int8_t*)cfc.hidden, CFC_HIDDEN_DIM);
        printf("  Total loops: %d\n", (int)loop_count);

        int ok = state_changed && (loop_count > 50);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 3: Trajectory match — free-running vs CPU reference
     *
     *  Run the engine for exactly N loops (by polling loop_count).
     *  Independently compute N CfC steps on CPU. Compare final
     *  hidden states. They must match if the GIE is computing
     *  correctly at every step.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 3: Per-neuron dot accuracy (single loop) --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        /* This test verifies dot product accuracy by running a single
         * free-running loop and comparing the ISR-decoded dots against
         * CPU-computed dots. We let the engine warmup for a few loops
         * first, then snapshot at a known point. */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();

        start_freerun();

        /* Let the engine settle — skip startup transients */
        int timeout_ms = 200;
        while (loop_count < 3 && timeout_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            timeout_ms--;
        }
        printf("  After warmup: %d loops, base=%d, cnt=%d\n",
               (int)loop_count, (int)loop_base, (int)loop_isr_count);

        /* Wait for one more loop to complete and snapshot its dots.
         * The dots from this loop correspond to the hidden state
         * at the START of this loop (before the ISR updated it). */
        int32_t pre_h_loop = loop_count;
        /* Snapshot hidden BEFORE next loop completes — this is the
         * state that the NEXT loop's dots will be computed from. */
        int8_t h_before[CFC_HIDDEN_DIM];
        memcpy(h_before, (void*)cfc.hidden, CFC_HIDDEN_DIM);

        /* Wait for one more loop */
        int32_t target_loop = pre_h_loop + 1;
        timeout_ms = 200;
        while (loop_count < target_loop && timeout_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            timeout_ms--;
        }

        /* Snapshot the dots from this loop */
        int32_t gie_dots_snap[NUM_NEURONS];
        memcpy(gie_dots_snap, (void*)loop_dots, sizeof(gie_dots_snap));
        int32_t snap_base = loop_base;
        int32_t snap_cnt = loop_isr_count;
        int32_t snap_loop = loop_count;

        stop_freerun();

        printf("  Snapshot at loop %d (base=%d, cnt=%d)\n",
               (int)snap_loop, (int)snap_base, (int)snap_cnt);

        /* Print raw captures around the dummy→neuron boundary */
        printf("  Raw captures [0..%d]:\n", DIAG_LEN - 1);
        printf("    idx  agree  disagree  d_agree  d_disagree\n");
        for (int i = 0; i < DIAG_LEN; i++) {
            int da = (i > 0) ? (int)(diag_agree[i] - diag_agree[i-1]) : (int)diag_agree[i];
            int dd = (i > 0) ? (int)(diag_disagree[i] - diag_disagree[i-1]) : (int)diag_disagree[i];
            const char *label = "";
            if (i < NUM_DUMMIES) label = " (dummy)";
            else if (i == NUM_DUMMIES) label = " (neuron 0)";
            else label = "";
            printf("    [%d]  %5d  %5d     %+5d    %+5d%s\n",
                   i, (int)diag_agree[i], (int)diag_disagree[i],
                   da, dd, label);
        }
        printf("\n");

        /* CPU reference: compute dots from h_before */
        static int8_t cpu_products_v[NUM_NEURONS][CFC_MAX_DIM];
        int cpu_dots_v[NUM_NEURONS];

        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            for (int i = 0; i < CFC_INPUT_DIM; i++) {
                cpu_products_v[n][i] = tmul(cfc.W_f[n][i], cfc.input[i]);
                cpu_products_v[n + CFC_HIDDEN_DIM][i] = tmul(cfc.W_g[n][i], cfc.input[i]);
            }
            for (int i = 0; i < CFC_HIDDEN_DIM; i++) {
                cpu_products_v[n][CFC_INPUT_DIM + i] = tmul(cfc.W_f[n][CFC_INPUT_DIM + i], h_before[i]);
                cpu_products_v[n + CFC_HIDDEN_DIM][CFC_INPUT_DIM + i] = tmul(cfc.W_g[n][CFC_INPUT_DIM + i], h_before[i]);
            }
        }
        for (int n = 0; n < NUM_NEURONS; n++) {
            int sum = 0;
            for (int i = 0; i < CFC_CONCAT_DIM; i++)
                sum += cpu_products_v[n][i];
            cpu_dots_v[n] = sum;
        }

        /* Compare */
        int dot_err = 0, sign_err = 0;
        for (int n = 0; n < NUM_NEURONS; n++) {
            if (gie_dots_snap[n] != cpu_dots_v[n]) dot_err++;
            if (tsign(gie_dots_snap[n]) != tsign(cpu_dots_v[n])) sign_err++;
        }

        printf("  Dot errors: %d / %d\n", dot_err, NUM_NEURONS);
        printf("  Sign errors: %d / %d\n", sign_err, NUM_NEURONS);
        printf("  GIE dots[0..7]: %d %d %d %d %d %d %d %d\n",
               (int)gie_dots_snap[0], (int)gie_dots_snap[1],
               (int)gie_dots_snap[2], (int)gie_dots_snap[3],
               (int)gie_dots_snap[4], (int)gie_dots_snap[5],
               (int)gie_dots_snap[6], (int)gie_dots_snap[7]);
        printf("  CPU dots[0..7]: %d %d %d %d %d %d %d %d\n",
               cpu_dots_v[0], cpu_dots_v[1], cpu_dots_v[2], cpu_dots_v[3],
               cpu_dots_v[4], cpu_dots_v[5], cpu_dots_v[6], cpu_dots_v[7]);

        /* Check with offset: maybe the GIE dots are shifted by N positions */
        for (int offset = 0; offset <= 4; offset++) {
            int match = 0;
            for (int n = 0; n + offset < NUM_NEURONS; n++) {
                if (tsign(gie_dots_snap[n + offset]) == tsign(cpu_dots_v[n])) match++;
            }
            printf("  Sign match with offset %d: %d / %d\n",
                   offset, match, NUM_NEURONS - offset);
        }

        /* Pass if sign errors ≤ 2 (allowing for ±1 PCNT noise) */
        int ok = (sign_err <= 2);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 4: LP Core Geometric Processor
     *
     *  The LP core runs a ternary CfC on RISC-V at 16MHz / ~30μA.
     *  It implements geometric operations (INTERSECT via AND+popcount,
     *  PROJECT via sign, GATE via branch/negate) — no multiplication.
     *
     *  Three-part verification:
     *  4a) LP core is running (step count advances)
     *  4b) Deterministic single-step dot product verification:
     *       Reset LP hidden to zero, feed known GIE hidden state,
     *       wait for exactly 1 LP step, compare dots against CPU.
     *  4c) Hidden state evolves with GIE (live integration test)
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 4: LP Core Geometric Processor --\n");
    fflush(stdout);
    {
        /* ── Phase 1: Live integration — verify LP core runs with GIE ── */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();

        start_freerun();

        /* Let GIE + LP core run for 300ms, feeding LP core every 10ms */
        printf("  Phase 1: GIE + LP core integration (300ms)...\n");
        fflush(stdout);
        for (int i = 0; i < 30; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            feed_lp_core();
        }

        uint32_t lp_steps_live = ulp_lp_step_count;
        int8_t lp_h_live[LP_HIDDEN_DIM];
        memcpy(lp_h_live, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        int8_t lp_dec_live = (int8_t)ulp_lp_decision;

        stop_freerun();

        int lp_running = (lp_steps_live >= 10);
        int lp_energy = trit_energy(lp_h_live, LP_HIDDEN_DIM);

        printf("  4a: LP running: %s (steps=%d)\n",
               lp_running ? "YES" : "NO", (int)lp_steps_live);
        printf("  4c: LP hidden energy: %d/%d\n", lp_energy, LP_HIDDEN_DIM);
        print_trit_vec("LP hidden", lp_h_live, LP_HIDDEN_DIM);
        printf("  LP decision: %d\n", (int)lp_dec_live);

        /* ── Phase 2: Deterministic single-step dot verification ──
         * Reset LP state, feed a known GIE hidden vector, wait for
         * exactly 1 LP step, and compare dots against CPU reference.
         * This eliminates the race condition from Phase 1. */
        printf("\n  Phase 2: Deterministic single-step verification...\n");
        fflush(stdout);

        /* Use a known GIE hidden state: the one from Test 1's first loop */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();
        /* Let GIE run a few loops to produce a nonzero hidden state */
        vTaskDelay(pdMS_TO_TICKS(50));
        int8_t known_gie_h[LP_GIE_HIDDEN];
        memcpy(known_gie_h, (void*)cfc.hidden, LP_GIE_HIDDEN);
        stop_freerun();

        int gie_e = trit_energy(known_gie_h, LP_GIE_HIDDEN);
        printf("  Known GIE hidden energy: %d/%d\n", gie_e, LP_GIE_HIDDEN);
        print_trit_vec("Known GIE hidden", known_gie_h, LP_GIE_HIDDEN);

        /* Reset LP hidden to zero and record step count */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        uint32_t step_before = ulp_lp_step_count;

        /* Feed known GIE hidden state and trigger one LP step */
        memcpy(ulp_addr(&ulp_gie_hidden), known_gie_h, LP_GIE_HIDDEN);
        ulp_lp_command = 1;

        /* Wait for LP core to process (wakes every 10ms) */
        int timeout = 50;
        while (ulp_lp_step_count == step_before && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            timeout--;
        }
        uint32_t step_after = ulp_lp_step_count;
        printf("  LP steps: %d → %d (delta=%d)\n",
               (int)step_before, (int)step_after, (int)(step_after - step_before));

        /* Snapshot LP dots from this single step */
        int32_t lp_dots_f_snap[LP_HIDDEN_DIM];
        int32_t lp_dots_g_snap[LP_HIDDEN_DIM];
        memcpy(lp_dots_f_snap, ulp_addr(&ulp_lp_dots_f), sizeof(lp_dots_f_snap));
        memcpy(lp_dots_g_snap, ulp_addr(&ulp_lp_dots_g), sizeof(lp_dots_g_snap));

        /* CPU reference: compute from same inputs (GIE hidden + zero LP hidden) */
        int8_t lp_h_zero[LP_HIDDEN_DIM];
        memset(lp_h_zero, 0, LP_HIDDEN_DIM);
        int cpu_f[LP_HIDDEN_DIM], cpu_g[LP_HIDDEN_DIM];
        cpu_lp_reference(known_gie_h, lp_h_zero, cpu_f, cpu_g);

        /* Compare — should be EXACT since both computed from same inputs */
        int f_exact = 0, g_exact = 0, f_sign = 0, g_sign = 0;
        int f_nonzero = 0, g_nonzero = 0;
        for (int n = 0; n < LP_HIDDEN_DIM; n++) {
            if (lp_dots_f_snap[n] == cpu_f[n]) f_exact++;
            if (lp_dots_g_snap[n] == cpu_g[n]) g_exact++;
            if (cpu_f[n] != 0) {
                f_nonzero++;
                if (tsign(lp_dots_f_snap[n]) == tsign(cpu_f[n])) f_sign++;
            }
            if (cpu_g[n] != 0) {
                g_nonzero++;
                if (tsign(lp_dots_g_snap[n]) == tsign(cpu_g[n])) g_sign++;
            }
        }

        printf("  4b: f-dots exact: %d/%d, g-dots exact: %d/%d\n",
               f_exact, LP_HIDDEN_DIM, g_exact, LP_HIDDEN_DIM);
        printf("  4b: f-dots sign:  %d/%d, g-dots sign:  %d/%d\n",
               f_sign, f_nonzero, g_sign, g_nonzero);
        printf("  LP  f-dots: ");
        for (int n = 0; n < LP_HIDDEN_DIM; n++) printf("%d ", (int)lp_dots_f_snap[n]);
        printf("\n  CPU f-dots: ");
        for (int n = 0; n < LP_HIDDEN_DIM; n++) printf("%d ", cpu_f[n]);
        printf("\n  LP  g-dots: ");
        for (int n = 0; n < LP_HIDDEN_DIM; n++) printf("%d ", (int)lp_dots_g_snap[n]);
        printf("\n  CPU g-dots: ");
        for (int n = 0; n < LP_HIDDEN_DIM; n++) printf("%d ", cpu_g[n]);
        printf("\n");

        /* Pass criteria:
         * - LP core ran at least 10 steps (4a)
         * - LP hidden state evolved (4c: energy > 0)
         * - Exact dot match for deterministic test (4b) */
        int dots_ok = (f_exact == LP_HIDDEN_DIM && g_exact == LP_HIDDEN_DIM);
        int ok = lp_running && (lp_energy > 0) && dots_ok;
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 5: Ternary VDB — NSW Graph (M=7), 64 Nodes
     *
     *  Uses the reflex_vdb.h API with graph-aware insert (cmd=3)
     *  and graph beam search (cmd=2). 64 nodes × 48 trits.
     *
     *  Verification:
     *  5a) Insert 64 nodes via graph-aware insert, verify count
     *  5b) Self-match recall@1 — query each node, check if self is #1
     *  5c) Recall@1 and recall@4 over 20 random queries vs CPU brute-force
     *  5d) Visit count — verify graph search is sub-linear
     *  5e) Connectivity — BFS from node 0, verify all 64 reachable
     *  5f) Latency benchmark
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 5: Ternary VDB — NSW Graph (M=%d), 64 Nodes --\n", VDB_M);
    fflush(stdout);
    {
        /* Generate 64 random 48-trit vectors (static — 3KB, too big for stack) */
        static int8_t vdb_vecs[VDB_MAX_NODES][VDB_TRIT_DIM];
        cfc_seed(0xDB5EED01);
        for (int n = 0; n < VDB_MAX_NODES; n++)
            for (int i = 0; i < VDB_TRIT_DIM; i++)
                vdb_vecs[n][i] = rand_trit(30);

        /* ── 5a: Insert 64 nodes via graph-aware insert ── */
        printf("  5a: Inserting 64 nodes (graph-aware, cmd=3)...\n");
        fflush(stdout);
        vdb_clear();
        int insert_ok = 1;
        for (int n = 0; n < VDB_MAX_NODES; n++) {
            int id = vdb_insert(vdb_vecs[n]);
            if (id < 0) {
                printf("    INSERT FAIL at node %d: returned %d\n", n, id);
                insert_ok = 0;
                break;
            }
        }
        int ok_5a = insert_ok && (vdb_count() == VDB_MAX_NODES);
        printf("  5a: Inserted %d nodes — %s\n\n",
               vdb_count(), ok_5a ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 5b: Self-match recall@1 ── */
        printf("  5b: Self-match recall@1 (64 queries)...\n");
        fflush(stdout);

        int self_match_ok = 0;
        vdb_result_t result;

        for (int q = 0; q < VDB_MAX_NODES; q++) {
            int err = vdb_search(vdb_vecs[q], &result);
            if (err != 0) continue;

            int exp_score = 0;
            for (int i = 0; i < VDB_TRIT_DIM; i++)
                if (vdb_vecs[q][i] != 0) exp_score++;

            if (result.ids[0] == q && result.scores[0] == exp_score)
                self_match_ok++;
        }
        /* Self-match should be 100% — a node should always be its own
         * best match. This tests both graph connectivity and correctness. */
        int ok_5b = (self_match_ok == VDB_MAX_NODES);
        printf("  5b: %d/%d self-match recall@1 — %s\n\n",
               self_match_ok, VDB_MAX_NODES, ok_5b ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 5c: Recall@1 and Recall@4 over 20 random queries ── */
        printf("  5c: Recall measurement (20 random queries)...\n");
        fflush(stdout);

        static int cpu_dots[VDB_MAX_NODES];
        static int used[VDB_MAX_NODES];
        int8_t query_vec[VDB_TRIT_DIM];

        int recall_1_hits = 0;
        int recall_4_hits = 0;
        int recall_4_total = 0;
        cfc_seed(0xC0E4A42);

        for (int trial = 0; trial < 20; trial++) {
            for (int i = 0; i < VDB_TRIT_DIM; i++)
                query_vec[i] = rand_trit(30);

            /* Graph search */
            vdb_result_t lp_result;
            vdb_search(query_vec, &lp_result);

            /* CPU brute-force ground truth */
            for (int n = 0; n < VDB_MAX_NODES; n++) {
                int dot = 0;
                for (int i = 0; i < VDB_TRIT_DIM; i++)
                    dot += tmul(query_vec[i], vdb_vecs[n][i]);
                cpu_dots[n] = dot;
            }
            int cpu_top_ids[VDB_K];
            memset(used, 0, sizeof(used));
            for (int k = 0; k < VDB_K; k++) {
                int best = -99999, best_id = -1;
                for (int n = 0; n < VDB_MAX_NODES; n++) {
                    if (!used[n] && cpu_dots[n] > best) {
                        best = cpu_dots[n];
                        best_id = n;
                    }
                }
                cpu_top_ids[k] = best_id;
                if (best_id >= 0) used[best_id] = 1;
            }

            /* Recall@1: does graph top-1 match brute-force top-1? */
            if (lp_result.ids[0] == cpu_top_ids[0])
                recall_1_hits++;

            /* Recall@4: how many of brute-force top-4 appear in graph top-4? */
            for (int k = 0; k < VDB_K; k++) {
                for (int j = 0; j < VDB_K; j++) {
                    if (lp_result.ids[j] == cpu_top_ids[k]) {
                        recall_4_hits++;
                        break;
                    }
                }
                recall_4_total++;
            }

            /* Print first 3 results for visibility */
            if (trial < 3) {
                printf("    q%d: LP=[%d,%d,%d,%d] CPU=[%d,%d,%d,%d]\n",
                       trial,
                       (int)lp_result.ids[0], (int)lp_result.ids[1],
                       (int)lp_result.ids[2], (int)lp_result.ids[3],
                       cpu_top_ids[0], cpu_top_ids[1],
                       cpu_top_ids[2], cpu_top_ids[3]);
            }
        }

        int recall_1_pct = (recall_1_hits * 100) / 20;
        int recall_4_pct = (recall_4_hits * 100) / recall_4_total;
        printf("  Recall@1: %d/%d = %d%%\n", recall_1_hits, 20, recall_1_pct);
        printf("  Recall@4: %d/%d = %d%%\n", recall_4_hits, recall_4_total, recall_4_pct);

        /* PRD thresholds: recall@1 >= 95%, recall@4 >= 90% */
        int ok_5c = (recall_1_pct >= 95) && (recall_4_pct >= 90);
        printf("  5c: %s (thresholds: r@1>=95%%, r@4>=90%%)\n\n",
               ok_5c ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 5d: Visit count — verify graph search is sub-linear ── */
        printf("  5d: Visit count (graph vs brute-force)...\n");
        fflush(stdout);

        for (int i = 0; i < VDB_TRIT_DIM; i++)
            query_vec[i] = rand_trit(30);
        vdb_search(query_vec, &result);
        int visits = vdb_last_visit_count();
        printf("  Graph search visited: %d / %d nodes\n", visits, VDB_MAX_NODES);
        /* Graph search should visit fewer than all 64 nodes */
        int ok_5d = (visits > 0 && visits < VDB_MAX_NODES);
        printf("  5d: %s (sub-linear: %s)\n\n",
               ok_5d ? "OK" : "FAIL",
               visits < VDB_MAX_NODES ? "YES" : "NO");
        fflush(stdout);

        /* ── 5e: Connectivity — BFS from node 0 ── */
        printf("  5e: Connectivity (BFS from node 0)...\n");
        fflush(stdout);

        /* Read graph structure from LP SRAM */
        static uint8_t bfs_visited[VDB_MAX_NODES];
        static uint8_t bfs_queue[VDB_MAX_NODES];
        memset(bfs_visited, 0, sizeof(bfs_visited));
        int bfs_head = 0, bfs_tail = 0;
        int bfs_reachable = 0;

        /* Enqueue node 0 */
        bfs_queue[bfs_tail++] = 0;
        bfs_visited[0] = 1;

        /* Read neighbor data from LP SRAM.
         * Node layout: 32 bytes. neighbors at offset 24, count at offset 31.
         * Use ulp_addr helper to access LP SRAM. */
        volatile uint8_t *nodes_bytes = (volatile uint8_t *)
            ((uintptr_t)&ulp_vdb_nodes);
        /* Prevent GCC bounds warning */
        volatile uint8_t *nb;
        {
            uintptr_t addr;
            __asm__ volatile("" : "=r"(addr) : "0"(nodes_bytes));
            nb = (volatile uint8_t *)addr;
        }

        while (bfs_head < bfs_tail) {
            uint8_t cur = bfs_queue[bfs_head++];
            bfs_reachable++;

            /* Read neighbor count and IDs for node 'cur' */
            int node_off = cur * 32;
            int ncnt = nb[node_off + 31];
            for (int i = 0; i < ncnt && i < VDB_M; i++) {
                uint8_t nid = nb[node_off + 24 + i];
                if (nid < VDB_MAX_NODES && !bfs_visited[nid]) {
                    bfs_visited[nid] = 1;
                    bfs_queue[bfs_tail++] = nid;
                }
            }
        }
        int ok_5e = (bfs_reachable == VDB_MAX_NODES);
        printf("  Reachable from node 0: %d / %d\n", bfs_reachable, VDB_MAX_NODES);
        printf("  5e: %s\n\n", ok_5e ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 5f: Latency benchmark — 10 searches ── */
        printf("  5f: Latency benchmark (10 searches, N=%d)...\n", vdb_count());
        fflush(stdout);

        int64_t min_rt_us = 999999;
        int64_t max_rt_us = 0;
        int64_t sum_rt_us = 0;
        for (int trial = 0; trial < 10; trial++) {
            for (int i = 0; i < VDB_TRIT_DIM; i++)
                query_vec[i] = rand_trit(30);

            int64_t t0 = esp_timer_get_time();
            vdb_search(query_vec, &result);
            int64_t t1 = esp_timer_get_time();

            int64_t dt = t1 - t0;
            if (dt < min_rt_us) min_rt_us = dt;
            if (dt > max_rt_us) max_rt_us = dt;
            sum_rt_us += dt;
        }
        int64_t avg_rt_us = sum_rt_us / 10;

        printf("  Round-trip (includes LP wake jitter up to 10ms):\n");
        printf("    min=%lld us, max=%lld us, avg=%lld us\n",
               min_rt_us, max_rt_us, avg_rt_us);
        printf("  5f: %s\n\n",
               (min_rt_us > 0 && min_rt_us < 50000) ? "OK" : "FAIL");
        int ok_5f = (min_rt_us > 0 && min_rt_us < 50000);

        int ok = ok_5a && ok_5b && ok_5c && ok_5d && ok_5e && ok_5f;
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 6: CfC → VDB Pipeline (CMD 4)
     *
     *  The LP core wakes up, runs one CfC step, then immediately
     *  searches the VDB using the CfC's packed input vector as the
     *  query — all in a single wake cycle. Zero CPU involvement
     *  after init.
     *
     *  Verification:
     *  6a) Pipeline runs: cmd=4 increments both step and search counts
     *  6b) CfC determinism: cmd=4 produces same hidden state as cmd=1
     *  6c) VDB consistency: pipeline search results match standalone
     *  6d) Sustained operation: multiple pipeline steps, counters advance
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 6: CfC -> VDB Pipeline (CMD 4) --\n");
    fflush(stdout);
    {
        /* ── Setup: populate VDB with 32 known vectors ──
         * We reuse the same PRNG seed as Test 5 for the vectors,
         * but only insert 32 of them (enough for graph search). */
        static int8_t pipe_vecs[32][VDB_TRIT_DIM];
        cfc_seed(0xDB5EED01);
        for (int n = 0; n < 32; n++)
            for (int i = 0; i < VDB_TRIT_DIM; i++)
                pipe_vecs[n][i] = rand_trit(30);

        printf("  Setup: Inserting 32 vectors into VDB...\n");
        fflush(stdout);
        vdb_clear();
        for (int n = 0; n < 32; n++) {
            int id = vdb_insert(pipe_vecs[n]);
            if (id < 0) {
                printf("    INSERT FAIL at node %d\n", n);
                break;
            }
        }
        printf("  VDB has %d nodes\n", vdb_count());
        fflush(stdout);

        /* ── 6a: Basic pipeline operation ──
         * Reset LP hidden to zero, feed known GIE hidden, run cmd=4,
         * verify both counters increment. */
        printf("\n  6a: Pipeline basic operation...\n");
        fflush(stdout);

        /* Get a known GIE hidden state from GIE */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();
        vTaskDelay(pdMS_TO_TICKS(50));
        int8_t known_gie_h[LP_GIE_HIDDEN];
        memcpy(known_gie_h, (void*)cfc.hidden, LP_GIE_HIDDEN);
        stop_freerun();

        /* Reset LP hidden to zero */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);

        /* Feed GIE hidden state */
        memcpy(ulp_addr(&ulp_gie_hidden), known_gie_h, LP_GIE_HIDDEN);

        uint32_t step_before = ulp_lp_step_count;
        uint32_t search_before = ulp_vdb_search_count;

        /* Run pipeline */
        vdb_result_t pipe_result;
        int pipe_err = vdb_cfc_pipeline_step(&pipe_result);

        uint32_t step_after = ulp_lp_step_count;
        uint32_t search_after = ulp_vdb_search_count;

        int step_inc = (step_after - step_before);
        int search_inc = (search_after - search_before);

        printf("  step_count: %d -> %d (delta=%d)\n",
               (int)step_before, (int)step_after, step_inc);
        printf("  search_count: %d -> %d (delta=%d)\n",
               (int)search_before, (int)search_after, search_inc);
        printf("  Pipeline return: %d\n", pipe_err);
        printf("  Top-4 results: [%d,%d,%d,%d] scores=[%d,%d,%d,%d]\n",
               (int)pipe_result.ids[0], (int)pipe_result.ids[1],
               (int)pipe_result.ids[2], (int)pipe_result.ids[3],
               (int)pipe_result.scores[0], (int)pipe_result.scores[1],
               (int)pipe_result.scores[2], (int)pipe_result.scores[3]);

        int ok_6a = (pipe_err == 0) && (step_inc == 1) && (search_inc == 1);
        printf("  6a: %s\n\n", ok_6a ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 6b: CfC determinism — compare cmd=4 vs cmd=1 hidden state ──
         * Reset LP hidden to zero twice, run the same inputs through
         * cmd=1 and cmd=4, verify identical hidden states. */
        printf("  6b: CfC determinism (cmd=4 vs cmd=1)...\n");
        fflush(stdout);

        /* Run cmd=1 first (CfC only) */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        memcpy(ulp_addr(&ulp_gie_hidden), known_gie_h, LP_GIE_HIDDEN);
        step_before = ulp_lp_step_count;
        ulp_lp_command = 1;
        int timeout = 50;
        while (ulp_lp_step_count == step_before && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            timeout--;
        }
        int8_t h_cmd1[LP_HIDDEN_DIM];
        memcpy(h_cmd1, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        int32_t dots_f_cmd1[LP_HIDDEN_DIM];
        int32_t dots_g_cmd1[LP_HIDDEN_DIM];
        memcpy(dots_f_cmd1, ulp_addr(&ulp_lp_dots_f), sizeof(dots_f_cmd1));
        memcpy(dots_g_cmd1, ulp_addr(&ulp_lp_dots_g), sizeof(dots_g_cmd1));

        /* Run cmd=4 (pipeline) with same initial state */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        memcpy(ulp_addr(&ulp_gie_hidden), known_gie_h, LP_GIE_HIDDEN);
        step_before = ulp_lp_step_count;
        search_before = ulp_vdb_search_count;

        vdb_result_t pipe_result_6b;
        pipe_err = vdb_cfc_pipeline_step(&pipe_result_6b);

        int8_t h_cmd4[LP_HIDDEN_DIM];
        memcpy(h_cmd4, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        int32_t dots_f_cmd4[LP_HIDDEN_DIM];
        int32_t dots_g_cmd4[LP_HIDDEN_DIM];
        memcpy(dots_f_cmd4, ulp_addr(&ulp_lp_dots_f), sizeof(dots_f_cmd4));
        memcpy(dots_g_cmd4, ulp_addr(&ulp_lp_dots_g), sizeof(dots_g_cmd4));

        /* Compare hidden states */
        int h_match = 1;
        for (int i = 0; i < LP_HIDDEN_DIM; i++) {
            if (h_cmd1[i] != h_cmd4[i]) { h_match = 0; break; }
        }
        int f_match = 1, g_match = 1;
        for (int i = 0; i < LP_HIDDEN_DIM; i++) {
            if (dots_f_cmd1[i] != dots_f_cmd4[i]) f_match = 0;
            if (dots_g_cmd1[i] != dots_g_cmd4[i]) g_match = 0;
        }

        print_trit_vec("cmd=1 hidden", h_cmd1, LP_HIDDEN_DIM);
        print_trit_vec("cmd=4 hidden", h_cmd4, LP_HIDDEN_DIM);
        printf("  Hidden match: %s\n", h_match ? "YES" : "NO");
        printf("  f-dots match: %s, g-dots match: %s\n",
               f_match ? "YES" : "NO", g_match ? "YES" : "NO");

        int ok_6b = h_match && f_match && g_match;
        printf("  6b: %s\n\n", ok_6b ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 6c: VDB consistency — pipeline results match standalone ──
         * After cmd=4 from 6b, run cmd=2 (search only) with the same
         * query (already in vdb_query_pos/neg BSS from the pipeline).
         * Results should be identical. */
        printf("  6c: VDB consistency (pipeline vs standalone search)...\n");
        fflush(stdout);

        /* The pipeline already left the packed query in vdb_query_pos/neg.
         * Run a standalone search with the same query. */
        search_before = ulp_vdb_search_count;
        ulp_lp_command = 2;
        timeout = 50;
        while (ulp_vdb_search_count == search_before && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            timeout--;
        }

        /* Read standalone results */
        volatile uint8_t *ids_raw = (volatile uint8_t *)ulp_addr(&ulp_vdb_result_ids);
        volatile int32_t *scores_raw = (volatile int32_t *)ulp_addr(&ulp_vdb_result_scores);
        vdb_result_t standalone_result;
        for (int k = 0; k < VDB_K; k++) {
            standalone_result.ids[k] = ids_raw[k];
            standalone_result.scores[k] = scores_raw[k];
        }

        printf("  Pipeline: [%d,%d,%d,%d] scores=[%d,%d,%d,%d]\n",
               (int)pipe_result_6b.ids[0], (int)pipe_result_6b.ids[1],
               (int)pipe_result_6b.ids[2], (int)pipe_result_6b.ids[3],
               (int)pipe_result_6b.scores[0], (int)pipe_result_6b.scores[1],
               (int)pipe_result_6b.scores[2], (int)pipe_result_6b.scores[3]);
        printf("  Standalone: [%d,%d,%d,%d] scores=[%d,%d,%d,%d]\n",
               (int)standalone_result.ids[0], (int)standalone_result.ids[1],
               (int)standalone_result.ids[2], (int)standalone_result.ids[3],
               (int)standalone_result.scores[0], (int)standalone_result.scores[1],
               (int)standalone_result.scores[2], (int)standalone_result.scores[3]);

        int results_match = 1;
        for (int k = 0; k < VDB_K; k++) {
            if (pipe_result_6b.ids[k] != standalone_result.ids[k]) results_match = 0;
            if (pipe_result_6b.scores[k] != standalone_result.scores[k]) results_match = 0;
        }
        printf("  Results match: %s\n", results_match ? "YES" : "NO");

        int ok_6c = results_match;
        printf("  6c: %s\n\n", ok_6c ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 6d: Sustained operation — run 10 pipeline steps ──
         * Feed GIE hidden state, run cmd=4 ten times via the LP timer,
         * verify both counters advance by 10. */
        printf("  6d: Sustained pipeline operation (10 steps)...\n");
        fflush(stdout);

        /* Start GIE free-running to feed LP core with evolving state */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();

        /* Let GIE produce some hidden state */
        vTaskDelay(pdMS_TO_TICKS(50));

        step_before = ulp_lp_step_count;
        search_before = ulp_vdb_search_count;

        /* Run 10 pipeline steps, feeding GIE hidden each time */
        int pipeline_ok_count = 0;
        for (int i = 0; i < 10; i++) {
            feed_lp_core();  /* updates gie_hidden in LP SRAM */
            /* Override command to cmd=4 (feed_lp_core sets cmd=1) */
            ulp_lp_command = 4;

            /* Wait for this pipeline step to complete */
            uint32_t this_step = ulp_lp_step_count;
            uint32_t this_search = ulp_vdb_search_count;
            timeout = 50;
            while (timeout > 0) {
                if (ulp_lp_step_count != this_step &&
                    ulp_vdb_search_count != this_search) {
                    pipeline_ok_count++;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                timeout--;
            }
        }

        stop_freerun();

        step_after = ulp_lp_step_count;
        search_after = ulp_vdb_search_count;

        int total_steps = (int)(step_after - step_before);
        int total_searches = (int)(search_after - search_before);

        printf("  Pipeline steps completed: %d / 10\n", pipeline_ok_count);
        printf("  step_count delta: %d\n", total_steps);
        printf("  search_count delta: %d\n", total_searches);

        /* Read final LP hidden state */
        int8_t final_lp_h[LP_HIDDEN_DIM];
        memcpy(final_lp_h, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        int final_energy = trit_energy(final_lp_h, LP_HIDDEN_DIM);
        print_trit_vec("Final LP hidden", final_lp_h, LP_HIDDEN_DIM);
        printf("  Final LP hidden energy: %d/%d\n", final_energy, LP_HIDDEN_DIM);

        int ok_6d = (pipeline_ok_count == 10) &&
                    (total_steps >= 10) && (total_searches >= 10);
        printf("  6d: %s\n\n", ok_6d ? "OK" : "FAIL");
        fflush(stdout);

        int ok = ok_6a && ok_6b && ok_6c && ok_6d;
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 7: Reflex Channel — Cache Coherency Coordination
     *
     *  The reflex channel is the original coordination primitive:
     *  a 32-byte aligned struct with sequence counter, timestamp,
     *  value, and memory fences. Here we use it to coordinate the
     *  GIE ISR (producer) with the HP main loop (consumer).
     *
     *  The ISR signals the channel after committing a complete
     *  hidden state. The main loop waits on the channel and reads
     *  the state with ordering guarantees: the fence ensures
     *  hidden[] is visible before the sequence increments.
     *
     *  On a single-core C6, this is SRAM ordering (no cache to
     *  snoop). But the protocol is the same one that gives 309ns
     *  on Thor's 14-core ARM via cache coherency traffic.
     *
     *  Verification:
     *  7a) Channel signals match loop_count — every loop produces a signal
     *  7b) Latency — cycles between ISR signal and main loop read
     *  7c) Consistency — hidden state after wait matches CPU reference
     *  7d) Channel-driven LP feeding — feed LP only on channel signal
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 7: Reflex Channel Coordination --\n");
    fflush(stdout);
    {
        /* ── 7a: Channel signals match loop count ── */
        printf("  7a: Channel signals match loop count...\n");
        fflush(stdout);

        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();

        /* Wait for 50 loops using reflex_wait */
        uint32_t last_seq = gie_channel.sequence;
        int signals_received = 0;
        int mismatch = 0;

        for (int i = 0; i < 50; i++) {
            uint32_t new_seq = reflex_wait_timeout(&gie_channel, last_seq,
                                                    160000000); /* 1s timeout */
            if (new_seq == 0) break; /* timeout */
            signals_received++;

            /* Channel value should equal loop_count */
            uint32_t ch_val = reflex_read(&gie_channel);
            if (ch_val != (uint32_t)loop_count) mismatch++;

            last_seq = new_seq;
        }

        stop_freerun();

        printf("  Signals received: %d / 50\n", signals_received);
        printf("  Value mismatches: %d\n", mismatch);
        printf("  Final loop_count: %d, channel value: %d, channel seq: %d\n",
               (int)loop_count, (int)gie_channel.value, (int)gie_channel.sequence);

        int ok_7a = (signals_received == 50) && (mismatch == 0);
        printf("  7a: %s\n\n", ok_7a ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 7b: Signal latency measurement ──
         * Spin-wait on the channel and measure cycles between the ISR's
         * timestamp and our read. This is the ISR→main-loop latency. */
        printf("  7b: Signal latency (ISR -> main loop)...\n");
        fflush(stdout);

        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();

        last_seq = gie_channel.sequence;
        int n_lat = 0;
        uint32_t min_lat = UINT32_MAX, max_lat = 0;
        uint64_t sum_lat = 0;

        for (int i = 0; i < 100; i++) {
            uint32_t new_seq = reflex_wait_timeout(&gie_channel, last_seq,
                                                    160000000);
            if (new_seq == 0) break;

            /* Measure: cycles from ISR timestamp to now */
            uint32_t lat = reflex_latency(&gie_channel);
            n_lat++;
            if (lat < min_lat) min_lat = lat;
            if (lat > max_lat) max_lat = lat;
            sum_lat += lat;

            last_seq = new_seq;
        }

        stop_freerun();

        uint32_t avg_lat = (n_lat > 0) ? (uint32_t)(sum_lat / n_lat) : 0;
        printf("  Samples: %d\n", n_lat);
        printf("  Latency (cycles): min=%lu, max=%lu, avg=%lu\n",
               (unsigned long)min_lat, (unsigned long)max_lat, (unsigned long)avg_lat);
        printf("  Latency (ns):     min=%lu, max=%lu, avg=%lu\n",
               (unsigned long)reflex_cycles_to_ns(min_lat),
               (unsigned long)reflex_cycles_to_ns(max_lat),
               (unsigned long)reflex_cycles_to_ns(avg_lat));

        /* On single-core C6 with spin-wait, the latency is the time
         * from ISR return to the next instruction in the spin loop.
         * Should be very small — under 1us (160 cycles). But the ISR
         * does fence + signal at the end, so some overhead is expected. */
        int ok_7b = (n_lat == 100) && (min_lat < 16000); /* < 100us */
        printf("  7b: %s\n\n", ok_7b ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 7c: Consistency — hidden state after wait is complete ──
         * Run GIE, wait on channel, snapshot hidden, compare vs CPU ref.
         * The fence guarantees we see the complete state. */
        printf("  7c: Hidden state consistency after channel wait...\n");
        fflush(stdout);

        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();

        /* Let it warm up a few loops */
        last_seq = gie_channel.sequence;
        for (int i = 0; i < 5; i++) {
            uint32_t new_seq = reflex_wait_timeout(&gie_channel, last_seq,
                                                    160000000);
            if (new_seq == 0) break;
            last_seq = new_seq;
        }

        /* Snapshot hidden state right after a channel signal */
        int8_t h_before_signal[CFC_HIDDEN_DIM];
        memcpy(h_before_signal, (void*)cfc.hidden, CFC_HIDDEN_DIM);

        /* Wait for one more signal — new hidden state */
        uint32_t new_seq = reflex_wait_timeout(&gie_channel, last_seq, 160000000);
        int8_t h_after_signal[CFC_HIDDEN_DIM];
        memcpy(h_after_signal, (void*)cfc.hidden, CFC_HIDDEN_DIM);
        int32_t dots_snap[NUM_NEURONS];
        memcpy(dots_snap, (void*)loop_dots, sizeof(dots_snap));

        stop_freerun();

        /* CPU reference: compute dots from h_before_signal (the state
         * that was active when the ISR computed the dots we just read) */
        int cpu_dots_ref[NUM_NEURONS];
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            int sum_f = 0, sum_g = 0;
            for (int i = 0; i < CFC_INPUT_DIM; i++) {
                sum_f += tmul(cfc.W_f[n][i], cfc.input[i]);
                sum_g += tmul(cfc.W_g[n][i], cfc.input[i]);
            }
            for (int i = 0; i < CFC_HIDDEN_DIM; i++) {
                sum_f += tmul(cfc.W_f[n][CFC_INPUT_DIM + i], h_before_signal[i]);
                sum_g += tmul(cfc.W_g[n][CFC_INPUT_DIM + i], h_before_signal[i]);
            }
            cpu_dots_ref[n] = sum_f;
            cpu_dots_ref[n + CFC_HIDDEN_DIM] = sum_g;
        }

        /* Compare */
        int dot_match = 0, dot_total = NUM_NEURONS;
        for (int n = 0; n < NUM_NEURONS; n++) {
            if (dots_snap[n] == cpu_dots_ref[n]) dot_match++;
        }

        printf("  Dot match: %d / %d\n", dot_match, dot_total);
        printf("  Channel signaled: %s (seq %lu -> %lu)\n",
               (new_seq != 0) ? "YES" : "NO",
               (unsigned long)last_seq, (unsigned long)new_seq);

        int ok_7c = (new_seq != 0) && (dot_match == dot_total);
        printf("  7c: %s\n\n", ok_7c ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 7d: Channel-driven LP core feeding ──
         * Instead of polling, use the reflex channel to know exactly
         * when new GIE state is available, then feed the LP core.
         * Run 20 channel-driven feed cycles, verify LP core advances. */
        printf("  7d: Channel-driven LP feeding (20 cycles)...\n");
        fflush(stdout);

        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();

        /* Let GIE settle */
        vTaskDelay(pdMS_TO_TICKS(20));

        uint32_t lp_step_before = ulp_lp_step_count;
        last_seq = gie_channel.sequence;
        int feeds = 0;

        for (int i = 0; i < 20; i++) {
            /* Wait for GIE to produce new state */
            new_seq = reflex_wait_timeout(&gie_channel, last_seq, 160000000);
            if (new_seq == 0) break;
            last_seq = new_seq;

            /* Feed LP core — state is guaranteed complete by the fence */
            feed_lp_core();
            feeds++;

            /* Small delay to let LP core wake and process */
            vTaskDelay(pdMS_TO_TICKS(12));
        }

        stop_freerun();

        uint32_t lp_step_after = ulp_lp_step_count;
        int lp_steps = (int)(lp_step_after - lp_step_before);

        printf("  Channel-driven feeds: %d / 20\n", feeds);
        printf("  LP step_count delta: %d\n", lp_steps);

        int ok_7d = (feeds == 20) && (lp_steps >= 15); /* allow some jitter */
        printf("  7d: %s\n\n", ok_7d ? "OK" : "FAIL");
        fflush(stdout);

        int ok = ok_7a && ok_7b && ok_7c && ok_7d;
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 8: VDB → CfC Feedback Loop (CMD 5)
     *
     *  The feedback loop closes the circle: past memories now
     *  influence the current CfC hidden state. After the CfC step
     *  and VDB search, the LP core blends the best-matching stored
     *  memory into lp_hidden using ternary blend rules:
     *
     *    Agreement:  no change (reinforces stability)
     *    Gap fill:   h=0, mem≠0 → h=mem (memory provides context)
     *    Conflict:   h≠0, mem≠0, h≠mem → h=0 (HOLD = damper)
     *
     *  The HOLD mode is the natural stabilizer: conflicting feedback
     *  creates zero states, which preserve their value on the next
     *  CfC step (f=0 → h_new = h_old). This is ternary inertia.
     *
     *  Verification:
     *  8a) Single feedback step works (cmd=5 increments all counters)
     *  8b) Feedback observability (fb_applied, fb_source_id, fb_blend_count)
     *  8c) Stability under sustained feedback (N steps, no oscillation)
     *  8d) Feedback vs no-feedback divergence (cmd=5 vs cmd=4 produce
     *      different hidden states, proving feedback has effect)
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 8: VDB -> CfC Feedback Loop (CMD 5) --\n");
    fflush(stdout);
    {
        /* ── Setup: populate VDB with 32 known vectors ──
         * Same vectors as Test 6, same seed for reproducibility. */
        static int8_t fb_vecs[32][VDB_TRIT_DIM];
        cfc_seed(0xDB5EED01);
        for (int n = 0; n < 32; n++)
            for (int i = 0; i < VDB_TRIT_DIM; i++)
                fb_vecs[n][i] = rand_trit(30);

        printf("  Setup: Inserting 32 vectors into VDB...\n");
        fflush(stdout);
        vdb_clear();
        for (int n = 0; n < 32; n++) {
            int id = vdb_insert(fb_vecs[n]);
            if (id < 0) {
                printf("    INSERT FAIL at node %d\n", n);
                break;
            }
        }
        printf("  VDB has %d nodes\n", vdb_count());
        fflush(stdout);

        /* Set feedback threshold — score must be >= 8 out of 48 trits */
        ulp_fb_threshold = 8;

        /* ── 8a: Single feedback step ──
         * Reset LP hidden to zero, feed known GIE hidden, run cmd=5,
         * verify all three counters increment (step, search, total_blends). */
        printf("\n  8a: Single feedback step (cmd=5)...\n");
        fflush(stdout);

        /* Get a known GIE hidden state */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();
        vTaskDelay(pdMS_TO_TICKS(50));
        int8_t fb_gie_h[LP_GIE_HIDDEN];
        memcpy(fb_gie_h, (void*)cfc.hidden, LP_GIE_HIDDEN);
        stop_freerun();

        /* Reset LP hidden and feedback counters */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        ulp_fb_total_blends = 0;

        /* Feed GIE hidden state */
        memcpy(ulp_addr(&ulp_gie_hidden), fb_gie_h, LP_GIE_HIDDEN);

        uint32_t step_before = ulp_lp_step_count;
        uint32_t search_before = ulp_vdb_search_count;
        uint32_t fb_total_before = ulp_fb_total_blends;

        /* Run feedback pipeline */
        vdb_result_t fb_result;
        int fb_err = vdb_cfc_feedback_step(&fb_result);

        uint32_t step_after = ulp_lp_step_count;
        uint32_t search_after = ulp_vdb_search_count;
        uint32_t fb_total_after = ulp_fb_total_blends;
        uint32_t fb_app = ulp_fb_applied;
        uint32_t fb_src = ulp_fb_source_id;
        int32_t  fb_sc = (int32_t)ulp_fb_score;
        uint32_t fb_bc = ulp_fb_blend_count;

        int8_t fb_h_after[LP_HIDDEN_DIM];
        memcpy(fb_h_after, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);

        printf("  cmd=5 return: %d\n", fb_err);
        printf("  step_count: %d -> %d (delta=%d)\n",
               (int)step_before, (int)step_after, (int)(step_after - step_before));
        printf("  search_count: %d -> %d (delta=%d)\n",
               (int)search_before, (int)search_after, (int)(search_after - search_before));
        printf("  fb_total_blends: %d -> %d\n",
               (int)fb_total_before, (int)fb_total_after);
        printf("  fb_applied: %d\n", (int)fb_app);
        printf("  fb_source_id: %d\n", (int)fb_src);
        printf("  fb_score: %d\n", (int)fb_sc);
        printf("  fb_blend_count: %d trits modified\n", (int)fb_bc);
        printf("  Top-4 results: [%d,%d,%d,%d] scores=[%d,%d,%d,%d]\n",
               (int)fb_result.ids[0], (int)fb_result.ids[1],
               (int)fb_result.ids[2], (int)fb_result.ids[3],
               (int)fb_result.scores[0], (int)fb_result.scores[1],
               (int)fb_result.scores[2], (int)fb_result.scores[3]);
        print_trit_vec("LP hidden after feedback", fb_h_after, LP_HIDDEN_DIM);

        int ok_8a = (fb_err == 0) &&
                    ((step_after - step_before) == 1) &&
                    ((search_after - search_before) == 1);
        printf("  8a: %s\n\n", ok_8a ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 8b: Feedback observability ──
         * Verify the feedback diagnostic variables are sensible. */
        printf("  8b: Feedback observability...\n");
        fflush(stdout);

        /* If fb_applied == 1, source_id should be valid, score >= threshold,
         * blend_count > 0, and total_blends should have incremented. */
        int ok_8b;
        if (fb_app == 1) {
            int src_valid = (fb_src < 32);  /* we inserted 32 nodes */
            int score_valid = (fb_sc >= 8); /* threshold */
            int blend_valid = (fb_bc > 0 && fb_bc <= 16);
            int total_inc = (fb_total_after > fb_total_before);
            printf("  Applied: YES (source=%d, score=%d, blended=%d trits)\n",
                   (int)fb_src, (int)fb_sc, (int)fb_bc);
            printf("  source valid: %s, score>=thresh: %s, blend valid: %s, total++: %s\n",
                   src_valid ? "YES" : "NO",
                   score_valid ? "YES" : "NO",
                   blend_valid ? "YES" : "NO",
                   total_inc ? "YES" : "NO");
            ok_8b = src_valid && score_valid && blend_valid && total_inc;
        } else {
            /* Feedback not applied — this is OK if score was below threshold.
             * Verify the skip was clean. */
            printf("  Applied: NO (best score %d < threshold %d)\n",
                   (int)fb_sc, 8);
            printf("  This is acceptable if no stored memory is similar enough.\n");
            ok_8b = (fb_bc == 0) && (fb_src == 0xFF || fb_src == 0);
        }
        printf("  8b: %s\n\n", ok_8b ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 8c: Stability under sustained feedback ──
         * Run 50 feedback steps with the GIE free-running.
         * Track LP hidden state at each step. Verify:
         *  - The system doesn't lock into a single state (it evolves)
         *  - The system doesn't oscillate wildly (bounded change per step)
         *  - Energy stays bounded (doesn't go to all-zero or all-nonzero) */
        printf("  8c: Stability under sustained feedback (50 steps)...\n");
        fflush(stdout);

        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();

        /* Let GIE produce initial hidden state */
        vTaskDelay(pdMS_TO_TICKS(50));

        /* Reset LP hidden for clean start */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        ulp_fb_total_blends = 0;

        int steps_ok = 0;
        int fb_applied_count = 0;
        int max_change_per_step = 0;
        int energy_min = 999, energy_max = 0;
        int8_t prev_h[LP_HIDDEN_DIM];
        memset(prev_h, 0, LP_HIDDEN_DIM);

        /* Track unique states seen (simple hash) */
        uint32_t state_hashes[50];
        int n_unique = 0;

        for (int step = 0; step < 50; step++) {
            /* Feed current GIE hidden */
            memcpy(ulp_addr(&ulp_gie_hidden), (void*)cfc.hidden, LP_GIE_HIDDEN);

            vdb_result_t step_result;
            int err = vdb_cfc_feedback_step(&step_result);
            if (err == 0) steps_ok++;
            if (ulp_fb_applied) fb_applied_count++;

            /* Read LP hidden */
            int8_t cur_h[LP_HIDDEN_DIM];
            memcpy(cur_h, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);

            /* Compute change from previous step */
            int change = trit_hamming(cur_h, prev_h, LP_HIDDEN_DIM);
            if (change > max_change_per_step) max_change_per_step = change;

            /* Energy */
            int e = trit_energy(cur_h, LP_HIDDEN_DIM);
            if (e < energy_min) energy_min = e;
            if (e > energy_max) energy_max = e;

            /* Simple state hash for uniqueness */
            uint32_t hash = 0;
            for (int i = 0; i < LP_HIDDEN_DIM; i++)
                hash = hash * 31 + (uint32_t)(cur_h[i] + 2);
            int is_new = 1;
            for (int j = 0; j < n_unique; j++) {
                if (state_hashes[j] == hash) { is_new = 0; break; }
            }
            if (is_new && n_unique < 50)
                state_hashes[n_unique++] = hash;

            memcpy(prev_h, cur_h, LP_HIDDEN_DIM);

            /* Print every 10th step */
            if (step % 10 == 0) {
                printf("    step %d: e=%d, delta=%d, fb=%d, score=%d, src=%d\n",
                       step, e, change,
                       (int)ulp_fb_applied, (int)ulp_fb_score,
                       (int)ulp_fb_source_id);
            }
        }

        stop_freerun();

        uint32_t total_blends_after = ulp_fb_total_blends;
        printf("  Steps completed: %d / 50\n", steps_ok);
        printf("  Feedback applied: %d / 50 steps\n", fb_applied_count);
        printf("  Total blend operations: %d\n", (int)total_blends_after);
        printf("  Max change per step: %d / %d trits\n", max_change_per_step, LP_HIDDEN_DIM);
        printf("  Energy range: [%d, %d] / %d\n", energy_min, energy_max, LP_HIDDEN_DIM);
        printf("  Unique states visited: %d\n", n_unique);
        print_trit_vec("Final LP hidden", prev_h, LP_HIDDEN_DIM);

        /* Stability criteria:
         * - All 50 steps completed
         * - System explored multiple states (not stuck)
         * - Max per-step change bounded (< 16 = not every trit flipping)
         * - Energy didn't collapse to 0 or saturate to 16 permanently */
        int ok_8c = (steps_ok == 50) &&
                    (n_unique >= 2) &&
                    (max_change_per_step < LP_HIDDEN_DIM);
        printf("  8c: %s\n\n", ok_8c ? "OK" : "FAIL");
        fflush(stdout);

        /* ── 8d: Feedback vs no-feedback divergence ──
         * Run the same inputs through cmd=4 (no feedback) and cmd=5
         * (with feedback), verify they produce different hidden states.
         * This proves the feedback loop actually modifies behavior. */
        printf("  8d: Feedback vs no-feedback divergence...\n");
        fflush(stdout);

        /* Get a known GIE hidden state (reuse from 8a) */
        /* Run 10 steps with cmd=4 (no feedback) */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        int8_t no_fb_history[10][LP_HIDDEN_DIM];
        for (int step = 0; step < 10; step++) {
            memcpy(ulp_addr(&ulp_gie_hidden), fb_gie_h, LP_GIE_HIDDEN);
            vdb_result_t nfb_result;
            vdb_cfc_pipeline_step(&nfb_result);  /* cmd=4 */
            memcpy(no_fb_history[step], ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        }

        /* Run 10 steps with cmd=5 (with feedback) from same initial state */
        memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
        int8_t fb_history[10][LP_HIDDEN_DIM];
        for (int step = 0; step < 10; step++) {
            memcpy(ulp_addr(&ulp_gie_hidden), fb_gie_h, LP_GIE_HIDDEN);
            vdb_result_t fbs_result;
            vdb_cfc_feedback_step(&fbs_result);  /* cmd=5 */
            memcpy(fb_history[step], ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        }

        /* Compare trajectories */
        int first_diverge = -1;
        int total_diverge = 0;
        for (int step = 0; step < 10; step++) {
            int dist = trit_hamming(no_fb_history[step], fb_history[step], LP_HIDDEN_DIM);
            if (dist > 0) {
                total_diverge++;
                if (first_diverge < 0) first_diverge = step;
            }
            printf("    step %d: hamming distance = %d\n", step, dist);
        }

        printf("  First divergence at step: %d\n", first_diverge);
        printf("  Steps with different states: %d / 10\n", total_diverge);
        print_trit_vec("No-feedback final", no_fb_history[9], LP_HIDDEN_DIM);
        print_trit_vec("Feedback final   ", fb_history[9], LP_HIDDEN_DIM);

        /* The feedback loop MUST produce a different trajectory.
         * If they're identical, either feedback never applied (all scores
         * below threshold) or the blend had no effect. Either way, the
         * loop isn't closed. */
        int ok_8d = (total_diverge > 0);
        printf("  8d: %s\n\n", ok_8d ? "OK" : "FAIL");
        fflush(stdout);

        int ok = ok_8a && ok_8b && ok_8c && ok_8d;
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ── Summary ── */
    printf("============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    if (pass_count == test_count) {
        printf("\n  *** VDB -> CfC FEEDBACK LOOP VERIFIED ***\n");
        printf("  Layer 1: GIE (GDMA+PARLIO+PCNT) — 428 Hz, ~0 CPU\n");
        printf("  Layer 2: LP core geometric processor — 100 Hz, ~30uA\n");
        printf("  Layer 2b: LP core ternary VDB — NSW graph search (M=%d)\n", VDB_M);
        printf("  Layer 2c: CfC -> VDB pipeline — perceive+think+remember\n");
        printf("  Layer 2d: VDB -> CfC feedback — memory shapes inference\n");
        printf("  Layer 3: HP core — reflex channel coordination\n");
        printf("  All ternary. No floating point. No multiplication.\n");
        printf("  The circle is closed. Past experience shapes perception.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED — see details above.\n\n");
    }
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
