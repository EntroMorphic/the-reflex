/**
 * reflex_fabric_autonomous.h - CPU-Free Neural Inference via ETM
 *
 * Architecture:
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                   AUTONOMOUS FABRIC                                     │
 *   │                                                                         │
 *   │   The CPU can sleep while the fabric runs inference.                   │
 *   │                                                                         │
 *   │   TIMER (periodic) ──► ETM ──► GPIO TOGGLE ──► RMT START              │
 *   │                                     │              │                    │
 *   │                                     │              ▼                    │
 *   │                                     │         Generate Pulses          │
 *   │                                     │              │                    │
 *   │                                     │              ▼                    │
 *   │                                     └─────► PCNT (accumulate)          │
 *   │                                                    │                    │
 *   │                                                    ▼                    │
 *   │                                             Result Ready               │
 *   │                                                                         │
 *   │   CPU wakes only to:                                                   │
 *   │     1. Read PCNT result                                                │
 *   │     2. Set up next inference                                           │
 *   │     3. Go back to sleep                                                │
 *   │                                                                         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * For truly CPU-free inference with Yinsen CfC, we need:
 *   1. Pre-computed LUTs for sparse dot product
 *   2. Pre-computed LUTs for activation functions
 *   3. GDMA to chain memory reads
 *   4. PCNT to accumulate results
 *   5. ETM to orchestrate without CPU
 *
 * This file provides a simpler demo:
 *   - Timer-triggered inference
 *   - CPU sleeps between inferences
 *   - Wake on completion
 */

#ifndef REFLEX_FABRIC_AUTONOMOUS_H
#define REFLEX_FABRIC_AUTONOMOUS_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "driver/gptimer.h"
#include "esp_etm.h"
#include "driver/gpio_etm.h"

#include "reflex_turing_fabric.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define FABRIC_AUTO_TIMER_RESOLUTION_HZ     1000000   // 1 MHz
#define FABRIC_AUTO_INFERENCE_PERIOD_US     10000     // 10 ms = 100 Hz
#define FABRIC_AUTO_TRIGGER_GPIO            7         // GPIO to trigger inference
#define FABRIC_AUTO_DONE_GPIO               8         // GPIO to signal completion

// ============================================================
// Autonomous Engine State
// ============================================================

typedef struct {
    // Base fabric
    fabric_engine_t fabric;
    
    // Timer for periodic inference
    gptimer_handle_t timer;
    
    // ETM channels
    esp_etm_channel_handle_t timer_to_gpio;     // Timer alarm → GPIO trigger
    esp_etm_channel_handle_t gpio_to_done;      // Inference complete → GPIO done
    
    // ETM events and tasks
    esp_etm_event_handle_t timer_event;
    esp_etm_task_handle_t trigger_task;
    esp_etm_task_handle_t done_task;
    
    // Synchronization
    SemaphoreHandle_t inference_done;
    
    // State
    bool running;
    uint32_t inference_count;
    uint32_t total_sleep_us;
    
    // Last result
    int last_result;
    uint8_t last_input;
    
} fabric_auto_engine_t;

// ============================================================
// Timer Callback (minimal CPU involvement)
// ============================================================

static bool IRAM_ATTR fabric_auto_timer_isr(gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata, void *user_data)
{
    fabric_auto_engine_t* engine = (fabric_auto_engine_t*)user_data;
    
    // The timer alarm triggers via ETM, so we don't need to do much here
    // Just count
    engine->inference_count++;
    
    // In a real system, ETM would handle this without ISR
    // For demo, we signal completion
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(engine->inference_done, &xHigherPriorityTaskWoken);
    
    return xHigherPriorityTaskWoken == pdTRUE;
}

// ============================================================
// Initialization
// ============================================================

