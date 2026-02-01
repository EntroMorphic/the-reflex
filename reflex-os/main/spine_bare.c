/**
 * spine_bare.c - THE PURE SPINE
 *
 * Minimal external dependencies. Uses direct register access
 * for GPIO and timing, but ESP-IDF printf for serial during
 * transition to full bare metal.
 *
 * Dependencies:
 *   - <stdint.h>     (types only, no functions)
 *   - <stdio.h>      (printf - transitional, to be removed)
 *   - reflex.h       (direct CSR access for cycle counter)
 *   - reflex_gpio.h  (direct register access)
 *
 * Next step: Replace printf with reflex_uart.h
 */

#include <stdint.h>
#include <stdio.h>
#include "reflex.h"
#include "reflex_gpio.h"

// ============================================================
// Configuration
// ============================================================

#define PIN_LED             8           // Onboard LED
#define FORCE_THRESHOLD     128         // 50% anomaly rate
#define BENCHMARK_SAMPLES   10000       // Per test
#define ADVERSARIAL_SAMPLES 100000      // Stress test

// ============================================================
// Bare Metal Primitives
// ============================================================

// Fast PRNG (xorshift32)
static uint32_t prng_state = 0x12345678;

static inline uint32_t fast_random(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

// Delay in cycles (busy wait)
static inline void delay_cycles(uint32_t cycles) {
    uint32_t start = reflex_cycles();
    while ((reflex_cycles() - start) < cycles) {
        __asm__ volatile("nop");
    }
}

// Delay in milliseconds
static inline void delay_ms(uint32_t ms) {
    delay_cycles(ms * 160000);  // 160MHz = 160K cycles/ms
}

// Critical section (disable interrupts)
static inline uint32_t enter_critical(void) {
    uint32_t mstatus;
    __asm__ volatile(
        "csrrci %0, mstatus, 0x8"  // Clear MIE, return old
        : "=r"(mstatus) :: "memory"
    );
    return mstatus;
}

static inline void exit_critical(uint32_t saved) {
    __asm__ volatile(
        "csrw mstatus, %0"
        :: "r"(saved) : "memory"
    );
}

// ============================================================
// Test Results
// ============================================================

typedef struct {
    const char* name;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t avg_cycles;
    uint32_t samples;
} test_result_t;

static void print_result(test_result_t* r) {
    printf("  %-16s %4lu cy = %4lu ns  (%lu-%lu ns)\n",
           r->name,
           (unsigned long)r->avg_cycles,
           (unsigned long)reflex_cycles_to_ns(r->avg_cycles),
           (unsigned long)reflex_cycles_to_ns(r->min_cycles),
           (unsigned long)reflex_cycles_to_ns(r->max_cycles));
    fflush(stdout);
}

// ============================================================
// Benchmarks
// ============================================================

// Test 1: Baseline (PRNG + threshold + GPIO)
static test_result_t test_baseline(void) {
    test_result_t r = {.name = "Baseline", .min_cycles = UINT32_MAX};
    uint64_t sum = 0;
    
    prng_state = reflex_cycles();
    uint32_t saved = enter_critical();
    
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        uint8_t anomaly = (force > FORCE_THRESHOLD);
        gpio_write(PIN_LED, !anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
    }
    
    exit_critical(saved);
    r.avg_cycles = (uint32_t)(sum / BENCHMARK_SAMPLES);
    r.samples = BENCHMARK_SAMPLES;
    return r;
}

// Test 2: No GPIO (isolate GPIO cost)
static test_result_t test_no_gpio(void) {
    test_result_t r = {.name = "No GPIO", .min_cycles = UINT32_MAX};
    uint64_t sum = 0;
    volatile uint8_t sink = 0;
    
    prng_state = reflex_cycles();
    uint32_t saved = enter_critical();
    
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        uint8_t anomaly = (force > FORCE_THRESHOLD);
        sink = anomaly;  // Prevent optimization
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
    }
    
    exit_critical(saved);
    r.avg_cycles = (uint32_t)(sum / BENCHMARK_SAMPLES);
    r.samples = BENCHMARK_SAMPLES;
    (void)sink;
    return r;
}

// Test 3: Pure decision (no PRNG, no GPIO - just threshold)
static test_result_t test_pure_decision(void) {
    test_result_t r = {.name = "Pure decision", .min_cycles = UINT32_MAX};
    uint64_t sum = 0;
    volatile uint8_t sink = 0;
    
    uint32_t saved = enter_critical();
    
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = 100;  // Constant
        uint8_t anomaly = (force > FORCE_THRESHOLD);
        sink = anomaly;
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
    }
    
    exit_critical(saved);
    r.avg_cycles = (uint32_t)(sum / BENCHMARK_SAMPLES);
    r.samples = BENCHMARK_SAMPLES;
    (void)sink;
    return r;
}

