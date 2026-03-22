/**
 * cfc_spine_demo.c - CfC Closed-Loop Spine Demo
 *
 * Demonstrates the liquid neural network integrated into the reflex arc:
 *   Sensor → CfC → Decision → Actuator
 *
 * This is THE FUSION: CNS topology + liquid neural network.
 *
 * The CfC spine runs at 31 kHz, providing:
 *   - Temporal filtering
 *   - Predictive anticipation  
 *   - Adaptive thresholds
 *   - Anomaly detection
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Use ESP-IDF for printf (this is a demo, not the summit)
#include <stdio.h>

#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_cfc_spine.h"

// ============================================================
// Configuration
// ============================================================

#define PIN_LED             8
#define PIN_ANOMALY_LED     9       // Could be another LED or GPIO output

#define DEMO_TICKS          10000   // Per test
#define BENCHMARK_TICKS     50000   // For throughput measurement

// Simulated sensor parameters
#define SENSOR_NOISE_BITS   3       // LSB noise
#define SENSOR_SPIKE_PROB   50      // 1 in N chance of spike
#define SENSOR_TREND_RATE   1       // Slow drift

// ============================================================
// Simulated Sensor
// ============================================================

static uint32_t prng_state = 0x12345678;

static inline uint32_t fast_random(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

// Simulated force sensor with noise, drift, and occasional spikes
typedef struct {
    uint8_t base_value;     // Underlying true value
    int8_t drift_direction; // +1 or -1
    uint32_t tick_count;
} simulated_sensor_t;

static simulated_sensor_t sensor;

static void sensor_init(void) {
    sensor.base_value = 128;  // Start at midpoint
    sensor.drift_direction = 1;
    sensor.tick_count = 0;
}

static uint64_t sensor_read(void) {
    sensor.tick_count++;
    
    // Slow drift
    if ((sensor.tick_count % 100) == 0) {
        sensor.base_value += sensor.drift_direction * SENSOR_TREND_RATE;
        if (sensor.base_value > 200) sensor.drift_direction = -1;
        if (sensor.base_value < 50) sensor.drift_direction = 1;
    }
    
    // Add noise
    uint8_t noise = fast_random() & ((1 << SENSOR_NOISE_BITS) - 1);
    uint8_t value = sensor.base_value + noise - (1 << (SENSOR_NOISE_BITS - 1));
    
    // Occasional spike
    if ((fast_random() % SENSOR_SPIKE_PROB) == 0) {
        value = fast_random() & 0xFF;  // Random value
    }
    
    // Compute derivative (approximate)
    static uint8_t prev_value = 128;
    int8_t derivative = (int8_t)(value - prev_value);
    prev_value = value;
    
    // Compute simple integral (low-pass filtered value)
    static uint16_t integral = 128 << 4;
    integral = (integral * 15 + ((uint16_t)value << 4)) >> 4;
    uint8_t integral_byte = integral >> 4;
    
    // Status flags
    uint8_t flags = 0;
    if (value < 10) flags |= 0x01;   // Low limit
    if (value > 245) flags |= 0x02;  // High limit
    
    return cfc_encode_force(value, derivative, integral_byte, flags);
}

// ============================================================
// Global Spine Instance
// ============================================================

static cfc_spine_t spine;

// ============================================================
// Demo Functions
// ============================================================

static void demo_basic_loop(void) {
    printf("\n  Running basic closed-loop demo (%d ticks)...\n", DEMO_TICKS);
    
    uint32_t emergency_stops = 0;
    uint32_t trend_ups = 0;
    uint32_t trend_downs = 0;
    
    sensor_init();
    
    for (int i = 0; i < DEMO_TICKS; i++) {
        // Read sensor
        uint64_t input = sensor_read();
        
        // Process through CfC spine
        uint64_t output = cfc_spine_tick(&spine, input);
        
        // React to output
        if (cfc_spine_emergency(output)) {
            emergency_stops++;
            gpio_write(PIN_LED, 0);  // LED on = emergency
        } else {
            gpio_write(PIN_LED, 1);  // LED off = normal
        }
        
        if (output & CFC_OUT_TREND_UP) trend_ups++;
        if (output & CFC_OUT_TREND_DOWN) trend_downs++;
    }
    
    cfc_spine_stats_t stats = cfc_spine_get_stats(&spine);
    
    printf("  Results:\n");
    printf("    Total ticks:     %lu\n", (unsigned long)stats.total_ticks);
    printf("    Anomalies:       %lu (%.2f%%)\n", 
           (unsigned long)stats.anomaly_count,
           stats.anomaly_rate * 100.0f);
    printf("    Spikes:          %lu\n", (unsigned long)stats.spike_count);
    printf("    Emergency stops: %lu\n", (unsigned long)emergency_stops);
    printf("    Trend up:        %lu\n", (unsigned long)trend_ups);
    printf("    Trend down:      %lu\n", (unsigned long)trend_downs);
    printf("    Avg tick:        %lu ns\n", (unsigned long)stats.avg_tick_ns);
    printf("    Last tick:       %lu ns\n", (unsigned long)stats.last_tick_ns);
}

static void demo_throughput_benchmark(void) {
    printf("\n  Benchmarking throughput (%d ticks)...\n", BENCHMARK_TICKS);
    
    sensor_init();
    
    uint32_t t0 = reflex_cycles();
    
    for (int i = 0; i < BENCHMARK_TICKS; i++) {
        uint64_t input = sensor_read();
        uint64_t output = cfc_spine_tick(&spine, input);
        (void)output;
    }
    
    uint32_t total_cycles = reflex_cycles() - t0;
    uint32_t total_ns = reflex_cycles_to_ns(total_cycles);
    uint32_t ns_per_tick = total_ns / BENCHMARK_TICKS;
    uint32_t ticks_per_sec = total_ns > 0 ? (1000000000ULL * BENCHMARK_TICKS) / total_ns : 0;
    
    printf("  Results:\n");
    printf("    Total time:      %lu ms\n", (unsigned long)(total_ns / 1000000));
    printf("    Per tick:        %lu ns\n", (unsigned long)ns_per_tick);
    printf("    Throughput:      %lu kHz\n", (unsigned long)(ticks_per_sec / 1000));
}

static void demo_latency_histogram(void) {
    printf("\n  Measuring latency distribution (1000 samples)...\n");
    
    #define HIST_BUCKETS 10
    #define HIST_BUCKET_NS 5000  // 5μs per bucket
    uint32_t histogram[HIST_BUCKETS + 1] = {0};  // Last bucket = overflow
    
    sensor_init();
    
    for (int i = 0; i < 1000; i++) {
        uint64_t input = sensor_read();
        
        uint32_t t0 = reflex_cycles();
        uint64_t output = cfc_spine_tick(&spine, input);
        uint32_t ns = reflex_cycles_to_ns(reflex_cycles() - t0);
        (void)output;
        
        uint32_t bucket = ns / HIST_BUCKET_NS;
        if (bucket >= HIST_BUCKETS) bucket = HIST_BUCKETS;
        histogram[bucket]++;
    }
    
    printf("  Latency histogram:\n");
    for (int i = 0; i <= HIST_BUCKETS; i++) {
        if (i < HIST_BUCKETS) {
            printf("    %2d-%2d μs: ", i * 5, (i + 1) * 5);
        } else {
            printf("    >%2d μs:   ", HIST_BUCKETS * 5);
        }
        
        // Simple bar chart
        uint32_t bar_len = histogram[i] / 10;
        for (uint32_t j = 0; j < bar_len && j < 40; j++) {
            printf("#");
        }
        printf(" %lu\n", (unsigned long)histogram[i]);
    }
}

static void demo_motor_control(void) {
    printf("\n  Simulating motor control loop (1000 ticks)...\n");
    
    sensor_init();
    
    uint32_t action_counts[6] = {0};  // NONE, BRAKE, RELEASE, HOLD, TRACK, AVOID
    
    for (int i = 0; i < 1000; i++) {
        uint64_t input = sensor_read();
        uint64_t output = cfc_spine_tick(&spine, input);
        
        cfc_motor_command_t cmd = cfc_decode_motor(output);
        
        if (cmd.emergency_stop) {
            // Immediate stop - override all other actions
            gpio_write(PIN_LED, 0);
        } else {
            gpio_write(PIN_LED, 1);
            
            // Track action distribution
            if (cmd.action < 6) {
                action_counts[cmd.action]++;
            }
        }
    }
    
    printf("  Action distribution:\n");
    printf("    NONE:    %lu\n", (unsigned long)action_counts[0]);
    printf("    BRAKE:   %lu\n", (unsigned long)action_counts[1]);
    printf("    RELEASE: %lu\n", (unsigned long)action_counts[2]);
    printf("    HOLD:    %lu\n", (unsigned long)action_counts[3]);
    printf("    TRACK:   %lu\n", (unsigned long)action_counts[4]);
    printf("    AVOID:   %lu\n", (unsigned long)action_counts[5]);
}

static void demo_hidden_state_evolution(void) {
    printf("\n  Observing hidden state evolution...\n");
    
    // Reset spine to clear hidden state
    cfc_spine_init(&spine, reflex_cycles());
    
    sensor_init();
    sensor.base_value = 50;  // Start low
    
    printf("  Input(base) -> Hidden state (hex)\n");
    
    for (int i = 0; i < 20; i++) {
        // Gradually increase base value
        sensor.base_value += 10;
        
        uint64_t input = sensor_read();
        uint64_t output = cfc_spine_tick(&spine, input);
        
        printf("    %3d -> 0x%016llx (out: 0x%04llx)\n",
               (int)sensor.base_value,
               (unsigned long long)spine.network.hidden,
               (unsigned long long)(output & 0xFFFF));
    }
}

// ============================================================
// Main Entry Point
// ============================================================

void app_main(void) {
    gpio_set_output(PIN_LED);
    gpio_write(PIN_LED, 1);
    
    printf("\n");
    printf("================================================================\n");
    printf("          CfC SPINE: CLOSED-LOOP NEURAL CONTROL                \n");
    printf("================================================================\n");
    printf("\n");
    printf("  The liquid neural network becomes part of the reflex arc:\n");
    printf("    Sensor -> CfC (31 kHz) -> Decision -> Actuator\n");
    printf("\n");
    printf("  Capabilities:\n");
    printf("    - Temporal filtering (smooths noisy inputs)\n");
    printf("    - Predictive anticipation (hidden state carries history)\n");
    printf("    - Adaptive thresholds (learned, not hardcoded)\n");
    printf("    - Anomaly detection (deviation from learned patterns)\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize spine
    printf("  Initializing CfC spine...\n");
    cfc_spine_init(&spine, reflex_cycles());
    printf("    Network size: %lu bytes\n", (unsigned long)cfc_turbo_memory_size());
    printf("    Hidden state: 64 bits\n");
    printf("\n");
    fflush(stdout);
    
    // Run demos
    printf("================================================================\n");
    printf("                        DEMOS                                   \n");
    printf("================================================================\n");
    fflush(stdout);
    
    demo_basic_loop();
    fflush(stdout);
    
    demo_throughput_benchmark();
    fflush(stdout);
    
    demo_latency_histogram();
    fflush(stdout);
    
    demo_motor_control();
    fflush(stdout);
    
    demo_hidden_state_evolution();
    fflush(stdout);
    
    // Summary
    printf("\n");
    printf("================================================================\n");
    printf("                       SUMMARY                                  \n");
    printf("================================================================\n");
    printf("\n");
    printf("  CfC Spine integration complete.\n");
    printf("\n");
    printf("  The liquid neural network is now part of the reflex arc.\n");
    printf("  Every sensor reading flows through 64 neurons.\n");
    printf("  Every decision carries temporal context.\n");
    printf("  Every output is neurally-informed.\n");
    printf("\n");
    printf("  At 31 kHz, the spine can:\n");
    printf("    - Process 310 readings per 10ms control loop\n");
    printf("    - React to anomalies in <32 microseconds\n");
    printf("    - Maintain predictive hidden state continuously\n");
    printf("\n");
    printf("  This is closed-loop neural control on a $5 chip.\n");
    printf("\n");
    printf("================================================================\n");
    printf("\n");
    printf("  Heartbeat active...\n\n");
    fflush(stdout);
    
    // Heartbeat with occasional status
    uint32_t loop_count = 0;
    while (1) {
        gpio_toggle(PIN_LED);
        
        // Run one spine tick per heartbeat (just to show it's alive)
        uint64_t input = sensor_read();
        uint64_t output = cfc_spine_tick(&spine, input);
        (void)output;
        
        loop_count++;
        if ((loop_count % 20) == 0) {
            cfc_spine_stats_t stats = cfc_spine_get_stats(&spine);
            printf("  [%lu ticks, %lu anomalies, hidden=0x%016llx]\n",
                   (unsigned long)stats.total_ticks,
                   (unsigned long)stats.anomaly_count,
                   (unsigned long long)spine.network.hidden);
            fflush(stdout);
        }
        
        // Delay
        uint32_t start = reflex_cycles();
        while ((reflex_cycles() - start) < 80000000);  // 500ms at 160MHz
    }
}
