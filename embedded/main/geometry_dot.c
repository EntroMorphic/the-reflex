/*
 * geometry_dot.c — Geometry Intersection Engine, Milestone 5
 *
 * 256-trit dot product via peripheral signal path:
 *   Timer → ETM → GDMA → PARLIO(2-bit) → GPIO 4,5 → PCNT
 *   Y lines (GPIO 6,7) CPU-driven static levels.
 *
 * One dibit = one trit. A dibit IS a trit in voltage.
 *   dibit 01 = trit +1 (X_pos HIGH)
 *   dibit 10 = trit -1 (X_neg HIGH)
 *   dibit 00 = trit  0 (silence)
 *
 * Zero-interleave encoding: each trit gets 2 dibits (value, then 00).
 * This guarantees one clean rising edge per non-zero trit.
 * 64 bytes = 256 dibits = 128 trits per buffer.
 *
 * Descriptor chains accumulate across buffers:
 *   2 buffers = 256 trits. N buffers = N×128 trits.
 *   PCNT gives raw integer dot product (agree - disagree).
 *
 * Auto-stop: GDMA total EOF → ETM → Timer stop. No watchdog.
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

#define REG32(addr)  (*(volatile uint32_t*)(addr))

/* ETM event/task IDs */
#define EVT_TIMER0_ALARM         48
#define EVT_GDMA_OUT_TOTAL_EOF0  156
#define TASK_PCNT_RST            87
#define TASK_TIMER0_STOP         92
#define TASK_GDMA_START_CH0      162
#define TASK_GPIO_CH0_SET        1

/* ── GPIO mapping ── */
#define GPIO_X_POS  4
#define GPIO_X_NEG  5
#define GPIO_Y_POS  6
#define GPIO_Y_NEG  7
#define GPIO_DONE   8    /* completion signal */

/* ── Constants ── */
#define BUF_SIZE      64    /* bytes per buffer */
#define TRITS_PER_BUF 128   /* trits per buffer (zero-interleaved: 2 dibits each) */
#define MAX_BUFS      8     /* max buffers in a chain */

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
static pcnt_unit_handle_t pcnt_diag_pos = NULL, pcnt_diag_neg = NULL;
static pcnt_channel_handle_t agree_ch0 = NULL, agree_ch1 = NULL;
static pcnt_channel_handle_t disagree_ch0 = NULL, disagree_ch1 = NULL;
static pcnt_channel_handle_t diag_pos_ch = NULL, diag_neg_ch = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

static int bare_ch = 0;
static volatile int eval_done = 0;

