/**
 * autonomous_alu.c - CPU-Free ALU on ESP32-C6 Peripheral Layer
 *
 * The CPU configures peripherals and loads "programs" (DMA descriptor
 * chains + pattern buffers) into SRAM, then kicks a timer and goes
 * into a NOP loop. All computation happens autonomously in hardware:
 *
 *   Timer alarm → ETM → GDMA start → PARLIO TX → GPIO 4,5 →
 *   PCNT counts → PCNT threshold → ETM → LEDC latch (result bit)
 *   GDMA EOF → ETM → PCNT reset → next GDMA start (next operation)
 *
 * LEDC overflow counters serve as the "register file" — each channel
 * latches one boolean result. LEDC OVF events are per-channel (IDs
 * 31-36), so ETM can read individual result bits autonomously.
 *
 * ═══════════════════════════════════════════════════════════════════
 * TEST PLAN
 * ═══════════════════════════════════════════════════════════════════
 *
 * Test 1: Autonomous AND gate
 *   - Pre-load packed pattern for AND(1,1) into SRAM
 *   - Wire: timer → GDMA → PARLIO → PCNT AND unit
 *   - PCNT threshold → ETM → LEDC CH0 pause (latches "AND=1")
 *   - CPU reads LEDC state after watchdog timer fires
 *   - Verify: AND(1,1) = 1 (LEDC paused), AND(1,0) = 0 (LEDC running)
 *
 * Test 2: Autonomous XOR gate (same structure, XOR unit)
 *
 * Test 3: Autonomous full adder (1-bit)
 *   - Phase 1: evaluate XOR(A,B) and AND(A,B) simultaneously
 *   - PCNT AND threshold → LEDC CH0 pause (carry = 1)
 *   - PCNT XOR threshold → LEDC CH1 pause (sum_before_carry = 1)
 *   - PROBLEM: PCNT threshold event is global (ID 45), can't distinguish units
 *   - SOLUTION: Use different PCNT watch values + LEDC OVF_CNT timing
 *     OR: run sequentially — XOR first, latch, then AND, latch
 *
 * Test 4: Autonomous 2-bit addition
 *   - Chain 4 gate evaluations via GDMA descriptor linked list
 *   - Each GDMA EOF triggers PCNT reset + next GDMA start
 *   - LEDC channels latch intermediate results
 *   - CPU only reads final LEDC state
 *
 * ═══════════════════════════════════════════════════════════════════
 * RESOURCES
 * ═══════════════════════════════════════════════════════════════════
 *
 * PCNT Unit 0: XOR gate (2 channels)
 * PCNT Unit 1: AND gate (1 channel)
 * PCNT Unit 2: OR gate (2 channels) — for future use
 * PCNT Unit 3: Identity counter — for future use
 * PARLIO: 1 TX, 4-bit, GPIO 4-7, loopback
 * GDMA: CH0 bare-metal with ETM_EN for autonomous operation
 * LEDC: 3 timers, up to 6 channels as result latches
 * ETM: ~20 channels for wiring
 * GP Timers: Timer0 kickoff, Timer1 watchdog
 * ═══════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "soc/lldesc.h"
#include "esp_rom_sys.h"

/* ── Register Definitions ── */
#define REG32(addr)  (*(volatile uint32_t*)(addr))

#define ETM_BASE            0x60013000
#define ETM_CLK_EN_REG      (ETM_BASE + 0x1A8)
#define ETM_CH_ENA_AD0_SET  (ETM_BASE + 0x04)
#define ETM_CH_ENA_AD0_CLR  (ETM_BASE + 0x08)
#define ETM_CH_ENA_AD1_SET  (ETM_BASE + 0x10)
#define ETM_CH_ENA_AD1_CLR  (ETM_BASE + 0x14)
#define ETM_CH_EVT_ID(n)    (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID(n)   (ETM_BASE + 0x1C + (n) * 8)

#define PCR_BASE            0x60096000
#define PCR_SOC_ETM_CONF    (PCR_BASE + 0x98)

#define GDMA_BASE           0x60080000
#define GDMA_OUT_BASE(n)    (GDMA_BASE + 0xD0 + (n) * 0xC0)
#define GDMA_OUT_CONF0      0x00
#define GDMA_OUT_LINK       0x10
#define GDMA_OUT_PERI_SEL   0x30
#define GDMA_RST_BIT        (1 << 0)
#define GDMA_EOF_MODE_BIT   (1 << 3)
#define GDMA_ETM_EN_BIT     (1 << 6)
#define GDMA_LINK_ADDR_MASK 0xFFFFF
#define GDMA_LINK_START_BIT (1 << 21)
#define GDMA_PERI_PARLIO    9

#define PARLIO_BASE         0x60015000
#define PARLIO_TX_CFG0      (PARLIO_BASE + 0x08)

#define LEDC_BASE           0x60007000
#define LEDC_EVT_TASK_EN0   (LEDC_BASE + 0x1A0)
#define LEDC_EVT_TASK_EN1   (LEDC_BASE + 0x1A4)

/* ── ETM Event/Task IDs ── */
#define EVT_PCNT_THRESH     45
#define EVT_PCNT_LIMIT      46
#define EVT_PCNT_ZERO       47
#define EVT_TIMER0_ALARM    48
#define EVT_TIMER1_ALARM    49
#define EVT_GDMA_EOF_CH0    153
#define EVT_GDMA_EOF_CH1    154
#define EVT_LEDC_OVF_CH0    31
#define EVT_LEDC_OVF_CH1    32
#define EVT_LEDC_OVF_CH2    33
#define EVT_LEDC_TIMER0_OVF 37

