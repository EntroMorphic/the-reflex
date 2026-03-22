/**
 * spine_main.c - Pure Spine CNS Demo (Simplified)
 *
 * Proves the CNS topology using only endogenous C6 systems.
 * No external sensors, no Thor, no network. Just silicon.
 *
 * The Spine:
 *   Timer (synthetic sensor) → Reflex check → LED (actuator)
 *                                   ↓
 *                              UART telemetry
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "reflex.h"
#include "reflex_c6.h"

static const char* TAG = "SPINE";

// ============================================================
// Configuration
// ============================================================

#define SENSOR_RATE_HZ      1000        // 1kHz synthetic sensor
#define SENSOR_PERIOD_US    (1000000 / SENSOR_RATE_HZ)
#define FORCE_THRESHOLD     128         // Anomaly if > 128 (50% chance)
#define REPORT_INTERVAL_MS  1000        // Print stats every second

// ============================================================
// State
// ============================================================

static volatile uint32_t signal_count = 0;
static volatile uint32_t anomaly_count = 0;
static volatile uint32_t min_cycles = UINT32_MAX;
static volatile uint32_t max_cycles = 0;
static volatile uint64_t sum_cycles = 0;

// ============================================================
// Timer Callback - The Synthetic Sensor
// ============================================================

static void IRAM_ATTR sensor_callback(void* arg) {
    uint32_t t0 = reflex_cycles();
    
    // Generate synthetic force
    uint32_t force = esp_random() & 0xFF;
    
    // REFLEX: threshold check
    bool anomaly = (force > FORCE_THRESHOLD);
    
    // ACTUATE: LED (ON = normal, OFF = anomaly)
    reflex_led_set(!anomaly);
    
    // Measure reflex time
    uint32_t t1 = reflex_cycles();
    uint32_t cycles = t1 - t0;
    
    // Update stats
    signal_count++;
    if (anomaly) anomaly_count++;
    if (cycles < min_cycles) min_cycles = cycles;
    if (cycles > max_cycles) max_cycles = cycles;
    sum_cycles += cycles;
}

// ============================================================
// Main
// ============================================================

// ============================================================
// Falsification Suite - Break the 87ns claim
// ============================================================

// Simple fast PRNG (xorshift)
static uint32_t prng_state = 0x12345678;
static inline uint32_t fast_random(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

typedef struct {
    const char* name;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t avg_cycles;
    uint32_t anomalies;
} test_result_t;

// Test 1: Baseline (original)
static test_result_t test_baseline(void) {
    test_result_t r = {.name = "Baseline"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 10000;
    
    prng_state = reflex_cycles();
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    return r;
}

// Test 2: No GPIO - is LED write the bottleneck?
static test_result_t test_no_gpio(void) {
    test_result_t r = {.name = "No GPIO"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 10000;
    volatile bool sink = false;  // Prevent optimization
    
    prng_state = reflex_cycles();
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        bool anomaly = (force > FORCE_THRESHOLD);
        sink = anomaly;  // Don't write GPIO
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    (void)sink;
    return r;
}

// Test 3: Always anomaly - does branch prediction matter?
static test_result_t test_always_anomaly(void) {
    test_result_t r = {.name = "Always anomaly"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 10000;
    
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = 255;  // Always above threshold
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    return r;
}

// Test 4: Never anomaly - opposite branch
static test_result_t test_never_anomaly(void) {
    test_result_t r = {.name = "Never anomaly"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 10000;
    
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = 0;  // Always below threshold
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    return r;
}

// Test 5: With memory access - cache effects
static volatile uint32_t memory_buffer[256];
static test_result_t test_memory_access(void) {
    test_result_t r = {.name = "Memory access"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 10000;
    
    // Initialize buffer
    for (int i = 0; i < 256; i++) memory_buffer[i] = i;
    
    prng_state = reflex_cycles();
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t idx = fast_random() & 0xFF;
        uint32_t force = memory_buffer[idx];  // Memory read
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    return r;
}

// Test 6: Sustained 100K iterations - drift?
static test_result_t test_sustained(void) {
    test_result_t r = {.name = "Sustained 100K"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 100000;
    
    prng_state = reflex_cycles();
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    return r;
}

// Test 7: Channel signal overhead
static reflex_channel_t test_channel;
static test_result_t test_with_channel(void) {
    test_result_t r = {.name = "With channel"};
    r.min_cycles = UINT32_MAX;
    uint64_t sum = 0;
    const int N = 10000;
    
    memset((void*)&test_channel, 0, sizeof(test_channel));
    prng_state = reflex_cycles();
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        reflex_signal(&test_channel, anomaly ? 1 : 0);  // Add channel signal
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (anomaly) r.anomalies++;
    }
    
    reflex_exit_critical(saved);
    r.avg_cycles = sum / N;
    return r;
}

static void print_result(test_result_t* r) {
    ESP_LOGW(TAG, "  %-14s %4lu cy %4lu ns (%lu-%lu)", 
             r->name,
             (unsigned long)r->avg_cycles,
             (unsigned long)reflex_cycles_to_ns(r->avg_cycles),
             (unsigned long)reflex_cycles_to_ns(r->min_cycles),
             (unsigned long)reflex_cycles_to_ns(r->max_cycles));
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));  // Let UART flush
}

static void benchmark_bare_metal(void) {
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔═════════════════════════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║           BARE METAL BENCHMARK (no RTOS)                    ║");
    ESP_LOGW(TAG, "╚═════════════════════════════════════════════════════════════╝");
    
    uint32_t bm_min = UINT32_MAX;
    uint32_t bm_max = 0;
    uint64_t bm_sum = 0;
    uint32_t bm_anomalies = 0;
    const int NUM_SAMPLES = 10000;
    
    // Seed PRNG
    prng_state = reflex_cycles();
    
    // Disable interrupts for accurate measurement
    uint32_t saved = reflex_enter_critical();
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        
        // Synthetic force (fast PRNG)
        uint32_t force = (fast_random() & 0xFF);
        
        // Reflex check
        bool anomaly = (force > FORCE_THRESHOLD);
        
        // Actuate LED
        reflex_led_set(!anomaly);
        
        uint32_t t1 = reflex_cycles();
        uint32_t cycles = t1 - t0;
        
        if (cycles < bm_min) bm_min = cycles;
        if (cycles > bm_max) bm_max = cycles;
        bm_sum += cycles;
        if (anomaly) bm_anomalies++;
    }
    
    reflex_exit_critical(saved);
    
    uint32_t bm_avg = (uint32_t)(bm_sum / NUM_SAMPLES);
    
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Bare metal reflex (interrupts disabled):");
    ESP_LOGW(TAG, "  Samples:   %d", NUM_SAMPLES);
    ESP_LOGW(TAG, "  Anomalies: %lu (%.1f%%)", 
             (unsigned long)bm_anomalies, 100.0f * bm_anomalies / NUM_SAMPLES);
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "  ┌────────────────────────────────────┐");
    ESP_LOGW(TAG, "  │  TRUE REFLEX LATENCY               │");
    ESP_LOGW(TAG, "  ├────────────────────────────────────┤");
    ESP_LOGW(TAG, "  │  Min: %4lu cycles = %4lu ns        │", 
             (unsigned long)bm_min, (unsigned long)reflex_cycles_to_ns(bm_min));
    ESP_LOGW(TAG, "  │  Max: %4lu cycles = %4lu ns        │", 
             (unsigned long)bm_max, (unsigned long)reflex_cycles_to_ns(bm_max));
    ESP_LOGW(TAG, "  │  Avg: %4lu cycles = %4lu ns        │", 
             (unsigned long)bm_avg, (unsigned long)reflex_cycles_to_ns(bm_avg));
    ESP_LOGW(TAG, "  └────────────────────────────────────┘");
    ESP_LOGW(TAG, "");
    
    // Falsification suite
    ESP_LOGW(TAG, "╔═════════════════════════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║           FALSIFICATION SUITE                               ║");
    ESP_LOGW(TAG, "╚═════════════════════════════════════════════════════════════╝");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "  Test             Avg     ns     (min, max)");
    ESP_LOGW(TAG, "  ─────────────────────────────────────────────");
    
    test_result_t r;
    
    r = test_baseline();      print_result(&r);
    r = test_no_gpio();       print_result(&r);
    r = test_always_anomaly();print_result(&r);
    r = test_never_anomaly(); print_result(&r);
    r = test_memory_access(); print_result(&r);
    r = test_with_channel();  print_result(&r);
    // Skip sustained - takes too long, truncates UART
    // r = test_sustained();     print_result(&r);
    
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "  No GPIO < Baseline = GPIO cost");
    ESP_LOGW(TAG, "  Always/Never same = no branch effect");
    ESP_LOGW(TAG, "  Memory > Baseline = cache cost");
    ESP_LOGW(TAG, "  Channel > Baseline = fence cost");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // ADVERSARIAL: Test with interrupts ENABLED (realistic)
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║   ADVERSARIAL: Interrupts ENABLED        ║");
    ESP_LOGW(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "  Running 100K iterations with interrupts ON...");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    uint32_t adv_min = UINT32_MAX;
    uint32_t adv_max = 0;
    uint64_t adv_sum = 0;
    uint32_t adv_anomalies = 0;
    uint32_t adv_spikes = 0;  // Count iterations > 1000 cycles
    const int ADV_N = 100000;
    
    prng_state = reflex_cycles();
    
    // NO critical section - interrupts stay on
    for (int i = 0; i < ADV_N; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        bool anomaly = (force > FORCE_THRESHOLD);
        reflex_led_set(!anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < adv_min) adv_min = cycles;
        if (cycles > adv_max) adv_max = cycles;
        adv_sum += cycles;
        if (anomaly) adv_anomalies++;
        if (cycles > 1000) adv_spikes++;  // >6μs is a spike
    }
    
    uint32_t adv_avg = (uint32_t)(adv_sum / ADV_N);
    
    ESP_LOGW(TAG, "  Interrupts ON (realistic):");
    ESP_LOGW(TAG, "    Samples:   %d", ADV_N);
    ESP_LOGW(TAG, "    Min:       %lu cycles = %lu ns", 
             (unsigned long)adv_min, (unsigned long)reflex_cycles_to_ns(adv_min));
    ESP_LOGW(TAG, "    Max:       %lu cycles = %lu ns", 
             (unsigned long)adv_max, (unsigned long)reflex_cycles_to_ns(adv_max));
    ESP_LOGW(TAG, "    Avg:       %lu cycles = %lu ns", 
             (unsigned long)adv_avg, (unsigned long)reflex_cycles_to_ns(adv_avg));
    ESP_LOGW(TAG, "    Spikes:    %lu (%.2f%%) > 6μs", 
             (unsigned long)adv_spikes, 100.0f * adv_spikes / ADV_N);
    ESP_LOGW(TAG, "");
    
    if (adv_max > 10000) {
        ESP_LOGW(TAG, "  ⚠️  WARNING: Max latency %.1f μs - interrupts cause jitter",
                 reflex_cycles_to_ns(adv_max) / 1000.0f);
    }
    if (adv_spikes > ADV_N / 100) {
        ESP_LOGW(TAG, "  ⚠️  WARNING: %.1f%% spikes - not suitable for hard RT",
                 100.0f * adv_spikes / ADV_N);
    }
    ESP_LOGW(TAG, "");
}

void app_main(void) {
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔═════════════════════════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║                THE REFLEX: PURE SPINE DEMO                  ║");
    ESP_LOGW(TAG, "║                                                             ║");
    ESP_LOGW(TAG, "║  No external sensors. No Thor. No network.                  ║");
    ESP_LOGW(TAG, "║  Just silicon proving the CNS topology.                     ║");
    ESP_LOGW(TAG, "║                                                             ║");
    ESP_LOGW(TAG, "║  Timer (1kHz) → Reflex → LED                                ║");
    ESP_LOGW(TAG, "╚═════════════════════════════════════════════════════════════╝");
    ESP_LOGW(TAG, "");
    
    // Run bare metal benchmark first
    reflex_led_init();
    benchmark_bare_metal();
    
    // Benchmark complete - just idle with LED
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Benchmarks complete. LED will blink to show we're alive.");
    ESP_LOGW(TAG, "");
    
    while (1) {
        reflex_led_toggle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
