/**
 * multi_channel_gdma.c - Multi-Channel GDMA with ETM Pattern Selection
 *
 * THE CARDS SELECT THE NEXT CARD
 *
 * Architecture:
 *   Three GDMA channels, each with a different pattern (punchcard).
 *   PCNT accumulates pulses. Different events trigger different channels:
 *
 *   PCNT threshold (32) → ETM event 45 → GDMA CH0 starts → Pattern A
 *   PCNT limit (64)     → ETM event 46 → GDMA CH1 starts → Pattern B  
 *   Timer alarm         → ETM event 48 → GDMA CH2 starts → Pattern C
 *
 *   DMA EOF events → PCNT reset (prepare for next decision)
 *
 * ETM Event IDs:
 *   45 = PCNT_EVT_CNT_EQ_THRESH (threshold watch point)
 *   46 = PCNT_EVT_CNT_EQ_LMT (high limit reached)
 *   48 = TIMER0_EVT_CNT_CMP (timer comparator match)
 *   153/154/155 = GDMA_EVT_OUT_EOF_CH0/1/2
 *
 * ETM Task IDs:
 *   162 = GDMA_TASK_OUT_START_CH0
 *   163 = GDMA_TASK_OUT_START_CH1
 *   164 = GDMA_TASK_OUT_START_CH2
 *   87 = PCNT_TASK_CNT_RST
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "driver/gpio.h"
#include "hal/gdma_ll.h"
#include "soc/gdma_struct.h"
#include "soc/soc_etm_source.h"
#include "soc/lldesc.h"

#include "etm_stream.h"

static const char *TAG = "MULTICHAN";

// ============================================================
// ETM Register Definitions
// ============================================================

#define ETM_BASE                    0x600B8000
#define ETM_CH_ENA_SET_REG          (ETM_BASE + 0x04)
#define ETM_CH_ENA_CLR_REG          (ETM_BASE + 0x08)
#define ETM_CH_EVT_ID_REG(n)        (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n)       (ETM_BASE + 0x1C + (n) * 8)
#define ETM_REG(addr)               (*(volatile uint32_t*)(addr))

#define PCR_BASE                    0x60096000
#define PCR_SOC_ETM_CONF            (PCR_BASE + 0x90)
#define PCR_GDMA_CONF_REG           (PCR_BASE + 0xbc)

// ============================================================
// GDMA Register Definitions (bare metal)
// ============================================================

#define GDMA_BASE                   0x60080000

// OUT channel base addresses
#define GDMA_CH0_OUT_BASE           (GDMA_BASE + 0xd0)
#define GDMA_CH1_OUT_BASE           (GDMA_BASE + 0x130)
#define GDMA_CH2_OUT_BASE           (GDMA_BASE + 0x190)

// Register offsets from channel base
#define GDMA_OUT_CONF0_OFF          0x00
#define GDMA_OUT_CONF1_OFF          0x04
#define GDMA_OUT_LINK_OFF           0x10
#define GDMA_OUT_PERI_SEL_OFF       0x30

// Bits
#define GDMA_OUT_RST                (1 << 0)
#define GDMA_OUT_EOF_MODE           (1 << 3)
#define GDMA_OUT_ETM_EN             (1 << 6)
#define GDMA_OUTLINK_START          (1 << 21)
#define GDMA_OUTLINK_ADDR_MASK      0xFFFFF

// Peripheral select for PARLIO
#define GDMA_PERI_SEL_PARLIO        9

// ============================================================
// Configuration
// ============================================================

#define GPIO_OUT_0          4
#define GPIO_OUT_1          5
#define GPIO_OUT_2          6
#define GPIO_OUT_3          7

#define PARLIO_CLK_HZ       1000000   // 1 MHz

// PCNT thresholds
#define THRESHOLD_A         32    // Watch point - fires event 45
#define THRESHOLD_B         64    // High limit - fires event 46

// ============================================================
// Patterns (Punchcards)
// ============================================================

// Pattern A: Sparse pulses
static uint8_t __attribute__((aligned(4))) pattern_a[64];

// Pattern B: Medium pulses
static uint8_t __attribute__((aligned(4))) pattern_b[64];

// Pattern C: Dense pulses
static uint8_t __attribute__((aligned(4))) pattern_c[64];

// DMA descriptors - one per channel
static lldesc_t __attribute__((aligned(4))) desc_a;
static lldesc_t __attribute__((aligned(4))) desc_b;
static lldesc_t __attribute__((aligned(4))) desc_c;

// ============================================================
// Global Handles
// ============================================================

static parlio_tx_unit_handle_t parlio = NULL;
static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static gptimer_handle_t timer = NULL;

// Statistics
static volatile uint32_t pattern_a_runs = 0;
static volatile uint32_t pattern_b_runs = 0;
static volatile uint32_t pattern_c_runs = 0;

// ============================================================
// Clock and ETM Enable
// ============================================================

static void enable_clocks(void) {
    // Enable ETM clock
    volatile uint32_t *etm_conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *etm_conf &= ~(1 << 1);  // Clear reset
    *etm_conf |= (1 << 0);   // Enable clock
    
    // Enable GDMA clock
    volatile uint32_t *gdma_conf = (volatile uint32_t*)PCR_GDMA_CONF_REG;
    *gdma_conf &= ~(1 << 1);  // Clear reset
    *gdma_conf |= (1 << 0);   // Enable clock
    
    ESP_LOGI(TAG, "ETM and GDMA clocks enabled");
}

// ============================================================
// Pattern Generation
// ============================================================

static void generate_patterns(void) {
    // Pattern A: Very sparse - 1 pulse every 8 bytes
    memset(pattern_a, 0x00, 64);
    for (int i = 0; i < 64; i += 8) {
        pattern_a[i] = 0x01;
    }
    
    // Pattern B: Medium density
    for (int i = 0; i < 64; i++) {
        pattern_b[i] = (i % 2 == 0) ? 0x05 : 0x0A;
    }
    
    // Pattern C: Dense - all channels
    for (int i = 0; i < 64; i++) {
        pattern_c[i] = 0x55;
    }
    
    ESP_LOGI(TAG, "Patterns generated: A(sparse), B(medium), C(dense)");
}

// ============================================================
// DMA Descriptor Setup
// ============================================================

static void setup_descriptors(void) {
    // Descriptor A
    desc_a.size = 64;
    desc_a.length = 64;
    desc_a.buf = pattern_a;
    desc_a.owner = 1;
    desc_a.eof = 1;
    desc_a.sosf = 0;
    desc_a.offset = 0;
    desc_a.qe.stqe_next = NULL;  // Single shot
    
    // Descriptor B
    desc_b.size = 64;
    desc_b.length = 64;
    desc_b.buf = pattern_b;
    desc_b.owner = 1;
    desc_b.eof = 1;
    desc_b.sosf = 0;
    desc_b.offset = 0;
    desc_b.qe.stqe_next = NULL;
    
    // Descriptor C
    desc_c.size = 64;
    desc_c.length = 64;
    desc_c.buf = pattern_c;
    desc_c.owner = 1;
    desc_c.eof = 1;
    desc_c.sosf = 0;
    desc_c.offset = 0;
    desc_c.qe.stqe_next = NULL;
    
    ESP_LOGI(TAG, "DMA descriptors configured");
    ESP_LOGI(TAG, "  desc_a @ %p -> pattern_a", &desc_a);
    ESP_LOGI(TAG, "  desc_b @ %p -> pattern_b", &desc_b);
    ESP_LOGI(TAG, "  desc_c @ %p -> pattern_c", &desc_c);
}

// ============================================================
// Bare-Metal GDMA Setup
// ============================================================

static void setup_gdma_channel(int ch, lldesc_t *desc) {
    uint32_t base;
    switch (ch) {
        case 0: base = GDMA_CH0_OUT_BASE; break;
        case 1: base = GDMA_CH1_OUT_BASE; break;
        case 2: base = GDMA_CH2_OUT_BASE; break;
        default: return;
    }
    
    // Reset channel
    ETM_REG(base + GDMA_OUT_CONF0_OFF) = GDMA_OUT_RST;
    ETM_REG(base + GDMA_OUT_CONF0_OFF) = 0;
    
    // Configure: EOF mode, ETM enable
    uint32_t conf0 = GDMA_OUT_EOF_MODE | GDMA_OUT_ETM_EN;
    ETM_REG(base + GDMA_OUT_CONF0_OFF) = conf0;
    
    // Set peripheral: PARLIO
    ETM_REG(base + GDMA_OUT_PERI_SEL_OFF) = GDMA_PERI_SEL_PARLIO;
    
    // Set descriptor address
    uint32_t link = ((uint32_t)desc) & GDMA_OUTLINK_ADDR_MASK;
    ETM_REG(base + GDMA_OUT_LINK_OFF) = link;
    
    ESP_LOGI(TAG, "GDMA CH%d configured with ETM, desc @ %p", ch, desc);
}

static void setup_all_gdma(void) {
    // NOTE: PARLIO driver uses GDMA internally, so we can't also use
    // bare-metal GDMA for the same channels without conflict.
    // For now, we'll skip bare-metal GDMA setup and rely on PARLIO.
    // The ETM wiring is still set up to demonstrate the concept.
    ESP_LOGI(TAG, "Skipping bare-metal GDMA setup (PARLIO uses GDMA internally)");
    
    // In a fully bare-metal implementation, we would:
    // setup_gdma_channel(0, &desc_a);
    // setup_gdma_channel(1, &desc_b);
    // setup_gdma_channel(2, &desc_c);
}

// ============================================================
// ETM Wiring for Autonomous Pattern Selection
// ============================================================

static void setup_etm_wiring(void) {
    ESP_LOGI(TAG, "Wiring ETM for autonomous pattern selection...");
    
    // ETM Channel 0: PCNT threshold (32) → GDMA CH0 start (Pattern A)
    // Event 45 = PCNT_EVT_CNT_EQ_THRESH
    // Task 162 = GDMA_TASK_OUT_START_CH0
    ETM_REG(ETM_CH_EVT_ID_REG(0)) = 45;   // PCNT_EVT_CNT_EQ_THRESH
    ETM_REG(ETM_CH_TASK_ID_REG(0)) = 162; // GDMA_TASK_OUT_START_CH0
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 0);
    ESP_LOGI(TAG, "  ETM CH0: PCNT threshold -> GDMA CH0 (Pattern A)");
    
    // ETM Channel 1: PCNT limit (64) → GDMA CH1 start (Pattern B)
    // Event 46 = PCNT_EVT_CNT_EQ_LMT
    // Task 163 = GDMA_TASK_OUT_START_CH1
    ETM_REG(ETM_CH_EVT_ID_REG(1)) = 46;   // PCNT_EVT_CNT_EQ_LMT
    ETM_REG(ETM_CH_TASK_ID_REG(1)) = 163; // GDMA_TASK_OUT_START_CH1
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 1);
    ESP_LOGI(TAG, "  ETM CH1: PCNT limit -> GDMA CH1 (Pattern B)");
    
    // ETM Channel 2: Timer compare → GDMA CH2 start (Pattern C)
    // Event 48 = TIMER0_EVT_CNT_CMP_TIMER0
    // Task 164 = GDMA_TASK_OUT_START_CH2
    ETM_REG(ETM_CH_EVT_ID_REG(2)) = 48;   // TIMER0_EVT_CNT_CMP
    ETM_REG(ETM_CH_TASK_ID_REG(2)) = 164; // GDMA_TASK_OUT_START_CH2
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 2);
    ESP_LOGI(TAG, "  ETM CH2: Timer compare -> GDMA CH2 (Pattern C)");
    
    // ETM Channel 3: GDMA CH0 EOF → PCNT reset
    // Event 153 = GDMA_EVT_OUT_EOF_CH0
    // Task 87 = PCNT_TASK_CNT_RST
    ETM_REG(ETM_CH_EVT_ID_REG(3)) = 153;  // GDMA_EVT_OUT_EOF_CH0
    ETM_REG(ETM_CH_TASK_ID_REG(3)) = 87;  // PCNT_TASK_CNT_RST
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 3);
    ESP_LOGI(TAG, "  ETM CH3: GDMA CH0 EOF -> PCNT reset");
    
    // ETM Channel 4: GDMA CH1 EOF → PCNT reset
    ETM_REG(ETM_CH_EVT_ID_REG(4)) = 154;  // GDMA_EVT_OUT_EOF_CH1
    ETM_REG(ETM_CH_TASK_ID_REG(4)) = 87;  // PCNT_TASK_CNT_RST
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 4);
    ESP_LOGI(TAG, "  ETM CH4: GDMA CH1 EOF -> PCNT reset");
    
    // ETM Channel 5: GDMA CH2 EOF → PCNT reset
    ETM_REG(ETM_CH_EVT_ID_REG(5)) = 155;  // GDMA_EVT_OUT_EOF_CH2
    ETM_REG(ETM_CH_TASK_ID_REG(5)) = 87;  // PCNT_TASK_CNT_RST
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 5);
    ESP_LOGI(TAG, "  ETM CH5: GDMA CH2 EOF -> PCNT reset");
    
    ESP_LOGI(TAG, "ETM wiring complete!");
}

// ============================================================
// Hardware Setup
// ============================================================

static esp_err_t setup_timer(void) {
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    esp_err_t ret = gptimer_new_timer(&cfg, &timer);
    if (ret != ESP_OK) return ret;
    
    // Set alarm for Pattern C trigger (e.g., after 100ms)
    gptimer_alarm_config_t alarm = {
        .alarm_count = 100000,  // 100ms
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action(timer, &alarm);
    gptimer_enable(timer);
    
    ESP_LOGI(TAG, "Timer configured with 100ms alarm");
    return ESP_OK;
}

static esp_err_t setup_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_CLK_HZ,
        .data_width = 4,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 8,
        .max_transfer_size = 1024,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = (i < 4) ? (GPIO_OUT_0 + i) : -1;
    }
    
    esp_err_t ret = parlio_new_tx_unit(&cfg, &parlio);
    if (ret != ESP_OK) return ret;
    
    parlio_tx_unit_enable(parlio);
    ESP_LOGI(TAG, "PARLIO configured on GPIO %d-%d", GPIO_OUT_0, GPIO_OUT_3);
    return ESP_OK;
}

static esp_err_t setup_pcnt(void) {
    // Configure PCNT with both threshold watch point AND high limit
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = THRESHOLD_B,  // 64 - fires PCNT_EVT_CNT_EQ_LMT
    };
    esp_err_t ret = pcnt_new_unit(&cfg, &pcnt_unit);
    if (ret != ESP_OK) return ret;
    
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = GPIO_OUT_0,
        .level_gpio_num = -1,
    };
    ret = pcnt_new_channel(pcnt_unit, &chan_cfg, &pcnt_chan);
    if (ret != ESP_OK) return ret;
    
    pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    // Add watch point for threshold A (fires event 45)
    pcnt_unit_add_watch_point(pcnt_unit, THRESHOLD_A);
    
    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_start(pcnt_unit);
    
    ESP_LOGI(TAG, "PCNT configured:");
    ESP_LOGI(TAG, "  Watch point (threshold): %d -> event 45", THRESHOLD_A);
    ESP_LOGI(TAG, "  High limit: %d -> event 46", THRESHOLD_B);
    return ESP_OK;
}

// ============================================================
// Test: Manual Pattern Execution
// ============================================================

static void test_manual_patterns(void) {
    printf("\n=== TEST: Manual Pattern Execution ===\n");
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    int count;
    
    // Clear PCNT
    pcnt_unit_clear_count(pcnt_unit);
    
    // Test Pattern A
    printf("Sending Pattern A (sparse)...\n");
    parlio_tx_unit_transmit(parlio, pattern_a, 64 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    pcnt_unit_get_count(pcnt_unit, &count);
    printf("  PCNT count: %d (expect ~8)\n", count);
    
    // Test Pattern B
    pcnt_unit_clear_count(pcnt_unit);
    printf("Sending Pattern B (medium)...\n");
    parlio_tx_unit_transmit(parlio, pattern_b, 64 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    pcnt_unit_get_count(pcnt_unit, &count);
    printf("  PCNT count: %d (expect ~64)\n", count);
    
    // Test Pattern C
    pcnt_unit_clear_count(pcnt_unit);
    printf("Sending Pattern C (dense)...\n");
    parlio_tx_unit_transmit(parlio, pattern_c, 64 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    pcnt_unit_get_count(pcnt_unit, &count);
    printf("  PCNT count: %d (expect ~128)\n", count);
    
    printf("Manual test complete.\n");
}

// ============================================================
// Test: ETM-Triggered GDMA
// ============================================================

static void test_etm_triggered_gdma(void) {
    printf("\n=== TEST: ETM-Triggered GDMA ===\n");
    printf("Sending initial pulses to trigger PCNT threshold...\n");
    
    // Clear PCNT
    pcnt_unit_clear_count(pcnt_unit);
    
    // Send enough pulses to cross threshold (32)
    // Pattern B generates ~64 pulses, so it will cross both thresholds
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_b, 64 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int count;
    pcnt_unit_get_count(pcnt_unit, &count);
    printf("After initial burst: PCNT = %d\n", count);
    
    // If ETM is working, GDMA CH0 should have been triggered at threshold 32
    // and GDMA CH1 at limit 64
    
    // Give ETM time to process
    vTaskDelay(pdMS_TO_TICKS(10));
    
    printf("ETM should have triggered:\n");
    printf("  - GDMA CH0 at threshold %d\n", THRESHOLD_A);
    printf("  - GDMA CH1 at limit %d\n", THRESHOLD_B);
    
    // Check if PCNT was reset by DMA EOF
    pcnt_unit_get_count(pcnt_unit, &count);
    printf("After ETM processing: PCNT = %d (should be low if reset worked)\n", count);
}

// ============================================================
// Test: Autonomous Loop
// ============================================================

static void test_autonomous_loop(void) {
    printf("\n=== TEST: Autonomous Loop ===\n");
    printf("Starting timer for periodic Pattern C trigger...\n");
    
    // Start timer (will trigger GDMA CH2 every 100ms via ETM)
    gptimer_start(timer);
    
    // Clear PCNT
    pcnt_unit_clear_count(pcnt_unit);
    
    printf("Autonomous system running. Monitoring for 5 seconds...\n");
    
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        
        int count;
        pcnt_unit_get_count(pcnt_unit, &count);
        printf("[%d.%ds] PCNT=%d\n", i/10, i%10, count);
    }
    
    gptimer_stop(timer);
    printf("Autonomous test complete.\n");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    // Wait for USB CDC
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   MULTI-CHANNEL GDMA WITH ETM PATTERN SELECTION               ║\n");
    printf("║                                                               ║\n");
    printf("║   THE CARDS SELECT THE NEXT CARD                              ║\n");
    printf("║                                                               ║\n");
    printf("║   PCNT threshold (32) -> ETM -> GDMA CH0 -> Pattern A         ║\n");
    printf("║   PCNT limit (64)     -> ETM -> GDMA CH1 -> Pattern B         ║\n");
    printf("║   Timer alarm         -> ETM -> GDMA CH2 -> Pattern C         ║\n");
    printf("║                                                               ║\n");
    printf("║   DMA EOF -> ETM -> PCNT reset (prepare for next decision)    ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize
    printf("Initializing hardware...\n");
    
    enable_clocks();
    generate_patterns();
    setup_descriptors();
    
    if (setup_timer() != ESP_OK) { printf("Timer failed\n"); return; }
    if (setup_parlio() != ESP_OK) { printf("PARLIO failed\n"); return; }
    if (setup_pcnt() != ESP_OK) { printf("PCNT failed\n"); return; }
    
    setup_all_gdma();
    setup_etm_wiring();
    
    printf("Hardware initialized!\n\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run tests
    test_manual_patterns();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_etm_triggered_gdma();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_autonomous_loop();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   MULTI-CHANNEL GDMA SUMMARY                                  ║\n");
    printf("║                                                               ║\n");
    printf("║   ETM Wiring:                                                 ║\n");
    printf("║   - PCNT threshold -> GDMA CH0 (Pattern A)                    ║\n");
    printf("║   - PCNT limit -> GDMA CH1 (Pattern B)                        ║\n");
    printf("║   - Timer alarm -> GDMA CH2 (Pattern C)                       ║\n");
    printf("║   - DMA EOF -> PCNT reset                                     ║\n");
    printf("║                                                               ║\n");
    printf("║   The punchcard computer is wired.                            ║\n");
    printf("║   Now we need to verify autonomous execution.                 ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    printf("\n\"It's all in the reflexes.\" - Jack Burton\n\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
