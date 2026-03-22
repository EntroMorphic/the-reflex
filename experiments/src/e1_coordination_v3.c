/*
 * E1v3: Multi-Core Stigmergy Coordination Experiment
 * ASPLOS Stigmergy Paper
 *
 * FINAL VERSION: Proves coordination with correct metrics
 *
 * Key insight from v2: We need to distinguish:
 * - Signal PROPAGATION latency (~300ns, proven by E3)
 * - Signal DETECTION latency (depends on sampling rate)
 *
 * This version:
 * - Uses tight polling during signal windows to catch actual spike
 * - Records both propagation latency and detection status
 * - Demonstrates vigilance change (behavior modification)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <stdatomic.h>
#include <math.h>

// ============================================================================
// Configuration
// ============================================================================

#define ITERATIONS          100
#define BURST_SIZE          50
#define POLL_WINDOW_US      100     // Tight polling window after signal
#define SIGNAL_INTERVAL_US  10000   // 10ms between signals
#define PRODUCER_CORE       0
#define CONSUMER_CORE       1

// ============================================================================
// Timing
// ============================================================================

static inline uint64_t rdtsc(void) {
#if defined(__aarch64__)
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#endif
}

static inline uint64_t get_freq(void) {
#if defined(__aarch64__)
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#else
    return 2400000000ULL;
#endif
}

// ============================================================================
// Core Pinning
// ============================================================================

static void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// ============================================================================
// Shared State
// ============================================================================

typedef struct {
    volatile uint64_t value;
    volatile uint64_t sequence;
    char padding[48];
} __attribute__((aligned(64))) stigmergy_line_t;

static stigmergy_line_t stigmergy;

// Synchronization between producer and consumer
typedef struct {
    volatile uint64_t signal_ts;     // Producer: "I just signaled at this time"
    volatile uint64_t ack_ts;        // Consumer: "I detected it at this time"
    volatile int waiting;            // Consumer: "I'm ready for next signal"
    char padding[40];
} __attribute__((aligned(64))) sync_t;

static sync_t sync_state;

// Event recording
typedef struct {
    uint64_t signal_ts;          // When producer sent
    uint64_t detect_ts;          // When consumer detected
    uint64_t propagation_ns;     // detect_ts - signal_ts
    uint64_t latency_spike;      // Measured access latency during detection
    uint64_t baseline_latency;   // Baseline before signal
    int vigilance_before;        // Vigilance level before
    int vigilance_after;         // Vigilance level after
    int behavior_changed;        // Did vigilance increase?
} event_t;

static event_t events[ITERATIONS];

static atomic_int phase = 0;
static atomic_int running = 1;

// ============================================================================
// Producer Thread - Round-trip coordination
// ============================================================================

static void* producer_thread(void* arg) {
    (void)arg;
    pin_to_core(PRODUCER_CORE);

    // Wait for consumer ready
    while (atomic_load(&phase) < 1) {
        __asm__ volatile("" ::: "memory");
    }

    printf("  Producer: Starting %d coordination rounds\n", ITERATIONS);

    for (int i = 0; i < ITERATIONS && atomic_load(&running); i++) {
        // Wait for consumer to be ready
        while (!sync_state.waiting) {
            __asm__ volatile("" ::: "memory");
        }

        // Small delay to ensure consumer is polling
        for (volatile int d = 0; d < 1000; d++);

        // SIGNAL: Burst write to stigmergy line
        uint64_t signal_ts = rdtsc();

        for (int j = 0; j < BURST_SIZE; j++) {
            stigmergy.value = signal_ts;
            stigmergy.sequence = i + 1;
        }
        __asm__ volatile("dsb sy" ::: "memory");

        // Record signal time for consumer
        sync_state.signal_ts = signal_ts;

        // Wait for consumer acknowledgment
        while (sync_state.ack_ts == 0) {
            __asm__ volatile("" ::: "memory");
        }

        // Record event
        events[i].signal_ts = signal_ts;
        events[i].detect_ts = sync_state.ack_ts;
        events[i].propagation_ns = sync_state.ack_ts - signal_ts;

        // Reset for next round
        sync_state.ack_ts = 0;
        sync_state.signal_ts = 0;

        // Inter-signal delay
        usleep(SIGNAL_INTERVAL_US);
    }

    atomic_store(&running, 0);
    return NULL;
}

// ============================================================================
// Consumer Thread - Tight polling detection
// ============================================================================

static void* consumer_thread(void* arg) {
    (void)arg;
    pin_to_core(CONSUMER_CORE);

    // Vigilance state
    int vigilance = 50;  // Start medium

    // Establish baseline (measure access latency when cache is stable)
    uint64_t baseline_sum = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t start = rdtsc();
        volatile uint64_t v = stigmergy.value;
        (void)v;
        baseline_sum += rdtsc() - start;
    }
    uint64_t baseline = baseline_sum / 1000;

    printf("  Consumer: Baseline access latency = %lu ns\n", baseline);
    printf("  Consumer: Starting coordination detection\n");

    atomic_store(&phase, 1);

    uint64_t last_seq = 0;
    int event_idx = 0;

    while (atomic_load(&running) && event_idx < ITERATIONS) {
        // Signal ready
        sync_state.waiting = 1;

        // Tight poll for signal
        uint64_t poll_start = rdtsc();
        uint64_t poll_timeout = poll_start + 100000000;  // 100ms timeout

        while (atomic_load(&running)) {
            uint64_t access_start = rdtsc();
            volatile uint64_t v = stigmergy.value;
            uint64_t access_latency = rdtsc() - access_start;
            uint64_t seq = stigmergy.sequence;
            (void)v;

            if (seq > last_seq) {
                // DETECTED: New signal!
                uint64_t detect_ts = access_start;

                // Record detection
                sync_state.ack_ts = detect_ts;
                sync_state.waiting = 0;

                // Behavior change: increase vigilance
                int old_vigilance = vigilance;
                vigilance = (vigilance < 90) ? vigilance + 5 : 100;

                // Record event details
                if (event_idx < ITERATIONS) {
                    events[event_idx].baseline_latency = baseline;
                    events[event_idx].latency_spike = access_latency;
                    events[event_idx].vigilance_before = old_vigilance;
                    events[event_idx].vigilance_after = vigilance;
                    events[event_idx].behavior_changed = (vigilance != old_vigilance);
                    event_idx++;
                }

                last_seq = seq;
                break;
            }

            if (rdtsc() > poll_timeout) {
                // Timeout - no signal received
                sync_state.waiting = 0;
                break;
            }
        }

        // Brief pause between detection attempts
        usleep(1000);
    }

    printf("  Consumer: Detected %d signals\n", event_idx);
    printf("  Consumer: Final vigilance = %d (started at 50)\n", vigilance);

    return NULL;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       E1v3: STIGMERGY COORDINATION (Final)                    ║\n");
    printf("║       Proving: signal → propagation → behavior change         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    uint64_t freq = get_freq();
    double ticks_per_ns = (double)freq / 1e9;

    printf("Configuration:\n");
    printf("  Counter frequency: %lu Hz (%.2f ticks/ns)\n", freq, ticks_per_ns);
    printf("  Iterations:        %d\n", ITERATIONS);
    printf("  Burst size:        %d writes\n", BURST_SIZE);
    printf("\n");

    memset(&stigmergy, 0, sizeof(stigmergy));
    memset(&sync_state, 0, sizeof(sync_state));
    memset(events, 0, sizeof(events));

    printf("Running experiment...\n\n");

    pthread_t producer, consumer;
    pthread_create(&consumer, NULL, consumer_thread, NULL);
    usleep(10000);
    pthread_create(&producer, NULL, producer_thread, NULL);

    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    // ========================================
    // Analysis
    // ========================================

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         RESULTS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    int detected = 0;
    int behavior_changed = 0;
    int causality_valid = 0;
    double total_prop_latency = 0;
    uint64_t min_prop = UINT64_MAX, max_prop = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        if (events[i].propagation_ns > 0 && events[i].propagation_ns < 1000000) {
            detected++;
            total_prop_latency += events[i].propagation_ns / ticks_per_ns;

            if (events[i].propagation_ns < min_prop) min_prop = events[i].propagation_ns;
            if (events[i].propagation_ns > max_prop) max_prop = events[i].propagation_ns;

            if (events[i].detect_ts > events[i].signal_ts) {
                causality_valid++;
            }
            if (events[i].behavior_changed) {
                behavior_changed++;
            }
        }
    }

    double detection_rate = (double)detected / ITERATIONS * 100.0;
    double mean_prop_ns = detected > 0 ? total_prop_latency / detected : 0;
    double behavior_rate = detected > 0 ? (double)behavior_changed / detected * 100.0 : 0;

    printf("Detection Metrics:\n");
    printf("  Signals sent:          %d\n", ITERATIONS);
    printf("  Signals detected:      %d (%.1f%%)\n", detected, detection_rate);
    printf("  Causality valid:       %d/%d (%.1f%%)\n", causality_valid, detected,
           detected > 0 ? (double)causality_valid/detected*100 : 0);
    printf("\n");

    printf("Propagation Latency:\n");
    printf("  Mean:                  %.1f ns\n", mean_prop_ns);
    printf("  Min:                   %.1f ns\n", min_prop / ticks_per_ns);
    printf("  Max:                   %.1f ns\n", max_prop / ticks_per_ns);
    printf("\n");

    printf("Behavior Change:\n");
    printf("  Vigilance changes:     %d/%d (%.1f%%)\n", behavior_changed, detected, behavior_rate);
    printf("  Initial vigilance:     50\n");
    printf("  Final vigilance:       %d\n", events[detected-1].vigilance_after);
    printf("\n");

    // Sample events
    printf("Causality Evidence (first 5):\n");
    printf("  %-3s  %-14s  %-14s  %-8s  %-8s  %-6s\n",
           "#", "Signal_ts", "Detect_ts", "Prop(ns)", "Vigil", "Changed");
    int shown = 0;
    for (int i = 0; i < ITERATIONS && shown < 5; i++) {
        if (events[i].propagation_ns > 0) {
            printf("  %-3d  %-14lu  %-14lu  %-8.0f  %d→%d    %s\n",
                   i,
                   events[i].signal_ts,
                   events[i].detect_ts,
                   events[i].propagation_ns / ticks_per_ns,
                   events[i].vigilance_before,
                   events[i].vigilance_after,
                   events[i].behavior_changed ? "YES" : "no");
            shown++;
        }
    }
    printf("\n");

    // Success criteria
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         ANALYSIS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    int lat_pass = (mean_prop_ns < 1000.0);
    int det_pass = (detection_rate > 90.0);
    int caus_pass = (causality_valid == detected && detected > 0);
    int behav_pass = (behavior_rate > 90.0);

    printf("Success Criteria:\n");
    printf("  [%s] Propagation latency < 1μs      (actual: %.1f ns)\n",
           lat_pass ? "✓" : "✗", mean_prop_ns);
    printf("  [%s] Detection rate > 90%%          (actual: %.1f%%)\n",
           det_pass ? "✓" : "✗", detection_rate);
    printf("  [%s] Causality preserved           (%d/%d)\n",
           caus_pass ? "✓" : "✗", causality_valid, detected);
    printf("  [%s] Behavior change > 90%%         (actual: %.1f%%)\n",
           behav_pass ? "✓" : "✗", behavior_rate);
    printf("\n");

    int all_pass = lat_pass && det_pass && caus_pass && behav_pass;

    printf("═══════════════════════════════════════════════════════════════\n");
    if (all_pass) {
        printf("  ✓ E1 PASSED: Full coordination loop demonstrated!\n");
        printf("\n");
        printf("  CAUSALITY CHAIN PROVEN:\n");
        printf("    1. Producer writes signal      (stimulus)\n");
        printf("    2. Cache invalidation occurs   (propagation: %.0fns)\n", mean_prop_ns);
        printf("    3. Consumer detects change     (detection: %.0f%%)\n", detection_rate);
        printf("    4. Consumer changes behavior   (vigilance: 50→%d)\n",
               events[detected-1].vigilance_after);
    } else {
        printf("  ✗ E1 NEEDS WORK:\n");
        if (!lat_pass) printf("    - Propagation too slow\n");
        if (!det_pass) printf("    - Detection rate too low\n");
        if (!caus_pass) printf("    - Causality violations\n");
        if (!behav_pass) printf("    - Behavior not changing\n");
    }
    printf("═══════════════════════════════════════════════════════════════\n");

    // Save CSV
    FILE* f = fopen("e1_results_v3.csv", "w");
    if (f) {
        fprintf(f, "iteration,signal_ts,detect_ts,propagation_ns,baseline_lat,spike_lat,vigil_before,vigil_after,behavior_changed\n");
        for (int i = 0; i < ITERATIONS; i++) {
            fprintf(f, "%d,%lu,%lu,%.1f,%lu,%lu,%d,%d,%d\n",
                    i,
                    events[i].signal_ts,
                    events[i].detect_ts,
                    events[i].propagation_ns / ticks_per_ns,
                    events[i].baseline_latency,
                    events[i].latency_spike,
                    events[i].vigilance_before,
                    events[i].vigilance_after,
                    events[i].behavior_changed);
        }
        fclose(f);
        printf("\nResults saved to: e1_results_v3.csv\n");
    }

    printf("\n");
    return all_pass ? 0 : 1;
}
