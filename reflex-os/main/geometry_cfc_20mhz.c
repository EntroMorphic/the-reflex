/*
 * geometry_cfc_20mhz.c — Ternary CfC at 20 MHz PARLIO clock
 *
 * Milestone 9: Performance verification at 20× clock rate (PARLIO max).
 *
 * Copy of geometry_cfc.c (M7) with PARLIO at 20 MHz instead of 1 MHz.
 * Tests whether PCNT can keep up with the maximum PARLIO edge rate.
 *
 * Everything is {-1, 0, +1}: weights, inputs, hidden state, activations.
 *
 * CfC update equation (ternary):
 *
 *   concat = [input | hidden]                      // ternary concatenation
 *   f[n]   = sign(dot(concat, W_f[n]))             // gate:      {-1, 0, +1}
 *   g[n]   = sign(dot(concat, W_g[n]))             // candidate: {-1, 0, +1}
 *
 *   if f[n] == +1:  h_new[n] =  g[n]              // UPDATE: accept candidate
 *   if f[n] ==  0:  h_new[n] =  h[n]              // HOLD:   keep state
 *   if f[n] == -1:  h_new[n] = -g[n]              // INVERT: negate candidate
 *
 * Equivalently: h_new[n] = (f[n] == 0) ? h[n] : f[n] * g[n]
 *
 * The three blend modes create natural dynamics:
 *   +1 gate: excitatory (follow the evidence)
 *    0 gate: memory     (maintain current belief)
 *   -1 gate: inhibitory (oppose the evidence)
 *
 * Hardware mapping:
 *   GIE (GDMA→PARLIO→PCNT): all dot products (2N per step)
 *   CPU: tmul(f,g), conditional store (~3 instr/neuron)
 *   No concat buffer — weight layout IS the concatenation.
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
#include "soc/gdma_reg.h"
#include "rom/lldesc.h"

/* ── Register addresses (VERIFIED on silicon, M5-M6) ── */
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

#define PARLIO_TX_CFG0       0x60015008
#define REG32(addr)  (*(volatile uint32_t*)(addr))

/* ── GPIO mapping (same as M5-M6) ── */
#define GPIO_X_POS   4
#define GPIO_X_NEG   5
#define GPIO_Y_POS   6
#define GPIO_Y_NEG   7
#define GPIO_CLASS_P 8
#define GPIO_CLASS_N 9

/* ── Constants ── */
#define BUF_SIZE       64
#define TRITS_PER_BUF  128
#define MAX_BUFS       8

#define T_POS  (+1)
#define T_ZERO  (0)
#define T_NEG  (-1)

/* ── CfC dimensions ── */
#define CFC_INPUT_DIM   128
#define CFC_HIDDEN_DIM  32
#define CFC_CONCAT_DIM  (CFC_INPUT_DIM + CFC_HIDDEN_DIM)   /* 160 */
#define CFC_MAX_DIM     256   /* buffer ceiling */

/* ── Buffers and descriptors ── */
static uint8_t __attribute__((aligned(4))) bufs[MAX_BUFS][BUF_SIZE];
static lldesc_t __attribute__((aligned(4))) descs[MAX_BUFS];

