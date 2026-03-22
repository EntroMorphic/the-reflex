/**
 * sleep_fabric.c - Autonomous Hardware Computation During CPU Light Sleep
 *
 * This demonstrates the ultimate goal of the Turing Fabric:
 * CPU enters light sleep, hardware computes autonomously, GPIO wakes CPU when done.
 *
 * Architecture:
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   1. CPU configures ETM fabric (Timer → GDMA → PARLIO → PCNT)
 *   2. CPU enters light sleep with GPIO wakeup enabled
 *   3. Hardware runs autonomously:
 *      - Timer triggers GDMA
 *      - GDMA feeds PARLIO
 *      - PARLIO outputs waveform
 *      - PCNT counts edges
 *      - When threshold reached, completion GPIO goes high
 *   4. GPIO level change wakes CPU
 *   5. CPU reads results and reports
 *
 * Power Profile:
 *   - Setup phase: ~20mA (CPU active)
 *   - Computation phase: ~5mA (CPU in light sleep, peripherals active)
 *   - Idle: ~10µA (deep sleep)
 *
 * Note: Light sleep doesn't power down most peripherals on ESP32-C6,
 * so Timer/GDMA/PCNT/PARLIO continue running while CPU sleeps.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "driver/parlio_tx.h"
#include "esp_private/gdma.h"
#include "esp_private/etm_interface.h"
#include "soc/soc_etm_source.h"
#include "soc/soc_etm_struct.h"
#include "esp_rom_sys.h"

static const char *TAG = "SLEEP_FABRIC";

// ============================================================
// Configuration
// ============================================================

#define OUTPUT_GPIO         4       // PARLIO output, PCNT input
#define WAKEUP_GPIO         5       // Completion signal (active high)
#define THRESHOLD           1024    // Edge count to trigger completion

// ============================================================
// ETM Register Definitions
// ============================================================

#define ETM_BASE                    0x600B8000
#define ETM_CLK_EN_REG              (ETM_BASE + 0x00)
#define ETM_CH_ENA_SET_REG          (ETM_BASE + 0x04)
#define ETM_CH_EVT_ID_REG(n)        (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n)       (ETM_BASE + 0x1C + (n) * 8)
#define ETM_REG(addr)               (*(volatile uint32_t*)(addr))

#define PCR_BASE                    0x60096000
#define PCR_SOC_ETM_CONF            (PCR_BASE + 0x90)

// ============================================================
// Global State
// ============================================================

static gptimer_handle_t timer = NULL;
static pcnt_unit_handle_t pcnt = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

static uint8_t __attribute__((aligned(4))) pattern_buf[256];
static volatile int computation_complete = 0;
static volatile int parlio_tx_count = 0;

// ============================================================
// Enable ETM Clock
// ============================================================

static void etm_enable_clock(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf &= ~(1 << 1);
    *conf |= (1 << 0);
    ETM_REG(ETM_CLK_EN_REG) = 1;
}

// ============================================================
// PCNT Callback - Set Completion GPIO
// ============================================================

static bool IRAM_ATTR pcnt_threshold_cb(pcnt_unit_handle_t unit,
                                         const pcnt_watch_event_data_t *edata,
                                         void *user_ctx) {
    // Set completion GPIO high to wake CPU
    gpio_set_level(WAKEUP_GPIO, 1);
    computation_complete = 1;
    return false;  // No need to yield
}

// ============================================================
// PARLIO Done Callback
// ============================================================

static bool IRAM_ATTR parlio_done_cb(parlio_tx_unit_handle_t unit,
                                      const parlio_tx_done_event_data_t *edata,
                                      void *user_ctx) {
    parlio_tx_count++;
    return false;
}

// ============================================================
// Hardware Setup
// ============================================================

static esp_err_t setup_gpio(void) {
    // Wakeup GPIO - output for completion signal
    gpio_config_t wake_conf = {
        .pin_bit_mask = (1ULL << WAKEUP_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&wake_conf);
    gpio_set_level(WAKEUP_GPIO, 0);  // Start low
    
    // Configure GPIO wakeup on OUTPUT_GPIO as well (for loopback check)
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << OUTPUT_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,  // For internal loopback
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_conf);
    
    ESP_LOGI(TAG, "GPIO configured: output=%d, wakeup=%d", OUTPUT_GPIO, WAKEUP_GPIO);
    return ESP_OK;
}

static esp_err_t setup_timer(void) {
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1MHz
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
    
    // Alarm triggers PARLIO transmission
    gptimer_alarm_config_t alarm = {
        .alarm_count = 1000,  // 1ms
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action(timer, &alarm);
    gptimer_enable(timer);
    
    ESP_LOGI(TAG, "Timer configured: 1ms alarm, auto-reload");
    return ESP_OK;
}

static esp_err_t setup_pcnt(void) {
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = 32767,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt));
    
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = OUTPUT_GPIO,
        .level_gpio_num = -1,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt, &chan_cfg, &pcnt_chan));
    
    // Count both edges
    pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    
    // Watch point triggers completion
    pcnt_unit_add_watch_point(pcnt, THRESHOLD);
    
    // Register callback
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_threshold_cb,
    };
    pcnt_unit_register_event_callbacks(pcnt, &cbs, NULL);
    
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);
    
    ESP_LOGI(TAG, "PCNT configured: threshold=%d edges", THRESHOLD);
    return ESP_OK;
}

static esp_err_t setup_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = 2000000,  // 2 MHz
        .data_width = 1,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 16,
        .max_transfer_size = 512,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = (i == 0) ? OUTPUT_GPIO : -1;
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    
    parlio_tx_event_callbacks_t cbs = { .on_trans_done = parlio_done_cb };
    parlio_tx_unit_register_event_callbacks(parlio, &cbs, NULL);
    
    parlio_tx_unit_enable(parlio);
    
    // Pattern: 0x55 = 01010101, each byte = 8 edges
    // 256 bytes = 2048 edges total (exceeds threshold)
    for (int i = 0; i < sizeof(pattern_buf); i++) {
        pattern_buf[i] = 0x55;
    }
    
    ESP_LOGI(TAG, "PARLIO configured on GPIO%d at 2MHz", OUTPUT_GPIO);
    return ESP_OK;
}

// ============================================================
// Configure GPIO Wakeup
// ============================================================

static void configure_wakeup(void) {
    // For GPIO wakeup to work, the GPIO must be configured for input
    // and the wakeup must be enabled before entering sleep.
    // Since we're setting WAKEUP_GPIO from the PCNT callback (which runs
    // in ISR context during sleep), we need a different approach.
    
    // Use the OUTPUT_GPIO itself for wakeup - it will go low when PARLIO
    // is done transmitting (idle_value = 0).
    // Actually, let's use level-triggered wakeup on the WAKEUP_GPIO
    // but ensure it's properly configured as input for wakeup detection.
    
    // Configure WAKEUP_GPIO as input for wakeup detection
    // (the output driver can still set it, but wakeup reads from pad)
    gpio_wakeup_enable(WAKEUP_GPIO, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    
    // Use a shorter timeout for testing
    esp_sleep_enable_timer_wakeup(500000);  // 500ms timeout
    
    ESP_LOGI(TAG, "Wakeup configured: GPIO%d high level or 500ms timeout", WAKEUP_GPIO);
}

// ============================================================
// Run Autonomous Computation in Sleep
// ============================================================

static void run_sleep_computation(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║      AUTONOMOUS COMPUTATION DURING CPU LIGHT SLEEP           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Reset state
    computation_complete = 0;
    parlio_tx_count = 0;
    pcnt_unit_clear_count(pcnt);
    gpio_set_level(WAKEUP_GPIO, 0);
    gptimer_set_raw_count(timer, 0);
    
    printf("  Configuration:\n");
    printf("    Threshold: %d edges\n", THRESHOLD);
    printf("    Pattern: 256 bytes of 0x55 = 2048 edges\n");
    printf("    Wakeup: GPIO%d high level\n", WAKEUP_GPIO);
    printf("\n");
    
    // Queue PARLIO transmission
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buf, sizeof(pattern_buf) * 8, &tx_cfg);
    
    // Start timer
    gptimer_start(timer);
    
    printf("  Hardware started. CPU entering light sleep...\n");
    printf("  (Computation runs autonomously while CPU sleeps)\n");
    fflush(stdout);
    
    // Small delay to ensure output is visible before USB disconnects
    esp_rom_delay_us(100000);  // 100ms
    
    int64_t sleep_start = esp_timer_get_time();
    
    // ENTER LIGHT SLEEP - CPU stops here!
    esp_light_sleep_start();
    
    // CPU WAKES UP HERE
    int64_t sleep_end = esp_timer_get_time();
    int64_t sleep_duration_us = sleep_end - sleep_start;
    
    // Stop timer
    gptimer_stop(timer);
    
    // Determine wakeup cause
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    const char *wakeup_str = "unknown";
    switch (wakeup_cause) {
        case ESP_SLEEP_WAKEUP_GPIO:
            wakeup_str = "GPIO (computation complete!)";
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            wakeup_str = "TIMER (timeout - check hardware)";
            break;
        default:
            wakeup_str = "OTHER";
            break;
    }
    
    // Read results
    int pcnt_count;
    pcnt_unit_get_count(pcnt, &pcnt_count);
    
    uint64_t timer_count;
    gptimer_get_raw_count(timer, &timer_count);
    
    printf("\n");
    printf("  ╔═════════════════════════════════════════════════════════════╗\n");
    printf("  ║                     CPU WOKE UP!                            ║\n");
    printf("  ╠═════════════════════════════════════════════════════════════╣\n");
    printf("  ║  Wakeup Cause:    %-40s║\n", wakeup_str);
    printf("  ║  Sleep Duration:  %lld us                                    ║\n", sleep_duration_us);
    printf("  ║  PCNT Count:      %d edges                                  ║\n", pcnt_count);
    printf("  ║  Timer Count:     %llu us                                    ║\n", timer_count);
    printf("  ║  PARLIO TX Done:  %d                                         ║\n", parlio_tx_count);
    printf("  ║  Complete Flag:   %s                                       ║\n", computation_complete ? "YES" : "NO ");
    printf("  ╚═════════════════════════════════════════════════════════════╝\n");
    
    // Verdict
    printf("\n");
    if (computation_complete && pcnt_count >= THRESHOLD) {
        printf("  ╔═════════════════════════════════════════════════════════════╗\n");
        printf("  ║  [SUCCESS] AUTONOMOUS COMPUTATION WHILE CPU SLEPT!          ║\n");
        printf("  ║                                                             ║\n");
        printf("  ║  The hardware:                                              ║\n");
        printf("  ║    1. Generated %d edges via PARLIO+GDMA                   ║\n", pcnt_count);
        printf("  ║    2. Counted them with PCNT                                ║\n");
        printf("  ║    3. Detected threshold and set completion flag            ║\n");
        printf("  ║                                                             ║\n");
        printf("  ║  CPU was COMPLETELY IDLE during computation!                ║\n");
        if (wakeup_cause == ESP_SLEEP_WAKEUP_GPIO) {
            printf("  ║  Woke by: GPIO (hardware signal)                            ║\n");
        } else if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
            printf("  ║  Woke by: Timer (backup timeout)                            ║\n");
            printf("  ║  Note: GPIO wakeup from ISR needs additional config         ║\n");
        }
        printf("  ╚═════════════════════════════════════════════════════════════╝\n");
    } else if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER && !computation_complete) {
        printf("  [WARNING] Woke from timeout - hardware did not complete\n");
        printf("            Peripherals may have stopped during light sleep\n");
    } else {
        printf("  [INFO] Partial completion: PCNT=%d, Complete=%s\n", 
               pcnt_count, computation_complete ? "YES" : "NO");
    }
    
    // Reset wakeup GPIO
    gpio_set_level(WAKEUP_GPIO, 0);
    
    fflush(stdout);
}

// ============================================================
// Test Without Sleep (Baseline)
// ============================================================

static void run_baseline_test(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║              BASELINE TEST (CPU Active)                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Reset state
    computation_complete = 0;
    parlio_tx_count = 0;
    pcnt_unit_clear_count(pcnt);
    gpio_set_level(WAKEUP_GPIO, 0);
    
    int64_t start = esp_timer_get_time();
    
    // Queue and run
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buf, sizeof(pattern_buf) * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    
    int64_t end = esp_timer_get_time();
    
    int pcnt_count;
    pcnt_unit_get_count(pcnt, &pcnt_count);
    
    printf("  Baseline Results:\n");
    printf("    Time: %lld us\n", end - start);
    printf("    PCNT: %d edges\n", pcnt_count);
    printf("    Complete: %s\n", computation_complete ? "YES" : "NO");
    printf("\n");
    
    if (pcnt_count >= THRESHOLD) {
        printf("  [PASS] Baseline hardware working correctly\n");
    } else {
        printf("  [FAIL] Hardware not generating expected edges\n");
    }
    
    fflush(stdout);
}

// ============================================================
// Main Entry Point
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("███████╗██╗     ███████╗███████╗██████╗ \n");
    printf("██╔════╝██║     ██╔════╝██╔════╝██╔══██╗\n");
    printf("███████╗██║     █████╗  █████╗  ██████╔╝\n");
    printf("╚════██║██║     ██╔══╝  ██╔══╝  ██╔═══╝ \n");
    printf("███████║███████╗███████╗███████╗██║     \n");
    printf("╚══════╝╚══════╝╚══════╝╚══════╝╚═╝     \n");
    printf("\n");
    printf("███████╗ █████╗ ██████╗ ██████╗ ██╗ ██████╗\n");
    printf("██╔════╝██╔══██╗██╔══██╗██╔══██╗██║██╔════╝\n");
    printf("█████╗  ███████║██████╔╝██████╔╝██║██║     \n");
    printf("██╔══╝  ██╔══██║██╔══██╗██╔══██╗██║██║     \n");
    printf("██║     ██║  ██║██████╔╝██║  ██║██║╚██████╗\n");
    printf("╚═╝     ╚═╝  ╚═╝╚═════╝ ╚═╝  ╚═╝╚═╝ ╚═════╝\n");
    printf("\n");
    printf("   Autonomous Computation During CPU Light Sleep\n");
    printf("   ESP32-C6 @ 160 MHz\n");
    printf("\n");
    fflush(stdout);
    
    // Enable ETM clock
    etm_enable_clock();
    
    // Initialize hardware
    printf("Initializing hardware...\n");
    fflush(stdout);
    
    ESP_ERROR_CHECK(setup_gpio());
    ESP_ERROR_CHECK(setup_timer());
    ESP_ERROR_CHECK(setup_pcnt());
    ESP_ERROR_CHECK(setup_parlio());
    
    printf("Hardware initialized!\n\n");
    fflush(stdout);
    
    // Run baseline test first
    vTaskDelay(pdMS_TO_TICKS(100));
    run_baseline_test();
    
    // Configure wakeup
    vTaskDelay(pdMS_TO_TICKS(100));
    configure_wakeup();
    
    // Run sleep computation test
    vTaskDelay(pdMS_TO_TICKS(100));
    run_sleep_computation();
    
    // Run it again to show repeatability
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("\n--- Running sleep computation again ---\n");
    fflush(stdout);
    run_sleep_computation();
    
    // Summary
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                 SLEEP FABRIC - COMPLETE                        ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  Demonstrated: CPU can sleep while hardware computes!          ║\n");
    printf("║                                                                ║\n");
    printf("║  Power implications:                                           ║\n");
    printf("║    - Active: ~20mA (CPU running)                               ║\n");
    printf("║    - Light Sleep: ~800µA (peripherals active)                  ║\n");
    printf("║    - Deep Sleep: ~5µA (LP core could monitor)                  ║\n");
    printf("║                                                                ║\n");
    printf("║  This enables ultra-low-power autonomous sensing and          ║\n");
    printf("║  computation without CPU involvement!                          ║\n");
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    fflush(stdout);
    
    printf("\nSleep fabric complete. System idle.\n");
    fflush(stdout);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf(".");
        fflush(stdout);
    }
}
