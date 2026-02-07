/*
 * ternary_alu.c — Ternary Sub-CPU ALU on ESP32-C6
 *
 * Balanced ternary multiplication {-1, 0, +1} using the peripheral signal path:
 *   Timer → ETM → GDMA → PARLIO(2-bit) → GPIO 4,5 → PCNT → threshold → result
 *   GPIO 6,7 (Y lines) driven by CPU as static levels before each evaluation.
 *
 * GPIO encoding:
 *   GPIO 4 = X_pos (DMA-driven, toggles to create edges when X = +1)
 *   GPIO 5 = X_neg (DMA-driven, toggles to create edges when X = -1)
 *   GPIO 6 = Y_pos (CPU-driven static level, HIGH when Y = +1)
 *   GPIO 7 = Y_neg (CPU-driven static level, HIGH when Y = -1)
 *
 * PCNT units:
 *   Unit 0: TMUL agree   — X edges gated by matching Y: (+)(+) or (-)(-) 
 *   Unit 1: TMUL disagree — X edges gated by opposite Y: (+)(-) or (-)(+)
 *   Unit 2: X_pos edge count (diagnostic, no gating)
 *   Unit 3: X_neg edge count (diagnostic, no gating)
 *
 * Result:
 *   TMUL(X,Y) = sign(agree - disagree):  >0 → +1,  =0 → 0,  <0 → -1
 *
 * Entropy as structure:
 *   Zero inputs produce no edges → no PCNT activity → no energy.
 *   Sparsity is hardware-native: silence IS the zero computation.
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * ESP-IDF: v5.4
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "driver/ledc.h"
#include "driver/parlio_tx.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "soc/gdma_reg.h"
#include "rom/lldesc.h"

/* ── Register addresses (VERIFIED on silicon) ── */
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
#define LEDC_BASE            0x60007000
#define LEDC_EVT_TASK_EN0    (LEDC_BASE + 0x1A0)
#define LEDC_EVT_TASK_EN1    (LEDC_BASE + 0x1A4)

#define REG32(addr)  (*(volatile uint32_t*)(addr))

/* ETM event/task IDs (verified) */
#define EVT_PCNT_THRESH      45
#define EVT_TIMER0_ALARM     48
#define EVT_TIMER1_ALARM     49
#define TASK_PCNT_RST         87
#define TASK_TIMER0_STOP      92
#define TASK_GDMA_START_CH0  162

/* ── GPIO mapping for ternary encoding ── */
#define GPIO_X_POS  4    /* X = +1 signal line */
#define GPIO_X_NEG  5    /* X = -1 signal line */
#define GPIO_Y_POS  6    /* Y = +1 level gate */
#define GPIO_Y_NEG  7    /* Y = -1 level gate */

/* ── Pattern buffers ── */
#define PAT_SIZE 64

/* PCNT watch threshold — above noise floor, below real edge count.
 * 2-bit PARLIO mode: 64 bytes × 4 dibits × alternating = 128 rising edges.
 * Y lines are CPU-driven (static) so no byte-boundary glitch noise on Y.
 * Noise floor should be near zero. Threshold 40 gives wide margin. */
#define PCNT_WATCH_THRESH 40

/* Ternary values */
#define TRIT_POS  (+1)
#define TRIT_ZERO  (0)
#define TRIT_NEG  (-1)

/* ── Pattern buffers (X-only; Y is CPU-driven via gpio_set_level) ── */
static uint8_t __attribute__((aligned(4))) pat_xpos[PAT_SIZE];  /* X = +1 */
static uint8_t __attribute__((aligned(4))) pat_xneg[PAT_SIZE];  /* X = -1 */
static uint8_t __attribute__((aligned(4))) pat_xzero[PAT_SIZE]; /* X =  0 */

/* GDMA descriptors */
static lldesc_t __attribute__((aligned(4))) desc[4];

/* ── Peripheral handles ── */
static gptimer_handle_t timer0 = NULL, timer1 = NULL;
static pcnt_unit_handle_t tmul_agree = NULL, tmul_disagree = NULL;
static pcnt_unit_handle_t tadd_pos = NULL, tadd_neg = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static pcnt_channel_handle_t addpos_ch0 = NULL, addpos_ch1 = NULL;
static pcnt_channel_handle_t addneg_ch0 = NULL, addneg_ch1 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

static volatile int watchdog_fired = 0;
static volatile uint32_t transition_count = 0;
static int etm_used = 0;
static int bare_ch = 0;  /* Which GDMA channel PARLIO owns */

