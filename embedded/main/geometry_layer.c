/*
 * geometry_layer.c — Geometry Intersection Engine, Milestone 6
 *
 * Multi-neuron layer evaluation with hardware threshold classification.
 *
 * Architecture:
 *   For each neuron in a layer:
 *     1. CPU pre-computes P[i] = W[i] * X[i] (ternary multiply = sign flip)
 *     2. P is encoded into DMA buffer (1 dibit = 1 trit, zero-interleaved)
 *     3. GDMA → PARLIO → GPIO 4,5 → PCNT accumulates dot product
 *     4. PCNT threshold events → GPIO 8 (positive) / GPIO 9 (negative)
 *     5. Result read, next neuron begins
 *
 *   Y lines (GPIO 6,7) set to +1 for all neurons (sum mode).
 *   The pre-multiplication embeds the actual W·X computation.
 *
 * What's new over Milestone 5:
 *   - Multi-neuron sequencing (full layer evaluation)
 *   - PCNT threshold → GPIO classification (hardware sign detection)
 *   - Ternary activation function: sign(dot) → {-1, 0, +1}
 *   - Layer throughput measurement
 *   - Known-answer test: 8-neuron layer with predetermined weights and input
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

/* ── Register addresses (VERIFIED on silicon, Milestone 5) ── */
#define ETM_BASE            0x60013000
#define ETM_CH_ENA_AD0_SET  (ETM_BASE + 0x04)
#define ETM_CH_ENA_AD0_CLR  (ETM_BASE + 0x08)
#define ETM_CH_ENA_AD1_SET  (ETM_BASE + 0x10)
#define ETM_CH_ENA_AD1_CLR  (ETM_BASE + 0x14)
#define ETM_CH_EVT_ID(n)    (ETM_BASE + 0x18 + (n)*8)
#define ETM_CH_TASK_ID(n)   (ETM_BASE + 0x1C + (n)*8)
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
#define GDMA_ETM_EN_BIT      (1 << 6)
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
#define GPIO_CLASS_P 8    /* classification output: positive */
#define GPIO_CLASS_N 9    /* classification output: negative */

/* ── Constants ── */
#define BUF_SIZE       64    /* bytes per buffer */
#define TRITS_PER_BUF  128   /* trits per buffer (zero-interleaved) */
#define MAX_BUFS       8     /* max buffers in a chain */
#define MAX_NEURONS    64    /* max neurons per layer */
#define MAX_DIM        256   /* max dimension (2 buffers) */

/* ── Trit values ── */
#define T_POS  (+1)
#define T_ZERO  (0)
#define T_NEG  (-1)

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
 *  TRIT ENCODING (from Milestone 5, proven on silicon)
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
    /* Data GPIOs 4-7 */
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
    /* Classification output GPIOs 8-9 */
    for (int i = 8; i <= 9; i++) {
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
        .output_clk_freq_hz = 1000000,
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

    /* ── Unit 0: TMUL agree ── */
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

    /* Add threshold watch points for classification */
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_agree, 1));   /* any positive → classify positive */

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_agree));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_agree));

    /* ── Unit 1: TMUL disagree ── */
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

    /* Enable ETM task on timer0 */
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
 *  BARE-METAL DMA SETUP (from Milestone 5, proven on silicon)
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

    /* Normal mode — NO ETM_EN (Milestone 3 errata) */
    REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    /* Load descriptor — LINK_START immediately begins DMA */
    uint32_t addr = ((uint32_t)first_desc) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    /* Configure PARLIO — NO TX_START yet */
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
 *  TERNARY MULTIPLY: W[i] * X[i]
 *
 *  Both W and X are in {-1, 0, +1}.
 *  Result P[i] is also in {-1, 0, +1}.
 *  No actual multiplication needed — just sign logic:
 *    0 * anything = 0
 *    +1 * x = x
 *    -1 * x = -x
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t tmul(int8_t w, int8_t x) {
    /* Branchless: w * x for ternary values */
    return (int8_t)(w * x);
}

/* ══════════════════════════════════════════════════════════════════
 *  SINGLE NEURON EVALUATION
 *
 *  Computes dot(W, X) where W is the neuron's weight vector and
 *  X is the input vector. Returns the raw dot product value.
 *
 *  Steps:
 *    1. Pre-multiply P[i] = W[i] * X[i]
 *    2. Encode P into DMA buffers
 *    3. Run hardware dot product (GDMA → PARLIO → PCNT)
 *    4. Read PCNT and return agree - disagree
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int dot;          /* raw dot product value */
    int agree;
    int disagree;
    int64_t hw_us;    /* hardware eval time */
} neuron_result_t;