static inline esp_err_t fabric_auto_init(fabric_auto_engine_t* engine) {
    memset(engine, 0, sizeof(fabric_auto_engine_t));
    
    // Initialize base fabric
    esp_err_t ret = fabric_init(&engine->fabric);
    if (ret != ESP_OK) return ret;
    
    // Create semaphore
    engine->inference_done = xSemaphoreCreateBinary();
    if (!engine->inference_done) return ESP_ERR_NO_MEM;
    
    // Create timer
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = FABRIC_AUTO_TIMER_RESOLUTION_HZ,
    };
    ret = gptimer_new_timer(&timer_config, &engine->timer);
    if (ret != ESP_OK) return ret;
    
    // Set alarm
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = FABRIC_AUTO_INFERENCE_PERIOD_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ret = gptimer_set_alarm_action(engine->timer, &alarm_config);
    if (ret != ESP_OK) return ret;
    
    // Register callback
    gptimer_event_callbacks_t timer_cbs = {
        .on_alarm = fabric_auto_timer_isr,
    };
    ret = gptimer_register_event_callbacks(engine->timer, &timer_cbs, engine);
    if (ret != ESP_OK) return ret;
    
    // Enable timer
    ret = gptimer_enable(engine->timer);
    if (ret != ESP_OK) return ret;
    
    // Configure trigger GPIO as output
    gpio_config_t trigger_conf = {
        .pin_bit_mask = (1ULL << FABRIC_AUTO_TRIGGER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&trigger_conf);
    
    // Configure done GPIO as output
    gpio_config_t done_conf = {
        .pin_bit_mask = (1ULL << FABRIC_AUTO_DONE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&done_conf);
    
    engine->running = false;
    return ESP_OK;
}

// ============================================================
// Start/Stop
// ============================================================

static inline esp_err_t fabric_auto_start(fabric_auto_engine_t* engine) {
    engine->running = true;
    return gptimer_start(engine->timer);
}

static inline esp_err_t fabric_auto_stop(fabric_auto_engine_t* engine) {
    engine->running = false;
    return gptimer_stop(engine->timer);
}

// ============================================================
// Run One Inference (for testing)
// ============================================================

static inline int fabric_auto_run_once(fabric_auto_engine_t* engine, uint8_t input) {
    engine->last_input = input;
    
    // Toggle trigger GPIO to indicate start
    gpio_set_level(FABRIC_AUTO_TRIGGER_GPIO, 1);
    
    // Run sparse dot via hardware
    int result = fabric_sparse_dot_hw(&engine->fabric, input, 
        &engine->fabric.weights->gate[0]);
    
    // Toggle done GPIO to indicate complete
    gpio_set_level(FABRIC_AUTO_DONE_GPIO, 1);
    gpio_set_level(FABRIC_AUTO_TRIGGER_GPIO, 0);
    gpio_set_level(FABRIC_AUTO_DONE_GPIO, 0);
    
    engine->last_result = result;
    return result;
}

// ============================================================
// Sleep-Wake Demo
// ============================================================

/**
 * Demonstrate CPU sleeping while fabric is "ready"
 * 
 * In a full implementation:
 *   1. ETM connects timer → GDMA → RMT → PCNT chain
 *   2. CPU enters light sleep
 *   3. Fabric runs inference autonomously
 *   4. PCNT threshold interrupt wakes CPU
 *   5. CPU reads result and goes back to sleep
 *
 * This demo simulates the pattern with timer ISR.
 */
static inline void fabric_auto_sleep_demo(fabric_auto_engine_t* engine, int duration_ms) {
    printf("  Entering sleep-wake demo for %d ms...\n", duration_ms);
    printf("  Timer period: %d us = %d Hz\n", 
        FABRIC_AUTO_INFERENCE_PERIOD_US,
        1000000 / FABRIC_AUTO_INFERENCE_PERIOD_US);
    
    // Reset counters
    engine->inference_count = 0;
    engine->total_sleep_us = 0;
    
    // Start timer
    fabric_auto_start(engine);
    
    uint32_t start_time = esp_log_timestamp();
    uint32_t end_time = start_time + duration_ms;
    
    int wake_count = 0;
    while (esp_log_timestamp() < end_time) {
        // Wait for inference trigger (simulating sleep)
        uint32_t sleep_start = esp_log_timestamp();
        
        if (xSemaphoreTake(engine->inference_done, pdMS_TO_TICKS(100)) == pdTRUE) {
            uint32_t sleep_end = esp_log_timestamp();
            engine->total_sleep_us += (sleep_end - sleep_start) * 1000;
            
            // Run inference on wake
            uint8_t input = (uint8_t)(engine->inference_count & 0xFF);
            fabric_auto_run_once(engine, input);
            wake_count++;
        }
    }
    
    // Stop timer
    fabric_auto_stop(engine);
    
    printf("  Demo complete:\n");
    printf("    Duration: %d ms\n", duration_ms);
    printf("    Inferences: %lu\n", engine->inference_count);
    printf("    Wake cycles: %d\n", wake_count);
    printf("    Effective rate: %.1f Hz\n", 
        (float)engine->inference_count * 1000.0f / duration_ms);
    printf("    CPU \"sleep\" time: %.1f%%\n",
        (float)engine->total_sleep_us / (duration_ms * 10.0f));
}

// ============================================================
// Cleanup
// ============================================================

static inline void fabric_auto_deinit(fabric_auto_engine_t* engine) {
    if (engine->timer) {
        gptimer_stop(engine->timer);
        gptimer_disable(engine->timer);
        gptimer_del_timer(engine->timer);
    }
    
    if (engine->inference_done) {
        vSemaphoreDelete(engine->inference_done);
    }
    
    fabric_deinit(&engine->fabric);
    
    memset(engine, 0, sizeof(fabric_auto_engine_t));
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_FABRIC_AUTONOMOUS_H
