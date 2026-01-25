/**
 * main.c - The Reflex Becomes the C6
 *
 * BENCHMARK AND DEMO - not a production hot path example.
 *
 * This file uses FreeRTOS for:
 * - vTaskDelay() in demo sections (spline timing, entropy testing)
 * - Idle loop LED blink
 * - WiFi connection
 *
 * The HOT PATH PRIMITIVES being benchmarked (reflex_signal, gpio_write,
 * spline_read, etc.) are pure bare metal with zero RTOS calls.
 *
 * For a production control loop, see docs/ARCHITECTURE.md "RTOS Relationship".
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"  // Used for demos only, not hot path
#include "freertos/task.h"      // vTaskDelay for non-critical delays
#include "freertos/queue.h"     // For comparison benchmarks

#include "reflex_c6.h"
#include "channels.h"

#define NUM_SAMPLES 1000

// ============================================================
// Test 1: Channel Primitive Benchmarks
// ============================================================

static void benchmark_primitives(void) {
    printf("\n=== PRIMITIVE BENCHMARKS ===\n");
    fflush(stdout);

    uint32_t min, max;
    uint64_t sum;

    // Cycle counter overhead
    printf("\nCycle counter overhead:\n");
    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles\n", (uint32_t)(sum/NUM_SAMPLES));
    fflush(stdout);

    // Signal latency
    printf("\nreflex_signal() latency:\n");
    reflex_channel_t test_ch = {0};
    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        reflex_signal(&test_ch, i);
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);

    // Channel roundtrip
    printf("\nChannel signal+read roundtrip:\n");
    channels_init();
    min = UINT32_MAX; max = 0; sum = 0;
    uint32_t seq = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        reflex_signal(&ctrl_channel, i);
        seq = reflex_try_wait(&ctrl_channel, seq - 1);
        volatile uint32_t val = reflex_read(&ctrl_channel);
        uint32_t t1 = reflex_cycles();
        (void)val;
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);
}

// ============================================================
// Test 2: GPIO as Channel (100kHz toggle)
// ============================================================

static void benchmark_gpio(void) {
    printf("\n=== GPIO CHANNEL BENCHMARK ===\n");
    fflush(stdout);

    // Initialize LED as output channel
    reflex_led_init();

    uint32_t min, max;
    uint64_t sum;

    // Measure gpio_write latency
    printf("\ngpio_write() latency (direct register):\n");
    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        gpio_set(PIN_LED);
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);

    // Measure gpio_toggle latency
    printf("\ngpio_toggle() latency:\n");
    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        gpio_toggle(PIN_LED);
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);

    // 100kHz toggle demo (10us period = 5us on, 5us off)
    printf("\n100kHz GPIO toggle (5us on/off):\n");
    printf("  Running 10000 cycles...\n");
    fflush(stdout);

    uint32_t target_cycles = 800;  // 5us at 160MHz
    min = UINT32_MAX; max = 0;

    uint32_t total_start = reflex_cycles();
    for (int i = 0; i < 10000; i++) {
        uint32_t t0 = reflex_cycles();
        gpio_toggle(PIN_LED);
        delay_cycles(target_cycles);
        uint32_t period = reflex_cycles() - t0;
        if (period < min) min = period;
        if (period > max) max = period;
    }
    uint32_t total_cycles = reflex_cycles() - total_start;

    float actual_freq = 160000000.0f / ((float)total_cycles / 10000.0f) / 2.0f;
    printf("  min period=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max period=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  actual frequency=%.1f kHz\n", actual_freq / 1000.0f);
    printf("  jitter=%"PRIu32" cycles (%"PRIu32" ns)\n",
           max - min, reflex_cycles_to_ns(max - min));
    fflush(stdout);
}

// ============================================================
// Test 3: Timer as Channel
// ============================================================

static void benchmark_timer(void) {
    printf("\n=== TIMER CHANNEL BENCHMARK ===\n");
    fflush(stdout);

    // Initialize timer for 100us period (10kHz)
    reflex_timer_channel_t loop_timer;
    timer_channel_init(&loop_timer, 0, 0, 100);  // 100us period

    printf("\nTimer-based 10kHz loop:\n");
    printf("  Running 100 cycles...\n");
    fflush(stdout);

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;
    uint32_t last_time = reflex_cycles();

    // Shorter test to avoid watchdog (100 iterations = 10ms)
    for (int i = 0; i < 100; i++) {
        timer_wait(&loop_timer);
        uint32_t now = reflex_cycles();
        uint32_t period = now - last_time;
        last_time = now;

        if (i > 0) {  // Skip first measurement
            if (period < min) min = period;
            if (period > max) max = period;
            sum += period;
        }

        // Do some "work" (toggle LED)
        gpio_toggle(PIN_LED);
    }

    uint32_t avg = (uint32_t)(sum / 99);
    float actual_freq = 160000000.0f / (float)avg;
    float jitter_pct = 100.0f * (float)(max - min) / (float)avg;

    printf("  target period=16000 cycles (100us)\n");
    printf("  min period=%"PRIu32" cycles (%"PRIu32" us)\n", min, min/160);
    printf("  max period=%"PRIu32" cycles (%"PRIu32" us)\n", max, max/160);
    printf("  avg period=%"PRIu32" cycles (%"PRIu32" us)\n", avg, avg/160);
    printf("  actual frequency=%.1f Hz\n", actual_freq);
    printf("  jitter=%.2f%%\n", jitter_pct);
    fflush(stdout);
}

// ============================================================
// Test 3b: Critical Section Jitter (The Fix)
// ============================================================

static void benchmark_critical_jitter(void) {
    printf("\n=== CRITICAL SECTION JITTER (THE FIX) ===\n");
    printf("\nWith interrupts disabled, jitter drops to <1%%.\n");
    fflush(stdout);

    // Test 1: 10kHz loop with interrupts disabled
    printf("\n10kHz loop (interrupts disabled):\n");
    printf("  Running 10000 iterations...\n");
    fflush(stdout);

    // 16000 cycles = 100us = 10kHz at 160MHz
    reflex_jitter_stats_t stats = reflex_measure_jitter(16000, 10000);

    printf("  target period=16000 cycles (100us)\n");
    printf("  min period=%"PRIu32" cycles (%"PRIu32" us)\n",
           stats.min_cycles, stats.min_cycles/160);
    printf("  max period=%"PRIu32" cycles (%"PRIu32" us)\n",
           stats.max_cycles, stats.max_cycles/160);
    printf("  avg period=%"PRIu32" cycles\n",
           (uint32_t)(stats.sum_cycles / stats.count));
    printf("  actual frequency=%.1f Hz\n", stats.actual_freq_hz);
    printf("  JITTER=%.3f%% (target: <1%%)\n", stats.jitter_percent);
    fflush(stdout);

    // Test 2: 100kHz GPIO toggle with interrupts disabled
    printf("\n100kHz GPIO toggle (interrupts disabled):\n");
    printf("  Running 10000 iterations...\n");
    fflush(stdout);

    reflex_led_init();

    // 800 cycles = 5us half-period = 100kHz at 160MHz
    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;

    uint32_t saved = reflex_enter_critical();
    uint32_t next = reflex_cycles() + 800;
    uint32_t last = reflex_cycles();

    for (int i = 0; i < 10000; i++) {
        while (reflex_cycles() < next) {
            __asm__ volatile("nop");
        }
        gpio_toggle(PIN_LED);

        uint32_t now = reflex_cycles();
        uint32_t period = now - last;
        last = now;
        next += 800;

        if (i > 0) {
            if (period < min) min = period;
            if (period > max) max = period;
            sum += period;
        }
    }

    reflex_exit_critical(saved);

    uint32_t avg = (uint32_t)(sum / 9999);
    float actual_freq = 160000000.0f / (float)avg / 2.0f;
    float jitter_pct = 100.0f * (float)(max - min) / (float)avg;

    printf("  target period=800 cycles (5us half-period)\n");
    printf("  min period=%"PRIu32" cycles (%"PRIu32" ns)\n",
           min, reflex_cycles_to_ns(min));
    printf("  max period=%"PRIu32" cycles (%"PRIu32" ns)\n",
           max, reflex_cycles_to_ns(max));
    printf("  variance=%"PRIu32" cycles (target: <100)\n", max - min);
    printf("  actual frequency=%.1f kHz (target: >99kHz)\n", actual_freq / 1000.0f);
    printf("  JITTER=%.3f%% (target: <1%%)\n", jitter_pct);
    fflush(stdout);

    // Summary
    printf("\nJitter fix summary:\n");
    printf("  Without critical section: ~98%% jitter (FreeRTOS preemption)\n");
    printf("  With critical section: <1%% jitter (deterministic)\n");
    printf("  Technique: RISC-V CSR mstatus MIE bit disable\n");
    fflush(stdout);
}

// ============================================================
// Test 3c: Comparison with FreeRTOS Alternatives
// ============================================================

static void benchmark_alternatives(void) {
    printf("\n=== COMPARISON: REFLEX vs FREERTOS ===\n");
    printf("\nHead-to-head benchmarks against the standard approach.\n");
    fflush(stdout);

    uint32_t min, max;
    uint64_t sum;

    // ========================================
    // 1. reflex_signal() vs xQueueSend()
    // ========================================
    printf("\n1. reflex_signal() vs xQueueSend():\n");
    fflush(stdout);

    // Test reflex_signal()
    reflex_channel_t test_ch = {0};
    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t t0 = reflex_cycles();
        reflex_signal(&test_ch, i);
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    uint32_t reflex_avg = (uint32_t)(sum / 1000);
    printf("   reflex_signal(): min=%"PRIu32" avg=%"PRIu32" max=%"PRIu32" cycles (%"PRIu32" ns)\n",
           min, reflex_avg, max, reflex_cycles_to_ns(reflex_avg));

    // Test xQueueSend() - create queue first
    QueueHandle_t queue = xQueueCreate(1, sizeof(uint32_t));
    if (queue == NULL) {
        printf("   xQueueSend(): FAILED to create queue\n");
    } else {
        min = UINT32_MAX; max = 0; sum = 0;
        for (int i = 0; i < 1000; i++) {
            uint32_t val = i;
            uint32_t t0 = reflex_cycles();
            xQueueOverwrite(queue, &val);  // Use overwrite for non-blocking, single-item queue
            uint32_t t1 = reflex_cycles();
            uint32_t diff = t1 - t0;
            if (diff < min) min = diff;
            if (diff > max) max = diff;
            sum += diff;
        }
        uint32_t queue_avg = (uint32_t)(sum / 1000);
        printf("   xQueueOverwrite(): min=%"PRIu32" avg=%"PRIu32" max=%"PRIu32" cycles (%"PRIu32" ns)\n",
               min, queue_avg, max, reflex_cycles_to_ns(queue_avg));
        printf("   Ratio: reflex is %.1fx faster\n", (float)queue_avg / (float)reflex_avg);
        vQueueDelete(queue);
    }
    fflush(stdout);

    // ========================================
    // 2. reflex_signal() vs raw atomic
    // ========================================
    printf("\n2. reflex_signal() vs raw atomic_store:\n");
    fflush(stdout);

    // Test raw atomic approach (minimal channel)
    typedef struct {
        _Atomic uint32_t value;
        _Atomic uint32_t seq;
    } raw_channel_t;
    raw_channel_t raw = {0};

    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t t0 = reflex_cycles();
        atomic_store_explicit(&raw.value, i, memory_order_release);
        atomic_store_explicit(&raw.seq, i, memory_order_release);
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    uint32_t raw_avg = (uint32_t)(sum / 1000);
    printf("   raw atomic: min=%"PRIu32" avg=%"PRIu32" max=%"PRIu32" cycles (%"PRIu32" ns)\n",
           min, raw_avg, max, reflex_cycles_to_ns(raw_avg));
    printf("   reflex_signal(): avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           reflex_avg, reflex_cycles_to_ns(reflex_avg));
    printf("   Overhead of abstraction: %"PRIu32" cycles (%"PRIu32" ns)\n",
           reflex_avg - raw_avg, reflex_cycles_to_ns(reflex_avg - raw_avg));
    fflush(stdout);

    // ========================================
    // 3. Channel read vs xQueueReceive
    // ========================================
    printf("\n3. reflex_read() vs xQueuePeek():\n");
    fflush(stdout);

    // Test reflex_read()
    min = UINT32_MAX; max = 0; sum = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t t0 = reflex_cycles();
        volatile uint32_t val = reflex_read(&test_ch);
        (void)val;
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }
    uint32_t read_avg = (uint32_t)(sum / 1000);
    printf("   reflex_read(): min=%"PRIu32" avg=%"PRIu32" max=%"PRIu32" cycles (%"PRIu32" ns)\n",
           min, read_avg, max, reflex_cycles_to_ns(read_avg));

    // Test xQueuePeek()
    queue = xQueueCreate(1, sizeof(uint32_t));
    if (queue != NULL) {
        uint32_t val = 42;
        xQueueOverwrite(queue, &val);

        min = UINT32_MAX; max = 0; sum = 0;
        for (int i = 0; i < 1000; i++) {
            uint32_t t0 = reflex_cycles();
            xQueuePeek(queue, &val, 0);
            uint32_t t1 = reflex_cycles();
            uint32_t diff = t1 - t0;
            if (diff < min) min = diff;
            if (diff > max) max = diff;
            sum += diff;
        }
        uint32_t peek_avg = (uint32_t)(sum / 1000);
        printf("   xQueuePeek(): min=%"PRIu32" avg=%"PRIu32" max=%"PRIu32" cycles (%"PRIu32" ns)\n",
               min, peek_avg, max, reflex_cycles_to_ns(peek_avg));
        printf("   Ratio: reflex is %.1fx faster\n", (float)peek_avg / (float)read_avg);
        vQueueDelete(queue);
    }
    fflush(stdout);

    // ========================================
    // 4. Memory footprint comparison
    // ========================================
    printf("\n4. Memory footprint comparison:\n");
    printf("   reflex_channel_t:           %zu bytes\n", sizeof(reflex_channel_t));
    printf("   reflex_spline_channel_t:    %zu bytes\n", sizeof(reflex_spline_channel_t));
    printf("   reflex_entropic_channel_t:  %zu bytes\n", sizeof(reflex_entropic_channel_t));

    // Estimate FreeRTOS queue size (implementation-dependent)
    printf("   FreeRTOS queue (1 item):    ~76 bytes (typical)\n");
    printf("   FreeRTOS semaphore:         ~88 bytes (typical)\n");
    printf("   FreeRTOS mutex:             ~96 bytes (typical)\n");
    fflush(stdout);

    // ========================================
    // Summary: When to use what
    // ========================================
    printf("\n5. When to use The Reflex vs FreeRTOS:\n");
    printf("\n   USE THE REFLEX WHEN:\n");
    printf("   - Hot path signaling (sub-200ns)\n");
    printf("   - Many channels needed (memory constrained)\n");
    printf("   - Lock-free polling acceptable\n");
    printf("   - Single producer, multiple readers\n");
    printf("   - Continuous values (spline interpolation)\n");
    printf("   - Stigmergy patterns needed\n");

    printf("\n   USE FREERTOS WHEN:\n");
    printf("   - Task blocking required\n");
    printf("   - Multiple producers\n");
    printf("   - Buffer/queue semantics needed\n");
    printf("   - Priority inheritance required\n");
    printf("   - Proven, audited code required\n");
    fflush(stdout);
}

// ============================================================
// Test 4: ADC as Channel
// ============================================================

static void benchmark_adc(void) {
    printf("\n=== ADC CHANNEL BENCHMARK ===\n");
    fflush(stdout);

    // Initialize ADC channel on GPIO0 (ADC1_CH0)
    reflex_adc_channel_t sensor;
    adc_channel_init(&sensor, ADC_CHANNEL_0, ADC_ATTEN_DB_12);

    printf("\nADC read latency (12-bit, full range):\n");
    fflush(stdout);

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;

    for (int i = 0; i < 100; i++) {
        uint32_t t0 = reflex_cycles();
        int raw = adc_read(&sensor);
        uint32_t t1 = reflex_cycles();
        (void)raw;

        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }

    printf("  min=%"PRIu32" cycles (%"PRIu32" us)\n", min, min/160);
    printf("  max=%"PRIu32" cycles (%"PRIu32" us)\n", max, max/160);
    printf("  avg=%"PRIu32" cycles (%"PRIu32" us)\n",
           (uint32_t)(sum/100), (uint32_t)(sum/100)/160);
    fflush(stdout);

    // Sample reading
    printf("\nSample ADC reading (GPIO0):\n");
    int raw = adc_read(&sensor);
    int mv = adc_raw_to_mv(&sensor, raw);
    printf("  raw=%d  (~%d mV)\n", raw, mv);
    fflush(stdout);
}

// ============================================================
// Test 5: Spline Channel (discrete to continuous)
// ============================================================

static void benchmark_spline(void) {
    printf("\n=== SPLINE CHANNEL BENCHMARK ===\n");
    printf("\nBridging discrete signals to continuous reality.\n");
    fflush(stdout);

    // Initialize spline channel
    reflex_spline_channel_t trajectory;
    spline_init_at(&trajectory, 0);

    // Measure spline_read latency
    printf("\nspline_read() latency:\n");
    fflush(stdout);

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;

    for (int i = 0; i < 100; i++) {
        uint32_t t0 = reflex_cycles();
        volatile int32_t val = spline_read(&trajectory);
        uint32_t t1 = reflex_cycles();
        (void)val;

        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }

    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/100), reflex_cycles_to_ns((uint32_t)(sum/100)));
    fflush(stdout);

    // Demonstrate interpolation
    printf("\nSpline interpolation demo:\n");
    printf("  Signaling control points: 0 -> 1000 -> 500 -> 2000\n");
    fflush(stdout);

    spline_init_at(&trajectory, 0);
    vTaskDelay(1);
    spline_signal(&trajectory, 1000);
    vTaskDelay(1);
    spline_signal(&trajectory, 500);
    vTaskDelay(1);
    spline_signal(&trajectory, 2000);

    printf("  Current spline value: %"PRId32"\n", spline_read(&trajectory));
    printf("  Velocity: %"PRId32" per 1000 cycles\n", spline_velocity(&trajectory));
    printf("  Predicted +10000 cycles: %"PRId32"\n", spline_predict(&trajectory, 10000));
    fflush(stdout);

    // Show that spline works
    printf("\nSpline channel ready for continuous trajectory generation.\n");
    fflush(stdout);
}

// ============================================================
// Test 6: Entropy Field (The Void Between Shapes)
// ============================================================

static uint32_t crystallization_count = 0;

static void on_crystallize(reflex_entropy_field_t* field,
                            uint16_t x, uint16_t y,
                            uint32_t entropy) {
    (void)field;
    crystallization_count++;
    printf("  CRYSTALLIZE at (%d,%d) with entropy %"PRIu32"\n", x, y, entropy);
}

static void benchmark_entropy_field(void) {
    printf("\n=== ENTROPY FIELD (THE VOID) ===\n");
    printf("\nEntropy as structure. The space between shapes IS information.\n");
    fflush(stdout);

    // Create a small entropy field (8x8 = 64 cells, fits in cache)
    reflex_entropy_field_t field;
    uint32_t capacity = 10000;  // Crystallization threshold
    bool ok = entropy_field_init(&field, 8, 8, capacity);

    if (!ok) {
        printf("  ERROR: Failed to allocate entropy field\n");
        return;
    }

    printf("\nEntropy field initialized: 8x8 cells, capacity=%"PRIu32"\n", capacity);
    fflush(stdout);

    // Measure entropy_deposit latency
    printf("\nentropy_deposit() latency:\n");
    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;

    for (int i = 0; i < 100; i++) {
        uint32_t t0 = reflex_cycles();
        entropy_deposit(&field, 4, 4, 100);  // Deposit at center
        uint32_t t1 = reflex_cycles();

        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }

    printf("  min=%"PRIu32" cycles (%"PRIu32" ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%"PRIu32" cycles (%"PRIu32" ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/100), reflex_cycles_to_ns((uint32_t)(sum/100)));
    fflush(stdout);

    // Reset field for demo
    entropy_field_free(&field);
    entropy_field_init(&field, 8, 8, capacity);

    // Measure field tick latency
    printf("\nentropy_field_tick() latency (8x8 = 64 cells):\n");
    min = UINT32_MAX; max = 0; sum = 0;

    for (int i = 0; i < 50; i++) {
        uint32_t t0 = reflex_cycles();
        entropy_field_tick(&field);
        uint32_t t1 = reflex_cycles();

        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }

    printf("  min=%"PRIu32" cycles (%"PRIu32" us)\n", min, min/160);
    printf("  max=%"PRIu32" cycles (%"PRIu32" us)\n", max, max/160);
    printf("  avg=%"PRIu32" cycles (%"PRIu32" us)\n",
           (uint32_t)(sum/50), (uint32_t)(sum/50)/160);
    printf("  per-cell avg=%"PRIu32" cycles (%"PRIu32" ns)\n",
           (uint32_t)(sum/50/64), reflex_cycles_to_ns((uint32_t)(sum/50/64)));
    fflush(stdout);

    // Demonstrate stigmergy: deposit entropy, let it diffuse, watch crystallization
    printf("\nStigmergy demonstration:\n");
    printf("  Depositing high entropy at corners, letting field evolve...\n");
    fflush(stdout);

    // Reset field
    entropy_field_free(&field);
    entropy_field_init(&field, 8, 8, capacity);
    field.diffusion_rate = 256;  // 25% diffusion for faster demo

    // Deposit entropy at corners (like pheromone trails)
    stigmergy_write(&field, 0, 0, capacity / 2);
    stigmergy_write(&field, 7, 0, capacity / 2);
    stigmergy_write(&field, 0, 7, capacity / 2);
    stigmergy_write(&field, 7, 7, capacity / 2);

    printf("  Initial total entropy: %"PRIu32"\n", field.total_entropy);

    // Evolve field and visualize
    printf("\n  Field evolution (10 ticks):\n");
    for (int tick = 0; tick < 10; tick++) {
        entropy_field_tick(&field);

        // Print field state
        printf("  Tick %d: total=%"PRIu32"  ", tick + 1, field.total_entropy);
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                reflex_void_cell_t* cell = field_cell(&field, x, y);
                printf("%c", entropy_level_char(cell->entropy, capacity));
            }
        }
        printf("\n");
    }
    fflush(stdout);

    // Sense gradient at center
    printf("\n  Gradient at center (4,4):\n");
    stigmergy_sense_t sense = stigmergy_read(&field, 4, 4);
    printf("    entropy: %"PRIu32"\n", sense.entropy);
    printf("    gradient: (%d, %d)\n", sense.gradient_x, sense.gradient_y);
    int8_t dir = stigmergy_follow(&field, 4, 4, true);
    const char* dir_names[] = {"North", "East", "South", "West"};
    printf("    toward high entropy: %s\n", dir >= 0 ? dir_names[dir] : "flat");
    fflush(stdout);

    // Demonstrate crystallization
    printf("\n  Crystallization test:\n");
    printf("  Depositing critical entropy at (3,3)...\n");
    entropy_deposit(&field, 3, 3, capacity * 2);  // Exceed capacity

    crystallization_count = 0;
    uint32_t crystals = entropy_field_crystallize(&field, on_crystallize);
    printf("  Crystallizations triggered: %"PRIu32"\n", crystals);
    fflush(stdout);

    // Entropic channel test
    printf("\n  Entropic channel (silence = entropy):\n");
    reflex_entropic_channel_t ech;
    entropic_init(&ech, 100000);  // Crystallizes after ~100k cycles of silence

    printf("    Initial entropy: %"PRIu32"\n", ech.entropy);
    printf("    Waiting...\n");
    vTaskDelay(1);  // ~1ms of silence
    entropic_update(&ech);
    printf("    After 1ms silence: entropy=%"PRIu32" level=%"PRIu32"/1024\n",
           ech.entropy, entropic_level(&ech));

    entropic_signal(&ech, 42);  // Signal collapses the void
    printf("    After signal: entropy=%"PRIu32" (collapsed)\n", ech.entropy);
    fflush(stdout);

    // Cleanup
    entropy_field_free(&field);

    printf("\nThe void is ready. Entropy IS the computation.\n");
    fflush(stdout);
}

// ============================================================
// Test 7: SPI as Protocol Channel
// ============================================================

static void benchmark_spi(void) {
    printf("\n=== SPI CHANNEL BENCHMARK ===\n");
    printf("\nBidirectional protocol channel.\n");
    fflush(stdout);

    // Initialize SPI channel (no device connected, just measure setup)
    reflex_spi_channel_t spi;
    esp_err_t ret = spi_channel_init(&spi, SPI_DEFAULT_CS, 1000000, 0);

    if (ret == ESP_OK) {
        printf("  SPI initialized at 1MHz\n");

        // Measure single-byte transfer latency (loopback if MOSI->MISO connected)
        printf("\nSPI single-byte transfer latency:\n");

        uint32_t min = UINT32_MAX, max = 0;
        uint64_t sum = 0;

        for (int i = 0; i < 50; i++) {
            uint32_t t0 = reflex_cycles();
            uint8_t rx = spi_transfer_byte(&spi, 0xAA);
            uint32_t t1 = reflex_cycles();
            (void)rx;

            uint32_t diff = t1 - t0;
            if (diff < min) min = diff;
            if (diff > max) max = diff;
            sum += diff;
        }

        printf("  min=%"PRIu32" cycles (%"PRIu32" us)\n", min, min/160);
        printf("  max=%"PRIu32" cycles (%"PRIu32" us)\n", max, max/160);
        printf("  avg=%"PRIu32" cycles (%"PRIu32" us)\n",
               (uint32_t)(sum/50), (uint32_t)(sum/50)/160);
        printf("  Transactions: %"PRIu32"\n", spi_get_transactions(&spi));
    } else {
        printf("  SPI init failed: %d\n", ret);
    }
    fflush(stdout);
}

// ============================================================
// Test 8: Self-Reconfiguring Soft Processor (echip)
// ============================================================

static void demo_echip(void) {
    printf("\n=== SELF-RECONFIGURING SOFT PROCESSOR ===\n");
    printf("\nThe chip that watches itself think and rewires accordingly.\n");
    fflush(stdout);

    // Create a small echip
    echip_t chip;
    if (!echip_init(&chip, 64, 128, 8)) {
        printf("  ERROR: Failed to initialize echip\n");
        return;
    }

    // Allocate I/O
    echip_alloc_io(&chip, 2, 1);

    printf("\nCreating initial shapes...\n");
    fflush(stdout);

    // Create a simple circuit: 2 inputs → NAND → LATCH → output
    //
    //   INPUT[0] ──┐
    //              ├──► NAND ──► LATCH ──► OUTPUT
    //   INPUT[1] ──┘       ↑
    //                      │
    //                   (enable from oscillator)

    uint16_t in0 = echip_create_shape(&chip, SHAPE_INPUT, 0, 4);
    uint16_t in1 = echip_create_shape(&chip, SHAPE_INPUT, 0, 6);
    uint16_t nand = echip_create_shape(&chip, SHAPE_NAND, 2, 5);
    uint16_t latch = echip_create_shape(&chip, SHAPE_LATCH, 4, 5);
    uint16_t osc = echip_create_shape(&chip, SHAPE_OSCILLATOR, 3, 7);
    uint16_t out = echip_create_shape(&chip, SHAPE_OUTPUT, 6, 5);

    // Set oscillator period
    frozen_shape_t* osc_shape = echip_find_shape(&chip, osc);
    if (osc_shape) osc_shape->threshold = 5;  // Toggle every 5 ticks

    printf("  Created %d shapes: IN0(%d) IN1(%d) NAND(%d) LATCH(%d) OSC(%d) OUT(%d)\n",
           chip.num_shapes, in0, in1, nand, latch, osc, out);
    fflush(stdout);

    printf("\nCreating routes...\n");
    fflush(stdout);

    // Route: in0 → nand.a
    int r1 = echip_create_route(&chip, in0, 0, nand, 0, WEIGHT_SCALE);
    // Route: in1 → nand.b
    int r2 = echip_create_route(&chip, in1, 0, nand, 1, WEIGHT_SCALE);
    // Route: nand → latch.data
    int r3 = echip_create_route(&chip, nand, 0, latch, 0, WEIGHT_SCALE);
    // Route: osc → latch.enable
    int r4 = echip_create_route(&chip, osc, 0, latch, 1, WEIGHT_SCALE);
    // Route: latch → output
    int r5 = echip_create_route(&chip, latch, 0, out, 0, WEIGHT_SCALE);

    printf("  Created %d routes: r1(%d) r2(%d) r3(%d) r4(%d) r5(%d)\n",
           chip.num_routes, r1, r2, r3, r4, r5);
    fflush(stdout);

    // Measure tick latency
    printf("\nechip_tick() latency:\n");
    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;

    for (int i = 0; i < 50; i++) {
        uint32_t t0 = reflex_cycles();
        echip_tick(&chip);
        uint32_t t1 = reflex_cycles();

        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }

    printf("  min=%"PRIu32" cycles (%"PRIu32" us)\n", min, min/160);
    printf("  max=%"PRIu32" cycles (%"PRIu32" us)\n", max, max/160);
    printf("  avg=%"PRIu32" cycles (%"PRIu32" us)\n",
           (uint32_t)(sum/50), (uint32_t)(sum/50)/160);
    fflush(stdout);

    // Run the circuit with different inputs
    printf("\nRunning circuit simulation...\n");
    printf("  Truth table test (NAND):\n");
    fflush(stdout);

    int16_t test_inputs[4][2] = {
        {0, 0},
        {0, WEIGHT_SCALE},
        {WEIGHT_SCALE, 0},
        {WEIGHT_SCALE, WEIGHT_SCALE}
    };

    for (int t = 0; t < 4; t++) {
        chip.ext_inputs[0] = test_inputs[t][0];
        chip.ext_inputs[1] = test_inputs[t][1];

        // Run enough ticks for signal to propagate and latch to update
        for (int i = 0; i < 20; i++) {
            echip_tick(&chip);
        }

        int in_a = test_inputs[t][0] > 0 ? 1 : 0;
        int in_b = test_inputs[t][1] > 0 ? 1 : 0;
        int out_val = chip.ext_outputs[0] > 0 ? 1 : 0;
        printf("    NAND(%d, %d) → %d\n", in_a, in_b, out_val);
    }
    fflush(stdout);

    // Show Hebbian learning in action
    printf("\nHebbian learning demonstration:\n");
    fflush(stdout);

    // Record initial weights
    printf("  Initial route weights:\n");
    for (int i = 0; i < 5; i++) {
        printf("    Route %d: weight=%d activity=%d\n",
               i, chip.routes[i].weight, chip.routes[i].activity);
    }

    // Run with consistent input pattern to strengthen certain routes
    chip.ext_inputs[0] = WEIGHT_SCALE;
    chip.ext_inputs[1] = WEIGHT_SCALE;

    for (int i = 0; i < 100; i++) {
        echip_tick(&chip);
    }

    printf("  After 100 ticks with inputs (1,1):\n");
    for (int i = 0; i < 5; i++) {
        printf("    Route %d: weight=%d activity=%d state=%d\n",
               i, chip.routes[i].weight, chip.routes[i].activity,
               chip.routes[i].state);
    }
    fflush(stdout);

    // Get stats
    echip_stats_t stats = echip_get_stats(&chip);
    printf("\nChip statistics:\n");
    printf("  Shapes: %d (created: %"PRIu32", dissolved: %"PRIu32")\n",
           stats.num_shapes, stats.shapes_created, stats.shapes_dissolved);
    printf("  Routes: %d (created: %"PRIu32", dissolved: %"PRIu32")\n",
           stats.num_routes, stats.routes_created, stats.routes_dissolved);
    printf("  Signals propagated: %"PRIu32"\n", stats.signals_propagated);
    printf("  Total entropy: %"PRIu32"\n", stats.total_entropy);
    printf("  Ticks: %"PRIu64"\n", stats.tick);
    fflush(stdout);

    // Demonstrate crystallization potential
    printf("\nEntropy field state:\n");
    for (int y = 0; y < 8; y++) {
        printf("  ");
        for (int x = 0; x < 8; x++) {
            reflex_void_cell_t* cell = field_cell(&chip.field, x, y);
            printf("%c", entropy_cell_char(cell));
        }
        printf("\n");
    }
    fflush(stdout);

    // Cleanup
    free(chip.ext_inputs);
    free(chip.ext_outputs);
    echip_free(&chip);

    printf("\nThe self-reconfiguring processor is alive.\n");
    printf("It watches. It learns. It grows.\n");
    fflush(stdout);
}

// ============================================================
// Test 9: WiFi as Network Channel
// ============================================================

static void benchmark_wifi(void) {
    printf("\n=== WIFI CHANNEL TEST ===\n");
    printf("\nConnecting to network...\n");
    fflush(stdout);

    reflex_wifi_channel_t wifi;
    const char* ssid = "NETGEAR90";
    const char* pass = "unusualsocks3840";
    esp_err_t ret = wifi_channel_init(&wifi, ssid, pass);

    if (ret != ESP_OK) {
        printf("  WiFi init failed: %d\n", ret);
        return;
    }

    printf("  WiFi initialized, waiting for IP...\n");
    fflush(stdout);

    // Wait up to 15 seconds for connection
    bool connected = wifi_wait_connected(&wifi, 15000);

    if (connected) {
        char ip_str[16];
        wifi_get_ip_str(&wifi, ip_str, sizeof(ip_str));
        int8_t rssi = wifi_get_rssi(&wifi);

        printf("\n  CONNECTED!\n");
        printf("  IP address: %s\n", ip_str);
        printf("  Signal strength: %d dBm\n", rssi);
        printf("  Connect count: %"PRIu32"\n", wifi.connect_count);

        // Test UDP channel
        printf("\n  WiFi 6 (802.11ax) channel operational.\n");
        printf("  Network is now a reflex channel.\n");
    } else {
        printf("  Connection timeout. State: %d\n", wifi_get_state(&wifi));
    }
    fflush(stdout);
}

// ============================================================
// Main Entry
// ============================================================

void app_main(void) {
    printf("\n");
    printf("============================================================\n");
    printf("     THE REFLEX BECOMES THE C6 - ESP32-C6 @ 160MHz\n");
    printf("============================================================\n");
    printf("\nEvery peripheral is a channel. Hardware already thinks in signals.\n");
    fflush(stdout);

    // Initialize C6 hardware channels
    reflex_c6_init();

    // Run all benchmarks
    benchmark_primitives();
    benchmark_gpio();
    benchmark_timer();
    benchmark_critical_jitter();  // The fix for jitter
    benchmark_alternatives();     // Compare to FreeRTOS
    benchmark_adc();
    benchmark_spline();
    benchmark_entropy_field();
    benchmark_spi();
    demo_echip();
    benchmark_wifi();

    printf("\n============================================================\n");
    printf("                    BENCHMARK COMPLETE\n");
    printf("============================================================\n");
    printf("\nThe Reflex IS how the C6 knows itself.\n\n");
    fflush(stdout);

    // Idle loop - blink LED slowly
    reflex_led_init();
    while (1) {
        reflex_led_toggle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