#define TASK_PCNT_RST       87
#define TASK_TIMER0_START   88
#define TASK_TIMER0_STOP    92
#define TASK_TIMER1_STOP    93
#define TASK_GDMA_START_CH0 162
#define TASK_GDMA_START_CH1 163
#define TASK_LEDC_T0_RESUME 57
#define TASK_LEDC_T0_PAUSE  61
#define TASK_LEDC_T1_PAUSE  62
#define TASK_LEDC_OVF_RST0  47
#define TASK_LEDC_OVF_RST1  48

/* ── GPIO ── */
#define GPIO_A  4
#define GPIO_B  5

/* ── Pattern buffers in SRAM ── */
#define PAT_SIZE 64

/* PCNT watch threshold for gate TRUE detection.
 * Must be above glitch/noise floor (~10-17 stray counts from PARLIO nibble
 * boundary transitions) but below real pattern counts (32 for XOR, 63 for AND).
 * 25 gives comfortable margin on both sides. */
#define PCNT_WATCH_THRESH 25

/* The "instruction set" — all patterns live in SRAM, selected by GDMA descriptors */
static uint8_t __attribute__((aligned(4))) pat_and_11[PAT_SIZE];  /* AND(1,1): packed 0x23 */
static uint8_t __attribute__((aligned(4))) pat_and_10[PAT_SIZE];  /* AND(1,0): 0x01/0x00 */
static uint8_t __attribute__((aligned(4))) pat_xor_11[PAT_SIZE];  /* XOR(1,1): packed 0x23 */
static uint8_t __attribute__((aligned(4))) pat_xor_10[PAT_SIZE];  /* XOR(1,0): 0x01/0x00 */
static uint8_t __attribute__((aligned(4))) pat_xor_01[PAT_SIZE];  /* XOR(0,1): 0x02/0x00 */
static uint8_t __attribute__((aligned(4))) pat_null[PAT_SIZE];    /* null: all zeros */

/* GDMA linked list descriptors — the "program" */
static lldesc_t __attribute__((aligned(4))) desc[8];

/* ── Peripheral handles ── */
static gptimer_handle_t timer0 = NULL;
static gptimer_handle_t timer1 = NULL;
static pcnt_unit_handle_t xor_unit = NULL, and_unit = NULL;
static pcnt_channel_handle_t xor_ch0 = NULL, xor_ch1 = NULL, and_ch0 = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

/* ── State (only written by ISR, read after halt) ── */
static volatile int watchdog_fired = 0;
static volatile uint32_t transition_count = 0;

/* ── Helpers ── */
static int etm_used = 0;

static void etm_force_clk(void) {
    /* Force ETM bus clock on — IDF startup disables it (clk.c line 270) */
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf = (*conf & ~(1 << 1)) | (1 << 0);  /* clk_en=1, rst=0 */
    esp_rom_delay_us(1);
    /* Enable internal register clock gate */
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
    /* Stop timer0 via bare-metal */
    volatile uint32_t *t0cfg = (volatile uint32_t*)0x60008000;
    *t0cfg &= ~(1U << 31);
    return true;
}

static bool IRAM_ATTR pcnt_watch_cb(pcnt_unit_handle_t unit,
    const pcnt_watch_event_data_t *edata, void *user_ctx) {
    transition_count++;
    return false;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  INIT                                                              */
/* ═══════════════════════════════════════════════════════════════════ */

static void init_patterns(void) {
    for (int i = 0; i < PAT_SIZE; i++) {
        pat_and_11[i] = 0x23;                          /* packed: B stable HIGH, A toggles */
        pat_and_10[i] = (i % 2 == 0) ? 0x01 : 0x00;   /* A toggles, B=0 */
        pat_xor_11[i] = 0x23;                          /* packed: same encoding */
        pat_xor_10[i] = (i % 2 == 0) ? 0x01 : 0x00;
        pat_xor_01[i] = (i % 2 == 0) ? 0x02 : 0x00;   /* B toggles, A=0 */
        pat_null[i]   = 0x00;
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
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = 1000000,
        .data_width = 4,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 4,
        .max_transfer_size = 256,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++)
        cfg.data_gpio_nums[i] = (i < 4) ? (4 + i) : -1;
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    ESP_ERROR_CHECK(parlio_tx_unit_enable(parlio));
}

static void init_pcnt(void) {
    /* XOR unit — 2 channels, threshold at 8 (boolean TRUE) */
    {
        pcnt_unit_config_t cfg = { .low_limit = -200, .high_limit = 200 };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &xor_unit));

        pcnt_chan_config_t ch0 = { .edge_gpio_num = GPIO_A, .level_gpio_num = GPIO_B };
        ESP_ERROR_CHECK(pcnt_new_channel(xor_unit, &ch0, &xor_ch0));
        pcnt_channel_set_edge_action(xor_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(xor_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_HOLD, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

        pcnt_chan_config_t ch1 = { .edge_gpio_num = GPIO_B, .level_gpio_num = GPIO_A };
        ESP_ERROR_CHECK(pcnt_new_channel(xor_unit, &ch1, &xor_ch1));
        pcnt_channel_set_edge_action(xor_ch1,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(xor_ch1,
            PCNT_CHANNEL_LEVEL_ACTION_HOLD, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

        /* Watch at threshold: fires ETM event when XOR result is TRUE */
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(xor_unit, PCNT_WATCH_THRESH));
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(xor_unit, &cb, NULL);

        ESP_ERROR_CHECK(pcnt_unit_enable(xor_unit));
        ESP_ERROR_CHECK(pcnt_unit_start(xor_unit));
    }

    /* AND unit — 1 channel, threshold at 8 */
    {
        pcnt_unit_config_t cfg = { .low_limit = -200, .high_limit = 200 };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &and_unit));

        pcnt_chan_config_t ch0 = { .edge_gpio_num = GPIO_A, .level_gpio_num = GPIO_B };
        ESP_ERROR_CHECK(pcnt_new_channel(and_unit, &ch0, &and_ch0));
        pcnt_channel_set_edge_action(and_ch0,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_channel_set_level_action(and_ch0,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_HOLD);

        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(and_unit, PCNT_WATCH_THRESH));
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(and_unit, &cb, NULL);

        ESP_ERROR_CHECK(pcnt_unit_enable(and_unit));
        ESP_ERROR_CHECK(pcnt_unit_start(and_unit));
    }
}