/* ── Helpers ── */

static void etm_force_clk(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf = (*conf & ~(1 << 1)) | (1 << 0);
    esp_rom_delay_us(1);
    REG32(ETM_CLK_EN_REG) = 1;
    esp_rom_delay_us(1);
}

static void etm_wire(int ch, uint32_t evt, uint32_t task) {
    etm_force_clk();
    REG32(ETM_CH_EVT_ID(ch)) = evt;
    REG32(ETM_CH_TASK_ID(ch)) = task;
    if (ch < 32)
        REG32(ETM_CH_ENA_AD0_SET) = (1 << ch);
    else
        REG32(ETM_CH_ENA_AD1_SET) = (1 << (ch - 32));
    etm_used++;
}

static bool IRAM_ATTR watchdog_cb(gptimer_handle_t t,
    const gptimer_alarm_event_data_t *d, void *ctx) {
    watchdog_fired = 1;
    volatile uint32_t *t0cfg = (volatile uint32_t*)0x60008000;
    *t0cfg &= ~(1U << 31);
    return true;
}

static bool IRAM_ATTR pcnt_watch_cb(pcnt_unit_handle_t unit,
    const pcnt_watch_event_data_t *edata, void *user_ctx) {
    transition_count++;
    return false;
}

/* ══════════════════════════════════════════════════════════════════
 *  PATTERN ENCODING — 2-BIT PARLIO MODE
 *
 *  PARLIO 2-bit mode: drives ONLY GPIO 4 (X_pos) and GPIO 5 (X_neg).
 *  GPIO 6 (Y_pos) and GPIO 7 (Y_neg) are driven by CPU via gpio_set_level()
 *  BEFORE each test, making them truly static — immune to byte-boundary
 *  dead-cycle glitches.
 *
 *  Each byte has 4 dibit slots, clocked out LSB-first:
 *    bits [1:0] → 1st clock: GPIO5=bit1(X_neg), GPIO4=bit0(X_pos)
 *    bits [3:2] → 2nd clock
 *    bits [5:4] → 3rd clock
 *    bits [7:6] → 4th clock
 *
 *  Dibit encoding:
 *    0b01 = X_pos=1, X_neg=0 → X active positive
 *    0b10 = X_pos=0, X_neg=1 → X active negative
 *    0b00 = both LOW           → X inactive (zero)
 *
 *  For X=+1: alternate 0b01 and 0b00 within each byte:
 *    byte = 0b00_01_00_01 = 0x05
 *    4 dibits per byte: X_pos goes 1,0,1,0 → 2 rising edges per byte.
 *    64 bytes × 2 rising edges = 128 total rising edges on X_pos.
 *
 *  For X=-1: alternate 0b10 and 0b00:
 *    byte = 0b00_10_00_10 = 0x0A
 *    128 rising edges on X_neg.
 *
 *  For X=0: all zeros = 0x00. Total silence. No edges anywhere.
 *
 *  PCNT level gating reads GPIO 6/7 (Y lines) which are static CPU-driven
 *  levels. No byte-boundary glitches possible on Y.
 * ══════════════════════════════════════════════════════════════════ */

static void init_patterns(void) {
    for (int i = 0; i < PAT_SIZE; i++) {
        /* X=+1: dibits 01,00,01,00 → 0x05
         * X_pos toggles: 1,0,1,0 per byte → 2 rising edges/byte × 64 = 128 */
        pat_xpos[i] = 0x05;

        /* X=-1: dibits 10,00,10,00 → 0x0A
         * X_neg toggles: 1,0,1,0 per byte → 2 rising edges/byte × 64 = 128 */
        pat_xneg[i] = 0x0A;

        /* X=0: all zeros. Maximum silence. */
        pat_xzero[i] = 0x00;
    }
}

static void init_desc(lldesc_t *d, uint8_t *buf, lldesc_t *next) {
    memset(d, 0, sizeof(lldesc_t));
    d->size = PAT_SIZE;
    d->length = PAT_SIZE;
    d->buf = buf;
    d->owner = 1;
    d->eof = 1;
    d->empty = next ? ((uint32_t)next) : 0;
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
    }
}

