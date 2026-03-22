/**
 * reflex_fabric_etm.h - Pure ETM Neural Fabric (NO CPU, NO ESP-IDF)
 *
 * ZERO DEPENDENCIES except bare metal headers.
 *
 * This is the autonomous neural substrate that runs WITHOUT cores:
 *
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                     PURE ETM NEURAL FABRIC                              │
 *   │                                                                         │
 *   │   Timer ──ETM──► GDMA ──► RMT ──► GPIO ──► PCNT                        │
 *   │     │                              │          │                         │
 *   │     │                              │          │                         │
 *   │     │                        (pulses)    (count)                        │
 *   │     │                              │          │                         │
 *   │     │                              │          ▼                         │
 *   │     │                              │    Thresholds fire                 │
 *   │     │                              │          │                         │
 *   │     │                              │     ETM channels                   │
 *   │     │                              │          │                         │
 *   │     │                              │          ▼                         │
 *   │     │                              │    GPIO outputs                    │
 *   │     │                              │    (neural result)                 │
 *   │     │                              │                                    │
 *   │     └──────────────────────────────┴──────── loop ◄────────────────────┤
 *   │                                                                         │
 *   │   CPU: WFI (sleep) - only wakes if ULP_WAKEUP triggered               │
 *   │                                                                         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * COMPUTATIONAL LIMITS:
 *   - PCNT thresholds ARE the activation function (piecewise linear)
 *   - No computed addressing (no LUT[value] without CPU)
 *   - Feedforward only (no feedback without CPU)
 *   - 8 threshold events → 8-bit output resolution
 *
 * WHAT THIS CAN COMPUTE:
 *   - Single-layer perceptron
 *   - Threshold detection
 *   - Wake-on-pattern (trigger CPU on detection)
 */

#ifndef REFLEX_FABRIC_ETM_H
#define REFLEX_FABRIC_ETM_H

#include <stdint.h>
#include "reflex_etm.h"
#include "reflex_pcnt.h"
#include "reflex_rmt.h"
#include "reflex_timer_hw.h"
#include "reflex_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

// Hardware resources
#define FABRIC_RMT_CHANNEL          0       // RMT TX channel
#define FABRIC_RMT_GPIO             4       // RMT output pin
#define FABRIC_PCNT_UNIT            0       // Primary PCNT unit
#define FABRIC_PCNT_GPIO            4       // PCNT input (same as RMT out)

// Output GPIOs (threshold results)
#define FABRIC_OUT_GPIO_0           10
#define FABRIC_OUT_GPIO_1           11
#define FABRIC_OUT_GPIO_2           12
#define FABRIC_OUT_GPIO_3           13
#define FABRIC_OUT_GPIO_4           14
#define FABRIC_OUT_GPIO_5           15
#define FABRIC_OUT_GPIO_6           16
#define FABRIC_OUT_GPIO_7           17

// ETM channels
#define ETM_CH_TIMER_TO_RMT         0       // Timer alarm → RMT start
#define ETM_CH_RMT_TO_RESET         1       // RMT done → Timer reload
#define ETM_CH_THRESH_TO_GPIO_0     2       // PCNT thresh → GPIO 0
#define ETM_CH_THRESH_TO_GPIO_1     3       // PCNT thresh → GPIO 1
#define ETM_CH_THRESH_TO_GPIO_2     4       // etc.
#define ETM_CH_THRESH_TO_GPIO_3     5
#define ETM_CH_THRESH_TO_GPIO_4     6
#define ETM_CH_THRESH_TO_GPIO_5     7
#define ETM_CH_THRESH_TO_GPIO_6     8
#define ETM_CH_THRESH_TO_GPIO_7     9
#define ETM_CH_PATTERN_WAKEUP       10      // Pattern match → wake CPU

// Pulse configuration
#define FABRIC_PULSE_HIGH_TICKS     5       // 500ns at 10MHz (div=8)
#define FABRIC_PULSE_LOW_TICKS      5       // 500ns gap
#define FABRIC_RMT_DIVIDER          8       // 80MHz / 8 = 10MHz

