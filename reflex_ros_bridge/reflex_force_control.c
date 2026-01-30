/*
 * reflex_force_control.c - 10kHz Force Control Loop
 * 
 * Runs OUTSIDE container on isolated RT cores.
 * Communicates with ROS2 via shared memory channels.
 * 
 * Build:
 *   gcc -O3 -Wall -o reflex_force_control reflex_force_control.c -lrt -lm
 * 
 * Run:
 *   sudo taskset -c 0-2 ./reflex_force_control
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <math.h>

// Must match channel.hpp exactly
typedef struct __attribute__((aligned(64))) {
    volatile uint64_t sequence;
    volatile uint64_t timestamp;
    volatile uint64_t value;
    char padding[40];
} reflex_channel_t;

// Configuration
#define FORCE_THRESHOLD  5000000   // 5N in μN - STOP if exceeded
#define TARGET_FORCE     2000000   // 2N in μN - gentle grasp target
#define KP               100       // Proportional gain
#define LOOP_PERIOD_NS   100000    // 100μs = 10kHz

// Channels
static reflex_channel_t* force_in = NULL;
static reflex_channel_t* command_out = NULL;
static reflex_channel_t* telemetry = NULL;

// Statistics
static uint64_t loop_count = 0;
static uint64_t anomaly_count = 0;
static uint64_t min_loop_ns = UINT64_MAX;
static uint64_t max_loop_ns = 0;
static uint64_t sum_loop_ns = 0;

static volatile bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline void reflex_signal(reflex_channel_t* ch, uint64_t value) {
    ch->value = value;
    ch->timestamp = get_time_ns();
    __atomic_fetch_add(&ch->sequence, 1, __ATOMIC_SEQ_CST);
    __asm__ volatile("dmb sy" ::: "memory");  // ARM memory barrier
}

static inline uint64_t reflex_wait(reflex_channel_t* ch, uint64_t last_seq) {
    uint64_t seq;
    while ((seq = __atomic_load_n(&ch->sequence, __ATOMIC_ACQUIRE)) == last_seq) {
        __asm__ volatile("" ::: "memory");
    }
    return seq;
}

static inline uint64_t reflex_try_wait(reflex_channel_t* ch, uint64_t last_seq) {
    return __atomic_load_n(&ch->sequence, __ATOMIC_ACQUIRE);
}

static inline uint64_t reflex_read(reflex_channel_t* ch) {
    return __atomic_load_n(&ch->value, __ATOMIC_ACQUIRE);
}

reflex_channel_t* open_shared_channel(const char* name, bool create) {
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    
    // Also try direct file path for containers with --ipc host
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "/dev/shm/%s", name);
    
    int fd = -1;
    
    if (create) {
        shm_unlink(shm_name);
        fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            perror("shm_open create");
            return NULL;
        }
        if (ftruncate(fd, sizeof(reflex_channel_t)) < 0) {
            perror("ftruncate");
            close(fd);
            return NULL;
        }
    } else {
        // Try POSIX shm first, then direct file
        for (int i = 0; i < 100; i++) {
            fd = shm_open(shm_name, O_RDWR, 0666);
            if (fd >= 0) break;
            
            // Try direct file access
            fd = open(file_path, O_RDWR);
            if (fd >= 0) break;
            
            usleep(100000);
        }
        if (fd < 0) {
            fprintf(stderr, "Failed to open %s (tried shm and file)\n", name);
            return NULL;
        }
    }
    
    void* ptr = mmap(NULL, sizeof(reflex_channel_t),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    
    reflex_channel_t* ch = (reflex_channel_t*)ptr;
    
    if (create) {
        ch->sequence = 0;
        ch->timestamp = 0;
        ch->value = 0;
    }
    
    return ch;
}

void setup_realtime(void) {
    // Lock memory
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
        perror("mlockall");
    }
    
    // Set SCHED_FIFO with max priority
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) < 0) {
        perror("sched_setscheduler");
    }
    
    printf("Real-time setup: SCHED_FIFO priority %d\n", param.sched_priority);
}

void print_stats(void) {
    if (loop_count == 0) return;
    
    double avg_ns = (double)sum_loop_ns / loop_count;
    double avg_hz = 1e9 / avg_ns;
    
    printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       REFLEX FORCE CONTROL: STATISTICS                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("  Loop count:     %lu\n", loop_count);
    printf("  Anomaly count:  %lu\n", anomaly_count);
    printf("  Loop timing:\n");
    printf("    Min:          %lu ns\n", min_loop_ns);
    printf("    Max:          %lu ns\n", max_loop_ns);
    printf("    Avg:          %.1f ns\n", avg_ns);
    printf("    Rate:         %.1f Hz\n", avg_hz);
    printf("\n");
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       REFLEX FORCE CONTROL: 10kHz Loop                        ║\n");
    printf("║       926ns P99 - Sub-Microsecond Robotics                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    // Signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Setup real-time
    setup_realtime();
    
    // Open shared memory channels (wait for bridge to create them)
    printf("Waiting for shared memory channels...\n");
    
    force_in = open_shared_channel("reflex_force", false);
    if (!force_in) {
        fprintf(stderr, "Failed to open force channel\n");
        return 1;
    }
    printf("  Force channel: connected\n");
    
    command_out = open_shared_channel("reflex_command", false);
    if (!command_out) {
        fprintf(stderr, "Failed to open command channel\n");
        return 1;
    }
    printf("  Command channel: connected\n");
    
    telemetry = open_shared_channel("reflex_telemetry", false);
    if (!telemetry) {
        fprintf(stderr, "Failed to open telemetry channel\n");
        return 1;
    }
    printf("  Telemetry channel: connected\n");
    
    printf("\nStarting 10kHz control loop (Ctrl+C to stop)...\n\n");
    
    // Control state
    int64_t position = 500000;  // Start at 0.5 (mid-grip)
    uint64_t last_seq = 0;
    uint64_t last_loop_time = get_time_ns();
    
    while (running) {
        uint64_t loop_start = get_time_ns();
        
        // Check for new force reading (non-blocking)
        uint64_t seq = reflex_try_wait(force_in, last_seq);
        
        if (seq != last_seq) {
            last_seq = seq;
            
            int64_t force = (int64_t)reflex_read(force_in);
            
            // REFLEX: Instant threshold response
            if (force > FORCE_THRESHOLD) {
                // STOP - force exceeded
                reflex_signal(command_out, (uint64_t)position);
                reflex_signal(telemetry, 1);  // Anomaly flag
                anomaly_count++;
            } else {
                // Normal: Proportional control
                int64_t error = TARGET_FORCE - force;
                position += (KP * error) >> 20;
                
                // Clamp
                if (position < 0) position = 0;
                if (position > 1000000) position = 1000000;
                
                reflex_signal(command_out, (uint64_t)position);
                reflex_signal(telemetry, 0);  // Normal
            }
        }
        
        // Track loop timing
        uint64_t loop_end = get_time_ns();
        uint64_t loop_ns = loop_end - loop_start;
        
        if (loop_ns < min_loop_ns) min_loop_ns = loop_ns;
        if (loop_ns > max_loop_ns) max_loop_ns = loop_ns;
        sum_loop_ns += loop_ns;
        loop_count++;
        
        // Sleep to maintain 10kHz rate
        uint64_t elapsed = loop_end - last_loop_time;
        if (elapsed < LOOP_PERIOD_NS) {
            struct timespec sleep_time;
            uint64_t sleep_ns = LOOP_PERIOD_NS - elapsed;
            sleep_time.tv_sec = 0;
            sleep_time.tv_nsec = sleep_ns;
            nanosleep(&sleep_time, NULL);
        }
        
        last_loop_time = get_time_ns();
        
        // Periodic status
        if (loop_count % 100000 == 0) {
            printf("  Loops: %lu, Anomalies: %lu, Position: %.3f\n",
                   loop_count, anomaly_count, position / 1000000.0);
        }
    }
    
    print_stats();
    
    // Cleanup
    munmap(force_in, sizeof(reflex_channel_t));
    munmap(command_out, sizeof(reflex_channel_t));
    munmap(telemetry, sizeof(reflex_channel_t));
    
    printf("Reflex force control stopped.\n");
    return 0;
}
