/**
 * reflex_c6.h - The Reflex Becomes the ESP32-C6
 *
 * This header IS the C6. Every peripheral is a channel.
 * The hardware already thinks in signals. We're just listening.
 *
 * Include this one header to access the entire chip as channels.
 */

#ifndef REFLEX_C6_H
#define REFLEX_C6_H

// Core primitive
#include "reflex.h"

// Hardware channels
#include "reflex_gpio.h"
#include "reflex_timer.h"
#include "reflex_adc.h"
#include "reflex_spline.h"
#include "reflex_spi.h"
#include "reflex_wifi.h"

// Computational substrate
#include "reflex_void.h"
#include "reflex_echip.h"

// The C6 is complete. Every peripheral is a channel.
// #include "reflex_spi.h"
// #include "reflex_uart.h"
// #include "reflex_wifi.h"
// #include "reflex_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 Identity
// ============================================================

#define REFLEX_CHIP_NAME    "ESP32-C6"
#define REFLEX_CHIP_FREQ    160000000   // 160 MHz
#define REFLEX_CYCLE_NS     6           // ~6.25 ns per cycle

// ============================================================
// Pin Assignments (ESP32-C6-DevKitC-1)
// ============================================================

// Onboard LED (directly controllable)
#define PIN_LED             8

// USB Serial/JTAG (directly controllable)
#define PIN_USB_DN          12
#define PIN_USB_DP          13

// Boot button
#define PIN_BOOT            9

// ============================================================
// Quick Setup Macros
// ============================================================

/**
 * Initialize minimal hardware for reflex operation
 */
static inline void reflex_c6_init(void) {
    // Nothing required - peripherals are memory-mapped
    // Just configure pins as needed
}

/**
 * Setup the onboard LED as output channel
 */
static inline void reflex_led_init(void) {
    gpio_set_output(PIN_LED);
}

/**
 * Toggle the onboard LED
 */
static inline void reflex_led_toggle(void) {
    gpio_toggle(PIN_LED);
}

/**
 * Set the onboard LED
 */
static inline void reflex_led_set(bool on) {
    gpio_write(PIN_LED, on);
}

// ============================================================
// Critical Sections - Interrupt-Free Execution
// ============================================================

/**
 * Disable interrupts for deterministic timing.
 * Returns previous interrupt state for restoration.
 *
 * USE SPARINGLY: WiFi, USB, and other subsystems need interrupts.
 * Only disable for short, time-critical sections.
 */
static inline uint32_t reflex_enter_critical(void) {
    uint32_t mstatus;
    __asm__ volatile (
        "csrrci %0, mstatus, 0x8"  // Clear MIE bit, return old value
        : "=r"(mstatus)
        :
        : "memory"
    );
    return mstatus;
}

/**
 * Restore interrupts after critical section.
 * Pass the value returned by reflex_enter_critical().
 */
static inline void reflex_exit_critical(uint32_t saved_state) {
    __asm__ volatile (
        "csrw mstatus, %0"
        :
        : "r"(saved_state)
        : "memory"
    );
}

/**
 * Run a tight timing loop with interrupts disabled.
 * For precise periodic execution without jitter.
 *
 * @param period_cycles  Cycles per iteration
 * @param iterations     Number of iterations
 * @param callback       Function to call each iteration
 * @param ctx            Context passed to callback
 *
 * WARNING: Interrupts are disabled for the entire duration.
 * Keep iterations * period_cycles short (< 1ms recommended).
 */
typedef void (*reflex_loop_fn)(void* ctx, uint32_t iteration);

static inline void reflex_tight_loop(uint32_t period_cycles,
                                      uint32_t iterations,
                                      reflex_loop_fn callback,
                                      void* ctx) {
    uint32_t saved = reflex_enter_critical();
    uint32_t next = reflex_cycles() + period_cycles;

    for (uint32_t i = 0; i < iterations; i++) {
        // Wait for next period
        while (reflex_cycles() < next) {
            __asm__ volatile("nop");
        }
        next += period_cycles;

        // Execute callback
        callback(ctx, i);
    }

    reflex_exit_critical(saved);
}

// ============================================================
// Jitter Measurement
// ============================================================

typedef struct {
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t sum_cycles;
    uint32_t count;
    float jitter_percent;
    float actual_freq_hz;
} reflex_jitter_stats_t;

/**
 * Measure jitter of a tight loop.
 * Runs with interrupts disabled for accurate measurement.
 */
static inline reflex_jitter_stats_t reflex_measure_jitter(
    uint32_t period_cycles,
    uint32_t iterations) {

    reflex_jitter_stats_t stats = {
        .min_cycles = UINT32_MAX,
        .max_cycles = 0,
        .sum_cycles = 0,
        .count = 0
    };

    uint32_t saved = reflex_enter_critical();
    uint32_t next = reflex_cycles() + period_cycles;
    uint32_t last = reflex_cycles();

    for (uint32_t i = 0; i < iterations; i++) {
        // Wait for next period
        while (reflex_cycles() < next) {
            __asm__ volatile("nop");
        }

        uint32_t now = reflex_cycles();
        uint32_t actual = now - last;
        last = now;
        next += period_cycles;

        // Skip first iteration (warmup)
        if (i > 0) {
            if (actual < stats.min_cycles) stats.min_cycles = actual;
            if (actual > stats.max_cycles) stats.max_cycles = actual;
            stats.sum_cycles += actual;
            stats.count++;
        }
    }

    reflex_exit_critical(saved);

    // Calculate stats
    if (stats.count > 0) {
        uint32_t avg = (uint32_t)(stats.sum_cycles / stats.count);
        stats.jitter_percent = 100.0f * (float)(stats.max_cycles - stats.min_cycles) / (float)avg;
        stats.actual_freq_hz = (float)REFLEX_CHIP_FREQ / (float)avg;
    }

    return stats;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_C6_H