static void init_parlio(void) {
    /* 2-bit mode: PARLIO drives ONLY GPIO 4 (X_pos) and GPIO 5 (X_neg).
     * GPIO 6 (Y_pos) and GPIO 7 (Y_neg) are driven by CPU via gpio_set_level()
     * BEFORE each test. This eliminates byte-boundary glitches on Y lines.
     *
     * With 2-bit mode, each byte contains 4 dibit slots:
     *   bits [1:0] → first clock: GPIO4=bit0, GPIO5=bit1
     *   bits [3:2] → second clock
     *   bits [5:4] → third clock
     *   bits [7:6] → fourth clock
     * 64 bytes × 4 clocks = 256 dibit outputs = 256 potential edges. */
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
    /* Only 2 data pins: GPIO4 (X_pos) and GPIO5 (X_neg) */
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++)
        cfg.data_gpio_nums[i] = (i < 2) ? (4 + i) : -1;
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    ESP_ERROR_CHECK(parlio_tx_unit_enable(parlio));
}

/* ══════════════════════════════════════════════════════════════════
 *  PCNT CONFIGURATION — 4 units, 2 channels each
 *
 *  2-BIT PARLIO MODE: PARLIO drives GPIO 4 (X_pos) and GPIO 5 (X_neg).
 *  GPIO 6 (Y_pos) and GPIO 7 (Y_neg) are CPU-driven static levels,
 *  set via gpio_set_level() before each test. No Y edges exist.
 *
 *  Unit 0 (TMUL agree): counts when X and Y have same sign
 *    Ch0: edge=X_pos(GPIO4), level=Y_pos(GPIO6) → INC when Y=+1
 *    Ch1: edge=X_neg(GPIO5), level=Y_neg(GPIO7) → INC when Y=-1
 *    When Y=+1: only Ch0 active (counts X_pos edges). agree = X_pos count.
 *    When Y=-1: only Ch1 active (counts X_neg edges). agree = X_neg count.
 *    When Y=0:  both gated LOW → agree = 0. Perfect.
 *
 *  Unit 1 (TMUL disagree): counts when X and Y have opposite sign
 *    Ch0: edge=X_pos(GPIO4), level=Y_neg(GPIO7) → INC when Y=-1
 *    Ch1: edge=X_neg(GPIO5), level=Y_pos(GPIO6) → INC when Y=+1
 *    When Y=-1: Ch0 counts X_pos edges. disagree = X_pos count.
 *    When Y=+1: Ch1 counts X_neg edges. disagree = X_neg count.
 *    When Y=0:  both gated LOW → disagree = 0.
 *
 *  TMUL result = sign(agree - disagree):
 *    X=+1,Y=+1: agree=128(X_pos gated by Y_pos), disagree=0 → +1 ✓
 *    X=+1,Y=-1: agree=0, disagree=128(X_pos gated by Y_neg) → -1 ✓
 *    X=-1,Y=-1: agree=128(X_neg gated by Y_neg), disagree=0 → +1 ✓
 *    X=0,Y=any: no X edges → agree=0, disagree=0 → 0 ✓
 *    X=any,Y=0: both gates LOW → agree=0, disagree=0 → 0 ✓
 *
 *  Units 2,3 (TADD): diagnostic only — count X_pos and X_neg edges
 *    with no level gating. Ch1 watches Y lines but Y is static (no edges),
 *    so Ch1 counts are always 0. TADD deferred to future architecture.
 * ══════════════════════════════════════════════════════════════════ */