// Inference rate
#define FABRIC_PERIOD_US            10000   // 10ms = 100 Hz

// ============================================================
// Pulse Pattern Buffer (in RMT memory or SRAM)
// ============================================================

// Max pulses that fit in RMT memory (48 words)
#define FABRIC_MAX_PULSES           47

// ============================================================
// Perceptron Configuration
// ============================================================

/**
 * Sparse ternary weights encoded as pulse pattern
 *
 * For input x[7:0] and weights w[i] ∈ {-1, 0, +1}:
 *   - w = +1: add x pulses
 *   - w = -1: subtract (handled by dual PCNT or bias shift)
 *   - w = 0: no pulses
 *
 * The pulse pattern is precomputed for fixed weights.
 */
typedef struct {
    // Which inputs have +1 weight (bitmask)
    uint8_t pos_mask;
    
    // Which inputs have -1 weight (bitmask)
    uint8_t neg_mask;
    
    // Bias (in pulse count)
    int16_t bias;
    
} fabric_perceptron_t;

/**
 * Threshold configuration for activation
 *
 * 8 thresholds define a piecewise-linear activation.
 * When count crosses threshold[i], GPIO[i] goes high.
 */
typedef struct {
    int16_t thresh[8];      // Threshold values for each output bit
    uint8_t out_gpio[8];    // GPIO pin for each output
} fabric_thresholds_t;

// ============================================================
// Fabric State
// ============================================================

typedef struct {
    // Configuration
    fabric_perceptron_t perceptron;
    fabric_thresholds_t thresholds;
    
    // Inference count (for debugging, requires CPU to read)
    volatile uint32_t inference_count;
    
    // Current output (latched in GPIOs, this is shadow)
    uint8_t output;
    
} fabric_state_t;

// Global state (accessible for debugging)
static fabric_state_t g_fabric_state = {0};

// ============================================================
// Fabric Setup (runs ONCE, then CPU sleeps)
// ============================================================

/**
 * Configure the ETM crossbar for autonomous operation
 */
static inline void fabric_setup_etm(void) {
    // Disable all channels first
    etm_disable_all();
    
    // CH0: Timer alarm → RMT TX start
    etm_connect(ETM_CH_TIMER_TO_RMT, ETM_EVT_TIMER0_ALARM, ETM_TASK_RMT_TX_START);
    
    // CH1: RMT TX done → Timer reload (creates the loop)
    etm_connect(ETM_CH_RMT_TO_RESET, ETM_EVT_RMT_TX_END, ETM_TASK_TIMER0_RELOAD);
    
    // CH2-9: PCNT threshold → GPIO set
    // NOTE: ESP32-C6 only has ONE shared PCNT threshold event (ID 45)
    // This means we can't distinguish which threshold fired!
    // 
    // WORKAROUND: Use multiple PCNT units, each with different thresholds
    // PCNT0 thresh0 → GPIO0, PCNT1 thresh0 → GPIO1, etc.
    //
    // But event 45 fires for ANY threshold on ANY unit...
    // This is a hardware limitation we must work around.
    
    // For now, connect the single threshold event to one GPIO
    // Full 8-bit output requires more creative solutions
    etm_connect(ETM_CH_THRESH_TO_GPIO_0, ETM_EVT_PCNT_THRESH, ETM_TASK_GPIO_SET(0));
}

/**
 * Configure PCNT for pulse counting
 */
static inline void fabric_setup_pcnt(int16_t thresh0, int16_t thresh1) {
    // Initialize PCNT unit 0 to count pulses on RMT output GPIO
    pcnt_init_counter(FABRIC_PCNT_UNIT, FABRIC_PCNT_GPIO, thresh0, thresh1);
}

/**
 * Configure RMT for pulse generation
 */
static inline void fabric_setup_rmt(void) {
    rmt_init_tx(FABRIC_RMT_CHANNEL, FABRIC_RMT_GPIO, FABRIC_RMT_DIVIDER);
}

/**
 * Configure Timer for periodic triggers
 */
static inline void fabric_setup_timer(uint32_t period_us) {
    timer_init(period_us, 1);  // Auto-reload enabled
    timer_enable_etm_tasks();   // Allow ETM to control timer
}

