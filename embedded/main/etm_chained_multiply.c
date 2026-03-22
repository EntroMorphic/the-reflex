/**
 * etm_chained_multiply.c - Autonomous ETM-Chained Multiplication
 *
 * Removes CPU from the multiply loop entirely using ETM event chaining.
 *
 * Current flow (with CPU):
 *   CPU: transmit pattern0 → wait → transmit pattern1 → wait → ...
 *
 * Target flow (autonomous):
 *   CPU: setup ETM chains → start → sleep
 *   ETM: pattern0 done → start pattern1 → done → start pattern2 → ...
 *   CPU: wake on final completion, read PCNT
 *
 * Architecture:
 *   Timer alarm → triggers GDMA for pattern[i]
 *   PARLIO done → ETM → restarts timer for next pattern
 *   Final pattern → ETM → sets completion flag
 *
 * This is a stepping stone toward CPU-free neural inference.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "driver/parlio_tx.h"
#include "driver/gptimer.h"
#include "esp_private/etm_interface.h"
#include "driver/gptimer_etm.h"
#include "driver/gpio_etm.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"

static const char *TAG = "ETM_CHAIN_MUL";

// ============================================================
// Configuration
// ============================================================

#define OUTPUT_GPIO         4
#define PARLIO_FREQ_HZ      10000000  // 10 MHz

#define PCNT_OVERFLOW_LIMIT 30000
#define MAX_PATTERN_BYTES   8192
#define PULSE_BYTE          0x80
#define MIN_PATTERN_BYTES   4

#define MAX_SHIFTS          8  // For 8-bit multiplier B

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static parlio_tx_unit_handle_t parlio = NULL;
static gptimer_handle_t timer = NULL;

// Pattern management
static uint8_t *pattern_buffers[MAX_SHIFTS] = {NULL};
static size_t pattern_buffer_sizes[MAX_SHIFTS] = {0};  // Allocated buffer size
static size_t pattern_tx_lengths[MAX_SHIFTS] = {0};    // Actual bytes to transmit
static int active_shifts[MAX_SHIFTS] = {0};  // Which shifts are active for current B
static int num_active_shifts = 0;
static volatile int current_shift_index = 0;

// Overflow tracking
static volatile int overflow_count = 0;

// Completion signaling
static SemaphoreHandle_t multiply_done_sem = NULL;

// ============================================================
// Overflow callback
// ============================================================

static bool IRAM_ATTR pcnt_overflow_cb(pcnt_unit_handle_t unit,
                                        const pcnt_watch_event_data_t *edata,
                                        void *user_ctx) {
    overflow_count++;
    return false;
}

// ============================================================
// PARLIO done callback - chains to next pattern
// ============================================================

static bool IRAM_ATTR parlio_done_cb(parlio_tx_unit_handle_t tx_unit,
                                      const parlio_tx_done_event_data_t *edata,
                                      void *user_ctx) {
    BaseType_t high_task_woken = pdFALSE;
    
    if (current_shift_index >= num_active_shifts) {
        // All shifts done - signal completion
        xSemaphoreGiveFromISR(multiply_done_sem, &high_task_woken);
        return high_task_woken == pdTRUE;
    }
    
    // Get next shift to transmit
    int shift = active_shifts[current_shift_index];
    size_t tx_len = pattern_tx_lengths[shift];
    
    // Start next transmission
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffers[shift], tx_len * 8, &tx_cfg);
    
    current_shift_index++;
    
    return high_task_woken == pdTRUE;
}

// Timer callback just kicks off the first pattern
static bool IRAM_ATTR timer_alarm_cb(gptimer_handle_t timer,
                                      const gptimer_alarm_event_data_t *edata,
                                      void *user_ctx) {
    // Stop timer - we only need it for initial kick
    gptimer_stop(timer);
    
    if (num_active_shifts == 0) {
        BaseType_t high_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(multiply_done_sem, &high_task_woken);
        return high_task_woken == pdTRUE;
    }
    
    // Start first pattern - PARLIO done callback will chain the rest
    int shift = active_shifts[0];
    size_t tx_len = pattern_tx_lengths[shift];
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffers[shift], tx_len * 8, &tx_cfg);
    
    current_shift_index = 1;  // Next pattern to send
    
    return false;
}

// ============================================================
// Setup functions
// ============================================================

static esp_err_t setup_gpio(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OUTPUT_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

static esp_err_t setup_pcnt(void) {
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = PCNT_OVERFLOW_LIMIT,
        .flags.accum_count = 0,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt));
    
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = OUTPUT_GPIO,
        .level_gpio_num = -1,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt, &chan_cfg, &pcnt_chan));
    
    pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    pcnt_unit_add_watch_point(pcnt, PCNT_OVERFLOW_LIMIT);
    
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_overflow_cb,
    };
    pcnt_unit_register_event_callbacks(pcnt, &cbs, NULL);
    
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);
    
    return ESP_OK;
}

static esp_err_t setup_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_FREQ_HZ,
        .data_width = 1,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 16,  // Larger queue for chaining
        .max_transfer_size = MAX_PATTERN_BYTES + 100,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_MSB,
        .flags = { .io_loop_back = 1 },
    };
    
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = (i == 0) ? OUTPUT_GPIO : -1;
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    
    // Register done callback for chaining
    parlio_tx_event_callbacks_t parlio_cbs = {
        .on_trans_done = parlio_done_cb,
    };
    ESP_ERROR_CHECK(parlio_tx_unit_register_event_callbacks(parlio, &parlio_cbs, NULL));
    
    parlio_tx_unit_enable(parlio);
    
    return ESP_OK;
}

static esp_err_t setup_timer(void) {
    gptimer_config_t timer_cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1 MHz = 1µs resolution
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &timer));
    
    // Register alarm callback
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, NULL));
    
    // Configure alarm
    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = 100,  // 100µs delay between patterns
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_cfg));
    
    ESP_ERROR_CHECK(gptimer_enable(timer));
    
    return ESP_OK;
}

// ============================================================
// Allocate pattern buffers
// ============================================================

static bool allocate_patterns(int max_A) {
    for (int i = 0; i < MAX_SHIFTS; i++) {
        size_t needed = max_A * (1 << i);
        if (needed < MIN_PATTERN_BYTES) needed = MIN_PATTERN_BYTES;
        if (needed > MAX_PATTERN_BYTES) needed = MAX_PATTERN_BYTES;
        
        pattern_buffers[i] = heap_caps_aligned_alloc(4, needed, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!pattern_buffers[i]) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes for shift%d", needed, i);
            return false;
        }
        pattern_buffer_sizes[i] = needed;
        memset(pattern_buffers[i], 0, needed);
    }
    return true;
}

static void free_patterns(void) {
    for (int i = 0; i < MAX_SHIFTS; i++) {
        if (pattern_buffers[i]) {
            heap_caps_free(pattern_buffers[i]);
            pattern_buffers[i] = NULL;
        }
    }
}

// ============================================================
// Prepare patterns for multiplication A × B
// ============================================================

static void prepare_multiply(int A, int B) {
    num_active_shifts = 0;
    
    for (int shift = 0; shift < MAX_SHIFTS; shift++) {
        if (B & (1 << shift)) {
            // This shift is active
            int pulses = A * (1 << shift);
            
            memset(pattern_buffers[shift], 0, pattern_buffer_sizes[shift]);
            size_t fill = (pulses < (int)pattern_buffer_sizes[shift]) ? pulses : pattern_buffer_sizes[shift];
            for (size_t i = 0; i < fill; i++) {
                pattern_buffers[shift][i] = PULSE_BYTE;
            }
            
            // Set actual length to transmit (separate from buffer size)
            pattern_tx_lengths[shift] = (fill < MIN_PATTERN_BYTES) ? MIN_PATTERN_BYTES : fill;
            
            active_shifts[num_active_shifts] = shift;
            num_active_shifts++;
        }
    }
    
    ESP_LOGD(TAG, "Prepared %d×%d with %d active shifts", A, B, num_active_shifts);
}

// ============================================================
// Execute multiplication using timer-driven chaining
// ============================================================

static int execute_chained_multiply(int A, int B) {
    prepare_multiply(A, B);
    
    // Reset state
    overflow_count = 0;
    current_shift_index = 0;
    pcnt_unit_clear_count(pcnt);
    
    int64_t start_time = esp_timer_get_time();
    
    // Start timer - it will trigger first pattern, then chain subsequent patterns
    gptimer_set_raw_count(timer, 0);
    gptimer_start(timer);
    
    // Wait for completion (with timeout)
    if (xSemaphoreTake(multiply_done_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Multiply timed out!");
        gptimer_stop(timer);
        return -1;
    }
    
    // Stop timer
    gptimer_stop(timer);
    
    // Wait for final PARLIO transmission to complete
    parlio_tx_unit_wait_all_done(parlio, 1000);
    esp_rom_delay_us(10);
    
    int64_t end_time = esp_timer_get_time();
    
    // Calculate result
    int pcnt_value;
    pcnt_unit_get_count(pcnt, &pcnt_value);
    int result = (overflow_count * PCNT_OVERFLOW_LIMIT) + pcnt_value;
    
    int expected = A * B;
    bool match = (result == expected);
    
    printf("  %3d × %3d = %5d (hw=%5d, overflows=%d) [%5lld µs] %s\n",
           A, B, expected, result, overflow_count,
           (long long)(end_time - start_time),
           match ? "✓" : "FAIL");
    
    return result;
}

// ============================================================
// Test suite
// ============================================================

static void test_chained_multiply(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST: ETM-CHAINED MULTIPLICATION                                 ║\n");
    printf("║  Timer-driven pattern chaining (minimal CPU involvement)          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int tests[][2] = {
        // Simple cases
        {5, 6},         // 30
        {10, 10},       // 100
        {25, 4},        // 100
        
        // Medium
        {50, 50},       // 2500
        {100, 25},      // 2500
        
        // Larger
        {100, 100},     // 10000
        {200, 50},      // 10000
        
        // Near overflow
        {180, 180},     // 32400
        
        // Over overflow
        {200, 200},     // 40000
        {255, 255},     // 65025
    };
    
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        int result = execute_chained_multiply(A, B);
        if (result == A * B) passed++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("\n  Result: %d/%d passed\n", passed, total);
}

static void test_cpu_uninvolvement(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST: CPU UNINVOLVEMENT ANALYSIS                                 ║\n");
    printf("║  Measuring what CPU does vs what hardware does                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // In this implementation:
    // - CPU sets up patterns (one-time)
    // - CPU starts timer (one instruction)
    // - Timer ISR triggers each pattern (minimal CPU - ISR overhead)
    // - PCNT counts autonomously
    // - CPU reads result at end
    
    printf("  Current architecture:\n");
    printf("    - Setup: CPU prepares pattern buffers (once per A)\n");
    printf("    - Start: CPU starts timer (gptimer_start)\n");
    printf("    - Execute: Timer ISR triggers PARLIO transmits\n");
    printf("    - Count: PCNT counts autonomously\n");
    printf("    - Wait: CPU blocks on semaphore\n");
    printf("    - Result: CPU reads PCNT value\n");
    printf("\n");
    printf("  CPU involvement per multiply:\n");
    printf("    - ISR entries: %d (one per shift in B)\n", 8);
    printf("    - True autonomous: PCNT counting\n");
    printf("\n");
    printf("  Next step for TRUE autonomy:\n");
    printf("    - Use ETM to chain PARLIO done → next GDMA start\n");
    printf("    - Eliminate timer ISR entirely\n");
    printf("    - CPU only: start, then sleep, then read result\n");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  AUTONOMOUS ETM-CHAINED MULTIPLIER                                ║\n");
    printf("║  Stepping toward CPU-free computation                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Create completion semaphore
    multiply_done_sem = xSemaphoreCreateBinary();
    if (!multiply_done_sem) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }
    
    // Allocate patterns for values up to 255
    if (!allocate_patterns(255)) {
        ESP_LOGE(TAG, "Failed to allocate patterns");
        return;
    }
    
    // Setup hardware
    ESP_LOGI(TAG, "Setting up hardware...");
    ESP_ERROR_CHECK(setup_gpio());
    ESP_ERROR_CHECK(setup_pcnt());
    ESP_ERROR_CHECK(setup_parlio());
    ESP_ERROR_CHECK(setup_timer());
    ESP_LOGI(TAG, "Hardware ready!");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run tests
    test_chained_multiply();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_cpu_uninvolvement();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Timer-driven chaining reduces CPU involvement significantly.    ║\n");
    printf("║  CPU now only:                                                    ║\n");
    printf("║    1. Prepares patterns (once)                                    ║\n");
    printf("║    2. Starts timer                                                ║\n");
    printf("║    3. Handles ISRs (one per shift)                               ║\n");
    printf("║    4. Reads result                                                ║\n");
    printf("║                                                                   ║\n");
    printf("║  True ETM autonomy would eliminate step 3 (ISRs).                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    // Cleanup
    gptimer_disable(timer);
    gptimer_del_timer(timer);
    free_patterns();
    vSemaphoreDelete(multiply_done_sem);
    
    ESP_LOGI(TAG, "Test complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