static void init_pcnt(void) {
    pcnt_unit_config_t ucfg = { .low_limit = -300, .high_limit = 300 };

    /* ── Unit 0: TMUL agree ── */
    {
        ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &tmul_agree));

        /* Ch0: X_pos edges gated by Y_pos HIGH → INC on pos_edge, KEEP when HIGH */
        pcnt_chan_config_t ch0 = {
            .edge_gpio_num = GPIO_X_POS,
            .level_gpio_num = GPIO_Y_POS
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tmul_agree, &ch0, &agree_ch0));
        pcnt_channel_set_edge_action(agree_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(agree_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        /* Ch1: X_neg edges gated by Y_neg HIGH → INC on pos_edge, KEEP when HIGH */
        pcnt_chan_config_t ch1 = {
            .edge_gpio_num = GPIO_X_NEG,
            .level_gpio_num = GPIO_Y_NEG
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tmul_agree, &ch1, &agree_ch1));
        pcnt_channel_set_edge_action(agree_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(agree_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(tmul_agree, PCNT_WATCH_THRESH));
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(tmul_agree, &cb, NULL);
        ESP_ERROR_CHECK(pcnt_unit_enable(tmul_agree));
        ESP_ERROR_CHECK(pcnt_unit_start(tmul_agree));
    }

    /* ── Unit 1: TMUL disagree ── */
    {
        ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &tmul_disagree));

        /* Ch0: X_pos edges gated by Y_neg HIGH */
        pcnt_chan_config_t ch0 = {
            .edge_gpio_num = GPIO_X_POS,
            .level_gpio_num = GPIO_Y_NEG
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tmul_disagree, &ch0, &disagree_ch0));
        pcnt_channel_set_edge_action(disagree_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(disagree_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        /* Ch1: X_neg edges gated by Y_pos HIGH */
        pcnt_chan_config_t ch1 = {
            .edge_gpio_num = GPIO_X_NEG,
            .level_gpio_num = GPIO_Y_POS
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tmul_disagree, &ch1, &disagree_ch1));
        pcnt_channel_set_edge_action(disagree_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(disagree_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(tmul_disagree, PCNT_WATCH_THRESH));
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(tmul_disagree, &cb, NULL);
        ESP_ERROR_CHECK(pcnt_unit_enable(tmul_disagree));
        ESP_ERROR_CHECK(pcnt_unit_start(tmul_disagree));
    }

    /* ── Unit 2: TADD positive ── */
    {
        ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &tadd_pos));

        /* Ch0: X_pos edges, no level gating → INC always
         * Use Y_pos as level_gpio but KEEP for both HIGH and LOW */
        pcnt_chan_config_t ch0 = {
            .edge_gpio_num = GPIO_X_POS,
            .level_gpio_num = GPIO_Y_POS  /* required but ignored via KEEP/KEEP */
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tadd_pos, &ch0, &addpos_ch0));
        pcnt_channel_set_edge_action(addpos_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(addpos_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

        /* Ch1: Y_pos edges, no level gating → INC always */
        pcnt_chan_config_t ch1 = {
            .edge_gpio_num = GPIO_Y_POS,
            .level_gpio_num = GPIO_X_POS  /* required but ignored via KEEP/KEEP */
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tadd_pos, &ch1, &addpos_ch1));
        pcnt_channel_set_edge_action(addpos_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(addpos_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(tadd_pos, PCNT_WATCH_THRESH));
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(tadd_pos, &cb, NULL);
        ESP_ERROR_CHECK(pcnt_unit_enable(tadd_pos));
        ESP_ERROR_CHECK(pcnt_unit_start(tadd_pos));
    }

    /* ── Unit 3: TADD negative ── */
    {
        ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &tadd_neg));

        /* Ch0: X_neg edges, no level gating → INC always */
        pcnt_chan_config_t ch0 = {
            .edge_gpio_num = GPIO_X_NEG,
            .level_gpio_num = GPIO_Y_NEG  /* ignored via KEEP/KEEP */
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tadd_neg, &ch0, &addneg_ch0));
        pcnt_channel_set_edge_action(addneg_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(addneg_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

        /* Ch1: Y_neg edges, no level gating → INC always */
        pcnt_chan_config_t ch1 = {
            .edge_gpio_num = GPIO_Y_NEG,
            .level_gpio_num = GPIO_X_NEG  /* ignored via KEEP/KEEP */
        };
        ESP_ERROR_CHECK(pcnt_new_channel(tadd_neg, &ch1, &addneg_ch1));
        pcnt_channel_set_edge_action(addneg_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(addneg_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(tadd_neg, PCNT_WATCH_THRESH));
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(tadd_neg, &cb, NULL);
        ESP_ERROR_CHECK(pcnt_unit_enable(tadd_neg));
        ESP_ERROR_CHECK(pcnt_unit_start(tadd_neg));
    }
}

static void init_timers(void) {
    gptimer_config_t c0 = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c0, &timer0));

    gptimer_config_t c1 = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c1, &timer1));

    gptimer_event_callbacks_t cbs = { .on_alarm = watchdog_cb };
    gptimer_register_event_callbacks(timer1, &cbs, NULL);

    gptimer_enable(timer0);
    gptimer_enable(timer1);

    /* Enable ETM tasks for timers */
    volatile uint32_t *t0cfg = (volatile uint32_t*)0x60008000;
    *t0cfg |= (1 << 28);  /* ETM_EN for timer0 */
}

static void init_ledc(void) {
    /* Enable LEDC ETM events and tasks */
    REG32(LEDC_EVT_TASK_EN0) |= (0x7 << 8);
    REG32(LEDC_EVT_TASK_EN1) |= (0x7 << 16) | (0x7 << 28);

    ledc_timer_config_t lt = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 10000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&lt);
}

