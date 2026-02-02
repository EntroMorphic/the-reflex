/**
 * cfc_demo.c - Binary Ternary CfC Demo
 *
 * Demonstrates the liquid neural network running on bare metal.
 * NO MULTIPLY - just AND, POPCOUNT, SUB, LUT.
 *
 * Benchmarks:
 *   - Original: byte-wise operations
 *   - Optimized: 64-bit operations
 *   - DMA Pipelined: prefetch hints + cache-friendly layout
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_cfc.h"
#include "reflex_cfc_dma.h"
#include "reflex_cfc_turbo.h"

// ============================================================
// Configuration
// ============================================================

#define PIN_LED             8
#define NUM_TEST_CYCLES     1000
#define BENCHMARK_CYCLES    10000

// ============================================================
// Demo
// ============================================================

// Global CfC layers
static cfc_layer_t cfc;
static cfc_dma_layer_t cfc_dma;
static cfc_turbo_layer_t cfc_turbo;

// Simple delay
static inline void delay_ms(uint32_t ms) {
    uint32_t start = reflex_cycles();
    while ((reflex_cycles() - start) < ms * 160000);
}

void app_main(void) {
    gpio_set_output(PIN_LED);
    gpio_write(PIN_LED, 1);
    
    printf("\n");
    printf("================================================================\n");
    printf("         BINARY TERNARY CfC: LIQUID NEURAL NETWORK             \n");
    printf("================================================================\n");
    printf("\n");
    printf("  NO MULTIPLY. Just AND, POPCOUNT, SUB, LUT.\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize CfC
    printf("  Initializing CfC layer...\n");
    cfc_init_random(&cfc, reflex_cycles());
    
    cfc_stats_t stats = cfc_get_stats(&cfc);
    printf("\n");
    printf("  CfC Statistics:\n");
    printf("    Total memory:   %lu bytes\n", (unsigned long)stats.total_bytes);
    printf("    Weight memory:  %lu bytes\n", (unsigned long)stats.weight_bytes);
    printf("    State memory:   %lu bytes\n", (unsigned long)stats.state_bytes);
    printf("    Neurons:        %lu\n", (unsigned long)stats.num_neurons);
    printf("    Sparsity:       %lu%%\n", (unsigned long)stats.sparsity_percent);
    printf("\n");
    fflush(stdout);
    
    // Test forward pass
    printf("  Testing forward pass...\n");
    
    uint8_t input[8] = {0};
    uint8_t output[8] = {0};
    
    // Run a few forward passes with different inputs
    printf("\n  Input → Output:\n");
    
    for (int test = 0; test < 8; test++) {
        // Create test input
        memset(input, 0, 8);
        input[0] = (1 << test);  // Single bit set
        
        // Forward pass
        cfc_forward(&cfc, input, output);
        
        printf("    0x%02x → 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
               input[0],
               output[7], output[6], output[5], output[4],
               output[3], output[2], output[1], output[0]);
    }
    printf("\n");
    fflush(stdout);
    
    // Test hidden state dynamics (liquid behavior)
    printf("  Testing liquid dynamics (hidden state evolution)...\n\n");
    
    // Reset hidden state
    memset(cfc.hidden, 0, 8);
    
    // Feed constant input and watch hidden state evolve
    input[0] = 0xAA;  // Alternating bits
    input[1] = 0x55;
    
    printf("  Constant input 0x55AA, hidden state over time:\n");
    for (int t = 0; t < 10; t++) {
        cfc_forward(&cfc, input, output);
        printf("    t=%d: hidden=0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
               t,
               cfc.hidden[7], cfc.hidden[6], cfc.hidden[5], cfc.hidden[4],
               cfc.hidden[3], cfc.hidden[2], cfc.hidden[1], cfc.hidden[0]);
    }
    printf("\n");
    fflush(stdout);
    
    // Initialize DMA layer
    printf("  Initializing DMA-optimized layer...\n");
    cfc_dma_init_random(&cfc_dma, reflex_cycles() ^ 0xDEADBEEF);
    printf("    DMA layer size: %lu bytes\n", (unsigned long)cfc_dma_memory_size());
    
    // Initialize TURBO layer
    printf("  Initializing TURBO layer (transposed weights)...\n");
    cfc_turbo_init_random(&cfc_turbo, reflex_cycles() ^ 0xCAFEBABE);
    printf("    TURBO layer size: %lu bytes\n\n", (unsigned long)cfc_turbo_memory_size());
    fflush(stdout);
    
    // Benchmark
    printf("================================================================\n");
    printf("                    BENCHMARK                                   \n");
    printf("================================================================\n");
    printf("\n");
    
    uint64_t input64 = 0x55AA55AA55AA55AAULL;
    uint64_t output64 = 0;
    
    // Warm up all versions
    for (int i = 0; i < 100; i++) {
        cfc_forward(&cfc, input, output);
        cfc_forward_original(&cfc, input, output);
        cfc_dma_forward_pipelined(&cfc_dma, input64, &output64);
        cfc_turbo_forward(&cfc_turbo, input64, &output64);
    }
    
    // Benchmark ORIGINAL forward pass
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t sum_cycles = 0;
    
    for (int i = 0; i < BENCHMARK_CYCLES; i++) {
        input[0] = (uint8_t)i;
        
        uint32_t t0 = reflex_cycles();
        cfc_forward_original(&cfc, input, output);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles_orig = (uint32_t)(sum_cycles / BENCHMARK_CYCLES);
    uint32_t ns_per_forward_orig = reflex_cycles_to_ns(avg_cycles_orig);
    uint32_t forwards_per_sec_orig = ns_per_forward_orig > 0 ? 1000000000 / ns_per_forward_orig : 0;
    
    printf("  1. ORIGINAL (byte-wise):\n");
    printf("     Avg: %lu cycles = %lu ns = %lu kHz\n",
           (unsigned long)avg_cycles_orig,
           (unsigned long)ns_per_forward_orig,
           (unsigned long)(forwards_per_sec_orig / 1000));
    fflush(stdout);
    
    // Benchmark OPTIMIZED forward pass (64-bit)
    min_cycles = UINT32_MAX;
    max_cycles = 0;
    sum_cycles = 0;
    
    for (int i = 0; i < BENCHMARK_CYCLES; i++) {
        input[0] = (uint8_t)i;
        
        uint32_t t0 = reflex_cycles();
        cfc_forward(&cfc, input, output);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles_opt = (uint32_t)(sum_cycles / BENCHMARK_CYCLES);
    uint32_t ns_per_forward_opt = reflex_cycles_to_ns(avg_cycles_opt);
    uint32_t forwards_per_sec_opt = ns_per_forward_opt > 0 ? 1000000000 / ns_per_forward_opt : 0;
    
    printf("  2. OPTIMIZED (64-bit ops):\n");
    printf("     Avg: %lu cycles = %lu ns = %lu kHz\n",
           (unsigned long)avg_cycles_opt,
           (unsigned long)ns_per_forward_opt,
           (unsigned long)(forwards_per_sec_opt / 1000));
    fflush(stdout);
    
    // Benchmark DMA PIPELINED forward pass
    min_cycles = UINT32_MAX;
    max_cycles = 0;
    sum_cycles = 0;
    
    for (int i = 0; i < BENCHMARK_CYCLES; i++) {
        input64 = (uint64_t)i | 0x55AA000000000000ULL;
        
        uint32_t t0 = reflex_cycles();
        cfc_dma_forward_pipelined(&cfc_dma, input64, &output64);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles_dma = (uint32_t)(sum_cycles / BENCHMARK_CYCLES);
    uint32_t ns_per_forward_dma = reflex_cycles_to_ns(avg_cycles_dma);
    uint32_t forwards_per_sec_dma = ns_per_forward_dma > 0 ? 1000000000 / ns_per_forward_dma : 0;
    
    printf("  3. DMA PIPELINED (prefetch + cache-friendly):\n");
    printf("     Avg: %lu cycles = %lu ns = %lu kHz\n",
           (unsigned long)avg_cycles_dma,
           (unsigned long)ns_per_forward_dma,
           (unsigned long)(forwards_per_sec_dma / 1000));
    fflush(stdout);
    
    // Benchmark DMA with double-buffered prefetch simulation
    min_cycles = UINT32_MAX;
    max_cycles = 0;
    sum_cycles = 0;
    
    for (int i = 0; i < BENCHMARK_CYCLES; i++) {
        input64 = (uint64_t)i | 0x55AA000000000000ULL;
        
        uint32_t t0 = reflex_cycles();
        cfc_dma_forward(&cfc_dma, input64, &output64);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles_dma2 = (uint32_t)(sum_cycles / BENCHMARK_CYCLES);
    uint32_t ns_per_forward_dma2 = reflex_cycles_to_ns(avg_cycles_dma2);
    uint32_t forwards_per_sec_dma2 = ns_per_forward_dma2 > 0 ? 1000000000 / ns_per_forward_dma2 : 0;
    
    printf("  4. DMA DOUBLE-BUFFERED (simulated):\n");
    printf("     Avg: %lu cycles = %lu ns = %lu kHz\n",
           (unsigned long)avg_cycles_dma2,
           (unsigned long)ns_per_forward_dma2,
           (unsigned long)(forwards_per_sec_dma2 / 1000));
    fflush(stdout);
    
    // Benchmark TURBO forward pass (bit-parallel, approximate)
    min_cycles = UINT32_MAX;
    max_cycles = 0;
    sum_cycles = 0;
    
    for (int i = 0; i < BENCHMARK_CYCLES; i++) {
        input64 = (uint64_t)i | 0x55AA000000000000ULL;
        
        uint32_t t0 = reflex_cycles();
        cfc_turbo_forward(&cfc_turbo, input64, &output64);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles_turbo = (uint32_t)(sum_cycles / BENCHMARK_CYCLES);
    uint32_t ns_per_forward_turbo = reflex_cycles_to_ns(avg_cycles_turbo);
    uint32_t forwards_per_sec_turbo = ns_per_forward_turbo > 0 ? 1000000000 / ns_per_forward_turbo : 0;
    
    printf("  5. TURBO (bit-parallel, approximate):\n");
    printf("     Avg: %lu cycles = %lu ns = %lu kHz\n",
           (unsigned long)avg_cycles_turbo,
           (unsigned long)ns_per_forward_turbo,
           (unsigned long)(forwards_per_sec_turbo / 1000));
    fflush(stdout);
    
    // Benchmark TURBO PRECISE forward pass
    min_cycles = UINT32_MAX;
    max_cycles = 0;
    sum_cycles = 0;
    
    for (int i = 0; i < BENCHMARK_CYCLES; i++) {
        input64 = (uint64_t)i | 0x55AA000000000000ULL;
        
        uint32_t t0 = reflex_cycles();
        cfc_turbo_forward_precise(&cfc_turbo, input64, &output64);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles_turbo_p = (uint32_t)(sum_cycles / BENCHMARK_CYCLES);
    uint32_t ns_per_forward_turbo_p = reflex_cycles_to_ns(avg_cycles_turbo_p);
    uint32_t forwards_per_sec_turbo_p = ns_per_forward_turbo_p > 0 ? 1000000000 / ns_per_forward_turbo_p : 0;
    
    printf("  6. TURBO PRECISE (bit-parallel, exact):\n");
    printf("     Avg: %lu cycles = %lu ns = %lu kHz\n",
           (unsigned long)avg_cycles_turbo_p,
           (unsigned long)ns_per_forward_turbo_p,
           (unsigned long)(forwards_per_sec_turbo_p / 1000));
    printf("\n");
    fflush(stdout);
    
    // Summary comparison
    printf("  SPEEDUP vs Original:\n");
    printf("    Optimized:      %.2fx\n", (float)ns_per_forward_orig / (float)ns_per_forward_opt);
    printf("    DMA Pipelined:  %.2fx\n", (float)ns_per_forward_orig / (float)ns_per_forward_dma);
    printf("    DMA Double-Buf: %.2fx\n", (float)ns_per_forward_orig / (float)ns_per_forward_dma2);
    printf("    TURBO:          %.2fx\n", (float)ns_per_forward_orig / (float)ns_per_forward_turbo);
    printf("    TURBO PRECISE:  %.2fx\n", (float)ns_per_forward_orig / (float)ns_per_forward_turbo_p);
    printf("\n");
    fflush(stdout);
    
    // Use best result for summary
    uint32_t ns_per_forward = ns_per_forward_turbo;
    if (ns_per_forward_opt < ns_per_forward) ns_per_forward = ns_per_forward_opt;
    if (ns_per_forward_dma < ns_per_forward) ns_per_forward = ns_per_forward_dma;
    uint32_t forwards_per_sec = ns_per_forward > 0 ? 1000000000 / ns_per_forward : 0;
    
    // Benchmark individual operations
    printf("  Operation breakdown:\n");
    
    // Benchmark popcount
    uint8_t temp[8] = {0xFF, 0xAA, 0x55, 0x00, 0xFF, 0xAA, 0x55, 0x00};
    uint32_t t0 = reflex_cycles();
    volatile uint8_t pc = 0;
    for (int i = 0; i < 10000; i++) {
        pc = popcount64(temp);
    }
    uint32_t popcount_cycles = (reflex_cycles() - t0) / 10000;
    printf("    popcount64:      %lu cycles = %lu ns\n",
           (unsigned long)popcount_cycles,
           (unsigned long)reflex_cycles_to_ns(popcount_cycles));
    
    // Benchmark AND
    uint8_t a[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t b[8] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    uint8_t c[8];
    t0 = reflex_cycles();
    for (int i = 0; i < 10000; i++) {
        and64(a, b, c);
    }
    uint32_t and_cycles = (reflex_cycles() - t0) / 10000;
    printf("    and64:           %lu cycles = %lu ns\n",
           (unsigned long)and_cycles,
           (unsigned long)reflex_cycles_to_ns(and_cycles));
    
    // Benchmark ternary neuron
    t0 = reflex_cycles();
    volatile uint8_t pre = 0;
    for (int i = 0; i < 10000; i++) {
        pre = ternary_neuron(input, &cfc.f_weights[0], cfc.f_bias[0]);
    }
    uint32_t neuron_cycles = (reflex_cycles() - t0) / 10000;
    printf("    ternary_neuron:  %lu cycles = %lu ns\n",
           (unsigned long)neuron_cycles,
           (unsigned long)reflex_cycles_to_ns(neuron_cycles));
    
    // Benchmark sigmoid LUT
    t0 = reflex_cycles();
    volatile uint8_t sig = 0;
    for (int i = 0; i < 10000; i++) {
        sig = sigmoid_lut[64 + (i & 63)];
    }
    uint32_t sigmoid_cycles = (reflex_cycles() - t0) / 10000;
    printf("    sigmoid LUT:     %lu cycles = %lu ns\n",
           (unsigned long)sigmoid_cycles,
           (unsigned long)reflex_cycles_to_ns(sigmoid_cycles));
    
    (void)pc; (void)pre; (void)sig;  // Suppress unused warnings
    
    printf("\n");
    fflush(stdout);
    
    // Summary
    printf("================================================================\n");
    printf("                       SUMMARY                                  \n");
    printf("================================================================\n");
    printf("\n");
    printf("  Binary Ternary CfC:\n");
    printf("    - %lu bytes total\n", (unsigned long)stats.total_bytes);
    printf("    - %lu neurons\n", (unsigned long)stats.num_neurons);
    printf("    - %lu%% sparse\n", (unsigned long)stats.sparsity_percent);
    printf("    - %lu ns per forward pass (best)\n", (unsigned long)ns_per_forward);
    printf("    - ~%lu kHz effective frequency\n", (unsigned long)(forwards_per_sec / 1000));
    printf("\n");
    printf("  Operations used: AND, POPCOUNT, SUB, LUT\n");
    printf("  Operations NOT used: MULTIPLY, DIVIDE, FLOAT\n");
    printf("\n");
    printf("  This is a LIQUID NEURAL NETWORK running on bare metal.\n");
    printf("  The hidden state flows continuously through time.\n");
    printf("  Trained version could predict, filter, or control.\n");
    printf("\n");
    printf("================================================================\n");
    printf("\n");
    fflush(stdout);
    
    // Heartbeat
    printf("  Heartbeat active...\n");
    fflush(stdout);
    
    while (1) {
        gpio_toggle(PIN_LED);
        delay_ms(500);
    }
}
