/*
 * geometry_cfc_m8_etm.c — Autonomous Ternary CfC on the GIE
 *
 * The first fully-ternary CfC liquid neural network running on
 * autonomous hardware: GDMA→PARLIO→PCNT computes all 64 dot products
 * (32 f-pathway + 32 g-pathway) in a single DMA chain while the CPU
 * is free. CPU only premultiplies, encodes, fires, then applies the
 * ternary blend logic (~3 instructions per neuron).
 *
 * Architecture:
 *   - 3 dummy neurons (all zeros) absorb startup transients
 *   - 64 real neurons, each = [data 80B] + [separator 64B, eof=1]
 *   - GDMA EOF interrupt (LEVEL3) fires on each separator read-from-memory
 *   - ISR reads cumulative PCNT via direct register access (~10 cycles)
 *   - Per-neuron dot decoded by cumulative differencing
 *   - Auto-detects baseline (last (0,0) dummy capture)
 *
 * CfC update equation (ternary):
 *   f[n] = sign(dot(W_f[n], [input|hidden]))    gate:      {-1, 0, +1}
 *   g[n] = sign(dot(W_g[n], [input|hidden]))    candidate: {-1, 0, +1}
 *   if f == +1: h_new = g      (UPDATE)
 *   if f ==  0: h_new = h_old  (HOLD)
 *   if f == -1: h_new = -g     (INVERT)
 *
 * Tests:
 *   TEST 1: Chain without ISR (cumulative sum, sanity check)
 *   TEST 2: Chain with ISR (per-neuron capture, verify vs CPU)
 *   TEST 3: Throughput — 10 runs with different seeds, all must match
 *   TEST 4: CfC single step — hw dots → blend → hidden state match
 *   TEST 5: CfC 8-step trajectory — hidden state match at every step
 *   TEST 6: CfC dynamics — all three blend modes active, state evolves
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

/* ── Register addresses (verified on silicon, M5-M8) ── */
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

/* GDMA OUT interrupt registers — fixed addresses near base, NOT per-channel struct.
 * Verified against soc/gdma_reg.h: OUT CH0 starts at 0x30, stride 0x10 per channel. */
#define GDMA_OUT_INT_RAW_CH(ch)  (GDMA_BASE + 0x30 + (ch) * 0x10)
#define GDMA_OUT_INT_ST_CH(ch)   (GDMA_BASE + 0x34 + (ch) * 0x10)
#define GDMA_OUT_INT_ENA_CH(ch)  (GDMA_BASE + 0x38 + (ch) * 0x10)
#define GDMA_OUT_INT_CLR_CH(ch)  (GDMA_BASE + 0x3C + (ch) * 0x10)

/* GDMA OUT interrupt bits (from gdma_struct.h):
 *   bit 0 = out_done (last data from one outlink desc transmitted to peripheral)
 *   bit 1 = out_eof  (last data from one outlink desc read from memory)
 *   bit 2 = out_dscr_err
 *   bit 3 = out_total_eof (entire chain finished)
 */
#define GDMA_OUT_DONE_BIT       (1 << 0)
#define GDMA_OUT_EOF_BIT        (1 << 1)
#define GDMA_OUT_TOTAL_EOF_BIT  (1 << 3)

#define PARLIO_TX_CFG0       0x60015008
#define REG32(addr)  (*(volatile uint32_t*)(addr))

/* PCNT direct register access (for fast ISR reads) */
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
#define SEP_SIZE        64   /* separator: all zeros, no PCNT edges. */

/* Total descriptors: 2 dummies + 2 seps + (1 data + 1 separator) per neuron */
#define NUM_DUMMIES     3
#define TOTAL_DESCS     (NUM_DUMMIES * 2 + NUM_NEURONS * 2)

/* ── Static allocations (BSS) ── */
static uint8_t __attribute__((aligned(4))) neuron_bufs[NUM_NEURONS][NEURON_BUF_SIZE];
static uint8_t __attribute__((aligned(4))) sep_buf[SEP_SIZE];    /* shared, all zeros */
static uint8_t __attribute__((aligned(4))) dummy_buf[NEURON_BUF_SIZE]; /* dummy neuron 0 */
static lldesc_t __attribute__((aligned(4))) neuron_descs[TOTAL_DESCS];

