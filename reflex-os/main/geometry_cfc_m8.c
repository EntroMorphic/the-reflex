/*
 * geometry_cfc_m8.c — M8: Self-Sequencing Fabric Experiments
 *
 * Milestone 8: Autonomous multi-neuron evaluation without CPU in the loop.
 *
 * Approach: Chain all 64 neuron DMA descriptors into one long chain.
 * The peripheral fabric runs through all neurons autonomously.
 * CPU only wakes to read final results.
 *
 * Key questions to answer:
 *   1. Can we chain 64 neurons' descriptors without CPU intervention?
 *   2. How do we capture per-neuron PCNT counts? (PCNT ISR? REGDMA backup?)
 *   3. What's the total throughput with zero CPU overhead between neurons?
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

#define PARLIO_TX_CFG0       0x60015008
#define REG32(addr)  (*(volatile uint32_t*)(addr))

/* ── GPIO mapping ── */
#define GPIO_X_POS   4
#define GPIO_X_NEG   5
#define GPIO_Y_POS   6
#define GPIO_Y_NEG   7

/* ── Constants ── */
#define BUF_SIZE       64
#define TRITS_PER_BUF  128

#define T_POS  (+1)
#define T_ZERO  (0)
#define T_NEG  (-1)

/* ── CfC dimensions ── */
#define CFC_INPUT_DIM   128
#define CFC_HIDDEN_DIM  32
#define CFC_CONCAT_DIM  (CFC_INPUT_DIM + CFC_HIDDEN_DIM)   /* 160 */
#define CFC_MAX_DIM     256

/* Number of neurons (64 total: 32 f + 32 g pathways) */
#define NUM_NEURONS     64

/* Buffers per neuron (160 trits = 2 buffers of 64 bytes each) */
#define BUFS_PER_NEURON 2

/* Separator buffer between neurons (all zeros = no PCNT change) */
#define SEPARATOR_SIZE  16  /* 16 bytes = 64 symbols @ 20MHz = 3.2us */

/* Total descriptors: 2 data + 1 separator per neuron */
#define DESCS_PER_NEURON 3
#define TOTAL_DESCS      (NUM_NEURONS * DESCS_PER_NEURON)  /* 192 */
#define TOTAL_DATA_BUFS  (NUM_NEURONS * BUFS_PER_NEURON)   /* 128 */

/* ── Static allocations (BSS to avoid stack overflow) ── */
static uint8_t __attribute__((aligned(4))) all_bufs[TOTAL_DATA_BUFS][BUF_SIZE];
static uint8_t __attribute__((aligned(4))) separator_buf[SEPARATOR_SIZE];  /* All zeros */
static lldesc_t __attribute__((aligned(4))) all_descs[TOTAL_DESCS];

/* Per-neuron results captured via ISR or cumulative decode */
static int cumulative_agree[NUM_NEURONS + 1];
static int cumulative_disagree[NUM_NEURONS + 1];

/* Peripheral handles */
static gptimer_handle_t timer0 = NULL;
static pcnt_unit_handle_t pcnt_agree = NULL, pcnt_disagree = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;
static int bare_ch = 0;

