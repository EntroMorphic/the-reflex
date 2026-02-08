/*
 * geometry_cfc_m8_etm.c — M8: Self-Sequencing with ETM + ISR Capture
 *
 * Milestone 8 v2: Wire ETM for autonomous chain, use minimal ISR for
 * per-neuron result capture.
 *
 * Architecture:
 *   - 1 descriptor per neuron (80 bytes = 160 trits)
 *   - GDMA EOF interrupt after each neuron
 *   - ISR captures cumulative PCNT counts (~10 cycles)
 *   - ETM wiring: GDMA EOF event available for future REGDMA trigger
 *
 * Target: ~3,000 us for 64 neurons with per-neuron results
 * (vs 13,185 us in M8 v1 per-neuron mode)
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
#define ETM_CH_ENA_SET_REG  (ETM_BASE + 0x08)
#define ETM_CH_EVT_ID_REG(n)  (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n) (ETM_BASE + 0x1C + (n) * 8)

#define PCR_BASE             0x60096000
#define PCR_SOC_ETM_CONF     (PCR_BASE + 0x98)

#define GDMA_BASE            0x60080000
#define GDMA_OUT_BASE(ch)    (GDMA_BASE + 0xD0 + (ch)*0xC0)
#define GDMA_OUT_CONF0       0x00
#define GDMA_OUT_CONF1       0x04
#define GDMA_OUT_LINK        0x10
#define GDMA_OUT_PERI_SEL    0x30

/* OUT interrupt registers are at fixed addresses, not per-channel offsets */
#define GDMA_OUT_INT_RAW_CH(ch)  (GDMA_BASE + 0x30 + (ch) * 0x10)
#define GDMA_OUT_INT_ST_CH(ch)   (GDMA_BASE + 0x34 + (ch) * 0x10)
#define GDMA_OUT_INT_ENA_CH(ch)  (GDMA_BASE + 0x38 + (ch) * 0x10)
#define GDMA_OUT_INT_CLR_CH(ch)  (GDMA_BASE + 0x3C + (ch) * 0x10)
#define GDMA_RST_BIT         (1 << 0)
#define GDMA_EOF_MODE_BIT    (1 << 3)
#define GDMA_LINK_START_BIT  (1 << 21)
#define GDMA_LINK_ADDR_MASK  0x000FFFFF
#define GDMA_PERI_PARLIO     9

/* GDMA interrupt bits */
#define GDMA_OUT_EOF_INT_BIT (1 << 1)
#define GDMA_OUT_DONE_INT_BIT (1 << 0)

#define PARLIO_TX_CFG0       0x60015008
#define REG32(addr)  (*(volatile uint32_t*)(addr))

/* PCNT direct register access (for fast ISR) */
#define PCNT_BASE            0x60012000
#define PCNT_U0_CNT_REG      (PCNT_BASE + 0x30)
#define PCNT_U1_CNT_REG      (PCNT_BASE + 0x34)
#define PCNT_U2_CNT_REG      (PCNT_BASE + 0x38)
#define PCNT_U3_CNT_REG      (PCNT_BASE + 0x3C)

/* ETM event/task IDs (from soc_etm_reg.h) */
#define GDMA_EVT_OUT_EOF_CH0    153
#define GDMA_EVT_OUT_DONE_CH0   156
#define PCNT_TASK_CNT_RST       87
#define TIMER0_TASK_CNT_CAP     69

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

/* Number of neurons (64 total: 32 f + 32 g pathways) */
#define NUM_NEURONS     64

/* Buffer size: 160 trits = 80 bytes (2 trits per byte) */
#define NEURON_BUF_SIZE 80

/* ── Static allocations ── */
static uint8_t __attribute__((aligned(4))) neuron_bufs[NUM_NEURONS][NEURON_BUF_SIZE];
static lldesc_t __attribute__((aligned(4))) neuron_descs[NUM_NEURONS];

/* Cumulative capture arrays - filled by ISR */
static volatile int32_t cum_agree[NUM_NEURONS + 1];
static volatile int32_t cum_disagree[NUM_NEURONS + 1];
static volatile int capture_idx;
static volatile int chain_complete;

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
 *  Captures cumulative PCNT counts after each neuron.
 *  ~10-15 cycles per invocation.
 * ══════════════════════════════════════════════════════════════════ */

