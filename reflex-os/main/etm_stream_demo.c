/**
 * etm_stream_demo.c - ETM Fabric with Rerun Streaming
 *
 * Autonomous hardware computation with real-time visualization.
 * Streams structured events over USB serial to host running rerun_etm_monitor.py
 *
 * Architecture:
 *   ESP32-C6                              Host
 *   ┌──────────────────┐                  ┌──────────────────┐
 *   │  PARLIO → GPIO   │                  │  rerun_etm_      │
 *   │     ↓            │    USB Serial    │  monitor.py      │
 *   │   PCNT           │ ──────────────→  │     ↓            │
 *   │     ↓            │  Binary events   │  Rerun Viewer    │
 *   │   ETM Fabric     │                  │  - Time series   │
 *   │     ↓            │                  │  - State machine │
 *   │  etm_stream.h    │                  │  - Heatmap       │
 *   └──────────────────┘                  └──────────────────┘
 *
 * Run on host:
 *   python tools/rerun_etm_monitor.py /dev/ttyACM0
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
#include "driver/uart.h"
#include "soc/soc_etm_source.h"

#include "etm_stream.h"

static const char *TAG = "ETM_STREAM";

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

// ============================================================
// Configuration
// ============================================================

#define GPIO_OUT_0          4
#define GPIO_OUT_1          5
#define GPIO_OUT_2          6
#define GPIO_OUT_3          7

#define PARLIO_CLK_HZ       1000000   // 1 MHz

// Thresholds
#define THRESHOLD_A         32
#define THRESHOLD_B         64
#define THRESHOLD_C         128

// Streaming rate limit (microseconds between PCNT updates)
#define STREAM_INTERVAL_US  10000  // 10ms = 100 Hz

// ============================================================
// Patterns
// ============================================================

static uint8_t __attribute__((aligned(4))) pattern_a[64];  // Sparse
static uint8_t __attribute__((aligned(4))) pattern_b[64];  // Medium
static uint8_t __attribute__((aligned(4))) pattern_c[64];  // Dense

// ============================================================
// Global Handles
// ============================================================

static parlio_tx_unit_handle_t parlio = NULL;
static pcnt_unit_handle_t pcnt[4] = {NULL};
static pcnt_channel_handle_t pcnt_chan[4] = {NULL};
static gptimer_handle_t timer = NULL;

// State tracking
static volatile uint8_t current_state = STATE_IDLE;
static volatile uint32_t cycle_count = 0;
static volatile int64_t last_stream_time = 0;

// Threshold tracking (for detecting crossings)
static volatile int last_counts[4] = {0};
static volatile uint8_t threshold_crossed[4][3] = {{0}};  // [channel][threshold]

// ============================================================
// Streaming
// ============================================================

/**
 * Send a packet over USB serial (using stdout which goes to USB CDC)
 */
static void stream_send(etm_stream_packet_t *pkt) {
    // Write raw bytes - fwrite to stdout goes to USB CDC on ESP32-C6
    fwrite(pkt->buf, 1, pkt->len, stdout);
    fflush(stdout);
}

/**
 * Stream PCNT counts (rate-limited)
 */
static void stream_pcnt_update(int counts[4]) {
    int64_t now = esp_timer_get_time();
    if (now - last_stream_time < STREAM_INTERVAL_US) {
        return;  // Rate limit
    }
    last_stream_time = now;
    
    etm_stream_packet_t pkt;
    etm_stream_pcnt_update(&pkt, counts[0], counts[1], counts[2], counts[3]);
    stream_send(&pkt);
}

/**
 * Stream threshold crossing
 */
static void stream_threshold(uint8_t channel, uint8_t thresh_id, int16_t count) {
    etm_stream_packet_t pkt;
    etm_stream_threshold(&pkt, channel, thresh_id, count);
    stream_send(&pkt);
}

/**
 * Stream pattern start
 */
static void stream_pattern_start(uint8_t pattern_id) {
    etm_stream_packet_t pkt;
    etm_stream_pattern_start(&pkt, pattern_id);
    stream_send(&pkt);
}

/**
 * Stream pattern end
 */
static void stream_pattern_end(uint8_t pattern_id, uint32_t duration_us) {
    etm_stream_packet_t pkt;
    etm_stream_pattern_end(&pkt, pattern_id, duration_us);
    stream_send(&pkt);
}

/**
 * Stream state change
 */
static void stream_state_change(uint8_t old_state, uint8_t new_state) {
    etm_stream_packet_t pkt;
    etm_stream_state_change(&pkt, old_state, new_state);
    stream_send(&pkt);
    current_state = new_state;
}

/**
 * Stream cycle complete
 */
static void stream_cycle_complete(uint32_t cycle_num, uint32_t duration_us) {
    etm_stream_packet_t pkt;
    etm_stream_cycle_complete(&pkt, cycle_num, duration_us);
    stream_send(&pkt);
}

