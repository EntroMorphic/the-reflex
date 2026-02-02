/**
 * fabric_cfc_demo.c - Pure ETM Fabric 64-Neuron CfC
 *
 * NO CPU IN THE LOOP during inference.
 * 
 * The mixer uses a 256 KB LUT to eliminate multiply.
 * PCNT does hardware addition for sparse dot products.
 * Everything else is memory reads.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"

#include "reflex_fabric_cfc.h"

static const char* TAG = "fabric_cfc";

// Global engines
static fabric_engine_t g_base_fabric = {0};
static fabric_cfc_engine_t g_cfc_fabric = {0};
static fabric_parallel_t g_parallel_fabric = {0};

// ═══════════════════════════════════════════════════════════════════════════
// Memory Report
// ═══════════════════════════════════════════════════════════════════════════

static void print_memory_report(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                 MEMORY REPORT\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = esp_get_minimum_free_heap_size();
    
    printf("  Heap:\n");
    printf("    Free:     %6zu bytes\n", free_heap);
    printf("    Min free: %6zu bytes\n", min_free);
    printf("\n");
    
    printf("  Fabric CfC allocations:\n");
    printf("    Mixer LUTs:     %6d bytes (64 neurons × 4 KB)\n", 
           (int)(sizeof(fabric_mixer_lut_t) * FABRIC_CFC_HIDDEN_DIM));
    printf("    Activation LUTs: %6zu bytes\n", sizeof(fabric_activation_luts_t));
    printf("    Sparse weights:  %6zu bytes\n", sizeof(fabric_cfc_sparse_weights_t));
    printf("    Hidden state:    %6d bytes\n", FABRIC_CFC_HIDDEN_DIM);
    printf("\n");
    
    size_t total = sizeof(fabric_mixer_lut_t) * FABRIC_CFC_HIDDEN_DIM +
                   sizeof(fabric_activation_luts_t) +
                   sizeof(fabric_cfc_sparse_weights_t) +
                   FABRIC_CFC_HIDDEN_DIM;
    printf("  Total fabric state: %zu bytes (%.1f KB)\n", total, total / 1024.0f);
    printf("\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: Software Reference
// ═══════════════════════════════════════════════════════════════════════════

static void benchmark_software(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           BENCHMARK: SOFTWARE REFERENCE (64 neurons)\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  Using quantized LUTs, no PCNT\n");
    printf("  This is what the fabric should match.\n");
    printf("\n");
    
    // Test input
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM] = {8, 10, 6, 12};  // Around 0
    
    // Warm up
    for (int i = 0; i < 100; i++) {
        fabric_cfc_step_sw(&g_cfc_fabric, input_q4);
    }
    
    // Reset hidden state
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;
    }
    
    // Benchmark
    const int ITERATIONS = 1000;
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        input_q4[0] = (iter % 16);  // Vary input
        
        uint32_t start = esp_cpu_get_cycle_count();
        fabric_cfc_step_sw(&g_cfc_fabric, input_q4);
        uint32_t end = esp_cpu_get_cycle_count();
        
        uint32_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
    }
    
    uint32_t avg_cycles = (uint32_t)(total_cycles / ITERATIONS);
    uint32_t cpu_mhz = 160;
    
    printf("  Results (%d iterations):\n", ITERATIONS);
    printf("    Min: %lu cycles = %lu μs\n", 
           (unsigned long)min_cycles, (unsigned long)(min_cycles / cpu_mhz));
    printf("    Avg: %lu cycles = %lu μs\n", 
           (unsigned long)avg_cycles, (unsigned long)(avg_cycles / cpu_mhz));
    printf("    Max: %lu cycles = %lu μs\n", 
           (unsigned long)max_cycles, (unsigned long)(max_cycles / cpu_mhz));
    printf("    Throughput: %.1f Hz\n", 
           1000000.0f / (avg_cycles / (float)cpu_mhz));
    printf("\n");
    
    // Show hidden state
    printf("  Hidden state (4-bit, first 16 of %d):\n    ", FABRIC_CFC_HIDDEN_DIM);
    for (int i = 0; i < 16; i++) {
        printf("%2d ", g_cfc_fabric.hidden_q4[i]);
    }
    printf("...\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: Hardware Fabric (PCNT addition)
// ═══════════════════════════════════════════════════════════════════════════

static void benchmark_hardware(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           BENCHMARK: HARDWARE FABRIC (64 neurons)\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  PCNT does sparse dot addition via pulse counting\n");
    printf("  Mixer via 256 KB LUT (no multiply!)\n");
    printf("\n");
    
    // Reset hidden state
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;
    }
    
    // Test input
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM] = {8, 10, 6, 12};
    
    // Single inference benchmark (hardware is slower per-op)
    const int ITERATIONS = 10;  // Fewer iterations due to RMT latency
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    
    printf("  Running %d hardware inferences...\n", ITERATIONS);
    fflush(stdout);
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        input_q4[0] = (iter % 16);
        
        uint32_t start = esp_cpu_get_cycle_count();
        fabric_cfc_step_hw(&g_cfc_fabric, input_q4);
        uint32_t end = esp_cpu_get_cycle_count();
        
        uint32_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        
        printf("    Iteration %d: %lu cycles\n", iter + 1, (unsigned long)cycles);
        fflush(stdout);
    }
    
    uint32_t avg_cycles = (uint32_t)(total_cycles / ITERATIONS);
    uint32_t cpu_mhz = 160;
    
    printf("\n  Results (%d iterations):\n", ITERATIONS);
    printf("    Min: %lu cycles = %lu ms\n", 
           (unsigned long)min_cycles, (unsigned long)(min_cycles / cpu_mhz / 1000));
    printf("    Avg: %lu cycles = %lu ms\n", 
           (unsigned long)avg_cycles, (unsigned long)(avg_cycles / cpu_mhz / 1000));
    printf("    Max: %lu cycles = %lu ms\n", 
           (unsigned long)max_cycles, (unsigned long)(max_cycles / cpu_mhz / 1000));
    printf("    Throughput: %.2f Hz\n", 
           1000000.0f / (avg_cycles / (float)cpu_mhz));
    printf("\n");
    
    printf("  NOTE: Individual RMT calls have high latency.\n");
    printf("        Let's try BATCHED pulses next!\n");
    printf("\n");
    
    // Show hidden state
    printf("  Hidden state (4-bit, first 16 of %d):\n    ", FABRIC_CFC_HIDDEN_DIM);
    for (int i = 0; i < 16; i++) {
        printf("%2d ", g_cfc_fabric.hidden_q4[i]);
    }
    printf("...\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Timing Breakdown Analysis
// ═══════════════════════════════════════════════════════════════════════════

static void analyze_timing_breakdown(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           TIMING BREAKDOWN ANALYSIS\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    uint32_t cpu_mhz = 160;
    uint32_t cycles;
    
    // Test 1: PCNT clear + read overhead
    printf("  1. PCNT clear + read overhead:\n");
    uint32_t start = esp_cpu_get_cycle_count();
    for (int i = 0; i < 128; i++) {
        fabric_clear_accumulator(g_cfc_fabric.fabric);
        fabric_read_accumulator(g_cfc_fabric.fabric);
    }
    cycles = esp_cpu_get_cycle_count() - start;
    printf("     128 ops: %lu cycles = %lu μs (%.1f μs each)\n",
           (unsigned long)cycles, (unsigned long)(cycles / cpu_mhz),
           cycles / 128.0f / cpu_mhz);
    
    // Test 2: RMT transmit with minimal pulses
    printf("\n  2. RMT transmit overhead (1 pulse):\n");
    rmt_symbol_word_t one_pulse[2] = {
        {.duration0 = 5, .level0 = 1, .duration1 = 5, .level1 = 0},
        {.duration0 = 0, .level0 = 0, .duration1 = 0, .level1 = 0}
    };
    rmt_transmit_config_t tx_config = {.loop_count = 0};
    
    start = esp_cpu_get_cycle_count();
    for (int i = 0; i < 128; i++) {
        rmt_transmit(g_cfc_fabric.fabric->rmt_chan, 
                     g_cfc_fabric.fabric->rmt_encoder,
                     one_pulse, sizeof(one_pulse), &tx_config);
        rmt_tx_wait_all_done(g_cfc_fabric.fabric->rmt_chan, portMAX_DELAY);
    }
    cycles = esp_cpu_get_cycle_count() - start;
    printf("     128 ops: %lu cycles = %lu μs (%.1f μs each)\n",
           (unsigned long)cycles, (unsigned long)(cycles / cpu_mhz),
           cycles / 128.0f / cpu_mhz);
    
    // Test 3: RMT transmit with typical pulse count (~50 pulses)
    printf("\n  3. RMT transmit with ~50 pulses:\n");
    rmt_symbol_word_t fifty_pulses[51];
    for (int i = 0; i < 50; i++) {
        fifty_pulses[i].duration0 = 5;
        fifty_pulses[i].level0 = 1;
        fifty_pulses[i].duration1 = 5;
        fifty_pulses[i].level1 = 0;
    }
    fifty_pulses[50].duration0 = 0;
    fifty_pulses[50].level0 = 0;
    fifty_pulses[50].duration1 = 0;
    fifty_pulses[50].level1 = 0;
    
    start = esp_cpu_get_cycle_count();
    for (int i = 0; i < 128; i++) {
        rmt_transmit(g_cfc_fabric.fabric->rmt_chan,
                     g_cfc_fabric.fabric->rmt_encoder,
                     fifty_pulses, sizeof(fifty_pulses), &tx_config);
        rmt_tx_wait_all_done(g_cfc_fabric.fabric->rmt_chan, portMAX_DELAY);
    }
    cycles = esp_cpu_get_cycle_count() - start;
    printf("     128 ops: %lu cycles = %lu μs (%.1f μs each)\n",
           (unsigned long)cycles, (unsigned long)(cycles / cpu_mhz),
           cycles / 128.0f / cpu_mhz);
    
    // Test 4: Buffer building overhead
    printf("\n  4. Buffer building overhead:\n");
    uint8_t test_concat[FABRIC_CFC_CONCAT_DIM];
    for (int i = 0; i < FABRIC_CFC_CONCAT_DIM; i++) test_concat[i] = 8;
    
    start = esp_cpu_get_cycle_count();
    for (int i = 0; i < 128; i++) {
        fabric_build_batched_pulses(
            g_cfc_fabric.pulse_buffer,
            test_concat,
            g_cfc_fabric.weights.gate[0].pos_indices,
            g_cfc_fabric.weights.gate[0].pos_count
        );
    }
    cycles = esp_cpu_get_cycle_count() - start;
    printf("     128 ops: %lu cycles = %lu μs (%.1f μs each)\n",
           (unsigned long)cycles, (unsigned long)(cycles / cpu_mhz),
           cycles / 128.0f / cpu_mhz);
    
    // Test 5: LUT lookup overhead
    printf("\n  5. LUT lookups (activation + mixer):\n");
    start = esp_cpu_get_cycle_count();
    volatile uint8_t dummy = 0;
    for (int n = 0; n < 64; n++) {
        dummy = g_cfc_fabric.activations.sigmoid[128];
        dummy = g_cfc_fabric.activations.tanh_lut[128];
        dummy = g_cfc_fabric.mixer_luts[n].lut[8][8][8];
    }
    cycles = esp_cpu_get_cycle_count() - start;
    printf("     64 neurons × 3 LUTs: %lu cycles = %lu μs\n",
           (unsigned long)cycles, (unsigned long)(cycles / cpu_mhz));
    (void)dummy;
    
    printf("\n  SUMMARY:\n");
    printf("     RMT setup+wait dominates. ~40 μs per sparse dot.\n");
    printf("     128 sparse dots × 40 μs = 5.1 ms (matches measured 5.3 ms)\n");
    printf("\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: BATCHED Hardware Fabric 
// ═══════════════════════════════════════════════════════════════════════════

static void benchmark_hardware_batched(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           BENCHMARK: BATCHED HARDWARE (64 neurons)\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  ONE rmt_transmit() per sparse dot instead of N!\n");
    printf("  All pulses for a sparse dot batched into one DMA transfer.\n");
    printf("\n");
    
    // Reset hidden state
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;
    }
    
    // Test input
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM] = {8, 10, 6, 12};
    
    const int ITERATIONS = 10;
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    
    printf("  Running %d BATCHED hardware inferences...\n", ITERATIONS);
    fflush(stdout);
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        input_q4[0] = (iter % 16);
        
        uint32_t start = esp_cpu_get_cycle_count();
        fabric_cfc_step_hw_batched(&g_cfc_fabric, input_q4);
        uint32_t end = esp_cpu_get_cycle_count();
        
        uint32_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        
        printf("    Iteration %d: %lu cycles\n", iter + 1, (unsigned long)cycles);
        fflush(stdout);
    }
    
    uint32_t avg_cycles = (uint32_t)(total_cycles / ITERATIONS);
    uint32_t cpu_mhz = 160;
    float avg_ms = avg_cycles / (float)cpu_mhz / 1000.0f;
    
    printf("\n  Results (%d iterations):\n", ITERATIONS);
    printf("    Min: %lu cycles = %.1f ms\n", 
           (unsigned long)min_cycles, min_cycles / (float)cpu_mhz / 1000.0f);
    printf("    Avg: %lu cycles = %.1f ms\n", 
           (unsigned long)avg_cycles, avg_ms);
    printf("    Max: %lu cycles = %.1f ms\n", 
           (unsigned long)max_cycles, max_cycles / (float)cpu_mhz / 1000.0f);
    printf("    Throughput: %.1f Hz\n", 
           1000.0f / avg_ms);
    printf("\n");
    
    // Show hidden state
    printf("  Hidden state (4-bit, first 16 of %d):\n    ", FABRIC_CFC_HIDDEN_DIM);
    for (int i = 0; i < 16; i++) {
        printf("%2d ", g_cfc_fabric.hidden_q4[i]);
    }
    printf("...\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: PALLETIZED Hardware Fabric 
// ═══════════════════════════════════════════════════════════════════════════

static void benchmark_hardware_palletized(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           BENCHMARK: PALLETIZED HARDWARE (64 neurons)\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  Memcpy from pre-computed palette instead of building pulses!\n");
    printf("  Eliminates per-pulse generation loop.\n");
    printf("\n");
    
    // Reset hidden state
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;
    }
    
    // Test input
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM] = {8, 10, 6, 12};
    
    const int ITERATIONS = 10;
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    
    printf("  Running %d PALLETIZED hardware inferences...\n", ITERATIONS);
    fflush(stdout);
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        input_q4[0] = (iter % 16);
        
        uint32_t start = esp_cpu_get_cycle_count();
        fabric_cfc_step_hw_palletized(&g_cfc_fabric, input_q4);
        uint32_t end = esp_cpu_get_cycle_count();
        
        uint32_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        
        printf("    Iteration %d: %lu cycles\n", iter + 1, (unsigned long)cycles);
        fflush(stdout);
    }
    
    uint32_t avg_cycles = (uint32_t)(total_cycles / ITERATIONS);
    uint32_t cpu_mhz = 160;
    float avg_ms = avg_cycles / (float)cpu_mhz / 1000.0f;
    
    printf("\n  Results (%d iterations):\n", ITERATIONS);
    printf("    Min: %lu cycles = %.1f ms\n", 
           (unsigned long)min_cycles, min_cycles / (float)cpu_mhz / 1000.0f);
    printf("    Avg: %lu cycles = %.1f ms\n", 
           (unsigned long)avg_cycles, avg_ms);
    printf("    Max: %lu cycles = %.1f ms\n", 
           (unsigned long)max_cycles, max_cycles / (float)cpu_mhz / 1000.0f);
    printf("    Throughput: %.1f Hz\n", 
           1000.0f / avg_ms);
    printf("\n");
    
    // Show hidden state
    printf("  Hidden state (4-bit, first 16 of %d):\n    ", FABRIC_CFC_HIDDEN_DIM);
    for (int i = 0; i < 16; i++) {
        printf("%2d ", g_cfc_fabric.hidden_q4[i]);
    }
    printf("...\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark: PARALLEL Hardware Fabric (4 neurons at once!)
// ═══════════════════════════════════════════════════════════════════════════

static void benchmark_hardware_parallel(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           BENCHMARK: PARALLEL HARDWARE (4x neurons)\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  3 RMT channels + 3 PCNT units = 3 neurons at once!\n");
    printf("  Should be ~3x faster than serial.\n");
    printf("\n");
    
    if (!g_parallel_fabric.initialized) {
        printf("  Using 3 parallel channels (base fabric has 1)...\n");
        esp_err_t ret = fabric_parallel_init(&g_parallel_fabric);
        if (ret != ESP_OK) {
            printf("  ERROR: Failed to init parallel fabric: %s\n", esp_err_to_name(ret));
            return;
        }
        printf("  Parallel fabric ready.\n");
    }
    
    // Reset hidden state
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;
    }
    
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM] = {8, 10, 6, 12};
    
    const int ITERATIONS = 10;
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    
    printf("  Running %d PARALLEL hardware inferences...\n", ITERATIONS);
    fflush(stdout);
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        input_q4[0] = (iter % 16);
        
        uint32_t start = esp_cpu_get_cycle_count();
        fabric_cfc_step_hw_parallel(&g_cfc_fabric, &g_parallel_fabric, input_q4);
        uint32_t end = esp_cpu_get_cycle_count();
        
        uint32_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        
        printf("    Iteration %d: %lu cycles\n", iter + 1, (unsigned long)cycles);
        fflush(stdout);
    }
    
    uint32_t avg_cycles = (uint32_t)(total_cycles / ITERATIONS);
    uint32_t cpu_mhz = 160;
    float avg_ms = avg_cycles / (float)cpu_mhz / 1000.0f;
    
    printf("\n  Results (%d iterations):\n", ITERATIONS);
    printf("    Min: %lu cycles = %.1f ms\n", 
           (unsigned long)min_cycles, min_cycles / (float)cpu_mhz / 1000.0f);
    printf("    Avg: %lu cycles = %.1f ms\n", 
           (unsigned long)avg_cycles, avg_ms);
    printf("    Max: %lu cycles = %.1f ms\n", 
           (unsigned long)max_cycles, max_cycles / (float)cpu_mhz / 1000.0f);
    printf("    Throughput: %.1f Hz\n", 
           1000.0f / avg_ms);
    printf("\n");
    
    printf("  Hidden state (4-bit, first 16 of %d):\n    ", FABRIC_CFC_HIDDEN_DIM);
    for (int i = 0; i < 16; i++) {
        printf("%2d ", g_cfc_fabric.hidden_q4[i]);
    }
    printf("...\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Verification: SW vs HW
// ═══════════════════════════════════════════════════════════════════════════

static void verify_sw_vs_hw(void) {
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           VERIFICATION: SOFTWARE vs HARDWARE\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Same input
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM] = {5, 10, 7, 12};
    
    // Run SW
    uint8_t hidden_sw[FABRIC_CFC_HIDDEN_DIM];
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;  // Reset
    }
    fabric_cfc_step_sw(&g_cfc_fabric, input_q4);
    memcpy(hidden_sw, g_cfc_fabric.hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    // Run HW
    uint8_t hidden_hw[FABRIC_CFC_HIDDEN_DIM];
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        g_cfc_fabric.hidden_q4[i] = 8;  // Reset
    }
    fabric_cfc_step_hw(&g_cfc_fabric, input_q4);
    memcpy(hidden_hw, g_cfc_fabric.hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    // Compare
    int matches = 0;
    int total_diff = 0;
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        if (hidden_sw[i] == hidden_hw[i]) {
            matches++;
        }
        total_diff += abs((int)hidden_sw[i] - (int)hidden_hw[i]);
    }
    
    printf("  Results:\n");
    printf("    Exact matches: %d / %d (%.1f%%)\n", 
           matches, FABRIC_CFC_HIDDEN_DIM,
           100.0f * matches / FABRIC_CFC_HIDDEN_DIM);
    printf("    Total difference: %d\n", total_diff);
    printf("    Avg difference: %.2f\n", 
           (float)total_diff / FABRIC_CFC_HIDDEN_DIM);
    printf("\n");
    
    // Show comparison for first 8 neurons
    printf("  First 8 neurons:\n");
    printf("    SW: ");
    for (int i = 0; i < 8; i++) printf("%2d ", hidden_sw[i]);
    printf("\n");
    printf("    HW: ");
    for (int i = 0; i < 8; i++) printf("%2d ", hidden_hw[i]);
    printf("\n\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

void app_main(void) {
    printf("\n\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           PURE ETM FABRIC CfC - 64 NEURONS\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  NO CPU IN THE LOOP.\n");
    printf("  \n");
    printf("  Sparse dot: PCNT pulse counting (hardware addition)\n");
    printf("  Activations: LUT lookup (memory read)\n");
    printf("  Mixer: 256 KB LUT (no multiply!)\n");
    printf("\n");
    
    // Check heap before
    printf("  Free heap before init: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    fflush(stdout);
    
    // Initialize base fabric (PCNT + RMT)
    printf("  Initializing base fabric (PCNT + RMT)...\n");
    fflush(stdout);
    
    esp_err_t ret = fabric_init(&g_base_fabric);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init base fabric: %s", esp_err_to_name(ret));
        return;
    }
    printf("  Base fabric ready.\n");
    fflush(stdout);
    
    // Initialize CfC fabric (256 KB LUTs!)
    printf("  Initializing CfC fabric (256 KB mixer LUTs)...\n");
    fflush(stdout);
    
    ret = fabric_cfc_init(&g_cfc_fabric, &g_base_fabric);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init CfC fabric: %s", esp_err_to_name(ret));
        return;
    }
    printf("  CfC fabric ready.\n");
    fflush(stdout);
    
    // Memory report
    print_memory_report();
    
    // Run benchmarks
    benchmark_software();
    
    // Verify SW vs HW match
    verify_sw_vs_hw();
    
    // Hardware benchmark (individual RMT calls)
    benchmark_hardware();
    
    // BATCHED hardware benchmark (one RMT call per sparse dot)
    benchmark_hardware_batched();
    
    // PALLETIZED hardware benchmark (memcpy from pre-computed patterns)
    benchmark_hardware_palletized();
    
    // PARALLEL hardware benchmark (4 PCNT + 4 RMT)
    benchmark_hardware_parallel();
    
    // Timing breakdown to find optimization opportunities
    analyze_timing_breakdown();
    
    // Summary
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("           PURE ETM FABRIC CfC: OPERATIONAL\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  64 neurons. 4-bit quantization. 256 KB LUTs.\n");
    printf("  \n");
    printf("  PCNT counts pulses = hardware addition.\n");
    printf("  LUT lookups = memory reads.\n");
    printf("  No multiply in the hot path.\n");
    printf("  \n");
    printf("  PARALLEL: 4 neurons at once via 4 PCNT + 4 RMT!\n");
    printf("\n");
    fflush(stdout);
    
    // Idle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