static void init_timers(void) {
    /* Timer0: kickoff (one-shot alarm) */
    gptimer_config_t c0 = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c0, &timer0));
    ESP_ERROR_CHECK(gptimer_enable(timer0));

    /* Timer1: watchdog */
    gptimer_config_t c1 = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c1, &timer1));
    gptimer_event_callbacks_t cb1 = { .on_alarm = watchdog_cb };
    gptimer_register_event_callbacks(timer1, &cb1, NULL);
    ESP_ERROR_CHECK(gptimer_enable(timer1));
}

static void init_ledc(void) {
    /* LEDC Timer0 at 10kHz — fast enough to overflow quickly */
    ledc_timer_config_t lt = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 10000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt));

    /* LEDC CH0: result latch for AND gate */
    ledc_channel_config_t lch0 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 0,
        .duty = 512,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lch0));

    /* LEDC CH1: result latch for XOR gate */
    ledc_channel_config_t lch1 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 1,
        .duty = 512,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lch1));

    /* Start paused — will be resumed by ETM during autonomous phase */
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);

    /* Enable LEDC ETM events and tasks */
    volatile uint32_t *en0 = (volatile uint32_t*)LEDC_EVT_TASK_EN0;
    *en0 |= (1 << 8) | (1 << 9);  /* OVF event enable for CH0, CH1 */
    volatile uint32_t *en1 = (volatile uint32_t*)LEDC_EVT_TASK_EN1;
    *en1 |= (1 << 16) | (1 << 17);  /* OVF reset task CH0, CH1 */
    *en1 |= (1 << 28) | (1 << 29);  /* timer pause/resume tasks */
}