// Test 4: With channel signal
static reflex_channel_t test_channel;
static test_result_t test_with_channel(void) {
    test_result_t r = {.name = "With channel", .min_cycles = UINT32_MAX};
    uint64_t sum = 0;
    
    // Zero the channel
    test_channel.sequence = 0;
    test_channel.value = 0;
    
    prng_state = reflex_cycles();
    uint32_t saved = enter_critical();
    
    for (int i = 0; i < BENCHMARK_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        uint8_t anomaly = (force > FORCE_THRESHOLD);
        gpio_write(PIN_LED, !anomaly);
        reflex_signal(&test_channel, anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
    }
    
    exit_critical(saved);
    r.avg_cycles = (uint32_t)(sum / BENCHMARK_SAMPLES);
    r.samples = BENCHMARK_SAMPLES;
    return r;
}

// Test 5: Adversarial (interrupts ON)
static test_result_t test_adversarial(void) {
    test_result_t r = {.name = "Adversarial", .min_cycles = UINT32_MAX};
    uint64_t sum = 0;
    uint32_t spikes = 0;
    
    prng_state = reflex_cycles();
    
    // NO critical section - interrupts stay on
    for (int i = 0; i < ADVERSARIAL_SAMPLES; i++) {
        uint32_t t0 = reflex_cycles();
        uint32_t force = fast_random() & 0xFF;
        uint8_t anomaly = (force > FORCE_THRESHOLD);
        gpio_write(PIN_LED, !anomaly);
        uint32_t cycles = reflex_cycles() - t0;
        
        if (cycles < r.min_cycles) r.min_cycles = cycles;
        if (cycles > r.max_cycles) r.max_cycles = cycles;
        sum += cycles;
        if (cycles > 1000) spikes++;  // >6μs
    }
    
    r.avg_cycles = (uint32_t)(sum / ADVERSARIAL_SAMPLES);
    r.samples = ADVERSARIAL_SAMPLES;
    return r;
}

// ============================================================
// Main Entry Point
// ============================================================

/**
 * Bare metal entry point.
 * Called after minimal startup code.
 */
void app_main(void) {
    // Initialize LED
    gpio_set_output(PIN_LED);
    gpio_write(PIN_LED, 1);  // LED on = alive
    
    // Banner
    printf("\n");
    printf("================================================================\n");
    printf("         THE REFLEX: BARE METAL SPINE                          \n");
    printf("================================================================\n");
    printf("\n");
    printf("  Direct register access. Minimal dependencies.\n");
    printf("\n");
    printf("  What's bare metal:\n");
    printf("    - reflex_cycles() -> direct CSR 0x7e2 read\n");
    printf("    - gpio_write()    -> direct GPIO register\n");
    printf("    - gpio_set_output() -> direct IO_MUX register\n");
    printf("\n");
    printf("  Still using ESP-IDF:\n");
    printf("    - printf (to be replaced with direct UART)\n");
    printf("    - bootloader (to be replaced with minimal startup)\n");
    printf("\n");
    fflush(stdout);
    
    // Test cycle counter
    printf("  Cycle counter test: ");
    uint32_t c1 = reflex_cycles();
    for (volatile int i = 0; i < 100; i++);
    uint32_t c2 = reflex_cycles();
    if (c2 > c1) {
        printf("OK (delta=%lu cycles)\n", (unsigned long)(c2 - c1));
    } else {
        printf("FAIL\n");
    }
    printf("\n");
    fflush(stdout);
    
    // Run benchmarks
    printf("================================================================\n");
    printf("                    FALSIFICATION SUITE                         \n");
    printf("================================================================\n");
    printf("\n");
    printf("  Test             Avg        Range\n");
    printf("  ------------------------------------------------\n");
    fflush(stdout);
    
    test_result_t r;
    
    r = test_baseline();      print_result(&r);
    r = test_no_gpio();       print_result(&r);
    r = test_pure_decision(); print_result(&r);
    r = test_with_channel();  print_result(&r);
    
    printf("\n");
    printf("  Analysis:\n");
    printf("    Baseline - No GPIO = GPIO write cost\n");
    printf("    No GPIO - Pure = PRNG cost\n");
    printf("    With channel - Baseline = Fence cost\n");
    printf("\n");
    fflush(stdout);
    
    // Adversarial test
    printf("================================================================\n");
    printf("           ADVERSARIAL: INTERRUPTS ENABLED                      \n");
    printf("================================================================\n");
    printf("\n");
    printf("  Running %d samples with interrupts ON...\n", ADVERSARIAL_SAMPLES);
    fflush(stdout);
    
    r = test_adversarial();
    
    printf("\n");
    printf("  Results:\n");
    printf("    Avg: %lu ns\n", (unsigned long)reflex_cycles_to_ns(r.avg_cycles));
    printf("    Min: %lu ns\n", (unsigned long)reflex_cycles_to_ns(r.min_cycles));
    printf("    Max: %lu ns\n", (unsigned long)reflex_cycles_to_ns(r.max_cycles));
    printf("\n");
    fflush(stdout);
    
    // Summary
    printf("================================================================\n");
    printf("                       SUMMARY                                  \n");
    printf("================================================================\n");
    printf("\n");
    printf("  The Reflex uses direct register access for timing and GPIO.\n");
    printf("  The silicon doesn't care about your framework.\n");
    printf("\n");
    printf("  reflex_cycles(): direct CSR 0x7e2 (no esp_cpu.h)\n");
    printf("  gpio_write():    direct register (no driver/gpio.h)\n");
    printf("\n");
    printf("================================================================\n");
    printf("\n");
    fflush(stdout);
    
    // Heartbeat
    printf("  Heartbeat active (LED blink)...\n");
    fflush(stdout);
    
    while (1) {
        gpio_toggle(PIN_LED);
        delay_ms(500);
    }
}

// ============================================================
// Minimal Startup (if not using ESP-IDF bootloader)
// ============================================================

#ifdef REFLEX_MINIMAL_STARTUP

// Linker symbols
extern uint32_t _sbss, _ebss;
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _stack_top;

// Reset handler
void __attribute__((naked, section(".text.reset"))) _start(void) {
    // Set stack pointer
    __asm__ volatile("la sp, _stack_top");
    
    // Zero BSS
    uint32_t* p;
    for (p = &_sbss; p < &_ebss; p++) *p = 0;
    
    // Copy data from flash
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    
    // Jump to main
    app_main();
    
    // Hang
    while (1) __asm__ volatile("wfi");
}

// Trap handler (minimal)
void __attribute__((interrupt)) trap_handler(void) {
    // Just hang for now
    while (1);
}

#endif // REFLEX_MINIMAL_STARTUP