static void IRAM_ATTR gdma_eof_isr(void *arg) {
    uint32_t status = REG32(GDMA_OUT_INT_RAW_CH(bare_ch));

    if (status & GDMA_OUT_EOF_INT_BIT) {
        /* Clear interrupt */
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_EOF_INT_BIT;

        /* Capture cumulative PCNT values */
        if (capture_idx < NUM_NEURONS) {
            cum_agree[capture_idx] = (int16_t)(REG32(PCNT_U0_CNT_REG) & 0xFFFF);
            cum_disagree[capture_idx] = (int16_t)(REG32(PCNT_U1_CNT_REG) & 0xFFFF);
            capture_idx++;
        }
    }

    if (status & GDMA_OUT_DONE_INT_BIT) {
        REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = GDMA_OUT_DONE_INT_BIT;
        chain_complete = 1;
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

static void init_desc(lldesc_t *d, uint8_t *buf, int len, lldesc_t *next) {
    memset(d, 0, sizeof(lldesc_t));
    d->size = len;
    d->length = len;
    d->buf = buf;
    d->owner = 1;
    d->eof = 1;  /* EOF after each neuron! */
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
    printf("[GDMA ISR] Skipping ISR for debug\n");
    /* Temporarily disabled to isolate crash
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0xFF;
    REG32(GDMA_OUT_INT_ENA_CH(bare_ch)) = GDMA_OUT_EOF_INT_BIT | GDMA_OUT_DONE_INT_BIT;
    esp_err_t err = esp_intr_alloc(ETS_DMA_OUT_CH0_INTR_SOURCE + bare_ch,
                                   ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1,
                                   gdma_eof_isr, NULL, &gdma_isr_handle);
    if (err != ESP_OK) {
        printf("[GDMA ISR] Failed to allocate: %d\n", err);
    } else {
        printf("[GDMA ISR] Registered on CH%d\n", bare_ch);
    }
    */
}

/* ══════════════════════════════════════════════════════════════════
 *  ETM WIRING
 *
 *  Wire GDMA EOF → (available for future REGDMA trigger)
 *  For now, we use ISR, but ETM is ready.
 * ══════════════════════════════════════════════════════════════════ */

static void setup_etm(void) {
    /* ETM Channel 0: GDMA EOF → (placeholder - could trigger REGDMA) */
    /* For now, just demonstrate the wiring is possible */

    /* Enable ETM channel 0 */
    /* REG32(ETM_CH_EVT_ID_REG(0)) = GDMA_EVT_OUT_EOF_CH0;
       REG32(ETM_CH_TASK_ID_REG(0)) = ... (REGDMA task would go here)
       REG32(ETM_CH_ENA_SET_REG) = (1 << 0); */

    printf("[ETM] 50 channels available, ISR captures for now\n");
    printf("[ETM] Future: GDMA_EOF → REGDMA backup for true autonomy\n");
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS & PRNG
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t tmul(int8_t a, int8_t b) {
    return (int8_t)(a * b);
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

static void build_neuron_chain(void) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        lldesc_t *next = (n < NUM_NEURONS - 1) ? &neuron_descs[n + 1] : NULL;
        init_desc(&neuron_descs[n], neuron_bufs[n], NEURON_BUF_SIZE, next);
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
 *  RUN CHAIN WITH ISR CAPTURE
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

    /* Clear pending interrupts */
    REG32(GDMA_OUT_INT_CLR_CH(bare_ch)) = 0xFF;

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
    int dots[NUM_NEURONS];
    int matches;
    int64_t elapsed_us;
    int isr_count;
} chain_result_t;

static chain_result_t run_chain_with_isr(void) {
    chain_result_t r = {0};

    int total_bytes = NUM_NEURONS * NEURON_BUF_SIZE;

    /* Reset capture state */
    capture_idx = 0;
    chain_complete = 0;
    memset((void*)cum_agree, 0, sizeof(cum_agree));
    memset((void*)cum_disagree, 0, sizeof(cum_disagree));

    /* Drive Y = +1 */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Clear PCNT */
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* Setup GDMA */
    setup_gdma(&neuron_descs[0], total_bytes);
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* Fire PARLIO */
    int64_t t_start = esp_timer_get_time();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Wait for chain complete (with timeout) */
    int timeout = 10000;  /* 10ms max */
    while (!chain_complete && timeout > 0) {
        esp_rom_delay_us(10);
        timeout -= 10;
    }

    int64_t t_end = esp_timer_get_time();
    r.elapsed_us = t_end - t_start;
    r.isr_count = capture_idx;

    parlio_stop_and_reset();

    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    /* Decode per-neuron dots from cumulative values */
    /* cum_agree[n] is cumulative AFTER neuron n completes */
    /* dot[0] = cum[0] - 0 */
    /* dot[n] = cum[n] - cum[n-1] */
    for (int n = 0; n < NUM_NEURONS; n++) {
        int prev_agree = (n == 0) ? 0 : cum_agree[n - 1];
        int prev_disagree = (n == 0) ? 0 : cum_disagree[n - 1];
        int agree = cum_agree[n] - prev_agree;
        int disagree = cum_disagree[n] - prev_disagree;
        r.dots[n] = agree - disagree;

        if (r.dots[n] == cpu_dots[n]) r.matches++;
    }

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    /* ULTRA MINIMAL BOOT TEST - NO INIT */
    printf("\n\n=== M8 v2 BOOT TEST ===\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("ALIVE!\n");
    fflush(stdout);

    while (1) {
        printf(".");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* TESTS TEMPORARILY DISABLED FOR DEBUG */
#if 0
// ... all the test code would be here ...
#endif