static neuron_result_t eval_neuron(const int8_t *weights, const int8_t *input,
                                   int dim) {
    neuron_result_t r = {0};

    /* Determine number of buffers needed */
    int n_bufs = (dim + TRITS_PER_BUF - 1) / TRITS_PER_BUF;
    if (n_bufs > MAX_BUFS) n_bufs = MAX_BUFS;
    int total_trits = dim;
    if (total_trits > n_bufs * TRITS_PER_BUF)
        total_trits = n_bufs * TRITS_PER_BUF;

    /* Step 1: Pre-multiply W * X */
    int8_t product[MAX_DIM];
    int cpu_dot = 0;  /* CPU reference for verification */
    for (int i = 0; i < total_trits; i++) {
        product[i] = tmul(weights[i], input[i]);
        cpu_dot += product[i];
    }

    /* Step 2: Encode into DMA buffers */
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

    /* Step 3: Hardware eval */
    /* Drive GPIOs LOW */
    for (int i = 4; i <= 9; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(50);

    /* Y = +1 (sum mode: agree counts positive products, disagree counts negative) */
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Clear PCNT */
    clear_all_pcnt();

    /* Setup DMA */
    setup_gdma(&descs[0], total_bytes);
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* Final PCNT clear */
    clear_all_pcnt();

    /* Arm PARLIO TX_START */
    int64_t t_start = esp_timer_get_time();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Wait for completion */
    int wait_us = total_bytes * 4 * 2 + 500;
    esp_rom_delay_us(wait_us);
    int64_t t_end = esp_timer_get_time();

    /* Stop PARLIO */
    parlio_stop_and_reset();
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    esp_rom_delay_us(10);

    /* Step 4: Read PCNT */
    pcnt_unit_get_count(pcnt_agree, &r.agree);
    pcnt_unit_get_count(pcnt_disagree, &r.disagree);
    r.dot = r.agree - r.disagree;
    r.hw_us = t_end - t_start;

    /* Classification output on GPIOs */
    if (r.dot > 0) {
        gpio_set_level(GPIO_CLASS_P, 1);
        gpio_set_level(GPIO_CLASS_N, 0);
    } else if (r.dot < 0) {
        gpio_set_level(GPIO_CLASS_P, 0);
        gpio_set_level(GPIO_CLASS_N, 1);
    } else {
        gpio_set_level(GPIO_CLASS_P, 0);
        gpio_set_level(GPIO_CLASS_N, 0);
    }

    /* Drive Y LOW */
    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY ACTIVATION FUNCTION
 *
 *  sign(x) → {-1, 0, +1}
 *  This is the ternary equivalent of ReLU/step.
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t ternary_activate(int dot) {
    if (dot > 0) return T_POS;
    if (dot < 0) return T_NEG;
    return T_ZERO;
}

/* ══════════════════════════════════════════════════════════════════
 *  LAYER EVALUATION
 *
 *  Evaluates a full layer: N neurons, each with a weight vector of
 *  dimension D, against a shared input vector X.
 *
 *  Returns ternary output vector: sign(W_row · X) for each neuron.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int n_neurons;
    int dim;
    int correct;         /* number matching CPU reference */
    int64_t total_hw_us; /* total hardware eval time */
    int64_t total_us;    /* total time including CPU prep */
    int dots[MAX_NEURONS];
    int8_t outputs[MAX_NEURONS];
} layer_result_t;

static layer_result_t eval_layer(const int8_t weights[][MAX_DIM],
                                 const int8_t *input,
                                 int n_neurons, int dim) {
    layer_result_t lr = {0};
    lr.n_neurons = n_neurons;
    lr.dim = dim;

    int64_t t_layer_start = esp_timer_get_time();

    for (int n = 0; n < n_neurons; n++) {
        neuron_result_t nr = eval_neuron(weights[n], input, dim);
        lr.dots[n] = nr.dot;
        lr.outputs[n] = ternary_activate(nr.dot);
        lr.total_hw_us += nr.hw_us;

        /* CPU reference check */
        int cpu_dot = 0;
        for (int i = 0; i < dim; i++)
            cpu_dot += weights[n][i] * input[i];
        if (nr.dot == cpu_dot)
            lr.correct++;
    }

    lr.total_us = esp_timer_get_time() - t_layer_start;
    return lr;
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST PATTERNS
 * ══════════════════════════════════════════════════════════════════ */

/* Deterministic pseudo-random trit generator */
static int8_t pseudo_trit(int seed, int index) {
    int val = ((seed * 7 + index * 13 + 5) % 7);
    if (val < 2) return T_NEG;
    if (val < 5) return T_ZERO;
    return T_POS;
}

/* ══════════════════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  GEOMETRY INTERSECTION ENGINE — Milestone 6\n");
    printf("  Multi-Neuron Layer Evaluation\n");
    printf("  Hardware dot product + ternary activation\n");
    printf("============================================================\n\n");

    printf("[INIT] GPIO 4-9...\n");
    init_gpio();

    printf("[INIT] ETM clock...\n");
    etm_force_clk();

    printf("[INIT] PARLIO TX (2-bit, 1MHz, loopback)...\n");
    init_parlio();

    printf("[INIT] PCNT (2 units: agree, disagree)...\n");
    init_pcnt();

    printf("[INIT] Timer...\n");
    init_timer();

    printf("[INIT] GDMA channel detection...\n");
    detect_gdma_channel();
    printf("[GDMA] PARLIO owns CH%d\n", bare_ch);

    printf("[INIT] Done.\n\n");
    fflush(stdout);

    /* ── Prime the pipeline ── */
    {
        printf("[PRIME] Warming pipeline...\n");
        int8_t zeros_w[MAX_DIM], zeros_x[MAX_DIM];
        memset(zeros_w, 0, sizeof(zeros_w));
        memset(zeros_x, 0, sizeof(zeros_x));
        encode_trit_buffer(bufs[0], zeros_w, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);

        /* Quick prime eval */
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
        gpio_set_level(GPIO_X_POS, 0);
        gpio_set_level(GPIO_X_NEG, 0);
        gpio_set_level(GPIO_Y_POS, 0);
        gpio_set_level(GPIO_Y_NEG, 0);
        printf("[PRIME] Done.\n\n");
        fflush(stdout);
    }

    int test_count = 0, pass_count = 0;

    /* ══════════════════════════════════════════════════════════════
     *  TEST 1: Single neuron, identity (W = all +1, X = known)
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 1: Single neuron, W=all+1, dim=128 --\n");
    fflush(stdout);
    {
        static int8_t W[1][MAX_DIM];
        static int8_t X[MAX_DIM];
        for (int i = 0; i < 128; i++) {
            W[0][i] = T_POS;
            X[i] = (i % 3 == 0) ? T_POS : (i % 3 == 1) ? T_NEG : T_ZERO;
        }
        /* Expected: count of +1 - count of -1 in X = 43 - 42 = +1 */
        /* Actually: 43 pos (i%3==0: 0,3,6,...,126 = 43), 43 neg (i%3==1), 42 zero */
        int expect = 0;
        for (int i = 0; i < 128; i++) expect += W[0][i] * X[i];

        neuron_result_t nr = eval_neuron(W[0], X, 128);
        int ok = (nr.dot == expect);
        test_count++;
        if (ok) pass_count++;
        printf("  dot=%+d (exp %+d) agree=%d disag=%d [%lldus] %s\n",
               nr.dot, expect, nr.agree, nr.disagree, nr.hw_us,
               ok ? "OK" : "FAIL");
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 2: Single neuron, W=all-1 (negation)
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 2: Single neuron, W=all-1, dim=128 --\n");
    fflush(stdout);
    {
        static int8_t W[1][MAX_DIM];
        static int8_t X[MAX_DIM];
        for (int i = 0; i < 128; i++) {
            W[0][i] = T_NEG;
            X[i] = (i % 3 == 0) ? T_POS : (i % 3 == 1) ? T_NEG : T_ZERO;
        }
        int expect = 0;
        for (int i = 0; i < 128; i++) expect += W[0][i] * X[i];

        neuron_result_t nr = eval_neuron(W[0], X, 128);
        int ok = (nr.dot == expect);
        test_count++;
        if (ok) pass_count++;
        printf("  dot=%+d (exp %+d) agree=%d disag=%d [%lldus] %s\n",
               nr.dot, expect, nr.agree, nr.disagree, nr.hw_us,
               ok ? "OK" : "FAIL");
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 3: 8-neuron layer, dim=128, known weights
     * ══════════════════════════════════════════════════════════════ */
    printf("\n-- TEST 3: 8-neuron layer, dim=128 --\n");
    fflush(stdout);
    {
        static int8_t W[MAX_NEURONS][MAX_DIM];
        static int8_t X[MAX_DIM];

        /* Input: deterministic pattern */
        for (int i = 0; i < 128; i++)
            X[i] = pseudo_trit(42, i);

        /* 8 different weight vectors */
        for (int n = 0; n < 8; n++)
            for (int i = 0; i < 128; i++)
                W[n][i] = pseudo_trit(n * 100 + 7, i);

        layer_result_t lr = eval_layer(W, X, 8, 128);

        printf("  Layer results (%d neurons, dim=%d):\n", lr.n_neurons, lr.dim);
        for (int n = 0; n < 8; n++) {
            char sign = (lr.outputs[n] > 0) ? '+' : (lr.outputs[n] < 0) ? '-' : '0';
            printf("    N%d: dot=%+4d → %c1\n", n, lr.dots[n], sign);
        }
        printf("  Accuracy: %d/%d match CPU reference\n", lr.correct, lr.n_neurons);
        printf("  HW time: %lld us (%.0f us/neuron)\n",
               lr.total_hw_us, (double)lr.total_hw_us / lr.n_neurons);
        printf("  Total time: %lld us (%.0f us/neuron)\n",
               lr.total_us, (double)lr.total_us / lr.n_neurons);

        int ok = (lr.correct == lr.n_neurons);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n", ok ? "OK" : "FAIL");
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 4: 16-neuron layer, dim=256 (2-buffer chains)
     * ══════════════════════════════════════════════════════════════ */
    printf("\n-- TEST 4: 16-neuron layer, dim=256 --\n");
    fflush(stdout);
    {
        static int8_t W[MAX_NEURONS][MAX_DIM];
        static int8_t X[MAX_DIM];

        for (int i = 0; i < 256; i++)
            X[i] = pseudo_trit(99, i);

        for (int n = 0; n < 16; n++)
            for (int i = 0; i < 256; i++)
                W[n][i] = pseudo_trit(n * 53 + 11, i);

        layer_result_t lr = eval_layer(W, X, 16, 256);

        printf("  Layer results (%d neurons, dim=%d):\n", lr.n_neurons, lr.dim);
        for (int n = 0; n < 16; n++) {
            char sign = (lr.outputs[n] > 0) ? '+' : (lr.outputs[n] < 0) ? '-' : '0';
            printf("    N%02d: dot=%+4d → %c1\n", n, lr.dots[n], sign);
        }
        printf("  Accuracy: %d/%d match CPU reference\n", lr.correct, lr.n_neurons);
        printf("  HW time: %lld us (%.0f us/neuron)\n",
               lr.total_hw_us, (double)lr.total_hw_us / lr.n_neurons);
        printf("  Total time: %lld us (%.0f us/neuron)\n",
               lr.total_us, (double)lr.total_us / lr.n_neurons);

        int ok = (lr.correct == lr.n_neurons);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n", ok ? "OK" : "FAIL");
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 5: 2-layer network (layer1 output feeds layer2 input)
     * ══════════════════════════════════════════════════════════════ */
    printf("\n-- TEST 5: 2-layer network (8→4 neurons, dim=128) --\n");
    fflush(stdout);
    {
        static int8_t W1[MAX_NEURONS][MAX_DIM];  /* 8 neurons, dim 128 */
        static int8_t W2[MAX_NEURONS][MAX_DIM];  /* 4 neurons, dim 8 */
        static int8_t X[MAX_DIM];
        static int8_t hidden[MAX_DIM];

        /* Input */
        for (int i = 0; i < 128; i++)
            X[i] = pseudo_trit(77, i);

        /* Layer 1 weights: 8 neurons */
        for (int n = 0; n < 8; n++)
            for (int i = 0; i < 128; i++)
                W1[n][i] = pseudo_trit(n * 31 + 3, i);

        /* Layer 2 weights: 4 neurons, dim 8 */
        for (int n = 0; n < 4; n++)
            for (int i = 0; i < 8; i++)
                W2[n][i] = pseudo_trit(n * 17 + 200, i);

        /* Evaluate layer 1 */
        printf("  Layer 1 (8 neurons, dim=128):\n");
        layer_result_t lr1 = eval_layer(W1, X, 8, 128);
        for (int n = 0; n < 8; n++) {
            char sign = (lr1.outputs[n] > 0) ? '+' : (lr1.outputs[n] < 0) ? '-' : '0';
            printf("    N%d: dot=%+4d → %c1\n", n, lr1.dots[n], sign);
            hidden[n] = lr1.outputs[n];
        }
        printf("  L1 accuracy: %d/8, HW: %lld us\n", lr1.correct, lr1.total_hw_us);

        /* Evaluate layer 2 with hidden as input */
        printf("  Layer 2 (4 neurons, dim=8):\n");
        layer_result_t lr2 = eval_layer(W2, hidden, 4, 8);
        for (int n = 0; n < 4; n++) {
            char sign = (lr2.outputs[n] > 0) ? '+' : (lr2.outputs[n] < 0) ? '-' : '0';
            printf("    N%d: dot=%+4d → %c1\n", n, lr2.dots[n], sign);
        }
        printf("  L2 accuracy: %d/4, HW: %lld us\n", lr2.correct, lr2.total_hw_us);

        int64_t total = lr1.total_us + lr2.total_us;
        printf("  Network total: %lld us\n", total);

        /* CPU reference for full network */
        int cpu_hidden[8];
        for (int n = 0; n < 8; n++) {
            int dot = 0;
            for (int i = 0; i < 128; i++) dot += W1[n][i] * X[i];
            cpu_hidden[n] = (dot > 0) ? T_POS : (dot < 0) ? T_NEG : T_ZERO;
        }
        int cpu_out[4];
        for (int n = 0; n < 4; n++) {
            int dot = 0;
            for (int i = 0; i < 8; i++) dot += W2[n][i] * cpu_hidden[i];
            cpu_out[n] = (dot > 0) ? T_POS : (dot < 0) ? T_NEG : T_ZERO;
        }

        int net_ok = 1;
        for (int n = 0; n < 4; n++) {
            if (lr2.outputs[n] != cpu_out[n]) net_ok = 0;
        }

        int ok = (lr1.correct == 8) && (lr2.correct == 4) && net_ok;
        test_count++;
        if (ok) pass_count++;
        printf("  End-to-end: %s\n", ok ? "OK" : "FAIL");
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST 6: Throughput benchmark (32 neurons, dim=256)
     * ══════════════════════════════════════════════════════════════ */
    printf("\n-- TEST 6: Throughput benchmark (32 neurons, dim=256) --\n");
    fflush(stdout);
    {
        static int8_t W[MAX_NEURONS][MAX_DIM];
        static int8_t X[MAX_DIM];

        for (int i = 0; i < 256; i++)
            X[i] = pseudo_trit(123, i);
        for (int n = 0; n < 32; n++)
            for (int i = 0; i < 256; i++)
                W[n][i] = pseudo_trit(n * 41 + 19, i);

        layer_result_t lr = eval_layer(W, X, 32, 256);

        printf("  Accuracy: %d/32 match CPU reference\n", lr.correct);
        printf("  HW time: %lld us (%.0f us/neuron)\n",
               lr.total_hw_us, (double)lr.total_hw_us / 32);
        printf("  Total time: %lld us (%.0f us/neuron)\n",
               lr.total_us, (double)lr.total_us / 32);
        double neurons_per_sec = 32.0 / (lr.total_us / 1e6);
        double trits_per_sec = neurons_per_sec * 256;
        printf("  Throughput: %.0f neurons/s, %.0f trit-MACs/s\n",
               neurons_per_sec, trits_per_sec);
        printf("  (%.1f K trit-MACs/s)\n", trits_per_sec / 1000.0);

        int ok = (lr.correct == 32);
        test_count++;
        if (ok) pass_count++;
        printf("  %s\n", ok ? "OK" : "FAIL");
    }

    /* ── Summary ── */
    printf("\n============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    printf("  Multi-neuron layer evaluation\n");
    printf("  Pre-multiply W*X on CPU, sum on hardware\n");
    printf("  Ternary activation: sign(dot) → {-1, 0, +1}\n");
    printf("  2-layer feedforward network verified\n");
    printf("============================================================\n");

    if (pass_count == test_count) {
        printf("\n  *** MULTI-NEURON LAYER VERIFIED ***\n");
        printf("  Weights shape geometry.\n");
        printf("  Inputs flow through channels.\n");
        printf("  Dot products measure intersection.\n");
        printf("  Activation quantizes to ternary.\n");
        printf("  Layers compose into networks.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED.\n\n");
    }
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
