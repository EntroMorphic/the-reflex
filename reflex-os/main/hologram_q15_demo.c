/**
 * hologram_q15_demo.c - Holographic Intelligence with Yinsen Q15 CfC
 *
 * Zero floating-point in the hot path.
 * Each node IS the brain, not a part of it.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_cpu.h"
#include "driver/gpio.h"

#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_hologram_q15.h"

static const char* TAG = "hologram_q15";

#define PIN_LED     8

// ════════════════════════════════════════════════════════════════════════════
// Globals
// ════════════════════════════════════════════════════════════════════════════

static holo_q15_node_t g_node;

// ════════════════════════════════════════════════════════════════════════════
// Benchmark
// ════════════════════════════════════════════════════════════════════════════

static void run_benchmark(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                 YINSEN Q15 BENCHMARK\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Create test input in Q4.11
    int16_t input[HOLO_Q15_INPUT_DIM];
    int16_t output[HOLO_Q15_HIDDEN_DIM];
    
    for (int i = 0; i < HOLO_Q15_INPUT_DIM; i++) {
        input[i] = float_to_q11(0.5f);  // 0.5 in Q4.11
    }
    
    // Warm up
    for (int i = 0; i < 100; i++) {
        holo_q15_tick(&g_node, input, output);
    }
    
    // Benchmark
    const int ITERATIONS = 10000;
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    
    for (int i = 0; i < ITERATIONS; i++) {
        // Vary input slightly
        input[0] = float_to_q11(0.5f + (i % 100) * 0.001f);
        
        uint32_t start = esp_cpu_get_cycle_count();
        holo_q15_tick(&g_node, input, output);
        uint32_t end = esp_cpu_get_cycle_count();
        
        uint32_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
    }
    
    uint32_t avg_cycles = (uint32_t)(total_cycles / ITERATIONS);
    uint32_t cpu_mhz = 160;
    
    printf("  Benchmarking holo_q15_tick...\n");
    printf("  Results (%d iterations):\n", ITERATIONS);
    printf("    Min: %lu cycles = %lu ns\n", (unsigned long)min_cycles, (unsigned long)(min_cycles * 1000 / cpu_mhz));
    printf("    Avg: %lu cycles = %lu ns\n", (unsigned long)avg_cycles, (unsigned long)(avg_cycles * 1000 / cpu_mhz));
    printf("    Max: %lu cycles = %lu ns\n", (unsigned long)max_cycles, (unsigned long)(max_cycles * 1000 / cpu_mhz));
    printf("    Throughput: ~%lu kHz\n", (unsigned long)(1000000 / (avg_cycles * 1000 / cpu_mhz / 1000)));
    printf("\n");
    
    // Show output
    printf("  Hidden state (Q15):\n    ");
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        printf("%6d ", g_node.cfc.hidden[i]);
    }
    printf("\n\n");
    
    printf("  Hidden state (float equivalent):\n    ");
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        printf("%6.3f ", q15_to_float(g_node.cfc.hidden[i]));
    }
    printf("\n\n");
}

// ════════════════════════════════════════════════════════════════════════════
// Crystallization Test
// ════════════════════════════════════════════════════════════════════════════

static void run_crystallization_test(void) {
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                 CRYSTALLIZATION TEST\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Reset node
    holo_q15_init(&g_node, 1, esp_random());
    
    // Create a fake neighbor with similar state
    g_node.neighbors[0].active = true;
    g_node.neighbors[0].node_id = 2;
    g_node.neighbors[0].last_seen_tick = 0;
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        // Neighbor has similar hidden state (±10%)
        g_node.neighbors[0].hidden[i] = g_node.cfc.hidden[i];
    }
    g_node.neighbor_count = 1;
    
    uint8_t initial_crystallized = __builtin_popcount(g_node.crystallized_mask);
    
    // Run with consistent input (should lead to crystallization)
    int16_t input[HOLO_Q15_INPUT_DIM];
    int16_t output[HOLO_Q15_HIDDEN_DIM];
    
    for (int i = 0; i < HOLO_Q15_INPUT_DIM; i++) {
        input[i] = float_to_q11(0.3f);
    }
    
    printf("  Running 5000 ticks with 1 agreeing neighbor...\n");
    
    for (int t = 0; t < 5000; t++) {
        // Keep neighbor's state close to ours (simulating agreement)
        for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
            g_node.neighbors[0].hidden[i] = g_node.cfc.hidden[i];
        }
        g_node.neighbors[0].last_seen_tick = g_node.tick_count;
        
        holo_q15_tick(&g_node, input, output);
    }
    
    uint8_t final_crystallized = __builtin_popcount(g_node.crystallized_mask);
    
    printf("  Results:\n");
    printf("    Initial crystallized: %d neurons\n", initial_crystallized);
    printf("    Final crystallized:   %d neurons\n", final_crystallized);
    printf("    Crystallizations:     %lu\n", (unsigned long)g_node.crystallization_count);
    printf("    Confidence:           %.1f%%\n", q15_to_float(g_node.confidence) * 100.0f);
    printf("\n");
    
    // Show entropy per neuron
    printf("  Neuron entropy:\n    ");
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        printf("%3d ", g_node.neuron_entropy[i]);
    }
    printf("\n\n");
    
    printf("  Crystallized mask: 0x%02X\n", g_node.crystallized_mask);
    printf("  Potential mask:    0x%02X\n", g_node.potential_mask);
    printf("\n");
}

// ════════════════════════════════════════════════════════════════════════════
// Main Demo
// ════════════════════════════════════════════════════════════════════════════

static void demo_task(void* arg) {
    int16_t input[HOLO_Q15_INPUT_DIM];
    int16_t output[HOLO_Q15_HIDDEN_DIM];
    uint32_t tick = 0;
    
    while (1) {
        tick++;
        
        // Generate input from "sensor" (random for demo)
        for (int i = 0; i < HOLO_Q15_INPUT_DIM; i++) {
            input[i] = (int16_t)((esp_random() % 2048) - 1024);
        }
        
        // Run hologram tick
        holo_q15_tick(&g_node, input, output);
        
        // LED based on confidence
        bool led_on = (g_node.confidence > HOLO_Q15_CONF_LOW);
        gpio_write(PIN_LED, led_on ? 0 : 1);
        
        // Print status every 5 seconds
        if ((tick % 5000) == 0) {
            holo_q15_stats_t stats = holo_q15_get_stats(&g_node);
            
            printf("\n");
            printf("═══════════════════════════════════════════════════════════════\n");
            printf("  Q15 HOLOGRAM STATUS (tick %lu)\n", (unsigned long)stats.tick_count);
            printf("═══════════════════════════════════════════════════════════════\n");
            printf("  Neighbors:     %d\n", stats.neighbor_count);
            printf("  Confidence:    %.1f%% (Q15: %d)\n", stats.confidence_f * 100.0f, stats.confidence_q15);
            printf("  Crystallized:  %d neurons\n", stats.crystallized_count);
            printf("  Potential:     %d neurons\n", stats.potential_count);
            printf("\n");
            printf("  Hidden (Q15):  ");
            for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
                printf("%6d ", g_node.cfc.hidden[i]);
            }
            printf("\n");
            printf("  Hidden (f):    ");
            for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
                printf("%6.3f ", q15_to_float(g_node.cfc.hidden[i]));
            }
            printf("\n");
            printf("═══════════════════════════════════════════════════════════════\n\n");
            fflush(stdout);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Entry Point
// ════════════════════════════════════════════════════════════════════════════

void app_main(void) {
    printf("\n\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("       HOLOGRAPHIC INTELLIGENCE - YINSEN Q15 DEMO\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  Zero floating-point in the hot path.\n");
    printf("  Q15 format: 1.0 = 32767\n");
    printf("  Sparse ternary weights: {-1, 0, +1}\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize GPIO
    gpio_set_output(PIN_LED);
    gpio_write(PIN_LED, 1);
    
    // Initialize hologram node
    printf("  Initializing Q15 hologram node...\n");
    holo_q15_init(&g_node, 1, esp_random());
    printf("    Node size: %lu bytes\n", (unsigned long)sizeof(holo_q15_node_t));
    printf("    CfC size:  %lu bytes\n", (unsigned long)sizeof(holo_cfc_q15_t));
    printf("\n");
    fflush(stdout);
    
    // Run benchmark
    run_benchmark();
    
    // Run crystallization test
    run_crystallization_test();
    
    // Start live demo
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                      LIVE DEMO\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    fflush(stdout);
    
    xTaskCreate(demo_task, "hologram_q15", 4096, NULL, 5, NULL);
}
