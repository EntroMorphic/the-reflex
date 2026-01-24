/**
 * main.c - The Reflex Becomes the C6
 *
 * Demo showing hardware as channels:
 * - GPIO as output channels (LED toggle)
 * - Timers as periodic signal channels
 * - Full benchmark of channel primitives
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
// Test 6: SPI as Protocol Channel
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
// Test 7: WiFi as Network Channel (info only, no connect)
// ============================================================

static void benchmark_wifi(void) {
    printf("\n=== WIFI CHANNEL INFO ===\n");
    printf("\nNetwork events as channel signals.\n");
    fflush(stdout);

    // Just show the channel structure - don't actually connect
    // (Would need SSID/password to connect)

    printf("\nWiFi channel capabilities:\n");
    printf("  - Status channel: connection state changes\n");
    printf("  - Auto-reconnect on disconnect\n");
    printf("  - UDP channels for datagram I/O\n");
    printf("  - WiFi 6 (802.11ax) on ESP32-C6\n");
    printf("\nTo connect, call:\n");
    printf("  wifi_channel_init(&wifi, \"SSID\", \"password\");\n");
    printf("  wifi_wait_connected(&wifi, 10000);\n");
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
    benchmark_adc();
    benchmark_spline();
    benchmark_spi();
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