static void detect_gdma_channel(void) {
    for (int ch = 0; ch < 3; ch++) {
        uint32_t peri = REG32(GDMA_OUT_BASE(ch) + GDMA_OUT_PERI_SEL) & 0x3F;
        if (peri == GDMA_PERI_PARLIO) {
            bare_ch = ch;
            printf("[GDMA] PARLIO owns CH%d, bare-metal will use CH%d\n", ch, ch);
            return;
        }
    }
    printf("[GDMA] PARLIO channel not found, defaulting to CH0\n");
    bare_ch = 0;
}

static void setup_gdma_bare(lldesc_t *first_desc) {
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

    /* EOF mode + ETM trigger */
    REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT | GDMA_ETM_EN_BIT;
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    /* Load descriptor — LINK_START arms channel, ETM_EN defers transfer */
    uint32_t addr = ((uint32_t)first_desc) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    /* Configure PARLIO — NO TX_START yet (defer until after PCNT clear) */
    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);
    tx_cfg0 &= ~(1 << 19);       /* clear TX_START */
    tx_cfg0 |= (PAT_SIZE << 2);  /* BYTELEN = 64 */
    tx_cfg0 |= (1 << 18);        /* TX_GATING_EN */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY CLASSIFICATION
 *
 *  Given agree/disagree counts (for TMUL) or pos/neg counts (for TADD),
 *  classify the result as {+1, 0, -1}.
 *
 *  The noise margin IS the zero band. Counts within ±PCNT_WATCH_THRESH
 *  of each other are classified as zero. This is the ternary advantage:
 *  what was a bug in binary (stray counts) is a feature in ternary
 *  (the dead zone between +1 and -1).
 * ══════════════════════════════════════════════════════════════════ */

static int classify_ternary(int pos_count, int neg_count) {
    int diff = pos_count - neg_count;
    if (diff >= PCNT_WATCH_THRESH)  return TRIT_POS;
    if (diff <= -PCNT_WATCH_THRESH) return TRIT_NEG;
    return TRIT_ZERO;
}

/* ══════════════════════════════════════════════════════════════════
 *  AUTONOMOUS TERNARY GATE EVALUATION
 * ══════════════════════════════════════════════════════════════════ */

