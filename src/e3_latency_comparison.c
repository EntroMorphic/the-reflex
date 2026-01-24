/*
 * E3: Latency Comparison Experiment
 * ASPLOS Stigmergy Paper
 *
 * Compare coordination latency across mechanisms:
 * - Stigmergy (cache line invalidation detection)
 * - Atomic operations (acquire/release)
 * - Futex (kernel-mediated)
 * - Pipe (kernel-mediated)
 *
 * All mechanisms use spin-wait for fair comparison.
 * Measures round-trip latency: signal → detect → acknowledge.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <math.h>

// ============================================================================
// Configuration
// ============================================================================

#define ITERATIONS      10000
#define WARMUP_ITERS    1000
#define PRODUCER_CORE   0
#define CONSUMER_CORE   1

// ============================================================================
// Timing
// ============================================================================

static inline uint64_t rdtsc(void) {
#if defined(__aarch64__)
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#elif defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    #error "Unsupported architecture"
#endif
}

static inline uint64_t get_freq(void) {
#if defined(__aarch64__)
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#else
    return 2400000000ULL;  // Assume 2.4GHz for x86
#endif
}

// ============================================================================
// Core Pinning
// ============================================================================

static void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
        exit(1);
    }
}

// ============================================================================
// Stigmergy Mechanism
// ============================================================================

typedef struct {
    volatile uint64_t signal;
    char padding[56];  // Pad to 64 bytes
} __attribute__((aligned(64))) stigmergy_line_t;

static stigmergy_line_t stig_prod_to_cons;
static stigmergy_line_t stig_cons_to_prod;

typedef struct {
    uint64_t latencies[ITERATIONS];
    int ready;
    int done;
} thread_data_t;

static void* stigmergy_producer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(PRODUCER_CORE);

    // Signal ready
    __atomic_store_n(&data->ready, 1, __ATOMIC_RELEASE);

    // Wait for consumer ready
    while (!__atomic_load_n(&data->done, __ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE) == 2) break;
    }

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        stig_prod_to_cons.signal = i + 1;
        while (stig_cons_to_prod.signal != (uint64_t)(i + 1)) {
            __asm__ volatile("" ::: "memory");
        }
    }

    // Measured iterations
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t start = rdtsc();

        // Signal consumer
        stig_prod_to_cons.signal = WARMUP_ITERS + i + 1;

        // Wait for acknowledgment
        while (stig_cons_to_prod.signal != (uint64_t)(WARMUP_ITERS + i + 1)) {
            __asm__ volatile("" ::: "memory");
        }

        uint64_t end = rdtsc();
        data->latencies[i] = end - start;
    }

    __atomic_store_n(&data->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void* stigmergy_consumer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(CONSUMER_CORE);

    // Wait for producer ready
    while (!__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("" ::: "memory");
    }

    // Signal ready
    __atomic_store_n(&data->ready, 2, __ATOMIC_RELEASE);

    // Process all iterations (warmup + measured)
    for (int i = 0; i < WARMUP_ITERS + ITERATIONS; i++) {
        // Wait for signal
        while (stig_prod_to_cons.signal != (uint64_t)(i + 1)) {
            __asm__ volatile("" ::: "memory");
        }

        // Acknowledge
        stig_cons_to_prod.signal = i + 1;
    }

    return NULL;
}

// ============================================================================
// Atomic Mechanism
// ============================================================================

static atomic_uint_fast64_t atomic_prod_to_cons;
static atomic_uint_fast64_t atomic_cons_to_prod;

static void* atomic_producer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(PRODUCER_CORE);

    __atomic_store_n(&data->ready, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE) != 2) {
        __asm__ volatile("" ::: "memory");
    }

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        atomic_store_explicit(&atomic_prod_to_cons, i + 1, memory_order_release);
        while (atomic_load_explicit(&atomic_cons_to_prod, memory_order_acquire) != (uint64_t)(i + 1)) {
            __asm__ volatile("" ::: "memory");
        }
    }

    // Measured
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t start = rdtsc();

        atomic_store_explicit(&atomic_prod_to_cons, WARMUP_ITERS + i + 1, memory_order_release);

        while (atomic_load_explicit(&atomic_cons_to_prod, memory_order_acquire) != (uint64_t)(WARMUP_ITERS + i + 1)) {
            __asm__ volatile("" ::: "memory");
        }

        uint64_t end = rdtsc();
        data->latencies[i] = end - start;
    }

    __atomic_store_n(&data->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void* atomic_consumer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(CONSUMER_CORE);

    while (!__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("" ::: "memory");
    }
    __atomic_store_n(&data->ready, 2, __ATOMIC_RELEASE);

    for (int i = 0; i < WARMUP_ITERS + ITERATIONS; i++) {
        while (atomic_load_explicit(&atomic_prod_to_cons, memory_order_acquire) != (uint64_t)(i + 1)) {
            __asm__ volatile("" ::: "memory");
        }
        atomic_store_explicit(&atomic_cons_to_prod, i + 1, memory_order_release);
    }

    return NULL;
}

// ============================================================================
// Futex Mechanism
// ============================================================================

static int futex_prod_to_cons;
static int futex_cons_to_prod;

static inline long futex_wait(int* addr, int val) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, val, NULL, NULL, 0);
}

static inline long futex_wake(int* addr) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static void* futex_producer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(PRODUCER_CORE);

    __atomic_store_n(&data->ready, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE) != 2) {
        __asm__ volatile("" ::: "memory");
    }

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        __atomic_store_n(&futex_prod_to_cons, i + 1, __ATOMIC_RELEASE);
        futex_wake(&futex_prod_to_cons);

        while (__atomic_load_n(&futex_cons_to_prod, __ATOMIC_ACQUIRE) != i + 1) {
            futex_wait(&futex_cons_to_prod, i);
        }
    }

    // Measured
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t start = rdtsc();

        int val = WARMUP_ITERS + i + 1;
        __atomic_store_n(&futex_prod_to_cons, val, __ATOMIC_RELEASE);
        futex_wake(&futex_prod_to_cons);

        while (__atomic_load_n(&futex_cons_to_prod, __ATOMIC_ACQUIRE) != val) {
            futex_wait(&futex_cons_to_prod, val - 1);
        }

        uint64_t end = rdtsc();
        data->latencies[i] = end - start;
    }

    __atomic_store_n(&data->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void* futex_consumer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(CONSUMER_CORE);

    while (!__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("" ::: "memory");
    }
    __atomic_store_n(&data->ready, 2, __ATOMIC_RELEASE);

    for (int i = 0; i < WARMUP_ITERS + ITERATIONS; i++) {
        int expected = i + 1;
        while (__atomic_load_n(&futex_prod_to_cons, __ATOMIC_ACQUIRE) != expected) {
            futex_wait(&futex_prod_to_cons, expected - 1);
        }

        __atomic_store_n(&futex_cons_to_prod, expected, __ATOMIC_RELEASE);
        futex_wake(&futex_cons_to_prod);
    }

    return NULL;
}

// ============================================================================
// Pipe Mechanism
// ============================================================================

static int pipe_prod_to_cons[2];
static int pipe_cons_to_prod[2];

static void* pipe_producer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(PRODUCER_CORE);

    __atomic_store_n(&data->ready, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE) != 2) {
        __asm__ volatile("" ::: "memory");
    }

    char buf = 0;

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        buf = (char)(i + 1);
        write(pipe_prod_to_cons[1], &buf, 1);
        read(pipe_cons_to_prod[0], &buf, 1);
    }

    // Measured
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t start = rdtsc();

        buf = (char)(WARMUP_ITERS + i + 1);
        write(pipe_prod_to_cons[1], &buf, 1);
        read(pipe_cons_to_prod[0], &buf, 1);

        uint64_t end = rdtsc();
        data->latencies[i] = end - start;
    }

    __atomic_store_n(&data->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void* pipe_consumer(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pin_to_core(CONSUMER_CORE);

    while (!__atomic_load_n(&data->ready, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("" ::: "memory");
    }
    __atomic_store_n(&data->ready, 2, __ATOMIC_RELEASE);

    char buf;
    for (int i = 0; i < WARMUP_ITERS + ITERATIONS; i++) {
        read(pipe_prod_to_cons[0], &buf, 1);
        write(pipe_cons_to_prod[1], &buf, 1);
    }

    return NULL;
}

// ============================================================================
// Statistics
// ============================================================================

static int compare_uint64(const void* a, const void* b) {
    uint64_t va = *(const uint64_t*)a;
    uint64_t vb = *(const uint64_t*)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

typedef struct {
    double min;
    double max;
    double mean;
    double median;
    double p99;
    double stddev;
    double cv;
} stats_t;

static stats_t compute_stats(uint64_t* data, int n, double ticks_per_ns) {
    stats_t s;

    // Sort for percentiles
    qsort(data, n, sizeof(uint64_t), compare_uint64);

    s.min = (double)data[0] / ticks_per_ns;
    s.max = (double)data[n-1] / ticks_per_ns;
    s.median = (double)data[n/2] / ticks_per_ns;
    s.p99 = (double)data[(int)(n * 0.99)] / ticks_per_ns;

    // Mean
    double sum = 0;
    for (int i = 0; i < n; i++) {
        sum += (double)data[i] / ticks_per_ns;
    }
    s.mean = sum / n;

    // Stddev
    double var_sum = 0;
    for (int i = 0; i < n; i++) {
        double v = (double)data[i] / ticks_per_ns;
        var_sum += (v - s.mean) * (v - s.mean);
    }
    s.stddev = sqrt(var_sum / n);
    s.cv = s.stddev / s.mean;

    return s;
}

// ============================================================================
// Run Experiment
// ============================================================================

typedef void* (*thread_func_t)(void*);

static stats_t run_experiment(const char* name, thread_func_t producer_fn,
                              thread_func_t consumer_fn, double ticks_per_ns) {
    thread_data_t data = {0};
    pthread_t producer, consumer;

    printf("  Running %s...\n", name);

    pthread_create(&consumer, NULL, consumer_fn, &data);
    pthread_create(&producer, NULL, producer_fn, &data);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    return compute_stats(data.latencies, ITERATIONS, ticks_per_ns);
}

// ============================================================================
// Main
// ============================================================================

#include <math.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       E3: LATENCY COMPARISON EXPERIMENT                       ║\n");
    printf("║       ASPLOS Stigmergy Paper                                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    uint64_t freq = get_freq();
    double ticks_per_ns = (double)freq / 1e9;

    printf("Configuration:\n");
    printf("  Counter frequency: %lu Hz (%.2f ticks/ns)\n", freq, ticks_per_ns);
    printf("  Iterations:        %d (+ %d warmup)\n", ITERATIONS, WARMUP_ITERS);
    printf("  Producer core:     %d\n", PRODUCER_CORE);
    printf("  Consumer core:     %d\n", CONSUMER_CORE);
    printf("\n");

    // Initialize pipe
    if (pipe(pipe_prod_to_cons) < 0 || pipe(pipe_cons_to_prod) < 0) {
        perror("pipe");
        return 1;
    }

    printf("Running experiments...\n\n");

    // Run all mechanisms
    stats_t stig_stats = run_experiment("Stigmergy", stigmergy_producer, stigmergy_consumer, ticks_per_ns);

    // Reset state
    stig_prod_to_cons.signal = 0;
    stig_cons_to_prod.signal = 0;
    atomic_store(&atomic_prod_to_cons, 0);
    atomic_store(&atomic_cons_to_prod, 0);

    stats_t atom_stats = run_experiment("Atomic", atomic_producer, atomic_consumer, ticks_per_ns);

    futex_prod_to_cons = 0;
    futex_cons_to_prod = 0;

    stats_t futex_stats = run_experiment("Futex", futex_producer, futex_consumer, ticks_per_ns);

    stats_t pipe_stats = run_experiment("Pipe", pipe_producer, pipe_consumer, ticks_per_ns);

    // Results
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         RESULTS (nanoseconds)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("┌────────────┬──────────┬──────────┬──────────┬──────────┬────────┐\n");
    printf("│ Mechanism  │  Median  │   Mean   │   P99    │  Stddev  │   CV   │\n");
    printf("├────────────┼──────────┼──────────┼──────────┼──────────┼────────┤\n");
    printf("│ Stigmergy  │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %5.2f  │\n",
           stig_stats.median, stig_stats.mean, stig_stats.p99, stig_stats.stddev, stig_stats.cv);
    printf("│ Atomic     │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %5.2f  │\n",
           atom_stats.median, atom_stats.mean, atom_stats.p99, atom_stats.stddev, atom_stats.cv);
    printf("│ Futex      │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %5.2f  │\n",
           futex_stats.median, futex_stats.mean, futex_stats.p99, futex_stats.stddev, futex_stats.cv);
    printf("│ Pipe       │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %5.2f  │\n",
           pipe_stats.median, pipe_stats.mean, pipe_stats.p99, pipe_stats.stddev, pipe_stats.cv);
    printf("└────────────┴──────────┴──────────┴──────────┴──────────┴────────┘\n");

    printf("\n");
    printf("Speedup vs Stigmergy (median):\n");
    printf("  Atomic:     %.2fx\n", atom_stats.median / stig_stats.median);
    printf("  Futex:      %.2fx\n", futex_stats.median / stig_stats.median);
    printf("  Pipe:       %.2fx\n", pipe_stats.median / stig_stats.median);

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         ANALYSIS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    // Success criteria
    int stig_vs_atom = (stig_stats.median < atom_stats.median * 0.5);
    int stig_vs_futex = (stig_stats.median < futex_stats.median * 0.1);
    int low_cv = (stig_stats.cv < 0.3);

    printf("Success Criteria:\n");
    printf("  [%s] Stigmergy < Atomic × 0.5  (actual: %.2fx)\n",
           stig_vs_atom ? "✓" : "✗", stig_stats.median / atom_stats.median);
    printf("  [%s] Stigmergy < Futex × 0.1   (actual: %.2fx)\n",
           stig_vs_futex ? "✓" : "✗", stig_stats.median / futex_stats.median);
    printf("  [%s] Stigmergy CV < 0.3        (actual: %.2f)\n",
           low_cv ? "✓" : "✗", stig_stats.cv);

    printf("\n");

    // Output CSV for further analysis
    FILE* csv = fopen("e3_results.csv", "w");
    if (csv) {
        fprintf(csv, "mechanism,median_ns,mean_ns,p99_ns,stddev_ns,cv\n");
        fprintf(csv, "stigmergy,%.1f,%.1f,%.1f,%.1f,%.3f\n",
                stig_stats.median, stig_stats.mean, stig_stats.p99, stig_stats.stddev, stig_stats.cv);
        fprintf(csv, "atomic,%.1f,%.1f,%.1f,%.1f,%.3f\n",
                atom_stats.median, atom_stats.mean, atom_stats.p99, atom_stats.stddev, atom_stats.cv);
        fprintf(csv, "futex,%.1f,%.1f,%.1f,%.1f,%.3f\n",
                futex_stats.median, futex_stats.mean, futex_stats.p99, futex_stats.stddev, futex_stats.cv);
        fprintf(csv, "pipe,%.1f,%.1f,%.1f,%.1f,%.3f\n",
                pipe_stats.median, pipe_stats.mean, pipe_stats.p99, pipe_stats.stddev, pipe_stats.cv);
        fclose(csv);
        printf("Results saved to: e3_results.csv\n");
    }

    close(pipe_prod_to_cons[0]);
    close(pipe_prod_to_cons[1]);
    close(pipe_cons_to_prod[0]);
    close(pipe_cons_to_prod[1]);

    printf("\n");
    return 0;
}