/**
 * Stream heartbeat
 */
static void stream_heartbeat(void) {
    etm_stream_packet_t pkt;
    uint32_t uptime_ms = esp_timer_get_time() / 1000;
    etm_stream_heartbeat(&pkt, uptime_ms, cycle_count);
    stream_send(&pkt);
}

/**
 * Stream DMA EOF
 */
static void stream_dma_eof(uint8_t channel) {
    etm_stream_packet_t pkt;
    etm_stream_dma_eof(&pkt, channel);
    stream_send(&pkt);
}

/**
 * Stream text log
 */
static void stream_log(const char *text) {
    etm_stream_packet_t pkt;
    etm_stream_text_log(&pkt, text);
    stream_send(&pkt);
}

// ============================================================
// Pattern Generation
// ============================================================

static void generate_patterns(void) {
    // Pattern A: Sparse - 1 pulse every 8 bytes on channel 0
    memset(pattern_a, 0x00, 64);
    for (int i = 0; i < 64; i += 8) {
        pattern_a[i] = 0x01;
    }
    
    // Pattern B: Medium - alternating channels
    for (int i = 0; i < 64; i++) {
        pattern_b[i] = (i % 2 == 0) ? 0x05 : 0x0A;
    }
    
    // Pattern C: Dense - all channels
    for (int i = 0; i < 64; i++) {
        pattern_c[i] = 0x55;
    }
    
    ESP_LOGI(TAG, "Patterns generated");
}

// ============================================================
// Hardware Setup
// ============================================================

static void etm_enable_clock(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf &= ~(1 << 1);
    *conf |= (1 << 0);
}

static void etm_wire_fabric(void) {
    // CH10: PCNT threshold -> Timer capture
    ETM_REG(ETM_CH_EVT_ID_REG(10)) = PCNT_EVT_CNT_EQ_THRESH;
    ETM_REG(ETM_CH_TASK_ID_REG(10)) = 69;  // TIMER0_TASK_CNT_CAP
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 10);
    
    // CH11: GDMA EOF -> PCNT reset
    ETM_REG(ETM_CH_EVT_ID_REG(11)) = GDMA_EVT_OUT_EOF_CH0;
    ETM_REG(ETM_CH_TASK_ID_REG(11)) = PCNT_TASK_CNT_RST;
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 11);
    
    ESP_LOGI(TAG, "ETM wired");
}

static esp_err_t setup_timer(void) {
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    esp_err_t ret = gptimer_new_timer(&cfg, &timer);
    if (ret != ESP_OK) return ret;
    gptimer_enable(timer);
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
    return ESP_OK;
}

static esp_err_t setup_pcnt(void) {
    for (int i = 0; i < 4; i++) {
        pcnt_unit_config_t cfg = {
            .low_limit = -32768,
            .high_limit = 32767,
        };
        esp_err_t ret = pcnt_new_unit(&cfg, &pcnt[i]);
        if (ret != ESP_OK) return ret;
        
        pcnt_chan_config_t chan_cfg = {
            .edge_gpio_num = GPIO_OUT_0 + i,
            .level_gpio_num = -1,
        };
        ret = pcnt_new_channel(pcnt[i], &chan_cfg, &pcnt_chan[i]);
        if (ret != ESP_OK) return ret;
        
        pcnt_channel_set_edge_action(pcnt_chan[i],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_HOLD);
        
        // ESP32-C6 PCNT only supports 2 watchpoints per unit
        // Use thresholds A and B (32 and 64)
        pcnt_unit_add_watch_point(pcnt[i], THRESHOLD_A);
        pcnt_unit_add_watch_point(pcnt[i], THRESHOLD_B);
        
        pcnt_unit_enable(pcnt[i]);
        pcnt_unit_start(pcnt[i]);
    }
    return ESP_OK;
}

static void clear_all_pcnt(void) {
    for (int i = 0; i < 4; i++) {
        pcnt_unit_clear_count(pcnt[i]);
        last_counts[i] = 0;
        for (int j = 0; j < 3; j++) {
            threshold_crossed[i][j] = 0;
        }
    }
}

static void read_all_pcnt(int counts[4]) {
    for (int i = 0; i < 4; i++) {
        pcnt_unit_get_count(pcnt[i], &counts[i]);
    }
}

/**
 * Check for threshold crossings and stream events
 */
static void check_thresholds(int counts[4]) {
    static const int thresholds[3] = {THRESHOLD_A, THRESHOLD_B, THRESHOLD_C};
    
    for (int ch = 0; ch < 4; ch++) {
        for (int t = 0; t < 3; t++) {
            if (!threshold_crossed[ch][t] && 
                counts[ch] >= thresholds[t] && 
                last_counts[ch] < thresholds[t]) {
                // Threshold just crossed
                threshold_crossed[ch][t] = 1;
                stream_threshold(ch, t, counts[ch]);
            }
        }
        last_counts[ch] = counts[ch];
    }
}

