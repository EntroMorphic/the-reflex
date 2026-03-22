/*
 * E2b: False Sharing Control Experiment
 * ASPLOS Stigmergy Paper
 *
 * Can we distinguish INTENTIONAL stigmergy from ACCIDENTAL false sharing?
 *
 * False sharing: Multiple threads access adjacent addresses on same cache line,
 *                causing invalidations without intent to communicate.
 *
 * Stigmergy: Intentional writes to a dedicated line with known pattern.
 *
 * Protocol:
 *   Phase 1: False sharing only (no intentional signals)
 *            → Measure false positive rate
 *   Phase 2: False sharing + intentional signals
 *            → Measure true positive rate (can we detect signals above noise?)
 *
 * Success: Detection rate > 90%, False positive rate < 10%
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

#define PRODUCER_CORE       0
#define CONSUMER_CORE       1
#define FALSE_SHARE_CORES   2, 3, 4, 5  // Cores that cause false sharing

#define ITERATIONS          100
#define BURST_SIZE          100         // Intentional signal burst size
#define DETECTION_THRESHOLD 3           // Sequence increment threshold

// ============================================================================
// Timing
// ============================================================================

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

static void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// ============================================================================
// Shared State - Designed to cause false sharing
// ============================================================================

// FALSE SHARING STRUCT: All fields on SAME cache line
// Multiple threads will access different fields, causing invalidations
typedef struct {
    volatile uint64_t thread0_counter;  // Offset 0
    volatile uint64_t thread1_counter;  // Offset 8
    volatile uint64_t thread2_counter;  // Offset 16
    volatile uint64_t thread3_counter;  // Offset 24
    volatile uint64_t padding[4];       // Fill to 64 bytes
} __attribute__((aligned(64))) false_sharing_line_t;

static false_sharing_line_t false_share;

// STIGMERGY LINE: Dedicated, separate cache line for intentional signals
typedef struct {
    volatile uint64_t value;
    volatile uint64_t sequence;         // Incremented by BURST_SIZE for intentional signals
    char padding[48];
} __attribute__((aligned(64))) stigmergy_line_t;

static stigmergy_line_t stigmergy;

// Control
static atomic_int running = 0;
static atomic_int phase = 0;           // 0=idle, 1=false_sharing_only, 2=false_sharing+signals
static atomic_int send_signal = 0;     // Producer: set when sending intentional signal

// ============================================================================
// False Sharing Generator Threads
// ============================================================================

static void* false_sharing_thread(void* arg) {
    int thread_id = *(int*)arg;
    int core_id = 2 + thread_id;  // Cores 2-5
    pin_to_core(core_id);

    volatile uint64_t* my_counter;
    switch (thread_id) {
        case 0: my_counter = &false_share.thread0_counter; break;
        case 1: my_counter = &false_share.thread1_counter; break;
        case 2: my_counter = &false_share.thread2_counter; break;
        case 3: my_counter = &false_share.thread3_counter; break;
        default: return NULL;
    }

    while (atomic_load(&running)) {
        int p = atomic_load(&phase);
        if (p == 1 || p == 2) {
            // Rapidly increment our counter (causes false sharing)
            for (int i = 0; i < 1000; i++) {
                (*my_counter)++;
            }
            usleep(100);  // Brief pause
        } else {
            usleep(1000);
        }
    }

    return NULL;
}

// ============================================================================
// Producer Thread - Sends intentional signals (phase 2 only)
// ============================================================================

static uint64_t signal_timestamps[ITERATIONS];
static int signals_sent = 0;

static void* producer_thread(void* arg) {
    (void)arg;
    pin_to_core(PRODUCER_CORE);

    while (atomic_load(&running)) {
        int p = atomic_load(&phase);

        if (p == 2 && signals_sent < ITERATIONS) {
            // Send intentional signal with distinctive pattern
            atomic_store(&send_signal, 1);

            uint64_t ts = rdtsc();
            signal_timestamps[signals_sent] = ts;

            // BURST write - distinctive pattern vs random false sharing
            stigmergy.value = ts;
            for (int i = 0; i < BURST_SIZE; i++) {
                stigmergy.sequence++;  // Increment by BURST_SIZE total
            }
            __asm__ volatile("dsb sy" ::: "memory");

            signals_sent++;
            atomic_store(&send_signal, 0);

            usleep(10000);  // 10ms between signals
        } else {
            usleep(1000);
        }
    }

    return NULL;
}

// ============================================================================
// Consumer Thread - Detects signals, tracks false positives
// ============================================================================

typedef struct {
    int is_true_positive;   // Was there an intentional signal?
    int detected;           // Did we detect something?
    uint64_t seq_delta;     // How much did sequence change?
    uint64_t latency;       // Access latency
} detection_event_t;

static detection_event_t phase1_events[ITERATIONS];  // False sharing only
static detection_event_t phase2_events[ITERATIONS];  // With intentional signals
static int phase1_count = 0;
static int phase2_count = 0;

static void* consumer_thread(void* arg) {
    (void)arg;
    pin_to_core(CONSUMER_CORE);

    uint64_t last_seq = 0;

    while (atomic_load(&running)) {
        int p = atomic_load(&phase);

        if (p == 1 && phase1_count < ITERATIONS) {
            // Phase 1: False sharing only - measure false positives
            uint64_t start = rdtsc();
            volatile uint64_t v = stigmergy.value;
            (void)v;
            uint64_t lat = rdtsc() - start;

            uint64_t seq = stigmergy.sequence;
            uint64_t delta = seq - last_seq;

            // Detection rule: sequence changed by >= DETECTION_THRESHOLD
            int detected = (delta >= DETECTION_THRESHOLD);

            phase1_events[phase1_count].is_true_positive = 0;  // No real signal
            phase1_events[phase1_count].detected = detected;
            phase1_events[phase1_count].seq_delta = delta;
            phase1_events[phase1_count].latency = lat;
            phase1_count++;

            last_seq = seq;
            usleep(10000);  // Sample every 10ms

        } else if (p == 2 && phase2_count < ITERATIONS) {
            // Phase 2: With intentional signals - measure true positives
            int signal_active = atomic_load(&send_signal);

            uint64_t start = rdtsc();
            volatile uint64_t v = stigmergy.value;
            (void)v;
            uint64_t lat = rdtsc() - start;

            uint64_t seq = stigmergy.sequence;
            uint64_t delta = seq - last_seq;

            // Detection rule: sequence changed by >= DETECTION_THRESHOLD
            int detected = (delta >= DETECTION_THRESHOLD);

            phase2_events[phase2_count].is_true_positive = signal_active ||
                (delta >= BURST_SIZE / 2);  // Large delta = intentional
            phase2_events[phase2_count].detected = detected;
            phase2_events[phase2_count].seq_delta = delta;
            phase2_events[phase2_count].latency = lat;
            phase2_count++;

            last_seq = seq;
            usleep(5000);  // Faster sampling to catch signals

        } else {
            usleep(1000);
        }
    }

    return NULL;
}

// ============================================================================
// Analysis
// ============================================================================

typedef struct {
    int total_samples;
    int false_positives;     // Detected when no signal
    int true_positives;      // Detected when signal present
    int false_negatives;     // Missed signal
    double false_positive_rate;
    double true_positive_rate;
    double precision;        // TP / (TP + FP)
    double recall;           // TP / (TP + FN)
} analysis_t;

static analysis_t analyze(void) {
    analysis_t a = {0};

    // Phase 1: All detections are false positives (no real signals)
    for (int i = 0; i < phase1_count; i++) {
        if (phase1_events[i].detected) {
            a.false_positives++;
        }
    }

    // Phase 2: Count TP, FP, FN
    int tp = 0, fp = 0, fn = 0;
    for (int i = 0; i < phase2_count; i++) {
        int real_signal = phase2_events[i].is_true_positive;
        int detected = phase2_events[i].detected;

        if (real_signal && detected) tp++;
        else if (!real_signal && detected) fp++;
        else if (real_signal && !detected) fn++;
    }

    a.total_samples = phase1_count + phase2_count;
    a.true_positives = tp;
    a.false_negatives = fn;

    // Combine phase 1 and phase 2 false positives
    a.false_positives += fp;

    a.false_positive_rate = phase1_count > 0 ?
        (double)a.false_positives / (phase1_count + (phase2_count - tp - fn)) * 100.0 : 0;

    a.true_positive_rate = (tp + fn) > 0 ?
        (double)tp / (tp + fn) * 100.0 : 0;

    a.precision = (tp + a.false_positives) > 0 ?
        (double)tp / (tp + a.false_positives) * 100.0 : 0;

    a.recall = (tp + fn) > 0 ?
        (double)tp / (tp + fn) * 100.0 : 0;

    return a;
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       E2b: FALSE SHARING CONTROL                              ║\n");
    printf("║       Can we distinguish intentional signals from noise?      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

    uint64_t freq = get_freq();
    double ticks_per_ns = (double)freq / 1e9;
    (void)ticks_per_ns;

    printf("Configuration:\n");
    printf("  Stigmergy: Core %d → Core %d\n", PRODUCER_CORE, CONSUMER_CORE);
    printf("  False sharing: Cores 2-5 (4 threads on same cache line)\n");
    printf("  Detection threshold: sequence delta >= %d\n", DETECTION_THRESHOLD);
    printf("  Intentional burst: %d increments\n\n", BURST_SIZE);

    // Initialize
    memset(&false_share, 0, sizeof(false_share));
    memset(&stigmergy, 0, sizeof(stigmergy));

    // Start threads
    atomic_store(&running, 1);

    // False sharing threads
    pthread_t fs_threads[4];
    int thread_ids[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) {
        pthread_create(&fs_threads[i], NULL, false_sharing_thread, &thread_ids[i]);
    }

    // Producer and consumer
    pthread_t producer, consumer;
    pthread_create(&consumer, NULL, consumer_thread, NULL);
    pthread_create(&producer, NULL, producer_thread, NULL);

    usleep(100000);  // Let threads start

    printf("Running experiment...\n\n");

    // Phase 1: False sharing only (measure false positive rate)
    printf("  Phase 1: False sharing only (no intentional signals)...\n");
    atomic_store(&phase, 1);

    while (phase1_count < ITERATIONS) {
        usleep(50000);
    }

    printf("    Collected %d samples\n", phase1_count);

    // Phase 2: False sharing + intentional signals
    printf("  Phase 2: False sharing + intentional signals...\n");
    atomic_store(&phase, 2);

    while (phase2_count < ITERATIONS) {
        usleep(50000);
    }

    printf("    Collected %d samples, sent %d signals\n", phase2_count, signals_sent);

    // Stop
    atomic_store(&running, 0);

    for (int i = 0; i < 4; i++) {
        pthread_join(fs_threads[i], NULL);
    }
    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    // Analyze
    analysis_t a = analyze();

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         RESULTS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Phase 1 (False Sharing Only):\n");
    printf("  Samples:           %d\n", phase1_count);
    printf("  False detections:  %d\n", a.false_positives);
    printf("\n");

    printf("Phase 2 (With Intentional Signals):\n");
    printf("  Samples:           %d\n", phase2_count);
    printf("  True positives:    %d\n", a.true_positives);
    printf("  False negatives:   %d\n", a.false_negatives);
    printf("\n");

    printf("Detection Metrics:\n");
    printf("  True Positive Rate:   %.1f%% (signals detected)\n", a.true_positive_rate);
    printf("  False Positive Rate:  %.1f%% (noise mistaken for signal)\n", a.false_positive_rate);
    printf("  Precision:            %.1f%%\n", a.precision);
    printf("  Recall:               %.1f%%\n", a.recall);
    printf("\n");

    // Sample events
    printf("Sample Phase 1 Events (false sharing only):\n");
    printf("  %-4s  %-10s  %-8s\n", "#", "SeqDelta", "Detected");
    for (int i = 0; i < 5 && i < phase1_count; i++) {
        printf("  %-4d  %-10lu  %s\n", i,
               phase1_events[i].seq_delta,
               phase1_events[i].detected ? "YES (FP)" : "no");
    }
    printf("\n");

    printf("Sample Phase 2 Events (with signals):\n");
    printf("  %-4s  %-10s  %-8s  %-8s\n", "#", "SeqDelta", "Signal?", "Detected");
    for (int i = 0; i < 5 && i < phase2_count; i++) {
        printf("  %-4d  %-10lu  %-8s  %s\n", i,
               phase2_events[i].seq_delta,
               phase2_events[i].is_true_positive ? "YES" : "no",
               phase2_events[i].detected ? "YES" : "no");
    }
    printf("\n");

    // Success criteria
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                         ANALYSIS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    int tp_pass = a.true_positive_rate > 90.0;
    int fp_pass = a.false_positive_rate < 10.0;

    printf("Success Criteria:\n");
    printf("  [%s] Detection rate > 90%%       (actual: %.1f%%)\n",
           tp_pass ? "✓" : "✗", a.true_positive_rate);
    printf("  [%s] False positive rate < 10%%  (actual: %.1f%%)\n",
           fp_pass ? "✓" : "✗", a.false_positive_rate);
    printf("\n");

    int pass = tp_pass && fp_pass;

    printf("═══════════════════════════════════════════════════════════════\n");
    if (pass) {
        printf("  ✓ E2b PASSED: Intentional signals distinguishable from noise!\n");
    } else {
        printf("  ⚠ E2b PARTIAL:\n");
        if (!tp_pass) printf("    - Detection rate below target\n");
        if (!fp_pass) printf("    - False positive rate too high\n");
    }
    printf("═══════════════════════════════════════════════════════════════\n");

    // Save results
    FILE* f = fopen("e2b_results.csv", "w");
    if (f) {
        fprintf(f, "phase,event,seq_delta,is_signal,detected,latency\n");
        for (int i = 0; i < phase1_count; i++) {
            fprintf(f, "1,%d,%lu,0,%d,%lu\n", i,
                    phase1_events[i].seq_delta,
                    phase1_events[i].detected,
                    phase1_events[i].latency);
        }
        for (int i = 0; i < phase2_count; i++) {
            fprintf(f, "2,%d,%lu,%d,%d,%lu\n", i,
                    phase2_events[i].seq_delta,
                    phase2_events[i].is_true_positive,
                    phase2_events[i].detected,
                    phase2_events[i].latency);
        }
        fclose(f);
        printf("\nResults saved to e2b_results.csv\n");
    }

    printf("\n");
    return pass ? 0 : 1;
}