/**
 * Configure output GPIOs
 */
static inline void fabric_setup_outputs(void) {
    gpio_set_output(FABRIC_OUT_GPIO_0);
    gpio_set_output(FABRIC_OUT_GPIO_1);
    gpio_set_output(FABRIC_OUT_GPIO_2);
    gpio_set_output(FABRIC_OUT_GPIO_3);
    gpio_set_output(FABRIC_OUT_GPIO_4);
    gpio_set_output(FABRIC_OUT_GPIO_5);
    gpio_set_output(FABRIC_OUT_GPIO_6);
    gpio_set_output(FABRIC_OUT_GPIO_7);
    
    // Start all low
    gpio_clear(FABRIC_OUT_GPIO_0);
    gpio_clear(FABRIC_OUT_GPIO_1);
    gpio_clear(FABRIC_OUT_GPIO_2);
    gpio_clear(FABRIC_OUT_GPIO_3);
    gpio_clear(FABRIC_OUT_GPIO_4);
    gpio_clear(FABRIC_OUT_GPIO_5);
    gpio_clear(FABRIC_OUT_GPIO_6);
    gpio_clear(FABRIC_OUT_GPIO_7);
}

/**
 * Load pulse pattern into RMT memory
 *
 * This encodes the perceptron weights as a pulse sequence.
 * Called during setup; the pattern then runs autonomously.
 */
static inline void fabric_load_pattern(int num_pulses) {
    rmt_write_pulses(FABRIC_RMT_CHANNEL, num_pulses, 
                     FABRIC_PULSE_HIGH_TICKS, FABRIC_PULSE_LOW_TICKS);
}

/**
 * Full fabric initialization
 */
static inline void fabric_init(int num_pulses, int16_t thresh0, int16_t thresh1) {
    // Configure peripherals
    fabric_setup_outputs();
    fabric_setup_rmt();
    fabric_setup_pcnt(thresh0, thresh1);
    fabric_setup_timer(FABRIC_PERIOD_US);
    
    // Load the pulse pattern
    fabric_load_pattern(num_pulses);
    
    // Wire up ETM crossbar
    fabric_setup_etm();
    
    // Start timer (this kicks off the autonomous loop)
    timer_start();
}

/**
 * Enter autonomous mode - CPU sleeps, fabric runs
 */
static inline void fabric_run_autonomous(void) {
    // Wait for interrupt (sleep)
    // The ETM fabric runs without us
    __asm__ volatile("wfi");
}

// ============================================================
// The Fundamental Limit
// ============================================================

/*
 * WHAT THE PURE ETM FABRIC CAN DO:
 *
 *   1. Generate a fixed pulse pattern (RMT)
 *   2. Count those pulses (PCNT)
 *   3. Fire an event when count hits a threshold (PCNT → ETM)
 *   4. Set a GPIO based on that event (ETM → GPIO)
 *   5. Loop forever (Timer → ETM → RMT → PCNT → ETM → Timer)
 *
 * WHAT IT CANNOT DO:
 *
 *   1. Change the pulse pattern based on the count (computed addressing)
 *   2. Read the PCNT value and use it to index memory (LUT lookup)
 *   3. Chain multiple computations where result[n] affects input[n+1]
 *   4. Store intermediate values for later use
 *
 * THE CORE LIMITATION:
 *
 *   ETM is a CROSSBAR, not a computer.
 *   It routes events to tasks. Fixed mapping.
 *   It cannot say "if count > X, do Y, else do Z".
 *
 * THEREFORE:
 *
 *   The pure ETM fabric can compute THRESHOLD DETECTION.
 *   It cannot compute FEEDBACK (RNN/CfC).
 *   It cannot compute MULTI-LAYER (deep networks).
 *
 * FOR THOSE, WE NEED A CORE (LP or HP).
 * BUT: A core uses > 100 μW, ruling out RF harvesting.
 *
 * THE 20 μW SOLUTION IS THRESHOLD DETECTION.
 */

#ifdef __cplusplus
}
#endif

#endif // REFLEX_FABRIC_ETM_H