/* ISR capture arrays — 2 dummies + 64 real = 66 captures, plus margin */
#define ISR_CAPTURES    (NUM_DUMMIES + NUM_NEURONS + 4)
static volatile int32_t isr_agree[ISR_CAPTURES];
static volatile int32_t isr_disagree[ISR_CAPTURES];
static volatile int32_t isr_count;
static volatile int32_t chain_done;


/* Peripheral handles */
static gptimer_handle_t timer0 = NULL;
static pcnt_unit_handle_t pcnt_agree = NULL, pcnt_disagree = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;
static int bare_ch = 0;
static intr_handle_t gdma_isr_handle = NULL;

/* CfC state */
typedef struct {
    int8_t W_f[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t W_g[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t hidden[CFC_HIDDEN_DIM];
    int8_t input[CFC_INPUT_DIM];
} cfc_state_t;

static cfc_state_t cfc;
static int8_t all_products[NUM_NEURONS][CFC_MAX_DIM];
static int cpu_dots[NUM_NEURONS];

/* ══════════════════════════════════════════════════════════════════
 *  GDMA EOF ISR — Minimal, runs in IRAM
 *
 *  Captures cumulative PCNT counts after each descriptor EOF.
 *  At 20MHz, each 80-byte descriptor takes ~16us to stream.
 *  ISR body is ~10 register reads/writes = well under 1us.
 * ══════════════════════════════════════════════════════════════════ */

static void IRAM_ATTR gdma_eof_isr(void *arg) {
    /* Read masked status (INT_ST), not raw */
    uint32_t status = REG32(GDMA_OUT_INT_ST_CH(bare_ch));

    if (status & GDMA_OUT_EOF_BIT) {
        /* out_eof fires when separator is read from memory.
         * Capture cumulative PCNT — no clearing, no RMW races. */
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_EOF_BIT;

        int idx = isr_count;
        if (idx < ISR_CAPTURES) {
            isr_agree[idx] = (int16_t)(REG32(PCNT_U0_CNT_REG) & 0xFFFF);
            isr_disagree[idx] = (int16_t)(REG32(PCNT_U1_CNT_REG) & 0xFFFF);
            isr_count = idx + 1;
        }
    }

    if (status & GDMA_OUT_TOTAL_EOF_BIT) {
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_TOTAL_EOF_BIT;
        chain_done = 1;
    }

    /* Clear anything else (DONE, errors) */
    uint32_t others = status & ~(GDMA_OUT_EOF_BIT | GDMA_OUT_TOTAL_EOF_BIT);
    if (others) {
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = others;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  ENCODING
 * ══════════════════════════════════════════════════════════════════ */

static uint8_t encode_trit_pair(int t0, int t1) {
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

/* ── Helpers ── */

static void etm_force_clk(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf = (*conf & ~(1 << 1)) | (1 << 0);
    esp_rom_delay_us(1);
    REG32(ETM_CLK_EN_REG) = 1;
    esp_rom_delay_us(1);
}

/* ── Peripheral init ── */

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
        .output_clk_freq_hz = 20000000,  /* 20 MHz */
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
    /* Clear all pending OUT interrupts for our channel */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    /* Enable EOF (fires on separator eof=1) and TOTAL_EOF interrupts */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = GDMA_OUT_EOF_BIT | GDMA_OUT_TOTAL_EOF_BIT;

    /* Allocate ISR.
     * PARLIO driver claims ETS_PARL_IO_INTR_SOURCE (PARLIO peripheral interrupt).
     * The GDMA channel interrupt (ETS_DMA_OUT_CHn_INTR_SOURCE) is NOT claimed
     * by the PARLIO driver — it's free for us to use. */
    int intr_source = ETS_DMA_OUT_CH0_INTR_SOURCE + bare_ch;
    esp_err_t err = esp_intr_alloc(intr_source,
                                   ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3,
                                   gdma_eof_isr, NULL, &gdma_isr_handle);
    if (err != ESP_OK) {
        printf("[GDMA ISR] FAILED to allocate (source=%d, err=%d)\n", intr_source, err);
        printf("[GDMA ISR] Will fall back to polling mode\n");
        gdma_isr_handle = NULL;
        /* Disable interrupts since ISR isn't installed */
        REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;
    } else {
        printf("[GDMA ISR] OK — allocated on CH%d (source=%d)\n", bare_ch, intr_source);
    }
}

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

/* Build chain: [dummy0] → [sep] → [dummy1] → [sep] → [data0] → [sep] → ... → end
 *
 * Two dummy neurons (all zeros) absorb startup transients from GDMA/PARLIO.
 * Dummy 0's separator EOF may fire during GDMA prefetch (before PARLIO starts).
 * Dummy 1's separator EOF fires after PARLIO starts but before real data.
 * Use the LAST dummy capture as the baseline for cumulative differencing.
 *
 * eof=1 goes on the SEPARATOR, not the data descriptor.
 * When the separator's eof fires (memory-side read), the neuron's data
 * has already been fully transmitted through PARLIO. */
static void build_neuron_chain(void) {
    memset(sep_buf, 0, SEP_SIZE);
    memset(dummy_buf, 0, NEURON_BUF_SIZE);

    int d = 0;

    /* Two dummy neurons — absorb startup transient and PCNT clearing residue */
    for (int dum = 0; dum < NUM_DUMMIES; dum++) {
        /* Dummy data (all zeros — no PCNT edges) */
        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = NEURON_BUF_SIZE;
        neuron_descs[d].length = NEURON_BUF_SIZE;
        neuron_descs[d].buf = dummy_buf;
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 0;
        neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        d++;

        /* Dummy separator (eof=1 — ISR fires) */
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
        /* Data descriptor */
        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = NEURON_BUF_SIZE;
        neuron_descs[d].length = NEURON_BUF_SIZE;
        neuron_descs[d].buf = neuron_bufs[n];
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 0;
        neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        d++;

        /* Separator — eof=1 triggers ISR */
        memset(&neuron_descs[d], 0, sizeof(lldesc_t));
        neuron_descs[d].size = SEP_SIZE;
        neuron_descs[d].length = SEP_SIZE;
        neuron_descs[d].buf = sep_buf;
        neuron_descs[d].owner = 1;
        neuron_descs[d].eof = 1;
        if (n < NUM_NEURONS - 1) {
            neuron_descs[d].empty = (uint32_t)&neuron_descs[d + 1];
        } else {
            neuron_descs[d].empty = 0;
        }
        d++;
    }
}

static void cpu_compute_all_dots(void) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        int sum = 0;
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            sum += all_products[n][i];
        }
        cpu_dots[n] = sum;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  DETERMINISTIC INPUT GENERATOR
 * ══════════════════════════════════════════════════════════════════ */

static void gen_input(int8_t *input, uint32_t seed) {
    cfc_seed(seed);
    for (int i = 0; i < CFC_INPUT_DIM; i++)
        input[i] = rand_trit(40);  /* 40% sparse */
}

/* ══════════════════════════════════════════════════════════════════
 *  CfC STEP — apply blend logic to dot products, update hidden state
 *
 *  dots[0..31]  = f-pathway (gate)
 *  dots[32..63] = g-pathway (candidate)
 *
 *  Returns: number of UPDATE, HOLD, INVERT operations
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int8_t h_new[CFC_HIDDEN_DIM];
    int n_update, n_hold, n_invert;
} cfc_blend_result_t;

static cfc_blend_result_t cfc_apply_blend(const int *dots, int8_t *hidden) {
    cfc_blend_result_t r = {0};
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int8_t f = tsign(dots[n]);                    /* f-pathway: neuron n */
        int8_t g = tsign(dots[n + CFC_HIDDEN_DIM]);   /* g-pathway: neuron n+32 */

        if (f == T_ZERO) {
            r.h_new[n] = hidden[n];   /* HOLD */
            r.n_hold++;
        } else {
            r.h_new[n] = tmul(f, g);  /* UPDATE or INVERT */
            if (f == T_POS) r.n_update++;
            else r.n_invert++;
        }
    }
    /* Commit */
    memcpy(hidden, r.h_new, CFC_HIDDEN_DIM);
    return r;
}

/* Display helpers */
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
 *  DMA ENGINE CONTROL
 * ══════════════════════════════════════════════════════════════════ */

static void clear_all_pcnt(void) {
    pcnt_unit_clear_count(pcnt_agree);
    pcnt_unit_clear_count(pcnt_disagree);
}

static void setup_gdma(lldesc_t *first_desc, int total_bytes) {
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

    /* Clear all pending interrupts */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    uint32_t addr = ((uint32_t)first_desc) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);
    tx_cfg0 &= ~(1 << 19);
    tx_cfg0 |= (total_bytes << 2);
    tx_cfg0 |= (1 << 18);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

static void parlio_stop_and_reset(void) {
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(1 << 19);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    tx_cfg0 |= (1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(5);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

/* ══════════════════════════════════════════════════════════════════
 *  RUN CHAIN — no ISR (cumulative sum only, like M8 v1)
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int agree;
    int disagree;
    int dot;
    int64_t elapsed_us;
} chain_sum_result_t;

static chain_sum_result_t run_chain_sum(void) {
    chain_sum_result_t r = {0};
    /* dummy(80) + sep(64) + 64*(80+64) = 144 + 9216 = 9360 bytes */
    /* 2 dummies + 64 real, each = data + separator */
    int total_bytes = (NUM_DUMMIES + NUM_NEURONS) * (NEURON_BUF_SIZE + SEP_SIZE);

    /* Reset descriptor owner bits — GDMA clears these after processing */
    for (int d = 0; d < TOTAL_DESCS; d++) {
        neuron_descs[d].owner = 1;
    }

    /* Disable GDMA interrupts for this test */
    uint32_t saved_ena = REG32(GDMA_OUT_INT_ENA_CH(bare_ch));
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;

    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    clear_all_pcnt();
    setup_gdma(&neuron_descs[0], total_bytes);
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    int64_t t_start = esp_timer_get_time();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* 80 bytes * 64 neurons = 5120 bytes, * 4 symbols/byte = 20480 symbols
     * At 20 MHz: 20480 * 0.05us = 1024us + margin */
    int wait_us = (total_bytes * 4) / 20 + 500;
    esp_rom_delay_us(wait_us);
    int64_t t_end = esp_timer_get_time();

    parlio_stop_and_reset();

    pcnt_unit_get_count(pcnt_agree, &r.agree);
    pcnt_unit_get_count(pcnt_disagree, &r.disagree);
    r.dot = r.agree - r.disagree;
    r.elapsed_us = t_end - t_start;

    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    /* Restore interrupt enable */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = saved_ena;

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  RUN CHAIN — with ISR capture (per-neuron results)
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int dots[NUM_NEURONS];
    int matches;
    int64_t elapsed_us;
    int isr_fires;
} chain_isr_result_t;

static chain_isr_result_t run_chain_with_isr(void) {
    chain_isr_result_t r = {0};
    /* 2 dummies + 64 real, each = data + separator */
    int total_bytes = (NUM_DUMMIES + NUM_NEURONS) * (NEURON_BUF_SIZE + SEP_SIZE);

    /* ── Phase 1: Full pipeline quiesce ──
     * Stop PARLIO, reset GDMA, clear interrupts.
     * This ensures no residual state from a previous chain run. */
    parlio_stop_and_reset();
    esp_rom_delay_us(50);

    /* Disable and clear ALL GDMA interrupts */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;
    esp_rom_delay_us(10);
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;  /* double-clear */

    /* Reset descriptor owner bits — GDMA clears these after processing */
    for (int d = 0; d < TOTAL_DESCS; d++) {
        neuron_descs[d].owner = 1;
    }

    /* Reset ISR state — MUST happen before interrupts are enabled */
    isr_count = 0;
    chain_done = 0;
    memset((void*)isr_agree, 0, sizeof(isr_agree));
    memset((void*)isr_disagree, 0, sizeof(isr_disagree));

    /* Drive all GPIOs low and settle before enabling interrupts */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Triple-clear PCNT before anything else */
    clear_all_pcnt();
    esp_rom_delay_us(10);
    clear_all_pcnt();
    esp_rom_delay_us(10);
    clear_all_pcnt();
    esp_rom_delay_us(50);

    /* Clear and enable interrupts BEFORE setup — we want to catch ALL
     * EOFs including the dummy's, which fires during GDMA prefetch.
     * This gives us a reliable 66 captures (2 dummies + 64 real). */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = GDMA_OUT_EOF_BIT | GDMA_OUT_TOTAL_EOF_BIT;

    /* Now set Y_POS high for PCNT level gating */
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    setup_gdma(&neuron_descs[0], total_bytes);
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    chain_done = 0;

    int64_t t_start = esp_timer_get_time();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Wait for chain to complete via ISR flag, or timeout */
    int timeout_us = 20000;  /* 20ms generous timeout */
    while (!chain_done && timeout_us > 0) {
        esp_rom_delay_us(10);
        timeout_us -= 10;
    }

    /* If ISR never set chain_done, fall back to polling wait */
    if (!chain_done) {
        int wait_us = (total_bytes * 4) / 20 + 500;
        esp_rom_delay_us(wait_us);
    }

    int64_t t_end = esp_timer_get_time();

    /* ── Phase 7: Teardown ── */
    parlio_stop_and_reset();

    /* Disable interrupts until next run */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0x3F;

    /* Drive all GPIOs low and let them settle */
    for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(100);

    r.elapsed_us = t_end - t_start;

    /* Decode ISR captures.
     *
     * Chain has 3 dummies + 64 real neurons = 67 separator EOFs.
     * Dummies are all zeros — no PCNT edges.
     *
     * Find the last dummy capture (highest index with agree==disagree==0,
     * or just use the last capture before real neuron data appears).
     * Use it as baseline for differencing.
     *
     * With 3 dummies: dummy 0 absorbs GDMA prefetch transient,
     * dummy 1 absorbs PCNT clearing residue, dummy 2 provides
     * a clean (0,0) baseline after everything has settled.
     */
    /* Find baseline: scan early captures for the last one where cumulative
     * PCNT has stabilized (two consecutive captures with same values).
     * This is more robust than checking for absolute (0,0) — it handles
     * the case where PCNT clearing leaves a small residue, as long as
     * the residue is stable (same in both consecutive dummy captures).
     *
     * With 3 dummies we expect 3 early captures. Use the last one where
     * the delta from the previous capture is (0,0) as the baseline. */
    int base = -1;
    int scan_limit = (isr_count < NUM_DUMMIES + 2) ? isr_count : NUM_DUMMIES + 2;

    /* First try: find last consecutive pair with zero delta */
    for (int i = 1; i < scan_limit; i++) {
        int da = isr_agree[i] - isr_agree[i - 1];
        int dd = isr_disagree[i] - isr_disagree[i - 1];
        if (da == 0 && dd == 0) {
            base = i;  /* This capture has same cumulative as previous = stable */
        }
    }

    /* Fallback: find last absolute (0,0) */
    if (base < 0) {
        for (int i = 0; i < scan_limit; i++) {
            if (isr_agree[i] == 0 && isr_disagree[i] == 0) {
                base = i;
            }
        }
    }

    /* Last resort: use the last dummy capture */
    if (base < 0) base = (scan_limit > 0) ? scan_limit - 1 : 0;

    /* Real neuron captures start at base+1 */
    int avail = isr_count - base - 1;
    r.isr_fires = (avail > NUM_NEURONS) ? NUM_NEURONS : avail;

    for (int n = 0; n < NUM_NEURONS && (base + n + 1) < isr_count; n++) {
        int a = isr_agree[base + n + 1] - isr_agree[base + n];
        int d = isr_disagree[base + n + 1] - isr_disagree[base + n];
        r.dots[n] = a - d;
        if (r.dots[n] == cpu_dots[n]) r.matches++;
    }

    return r;
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

    /* Disable ISR during prime */
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = 0;

    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    clear_all_pcnt();
    setup_gdma(&neuron_descs[0], NEURON_BUF_SIZE);
    esp_rom_delay_us(500);
    clear_all_pcnt();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }
    esp_rom_delay_us(1500);
    parlio_stop_and_reset();
    for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);

    /* Clear any pending interrupts from prime */
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
    printf("  M8 v2: Per-Neuron ISR Capture\n");
    printf("  64 neurons × 80 bytes, GDMA EOF after each descriptor\n");
    printf("  PARLIO: 20 MHz | ISR captures cumulative PCNT\n");
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
    printf("[INIT] GDMA ISR...\n");
    init_gdma_isr();
    printf("[INIT] Done.\n\n");
    fflush(stdout);

    prime_pipeline();

    int test_count = 0, pass_count = 0;

    /* ══════════════════════════════════════════════════════════════
     *  TEST 1: Chain without ISR (cumulative sum sanity check)
     *
     *  Same as M8 v1 TEST 1 — verify the 80-byte-per-neuron chain
     *  produces correct cumulative sum. No ISR involved.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 1: Chain without ISR (cumulative sum) --\n");
    fflush(stdout);
    {
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_neuron_chain();
        cpu_compute_all_dots();

        int cpu_sum = 0;
        for (int n = 0; n < NUM_NEURONS; n++) cpu_sum += cpu_dots[n];

        chain_sum_result_t r = run_chain_sum();

        printf("  HW: agree=%d disagree=%d dot=%d\n", r.agree, r.disagree, r.dot);
        printf("  CPU: sum_all_dots=%d\n", cpu_sum);
        printf("  Elapsed: %lld us\n", r.elapsed_us);

        int ok = (r.dot == cpu_sum);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 2: Chain with ISR (per-neuron capture)
     *
     *  The main event. Fire the chain, ISR captures cumulative PCNT
     *  after each descriptor EOF, decode per-neuron dots.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 2: Chain with ISR (per-neuron capture) --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_neuron_chain();
        cpu_compute_all_dots();

        chain_isr_result_t r = run_chain_with_isr();

        printf("  ISR fires: %d / %d expected\n", r.isr_fires, NUM_NEURONS);
        printf("  Matches: %d / %d\n", r.matches, NUM_NEURONS);
        printf("  Elapsed: %lld us\n", r.elapsed_us);
        printf("  chain_done: %d\n", (int)chain_done);

        /* Show first few and last few */
        printf("  Sample results (HW vs CPU):\n");
        for (int i = 0; i < 4 && i < r.isr_fires; i++) {
            printf("    n[%2d]: hw=%+4d cpu=%+4d %s\n",
                   i, r.dots[i], cpu_dots[i],
                   (r.dots[i] == cpu_dots[i]) ? "OK" : "MISMATCH");
        }
        if (r.isr_fires > 8) printf("    ...\n");
        for (int i = (r.isr_fires > 4 ? r.isr_fires - 4 : 4); i < r.isr_fires; i++) {
            printf("    n[%2d]: hw=%+4d cpu=%+4d %s\n",
                   i, r.dots[i], cpu_dots[i],
                   (r.dots[i] == cpu_dots[i]) ? "OK" : "MISMATCH");
        }

        int ok = (r.isr_fires == NUM_NEURONS) && (r.matches == NUM_NEURONS);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 3: Throughput (10 runs averaged)
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 3: Throughput (10 runs) --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        int64_t total_us = 0;
        int all_match = 1;

        for (int run = 0; run < 10; run++) {
            cfc_init(1000 + run * 77, 50);
            premultiply_all();
            encode_all_neurons();
            build_neuron_chain();
            cpu_compute_all_dots();

            chain_isr_result_t r = run_chain_with_isr();
            total_us += r.elapsed_us;
            if (r.matches != NUM_NEURONS || r.isr_fires != NUM_NEURONS) {
                printf("  Run %d: isr=%d matches=%d\n",
                       run, r.isr_fires, r.matches);
                all_match = 0;
            }
        }

        double avg_us = (double)total_us / 10.0;
        printf("  Average: %.1f us per chain (with ISR capture)\n", avg_us);
        printf("  Frequency: %.1f Hz\n", 1e6 / avg_us);
        printf("  All matches: %s\n", all_match ? "YES" : "NO");

        /* Compare with M8 v1 per-neuron mode (13,190 us) and chain mode (2,139 us) */
        printf("\n  Comparison:\n");
        printf("    M8 v1 per-neuron: ~13,190 us (64 individual runs)\n");
        printf("    M8 v1 chain:       ~2,139 us (cumulative sum only)\n");
        printf("    M8 v2 ISR chain:   %.0f us (per-neuron results!)\n", avg_us);

        test_count++;
        if (all_match) pass_count++;
        printf("  %s\n\n", all_match ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 4: CfC single step — hw dots → blend → hidden state match
     *
     *  Run one CfC step via ISR chain AND via CPU. Both must produce
     *  identical hidden states (dot-for-dot verification).
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 4: CfC single step (hw vs cpu hidden state) --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        /* Two identical networks */
        static cfc_state_t cfc_cpu_ref;
        cfc_init(42, 50);
        memcpy(&cfc_cpu_ref, &cfc, sizeof(cfc_state_t));

        int8_t input[CFC_INPUT_DIM];
        gen_input(input, 9999);

        /* CPU reference step */
        memcpy(cfc_cpu_ref.input, input, CFC_INPUT_DIM);
        int cpu_ref_dots[NUM_NEURONS];
        {
            /* Premultiply using cpu_ref */
            for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
                int sum_f = 0, sum_g = 0;
                for (int i = 0; i < CFC_INPUT_DIM; i++) {
                    sum_f += cfc_cpu_ref.W_f[n][i] * input[i];
                    sum_g += cfc_cpu_ref.W_g[n][i] * input[i];
                }
                for (int i = 0; i < CFC_HIDDEN_DIM; i++) {
                    sum_f += cfc_cpu_ref.W_f[n][CFC_INPUT_DIM + i] * cfc_cpu_ref.hidden[i];
                    sum_g += cfc_cpu_ref.W_g[n][CFC_INPUT_DIM + i] * cfc_cpu_ref.hidden[i];
                }
                cpu_ref_dots[n] = sum_f;
                cpu_ref_dots[n + CFC_HIDDEN_DIM] = sum_g;
            }
        }
        cfc_blend_result_t cpu_blend = cfc_apply_blend(cpu_ref_dots, cfc_cpu_ref.hidden);

        /* HW step: premultiply, encode, chain, blend */
        memcpy(cfc.input, input, CFC_INPUT_DIM);
        premultiply_all();
        encode_all_neurons();
        build_neuron_chain();
        cpu_compute_all_dots();  /* for verification */

        chain_isr_result_t hw_r = run_chain_with_isr();
        cfc_blend_result_t hw_blend = cfc_apply_blend(hw_r.dots, cfc.hidden);

        /* Compare */
        int dot_match = 0;
        for (int n = 0; n < NUM_NEURONS; n++)
            if (hw_r.dots[n] == cpu_ref_dots[n]) dot_match++;

        int h_match = 0;
        for (int n = 0; n < CFC_HIDDEN_DIM; n++)
            if (cfc.hidden[n] == cfc_cpu_ref.hidden[n]) h_match++;

        printf("  Dots: %d/%d match\n", dot_match, NUM_NEURONS);
        printf("  Hidden: %d/%d match\n", h_match, CFC_HIDDEN_DIM);
        printf("  CPU: U=%d H=%d I=%d\n", cpu_blend.n_update, cpu_blend.n_hold, cpu_blend.n_invert);
        printf("  GIE: U=%d H=%d I=%d\n", hw_blend.n_update, hw_blend.n_hold, hw_blend.n_invert);
        printf("  Chain: %lld us, %d ISR fires\n", hw_r.elapsed_us, hw_r.isr_fires);
        print_trit_vec("h(cpu)", cfc_cpu_ref.hidden, CFC_HIDDEN_DIM);
        print_trit_vec("h(gie)", cfc.hidden, CFC_HIDDEN_DIM);

        int ok = (dot_match == NUM_NEURONS) && (h_match == CFC_HIDDEN_DIM);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 5: CfC 8-step trajectory — hidden state match at every step
     *
     *  Run 8 CfC steps with changing inputs on both hw and cpu.
     *  Hidden states must match at every step (errors accumulate).
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 5: CfC 8-step trajectory (hw vs cpu) --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        /* Two identical networks */
        static cfc_state_t cfc_cpu_ref;
        cfc_init(777, 45);
        memcpy(&cfc_cpu_ref, &cfc, sizeof(cfc_state_t));

        int all_match = 1;
        int64_t total_us = 0;

        for (int step = 0; step < 8; step++) {
            int8_t input[CFC_INPUT_DIM];
            gen_input(input, 2000 + step * 31);

            /* CPU reference step */
            memcpy(cfc_cpu_ref.input, input, CFC_INPUT_DIM);
            int cpu_ref_dots[NUM_NEURONS];
            for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
                int sum_f = 0, sum_g = 0;
                for (int i = 0; i < CFC_INPUT_DIM; i++) {
                    sum_f += cfc_cpu_ref.W_f[n][i] * input[i];
                    sum_g += cfc_cpu_ref.W_g[n][i] * input[i];
                }
                for (int i = 0; i < CFC_HIDDEN_DIM; i++) {
                    sum_f += cfc_cpu_ref.W_f[n][CFC_INPUT_DIM + i] * cfc_cpu_ref.hidden[i];
                    sum_g += cfc_cpu_ref.W_g[n][CFC_INPUT_DIM + i] * cfc_cpu_ref.hidden[i];
                }
                cpu_ref_dots[n] = sum_f;
                cpu_ref_dots[n + CFC_HIDDEN_DIM] = sum_g;
            }
            cfc_apply_blend(cpu_ref_dots, cfc_cpu_ref.hidden);

            /* HW step */
            memcpy(cfc.input, input, CFC_INPUT_DIM);
            premultiply_all();
            encode_all_neurons();
            build_neuron_chain();
            cpu_compute_all_dots();

            chain_isr_result_t hw_r = run_chain_with_isr();

            /* Check dot match BEFORE blend */
            int dot_match = 0;
            for (int n = 0; n < NUM_NEURONS; n++)
                if (hw_r.dots[n] == cpu_ref_dots[n]) dot_match++;

            cfc_blend_result_t hw_bl = cfc_apply_blend(hw_r.dots, cfc.hidden);
            total_us += hw_r.elapsed_us;

            int h_match = (trit_hamming(cfc.hidden, cfc_cpu_ref.hidden, CFC_HIDDEN_DIM) == 0);
            int energy = trit_energy(cfc.hidden, CFC_HIDDEN_DIM);

            printf("  step %d: h_match=%s dots=%d/%d isr=%d U=%d H=%d I=%d energy=%d [%lld us]\n",
                   step, h_match ? "yes" : "NO",
                   dot_match, NUM_NEURONS, hw_r.isr_fires,
                   hw_bl.n_update, hw_bl.n_hold, hw_bl.n_invert,
                   energy, hw_r.elapsed_us);

            if (!h_match) all_match = 0;
        }

        print_trit_vec("final h(cpu)", cfc_cpu_ref.hidden, CFC_HIDDEN_DIM);
        print_trit_vec("final h(gie)", cfc.hidden, CFC_HIDDEN_DIM);

        double avg_step_us = (double)total_us / 8.0;
        printf("  8 steps in %lld us (%.0f us/step, %.1f Hz)\n",
               total_us, avg_step_us, 1e6 / avg_step_us);

        test_count++;
        if (all_match) pass_count++;
        printf("  %s\n\n", all_match ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 6: CfC dynamics — all three blend modes, state evolution
     *
     *  Verify that the ternary CfC on GIE produces all three blend
     *  modes (UPDATE, HOLD, INVERT) and that state evolves over time.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 6: CfC dynamics (blend modes + evolution) --\n");
    fflush(stdout);
    if (gdma_isr_handle == NULL) {
        printf("  SKIPPED — ISR allocation failed\n\n");
        test_count++;
        fflush(stdout);
    } else {
        cfc_init(5555, 50);

        int total_u = 0, total_h = 0, total_i = 0;
        int state_changed = 0;
        int8_t prev_hidden[CFC_HIDDEN_DIM];
        memset(prev_hidden, 0, CFC_HIDDEN_DIM);

        for (int step = 0; step < 8; step++) {
            int8_t input[CFC_INPUT_DIM];
            gen_input(input, 3000 + step * 53);

            memcpy(cfc.input, input, CFC_INPUT_DIM);
            premultiply_all();
            encode_all_neurons();
            build_neuron_chain();
            cpu_compute_all_dots();

            chain_isr_result_t hw_r = run_chain_with_isr();
            cfc_blend_result_t bl = cfc_apply_blend(hw_r.dots, cfc.hidden);

            int dist = trit_hamming(cfc.hidden, prev_hidden, CFC_HIDDEN_DIM);
            int energy = trit_energy(cfc.hidden, CFC_HIDDEN_DIM);

            printf("  step %d: U=%2d H=%2d I=%2d | energy=%2d delta=%d\n",
                   step, bl.n_update, bl.n_hold, bl.n_invert, energy, dist);

            total_u += bl.n_update;
            total_h += bl.n_hold;
            total_i += bl.n_invert;
            if (dist > 0) state_changed = 1;
            memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
        }

        print_trit_vec("final h", cfc.hidden, CFC_HIDDEN_DIM);
        printf("  totals: %d update, %d hold, %d invert\n", total_u, total_h, total_i);

        int ok = (total_u > 0) && (total_h > 0) && (total_i > 0) && state_changed;
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
        printf("\n  *** AUTONOMOUS TERNARY CfC VERIFIED ***\n");
        printf("  64 dot products via single DMA chain (GDMA→PARLIO→PCNT)\n");
        printf("  Ternary blend: UPDATE (f=+1), HOLD (f=0), INVERT (f=-1)\n");
        printf("  h_new = (f==0) ? h_old : f * g\n");
        printf("  Hardware computes, CPU just blends. Sub-CPU liquid network.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED — see details above.\n\n");
    }
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
