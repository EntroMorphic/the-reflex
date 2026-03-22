/*
 * platform.h - Cross-platform support for stigmergy experiments
 *
 * Supports: ARM64 (Raspberry Pi, Jetson, Apple Silicon)
 *           x86_64 (Intel, AMD, Colab)
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <time.h>

// ============================================================================
// Architecture Detection
// ============================================================================

#if defined(__aarch64__) || defined(__arm64__)
    #define ARCH_ARM64 1
    #define ARCH_NAME "ARM64"
#elif defined(__x86_64__) || defined(__i386__)
    #define ARCH_X86 1
    #define ARCH_NAME "x86_64"
#else
    #error "Unsupported architecture. Need ARM64 or x86_64."
#endif

// ============================================================================
// High-Resolution Timing
// ============================================================================

#ifdef ARCH_ARM64

static inline uint64_t rdtsc(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline uint64_t get_freq(void) {
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

#else  // ARCH_X86

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t get_freq(void) {
    // x86 doesn't have a direct frequency register
    // Calibrate using clock_gettime
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t tsc_start = rdtsc();

    // Busy wait ~10ms
    volatile uint64_t dummy = 0;
    for (int i = 0; i < 10000000; i++) dummy++;

    uint64_t tsc_end = rdtsc();
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t ns = (end.tv_sec - start.tv_sec) * 1000000000ULL +
                  (end.tv_nsec - start.tv_nsec);

    // Return estimated frequency
    return (tsc_end - tsc_start) * 1000000000ULL / ns;
}

#endif

// ============================================================================
// Memory Barriers
// ============================================================================

#ifdef ARCH_ARM64
    #define memory_barrier()  __asm__ volatile("dsb sy" ::: "memory")
    #define read_barrier()    __asm__ volatile("dsb ld" ::: "memory")
    #define write_barrier()   __asm__ volatile("dsb st" ::: "memory")
#else  // ARCH_X86
    #define memory_barrier()  __asm__ volatile("mfence" ::: "memory")
    #define read_barrier()    __asm__ volatile("lfence" ::: "memory")
    #define write_barrier()   __asm__ volatile("sfence" ::: "memory")
#endif

// ============================================================================
// Compiler Barrier
// ============================================================================

#define compiler_barrier() __asm__ volatile("" ::: "memory")

// ============================================================================
// Cache Line Size
// ============================================================================

#define CACHE_LINE_SIZE 64

// ============================================================================
// Timing Helpers
// ============================================================================

static inline double ticks_to_ns(uint64_t ticks, uint64_t freq) {
    return (double)ticks * 1e9 / freq;
}

static inline void print_platform_info(void) {
    uint64_t freq = get_freq();
    printf("Platform: %s\n", ARCH_NAME);
    printf("Counter frequency: %lu Hz (%.2f GHz)\n", freq, freq / 1e9);
}

#endif // PLATFORM_H