static int run_ternary_eval(const char *name, uint8_t *x_pattern,
                             int y_val, int expect_tmul) {
    printf("  %-12s: ", name);
    fflush(stdout);

    /* Reset state */
    watchdog_fired = 0;
    transition_count = 0;

    /* Drive all GPIOs LOW first */
    for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(50);

    /* Set Y lines via CPU — static levels, no byte-boundary glitches */
    gpio_set_level(GPIO_Y_POS, (y_val == TRIT_POS) ? 1 : 0);
    gpio_set_level(GPIO_Y_NEG, (y_val == TRIT_NEG) ? 1 : 0);
    esp_rom_delay_us(50);

    /* Clear all PCNT units */
    pcnt_unit_clear_count(tmul_agree);
    pcnt_unit_clear_count(tmul_disagree);
    pcnt_unit_clear_count(tadd_pos);
    pcnt_unit_clear_count(tadd_neg);

    /* Set up descriptor */
    init_desc(&desc[0], x_pattern, NULL);

    /* Force ETM clock and clear all channels */
    etm_force_clk();
    REG32(ETM_CH_ENA_AD0_CLR) = 0xFFFFFFFF;
    REG32(ETM_CH_ENA_AD1_CLR) = 0x0003FFFF;
    etm_used = 0;
    esp_rom_delay_us(10);

    /* Set up GDMA bare-metal */
    setup_gdma_bare(&desc[0]);

    /* Wait for any leaked DMA data, then clear PCNT.
     * GDMA LINK_START may push data immediately despite ETM_EN.
     * The deferred TX_START prevents GPIO activity, but be safe. */
    esp_rom_delay_us(500);
    pcnt_unit_clear_count(tmul_agree);
    pcnt_unit_clear_count(tmul_disagree);
    pcnt_unit_clear_count(tadd_pos);
    pcnt_unit_clear_count(tadd_neg);
    esp_rom_delay_us(100);
    pcnt_unit_clear_count(tmul_agree);
    pcnt_unit_clear_count(tmul_disagree);
    pcnt_unit_clear_count(tadd_pos);
    pcnt_unit_clear_count(tadd_neg);

    /* Wire ETM */
    uint32_t gdma_start_task = TASK_GDMA_START_CH0 + bare_ch;

    /* Ch 0: Timer0 alarm → GDMA start */
    etm_wire(0, EVT_TIMER0_ALARM, gdma_start_task);
    /* Ch 1: Timer0 alarm → PCNT reset */
    etm_wire(1, EVT_TIMER0_ALARM, TASK_PCNT_RST);
    /* Ch 2: Timer1 alarm → Timer0 stop (watchdog) */
    etm_wire(2, EVT_TIMER1_ALARM, TASK_TIMER0_STOP);

    /* Final PCNT clear */
    pcnt_unit_clear_count(tmul_agree);
    pcnt_unit_clear_count(tmul_disagree);
    pcnt_unit_clear_count(tadd_pos);
    pcnt_unit_clear_count(tadd_neg);

    /* Arm PARLIO TX_START */
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Start watchdog (2ms — DMA takes ~256us, 8x margin) */
    gptimer_alarm_config_t a1 = { .alarm_count = 2000, .flags.auto_reload_on_alarm = false };
    gptimer_set_alarm_action(timer1, &a1);
    gptimer_set_raw_count(timer1, 0);
    gptimer_start(timer1);

    /* Kickoff timer (10us) */
    gptimer_alarm_config_t a0 = { .alarm_count = 10, .flags.auto_reload_on_alarm = false };
    gptimer_set_alarm_action(timer0, &a0);
    gptimer_set_raw_count(timer0, 0);

    int64_t t_start = esp_timer_get_time();
    gptimer_start(timer0);

    /* CPU idle — all computation in hardware */
    while (!watchdog_fired) {
        __asm__ volatile("nop");
    }

    int64_t t_end = esp_timer_get_time();
    gptimer_stop(timer0);
    gptimer_stop(timer1);

    /* Kill PARLIO TX and reset FIFO to prevent lingering GPIO states */
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 &= ~(1 << 19);   /* clear TX_START */
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
        tx_cfg0 |= (1 << 30);    /* FIFO reset */
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
        esp_rom_delay_us(5);
        tx_cfg0 &= ~(1 << 30);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Force X lines LOW before reading PCNT */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    esp_rom_delay_us(10);

    /* Read PCNT units */
    int agree_v, disagree_v, addpos_v, addneg_v;
    pcnt_unit_get_count(tmul_agree, &agree_v);
    pcnt_unit_get_count(tmul_disagree, &disagree_v);
    pcnt_unit_get_count(tadd_pos, &addpos_v);
    pcnt_unit_get_count(tadd_neg, &addneg_v);

    /* Classify TMUL result */
    int tmul_result = classify_ternary(agree_v, disagree_v);
    int tmul_ok = (tmul_result == expect_tmul);

    /* Display */
    const char *trit_sym[] = { "-1", " 0", "+1" };

    printf("TMUL=%s(%s)  agree=%d disag=%d  addP=%d addN=%d  [%lldus] %s\n",
           trit_sym[tmul_result + 1], tmul_ok ? "ok" : "FAIL",
           agree_v, disagree_v, addpos_v, addneg_v,
           t_end - t_start,
           tmul_ok ? "OK" : "FAIL");

    /* Drive Y lines back LOW after test */
    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    return tmul_ok;
}

/* ══════════════════════════════════════════════════════════════════ */
/*  SANITY CHECK: CPU-driven IDF PARLIO                              */
/* ══════════════════════════════════════════════════════════════════ */