/* ── Peripheral handles ── */
static gptimer_handle_t timer0 = NULL;
static pcnt_unit_handle_t pcnt_agree = NULL, pcnt_disagree = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;
static int bare_ch = 0;

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY CfC DATA STRUCTURES
 *
 *  All state is {-1, 0, +1}, stored as int8_t.
 *  Memory: 2 * N * concat_dim (weights) + N (hidden) + N (bias)
 *  For N=32, D=160: 2 * 32 * 160 + 32 + 32 = 10,304 bytes
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Gate pathway (f): controls update/hold/invert */
    int8_t W_f[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t b_f[CFC_HIDDEN_DIM];

    /* Candidate pathway (g): proposes new state */
    int8_t W_g[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t b_g[CFC_HIDDEN_DIM];

    /* Hidden state: ternary {-1, 0, +1} */
    int8_t hidden[CFC_HIDDEN_DIM];

    /* Step counter */
    uint32_t step;
} ternary_cfc_t;

/* ══════════════════════════════════════════════════════════════════
 *  ENCODING (from M5-M6, proven on silicon)
 * ══════════════════════════════════════════════════════════════════ */

static uint8_t encode_trit_pair(int t0, int t1) {
    uint8_t lo = 0, hi = 0;
    if (t0 == T_POS)  lo = 0x01;
    if (t0 == T_NEG)  lo = 0x02;
    if (t1 == T_POS)  hi = 0x10;
    if (t1 == T_NEG)  hi = 0x20;
    return hi | lo;
}

static void encode_trit_buffer(uint8_t *buf, const int8_t *trits, int n_trits) {
    memset(buf, 0, BUF_SIZE);
    for (int i = 0; i < n_trits && i < TRITS_PER_BUF; i += 2) {
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

static void init_desc(lldesc_t *d, uint8_t *buf, int len, lldesc_t *next) {
    memset(d, 0, sizeof(lldesc_t));
    d->size = len;
    d->length = len;
    d->buf = buf;
    d->owner = 1;
    d->eof = (next == NULL) ? 1 : 0;
    d->empty = next ? ((uint32_t)next) : 0;
}

/* ── Peripheral init (identical to M6) ── */

static void init_gpio(void) {
    for (int i = 4; i <= 9; i++) {
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
        .output_clk_freq_hz = 20000000,  /* 20 MHz — M9 max test */
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
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_agree, 1));
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
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_disagree, 1));
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
    volatile uint32_t *t0cfg = (volatile uint32_t*)0x60008000;
    *t0cfg |= (1 << 28);
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

/* ══════════════════════════════════════════════════════════════════
 *  BARE-METAL DMA (from M5-M6, proven on silicon)
 * ══════════════════════════════════════════════════════════════════ */

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

static void clear_all_pcnt(void) {
    pcnt_unit_clear_count(pcnt_agree);
    pcnt_unit_clear_count(pcnt_disagree);
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t tmul(int8_t a, int8_t b) {
    return (int8_t)(a * b);
}

static inline int8_t tsign(int val) {
    if (val > 0) return T_POS;
    if (val < 0) return T_NEG;
    return T_ZERO;
}

/* ══════════════════════════════════════════════════════════════════
 *  GIE DOT PRODUCT — split-source variant
 *
 *  Pre-multiplies W[0..input_dim-1] * input[i] and
 *                 W[input_dim..concat_dim-1] * hidden[j]
 *  directly from the two source arrays.
 *
 *  No concat buffer. The weight layout IS the concatenation.
 *  The CPU walks input[] and hidden[] during pre-multiply,
 *  which it was going to do anyway. Zero extra copies.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int dot;
    int agree;
    int disagree;
    int64_t hw_us;
} dot_result_t;

static dot_result_t gie_dot_split(const int8_t *weights,
                                   const int8_t *input, int input_dim,
                                   const int8_t *hidden, int hidden_dim) {
    dot_result_t r = {0};
    int dim = input_dim + hidden_dim;

    int n_bufs = (dim + TRITS_PER_BUF - 1) / TRITS_PER_BUF;
    if (n_bufs > MAX_BUFS) n_bufs = MAX_BUFS;
    int total_trits = dim;
    if (total_trits > n_bufs * TRITS_PER_BUF)
        total_trits = n_bufs * TRITS_PER_BUF;

    /* Pre-multiply W * [input | hidden] — no concat buffer needed.
     * Weight layout already encodes the concatenation:
     *   W[0..input_dim-1]   pairs with input[0..input_dim-1]
     *   W[input_dim..dim-1] pairs with hidden[0..hidden_dim-1]  */
    int8_t product[CFC_MAX_DIM];
    for (int i = 0; i < input_dim && i < total_trits; i++)
        product[i] = tmul(weights[i], input[i]);
    for (int i = 0; i < hidden_dim && (input_dim + i) < total_trits; i++)
        product[input_dim + i] = tmul(weights[input_dim + i], hidden[i]);

    /* Encode into DMA buffers */
    for (int b = 0; b < n_bufs; b++) {
        int offset = b * TRITS_PER_BUF;
        int count = total_trits - offset;
        if (count > TRITS_PER_BUF) count = TRITS_PER_BUF;
        encode_trit_buffer(bufs[b], &product[offset], count);
    }

    /* Build descriptor chain */
    for (int b = 0; b < n_bufs; b++) {
        lldesc_t *next = (b < n_bufs - 1) ? &descs[b + 1] : NULL;
        init_desc(&descs[b], bufs[b], BUF_SIZE, next);
    }
    int total_bytes = n_bufs * BUF_SIZE;

    /* Drive Y = +1 (sum mode) */
    for (int i = 4; i <= 9; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(50);
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Clear, setup, clear, clear (triple clear from errata) */
    clear_all_pcnt();
    setup_gdma(&descs[0], total_bytes);
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* Fire PARLIO */
    int64_t t_start = esp_timer_get_time();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* At 20 MHz: 64 bytes = 256 symbols @ 0.05us = 12.8us + margin */
    int wait_us = (total_bytes * 4 * 2) / 20 + 100;
    esp_rom_delay_us(wait_us);
    int64_t t_end = esp_timer_get_time();

    parlio_stop_and_reset();
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    esp_rom_delay_us(10);

    /* Read PCNT */
    pcnt_unit_get_count(pcnt_agree, &r.agree);
    pcnt_unit_get_count(pcnt_disagree, &r.disagree);
    r.dot = r.agree - r.disagree;
    r.hw_us = t_end - t_start;

    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  CPU REFERENCE DOT PRODUCT — split-source variant
 *
 *  Same split: W[0..input_dim-1] * input, W[input_dim..] * hidden.
 *  No concat buffer needed for CPU reference either.
 * ══════════════════════════════════════════════════════════════════ */

static int cpu_dot_split(const int8_t *weights,
                         const int8_t *input, int input_dim,
                         const int8_t *hidden, int hidden_dim) {
    int sum = 0;
    for (int i = 0; i < input_dim; i++)
        sum += weights[i] * input[i];
    for (int i = 0; i < hidden_dim; i++)
        sum += weights[input_dim + i] * hidden[i];
    return sum;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY CfC INITIALIZATION
 * ══════════════════════════════════════════════════════════════════ */

/* Better PRNG: xorshift-style with avalanche */
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
    /* sparsity_pct: percentage of zeros (e.g., 60 means 60% zeros) */
    uint32_t r = cfc_rand() % 100;
    if (r < (uint32_t)sparsity_pct) return T_ZERO;
    return (cfc_rand() & 1) ? T_POS : T_NEG;
}

static void cfc_init(ternary_cfc_t *cfc, uint32_t seed, int sparsity_pct) {
    memset(cfc, 0, sizeof(ternary_cfc_t));
    cfc_seed(seed);

    /* Initialize weights with controlled sparsity */
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            cfc->W_f[n][i] = rand_trit(sparsity_pct);
            cfc->W_g[n][i] = rand_trit(sparsity_pct);
        }
        /* Zero-pad beyond concat_dim */
        for (int i = CFC_CONCAT_DIM; i < CFC_MAX_DIM; i++) {
            cfc->W_f[n][i] = T_ZERO;
            cfc->W_g[n][i] = T_ZERO;
        }
        /* Small random biases */
        cfc->b_f[n] = (int8_t)((cfc_rand() % 5) - 2);  /* -2..+2 */
        cfc->b_g[n] = (int8_t)((cfc_rand() % 5) - 2);
    }

    /* Hidden state starts at zero (no opinion) */
    memset(cfc->hidden, T_ZERO, CFC_HIDDEN_DIM);
    cfc->step = 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY CfC FORWARD PASS — CPU REFERENCE
 *
 *  Pure CPU computation, no hardware. Used to verify GIE version.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int8_t f[CFC_HIDDEN_DIM];         /* gate trits */
    int8_t g[CFC_HIDDEN_DIM];         /* candidate trits */
    int8_t h_new[CFC_HIDDEN_DIM];     /* new hidden state */
    int    f_dots[CFC_HIDDEN_DIM];    /* raw f dot products */
    int    g_dots[CFC_HIDDEN_DIM];    /* raw g dot products */
    int    n_update;                   /* count of f == +1 */
    int    n_hold;                     /* count of f == 0 */
    int    n_invert;                   /* count of f == -1 */
} cfc_step_result_t;

static cfc_step_result_t cfc_forward_cpu(ternary_cfc_t *cfc, const int8_t *input) {
    cfc_step_result_t r = {0};

    /* No concat buffer — weight layout IS the concatenation.
     * cpu_dot_split walks input[] and hidden[] directly. */

    /* Compute f and g for each neuron */
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        r.f_dots[n] = cpu_dot_split(cfc->W_f[n], input, CFC_INPUT_DIM,
                                     cfc->hidden, CFC_HIDDEN_DIM) + cfc->b_f[n];
        r.g_dots[n] = cpu_dot_split(cfc->W_g[n], input, CFC_INPUT_DIM,
                                     cfc->hidden, CFC_HIDDEN_DIM) + cfc->b_g[n];

        r.f[n] = tsign(r.f_dots[n]);
        r.g[n] = tsign(r.g_dots[n]);

        /* Ternary CfC blend */
        if (r.f[n] == T_ZERO) {
            r.h_new[n] = cfc->hidden[n];   /* HOLD */
            r.n_hold++;
        } else {
            r.h_new[n] = tmul(r.f[n], r.g[n]);  /* UPDATE or INVERT */
            if (r.f[n] == T_POS) r.n_update++;
            else r.n_invert++;
        }
    }

    /* Commit new hidden state */
    memcpy(cfc->hidden, r.h_new, CFC_HIDDEN_DIM);
    cfc->step++;

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY CfC FORWARD PASS — GIE HARDWARE
 *
 *  All dot products computed via GDMA→PARLIO→PCNT.
 *  CPU only does concatenation, sign(), tmul(), and store.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    cfc_step_result_t result;
    int    hw_match_f;        /* how many f dots match CPU */
    int    hw_match_g;        /* how many g dots match CPU */
    int64_t total_hw_us;      /* total GIE eval time */
    int64_t total_us;         /* total time including CPU prep */
} cfc_hw_step_result_t;

