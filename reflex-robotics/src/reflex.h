/*
 * reflex.h - Cache Coherency Coordination Primitive
 *
 * Sub-microsecond inter-core signaling without syscalls.
 * Uses cache line invalidation as the coordination mechanism.
 *
 * Usage:
 *   reflex_channel_t ch;
 *   reflex_init(&ch);
 *
 *   // Producer
 *   reflex_signal(&ch, timestamp);
 *
 *   // Consumer
 *   uint64_t ts = reflex_wait(&ch, last_seq);
 */

#ifndef REFLEX_H
#define REFLEX_H

#define _GNU_SOURCE
#include <stdint.h>
#include <stdatomic.h>
#include <sched.h>
#include <pthread.h>

// ============================================================================
// Platform Detection
// ============================================================================

#if defined(__aarch64__) || defined(__arm64__)
    #define REFLEX_ARM64 1
#elif defined(__x86_64__) || defined(__i386__)
    #define REFLEX_X86 1
#else
    #error "Unsupported architecture"
#endif

// ============================================================================
// Timing
// ============================================================================

static inline uint64_t reflex_rdtsc(void) {
#ifdef REFLEX_ARM64
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#endif
}

static inline uint64_t reflex_get_freq(void) {
#ifdef REFLEX_ARM64
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#else
    return 2400000000ULL;  // Assume 2.4GHz
#endif
}

static inline void reflex_memory_barrier(void) {
#ifdef REFLEX_ARM64
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("mfence" ::: "memory");
#endif
}

static inline void reflex_compiler_barrier(void) {
    __asm__ volatile("" ::: "memory");
}

// ============================================================================
// Core Pinning
// ============================================================================

static inline int reflex_pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// ============================================================================
// Reflex Channel
// ============================================================================

typedef struct {
    volatile uint64_t sequence;    // Monotonically increasing
    volatile uint64_t timestamp;   // Producer's timestamp
    volatile uint64_t value;       // Optional payload
    char padding[40];              // Pad to 64 bytes
} __attribute__((aligned(64))) reflex_channel_t;

static inline void reflex_init(reflex_channel_t* ch) {
    ch->sequence = 0;
    ch->timestamp = 0;
    ch->value = 0;
}

// Producer: Signal with timestamp
static inline void reflex_signal(reflex_channel_t* ch, uint64_t ts) {
    ch->timestamp = ts;
    ch->sequence++;
    reflex_memory_barrier();
}

// Producer: Signal with timestamp and value
static inline void reflex_signal_value(reflex_channel_t* ch, uint64_t ts, uint64_t value) {
    ch->timestamp = ts;
    ch->value = value;
    ch->sequence++;
    reflex_memory_barrier();
}

// Consumer: Wait for new signal (spinning)
static inline uint64_t reflex_wait(reflex_channel_t* ch, uint64_t last_seq) {
    while (ch->sequence == last_seq) {
        reflex_compiler_barrier();
    }
    return ch->sequence;
}

// Consumer: Wait with timeout (returns 0 if timeout)
static inline uint64_t reflex_wait_timeout(reflex_channel_t* ch, uint64_t last_seq, uint64_t timeout_cycles) {
    uint64_t start = reflex_rdtsc();
    while (ch->sequence == last_seq) {
        if (reflex_rdtsc() - start > timeout_cycles) {
            return 0;
        }
        reflex_compiler_barrier();
    }
    return ch->sequence;
}

// Consumer: Non-blocking check
static inline int reflex_poll(reflex_channel_t* ch, uint64_t last_seq) {
    return ch->sequence != last_seq;
}

// Get timestamp from last signal
static inline uint64_t reflex_get_timestamp(reflex_channel_t* ch) {
    return ch->timestamp;
}

// Get value from last signal
static inline uint64_t reflex_get_value(reflex_channel_t* ch) {
    return ch->value;
}

// ============================================================================
// Latency Measurement
// ============================================================================

typedef struct {
    uint64_t signal_ts;
    uint64_t detect_ts;
    uint64_t latency_ticks;
} reflex_measurement_t;

static inline void reflex_measure_latency(reflex_measurement_t* m,
                                          uint64_t signal_ts,
                                          uint64_t detect_ts) {
    m->signal_ts = signal_ts;
    m->detect_ts = detect_ts;
    m->latency_ticks = detect_ts - signal_ts;
}

static inline double reflex_ticks_to_ns(uint64_t ticks) {
    return (double)ticks / ((double)reflex_get_freq() / 1e9);
}

#endif // REFLEX_H