static void run_sanity_check(void) {
    printf("\n-- SANITY CHECK: CPU-driven PARLIO (2-bit) + CPU-driven Y --\n");

    parlio_transmit_config_t tx = { .idle_value = 0 };
    struct {
        const char *name;
        uint8_t *x_pattern;
        int x_val, y_val;
    } checks[] = {
        { "X=+1,Y=+1", pat_xpos, +1, +1 },
        { "X=+1,Y=-1", pat_xpos, +1, -1 },
        { "X=-1,Y=+1", pat_xneg, -1, +1 },
        { "X=-1,Y=-1", pat_xneg, -1, -1 },
        { "X=+1,Y= 0", pat_xpos, +1,  0 },
        { "X= 0,Y=+1", pat_xzero, 0, +1 },
        { "X= 0,Y= 0", pat_xzero, 0,  0 },
    };
    int nchecks = sizeof(checks) / sizeof(checks[0]);

    for (int i = 0; i < nchecks; i++) {
        /* Drive all GPIOs LOW */
        for (int g = 4; g <= 7; g++) gpio_set_level(g, 0);
        esp_rom_delay_us(50);

        /* Set Y lines via CPU */
        gpio_set_level(GPIO_Y_POS, (checks[i].y_val == TRIT_POS) ? 1 : 0);
        gpio_set_level(GPIO_Y_NEG, (checks[i].y_val == TRIT_NEG) ? 1 : 0);
        esp_rom_delay_us(50);

        pcnt_unit_clear_count(tmul_agree);
        pcnt_unit_clear_count(tmul_disagree);
        pcnt_unit_clear_count(tadd_pos);
        pcnt_unit_clear_count(tadd_neg);

        parlio_tx_unit_transmit(parlio, checks[i].x_pattern, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(100);

        int av, dv, pv, nv;
        pcnt_unit_get_count(tmul_agree, &av);
        pcnt_unit_get_count(tmul_disagree, &dv);
        pcnt_unit_get_count(tadd_pos, &pv);
        pcnt_unit_get_count(tadd_neg, &nv);

        int x = checks[i].x_val, y = checks[i].y_val;
        int expect_mul = x * y;

        int tmul = classify_ternary(av, dv);

        printf("  %-12s agree=%3d disag=%3d addP=%3d addN=%3d → TMUL=%+d(exp %+d) %s\n",
               checks[i].name,
               av, dv, pv, nv,
               tmul, expect_mul,
               (tmul == expect_mul) ? "OK" : "FAIL");

        /* Drive Y back LOW */
        gpio_set_level(GPIO_Y_POS, 0);
        gpio_set_level(GPIO_Y_NEG, 0);
    }
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════ */
/*  MAIN                                                             */
/* ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    /* Wait for USB-Serial/JTAG to enumerate so output isn't lost */
    vTaskDelay(pdMS_TO_TICKS(3000));

    printf("\n");
    printf("============================================================\n");
    printf("  TERNARY SUB-CPU ALU — ESP32-C6\n");
    printf("  Balanced ternary TMUL {-1, 0, +1} via peripheral signal path\n");
    printf("  Timer -> ETM -> GDMA -> PARLIO(2-bit) -> GPIO 4,5 -> PCNT\n");
    printf("  Y lines (GPIO 6,7) = CPU-driven static levels\n");
    printf("============================================================\n\n");

    printf("[INIT] Patterns...\n");
    init_patterns();

    printf("[INIT] ETM clock...\n");
    etm_force_clk();

    printf("[INIT] GPIO 4-7...\n");
    init_gpio();

    printf("[INIT] PARLIO TX (2-bit, 1MHz, loopback, X-only)...\n");
    init_parlio();

    printf("[INIT] PCNT (4 units: agree, disagree, add_pos, add_neg)...\n");
    init_pcnt();

    printf("[INIT] Timers...\n");
    init_timers();

    printf("[INIT] LEDC (ETM tasks)...\n");
    init_ledc();

    printf("[INIT] GDMA channel detection...\n");
    detect_gdma_channel();

    printf("[INIT] Done.\n");
    fflush(stdout);

    /* ── Sanity check with CPU-driven PARLIO ── */
    run_sanity_check();

    /* ── Prime the ETM→GDMA pipeline with a dummy run ──
     * The first ETM-triggered DMA often fails (GDMA not yet armed).
     * Run a throwaway zero-pattern evaluation to warm the pipeline. */
    {
        printf("\n[PRIME] Dummy DMA run to warm ETM->GDMA pipeline...\n");
        fflush(stdout);
        run_ternary_eval("(prime)", pat_xzero, 0, 0);
        printf("[PRIME] Done.\n");
        fflush(stdout);
    }

    /* ── Full ternary TMUL test matrix: 9 cases ── */
    printf("\n-- AUTONOMOUS TERNARY TMUL EVALUATION --\n");
    printf("  Timer → ETM → GDMA → PARLIO(2-bit) → GPIO 4,5 → PCNT\n");
    printf("  Y lines (GPIO 6,7) driven by CPU before each test (static).\n");
    printf("  CPU in NOP loop during DMA evaluation.\n\n");
    fflush(stdout);

    int passed = 0, total = 0;

    /*  X pattern    Y value   expected TMUL = X*Y  */
    struct {
        const char *name;
        uint8_t *x_pattern;
        int y_val;
        int expect_tmul;
    } tests[] = {
        { "(+1)*(+1)", pat_xpos,  +1,  +1 },
        { "(+1)*( 0)", pat_xpos,   0,   0 },
        { "(+1)*(-1)", pat_xpos,  -1,  -1 },
        { "( 0)*(+1)", pat_xzero, +1,   0 },
        { "( 0)*( 0)", pat_xzero,  0,   0 },
        { "( 0)*(-1)", pat_xzero, -1,   0 },
        { "(-1)*(+1)", pat_xneg,  +1,  -1 },
        { "(-1)*( 0)", pat_xneg,   0,   0 },
        { "(-1)*(-1)", pat_xneg,  -1,  +1 },
    };

    int ntests = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < ntests; i++) {
        int ok = run_ternary_eval(tests[i].name, tests[i].x_pattern,
                                   tests[i].y_val, tests[i].expect_tmul);
        passed += ok;
        total++;
    }

    /* ── Sparsity verification ── */
    printf("\n-- SPARSITY TEST: Zero = Silence --\n");
    printf("  Verify that zero inputs produce zero PCNT activity.\n");
    printf("  Entropy as structure: nothing toggles, nothing counts.\n\n");
    fflush(stdout);

    {
        /* Clear everything */
        for (int i = 4; i <= 7; i++) gpio_set_level(i, 0);
        esp_rom_delay_us(50);
        pcnt_unit_clear_count(tmul_agree);
        pcnt_unit_clear_count(tmul_disagree);
        pcnt_unit_clear_count(tadd_pos);
        pcnt_unit_clear_count(tadd_neg);

        /* Transmit the zero X pattern with Y=0 (CPU-driven) */
        parlio_transmit_config_t tx = { .idle_value = 0 };
        parlio_tx_unit_transmit(parlio, pat_xzero, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(100);

        int av, dv, pv, nv;
        pcnt_unit_get_count(tmul_agree, &av);
        pcnt_unit_get_count(tmul_disagree, &dv);
        pcnt_unit_get_count(tadd_pos, &pv);
        pcnt_unit_get_count(tadd_neg, &nv);

        int all_zero = (av == 0 && dv == 0 && pv == 0 && nv == 0);
        printf("  X=0,Y=0: agree=%d disag=%d addP=%d addN=%d -> %s\n",
               av, dv, pv, nv, all_zero ? "SILENCE (zero computation)" : "NOISE DETECTED");

        /* Also test X=0,Y=+1 — Y level is HIGH (CPU) but no X edges from PARLIO */
        for (int g = 4; g <= 7; g++) gpio_set_level(g, 0);
        esp_rom_delay_us(50);
        gpio_set_level(GPIO_Y_POS, 1);  /* Y = +1 via CPU */
        esp_rom_delay_us(50);

        pcnt_unit_clear_count(tmul_agree);
        pcnt_unit_clear_count(tmul_disagree);
        pcnt_unit_clear_count(tadd_pos);
        pcnt_unit_clear_count(tadd_neg);

        parlio_tx_unit_transmit(parlio, pat_xzero, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(100);

        pcnt_unit_get_count(tmul_agree, &av);
        pcnt_unit_get_count(tmul_disagree, &dv);
        pcnt_unit_get_count(tadd_pos, &pv);
        pcnt_unit_get_count(tadd_neg, &nv);

        gpio_set_level(GPIO_Y_POS, 0);

        int mul_zero = (av == 0 && dv == 0);
        printf("  X=0,Y=+1: agree=%d disag=%d addP=%d addN=%d -> TMUL %s\n",
               av, dv, pv, nv,
               mul_zero ? "SILENCE (0 * +1 = 0)" : "NOISE");
        printf("    (Y_pos is CPU-driven static HIGH, no X edges -> no PCNT counts)\n");

        total += 2;
        passed += all_zero;
        passed += mul_zero;
    }

    /* ── Summary ── */
    printf("\n============================================================\n");
    printf("  RESULTS: %d / %d TMUL TESTS PASSED\n", passed, total);
    printf("============================================================\n");
    printf("  Signal path (CPU-free during evaluation):\n");
    printf("    Timer -> ETM -> GDMA -> PARLIO(2-bit) -> GPIO 4,5\n");
    printf("    Y lines (GPIO 6,7) = CPU-driven static levels\n");
    printf("    PCNT: agree(U0) disagree(U1) [TMUL]\n");
    printf("    PCNT: addP(U2) addN(U3) [diagnostic, TADD deferred]\n");
    printf("  Ternary classification: noise margin IS the zero band\n");
    printf("============================================================\n");

    if (passed == total) {
        printf("\n  *** TERNARY TMUL VERIFIED ***\n");
        printf("  Balanced ternary {-1, 0, +1} multiplication\n");
        printf("  natively in peripheral hardware.\n");
        printf("  Zero inputs = zero computation = structured silence.\n");
        printf("  The hardware computes in the natural basis of its physics.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED.\n\n");
    }
    fflush(stdout);

    /* Idle forever */
    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