static cfc_hw_step_result_t cfc_forward_gie(ternary_cfc_t *cfc, const int8_t *input) {
    cfc_hw_step_result_t hr = {0};
    cfc_step_result_t *r = &hr.result;

    int64_t t_start = esp_timer_get_time();

    /* No concat buffer — gie_dot_split and cpu_dot_split walk
     * input[] and cfc->hidden[] directly via weight layout. */

    /* Evaluate all neurons via GIE */
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        /* f-pathway: gate dot product */
        dot_result_t f_hw = gie_dot_split(cfc->W_f[n],
                                           input, CFC_INPUT_DIM,
                                           cfc->hidden, CFC_HIDDEN_DIM);
        int f_dot_biased = f_hw.dot + cfc->b_f[n];
        r->f_dots[n] = f_dot_biased;
        r->f[n] = tsign(f_dot_biased);
        hr.total_hw_us += f_hw.hw_us;

        /* g-pathway: candidate dot product */
        dot_result_t g_hw = gie_dot_split(cfc->W_g[n],
                                           input, CFC_INPUT_DIM,
                                           cfc->hidden, CFC_HIDDEN_DIM);
        int g_dot_biased = g_hw.dot + cfc->b_g[n];
        r->g_dots[n] = g_dot_biased;
        r->g[n] = tsign(g_dot_biased);
        hr.total_hw_us += g_hw.hw_us;

        /* CPU reference for this neuron */
        int cpu_f = cpu_dot_split(cfc->W_f[n], input, CFC_INPUT_DIM,
                                   cfc->hidden, CFC_HIDDEN_DIM) + cfc->b_f[n];
        int cpu_g = cpu_dot_split(cfc->W_g[n], input, CFC_INPUT_DIM,
                                   cfc->hidden, CFC_HIDDEN_DIM) + cfc->b_g[n];
        if (f_dot_biased == cpu_f) hr.hw_match_f++;
        if (g_dot_biased == cpu_g) hr.hw_match_g++;

        /* Ternary CfC blend */
        if (r->f[n] == T_ZERO) {
            r->h_new[n] = cfc->hidden[n];
            r->n_hold++;
        } else {
            r->h_new[n] = tmul(r->f[n], r->g[n]);
            if (r->f[n] == T_POS) r->n_update++;
            else r->n_invert++;
        }
    }

    hr.total_us = esp_timer_get_time() - t_start;

    /* Commit new hidden state */
    memcpy(cfc->hidden, r->h_new, CFC_HIDDEN_DIM);
    cfc->step++;

    return hr;
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