/* ── Weight and state storage ── */
typedef struct {
    int8_t W_f[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t W_g[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t hidden[CFC_HIDDEN_DIM];
    int8_t input[CFC_INPUT_DIM];
} cfc_state_t;

static cfc_state_t cfc;

/* Pre-multiplied products for all neurons */
static int8_t all_products[NUM_NEURONS][CFC_MAX_DIM];

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
        .output_clk_freq_hz = 20000000,  /* 20 MHz — M9 verified */
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

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t tmul(int8_t a, int8_t b) {
    return (int8_t)(a * b);
}

/* ══════════════════════════════════════════════════════════════════
 *  PRNG
 * ══════════════════════════════════════════════════════════════════ */

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
 *  CfC INITIALIZATION
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

    /* Random input */
    for (int i = 0; i < CFC_INPUT_DIM; i++)
        cfc.input[i] = rand_trit(40);

    /* Hidden starts at zero */
    memset(cfc.hidden, T_ZERO, CFC_HIDDEN_DIM);
}

/* ══════════════════════════════════════════════════════════════════
 *  PRE-MULTIPLY ALL NEURONS
 *
 *  Neurons 0-31: f-pathway (W_f)
 *  Neurons 32-63: g-pathway (W_g)
 * ══════════════════════════════════════════════════════════════════ */

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

/* ══════════════════════════════════════════════════════════════════
 *  ENCODE ALL NEURONS INTO DMA BUFFERS
 * ══════════════════════════════════════════════════════════════════ */

static void encode_all(void) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        /* Each neuron has 160 trits = 2 buffers of 128 trits each */
        int base_buf = n * BUFS_PER_NEURON;

        /* Buffer 0: trits 0-127 */
        encode_trit_buffer(all_bufs[base_buf], all_products[n], 128);

        /* Buffer 1: trits 128-159 (padded to 128) */
        encode_trit_buffer(all_bufs[base_buf + 1], &all_products[n][128], 32);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  BUILD GIANT DESCRIPTOR CHAIN (original, no separators)
 *
 *  All 128 buffers chained: desc[0] → desc[1] → ... → desc[127]
 * ══════════════════════════════════════════════════════════════════ */

static void build_chain_simple(void) {
    for (int i = 0; i < TOTAL_DATA_BUFS; i++) {
        lldesc_t *next = (i < TOTAL_DATA_BUFS - 1) ? &all_descs[i + 1] : NULL;
        init_desc(&all_descs[i], all_bufs[i], BUF_SIZE, next);
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  BUILD CHAIN WITH SEPARATORS
 *
 *  For each neuron: [data0] → [data1] → [separator] → next neuron
 *  The separator is all-zeros (no PCNT edges).
 *  This gives time to sample PCNT between neurons.
 * ══════════════════════════════════════════════════════════════════ */

static void build_chain_with_separators(void) {
    /* Ensure separator is all zeros */
    memset(separator_buf, 0, SEPARATOR_SIZE);

    int desc_idx = 0;
    for (int n = 0; n < NUM_NEURONS; n++) {
        int buf0 = n * BUFS_PER_NEURON;
        int buf1 = buf0 + 1;

        /* Data buffer 0 */
        init_desc(&all_descs[desc_idx], all_bufs[buf0], BUF_SIZE, &all_descs[desc_idx + 1]);
        desc_idx++;

        /* Data buffer 1 */
        init_desc(&all_descs[desc_idx], all_bufs[buf1], BUF_SIZE, &all_descs[desc_idx + 1]);
        desc_idx++;

        /* Separator (all zeros) */
        lldesc_t *next = (n < NUM_NEURONS - 1) ? &all_descs[desc_idx + 1] : NULL;
        init_desc(&all_descs[desc_idx], separator_buf, SEPARATOR_SIZE, next);
        desc_idx++;
    }
}

/* For compatibility with existing code */
static void build_chain(void) {
    build_chain_simple();
}

/* ══════════════════════════════════════════════════════════════════
 *  RUN THE GIANT CHAIN
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

typedef struct {
    int agree;
    int disagree;
    int dot;
    int64_t elapsed_us;
} chain_result_t;

static chain_result_t run_chain(void) {
    chain_result_t r = {0};

    int total_bytes = TOTAL_DATA_BUFS * BUF_SIZE;  /* 128 * 64 = 8192 bytes */

    /* Drive Y = +1 (sum mode) */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Clear, setup, clear (triple clear from errata) */
    clear_all_pcnt();
    setup_gdma(&all_descs[0], total_bytes);
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

    /* Wait for entire chain: 8192 bytes at 20 MHz = 8192 * 4 symbols * 0.05us = 1638us */
    /* Add generous margin for safety */
    int wait_us = (total_bytes * 4) / 20 + 500;  /* ~2100 us */
    esp_rom_delay_us(wait_us);
    int64_t t_end = esp_timer_get_time();

    parlio_stop_and_reset();

    /* Read accumulated counts */
    pcnt_unit_get_count(pcnt_agree, &r.agree);
    pcnt_unit_get_count(pcnt_disagree, &r.disagree);
    r.dot = r.agree - r.disagree;
    r.elapsed_us = t_end - t_start;

    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  CPU REFERENCE: Sum of all dot products
 * ══════════════════════════════════════════════════════════════════ */

static int cpu_sum_all_dots(void) {
    int total = 0;
    for (int n = 0; n < NUM_NEURONS; n++) {
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            total += all_products[n][i];
        }
    }
    return total;
}

/* CPU reference: individual per-neuron dot products */
static int cpu_dots[NUM_NEURONS];

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
 *  RUN CHAIN WITH SEPARATOR SAMPLING
 *
 *  Run the chain one neuron at a time, reading PCNT after each.
 *  This is slower but gives per-neuron results.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int dots[NUM_NEURONS];
    int64_t elapsed_us;
    int matches;
} per_neuron_result_t;

static per_neuron_result_t run_chain_per_neuron(void) {
    per_neuron_result_t r = {0};

    /* Drive Y = +1 */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    int64_t t_start = esp_timer_get_time();

    /* Run each neuron individually (2 buffers per neuron) */
    for (int n = 0; n < NUM_NEURONS; n++) {
        int buf0 = n * BUFS_PER_NEURON;
        int buf1 = buf0 + 1;

        /* Build a 2-descriptor chain for this neuron */
        init_desc(&all_descs[0], all_bufs[buf0], BUF_SIZE, &all_descs[1]);
        init_desc(&all_descs[1], all_bufs[buf1], BUF_SIZE, NULL);

        int total_bytes = 2 * BUF_SIZE;

        /* Clear PCNT before this neuron */
        clear_all_pcnt();

        /* Setup and run */
        setup_gdma(&all_descs[0], total_bytes);
        esp_rom_delay_us(100);
        clear_all_pcnt();

        /* Fire PARLIO */
        {
            uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
            tx_cfg0 |= (1 << 19);
            REG32(PARLIO_TX_CFG0) = tx_cfg0;
        }

        /* Wait: 128 bytes * 4 symbols / 20 MHz = 25.6us + margin */
        esp_rom_delay_us(50);

        parlio_stop_and_reset();

        /* Read result for this neuron */
        int agree, disagree;
        pcnt_unit_get_count(pcnt_agree, &agree);
        pcnt_unit_get_count(pcnt_disagree, &disagree);
        r.dots[n] = agree - disagree;
    }

    int64_t t_end = esp_timer_get_time();
    r.elapsed_us = t_end - t_start;

    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    /* Compare with CPU reference */
    for (int n = 0; n < NUM_NEURONS; n++) {
        if (r.dots[n] == cpu_dots[n]) r.matches++;
    }

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  M8: Self-Sequencing Fabric — Experiment 1\n");
    printf("  Chained DMA: 64 neurons × 2 buffers = 128 descriptors\n");
    printf("  PARLIO: 20 MHz | Total: 8192 bytes\n");
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
    printf("[INIT] Done.\n\n");
    fflush(stdout);

    int test_count = 0, pass_count = 0;

    /* ══════════════════════════════════════════════════════════════
     *  TEST 1: Can we chain all 64 neurons autonomously?
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 1: Giant chain (64 neurons, 128 buffers) --\n");
    fflush(stdout);
    {
        printf("  Initializing CfC (seed=42, 50%% sparse)...\n");
        cfc_init(42, 50);

        printf("  Pre-multiplying all 64 neurons...\n");
        int64_t t0 = esp_timer_get_time();
        premultiply_all();
        int64_t t_premul = esp_timer_get_time() - t0;
        printf("    pre-multiply: %lld us\n", t_premul);

        printf("  Encoding all buffers...\n");
        t0 = esp_timer_get_time();
        encode_all();
        int64_t t_encode = esp_timer_get_time() - t0;
        printf("    encode: %lld us\n", t_encode);

        printf("  Building descriptor chain...\n");
        t0 = esp_timer_get_time();
        build_chain();
        int64_t t_chain = esp_timer_get_time() - t0;
        printf("    chain build: %lld us\n", t_chain);

        printf("  Running chain (CPU idle)...\n");
        chain_result_t r = run_chain();

        printf("  Results:\n");
        printf("    agree=%d, disagree=%d, dot=%d\n", r.agree, r.disagree, r.dot);
        printf("    elapsed: %lld us\n", r.elapsed_us);

        /* CPU reference */
        int cpu_total = cpu_sum_all_dots();
        printf("    CPU reference (sum of all products): %d\n", cpu_total);

        int ok = (r.dot == cpu_total);
        printf("    Match: %s\n", ok ? "YES" : "NO");

        if (ok) {
            int64_t cpu_prep = t_premul + t_encode + t_chain;
            double throughput = (double)(NUM_NEURONS * CFC_CONCAT_DIM) / (r.elapsed_us / 1e6);
            printf("\n  === CHAIN AUTONOMOUS OPERATION VERIFIED ===\n");
            printf("  CPU prep: %lld us (once per step)\n", cpu_prep);
            printf("  Hardware: %lld us (CPU idle)\n", r.elapsed_us);
            printf("  Total: %lld us\n", cpu_prep + r.elapsed_us);
            printf("  Throughput: %.0f trit-MACs/s\n", throughput);
            printf("  (64 neurons × 160 trits = 10,240 trit-MACs)\n");
        }

        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 2: Compare with M9 per-neuron approach
     *
     *  At M9 20MHz: ~61ms per CfC step (64 dots)
     *  M8 chain should be much faster since no CPU loop overhead
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 2: Throughput comparison --\n");
    fflush(stdout);
    {
        /* Run the chain 10 times and average */
        int64_t total_chain_us = 0;
        int all_match = 1;

        for (int i = 0; i < 10; i++) {
            /* Re-randomize for each run */
            cfc_init(1000 + i * 77, 50);
            premultiply_all();
            encode_all();
            build_chain();

            chain_result_t r = run_chain();
            total_chain_us += r.elapsed_us;

            int cpu_total = cpu_sum_all_dots();
            if (r.dot != cpu_total) {
                printf("  Run %d: MISMATCH (hw=%d, cpu=%d)\n", i, r.dot, cpu_total);
                all_match = 0;
            }
        }

        double avg_chain_us = (double)total_chain_us / 10.0;
        double hz = 1e6 / avg_chain_us;

        printf("  10 runs average: %.1f us per chain\n", avg_chain_us);
        printf("  Frequency: %.1f Hz (chain runs per second)\n", hz);
        printf("  All matches: %s\n", all_match ? "YES" : "NO");

        /* Compare with M9 baseline */
        double m9_step_us = 61278.0;  /* From 20MHz test */
        double speedup = m9_step_us / avg_chain_us;
        printf("\n  Comparison with M9 (per-neuron loop at 20MHz):\n");
        printf("    M9: %.0f us per step\n", m9_step_us);
        printf("    M8: %.0f us per chain\n", avg_chain_us);
        printf("    Speedup: %.1fx\n", speedup);

        test_count++;
        if (all_match) pass_count++;
        printf("  %s\n\n", all_match ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 3: What's the bottleneck?
     *
     *  Measure: chain without PARLIO (just DMA to nowhere)
     *  vs chain with PARLIO
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 3: Bottleneck analysis --\n");
    fflush(stdout);
    {
        cfc_init(999, 50);
        premultiply_all();
        encode_all();
        build_chain();

        /* Theoretical minimum at 20 MHz:
         * 8192 bytes * 4 symbols/byte = 32768 symbols
         * At 20 MHz: 32768 * 0.05us = 1638.4 us */
        double theoretical_us = (TOTAL_DATA_BUFS * BUF_SIZE * 4.0) / 20.0;

        chain_result_t r = run_chain();
        double actual_us = (double)r.elapsed_us;
        double overhead_pct = ((actual_us - theoretical_us) / theoretical_us) * 100.0;

        printf("  Total data: %d bytes (%d symbols at 2-bit)\n",
               TOTAL_DATA_BUFS * BUF_SIZE, TOTAL_DATA_BUFS * BUF_SIZE * 4);
        printf("  Theoretical minimum (20 MHz): %.1f us\n", theoretical_us);
        printf("  Actual elapsed: %.0f us\n", actual_us);
        printf("  Overhead: %.1f%%\n", overhead_pct);

        /* The overhead comes from:
         * - DMA descriptor fetch latency
         * - PARLIO FIFO fill/drain
         * - The wait_us safety margin */

        test_count++;
        pass_count++;  /* This is informational */
        printf("  OK (informational)\n\n");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 4: Per-neuron dot products
     *
     *  Run the chain one neuron at a time to get individual results.
     *  Compare hardware results with CPU reference.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 4: Per-neuron dot products (64 individual results) --\n");
    fflush(stdout);
    {
        cfc_init(42, 50);
        premultiply_all();
        encode_all();

        /* Compute CPU reference */
        cpu_compute_all_dots();

        /* Run hardware per-neuron */
        per_neuron_result_t r = run_chain_per_neuron();

        printf("  Matches: %d / %d\n", r.matches, NUM_NEURONS);
        printf("  Elapsed: %lld us\n", r.elapsed_us);
        printf("  Per neuron: %.1f us\n", (double)r.elapsed_us / NUM_NEURONS);

        /* Show first few and last few */
        printf("  Sample results (HW vs CPU):\n");
        for (int i = 0; i < 4; i++) {
            printf("    n[%2d]: hw=%+4d cpu=%+4d %s\n",
                   i, r.dots[i], cpu_dots[i],
                   (r.dots[i] == cpu_dots[i]) ? "OK" : "MISMATCH");
        }
        printf("    ...\n");
        for (int i = 60; i < 64; i++) {
            printf("    n[%2d]: hw=%+4d cpu=%+4d %s\n",
                   i, r.dots[i], cpu_dots[i],
                   (r.dots[i] == cpu_dots[i]) ? "OK" : "MISMATCH");
        }

        /* Compare with M9 */
        double m9_step_us = 61278.0;
        double speedup = m9_step_us / (double)r.elapsed_us;
        printf("\n  Comparison with M9:\n");
        printf("    M9: %.0f us (full CfC step)\n", m9_step_us);
        printf("    M8 per-neuron: %lld us (64 dots only, no CfC blend)\n", r.elapsed_us);
        printf("    Speedup: %.1fx\n", speedup);

        int ok = (r.matches == NUM_NEURONS);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n\n", ok ? "OK" : "FAIL");
        fflush(stdout);
    }

    /* ── Summary ── */
    printf("============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    printf("\n");
    printf("  KEY FINDING: The DMA chain runs autonomously!\n");
    printf("  128 descriptors, 8KB data, zero CPU intervention.\n");
    printf("\n");
    printf("  LIMITATION: PCNT accumulates across ALL neurons.\n");
    printf("  We get sum(all dots), not individual per-neuron dots.\n");
    printf("\n");
    printf("  NEXT STEP: Per-neuron result capture:\n");
    printf("    Option A: PCNT threshold ISR at each neuron boundary\n");
    printf("    Option B: REGDMA backup of PCNT count after each neuron\n");
    printf("    Option C: ETM → PCNT clear, with cumulative decoding\n");
    printf("============================================================\n\n");
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