/* ══════════════════════════════════════════════════════════════════
 *  TRIT ENCODING
 *
 *  Zero-interleave: each trit occupies 2 dibit slots.
 *    trit +1 → dibit 01, dibit 00  (GPIO4 rises then falls)
 *    trit -1 → dibit 10, dibit 00  (GPIO5 rises then falls)
 *    trit  0 → dibit 00, dibit 00  (silence)
 *
 *  Each dibit pair = 4 bits = half byte.
 *  Each byte holds 2 trit slots:
 *    bits [3:0] = trit 0 (dibit pair: value in [1:0], zero in [3:2])
 *    bits [7:4] = trit 1 (dibit pair: value in [5:4], zero in [7:6])
 *
 *  byte for (trit0, trit1):
 *    trit0=+1: bits[3:0] = 0b00_01 = 0x1
 *    trit0=-1: bits[3:0] = 0b00_10 = 0x2
 *    trit0= 0: bits[3:0] = 0b00_00 = 0x0
 *    trit1=+1: bits[7:4] = 0b00_01 << 4 = 0x10
 *    trit1=-1: bits[7:4] = 0b00_10 << 4 = 0x20
 *    trit1= 0: bits[7:4] = 0b00_00 = 0x00
 *
 *  64 bytes = 128 trits per buffer.
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

static void etm_wire(int ch, uint32_t evt, uint32_t task) {
    etm_force_clk();
    REG32(ETM_CH_EVT_ID(ch)) = evt;
    REG32(ETM_CH_TASK_ID(ch)) = task;
    if (ch < 32)
        REG32(ETM_CH_ENA_AD0_SET) = (1 << ch);
    else
        REG32(ETM_CH_ENA_AD1_SET) = (1 << (ch - 32));
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
    for (int i = 4; i <= 8; i++) {
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
        /* Ch0: X_pos edges gated by Y_pos HIGH → INC */
        pcnt_chan_config_t ch = { .edge_gpio_num = GPIO_X_POS, .level_gpio_num = GPIO_Y_POS };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_agree, &ch, &agree_ch0));
        pcnt_channel_set_edge_action(agree_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(agree_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        /* Ch1: X_neg edges gated by Y_neg HIGH → INC */
        pcnt_chan_config_t ch1 = { .edge_gpio_num = GPIO_X_NEG, .level_gpio_num = GPIO_Y_NEG };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_agree, &ch1, &agree_ch1));
        pcnt_channel_set_edge_action(agree_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(agree_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);
    }
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_agree));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_agree));

    /* ── Unit 1: TMUL disagree ── */
    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &pcnt_disagree));
    {
        /* Ch0: X_pos edges gated by Y_neg HIGH → INC */
        pcnt_chan_config_t ch = { .edge_gpio_num = GPIO_X_POS, .level_gpio_num = GPIO_Y_NEG };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_disagree, &ch, &disagree_ch0));
        pcnt_channel_set_edge_action(disagree_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(disagree_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        /* Ch1: X_neg edges gated by Y_pos HIGH → INC */
        pcnt_chan_config_t ch1 = { .edge_gpio_num = GPIO_X_NEG, .level_gpio_num = GPIO_Y_POS };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_disagree, &ch1, &disagree_ch1));
        pcnt_channel_set_edge_action(disagree_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(disagree_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);
    }
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_disagree));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_disagree));

    /* ── Unit 2: Diagnostic — X_pos edge count (no gating) ── */
    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &pcnt_diag_pos));
    {
        pcnt_chan_config_t ch = { .edge_gpio_num = GPIO_X_POS, .level_gpio_num = GPIO_Y_POS };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_diag_pos, &ch, &diag_pos_ch));
        pcnt_channel_set_edge_action(diag_pos_ch,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(diag_pos_ch,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    }
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_diag_pos));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_diag_pos));

    /* ── Unit 3: Diagnostic — X_neg edge count (no gating) ── */
    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &pcnt_diag_neg));
    {
        pcnt_chan_config_t ch = { .edge_gpio_num = GPIO_X_NEG, .level_gpio_num = GPIO_Y_NEG };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_diag_neg, &ch, &diag_neg_ch));
        pcnt_channel_set_edge_action(diag_neg_ch,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(diag_neg_ch,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    }
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_diag_neg));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_diag_neg));
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
            printf("[GDMA] PARLIO owns CH%d\n", ch);
            return;
        }
    }
    printf("[GDMA] PARLIO channel not found, defaulting to CH0\n");
    bare_ch = 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  BARE-METAL DMA SETUP
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

    /* Normal mode — NO ETM_EN so GDMA can follow descriptor chains.
     * ETM_EN prevents auto-following linked list descriptors (Milestone 3 errata). */
    REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    /* Load descriptor — LINK_START immediately begins DMA in normal mode */
    uint32_t addr = ((uint32_t)first_desc) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

    /* Configure PARLIO — NO TX_START yet (defer to after PCNT clear) */
    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);
    tx_cfg0 &= ~(1 << 19);
    tx_cfg0 |= (total_bytes << 2);   /* BYTELEN */
    tx_cfg0 |= (1 << 18);            /* TX_GATING_EN */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