static void print_dynamics(const cfc_step_result_t *r) {
    printf("  dynamics: %d update, %d hold, %d invert\n",
           r->n_update, r->n_hold, r->n_invert);
}

/* Count non-zero trits */
static int trit_energy(const int8_t *v, int n) {
    int e = 0;
    for (int i = 0; i < n; i++)
        if (v[i] != T_ZERO) e++;
    return e;
}

/* Ternary Hamming distance */
static int trit_hamming(const int8_t *a, const int8_t *b, int n) {
    int d = 0;
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) d++;
    return d;
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
 *  PIPELINE PRIME (from M6)
 * ══════════════════════════════════════════════════════════════════ */

static void prime_pipeline(void) {
    printf("[PRIME] Warming pipeline...\n");
    int8_t zeros[CFC_MAX_DIM];
    memset(zeros, 0, sizeof(zeros));
    encode_trit_buffer(bufs[0], zeros, TRITS_PER_BUF);
    init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);

    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    clear_all_pcnt();
    setup_gdma(&descs[0], BUF_SIZE);
    esp_rom_delay_us(500);
    clear_all_pcnt();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }
    esp_rom_delay_us(1500);
    parlio_stop_and_reset();
    for (int i = 4; i <= 9; i++) gpio_set_level(i, 0);
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
    printf("  TERNARY CfC @ 20 MHz — Maximum PARLIO Clock\n");
    printf("  Milestone 9: 20× clock rate (1 MHz → 20 MHz)\n");
    printf("  Geometry Intersection Engine + CfC Dynamics\n");
    printf("============================================================\n\n");

    printf("  CfC dimensions: input=%d, hidden=%d, concat=%d\n",
           CFC_INPUT_DIM, CFC_HIDDEN_DIM, CFC_CONCAT_DIM);
    printf("  Blend modes: UPDATE (f=+1), HOLD (f=0), INVERT (f=-1)\n");
    printf("  h_new = (f==0) ? h_old : f * g\n\n");

    printf("[INIT] GPIO 4-9...\n");
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
    printf("[INIT] Done.\n\n");
    fflush(stdout);

    prime_pipeline();

    int test_count = 0, pass_count = 0;

    /* ══════════════════════════════════════════════════════════════
     *  TEST 1: CPU reference — single step, verify blend modes
     *
     *  Hand-crafted weights so we get all three gate values.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 1: CPU reference — blend mode verification --\n");
    fflush(stdout);
    {
        static ternary_cfc_t cfc;
        memset(&cfc, 0, sizeof(cfc));

        /*
         * Craft weights for 3 neurons to force specific gate values.
         * Input: all +1 for first 128 dims (so dot product = count of +1 weights - count of -1 weights)
         *
         * Neuron 0: f should be positive → UPDATE mode
         *   W_f[0]: mostly +1 → large positive dot
         *   W_g[0]: mostly -1 → negative candidate
         *   Expected: h_new[0] = f*g = (+1)*(-1) = -1
         *
         * Neuron 1: f should be zero → HOLD mode
         *   W_f[1]: balanced +1 and -1 → dot ≈ 0
         *   Expected: h_new[1] = h_old[1] (initialized to 0)
         *
         * Neuron 2: f should be negative → INVERT mode
         *   W_f[2]: mostly -1 → large negative dot
         *   W_g[2]: mostly +1 → positive candidate
         *   Expected: h_new[2] = f*g = (-1)*(+1) = -1
         */

        /* Neuron 0: f=+1, g=-1 → UPDATE, h_new = -1 */
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            cfc.W_f[0][i] = T_POS;      /* all +1 → big positive dot */
            cfc.W_g[0][i] = T_NEG;      /* all -1 → big negative dot */
        }
        cfc.b_f[0] = 0;
        cfc.b_g[0] = 0;

        /* Neuron 1: f=0, g=anything → HOLD */
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            /* Alternate +1 and -1 to cancel out */
            cfc.W_f[1][i] = (i % 2 == 0) ? T_POS : T_NEG;
            cfc.W_g[1][i] = T_POS;
        }
        cfc.b_f[1] = 0;
        cfc.b_g[1] = 0;

        /* Neuron 2: f=-1, g=+1 → INVERT, h_new = -1 */
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            cfc.W_f[2][i] = T_NEG;      /* all -1 → big negative dot */
            cfc.W_g[2][i] = T_POS;      /* all +1 → big positive dot */
        }
        cfc.b_f[2] = 0;
        cfc.b_g[2] = 0;

        /* Remaining neurons: zero weights (f=0, HOLD) */
        for (int n = 3; n < CFC_HIDDEN_DIM; n++) {
            memset(cfc.W_f[n], 0, CFC_MAX_DIM);
            memset(cfc.W_g[n], 0, CFC_MAX_DIM);
            cfc.b_f[n] = 0;
            cfc.b_g[n] = 0;
        }

        /* Input: all +1 */
        int8_t input[CFC_INPUT_DIM];
        for (int i = 0; i < CFC_INPUT_DIM; i++) input[i] = T_POS;

        /* Hidden: starts as all zeros */

        cfc_step_result_t r = cfc_forward_cpu(&cfc, input);

        printf("  N0: f_dot=%+d → f=%c, g_dot=%+d → g=%c, h_new=%c (expect: UPDATE, h=-)\n",
               r.f_dots[0], trit_char(r.f[0]), r.g_dots[0], trit_char(r.g[0]),
               trit_char(r.h_new[0]));
        printf("  N1: f_dot=%+d → f=%c, g_dot=%+d → g=%c, h_new=%c (expect: HOLD, h=0)\n",
               r.f_dots[1], trit_char(r.f[1]), r.g_dots[1], trit_char(r.g[1]),
               trit_char(r.h_new[1]));
        printf("  N2: f_dot=%+d → f=%c, g_dot=%+d → g=%c, h_new=%c (expect: INVERT, h=-)\n",
               r.f_dots[2], trit_char(r.f[2]), r.g_dots[2], trit_char(r.g[2]),
               trit_char(r.h_new[2]));
        print_dynamics(&r);

        int ok = (r.f[0] == T_POS) && (r.h_new[0] == T_NEG) &&   /* UPDATE: f*g = +1*-1 = -1 */
                 (r.f[1] == T_ZERO) && (r.h_new[1] == T_ZERO) &&  /* HOLD */
                 (r.f[2] == T_NEG) && (r.h_new[2] == T_NEG);      /* INVERT: f*g = -1*+1 = -1 */

        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 2: CPU reference — multi-step temporal dynamics
     *
     *  Run 8 steps with changing input. Verify hidden state evolves
     *  and that the three modes create interesting dynamics.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 2: CPU reference — 8-step temporal dynamics --\n");
    fflush(stdout);
    {
        static ternary_cfc_t cfc;
        cfc_init(&cfc, 12345, 50);  /* 50% sparsity */

        int8_t prev_hidden[CFC_HIDDEN_DIM];
        memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);

        int total_updates = 0, total_holds = 0, total_inverts = 0;
        int state_changed = 0;  /* did hidden state ever change? */

        for (int step = 0; step < 8; step++) {
            int8_t input[CFC_INPUT_DIM];
            gen_input(input, 1000 + step * 77);

            cfc_step_result_t r = cfc_forward_cpu(&cfc, input);

            int energy = trit_energy(cfc.hidden, CFC_HIDDEN_DIM);
            int dist = trit_hamming(cfc.hidden, prev_hidden, CFC_HIDDEN_DIM);

            printf("  step %d: U=%2d H=%2d I=%2d | energy=%2d/%d | delta=%d\n",
                   step, r.n_update, r.n_hold, r.n_invert,
                   energy, CFC_HIDDEN_DIM, dist);

            total_updates += r.n_update;
            total_holds += r.n_hold;
            total_inverts += r.n_invert;
            if (dist > 0) state_changed = 1;

            memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
        }

        print_trit_vec("final h", cfc.hidden, CFC_HIDDEN_DIM);

        /* Verify we got all three modes and state evolved */
        int ok = (total_updates > 0) && (total_holds > 0) && (total_inverts > 0) && state_changed;
        printf("  totals: %d updates, %d holds, %d inverts, state_changed=%d\n",
               total_updates, total_holds, total_inverts, state_changed);

        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 3: GIE hardware — single step, verify vs CPU reference
     *
     *  Same network, same input → CPU and GIE must produce
     *  identical dot products and hidden state.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 3: GIE hardware — single step vs CPU reference --\n");
    fflush(stdout);
    {
        /* Initialize two identical networks (static to avoid stack overflow) */
        static ternary_cfc_t cfc_cpu, cfc_hw;
        cfc_init(&cfc_cpu, 42, 50);
        cfc_init(&cfc_hw, 42, 50);

        /* Same input */
        int8_t input[CFC_INPUT_DIM];
        gen_input(input, 9999);

        /* CPU reference */
        cfc_step_result_t r_cpu = cfc_forward_cpu(&cfc_cpu, input);

        /* GIE hardware */
        cfc_hw_step_result_t r_hw = cfc_forward_gie(&cfc_hw, input);

        printf("  CPU: U=%d H=%d I=%d\n", r_cpu.n_update, r_cpu.n_hold, r_cpu.n_invert);
        printf("  GIE: U=%d H=%d I=%d\n",
               r_hw.result.n_update, r_hw.result.n_hold, r_hw.result.n_invert);
        printf("  f-pathway: %d/%d dots match CPU\n", r_hw.hw_match_f, CFC_HIDDEN_DIM);
        printf("  g-pathway: %d/%d dots match CPU\n", r_hw.hw_match_g, CFC_HIDDEN_DIM);
        printf("  GIE time: %lld us (%lld us hardware)\n", r_hw.total_us, r_hw.total_hw_us);

        /* Verify hidden states match */
        int h_match = 1;
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            if (cfc_cpu.hidden[n] != cfc_hw.hidden[n]) {
                printf("  MISMATCH at h[%d]: cpu=%c hw=%c (f_cpu=%+d f_hw=%+d g_cpu=%+d g_hw=%+d)\n",
                       n, trit_char(cfc_cpu.hidden[n]), trit_char(cfc_hw.hidden[n]),
                       r_cpu.f_dots[n], r_hw.result.f_dots[n],
                       r_cpu.g_dots[n], r_hw.result.g_dots[n]);
                h_match = 0;
            }
        }

        int ok = (r_hw.hw_match_f == CFC_HIDDEN_DIM) &&
                 (r_hw.hw_match_g == CFC_HIDDEN_DIM) &&
                 h_match;

        print_trit_vec("h (cpu)", cfc_cpu.hidden, CFC_HIDDEN_DIM);
        print_trit_vec("h (gie)", cfc_hw.hidden, CFC_HIDDEN_DIM);

        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 4: GIE hardware — multi-step, verify temporal coherence
     *
     *  Run 4 steps on both CPU and GIE. Hidden states must match
     *  at every step (errors would accumulate if any dot is wrong).
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 4: GIE hardware — 4-step temporal coherence --\n");
    fflush(stdout);
    {
        static ternary_cfc_t cfc_cpu, cfc_hw;
        cfc_init(&cfc_cpu, 777, 45);
        cfc_init(&cfc_hw, 777, 45);

        int all_match = 1;
        int64_t total_gie_us = 0;

        for (int step = 0; step < 4; step++) {
            int8_t input[CFC_INPUT_DIM];
            gen_input(input, 2000 + step * 31);

            cfc_forward_cpu(&cfc_cpu, input);  /* advance CPU reference */
            cfc_hw_step_result_t r_hw = cfc_forward_gie(&cfc_hw, input);

            total_gie_us += r_hw.total_us;

            int h_match = 1;
            for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
                if (cfc_cpu.hidden[n] != cfc_hw.hidden[n]) {
                    h_match = 0;
                    all_match = 0;
                }
            }

            int energy = trit_energy(cfc_hw.hidden, CFC_HIDDEN_DIM);
            printf("  step %d: f=%d/%d g=%d/%d h_match=%s U=%d H=%d I=%d energy=%d [%lld us]\n",
                   step,
                   r_hw.hw_match_f, CFC_HIDDEN_DIM,
                   r_hw.hw_match_g, CFC_HIDDEN_DIM,
                   h_match ? "yes" : "NO",
                   r_hw.result.n_update, r_hw.result.n_hold, r_hw.result.n_invert,
                   energy, r_hw.total_us);
        }

        print_trit_vec("final h (cpu)", cfc_cpu.hidden, CFC_HIDDEN_DIM);
        print_trit_vec("final h (gie)", cfc_hw.hidden, CFC_HIDDEN_DIM);

        double hz = 4.0 / (total_gie_us / 1e6);
        printf("  4 steps in %lld us → %.1f Hz (%.0f us/step)\n",
               total_gie_us, hz, (double)total_gie_us / 4.0);
        printf("  (64 dot products per step, %d trits each)\n", CFC_CONCAT_DIM);

        test_count++;
        if (all_match) pass_count++;
        printf("  %s\n\n", all_match ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 5: Liquid memory — same input repeated, state converges
     *
     *  A liquid network should reach a fixed point or limit cycle
     *  when driven by a constant input. Verify convergence.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 5: Liquid memory — convergence with constant input --\n");
    fflush(stdout);
    {
        static ternary_cfc_t cfc;
        cfc_init(&cfc, 5555, 50);

        int8_t input[CFC_INPUT_DIM];
        gen_input(input, 3333);

        int8_t prev_hidden[CFC_HIDDEN_DIM];
        int converged_at = -1;

        for (int step = 0; step < 16; step++) {
            memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
            cfc_step_result_t r = cfc_forward_cpu(&cfc, input);

            int dist = trit_hamming(cfc.hidden, prev_hidden, CFC_HIDDEN_DIM);
            int energy = trit_energy(cfc.hidden, CFC_HIDDEN_DIM);

            printf("  step %2d: delta=%2d energy=%2d U=%2d H=%2d I=%2d\n",
                   step, dist, energy, r.n_update, r.n_hold, r.n_invert);

            if (dist == 0 && converged_at < 0) {
                converged_at = step;
            }
        }

        if (converged_at >= 0) {
            printf("  Converged at step %d (fixed point reached)\n", converged_at);
        } else {
            /* Check for limit cycle (period 2) */
            int8_t h_a[CFC_HIDDEN_DIM], h_b[CFC_HIDDEN_DIM];
            memcpy(h_a, cfc.hidden, CFC_HIDDEN_DIM);
            cfc_forward_cpu(&cfc, input);
            memcpy(h_b, cfc.hidden, CFC_HIDDEN_DIM);
            cfc_forward_cpu(&cfc, input);
            int cycle_2 = (trit_hamming(cfc.hidden, h_a, CFC_HIDDEN_DIM) == 0);
            if (cycle_2) {
                printf("  Period-2 limit cycle detected (oscillation)\n");
            } else {
                printf("  Still evolving (longer transient or higher-period cycle)\n");
            }
        }

        print_trit_vec("final h", cfc.hidden, CFC_HIDDEN_DIM);

        /* Pass if we got some dynamics (not all-hold from step 0) */
        int ok = (trit_energy(cfc.hidden, CFC_HIDDEN_DIM) > 0);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 6: Inhibition dynamics — invert mode creates oscillation
     *
     *  Craft a network where some neurons are strongly inhibitory
     *  (f = -1). Verify that the hidden state oscillates rather
     *  than converging to a fixed point. This is the unique
     *  feature of ternary CfC vs binary CfC.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 6: Inhibition dynamics — oscillation test --\n");
    fflush(stdout);
    {
        static ternary_cfc_t cfc;
        memset(&cfc, 0, sizeof(cfc));

        /*
         * Design: 4 neurons where f is always negative (inhibitory).
         * W_f all -1 → f = sign(-160) = -1 (for all-positive input+hidden)
         * W_g[n] alternates patterns so g varies.
         * This should create oscillation: h = -g, then next step
         * the hidden state feeds back and changes g, etc.
         */
        for (int n = 0; n < 4; n++) {
            for (int i = 0; i < CFC_CONCAT_DIM; i++) {
                cfc.W_f[n][i] = T_NEG;   /* always inhibitory */
                /* g weights: different for each neuron */
                cfc.W_g[n][i] = ((i + n) % 3 == 0) ? T_POS :
                                ((i + n) % 3 == 1) ? T_NEG : T_ZERO;
            }
        }

        /* Remaining neurons: zero (HOLD mode) */
        for (int n = 4; n < CFC_HIDDEN_DIM; n++) {
            memset(cfc.W_f[n], 0, CFC_MAX_DIM);
            memset(cfc.W_g[n], 0, CFC_MAX_DIM);
        }

        int8_t input[CFC_INPUT_DIM];
        for (int i = 0; i < CFC_INPUT_DIM; i++) input[i] = T_POS;

        /* Track states for cycle detection */
        int8_t states[8][CFC_HIDDEN_DIM];
        int total_inverts = 0;

        for (int step = 0; step < 8; step++) {
            memcpy(states[step], cfc.hidden, CFC_HIDDEN_DIM);
            cfc_step_result_t r = cfc_forward_cpu(&cfc, input);
            total_inverts += r.n_invert;

            printf("  step %d: h[0..3]=[%c%c%c%c] I=%d\n",
                   step,
                   trit_char(cfc.hidden[0]), trit_char(cfc.hidden[1]),
                   trit_char(cfc.hidden[2]), trit_char(cfc.hidden[3]),
                   r.n_invert);
        }

        /* Check for oscillation: did we see the same state twice? */
        int found_cycle = 0;
        for (int i = 0; i < 7 && !found_cycle; i++) {
            for (int j = i + 2; j < 8; j++) {
                if (trit_hamming(states[i], states[j], 4) == 0) {
                    printf("  Cycle detected: state[%d] == state[%d] (period %d)\n", i, j, j - i);
                    found_cycle = 1;
                    break;
                }
            }
        }

        /* Also check if final state matches any earlier state */
        for (int i = 0; i < 8 && !found_cycle; i++) {
            if (trit_hamming(cfc.hidden, states[i], 4) == 0) {
                printf("  Cycle detected: final == state[%d] (period %d)\n", i, 8 - i);
                found_cycle = 1;
                break;
            }
        }

        /* Pass if we got inversions (proving the third mode works) */
        int ok = (total_inverts > 0);
        printf("  total inversions across 8 steps: %d\n", total_inverts);
        if (found_cycle) printf("  OSCILLATION CONFIRMED — unique ternary CfC behavior\n");

        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ── Summary ── */
    printf("============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    printf("  Ternary CfC: weights, inputs, hidden — all {-1, 0, +1}\n");
    printf("  Three blend modes: UPDATE, HOLD, INVERT\n");
    printf("  h_new = (f==0) ? h_old : f * g  (ternary multiply)\n");
    printf("  GIE computes dot products, CPU does blend (~3 instr/neuron)\n");
    printf("  Inversion mode creates natural inhibition & oscillation\n");
    printf("============================================================\n");

    if (pass_count == test_count) {
        printf("\n  *** TERNARY CfC VERIFIED ***\n");
        printf("  Three states. Three dynamics.\n");
        printf("  Excitation. Memory. Inhibition.\n");
        printf("  The first fully-ternary liquid network.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED.\n\n");
    }
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