// ============================================================
// Compute Loop with Streaming
// ============================================================

/**
 * Execute one pattern and stream all events
 */
static void execute_pattern(uint8_t pattern_id) {
    uint8_t *pattern;
    uint8_t new_state;
    
    switch (pattern_id) {
        case PATTERN_A:
            pattern = pattern_a;
            new_state = STATE_PATTERN_A;
            break;
        case PATTERN_B:
            pattern = pattern_b;
            new_state = STATE_PATTERN_B;
            break;
        case PATTERN_C:
            pattern = pattern_c;
            new_state = STATE_PATTERN_C;
            break;
        default:
            return;
    }
    
    // Stream state change
    stream_state_change(current_state, new_state);
    
    // Stream pattern start
    stream_pattern_start(pattern_id);
    
    int64_t t_start = esp_timer_get_time();
    
    // Execute pattern
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern, 64 * 8, &tx_cfg);
    
    // Stream PCNT updates during execution
    int counts[4];
    while (true) {
        read_all_pcnt(counts);
        check_thresholds(counts);
        stream_pcnt_update(counts);
        
        // Check if done (simple timeout)
        if (esp_timer_get_time() - t_start > 100000) {  // 100ms max
            break;
        }
        
        // Small delay
        vTaskDelay(1);
    }
    
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int64_t t_end = esp_timer_get_time();
    uint32_t duration = (uint32_t)(t_end - t_start);
    
    // Stream pattern end
    stream_pattern_end(pattern_id, duration);
    
    // Stream DMA EOF
    stream_dma_eof(0);
}

/**
 * Main compute loop - executes patterns and streams to Rerun
 */
static void compute_loop(int num_cycles) {
    stream_log("Starting compute loop");
    
    int64_t loop_start = esp_timer_get_time();
    
    for (int c = 0; c < num_cycles; c++) {
        int64_t cycle_start = esp_timer_get_time();
        
        // Clear PCNT for this cycle
        clear_all_pcnt();
        
        // Execute pattern sequence: A -> B -> C
        execute_pattern(PATTERN_A);
        execute_pattern(PATTERN_B);
        execute_pattern(PATTERN_C);
        
        // Stream cycle complete
        int64_t cycle_end = esp_timer_get_time();
        uint32_t cycle_duration = (uint32_t)(cycle_end - cycle_start);
        cycle_count = c + 1;
        stream_cycle_complete(cycle_count, cycle_duration);
        
        // Return to idle
        stream_state_change(current_state, STATE_IDLE);
        
        // Small delay between cycles
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    int64_t loop_end = esp_timer_get_time();
    
    // Final log
    char buf[64];
    snprintf(buf, sizeof(buf), "Loop complete: %d cycles in %lld ms",
             num_cycles, (loop_end - loop_start) / 1000);
    stream_log(buf);
}

/**
 * Heartbeat task - sends periodic status
 */
static void heartbeat_task(void *arg) {
    while (1) {
        stream_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 Hz heartbeat
    }
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    // Wait for USB CDC to be ready
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   ETM FABRIC STREAMING DEMO                                   ║\n");
    printf("║                                                               ║\n");
    printf("║   Real-time visualization via Rerun                           ║\n");
    printf("║                                                               ║\n");
    printf("║   On host, run:                                               ║\n");
    printf("║   python tools/rerun_etm_monitor.py /dev/ttyACM0              ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize hardware
    ESP_LOGI(TAG, "Initializing hardware...");
    
    etm_enable_clock();
    
    if (setup_timer() != ESP_OK) { ESP_LOGE(TAG, "Timer failed"); return; }
    if (setup_parlio() != ESP_OK) { ESP_LOGE(TAG, "PARLIO failed"); return; }
    if (setup_pcnt() != ESP_OK) { ESP_LOGE(TAG, "PCNT failed"); return; }
    
    etm_wire_fabric();
    generate_patterns();
    
    ESP_LOGI(TAG, "Hardware ready!");
    
    // Start heartbeat task
    xTaskCreate(heartbeat_task, "heartbeat", 2048, NULL, 5, NULL);
    
    // Wait a bit for host to connect
    ESP_LOGI(TAG, "Waiting 5 seconds for host connection...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Stream startup message
    stream_log("ETM Fabric initialized");
    stream_state_change(STATE_IDLE, STATE_IDLE);
    
    // Run continuous compute loop
    while (1) {
        ESP_LOGI(TAG, "Starting compute cycle...");
        compute_loop(10);  // 10 cycles per batch
        
        // Pause between batches
        ESP_LOGI(TAG, "Batch complete, pausing...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
