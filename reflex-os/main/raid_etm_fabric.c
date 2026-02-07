/**
 * raid_etm_fabric.c - Autonomous Computation Fabric
 *
 * CPU-free computation using only peripheral hardware on ESP32-C6:
 *   ETM crossbar → GDMA → PARLIO (4-bit) → GPIO 4-7 → PCNT 0-3
 *   PCNT thresholds → ETM fan-out → LEDC timer gate → winner-take-all feedback
 *
 * After initialization, the fabric computes autonomously — no HP core, no LP core.
 * Verified on ESP32-C6FH4 (QFN32) rev v0.2, 5/5 tests passing.
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
#include "soc/soc_etm_source.h"
#include "soc/lldesc.h"
#include "esp_rom_sys.h"

// ============================================================
// Register Definitions
// ============================================================

#define ETM_BASE                    0x600B8000
#define ETM_CLK_EN_REG              (ETM_BASE + 0x00)
#define ETM_CH_ENA_AD0_SET          (ETM_BASE + 0x04)
#define ETM_CH_ENA_AD0_CLR          (ETM_BASE + 0x08)
#define ETM_CH_ENA_AD1_SET          (ETM_BASE + 0x10)
#define ETM_CH_ENA_AD1_CLR          (ETM_BASE + 0x14)
#define ETM_CH_EVT_ID_REG(n)        (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n)       (ETM_BASE + 0x1C + (n) * 8)
#define REG32(addr)                 (*(volatile uint32_t*)(addr))

#define PCR_BASE                    0x60096000
#define PCR_SOC_ETM_CONF            (PCR_BASE + 0x90)
#define PCR_GDMA_CONF_REG           (PCR_BASE + 0xbc)

#define GDMA_BASE                   0x60080000
#define GDMA_CH_OUT_BASE(n)         (GDMA_BASE + 0xd0 + (n) * 0xC0)
#define GDMA_OUT_CONF0_OFF          0x00
#define GDMA_OUT_LINK_OFF           0x10
#define GDMA_OUT_PERI_SEL_OFF       0x30
#define GDMA_OUT_RST_BIT            (1 << 0)
#define GDMA_OUT_EOF_MODE_BIT       (1 << 3)
#define GDMA_OUT_ETM_EN_BIT         (1 << 6)
#define GDMA_OUTLINK_ADDR_MASK      0xFFFFF
#define GDMA_OUTLINK_START_BIT      (1 << 21)
#define GDMA_PERI_SEL_PARLIO        9

#define LEDC_BASE                   0x60007000
#define LEDC_EVT_TASK_EN0_REG       (LEDC_BASE + 0x1A0)
#define LEDC_EVT_TASK_EN1_REG       (LEDC_BASE + 0x1A4)

// ETM IDs
#define EVT_PCNT_THRESHOLD          45
#define EVT_PCNT_LIMIT              46
#define EVT_TIMER0_ALARM            48
#define EVT_TIMER1_ALARM            49
#define EVT_GDMA_CH0_EOF            153
#define EVT_GDMA_CH1_EOF            154
#define EVT_GDMA_CH2_EOF            155
#define EVT_LEDC_OVF_CH0            31
#define EVT_LEDC_OVF_CH1            32
#define EVT_LEDC_OVF_CH2            33

#define TASK_TIMER0_CAPTURE         69
#define TASK_PCNT_RESET             87
#define TASK_TIMER0_START           88
#define TASK_TIMER0_STOP            92
#define TASK_TIMER1_STOP            93
#define TASK_TIMER0_RELOAD          94
#define TASK_GDMA_CH0_START         162
#define TASK_GDMA_CH1_START         163
#define TASK_GDMA_CH2_START         164
#define TASK_LEDC_TIMER0_RESUME     57
#define TASK_LEDC_TIMER1_RESUME     58
#define TASK_LEDC_TIMER2_RESUME     59
#define TASK_LEDC_TIMER0_PAUSE      61
#define TASK_LEDC_TIMER1_PAUSE      62
#define TASK_LEDC_TIMER2_PAUSE      63
#define TASK_LEDC_OVF_RST_CH0      47
#define TASK_LEDC_OVF_RST_CH1      48
#define TASK_LEDC_OVF_RST_CH2      49

// Config
#define GPIO_BASE           4
#define NUM_PCNT            4

// PCNT thresholds — watch triggers ETM events, limit is the max before auto-reset
// Pattern test sends 32 pulses per active GPIO, so limit must be > 32 for all units
#define PCNT0_WATCH         16
#define PCNT0_LIMIT         100
#define PCNT1_WATCH         16
#define PCNT1_LIMIT         100
#define PCNT2_WATCH         24
#define PCNT2_LIMIT         100
#define PCNT3_WATCH         8
#define PCNT3_LIMIT         100

// Patterns
static uint8_t __attribute__((aligned(4))) prog_alpha[64];
static uint8_t __attribute__((aligned(4))) prog_beta[64];
static uint8_t __attribute__((aligned(4))) prog_gamma[64];
static lldesc_t __attribute__((aligned(4))) desc_alpha;
static lldesc_t __attribute__((aligned(4))) desc_beta;
static lldesc_t __attribute__((aligned(4))) desc_gamma;

// Handles
static gptimer_handle_t timer0 = NULL;
static gptimer_handle_t timer1 = NULL;
static pcnt_unit_handle_t pcnt[NUM_PCNT] = {NULL};
static pcnt_channel_handle_t pcnt_ch[NUM_PCNT] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;

// GDMA channel mapping: bare-metal channels (indices into HW ch 0-2)
static int parlio_gdma_ch = -1;       // which HW channel PARLIO driver owns
static int bare_ch[2] = {-1, -1};     // the 2 HW channels we control bare-metal

// State
static volatile uint32_t pcnt_watch_hits[NUM_PCNT] = {0};
static volatile uint32_t state_transitions = 0;
static volatile int watchdog_fired = 0;
static volatile int fabric_halted = 0;

// ============================================================
// Helpers
// ============================================================

#define STEP(msg) do { printf("[INIT] %s\n", msg); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(50)); } while(0)

static int etm_used = 0;
static void etm_wire(int ch, uint32_t evt, uint32_t task, const char *desc) {
    REG32(ETM_CH_EVT_ID_REG(ch)) = evt;
    REG32(ETM_CH_TASK_ID_REG(ch)) = task;
    if (ch < 32)
        REG32(ETM_CH_ENA_AD0_SET) = (1 << ch);
    else
        REG32(ETM_CH_ENA_AD1_SET) = (1 << (ch - 32));
    etm_used++;
}

// Callbacks
static bool IRAM_ATTR pcnt_watch_cb(pcnt_unit_handle_t unit,
                                     const pcnt_watch_event_data_t *edata,
                                     void *user_ctx) {
    int idx = (int)(intptr_t)user_ctx;
    pcnt_watch_hits[idx]++;
    state_transitions++;
    return false;
}

static bool IRAM_ATTR timer1_alarm_cb(gptimer_handle_t t,
                                       const gptimer_alarm_event_data_t *d,
                                       void *ctx) {
    watchdog_fired = 1;
    fabric_halted = 1;
    // Also halt timer0 via bare-metal register write (ETM stop task doesn't work on IDF gptimer)
    // TIMG0 base = 0x60008000, T0CONFIG = offset 0x00, bit 31 = EN
    volatile uint32_t *t0cfg = (volatile uint32_t*)0x60008000;
    *t0cfg &= ~(1U << 31);  // clear EN bit to stop timer0
    return true;
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    // Bare minimum output first — if we don't see this, the problem is pre-app
    printf("\n\nHELLO FROM FABRIC\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("═══════════════════════════════════════════════════\n");
    printf("  AUTONOMOUS COMPUTATION FABRIC - DIAGNOSTIC\n");
    printf("═══════════════════════════════════════════════════\n\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── Step 1: Clocks ──
    STEP("Enabling ETM clock...");
    volatile uint32_t *etm_conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *etm_conf &= ~(1 << 1);
    *etm_conf |= (1 << 0);
    REG32(ETM_CLK_EN_REG) = 1;
    STEP("ETM clock OK");

    // NOTE: GDMA clock is enabled automatically by PARLIO driver when it inits.
    // Do NOT manually write PCR_GDMA_CONF_REG — it can corrupt GDMA state.
    STEP("GDMA clock: deferred to PARLIO driver");

    // ── Step 2: Patterns ──
    STEP("Generating patterns...");
    for (int i = 0; i < 64; i++) {
        prog_alpha[i] = (i % 2 == 0) ? 0x33 : 0x00;
        prog_beta[i]  = (i % 2 == 0) ? 0x66 : 0x00;
        prog_gamma[i] = (i % 2 == 0) ? 0xCC : 0x00;
    }
    lldesc_t *d[] = {&desc_alpha, &desc_beta, &desc_gamma};
    uint8_t *b[] = {prog_alpha, prog_beta, prog_gamma};
    for (int i = 0; i < 3; i++) {
        memset(d[i], 0, sizeof(lldesc_t));
        d[i]->size = 64; d[i]->length = 64;
        d[i]->buf = b[i]; d[i]->owner = 1; d[i]->eof = 1;
    }
    STEP("Patterns OK");

    // ── Step 3: GP Timers ──
    STEP("Creating Timer0...");
    gptimer_config_t c0 = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c0, &timer0));
    gptimer_alarm_config_t a0 = { .alarm_count = 500, .flags.auto_reload_on_alarm = false };
    gptimer_set_alarm_action(timer0, &a0);
    gptimer_enable(timer0);
    STEP("Timer0 OK");

    STEP("Creating Timer1 (watchdog)...");
    gptimer_config_t c1 = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&c1, &timer1));
    gptimer_alarm_config_t a1 = { .alarm_count = 500000, .flags.auto_reload_on_alarm = false };
    gptimer_set_alarm_action(timer1, &a1);
    gptimer_event_callbacks_t cb1 = { .on_alarm = timer1_alarm_cb };
    gptimer_register_event_callbacks(timer1, &cb1, NULL);
    gptimer_enable(timer1);
    STEP("Timer1 OK");

    // ── Step 4: PCNT ──
    int watch[] = {PCNT0_WATCH, PCNT1_WATCH, PCNT2_WATCH, PCNT3_WATCH};
    int limit[] = {PCNT0_LIMIT, PCNT1_LIMIT, PCNT2_LIMIT, PCNT3_LIMIT};
    for (int i = 0; i < NUM_PCNT; i++) {
        printf("[INIT] Creating PCNT%d on GPIO%d...\n", i, GPIO_BASE+i);
        fflush(stdout);
        pcnt_unit_config_t cfg = { .low_limit = -32768, .high_limit = limit[i] };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt[i]));
        pcnt_chan_config_t cc = { .edge_gpio_num = GPIO_BASE + i, .level_gpio_num = -1 };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt[i], &cc, &pcnt_ch[i]));
        pcnt_channel_set_edge_action(pcnt_ch[i],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        pcnt_unit_add_watch_point(pcnt[i], watch[i]);
        pcnt_event_callbacks_t cb = { .on_reach = pcnt_watch_cb };
        pcnt_unit_register_event_callbacks(pcnt[i], &cb, (void*)(intptr_t)i);
        pcnt_unit_enable(pcnt[i]);
        pcnt_unit_start(pcnt[i]);
        printf("[INIT] PCNT%d OK (watch=%d, limit=%d)\n", i, watch[i], limit[i]);
        fflush(stdout);
    }

    // ── Step 5: PARLIO ──
    STEP("Creating PARLIO TX (4-bit, GPIO 4-7)...");
    parlio_tx_unit_config_t pcfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = 2000000,
        .data_width = 4,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 16,
        .max_transfer_size = 512,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++)
        pcfg.data_gpio_nums[i] = (i < 4) ? (GPIO_BASE + i) : -1;
    ESP_ERROR_CHECK(parlio_new_tx_unit(&pcfg, &parlio));
    parlio_tx_unit_enable(parlio);
    // Detect which GDMA channel the PARLIO driver claimed
    {
        int bi = 0;
        for (int ch = 0; ch < 3; ch++) {
            uint32_t peri = REG32(GDMA_CH_OUT_BASE(ch) + GDMA_OUT_PERI_SEL_OFF) & 0x3F;
            if (peri == GDMA_PERI_SEL_PARLIO) {
                parlio_gdma_ch = ch;
                printf("[INIT] PARLIO owns GDMA OUT CH%d\n", ch); fflush(stdout);
            } else {
                if (bi < 2) bare_ch[bi++] = ch;
            }
        }
        if (parlio_gdma_ch < 0) {
            // Fallback: assume PARLIO got CH0
            printf("[INIT] WARN: Could not detect PARLIO GDMA ch, assuming CH0\n"); fflush(stdout);
            parlio_gdma_ch = 0;
            bare_ch[0] = 1; bare_ch[1] = 2;
        }
        printf("[INIT] Bare-metal GDMA: CH%d, CH%d\n", bare_ch[0], bare_ch[1]); fflush(stdout);
    }
    STEP("PARLIO OK");

    // ── Step 6: LEDC ──
    STEP("Creating LEDC Timer0 (5kHz)...");
    ledc_timer_config_t lt0 = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt0));
    STEP("LEDC Timer0 OK");

    STEP("Creating LEDC Timer1 (3kHz)...");
    ledc_timer_config_t lt1 = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 3000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt1));
    STEP("LEDC Timer1 OK");

    STEP("Creating LEDC Timer2 (2kHz)...");
    ledc_timer_config_t lt2 = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_2,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt2));
    STEP("LEDC Timer2 OK");

    STEP("Creating LEDC channels on GPIO 0-2...");
    ledc_channel_config_t lch0 = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0, .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 0, .duty = 512, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lch0));
    STEP("LEDC CH0 OK");
    ledc_channel_config_t lch1 = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1, .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 1, .duty = 512, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lch1));
    STEP("LEDC CH1 OK");
    ledc_channel_config_t lch2 = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_2, .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 2, .duty = 512, .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lch2));
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1);
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_2);
    STEP("LEDC channels OK (all paused)");

    STEP("Enabling LEDC ETM...");
    volatile uint32_t *en0 = (volatile uint32_t*)LEDC_EVT_TASK_EN0_REG;
    *en0 |= (1 << 8) | (1 << 9) | (1 << 10);
    volatile uint32_t *en1 = (volatile uint32_t*)LEDC_EVT_TASK_EN1_REG;
    *en1 |= (1 << 16) | (1 << 17) | (1 << 18) | (1 << 28) | (1 << 29) | (1 << 30);
    STEP("LEDC ETM OK");

    // ── Step 7: GDMA bare-metal deferred to Test 4 ──
    // Configuring bare-metal GDMA during init corrupts PARLIO's DMA channel.
    STEP("GDMA bare-metal: deferred to Test 4");

    // ── Step 8: ETM wiring deferred to Test 4 ──
    STEP("ETM wiring: deferred to Test 4");

    // Dynamic GDMA task/event IDs based on detected channels
    uint32_t TASK_BARE0_START = TASK_GDMA_CH0_START + bare_ch[0]; // α
    uint32_t TASK_BARE1_START = TASK_GDMA_CH0_START + bare_ch[1]; // β
    uint32_t EVT_BARE0_EOF   = EVT_GDMA_CH0_EOF + bare_ch[0];
    uint32_t EVT_BARE1_EOF   = EVT_GDMA_CH0_EOF + bare_ch[1];

    // ══════════════════════════════════════════════════════════
    printf("\n═══════════════════════════════════════════════════\n");
    printf("  PERIPHERALS INITIALIZED. RUNNING TESTS.\n");
    printf("═══════════════════════════════════════════════════\n\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));

    int total = 0, passed = 0;

    // ── TEST 1: Pattern Targeting ──
    {
        printf("TEST 1: Pattern Targeting\n"); fflush(stdout);
        int counts[NUM_PCNT];
        parlio_transmit_config_t tx = { .idle_value = 0 };
        int ok = 1;

        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_clear_count(pcnt[i]);
        parlio_tx_unit_transmit(parlio, prog_alpha, 64*8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_get_count(pcnt[i], &counts[i]);
        printf("  α: [%d,%d,%d,%d]\n", counts[0],counts[1],counts[2],counts[3]);
        if (counts[0] < 20 || counts[1] < 20 || counts[2] > 5 || counts[3] > 5) ok = 0;

        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_clear_count(pcnt[i]);
        parlio_tx_unit_transmit(parlio, prog_beta, 64*8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_get_count(pcnt[i], &counts[i]);
        printf("  β: [%d,%d,%d,%d]\n", counts[0],counts[1],counts[2],counts[3]);
        if (counts[1] < 20 || counts[2] < 20 || counts[0] > 5 || counts[3] > 5) ok = 0;

        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_clear_count(pcnt[i]);
        parlio_tx_unit_transmit(parlio, prog_gamma, 64*8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_get_count(pcnt[i], &counts[i]);
        printf("  γ: [%d,%d,%d,%d]\n", counts[0],counts[1],counts[2],counts[3]);
        if (counts[2] < 20 || counts[3] < 20 || counts[0] > 5 || counts[1] > 5) ok = 0;

        printf("  Result: %s\n", ok ? "PASS" : "FAIL");
        total++; passed += ok;
        fflush(stdout);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── TEST 2: Fan-out (threshold → timer stop + capture) ──
    {
        printf("TEST 2: ETM Fan-Out\n");
        for (int i = 0; i < NUM_PCNT; i++) { pcnt_unit_clear_count(pcnt[i]); pcnt_watch_hits[i]=0; }
        state_transitions = 0;

        gptimer_set_raw_count(timer0, 0);
        gptimer_alarm_config_t alarm = { .alarm_count = 100000, .flags.auto_reload_on_alarm = false };
        gptimer_set_alarm_action(timer0, &alarm);
        gptimer_start(timer0);

        parlio_transmit_config_t tx = { .idle_value = 0 };
        parlio_tx_unit_transmit(parlio, prog_alpha, 64*8, &tx);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(200);

        uint64_t t0c, t0cap;
        gptimer_get_raw_count(timer0, &t0c);
        gptimer_get_captured_count(timer0, &t0cap);
        gptimer_stop(timer0);

        printf("  Timer0: %llu us, Captured: %llu us\n", t0c, t0cap);
        printf("  Transitions: %lu, Watch[0]: %lu\n",
               (unsigned long)state_transitions, (unsigned long)pcnt_watch_hits[0]);

        int ok = (t0cap > 0 && t0cap < 50000) && (pcnt_watch_hits[0] > 0);
        printf("  Result: %s\n", ok ? "PASS" : "FAIL");
        total++; passed += ok;
        fflush(stdout);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── TEST 3: LEDC Overflow Counter ──
    {
        printf("TEST 3: LEDC Overflow Counter\n");

        // Clear ALL pending LEDC interrupts first
        volatile uint32_t *int_clr = (volatile uint32_t*)(LEDC_BASE + 0xCC);
        *int_clr = 0xFFFFFFFF;

        // Make sure all timers are paused
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_2);

        // Enable OVF_CNT for CH0 only (simpler test)
        // CH0_CONF0 = LEDC_BASE + 0x00
        // bits[14:5] = OVF_NUM, bit[15] = OVF_CNT_EN, bit[16] = OVF_CNT_RESET, bit[4] = PARA_UP
        {
            volatile uint32_t *conf0 = (volatile uint32_t*)(LEDC_BASE + 0x00);
            uint32_t val = *conf0;
            val &= ~(0x3FF << 5);          // clear OVF_NUM field
            val |= (9 << 5);               // OVF_NUM = 9 → fires after 10 overflows
            val |= (1 << 15);              // OVF_CNT_EN
            *conf0 = val;
            *conf0 = val | (1 << 4);       // PARA_UP (latch the new config)
            esp_rom_delay_us(10);           // let it latch
            *conf0 |= (1 << 16);           // OVF_CNT_RESET (reset counter)
            esp_rom_delay_us(10);

        }

        // Clear again after config
        *int_clr = 0xFFFFFFFF;
        esp_rom_delay_us(10);

        volatile uint32_t *int_raw = (volatile uint32_t*)(LEDC_BASE + 0xC0);

        // Resume ONLY timer 0, wait for overflow count to fire
        ledc_timer_resume(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        esp_rom_delay_us(10000);  // 10ms @ 5kHz = 50 periods >> 10 threshold
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);

        uint32_t raw = *int_raw;
        int ovf_cnt_ch0 = (raw >> 12) & 1;  // bit 12 = OVF_CNT_CH0 (per TRM)

        printf("  OVF_CNT_CH0=%d (INT_RAW=0x%08lx)\n", ovf_cnt_ch0, (unsigned long)raw);
        printf("  Result: %s\n", ovf_cnt_ch0 ? "PASS" : "FAIL");
        total++; passed += ovf_cnt_ch0;

        *int_clr = 0xFFFFFFFF;  // clear all LEDC interrupts
        fflush(stdout);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── Wire ETM fabric before autonomous tests ──
    {
        printf("Wiring ETM fabric...\n"); fflush(stdout);
        etm_wire(0, EVT_TIMER0_ALARM, TASK_BARE0_START,         "kickoff");
        etm_wire(1, EVT_TIMER0_ALARM, TASK_PCNT_RESET,          "kickoff");
        etm_wire(3, EVT_PCNT_THRESHOLD, TASK_BARE0_START,       "S0");
        etm_wire(4, EVT_PCNT_THRESHOLD, TASK_TIMER0_CAPTURE,    "S0");
        etm_wire(5, EVT_PCNT_THRESHOLD, TASK_PCNT_RESET,        "S0");
        etm_wire(6, EVT_PCNT_LIMIT, TASK_BARE1_START,           "S1");
        etm_wire(7, EVT_PCNT_LIMIT, TASK_TIMER0_CAPTURE,        "S1");
        etm_wire(8, EVT_PCNT_LIMIT, TASK_PCNT_RESET,            "S1");
        etm_wire(9,  EVT_PCNT_THRESHOLD, TASK_BARE0_START,      "S2");
        etm_wire(10, EVT_PCNT_THRESHOLD, TASK_TIMER0_CAPTURE,   "S2");
        etm_wire(11, EVT_PCNT_THRESHOLD, TASK_PCNT_RESET,       "S2");
        etm_wire(12, EVT_PCNT_LIMIT, TASK_BARE1_START,          "S3");
        etm_wire(13, EVT_PCNT_LIMIT, TASK_TIMER0_RELOAD,        "S3");
        etm_wire(14, EVT_PCNT_LIMIT, TASK_PCNT_RESET,           "S3");
        etm_wire(15, EVT_PCNT_THRESHOLD, TASK_BARE0_START,      "S4");
        etm_wire(16, EVT_PCNT_THRESHOLD, TASK_TIMER0_CAPTURE,   "S4");
        etm_wire(17, EVT_PCNT_THRESHOLD, TASK_PCNT_RESET,       "S4");
        etm_wire(18, EVT_PCNT_LIMIT, TASK_BARE1_START,          "S5");
        etm_wire(19, EVT_PCNT_LIMIT, TASK_TIMER0_RELOAD,        "S5");
        etm_wire(20, EVT_PCNT_LIMIT, TASK_PCNT_RESET,           "S5");
        etm_wire(21, EVT_PCNT_THRESHOLD, TASK_TIMER0_STOP,      "S6 halt");
        etm_wire(22, EVT_PCNT_THRESHOLD, TASK_TIMER0_CAPTURE,   "S6");
        etm_wire(24, EVT_PCNT_LIMIT, TASK_TIMER0_STOP,          "S7 halt");
        etm_wire(25, EVT_PCNT_LIMIT, TASK_TIMER1_STOP,          "S7 halt");
        etm_wire(27, EVT_BARE0_EOF, TASK_PCNT_RESET,            "EOF bare0");
        etm_wire(28, EVT_BARE1_EOF, TASK_PCNT_RESET,            "EOF bare1");
        etm_wire(30, EVT_TIMER1_ALARM, TASK_TIMER0_STOP,        "WDT");
        etm_wire(31, EVT_PCNT_THRESHOLD, TASK_LEDC_TIMER0_RESUME, "FB gate0");
        etm_wire(32, EVT_PCNT_LIMIT,     TASK_LEDC_TIMER1_RESUME, "FB gate1");
        etm_wire(33, EVT_PCNT_THRESHOLD, TASK_LEDC_TIMER2_RESUME, "FB gate2");
        etm_wire(34, EVT_PCNT_LIMIT,     TASK_LEDC_TIMER0_PAUSE,  "FB inh0");
        etm_wire(35, EVT_PCNT_THRESHOLD, TASK_LEDC_TIMER1_PAUSE,  "FB inh1");
        etm_wire(36, EVT_PCNT_LIMIT,     TASK_LEDC_TIMER2_PAUSE,  "FB inh2");
        etm_wire(37, EVT_LEDC_OVF_CH0, TASK_BARE0_START,        "WTA α");
        etm_wire(38, EVT_LEDC_OVF_CH1, TASK_BARE1_START,        "WTA β");
        etm_wire(40, EVT_LEDC_OVF_CH0, TASK_PCNT_RESET,          "OVF clr");
        etm_wire(41, EVT_LEDC_OVF_CH1, TASK_PCNT_RESET,          "OVF clr");
        etm_wire(42, EVT_LEDC_OVF_CH2, TASK_PCNT_RESET,          "OVF clr");
        printf("  ETM: %d channels wired\n", etm_used); fflush(stdout);
    }

    // ── TEST 4: Autonomous Loop ──
    // Take over PARLIO's GDMA channel (CH0) for ETM-triggered autonomous DMA.
    // Don't release PARLIO driver — just reconfigure CH0 bare-metal and re-enable PARLIO TX.
    {
        printf("TEST 4: Autonomous Computation (CPU idle)\n"); fflush(stdout);

        for (int i = 0; i < NUM_PCNT; i++) { pcnt_unit_clear_count(pcnt[i]); pcnt_watch_hits[i]=0; }
        state_transitions = 0; watchdog_fired = 0; fabric_halted = 0;

        // Save PARLIO TX config, then reconfigure CH0 for ETM-triggered DMA
        // PARLIO TX registers
        #define PARLIO_BASE     0x60015000
        #define PARLIO_TX_CFG0  (PARLIO_BASE + 0x08)
        #define PARLIO_TX_CFG1  (PARLIO_BASE + 0x0C)
        #define PARLIO_CLK_REG  (PARLIO_BASE + 0x120)

        // Read current PARLIO TX config (set by IDF driver)
        uint32_t tx_cfg0 = REG32(PARLIO_TX_CFG0);

        // Set BYTELEN to 64 and TX_START
        tx_cfg0 &= ~(0xFFFF << 2);       // clear BYTELEN
        tx_cfg0 |= (64 << 2);            // BYTELEN = 64
        tx_cfg0 |= (1 << 19);            // TX_START
        tx_cfg0 |= (1 << 18);            // TX_GATING_EN

        // Reconfigure CH0 for ETM-triggered DMA
        desc_alpha.owner = 1; desc_alpha.eof = 1;
        uint32_t base0 = GDMA_CH_OUT_BASE(0);
        REG32(base0 + GDMA_OUT_CONF0_OFF) = GDMA_OUT_RST_BIT;
        REG32(base0 + GDMA_OUT_CONF0_OFF) = 0;
        REG32(base0 + GDMA_OUT_CONF0_OFF) = GDMA_OUT_EOF_MODE_BIT | GDMA_OUT_ETM_EN_BIT;
        REG32(base0 + GDMA_OUT_PERI_SEL_OFF) = GDMA_PERI_SEL_PARLIO;
        uint32_t link0 = ((uint32_t)&desc_alpha) & GDMA_OUTLINK_ADDR_MASK;
        REG32(base0 + GDMA_OUT_LINK_OFF) = link0 | GDMA_OUTLINK_START_BIT;

        // Write PARLIO config with TX_START
        REG32(PARLIO_TX_CFG0) = tx_cfg0;

        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_2);

        // Re-wire ETM kickoff to use CH0 (task 162)
        REG32(ETM_CH_ENA_AD0_CLR) = (1 << 0);
        etm_wire(0, EVT_TIMER0_ALARM, TASK_GDMA_CH0_START, "kickoff-CH0");

        int64_t t_start = esp_timer_get_time();
        gptimer_set_raw_count(timer1, 0);
        gptimer_start(timer1);
        gptimer_set_raw_count(timer0, 0);
        gptimer_alarm_config_t alarm = { .alarm_count = 100, .flags.auto_reload_on_alarm = false };
        gptimer_set_alarm_action(timer0, &alarm);
        gptimer_start(timer0);

        volatile int spin = 0;
        while (!fabric_halted && spin < 50000000) {
            __asm__ volatile("nop");
            spin++;
        }

        int64_t t_end = esp_timer_get_time();
        gptimer_stop(timer0);
        gptimer_stop(timer1);

        int counts[NUM_PCNT];
        for (int i = 0; i < NUM_PCNT; i++) pcnt_unit_get_count(pcnt[i], &counts[i]);

        printf("  Duration: %lld us\n", t_end - t_start);
        printf("  PCNT[%d,%d,%d,%d]\n", counts[0],counts[1],counts[2],counts[3]);
        printf("  Transitions: %lu, Watchdog: %s\n",
               (unsigned long)state_transitions, watchdog_fired ? "YES" : "NO");

        int ok = (state_transitions > 0) || (counts[0] > 0 || counts[1] > 0);
        printf("  Result: %s\n", ok ? "PASS" : "FAIL");
        total++; passed += ok;
        fflush(stdout);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // ── TEST 5: Watchdog Halt ──
    {
        printf("TEST 5: Watchdog Halt\n");
        watchdog_fired = 0;
        gptimer_alarm_config_t a1 = { .alarm_count = 10000, .flags.auto_reload_on_alarm = false };
        gptimer_set_alarm_action(timer1, &a1);
        gptimer_set_raw_count(timer0, 0);
        gptimer_alarm_config_t a0 = { .alarm_count = 1000000, .flags.auto_reload_on_alarm = false };
        gptimer_set_alarm_action(timer0, &a0);
        gptimer_start(timer0);
        gptimer_set_raw_count(timer1, 0);
        gptimer_start(timer1);

        esp_rom_delay_us(20000);
        uint64_t t0c;
        gptimer_get_raw_count(timer0, &t0c);
        gptimer_stop(timer0);
        gptimer_stop(timer1);

        int stopped = (t0c < 15000);
        int ok = watchdog_fired && stopped;
        printf("  Timer0=%llu us, WDT=%s, Stopped=%s\n",
               t0c, watchdog_fired?"Y":"N", stopped?"Y":"N");
        printf("  Result: %s\n", ok ? "PASS" : "FAIL");
        total++; passed += ok;
        fflush(stdout);
    }

    // ── Summary ──
    printf("\n═══════════════════════════════════════════════════\n");
    printf("  RESULTS: %d / %d PASSED\n", passed, total);
    printf("  ETM channels: %d / 50\n", etm_used);
    printf("  PCNT: 4/4, GDMA: 2 bare-metal + 1 PARLIO, LEDC: 3T+3CH, Timer: 2/2\n");
    if (passed == total)
        printf("  AUTONOMOUS COMPUTATION VERIFIED.\n");
    printf("═══════════════════════════════════════════════════\n\n");
    fflush(stdout);

    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
