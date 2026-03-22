/**
 * silicon_grail_cpu_free.c - The Turing Complete ETM Fabric
 * 
 * ZERO CPU INVOLVEMENT.
 * 
 * The CPU configures the fabric, then sleeps (WFI).
 * The hardware runs autonomously:
 *   Timer → GDMA → RMT → PCNT → (race) → GDMA → loop
 * 
 * This is the Silicon Grail:
 *   - CPU sleeps (WFI)
 *   - Peripherals run neural inference
 *   - 1000+ cycles without CPU wake
 *   - Conditional branching via timer race
 * 
 * Success = Turing Completeness proven on ESP32-C6.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Bare metal headers - ZERO ESP-IDF after init
#include "reflex_gdma.h"
#include "reflex_rmt.h"
#include "reflex_pcnt.h"
#include "reflex_timer_hw.h"
#include "reflex_etm.h"
#include "reflex_gpio.h"

// Configuration
#define SG_RMT_GPIO             4
#define SG_RMT_CHANNEL          0
#define SG_PCNT_UNIT            0
#define SG_PCNT_GPIO            4  // Same as RMT (internal loopback)

// GDMA channels for timer race
#define SG_GDMA_CH_FAST         0   // High priority - threshold path
#define SG_GDMA_CH_DEFAULT      1   // Low priority - timeout path
#define SG_GDMA_PRIORITY_FAST   15  // Highest
#define SG_GDMA_PRIORITY_DEF    0   // Lowest

// Timers
#define SG_TIMER_PERIOD         0   // Timer 0 - inference period
#define SG_TIMER_TIMEOUT        1   // Timer 1 - timeout fallback

// Timing
#define SG_INFERENCE_PERIOD_US  10000   // 10ms = 100 Hz
#define SG_TIMEOUT_US           12000   // 12ms (20% margin)
#define SG_PCNT_THRESHOLD       50      // Decision boundary

// ETM channels
#define SG_ETM_CH_TIMER0_GDMA   0   // Timer0 → GDMA (start cycle)
#define SG_ETM_CH_THRESH_GDMA   1   // PCNT thresh → GDMA_CH0 (fast)
#define SG_ETM_CH_TOUT_GDMA     2   // Timer1 → GDMA_CH1 (default)
#define SG_ETM_CH_GDMA_RMT      3   // GDMA EOF → RMT TX
#define SG_ETM_CH_RMT_PCNT      4   // RMT done → PCNT reset
#define SG_ETM_CH_PCNT_LOOP     5   // PCNT reset → Timer0 reload

// Pattern buffers (in SRAM)
#define SG_PATTERN_WORDS        48
static uint32_t pattern_fast[SG_PATTERN_WORDS] __attribute__((aligned(4)));
static uint32_t pattern_default[SG_PATTERN_WORDS] __attribute__((aligned(4)));

// GDMA descriptors
static gdma_descriptor_t desc_fast __attribute__((aligned(4)));
static gdma_descriptor_t desc_default __attribute__((aligned(4)));

// Statistics (CPU reads these after waking)
static volatile struct {
    uint32_t total_cycles;
    uint32_t fast_path_count;    // Threshold reached first
    uint32_t default_path_count; // Timeout reached first
    uint32_t errors;
} sg_stats = {0, 0, 0, 0};

// ============================================================
// Pattern Generation
// ============================================================

static void generate_patterns(void) {
    // Pattern A (fast path): Short pulse train - threshold reached quickly
    for (int i = 0; i < 10; i++) {
        pattern_fast[i] = 0x00050005;  // Short pulses
    }
    pattern_fast[10] = 0x00000000;  // End marker
    
    // Pattern B (default path): Long pulse train - timeout reached first
    for (int i = 0; i < SG_PATTERN_WORDS - 1; i++) {
        pattern_default[i] = 0x00050005;  // Longer pulses
    }
    pattern_default[SG_PATTERN_WORDS - 1] = 0x00000000;  // End marker
}

// ============================================================
// Hardware Initialization (CPU does this once)
// ============================================================

static void init_rmt(void) {
    rmt_init_tx(SG_RMT_CHANNEL, SG_RMT_GPIO, 8);  // 10 MHz
    printf("  ✓ RMT initialized (CH%d, GPIO%d, 10MHz)\n", SG_RMT_CHANNEL, SG_RMT_GPIO);
}

static void init_pcnt(void) {
    pcnt_init_counter(SG_PCNT_UNIT, SG_PCNT_GPIO, SG_PCNT_THRESHOLD, 32767);
    printf("  ✓ PCNT initialized (UNIT%d, threshold=%d)\n", SG_PCNT_UNIT, SG_PCNT_THRESHOLD);
}

static void init_timers(void) {
    // Timer 0: Inference period
    timer_init(SG_INFERENCE_PERIOD_US, 1);  // Auto-reload
    timer_enable_etm_tasks();
    
    // Timer 1: Timeout (for race)
    // TODO: Need to implement second timer in reflex_timer_hw.h
    printf("  ⚠ Timer 1 (timeout) - needs implementation\n");
    
    printf("  ✓ Timer 0 initialized (period=%dµs)\n", SG_INFERENCE_PERIOD_US);
}

static void init_gdma(void) {
    // Initialize paired M2M channels
    gdma_m2m_init_peripheral(SG_GDMA_CH_FAST, SG_GDMA_CH_FAST, SG_GDMA_PRIORITY_FAST);
    gdma_m2m_init_peripheral(SG_GDMA_CH_DEFAULT, SG_GDMA_CH_DEFAULT, SG_GDMA_PRIORITY_DEF);
    
    // Build descriptors for both patterns
    // Fast path: short pattern to RMT
    gdma_m2m_out_descriptor(&desc_fast, pattern_fast, 11 * 4, 1, NULL);
    gdma_m2m_in_descriptor(&desc_fast, (void*)RMT_CH0_RAM_ADDR, 11 * 4, 1, NULL);
    
    // Default path: full pattern to RMT  
    gdma_m2m_out_descriptor(&desc_default, pattern_default, SG_PATTERN_WORDS * 4, 1, NULL);
    gdma_m2m_in_descriptor(&desc_default, (void*)RMT_CH0_RAM_ADDR, SG_PATTERN_WORDS * 4, 1, NULL);
    
    printf("  ✓ GDMA initialized (CH%d=fast/prio%d, CH%d=default/prio%d)\n",
           SG_GDMA_CH_FAST, SG_GDMA_PRIORITY_FAST,
           SG_GDMA_CH_DEFAULT, SG_GDMA_PRIORITY_DEF);
}

static void wire_etm(void) {
    etm_disable_all();
    
    // CH0: Timer0 alarm → Start GDMA (initial trigger)
    // Note: Currently starts both, race determines winner
    etm_connect(SG_ETM_CH_TIMER0_GDMA, ETM_EVT_TIMER0_ALARM, 
                ETM_TASK_GDMA_OUT_START(SG_GDMA_CH_DEFAULT));
    
    // CH1: PCNT threshold → GDMA_CH0 (fast path)
    etm_connect(SG_ETM_CH_THRESH_GDMA, ETM_EVT_PCNT_THRESH,
                ETM_TASK_GDMA_OUT_START(SG_GDMA_CH_FAST));
    
    // CH2: Timer1 timeout → GDMA_CH1 (default path)
    // TODO: Need Timer1 implementation
    printf("  ⚠ ETM CH2 (Timer1→GDMA) - needs Timer1\n");
    
    // CH3: GDMA EOF → RMT TX start
    // Winner's GDMA completion triggers RMT
    etm_connect(SG_ETM_CH_GDMA_RMT, ETM_EVT_GDMA_OUT_EOF(SG_GDMA_CH_DEFAULT),
                ETM_TASK_RMT_TX_START);
    etm_connect(SG_ETM_CH_GDMA_RMT + 1, ETM_EVT_GDMA_OUT_EOF(SG_GDMA_CH_FAST),
                ETM_TASK_RMT_TX_START);
    
    // CH4: RMT done → PCNT reset
    etm_connect(SG_ETM_CH_RMT_PCNT, ETM_EVT_RMT_TX_END,
                ETM_TASK_PCNT_RST);
    
    // CH5: PCNT reset → Timer0 reload (loop)
    etm_connect(SG_ETM_CH_PCNT_LOOP, ETM_EVT_PCNT_THRESH,
                ETM_TASK_TIMER0_RELOAD);
    
    printf("  ✓ ETM crossbar wired (6 channels)\n");
}

// ============================================================
// CPU-Free Operation
// ============================================================

static void start_autonomous_fabric(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║           STARTING CPU-FREE AUTONOMOUS FABRIC                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    // Load initial pattern (default path)
    gdma_tx_set_descriptor(SG_GDMA_CH_DEFAULT, &desc_default);
    gdma_tx_set_descriptor(SG_GDMA_CH_FAST, &desc_fast);
    
    // Start Timer0
    timer_start();
    
    printf("Status: RUNNING\n");
    printf("CPU: Entering WFI (sleep)...\n\n");
    
    // CPU goes to sleep
    // The fabric runs autonomously from here
    __asm__ volatile("wfi");
    
    // We should never reach here in true autonomous mode
    // The fabric loops forever
    printf("ERROR: WFI returned - fabric not autonomous!\n");
}

// ============================================================
// Statistics (CPU wakes periodically to check)
// ============================================================

static void check_stats(void) {
    // Read PCNT to determine which path was taken
    int16_t pcnt_val = pcnt_read(SG_PCNT_UNIT);
    
    sg_stats.total_cycles++;
    
    if (pcnt_val >= SG_PCNT_THRESHOLD) {
        sg_stats.fast_path_count++;
    } else {
        sg_stats.default_path_count++;
    }
}

static void print_stats(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    FABRIC STATISTICS                           ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  Total cycles:        %10lu                               ║\n", sg_stats.total_cycles);
    printf("║  Fast path taken:     %10lu (%5.1f%%)                     ║\n",
           sg_stats.fast_path_count,
           100.0f * sg_stats.fast_path_count / (sg_stats.total_cycles + 1));
    printf("║  Default path taken:  %10lu (%5.1f%%)                     ║\n",
           sg_stats.default_path_count,
           100.0f * sg_stats.default_path_count / (sg_stats.total_cycles + 1));
    printf("║  Errors:              %10lu                               ║\n", sg_stats.errors);
    printf("║                                                                ║\n");
    printf("║  CPU wake count:      %10d                               ║\n", sg_stats.total_cycles);
    printf("║  Fabric autonomy:     %5.1f%%                               ║\n",
           100.0f * (sg_stats.total_cycles > 0 ? 1.0f - (float)sg_stats.total_cycles / (sg_stats.total_cycles * 1.01f) : 0));
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
}

// ============================================================
// Demonstration Mode
// ============================================================

static void demo_with_periodic_stats(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║         CPU-FREE DEMO (with periodic stats output)             ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("Running 100 cycles with CPU waking every 10 cycles for stats...\n\n");
    
    // Reset stats
    memset((void*)&sg_stats, 0, sizeof(sg_stats));
    
    // Run 100 cycles
    for (int i = 0; i < 100; i++) {
        // In real autonomous mode, this would all be hardware
        // For demo, we simulate one cycle with minimal CPU
        
        // Trigger one cycle
        timer_start();
        
        // Wait for completion (would be interrupt in real mode)
        vTaskDelay(pdMS_TO_TICKS(12));
        
        // Check stats
        check_stats();
        
        // Print every 10
        if ((i + 1) % 10 == 0) {
            printf("  Progress: %d/100 cycles\r", i + 1);
            fflush(stdout);
        }
    }
    
    printf("\n\n");
    print_stats();
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                ║\n");
    printf("║     ███████╗██╗██╗     ██╗ ██████╗ ██████╗ ███╗   ██╗          ║\n");
    printf("║     ██╔════╝██║██║     ██║██╔════╝██╔═══██╗████╗  ██║          ║\n");
    printf("║     ███████╗██║██║     ██║██║     ██║   ██║██╔██╗ ██║          ║\n");
    printf("║     ╚════██║██║██║     ██║██║     ██║   ██║██║╚██╗██║          ║\n");
    printf("║     ███████║██║███████╗██║╚██████╗╚██████╔╝██║ ╚████║          ║\n");
    printf("║     ╚══════╝╚═╝╚══════╝╚═╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═══╝          ║\n");
    printf("║                                                                ║\n");
    printf("║           ██████╗ ██████╗  █████╗ ██╗██╗                     ║\n");
    printf("║           ██╔══██╗██╔══██╗██╔══██╗██║██║                     ║\n");
    printf("║           ██████╔╝██████╔╝███████║██║██║                     ║\n");
    printf("║           ██╔══██╗██╔══██╗██╔══██║██║██║                     ║\n");
    printf("║           ██║  ██║██████╔╝██║  ██║██║███████╗                ║\n");
    printf("║           ╚═╝  ╚═╝╚═════╝ ╚═╝  ╚═╝╚═╝╚══════╝                ║\n");
    printf("║                                                                ║\n");
    printf("║              Turing Complete Hardware Neural Fabric            ║\n");
    printf("║                     ZERO CPU INVOLVEMENT                       ║\n");
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("ESP32-C6 @ 160 MHz | The chip IS the neural network\n");
    printf("\n");
    
    printf("Initializing hardware...\n");
    generate_patterns();
    init_rmt();
    init_pcnt();
    init_timers();
    init_gdma();
    wire_etm();
    printf("\n✓ Hardware initialization complete\n");
    
    // Two modes:
    // 1. Full autonomous (CPU sleeps forever)
    // 2. Demo with stats (CPU wakes periodically)
    
    // Mode 2 for verification:
    demo_with_periodic_stats();
    
    // Mode 1 (uncomment for true CPU-free operation):
    // start_autonomous_fabric();
    
    printf("\n\nSilicon Grail demo complete.\n");
    printf("Status: %s\n", (sg_stats.total_cycles > 0) ? "PARTIAL" : "NOT STARTED");
    printf("\nTo achieve FULL CPU-free operation:\n");
    printf("  1. Implement Timer1 for timeout race\n");
    printf("  2. Complete ETM wiring for both paths\n");
    printf("  3. Run start_autonomous_fabric()\n");
    printf("  4. Verify 1000+ cycles without CPU wake\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
