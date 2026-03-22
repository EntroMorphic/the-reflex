/*
 * latency_benchmark.c - Reflex Coordination Latency Benchmark
 *
 * Measures pure coordination latency without control loop overhead.
 * Compares reflex against futex for baseline.
 *
 * Build:
 *   gcc -O3 -Wall -pthread latency_benchmark.c -o latency_benchmark -lm
 *
 * Run:
 *   ./latency_benchmark
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "reflex.h"

// ============================================================================
// Configuration
// ============================================================================

#define ITERATIONS      100000
#define WARMUP_ITERS    10000
#define PRODUCER_CORE   0
#define CONSUMER_CORE   1

// ============================================================================
// Reflex Benchmark
// ============================================================================

static reflex_channel_t reflex_ch;
static uint64_t reflex_latencies[ITERATIONS];

typedef struct {
    int ready;
    int done;
} sync_state_t;

static sync_state_t sync_state = {0};

void* reflex_producer(void* arg) {
    (void)arg;
    reflex_pin_to_core(PRODUCER_CORE);

    // Wait for consumer
    while (!__atomic_load_n(&sync_state.ready, __ATOMIC_ACQUIRE)) {
        reflex_compiler_barrier();
    }

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        reflex_signal(&reflex_ch, reflex_rdtsc());
        while (reflex_ch.sequence != (uint64_t)(i + 1) * 2) {
            reflex_compiler_barrier();
        }
    }

    // Measured
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t start = reflex_rdtsc();
        reflex_signal(&reflex_ch, start);

        // Wait for ack (consumer increments sequence again)
        while (reflex_ch.sequence != (uint64_t)(WARMUP_ITERS + i + 1) * 2) {
            reflex_compiler_barrier();
        }
        uint64_t end = reflex_rdtsc();

        reflex_latencies[i] = end - start;
    }

    __atomic_store_n(&sync_state.done, 1, __ATOMIC_RELEASE);
    return NULL;
}

void* reflex_consumer(void* arg) {
    (void)arg;
    reflex_pin_to_core(CONSUMER_CORE);

    __atomic_store_n(&sync_state.ready, 1, __ATOMIC_RELEASE);

    uint64_t expected_seq = 1;

    while (!__atomic_load_n(&sync_state.done, __ATOMIC_ACQUIRE)) {
        // Wait for signal
        while (reflex_ch.sequence < expected_seq) {
            if (__atomic_load_n(&sync_state.done, __ATOMIC_ACQUIRE)) return NULL;
            reflex_compiler_barrier();
        }

        // Ack by incrementing sequence
        reflex_ch.sequence++;
        reflex_memory_barrier();
        expected_seq += 2;
    }

    return NULL;
}

// ============================================================================
// Futex Benchmark
// ============================================================================

static int futex_val;
static uint64_t futex_latencies[ITERATIONS];

static inline long futex_wait(int* addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

static inline long futex_wake(int* addr) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, 1, NULL, NULL, 0);
}

void* futex_producer(void* arg) {
    (void)arg;
    reflex_pin_to_core(PRODUCER_CORE);

    sync_state.ready = 0;
    sync_state.done = 0;

    while (!__atomic_load_n(&sync_state.ready, __ATOMIC_ACQUIRE)) {
        reflex_compiler_barrier();
    }

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        __atomic_store_n(&futex_val, 1, __ATOMIC_RELEASE);
        futex_wake(&futex_val);
        while (__atomic_load_n(&futex_val, __ATOMIC_ACQUIRE) != 2) {
            futex_wait(&futex_val, 1);
        }
        __atomic_store_n(&futex_val, 0, __ATOMIC_RELEASE);
    }

    // Measured
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t start = reflex_rdtsc();

        __atomic_store_n(&futex_val, 1, __ATOMIC_RELEASE);
        futex_wake(&futex_val);

        while (__atomic_load_n(&futex_val, __ATOMIC_ACQUIRE) != 2) {
            futex_wait(&futex_val, 1);
        }

        uint64_t end = reflex_rdtsc();
        futex_latencies[i] = end - start;

        __atomic_store_n(&futex_val, 0, __ATOMIC_RELEASE);
    }

    __atomic_store_n(&sync_state.done, 1, __ATOMIC_RELEASE);
    return NULL;
}

void* futex_consumer(void* arg) {
    (void)arg;
    reflex_pin_to_core(CONSUMER_CORE);

    __atomic_store_n(&sync_state.ready, 1, __ATOMIC_RELEASE);

    while (!__atomic_load_n(&sync_state.done, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&futex_val, __ATOMIC_ACQUIRE) != 1) {
            if (__atomic_load_n(&sync_state.done, __ATOMIC_ACQUIRE)) return NULL;
            futex_wait(&futex_val, 0);
        }
        __atomic_store_n(&futex_val, 2, __ATOMIC_RELEASE);
        futex_wake(&futex_val);
    }

    return NULL;
}

// ============================================================================
// Statistics
// ============================================================================

typedef struct {
    double min, max, mean, median, p99, stddev;
} stats_t;

int cmp_u64(const void* a, const void* b) {
    uint64_t va = *(const uint64_t*)a;
    uint64_t vb = *(const uint64_t*)b;
    return (va > vb) - (va < vb);
}

stats_t compute_stats(uint64_t* data, int n, double ticks_per_ns) {
    stats_t s = {0};

    uint64_t* sorted = malloc(n * sizeof(uint64_t));
    memcpy(sorted, data, n * sizeof(uint64_t));
    qsort(sorted, n, sizeof(uint64_t), cmp_u64);

    s.min = sorted[0] / ticks_per_ns;
    s.max = sorted[n-1] / ticks_per_ns;
    s.median = sorted[n/2] / ticks_per_ns;
    s.p99 = sorted[(int)(n * 0.99)] / ticks_per_ns;

    double sum = 0;
    for (int i = 0; i < n; i++) sum += sorted[i] / ticks_per_ns;
    s.mean = sum / n;

    double var = 0;
    for (int i = 0; i < n; i++) {
        double v = sorted[i] / ticks_per_ns - s.mean;
        var += v * v;
    }
    s.stddev = sqrt(var / n);

    free(sorted);
    return s;
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       REFLEX ROBOTICS: LATENCY BENCHMARK                      ║\n");
    printf("║       Reflex vs Futex Round-Trip Latency                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    uint64_t freq = reflex_get_freq();
    double ticks_per_ns = (double)freq / 1e9;

    printf("Configuration:\n");
    printf("  Counter frequency: %lu Hz\n", freq);
    printf("  Iterations: %d (+ %d warmup)\n", ITERATIONS, WARMUP_ITERS);
    printf("  Producer core: %d\n", PRODUCER_CORE);
    printf("  Consumer core: %d\n", CONSUMER_CORE);
    printf("\n");

    // ─────────────────────────────────────────────────────────────────────────
    // Reflex Benchmark
    // ─────────────────────────────────────────────────────────────────────────

    printf("Running Reflex benchmark...\n");

    reflex_init(&reflex_ch);
    sync_state.ready = 0;
    sync_state.done = 0;

    pthread_t prod, cons;
    pthread_create(&cons, NULL, reflex_consumer, NULL);
    pthread_create(&prod, NULL, reflex_producer, NULL);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    stats_t reflex_stats = compute_stats(reflex_latencies, ITERATIONS, ticks_per_ns);

    // ─────────────────────────────────────────────────────────────────────────
    // Futex Benchmark
    // ─────────────────────────────────────────────────────────────────────────

    printf("Running Futex benchmark...\n");

    futex_val = 0;
    sync_state.ready = 0;
    sync_state.done = 0;

    pthread_create(&cons, NULL, futex_consumer, NULL);
    pthread_create(&prod, NULL, futex_producer, NULL);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    stats_t futex_stats = compute_stats(futex_latencies, ITERATIONS, ticks_per_ns);

    // ─────────────────────────────────────────────────────────────────────────
    // Results
    // ─────────────────────────────────────────────────────────────────────────

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                    RESULTS (nanoseconds)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("┌────────────┬──────────┬──────────┬──────────┬──────────┐\n");
    printf("│ Mechanism  │  Median  │   Mean   │   P99    │  Stddev  │\n");
    printf("├────────────┼──────────┼──────────┼──────────┼──────────┤\n");
    printf("│ Reflex     │ %8.1f │ %8.1f │ %8.1f │ %8.1f │\n",
           reflex_stats.median, reflex_stats.mean, reflex_stats.p99, reflex_stats.stddev);
    printf("│ Futex      │ %8.1f │ %8.1f │ %8.1f │ %8.1f │\n",
           futex_stats.median, futex_stats.mean, futex_stats.p99, futex_stats.stddev);
    printf("└────────────┴──────────┴──────────┴──────────┴──────────┘\n\n");

    double speedup = futex_stats.median / reflex_stats.median;

    printf("Speedup: Reflex is %.1fx faster than Futex\n\n", speedup);

    printf("Implications for Robotics:\n");
    printf("  Futex-based coordination: ~%.0f ns → max ~%.0f kHz control\n",
           futex_stats.median, 1e9 / futex_stats.median / 1000);
    printf("  Reflex coordination:      ~%.0f ns → max ~%.0f kHz control\n",
           reflex_stats.median, 1e9 / reflex_stats.median / 1000);
    printf("\n");

    printf("═══════════════════════════════════════════════════════════════\n");
    if (speedup > 10) {
        printf("  ✓ REFLEX: %.0fx faster coordination\n", speedup);
        printf("  Sub-microsecond robotics coordination achieved.\n");
    } else {
        printf("  Reflex: %.1fx speedup over Futex\n", speedup);
    }
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
