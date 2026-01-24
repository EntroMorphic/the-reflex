/**
 * main.c - Reflex OS Demo for ESP32-C6
 * Simplified benchmark - no qsort, minimal stats
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"

#include "reflex.h"
#include "channels.h"

#define NUM_SAMPLES 1000

void app_main(void) {
    printf("\n============================================================\n");
    printf("        REFLEX OS - ESP32-C6 @ 160MHz\n");
    printf("============================================================\n");
    fflush(stdout);

    // Test 1: Cycle counter overhead
    printf("\nTest 1: Cycle counter overhead\n");
    fflush(stdout);

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t t1 = reflex_cycles();
        uint32_t diff = t1 - t0;
        if (diff < min) min = diff;
        if (diff > max) max = diff;
        sum += diff;
    }

    printf("  min=%" PRIu32 " cycles (%" PRIu32 " ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%" PRIu32 " cycles (%" PRIu32 " ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%" PRIu32 " cycles (%" PRIu32 " ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);

    // Test 2: Signal latency
    printf("\nTest 2: reflex_signal() latency\n");
    fflush(stdout);

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

    printf("  min=%" PRIu32 " cycles (%" PRIu32 " ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%" PRIu32 " cycles (%" PRIu32 " ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%" PRIu32 " cycles (%" PRIu32 " ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);

    // Test 3: Channel roundtrip
    printf("\nTest 3: Channel signal+read roundtrip\n");
    fflush(stdout);

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

    printf("  min=%" PRIu32 " cycles (%" PRIu32 " ns)\n", min, reflex_cycles_to_ns(min));
    printf("  max=%" PRIu32 " cycles (%" PRIu32 " ns)\n", max, reflex_cycles_to_ns(max));
    printf("  avg=%" PRIu32 " cycles (%" PRIu32 " ns)\n",
           (uint32_t)(sum/NUM_SAMPLES), reflex_cycles_to_ns((uint32_t)(sum/NUM_SAMPLES)));
    fflush(stdout);

    printf("\n============================================================\n");
    printf("BENCHMARK COMPLETE - The Reflex OS\n");
    printf("============================================================\n\n");
    fflush(stdout);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
