/**
 * reflex_timer.h - Hardware Timers as Channels for ESP32-C6
 *
 * Timers are channels that signal periodically.
 *
 * Pure cycle-counter approach - no ESP-IDF timer driver.
 * The CPU cycle counter IS the timer channel.
 */

#ifndef REFLEX_TIMER_H
#define REFLEX_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "reflex.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Timer Channel Structure (cycle-based)
// ============================================================

typedef struct {
    uint32_t period_cycles;         // Period in CPU cycles
    uint32_t last_cycles;           // Last trigger cycle count
    uint64_t count;                 // Number of signals received
} reflex_timer_channel_t;

// ============================================================
// Timer Channel API
// ============================================================

/**
 * Initialize timer channel with specified period
 *
 * @param timer     Timer channel structure
 * @param group     Ignored (for API compat)
 * @param timer_id  Ignored (for API compat)
 * @param period_us Period in microseconds
 */
static inline void timer_channel_init(reflex_timer_channel_t* timer,
                                       uint8_t group,
                                       uint8_t timer_id,
                                       uint32_t period_us) {
    (void)group;
    (void)timer_id;

    // At 160MHz, 1us = 160 cycles
    timer->period_cycles = period_us * 160;
    timer->last_cycles = reflex_cycles();
    timer->count = 0;
}

/**
 * Check if timer period has elapsed (non-blocking)
 */
static inline bool timer_check(reflex_timer_channel_t* timer) {
    return (reflex_cycles() - timer->last_cycles) >= timer->period_cycles;
}

/**
 * Wait for timer period to elapse (blocking)
 * This is a reflex: hardware signals (cycle counter), we respond
 */
static inline void timer_wait(reflex_timer_channel_t* timer) {
    while ((reflex_cycles() - timer->last_cycles) < timer->period_cycles) {
        __asm__ volatile("nop");
    }
    timer->last_cycles += timer->period_cycles;  // Maintain phase
    timer->count++;
}

/**
 * Try wait - non-blocking check and clear
 * Returns true if period elapsed
 */
static inline bool timer_try_wait(reflex_timer_channel_t* timer) {
    if (timer_check(timer)) {
        timer->last_cycles += timer->period_cycles;
        timer->count++;
        return true;
    }
    return false;
}

// ============================================================
// Simple Cycle-Based Delay
// ============================================================

/**
 * Busy-wait for specified number of CPU cycles
 * At 160MHz, 160 cycles = 1us
 */
static inline void delay_cycles(uint32_t cycles) {
    uint32_t start = reflex_cycles();
    while ((reflex_cycles() - start) < cycles) {
        __asm__ volatile("nop");
    }
}

/**
 * Busy-wait for specified microseconds
 */
static inline void delay_us(uint32_t us) {
    delay_cycles(us * 160);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_TIMER_H