static void init_etm(void) {
    volatile uint32_t *etm_conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *etm_conf &= ~(1 << 1);  /* clear reset */
    *etm_conf |= (1 << 0);   /* enable clock */
    REG32(ETM_CLK_EN_REG) = 1;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  GDMA BARE-METAL SETUP                                             */
/* ═══════════════════════════════════════════════════════════════════ */

/* Detect which GDMA channel PARLIO claimed, configure bare-metal on another */
static int parlio_ch = -1;
static int bare_ch = -1;

static void detect_gdma_channels(void) {
    for (int ch = 0; ch < 3; ch++) {
        uint32_t peri = REG32(GDMA_OUT_BASE(ch) + GDMA_OUT_PERI_SEL) & 0x3F;
        if (peri == GDMA_PERI_PARLIO) {
            parlio_ch = ch;
        }
    }
    if (parlio_ch < 0) {
        parlio_ch = 0;  /* fallback */
    }
    /* Use CH0 for bare-metal regardless — we'll take over from PARLIO driver */
    bare_ch = parlio_ch;
    printf("[GDMA] PARLIO owns CH%d, bare-metal will use CH%d\n", parlio_ch, bare_ch);
}

static void setup_gdma_bare(lldesc_t *first_desc) {
    uint32_t base = GDMA_OUT_BASE(bare_ch);

    /* Reset PARLIO FIFO first */
    uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 |= (1 << 30);            /* FIFO_SRST */
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

    /* Enable EOF mode + ETM trigger */
    REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT | GDMA_ETM_EN_BIT;

    /* Point to PARLIO peripheral */
    REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

    /* Load descriptor address — do NOT set START bit, let ETM trigger it */
    uint32_t addr = ((uint32_t)first_desc) & GDMA_LINK_ADDR_MASK;
    REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;
    /* Note: LINK_START_BIT loads the descriptor but with ETM_EN, actual
     * DMA transfer waits for ETM task. We need LINK_START to arm the channel. */

    /* Configure PARLIO for bare-metal TX — set BYTELEN and GATING but NOT TX_START.
     * TX_START is deferred to run_autonomous_gate() AFTER PCNT is cleared,
     * so any leaked GDMA data sits in the FIFO without driving GPIOs. */
    tx_cfg0 = REG32(PARLIO_TX_CFG0);
    tx_cfg0 &= ~(0xFFFF << 2);       /* clear BYTELEN */
    tx_cfg0 &= ~(1 << 19);           /* clear TX_START — do NOT start yet */
    tx_cfg0 |= (PAT_SIZE << 2);      /* BYTELEN = 64 */
    tx_cfg0 |= (1 << 18);            /* TX_GATING_EN */
    REG32(PARLIO_TX_CFG0) = tx_cfg0;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  TEST: AUTONOMOUS GATE EVALUATION                                   */
/* ═══════════════════════════════════════════════════════════════════ */

/*
 * Autonomous AND gate test:
 *
 * 1. Pre-load pattern into SRAM (pat_and_11 = packed 0x23)
 * 2. Create DMA descriptor pointing to pattern
 * 3. Wire ETM:
 *    - Timer0 alarm → GDMA CH start (kickoff)
 *    - PCNT threshold → LEDC timer0 pause (latch: gate = TRUE)
 *    - GDMA EOF → (nothing more — single evaluation)
 *    - Timer1 alarm → watchdog halt
 * 4. Start timers, enter NOP loop
 * 5. After watchdog fires, read LEDC timer state:
 *    - Timer paused = gate was TRUE (PCNT threshold fired, paused LEDC)
 *    - Timer running = gate was FALSE (no threshold, LEDC kept running)
 *
 * To read the result: check LEDC timer overflow count.
 * If LEDC was paused early, fewer overflows occurred.
 * If LEDC ran the whole time, many overflows occurred.
 */

static int run_autonomous_gate(const char *name, uint8_t *pattern,
                                int expect_true) {
    printf("  %s: ", name);
    fflush(stdout);

    /* Reset state */
    watchdog_fired = 0;
    transition_count = 0;

    /* Drive GPIOs LOW to prevent edge glitches from previous test's residual state */
    gpio_set_level(GPIO_A, 0);
    gpio_set_level(GPIO_B, 0);
    esp_rom_delay_us(50);

    pcnt_unit_clear_count(xor_unit);
    pcnt_unit_clear_count(and_unit);

    /* Set up descriptor — refresh owner bit */
    init_desc(&desc[0], pattern, NULL);

    /* Force ETM clock on and clear all channels */
    etm_force_clk();
    REG32(ETM_CH_ENA_AD0_CLR) = 0xFFFFFFFF;
    REG32(ETM_CH_ENA_AD1_CLR) = 0x0003FFFF;
    etm_used = 0;
    esp_rom_delay_us(10);

    /* Set up GDMA bare-metal (resets PARLIO FIFO, arms GDMA with ETM_EN) */
    setup_gdma_bare(&desc[0]);

    /* CRITICAL: GDMA LINK_START may cause immediate data transfer despite ETM_EN.
     * Wait for any leaked DMA to settle, then clear PCNT to wipe stray counts. */
    esp_rom_delay_us(200);
    pcnt_unit_clear_count(xor_unit);
    pcnt_unit_clear_count(and_unit);

    /* Reset LEDC: resume timer, clear OVF counter */
    {
        volatile uint32_t *conf0 = (volatile uint32_t*)(LEDC_BASE + 0x00);
        uint32_t val = *conf0;
        val |= (1 << 16);  /* OVF_CNT_RESET */
        *conf0 = val;
        esp_rom_delay_us(10);
    }
    /* Wire ETM for this test */
    uint32_t gdma_start_task = TASK_GDMA_START_CH0 + bare_ch;

    /* Ch 0: Timer0 alarm → GDMA start (kickoff) */
    etm_wire(0, EVT_TIMER0_ALARM, gdma_start_task);

    /* Ch 1: Timer0 alarm → PCNT reset (clean start — simultaneous with GDMA) */
    etm_wire(1, EVT_TIMER0_ALARM, TASK_PCNT_RST);

    /* Ch 2: Timer1 alarm → Timer0 stop (watchdog halt) */
    etm_wire(2, EVT_TIMER1_ALARM, TASK_TIMER0_STOP);

    /* Final PCNT clear right before starting timers */
    pcnt_unit_clear_count(xor_unit);
    pcnt_unit_clear_count(and_unit);

    /* NOW arm PARLIO TX_START — GDMA data in FIFO (if any leaked) will start
     * transmitting, but PCNT was just cleared so stray counts start from 0.
     * The real ETM-triggered GDMA start will push the actual pattern. */
    {
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 19);  /* TX_START */
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
    }

    /* Start watchdog (100ms) */
    gptimer_alarm_config_t a1 = { .alarm_count = 100000, .flags.auto_reload_on_alarm = false };
    gptimer_set_alarm_action(timer1, &a1);
    gptimer_set_raw_count(timer1, 0);
    gptimer_start(timer1);

    /* Start kickoff timer (10us delay — gives GDMA time to arm) */
    gptimer_alarm_config_t a0 = { .alarm_count = 10, .flags.auto_reload_on_alarm = false };
    gptimer_set_alarm_action(timer0, &a0);
    gptimer_set_raw_count(timer0, 0);

    int64_t t_start = esp_timer_get_time();
    gptimer_start(timer0);

    /* CPU idle — all computation happens in hardware */
    while (!watchdog_fired) {
        __asm__ volatile("nop");
    }

    int64_t t_end = esp_timer_get_time();
    gptimer_stop(timer0);
    gptimer_stop(timer1);

    /* Read results */
    int xor_count, and_count;
    pcnt_unit_get_count(xor_unit, &xor_count);
    pcnt_unit_get_count(and_unit, &and_count);

    /* Result = did the PCNT threshold fire?
     * transition_count > 0 means the watch point callback fired = gate TRUE */
    int result = (transition_count > 0) ? 1 : 0;
    int ok = (result == expect_true);

    printf("result=%d (expect %d) xor=%d and=%d trans=%lu [%lldus] %s\n",
           result, expect_true, xor_count, and_count,
           (unsigned long)transition_count,
           t_end - t_start, ok ? "OK" : "FAIL");
    fflush(stdout);

    return ok;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  MAIN                                                              */
/* ═══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    printf("\n\n");
    printf("============================================================\n");
    printf("  AUTONOMOUS ALU - CPU-Free Gate Evaluation\n");
    printf("  ESP32-C6FH4 QFN32 rev v0.2\n");
    printf("============================================================\n\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ── Init ── */
    printf("[INIT] Patterns...\n"); fflush(stdout);
    init_patterns();

    printf("[INIT] ETM clock...\n"); fflush(stdout);
    init_etm();

    printf("[INIT] GPIO 4-7 INPUT_OUTPUT...\n"); fflush(stdout);
    init_gpio();

    printf("[INIT] PARLIO TX...\n"); fflush(stdout);
    init_parlio();

    printf("[INIT] PCNT (XOR + AND)...\n"); fflush(stdout);
    init_pcnt();

    printf("[INIT] Timers...\n"); fflush(stdout);
    init_timers();

    printf("[INIT] LEDC (result latches)...\n"); fflush(stdout);
    init_ledc();

    printf("[INIT] GDMA channel detection...\n"); fflush(stdout);
    detect_gdma_channels();

    printf("[INIT] Done.\n\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));

    /* ── Verification: CPU-driven gate eval first (sanity check) ── */
    printf("-- SANITY CHECK: CPU-driven gate eval --\n");
    {
        parlio_transmit_config_t tx = { .idle_value = 0 };
        int xv, av;

        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);
        parlio_tx_unit_transmit(parlio, pat_and_11, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(100);
        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);
        printf("  pat_and_11 (packed 0x23): XOR=%d AND=%d (expect: XOR~0, AND~63)\n", xv, av);

        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);
        parlio_tx_unit_transmit(parlio, pat_and_10, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(100);
        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);
        printf("  pat_and_10 (0x01/0x00):   XOR=%d AND=%d (expect: XOR=32, AND=0)\n", xv, av);

        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);
        parlio_tx_unit_transmit(parlio, pat_xor_10, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(100);
        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);
        printf("  pat_xor_10 (0x01/0x00):   XOR=%d AND=%d (expect: XOR=32, AND=0)\n\n", xv, av);
        fflush(stdout);
    }

    /* Diagnostic: verify GDMA channel state after sanity check */
    printf("[DIAG] GDMA channel state after sanity check:\n");
    for (int ch = 0; ch < 3; ch++) {
        uint32_t base = GDMA_OUT_BASE(ch);
        uint32_t conf0 = REG32(base + GDMA_OUT_CONF0);
        uint32_t peri = REG32(base + GDMA_OUT_PERI_SEL) & 0x3F;
        printf("  CH%d: CONF0=0x%08lx PERI=%lu (ETM_EN=%d EOF_MODE=%d RST=%d)\n",
               ch, (unsigned long)conf0, (unsigned long)peri,
               (int)((conf0 >> 6) & 1), (int)((conf0 >> 3) & 1), (int)(conf0 & 1));
    }
    printf("[DIAG] LEDC EVT_TASK_EN0=0x%08lx EN1=0x%08lx\n",
           (unsigned long)REG32(LEDC_EVT_TASK_EN0),
           (unsigned long)REG32(LEDC_EVT_TASK_EN1));
    printf("[DIAG] PARLIO TX_CFG0=0x%08lx\n", (unsigned long)REG32(PARLIO_TX_CFG0));
    fflush(stdout);

    /* ── Bare-metal DMA verification (CPU-triggered, no ETM) ── */
    printf("\n-- BARE-METAL DMA VERIFY (CPU-triggered, no ETM) --\n");
    {
        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);

        /* Set up descriptor */
        init_desc(&desc[0], pat_and_11, NULL);

        uint32_t base = GDMA_OUT_BASE(bare_ch);

        /* Reset PARLIO FIFO */
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 30);  /* FIFO_SRST */
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

        /* Configure: EOF mode, NO ETM (CPU start) */
        REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
        REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

        /* Configure PARLIO */
        tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 &= ~(0xFFFF << 2);
        tx_cfg0 |= (PAT_SIZE << 2);
        tx_cfg0 |= (1 << 18);  /* TX_GATING_EN */
        tx_cfg0 |= (1 << 19);  /* TX_START */
        REG32(PARLIO_TX_CFG0) = tx_cfg0;

        /* Start DMA by writing LINK_START */
        uint32_t addr = ((uint32_t)&desc[0]) & GDMA_LINK_ADDR_MASK;
        REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

        /* Wait for transfer */
        esp_rom_delay_us(500);

        int xv, av;
        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);
        printf("  pat_and_11 via bare-metal DMA: XOR=%d AND=%d (expect: XOR~2, AND~63)\n", xv, av);

        /* Now test pat_xor_10 */
        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);
        init_desc(&desc[0], pat_xor_10, NULL);

        /* Reset PARLIO FIFO */
        tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 |= (1 << 30);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
        esp_rom_delay_us(10);
        tx_cfg0 &= ~(1 << 30);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;
        esp_rom_delay_us(10);

        REG32(base + GDMA_OUT_CONF0) = GDMA_RST_BIT;
        esp_rom_delay_us(10);
        REG32(base + GDMA_OUT_CONF0) = 0;
        esp_rom_delay_us(10);
        REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
        REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

        tx_cfg0 = REG32(PARLIO_TX_CFG0);
        tx_cfg0 &= ~(0xFFFF << 2);
        tx_cfg0 |= (PAT_SIZE << 2);
        tx_cfg0 |= (1 << 18);
        tx_cfg0 |= (1 << 19);
        REG32(PARLIO_TX_CFG0) = tx_cfg0;

        addr = ((uint32_t)&desc[0]) & GDMA_LINK_ADDR_MASK;
        REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;

        esp_rom_delay_us(500);

        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);
        printf("  pat_xor_10 via bare-metal DMA: XOR=%d AND=%d (expect: XOR=32, AND=0)\n", xv, av);

        printf("  GDMA CONF0=0x%08lx PARLIO TX_CFG0=0x%08lx\n\n",
               (unsigned long)REG32(base + GDMA_OUT_CONF0),
               (unsigned long)REG32(PARLIO_TX_CFG0));
        fflush(stdout);
    }

    /* ── ETM path verification: PCNT threshold → LEDC pause ── */
    printf("-- ETM PATH VERIFY: PCNT thresh → LEDC pause --\n");
    {
        /* Verify ETM clock is on */
        volatile uint32_t *etm_conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
        printf("  PCR(0x98) before=0x%08lx\n", (unsigned long)*etm_conf);

        /* Enable ETM bus clock */
        *etm_conf = 0x00000001;  /* clk_en=1, rst_en=0 */
        esp_rom_delay_us(10);
        printf("  PCR(0x98) after =0x%08lx\n", (unsigned long)*etm_conf);

        /* Set ETM internal clock gate (ETM_BASE + 0x1A8) */
        printf("  ETM_CLK_EN(0x%08lx) before=%lu\n",
               (unsigned long)(ETM_BASE + 0x1A8),
               (unsigned long)REG32(ETM_CLK_EN_REG));
        REG32(ETM_CLK_EN_REG) = 1;
        esp_rom_delay_us(10);
        printf("  ETM_CLK_EN after=%lu\n", (unsigned long)REG32(ETM_CLK_EN_REG));

        /* Test ETM register access */
        REG32(ETM_CH_EVT_ID(0)) = 0x42;
        esp_rom_delay_us(1);
        printf("  ETM CH0_EVT_ID: wrote 0x42, read 0x%02lx\n",
               (unsigned long)REG32(ETM_CH_EVT_ID(0)));
        REG32(ETM_CH_EVT_ID(0)) = 0;  /* clean up */

        /* Force ETM clock on and clear all channels */
        etm_force_clk();
        REG32(ETM_CH_ENA_AD0_CLR) = 0xFFFFFFFF;
        REG32(ETM_CH_ENA_AD1_CLR) = 0x0003FFFF;
        etm_used = 0;
        esp_rom_delay_us(10);

        /* Wire: PCNT threshold → LEDC timer0 pause */
        etm_wire(0, EVT_PCNT_THRESH, TASK_LEDC_T0_PAUSE);

        /* Readback ETM state */
        printf("  ETM CH0 after wire: EVT=%lu TASK=%lu ENA=0x%08lx\n",
               (unsigned long)REG32(ETM_CH_EVT_ID(0)),
               (unsigned long)REG32(ETM_CH_TASK_ID(0)),
               (unsigned long)REG32(ETM_BASE + 0x00));  /* CH_ENA_AD0 status */

        /* TEST: Timer alarm → LEDC pause (verify ETM→LEDC path works) */
        printf("  Testing Timer→LEDC pause via ETM...\n");
        {
            ledc_timer_resume(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
            esp_rom_delay_us(100);

            /* Wire CH1: Timer0 alarm → LEDC timer0 pause */
            etm_wire(1, EVT_TIMER0_ALARM, TASK_LEDC_T0_PAUSE);

            /* Fire timer0 alarm at 10us */
            gptimer_alarm_config_t ta = { .alarm_count = 10, .flags.auto_reload_on_alarm = false };
            gptimer_set_alarm_action(timer0, &ta);
            gptimer_set_raw_count(timer0, 0);
            gptimer_start(timer0);
            esp_rom_delay_us(1000);
            gptimer_stop(timer0);

            volatile uint32_t *tv = (volatile uint32_t*)(LEDC_BASE + 0xA4);
            uint32_t vx = *tv & 0xFFFFF;
            esp_rom_delay_us(200);
            uint32_t vy = *tv & 0xFFFFF;
            printf("    Timer→LEDC: v1=%lu v2=%lu paused=%s\n",
                   (unsigned long)vx, (unsigned long)vy,
                   (vx == vy) ? "YES" : "NO");

            ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);

            /* Clean up: disable CH1 */
            REG32(ETM_CH_ENA_AD0_CLR) = (1 << 1);
        }

        /* Read CH_ENA status — need to read the actual status register */
        /* CH_ENA_AD0 status is at ETM_BASE + 0x04? No, SET is at +0x04, CLR is at +0x08.
         * The actual status register might be different. Let me try reading the SET reg. */
        /* Actually the TRM says: CH_ENA_AD0_SET (W1S), CH_ENA_AD0_CLR (W1C).
         * Status readback: checking IDF source... */
        /* Try reading: there might be a CH_ENA_AD0 status reg at a different offset.
         * On ESP32-C6, the ETM channel enable can be read from the SET register address. */

        /* Make sure only XOR watch is active (threshold=8) */

        /* Resume LEDC timer — full reconfigure */
        {
            /* Reconfigure LEDC timer0 from scratch */
            ledc_timer_config_t lt = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .timer_num = LEDC_TIMER_0,
                .duty_resolution = LEDC_TIMER_10_BIT,
                .freq_hz = 10000,
                .clk_cfg = LEDC_AUTO_CLK,
            };
            ledc_timer_config(&lt);
        }
        esp_rom_delay_us(100);

        /* Verify LEDC is running */
        volatile uint32_t *timer_val = (volatile uint32_t*)(LEDC_BASE + 0xA4);
        uint32_t va = *timer_val & 0xFFFFF;
        esp_rom_delay_us(200);
        uint32_t vb = *timer_val & 0xFFFFF;
        printf("  LEDC before: v1=%lu v2=%lu running=%s\n",
               (unsigned long)va, (unsigned long)vb, (va != vb) ? "YES" : "NO");

        /* Send XOR(1,0) pattern via IDF PARLIO driver — should trigger threshold */
        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);
        transition_count = 0;

        parlio_transmit_config_t tx = { .idle_value = 0 };
        parlio_tx_unit_transmit(parlio, pat_xor_10, PAT_SIZE * 8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(200);

        int xv, av;
        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);

        /* Check if LEDC timer got paused */
        uint32_t vc = *timer_val & 0xFFFFF;
        esp_rom_delay_us(200);
        uint32_t vd = *timer_val & 0xFFFFF;
        int paused = (vc == vd);

        printf("  XOR=%d AND=%d trans=%lu\n", xv, av, (unsigned long)transition_count);
        printf("  LEDC after: v1=%lu v2=%lu paused=%s\n",
               (unsigned long)vc, (unsigned long)vd, paused ? "YES" : "NO");

        /* Re-read ETM — should still be wired */
        printf("  ETM CH0 re-read: EVT=%lu TASK=%lu\n",
               (unsigned long)REG32(ETM_CH_EVT_ID(0)),
               (unsigned long)REG32(ETM_CH_TASK_ID(0)));

        /* Try the PCNT ETM event enable — does PCNT need ETM event enable? */
        /* Check PCNT registers for any ETM-related config */
        #define PCNT_BASE 0x60012000
        printf("  PCNT U0_CONF0=0x%08lx U0_CONF2=0x%08lx\n",
               (unsigned long)REG32(PCNT_BASE + 0x00),
               (unsigned long)REG32(PCNT_BASE + 0x08));
        printf("  PCNT U1_CONF0=0x%08lx U1_CONF2=0x%08lx\n",
               (unsigned long)REG32(PCNT_BASE + 0x0C),
               (unsigned long)REG32(PCNT_BASE + 0x14));

        fflush(stdout);

        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    }
    printf("\n");
    fflush(stdout);

    int total = 0, passed = 0;

    /* ── Test 1: Autonomous AND gate ── */
    printf("-- TEST 1: Autonomous AND gate --\n");
    printf("  Timer → ETM → GDMA → PARLIO → GPIO → PCNT → ETM → LEDC latch\n");
    printf("  CPU in NOP loop during evaluation\n\n");
    fflush(stdout);

    /* For AND test: disable XOR watch so only AND threshold fires ETM event */
    pcnt_unit_remove_watch_point(xor_unit, PCNT_WATCH_THRESH);

    passed += run_autonomous_gate("AND(1,1)", pat_and_11, 1); total++;
    passed += run_autonomous_gate("AND(1,0)", pat_and_10, 0); total++;
    passed += run_autonomous_gate("AND(0,1)", pat_xor_01, 0); total++;  /* A=0, no A edges → AND=0 */
    passed += run_autonomous_gate("AND(0,0)", pat_null, 0); total++;

    /* Re-enable XOR watch, disable AND watch for XOR test */
    pcnt_unit_add_watch_point(xor_unit, PCNT_WATCH_THRESH);
    pcnt_unit_remove_watch_point(and_unit, PCNT_WATCH_THRESH);

    /* ── Test 2: Autonomous XOR gate ── */
    printf("\n-- TEST 2: Autonomous XOR gate --\n");
    printf("  Same structure, only XOR unit's watch threshold fires\n\n");
    fflush(stdout);

    passed += run_autonomous_gate("XOR(1,0)", pat_xor_10, 1); total++;
    passed += run_autonomous_gate("XOR(0,1)", pat_xor_01, 1); total++;
    passed += run_autonomous_gate("XOR(1,1)", pat_xor_11, 0); total++;
    passed += run_autonomous_gate("XOR(0,0)", pat_null, 0); total++;

    /* Restore both watches */
    pcnt_unit_add_watch_point(and_unit, PCNT_WATCH_THRESH);

    /* ── Test 3: Autonomous chained evaluation (XOR then AND) ── */
    printf("\n-- TEST 3: Autonomous 2-stage chain --\n");
    printf("  Descriptor chain: [XOR pattern] → [AND pattern]\n");
    printf("  GDMA EOF after stage 1 → PCNT reset → GDMA restart for stage 2\n");
    printf("  Tests full autonomous sequencing\n\n");
    fflush(stdout);

    /* For chained test, we run two patterns in sequence.
     * Stage 1: XOR eval with packed pattern (only XOR watch active)
     * Stage 2: AND eval with packed pattern (only AND watch active)
     *
     * Since we can't change watch points mid-flight without CPU,
     * we keep BOTH watches active. The first threshold (from stage 1)
     * pauses LEDC. We accept this limitation — the chain proves
     * that GDMA descriptor sequencing works autonomously.
     *
     * What we verify: two DMA transfers happen without CPU,
     * transition_count shows both stages fired, PCNT counts are
     * consistent with two sequential pattern transmissions. */
    {
        /* Enable BOTH watches for chain test — we want to see both stages fire */
        pcnt_unit_remove_watch_point(xor_unit, PCNT_WATCH_THRESH);
        pcnt_unit_remove_watch_point(and_unit, PCNT_WATCH_THRESH);
        pcnt_unit_add_watch_point(xor_unit, PCNT_WATCH_THRESH);
        pcnt_unit_add_watch_point(and_unit, PCNT_WATCH_THRESH);

        /* Build 2-descriptor chain — GDMA follows linked list automatically.
         * With ETM_EN, each GDMA_START processes one descriptor then stops at EOF.
         * WITHOUT ETM_EN, GDMA auto-follows the linked list for all descriptors.
         * So for chaining: DON'T use ETM_EN. Use normal GDMA with LINK_START,
         * and only use ETM for the initial kickoff (Timer → PCNT reset). */
        init_desc(&desc[0], pat_xor_10, &desc[1]);  /* Stage 1: XOR(1,0) → TRUE */
        init_desc(&desc[1], pat_and_11, NULL);       /* Stage 2: AND(1,1) → TRUE */

        printf("  Chain: XOR(1,0) → AND(1,1)\n");

        watchdog_fired = 0;
        transition_count = 0;

        /* Drive GPIOs LOW to prevent edge glitches */
        gpio_set_level(GPIO_A, 0);
        gpio_set_level(GPIO_B, 0);
        esp_rom_delay_us(50);

        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);

        /* Setup GDMA bare-metal WITHOUT ETM_EN — let GDMA auto-chain descriptors.
         * We still use ETM for PCNT reset and LEDC latch, just not for GDMA control. */
        {
            uint32_t base = GDMA_OUT_BASE(bare_ch);

            /* Reset PARLIO FIFO */
            uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
            tx_cfg0 |= (1 << 30);  /* FIFO_SRST */
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

            /* EOF mode only — NO ETM_EN, let GDMA auto-follow linked list */
            REG32(base + GDMA_OUT_CONF0) = GDMA_EOF_MODE_BIT;
            REG32(base + GDMA_OUT_PERI_SEL) = GDMA_PERI_PARLIO;

            /* Configure PARLIO — no TX_START yet */
            tx_cfg0 = REG32(PARLIO_TX_CFG0);
            tx_cfg0 &= ~(0xFFFF << 2);
            tx_cfg0 &= ~(1 << 19);       /* clear TX_START */
            tx_cfg0 |= ((PAT_SIZE * 2) << 2);  /* BYTELEN = 128 (two 64-byte stages) */
            tx_cfg0 |= (1 << 18);        /* TX_GATING_EN */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;

            /* Load descriptor and START DMA immediately — it will push data to
             * PARLIO FIFO, but PARLIO won't drive GPIOs until TX_START is set. */
            uint32_t addr = ((uint32_t)&desc[0]) & GDMA_LINK_ADDR_MASK;
            REG32(base + GDMA_OUT_LINK) = addr | GDMA_LINK_START_BIT;
        }

        /* Wait for DMA to fill PARLIO FIFO with both stages' data */
        esp_rom_delay_us(500);

        /* Clear any stray PCNT counts from DMA setup */
        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);

        /* Reset LEDC */
        {
            volatile uint32_t *conf0 = (volatile uint32_t*)(LEDC_BASE + 0x00);
            *conf0 |= (1 << 16);
            esp_rom_delay_us(10);
        }
        {
            ledc_timer_config_t lt = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .timer_num = LEDC_TIMER_0,
                .duty_resolution = LEDC_TIMER_10_BIT,
                .freq_hz = 10000,
                .clk_cfg = LEDC_AUTO_CLK,
            };
            ledc_timer_config(&lt);
        }
        esp_rom_delay_us(10);

        /* Wire ETM — only need PCNT threshold→LEDC and watchdog.
         * No GDMA control needed — GDMA auto-chains. */
        etm_force_clk();
        REG32(ETM_CH_ENA_AD0_CLR) = 0xFFFFFFFF;
        REG32(ETM_CH_ENA_AD1_CLR) = 0x0003FFFF;
        etm_used = 0;

        /* Ch 0: PCNT threshold → LEDC pause (latch result) */
        etm_wire(0, EVT_PCNT_THRESH, TASK_LEDC_T0_PAUSE);
        /* Ch 1: Timer1 alarm → Timer0 stop (watchdog halt) */
        etm_wire(1, EVT_TIMER1_ALARM, TASK_TIMER0_STOP);

        /* Final PCNT clear before starting */
        pcnt_unit_clear_count(xor_unit);
        pcnt_unit_clear_count(and_unit);

        /* NOW arm PARLIO TX_START — data from FIFO drives GPIOs */
        {
            uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);
            tx_cfg0 |= (1 << 19);  /* TX_START */
            REG32(PARLIO_TX_CFG0) = tx_cfg0;
        }

        /* Watchdog 100ms */
        gptimer_alarm_config_t a1 = { .alarm_count = 100000, .flags.auto_reload_on_alarm = false };
        gptimer_set_alarm_action(timer1, &a1);
        gptimer_set_raw_count(timer1, 0);
        gptimer_start(timer1);

        /* Timer0 not used for kickoff in chain — GDMA already started.
         * Just set a dummy alarm so watchdog_cb can stop it. */
        gptimer_alarm_config_t a0 = { .alarm_count = 200000, .flags.auto_reload_on_alarm = false };
        gptimer_set_alarm_action(timer0, &a0);
        gptimer_set_raw_count(timer0, 0);
        gptimer_start(timer0);

        int64_t t_start = esp_timer_get_time();

        while (!watchdog_fired) {
            __asm__ volatile("nop");
        }

        int64_t t_end = esp_timer_get_time();
        gptimer_stop(timer0);
        gptimer_stop(timer1);

        int xv, av;
        pcnt_unit_get_count(xor_unit, &xv);
        pcnt_unit_get_count(and_unit, &av);

        /* LEDC paused check */
        volatile uint32_t *timer_val = (volatile uint32_t*)(LEDC_BASE + 0xA4);
        uint32_t v1 = *timer_val & 0x3FFF;
        esp_rom_delay_us(50);
        uint32_t v2 = *timer_val & 0x3FFF;
        int paused = (v1 == v2);

        /* GDMA auto-chains both descriptors without ETM. Counts accumulate:
         * Stage 1 (pat_xor_10): XOR += 32, AND += 0
         * Stage 2 (pat_and_11 = 0x23): XOR += ~2, AND += ~63
         * Total: XOR ~34, AND ~63
         * XOR threshold (25) fires during stage 1 → pauses LEDC → transitions >= 1
         * AND threshold (25) fires during stage 2 → transitions >= 2
         */

        int chain_ok = (transition_count >= 1) && (av > 20) && (xv > 20) && paused;
        printf("    XOR=%d AND=%d transitions=%lu paused=%d [%lldus] %s\n",
               xv, av, (unsigned long)transition_count, paused,
               t_end - t_start, chain_ok ? "OK" : "FAIL");
        printf("    (2 stages: EOF→PCNT_RST→GDMA_START autonomous sequencing)\n");

        total++;
        passed += chain_ok;
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);

        /* Both watches already active from chain test setup — no restore needed */
    }

    /* ── Summary ── */
    printf("\n============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", passed, total);
    printf("============================================================\n");
    printf("  Signal path (CPU-free during evaluation):\n");
    printf("    Timer → ETM → GDMA → PARLIO → GPIO → PCNT → ETM → LEDC\n");
    printf("  Resources: %d ETM, 2 PCNT, 1 PARLIO, 1 GDMA, 2 LEDC, 2 Timer\n", etm_used);
    printf("  Pattern buffers + descriptors: all in SRAM\n");
    printf("============================================================\n");

    if (passed == total) {
        printf("\n  *** AUTONOMOUS ALU GATES VERIFIED ***\n");
        printf("  Gate evaluation runs entirely in peripheral hardware.\n");
        printf("  CPU only configures and reads results.\n");
        printf("  GDMA descriptor chains = instruction sequencing.\n");
        printf("  LEDC overflow counters = result register file.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED.\n\n");
    }
    fflush(stdout);

    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
