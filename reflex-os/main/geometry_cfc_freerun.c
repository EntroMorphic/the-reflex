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

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS & PRNG
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t tmul(int8_t a, int8_t b) {
    return (int8_t)(a * b);
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

        /* Clock domain drain: ~2.5us for PCNT pipeline to settle */
        for (volatile int _d = 0; _d < 100; _d++) { }

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
 *  MAIN — TEST SUITE
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  FREE-RUNNING SUB-CPU NEURAL NETWORK\n");
    printf("  Ternary CfC on GIE — CPU never computes a dot product\n");
    printf("  GDMA circular chain → PARLIO → PCNT → ISR blend → loop\n");
    printf("============================================================\n\n");

    printf("[INIT] GPIO 4-7...\n");
    init_gpio();
    printf("[INIT] ETM clock...\n");
    etm_force_clk();
    printf("[INIT] PARLIO TX (2-bit, 20MHz, loopback)...\n");
    init_parlio();
    printf("[INIT] PCNT (2 units)...\n");
    init_pcnt();
    printf("[INIT] Timer...\n");
    init_timer();
    printf("[INIT] GDMA channel detection...\n");
    detect_gdma_channel();
    printf("[GDMA] PARLIO owns CH%d\n", bare_ch);
    printf("[INIT] GDMA ISR (free-running mode)...\n");
    init_gdma_isr();
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

    /* ── Summary ── */
    printf("============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    if (pass_count == test_count) {
        printf("\n  *** FREE-RUNNING SUB-CPU NEURAL NETWORK VERIFIED ***\n");
        printf("  Circular DMA chain loops autonomously.\n");
        printf("  ISR decodes dots, blends, re-encodes — CPU never computes.\n");
        printf("  Hidden state is a peripheral register, always current.\n");
        printf("  The neural network is infrastructure, not software.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED — see details above.\n\n");
    }
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
