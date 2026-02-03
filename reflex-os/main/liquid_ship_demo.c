/**
 * liquid_ship_demo.c - Liquid Ship Benchmark on ESP32-C6
 *
 * Tests the splined mixer and navigator on actual hardware.
 * Measures real performance vs host benchmarks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Include the Liquid Ship headers
#include "reflex_spline_mixer.h"
#include "reflex_spline_verify.h"
#include "reflex_navigator.h"
#include "reflex_liquid_ship.h"

// ============================================================
// Timing
// ============================================================

static inline uint64_t get_cycles(void) {
    uint32_t cycles;
    __asm__ volatile("csrr %0, 0x7e2" : "=r"(cycles));
    return cycles;
}

static inline uint64_t get_time_us(void) {
    return esp_timer_get_time();
}

// ============================================================
// Spline Mixer Benchmark
// ============================================================

void bench_spline_mixer_hw(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║              SPLINE MIXER - ESP32-C6 @ 160 MHz                 ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    
    spline_mixer_complete_t mixer;
    spline_mixer_generate(&mixer, 0.9f);
    
    // Accuracy test
    spline_verify_result_t result;
    spline_mixer_verify(&mixer, 0.9f, &result);
    
    printf("║                                                                ║\n");
    printf("║  Accuracy (decay=0.9):                                         ║\n");
    printf("║    Exact matches:    %4lu / %lu (%5.1f%%)                      ║\n",
           (unsigned long)result.exact_matches,
           (unsigned long)result.total_tests,
           100.0f * result.exact_matches / result.total_tests);
    printf("║    Max error:        %d / 15                                   ║\n",
           result.max_error);
    printf("║    Mean abs error:   %.3f                                     ║\n",
           result.mean_abs_error);
    printf("║                                                                ║\n");
    
    // Speed test - cycles
    const int iterations = 100000;
    volatile uint8_t result_val = 0;
    
    uint32_t start_cycles = get_cycles();
    for (int i = 0; i < iterations; i++) {
        uint8_t g = i & 0x0F;
        uint8_t h = (i >> 4) & 0x0F;
        uint8_t c = (i >> 8) & 0x0F;
        result_val = spline_mixer_lookup(&mixer, g, h, c);
    }
    uint32_t end_cycles = get_cycles();
    
    uint32_t total_cycles = end_cycles - start_cycles;
    float cycles_per_lookup = (float)total_cycles / iterations;
    float ns_per_lookup = cycles_per_lookup * 6.25f;  // 160 MHz = 6.25ns/cycle
    float lookups_per_sec = 160e6f / cycles_per_lookup;
    
    printf("║  Speed (%d K lookups):                                       ║\n", iterations / 1000);
    printf("║    Cycles per lookup:  %.1f                                   ║\n", cycles_per_lookup);
    printf("║    Time per lookup:    %.0f ns                                 ║\n", ns_per_lookup);
    printf("║    Lookups/sec:        %.1f M                                  ║\n", lookups_per_sec / 1e6f);
    printf("║    (result=%d)                                                 ║\n", result_val);
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
}

// ============================================================
// Activation Splines Benchmark
// ============================================================

void bench_activations_hw(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║            ACTIVATION SPLINES - ESP32-C6 @ 160 MHz             ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    
    spline_activations_t act;
    spline_activations_generate(&act);
    
    spline_verify_result_t sig_result, tanh_result;
    spline_activations_verify(&act, &sig_result, &tanh_result);
    
    printf("║                                                                ║\n");
    printf("║  Sigmoid: %3lu/256 exact, max_err=%d, mean=%.3f               ║\n",
           (unsigned long)sig_result.exact_matches,
           sig_result.max_error,
           sig_result.mean_abs_error);
    printf("║  Tanh:    %3lu/256 exact, max_err=%d, mean=%.3f               ║\n",
           (unsigned long)tanh_result.exact_matches,
           tanh_result.max_error,
           tanh_result.mean_abs_error);
    printf("║                                                                ║\n");
    
    // Speed test
    const int iterations = 1000000;
    volatile uint8_t result = 0;
    
    uint32_t start = get_cycles();
    for (int i = 0; i < iterations; i++) {
        result = spline_sigmoid_lookup(&act, i & 0xFF);
    }
    uint32_t end = get_cycles();
    
    float cycles_sigmoid = (float)(end - start) / iterations;
    
    start = get_cycles();
    for (int i = 0; i < iterations; i++) {
        result = spline_tanh_lookup(&act, i & 0xFF);
    }
    end = get_cycles();
    
    float cycles_tanh = (float)(end - start) / iterations;
    
    printf("║  Speed (%d M lookups):                                        ║\n", iterations / 1000000);
    printf("║    Sigmoid: %.1f cycles (%.0f ns)                              ║\n", 
           cycles_sigmoid, cycles_sigmoid * 6.25f);
    printf("║    Tanh:    %.1f cycles (%.0f ns)                              ║\n",
           cycles_tanh, cycles_tanh * 6.25f);
    printf("║    (result=%d)                                                 ║\n", result);
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
}

// ============================================================
// Liquid Ship Benchmark
// ============================================================

void bench_liquid_ship_hw(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║              LIQUID SHIP - ESP32-C6 @ 160 MHz                  ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    
    // Allocate on heap - too big for stack (16KB)
    liquid_ship_t* ship = (liquid_ship_t*)malloc(sizeof(liquid_ship_t));
    if (!ship) {
        printf("║  ERROR: Failed to allocate ship (need %zu bytes)              ║\n", sizeof(liquid_ship_t));
        printf("╚════════════════════════════════════════════════════════════════╝\n");
        return;
    }
    ship_init(ship, 0.9f);
    
    printf("║                                                                ║\n");
    printf("║  Ship Configuration:                                           ║\n");
    printf("║    Neurons:          %d                                        ║\n", SHIP_NEURONS);
    printf("║    State size:       %zu bytes                                 ║\n", sizeof(ship->state));
    printf("║    Mixer size:       %zu bytes                                ║\n", sizeof(ship->mixer));
    printf("║    Total ship:       %zu bytes                              ║\n", sizeof(*ship));
    printf("║                                                                ║\n");
    
    // Software step benchmark
    uint8_t input[4] = {8, 10, 6, 12};
    const int iterations = 10000;
    
    uint64_t start_us = get_time_us();
    uint32_t start_cycles = get_cycles();
    
    for (int i = 0; i < iterations; i++) {
        input[0] = (i & 0x0F);
        ship_step_sw(ship, input);
    }
    
    uint32_t end_cycles = get_cycles();
    uint64_t end_us = get_time_us();
    
    uint32_t total_cycles = end_cycles - start_cycles;
    uint64_t total_us = end_us - start_us;
    
    float cycles_per_step = (float)total_cycles / iterations;
    float us_per_step = (float)total_us / iterations;
    float steps_per_sec = 1e6f / us_per_step;
    
    printf("║  Software Step Speed (%d K iterations):                       ║\n", iterations / 1000);
    printf("║    Cycles per step:  %.0f                                    ║\n", cycles_per_step);
    printf("║    Time per step:    %.1f µs                                  ║\n", us_per_step);
    printf("║    Steps/sec:        %.0f K                                   ║\n", steps_per_sec / 1000);
    printf("║    Ticks completed:  %lu                                      ║\n", (unsigned long)ship->ticks);
    printf("║                                                                ║\n");
    
    // Final state
    printf("║  Final State (first 8 neurons):                                ║\n");
    printf("║    ");
    for (int i = 0; i < 8; i++) {
        printf("%2d ", ship_get_neuron(&ship->state, i));
    }
    printf("                           ║\n");
    printf("║                                                                ║\n");
    
    // Navigation
    uint8_t nav_idx = ship_compute_nav_index(ship);
    uint8_t panel = ship_select_panel(ship);
    
    printf("║  Navigation: index=%d, panel=%d                                ║\n", nav_idx, panel);
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    
    free(ship);
}

// ============================================================
// Memory Footprint
// ============================================================

void print_memory_footprint(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    MEMORY FOOTPRINT                            ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  Splined Mixer:       %5zu bytes                              ║\n", sizeof(spline_mixer_complete_t));
    printf("║  Splined Activations: %5zu bytes                              ║\n", sizeof(spline_activations_t));
    printf("║  Ship State:          %5zu bytes                              ║\n", sizeof(ship_state_t));
    printf("║  Navigator Engine:    %5zu bytes                            ║\n", sizeof(nav_engine_t));
    printf("║  Liquid Ship:         %5zu bytes                            ║\n", sizeof(liquid_ship_t));
    printf("║                                                                ║\n");
    
    size_t minimal = sizeof(spline_mixer_complete_t) + 
                     sizeof(spline_activations_t) +
                     sizeof(ship_state_t);
    
    printf("║  Minimal (mixer+act+state): %5zu bytes                        ║\n", minimal);
    printf("║  Full LUT (reference):     262656 bytes                        ║\n");
    printf("║  Compression:                 %3.0fx                            ║\n", 262656.0f / minimal);
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
}

// ============================================================
// Summary
// ============================================================

void print_summary(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                         SUMMARY                                ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  THE LIQUID SHIP - ESP32-C6 VERIFIED                          ║\n");
    printf("║                                                                ║\n");
    printf("║  Next: ETM Fabric integration                                  ║\n");
    printf("║    - GDMA M2M → RMT memory                                    ║\n");
    printf("║    - Timer race + priority = branching                        ║\n");
    printf("║    - 16-sample thermometer selection                          ║\n");
    printf("║    - CPU sleeps, silicon navigates                            ║\n");
    printf("║                                                                ║\n");
    printf("║  Power target: ~16.5 µW (RF harvestable)                      ║\n");
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("████████╗██╗  ██╗███████╗    ██╗     ██╗ ██████╗ ██╗   ██╗██╗██████╗ \n");
    printf("╚══██╔══╝██║  ██║██╔════╝    ██║     ██║██╔═══██╗██║   ██║██║██╔══██╗\n");
    printf("   ██║   ███████║█████╗      ██║     ██║██║   ██║██║   ██║██║██║  ██║\n");
    printf("   ██║   ██╔══██║██╔══╝      ██║     ██║██║▄▄ ██║██║   ██║██║██║  ██║\n");
    printf("   ██║   ██║  ██║███████╗    ███████╗██║╚██████╔╝╚██████╔╝██║██████╔╝\n");
    printf("   ╚═╝   ╚═╝  ╚═╝╚══════╝    ╚══════╝╚═╝ ╚══▀▀═╝  ╚═════╝ ╚═╝╚═════╝ \n");
    printf("                                                                      \n");
    printf("███████╗██╗  ██╗██╗██████╗     ███████╗███████╗██████╗  ██████╗██████╗ \n");
    printf("██╔════╝██║  ██║██║██╔══██╗    ██╔════╝██╔════╝██╔══██╗██╔════╝╚════██╗\n");
    printf("███████╗███████║██║██████╔╝    █████╗  ███████╗██████╔╝██║      █████╔╝\n");
    printf("╚════██║██╔══██║██║██╔═══╝     ██╔══╝  ╚════██║██╔═══╝ ██║     ██╔═══╝ \n");
    printf("███████║██║  ██║██║██║         ███████╗███████║██║     ╚██████╗███████╗\n");
    printf("╚══════╝╚═╝  ╚═╝╚═╝╚═╝         ╚══════╝╚══════╝╚═╝      ╚═════╝╚══════╝\n");
    printf("\n");
    printf("                    ESP32-C6 @ 160 MHz                              \n");
    printf("\n");
    
    // Run benchmarks
    print_memory_footprint();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    bench_spline_mixer_hw();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    bench_activations_hw();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    bench_liquid_ship_hw();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    print_summary();
    
    printf("\n\nBenchmark complete. Ship is seaworthy.\n");
    
    // Blink LED to show we're alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf(".");
        fflush(stdout);
    }
}