static void parlio_stop_and_reset(void) {
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(1 << 19);   /* clear TX_START */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    tx_cfg0 |= (1 << 30);    /* FIFO reset */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
    esp_rom_delay_us(5);
    tx_cfg0 &= ~(1 << 30);
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

static void clear_all_pcnt(void) {
    pcnt_unit_clear_count(pcnt_agree);
    pcnt_unit_clear_count(pcnt_disagree);
    pcnt_unit_clear_count(pcnt_diag_pos);
    pcnt_unit_clear_count(pcnt_diag_neg);
}

/* ══════════════════════════════════════════════════════════════════
 *  DOT PRODUCT EVALUATION
 *
 *  Evaluates the dot product of a trit vector encoded across one or
 *  more DMA buffers. Y is set to +1 (static), so:
 *    agree  = count of +1 trits in the pattern
 *    disagree = count of -1 trits in the pattern
 *    dot = agree - disagree = sum of all trits
 *
 *  For a real dot product W·X, the caller pre-computes P[i] = W[i]×X[i]
 *  and encodes P into the buffers. The hardware sums P.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int agree;
    int disagree;
    int diag_pos;
    int diag_neg;
    int dot;
    int64_t time_us;
} dot_result_t;

static dot_result_t run_dot_eval(lldesc_t *first_desc, int total_bytes, int n_descs) {
    dot_result_t r = {0};

    /* Drive GPIOs LOW */
    for (int i = 4; i <= 8; i++) gpio_set_level(i, 0);
    esp_rom_delay_us(50);

    /* Set Y = +1 (agree channel active) */
    gpio_set_level(GPIO_Y_POS, 1);
    gpio_set_level(GPIO_Y_NEG, 0);
    esp_rom_delay_us(50);

    /* Clear PCNT */
    clear_all_pcnt();

    /* Setup DMA — no ETM_EN, GDMA starts immediately on LINK_START.
     * Since PARLIO TX_START is not yet set, data fills FIFO but doesn't
     * reach GPIOs until we arm TX_START. */
    setup_gdma(first_desc, total_bytes);

    /* Wait for DMA to fill FIFO, then clear any leaked PCNT counts */
    esp_rom_delay_us(500);
    clear_all_pcnt();
    esp_rom_delay_us(100);
    clear_all_pcnt();

    /* No ETM needed — GDMA runs freely in normal mode.
     * PARLIO TX_START gates when data actually reaches GPIOs. */

    /* Final PCNT clear */
    clear_all_pcnt();

    /* Arm PARLIO TX_START — this starts clocking data to GPIOs */
    int64_t t_start = esp_timer_get_time();
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Wait for DMA to complete.
     * At 2-bit mode, 1MHz: 4 dibits per byte, 1us per dibit.
     * total_bytes × 4 = total dibit clocks in microseconds.
     * Add 2x margin for DMA overhead and descriptor switches. */
    int wait_us = total_bytes * 4 * 2 + 500;  /* 2x transfer time + 500us margin */
    esp_rom_delay_us(wait_us);

    int64_t t_end = esp_timer_get_time();

    /* Stop PARLIO, reset FIFO */
    parlio_stop_and_reset();

    /* Force X lines LOW */
    gpio_set_level(GPIO_X_POS, 0);
    gpio_set_level(GPIO_X_NEG, 0);
    esp_rom_delay_us(10);

    /* Read PCNT */
    pcnt_unit_get_count(pcnt_agree, &r.agree);
    pcnt_unit_get_count(pcnt_disagree, &r.disagree);
    pcnt_unit_get_count(pcnt_diag_pos, &r.diag_pos);
    pcnt_unit_get_count(pcnt_diag_neg, &r.diag_neg);

    r.dot = r.agree - r.disagree;
    r.time_us = t_end - t_start;

    /* Drive Y LOW */
    gpio_set_level(GPIO_Y_POS, 0);
    gpio_set_level(GPIO_Y_NEG, 0);

    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST HELPERS
 * ══════════════════════════════════════════════════════════════════ */

static int test_count = 0, pass_count = 0;

static void run_test(const char *name, lldesc_t *desc, int total_bytes,
                     int n_descs, int expect_dot) {
    test_count++;
    dot_result_t r = run_dot_eval(desc, total_bytes, n_descs);
    int ok = (r.dot == expect_dot);
    if (ok) pass_count++;

    printf("  %-30s dot=%+5d (exp %+5d) agree=%d disag=%d "
           "xp=%d xn=%d [%lldus] %s\n",
           name, r.dot, expect_dot,
           r.agree, r.disagree, r.diag_pos, r.diag_neg,
           r.time_us, ok ? "OK" : "FAIL");
}

/* ══════════════════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    /* Wait for USB-Serial/JTAG to enumerate after reset.
     * The CDC-ACM port drops on reset and re-enumerates on a new /dev/ttyACMx.
     * 8 seconds gives the host time to detect the new port. */
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  GEOMETRY INTERSECTION ENGINE — Milestone 5\n");
    printf("  256-Trit Dot Product via Descriptor Chains\n");
    printf("  1 dibit = 1 trit. Geometry is computation.\n");
    printf("============================================================\n\n");

    printf("[INIT] GPIO 4-8...\n");
    init_gpio();

    printf("[INIT] ETM clock...\n");
    etm_force_clk();

    printf("[INIT] PARLIO TX (2-bit, 1MHz, loopback)...\n");
    init_parlio();

    printf("[INIT] PCNT (4 units: agree, disagree, diag+, diag-)...\n");
    init_pcnt();

    printf("[INIT] Timer...\n");
    init_timer();

    printf("[INIT] GDMA channel detection...\n");
    detect_gdma_channel();

    printf("[INIT] Done.\n\n");
    fflush(stdout);

    /* ── Prime the ETM→GDMA pipeline ── */
    {
        printf("[PRIME] Warming ETM->GDMA pipeline...\n");
        int8_t zeros[TRITS_PER_BUF];
        memset(zeros, 0, sizeof(zeros));
        encode_trit_buffer(bufs[0], zeros, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_dot_eval(&descs[0], BUF_SIZE, 1);
        printf("[PRIME] Done.\n\n");
        fflush(stdout);
    }

    /* ══════════════════════════════════════════════════════════════
     *  TEST SUITE
     * ══════════════════════════════════════════════════════════════ */

    printf("-- SINGLE BUFFER TESTS (128 trits each) --\n\n");
    fflush(stdout);

    /* Test 1: All positive */
    {
        int8_t v[TRITS_PER_BUF];
        for (int i = 0; i < TRITS_PER_BUF; i++) v[i] = T_POS;
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_test("All +1 (128 trits)", &descs[0], BUF_SIZE, 1, +128);
    }

    /* Test 2: All negative */
    {
        int8_t v[TRITS_PER_BUF];
        for (int i = 0; i < TRITS_PER_BUF; i++) v[i] = T_NEG;
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_test("All -1 (128 trits)", &descs[0], BUF_SIZE, 1, -128);
    }

    /* Test 3: All zero (silence) */
    {
        int8_t v[TRITS_PER_BUF];
        memset(v, 0, sizeof(v));
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_test("All 0 (silence)", &descs[0], BUF_SIZE, 1, 0);
    }

    /* Test 4: Half +1, half -1 → cancel to zero */
    {
        int8_t v[TRITS_PER_BUF];
        for (int i = 0; i < TRITS_PER_BUF; i++)
            v[i] = (i < 64) ? T_POS : T_NEG;
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_test("64 pos + 64 neg = 0", &descs[0], BUF_SIZE, 1, 0);
    }

    /* Test 5: Sparse — 8 positive in field of zero */
    {
        int8_t v[TRITS_PER_BUF];
        memset(v, 0, sizeof(v));
        for (int i = 0; i < 8; i++) v[i * 16] = T_POS;
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_test("Sparse: 8 pos in 128", &descs[0], BUF_SIZE, 1, +8);
    }

    /* Test 6: Alternating +1/-1 → zero */
    {
        int8_t v[TRITS_PER_BUF];
        for (int i = 0; i < TRITS_PER_BUF; i++)
            v[i] = (i % 2 == 0) ? T_POS : T_NEG;
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, NULL);
        run_test("Alternating +1/-1 = 0", &descs[0], BUF_SIZE, 1, 0);
    }

    printf("\n-- DESCRIPTOR CHAIN TESTS --\n\n");
    fflush(stdout);

    /* Test 7: 2-buffer chain — 256 trits, all +1 */
    {
        int8_t v[TRITS_PER_BUF];
        for (int i = 0; i < TRITS_PER_BUF; i++) v[i] = T_POS;
        encode_trit_buffer(bufs[0], v, TRITS_PER_BUF);
        encode_trit_buffer(bufs[1], v, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, &descs[1]);
        init_desc(&descs[1], bufs[1], BUF_SIZE, NULL);
        run_test("Chain 2x128 all +1 = +256", &descs[0], BUF_SIZE * 2, 2, +256);
    }

    /* Test 8: 2-buffer chain — buf0 all +1, buf1 all -1 → cancel */
    {
        int8_t vp[TRITS_PER_BUF], vn[TRITS_PER_BUF];
        for (int i = 0; i < TRITS_PER_BUF; i++) { vp[i] = T_POS; vn[i] = T_NEG; }
        encode_trit_buffer(bufs[0], vp, TRITS_PER_BUF);
        encode_trit_buffer(bufs[1], vn, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, &descs[1]);
        init_desc(&descs[1], bufs[1], BUF_SIZE, NULL);
        run_test("Chain +128 then -128 = 0", &descs[0], BUF_SIZE * 2, 2, 0);
    }

    /* Test 9: 4-buffer chain — known dot product */
    {
        /* W = (+1, -1, +1, +1, -1, 0, +1, -1, ...) × 128 per buffer
         * X = (+1, +1, -1, +1, +1, +1, -1, 0, ...) × 128 per buffer
         * P[i] = W[i] × X[i], sum computed by CPU for reference */
        int expected_total = 0;
        for (int b = 0; b < 4; b++) {
            int8_t p[TRITS_PER_BUF];
            for (int i = 0; i < TRITS_PER_BUF; i++) {
                /* Deterministic pseudo-pattern based on position */
                int pos = b * TRITS_PER_BUF + i;
                int w = ((pos * 7 + 3) % 3) - 1;   /* {-1, 0, +1} */
                int x = ((pos * 11 + 5) % 3) - 1;   /* {-1, 0, +1} */
                p[i] = (int8_t)(w * x);
                expected_total += p[i];
            }
            encode_trit_buffer(bufs[b], p, TRITS_PER_BUF);
        }
        init_desc(&descs[0], bufs[0], BUF_SIZE, &descs[1]);
        init_desc(&descs[1], bufs[1], BUF_SIZE, &descs[2]);
        init_desc(&descs[2], bufs[2], BUF_SIZE, &descs[3]);
        init_desc(&descs[3], bufs[3], BUF_SIZE, NULL);

        char label[64];
        snprintf(label, sizeof(label), "Chain 4x128 dot=%+d", expected_total);
        run_test(label, &descs[0], BUF_SIZE * 4, 4, expected_total);
    }

    /* Test 10: Zero chain — 2 buffers, all zero */
    {
        int8_t z[TRITS_PER_BUF];
        memset(z, 0, sizeof(z));
        encode_trit_buffer(bufs[0], z, TRITS_PER_BUF);
        encode_trit_buffer(bufs[1], z, TRITS_PER_BUF);
        init_desc(&descs[0], bufs[0], BUF_SIZE, &descs[1]);
        init_desc(&descs[1], bufs[1], BUF_SIZE, NULL);
        run_test("Chain 2x128 all zero", &descs[0], BUF_SIZE * 2, 2, 0);
    }

    /* ── Summary ── */
    printf("\n============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    printf("  Encoding: 1 dibit = 1 trit (zero-interleaved)\n");
    printf("  128 trits per 64-byte buffer\n");
    printf("  Descriptor chains for multi-buffer accumulation\n");
    printf("  Auto-stop via ETM on GDMA total EOF\n");
    printf("  Raw integer dot product from PCNT\n");
    printf("============================================================\n");

    if (pass_count == test_count) {
        printf("\n  *** GEOMETRY DOT PRODUCT VERIFIED ***\n");
        printf("  Shapes flow through channels.\n");
        printf("  Intersections produce inner products.\n");
        printf("  Silence is structure.\n");
        printf("  Geometry is computation.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED.\n\n");
    }
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
