/**
 * reflex_timer_hw.h - Bare Metal Timer for ESP32-C6
 *
 * ZERO DEPENDENCIES. Direct register access only.
 *
 * The timer provides the heartbeat for autonomous operation:
 *   - Periodic alarm events
 *   - ETM triggers without CPU
 *   - Configurable period from microseconds to seconds
 */

#ifndef REFLEX_TIMER_HW_H
#define REFLEX_TIMER_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Timer Group Register Addresses (from ESP32-C6 TRM)
// ============================================================

// Timer Group 0
#define TIMG0_BASE                  0x60008000

// Timer 0 within Group 0
#define TIMG0_T0_CONFIG             (TIMG0_BASE + 0x0000)
#define TIMG0_T0_LO                 (TIMG0_BASE + 0x0004)
#define TIMG0_T0_HI                 (TIMG0_BASE + 0x0008)
#define TIMG0_T0_UPDATE             (TIMG0_BASE + 0x000C)
#define TIMG0_T0_ALARMLO            (TIMG0_BASE + 0x0010)
#define TIMG0_T0_ALARMHI            (TIMG0_BASE + 0x0014)
#define TIMG0_T0_LOADLO             (TIMG0_BASE + 0x0018)
#define TIMG0_T0_LOADHI             (TIMG0_BASE + 0x001C)
#define TIMG0_T0_LOAD               (TIMG0_BASE + 0x0020)

// Watchdog (not used for neural fabric)
#define TIMG0_WDTCONFIG0            (TIMG0_BASE + 0x0024)

// Interrupt registers
#define TIMG0_INT_ENA               (TIMG0_BASE + 0x0060)
#define TIMG0_INT_RAW               (TIMG0_BASE + 0x0064)
#define TIMG0_INT_ST                (TIMG0_BASE + 0x0068)
#define TIMG0_INT_CLR               (TIMG0_BASE + 0x006C)

// ETM task/event configuration
#define TIMG0_ETM_TASK_EN           (TIMG0_BASE + 0x0070)

// Timer Group 1 (same layout, different base)
#define TIMG1_BASE                  0x60009000

// ============================================================
// T0_CONFIG Register Bits
// ============================================================

#define TIMG_T0_USE_XTAL            (1 << 9)   // Use XTAL (40MHz) vs APB (80MHz)
#define TIMG_T0_ALARM_EN            (1 << 10)  // Enable alarm
#define TIMG_T0_DIVCNT_RST          (1 << 12)  // Reset divider counter
#define TIMG_T0_DIVIDER_SHIFT       13         // Divider (bits 28:13)
#define TIMG_T0_DIVIDER_MASK        0xFFFF
#define TIMG_T0_AUTORELOAD          (1 << 29)  // Auto-reload on alarm
#define TIMG_T0_INCREASE            (1 << 30)  // Count up (vs down)
#define TIMG_T0_EN                  (1 << 31)  // Enable timer

// ============================================================
// Interrupt bits
// ============================================================

#define TIMG_T0_INT                 (1 << 0)   // Timer 0 interrupt
#define TIMG_WDT_INT                (1 << 1)   // Watchdog interrupt

// ============================================================
// ETM Task Enable bits
// ============================================================

#define TIMG_ETM_T0_START           (1 << 0)   // ETM can start timer 0
#define TIMG_ETM_T0_ALARM_ARM       (1 << 1)   // ETM can arm alarm
#define TIMG_ETM_T0_STOP            (1 << 2)   // ETM can stop timer
#define TIMG_ETM_T0_RELOAD          (1 << 3)   // ETM can reload timer

// ============================================================
// Direct Register Access
// ============================================================

#define TIMER_REG(addr)             (*(volatile uint32_t*)(addr))

// ============================================================
// Timer API
// ============================================================

/**
 * Initialize timer with specified period
 *
 * @param period_us   Period in microseconds
 * @param auto_reload Whether to auto-reload on alarm
 */
static inline void timer_init(uint32_t period_us, int auto_reload) {
    // Disable timer first
    TIMER_REG(TIMG0_T0_CONFIG) = 0;
    
    // Use XTAL clock (40 MHz) with divider 40 = 1 MHz = 1 tick/μs
    uint32_t divider = 40;
    
    uint32_t config = 0;
    config |= TIMG_T0_USE_XTAL;                         // Use 40 MHz XTAL
    config |= (divider << TIMG_T0_DIVIDER_SHIFT);       // Divider = 40
    config |= TIMG_T0_INCREASE;                         // Count up
    config |= TIMG_T0_ALARM_EN;                         // Enable alarm
    if (auto_reload) {
        config |= TIMG_T0_AUTORELOAD;                   // Auto-reload
    }
    
    TIMER_REG(TIMG0_T0_CONFIG) = config;
    
    // Set alarm value (in ticks, 1 tick = 1 μs with divider=40)
    TIMER_REG(TIMG0_T0_ALARMLO) = period_us;
    TIMER_REG(TIMG0_T0_ALARMHI) = 0;
    
    // Set reload value to 0
    TIMER_REG(TIMG0_T0_LOADLO) = 0;
    TIMER_REG(TIMG0_T0_LOADHI) = 0;
    
    // Load the reload value
    TIMER_REG(TIMG0_T0_LOAD) = 1;
    
    // Clear any pending interrupt
    TIMER_REG(TIMG0_INT_CLR) = TIMG_T0_INT;
}

/**
 * Enable ETM tasks for the timer
 * This allows ETM to control the timer without CPU
 */
static inline void timer_enable_etm_tasks(void) {
    TIMER_REG(TIMG0_ETM_TASK_EN) = 
        TIMG_ETM_T0_START | 
        TIMG_ETM_T0_ALARM_ARM | 
        TIMG_ETM_T0_STOP | 
        TIMG_ETM_T0_RELOAD;
}

/**
 * Start the timer
 */
static inline void timer_start(void) {
    TIMER_REG(TIMG0_T0_CONFIG) |= TIMG_T0_EN;
}

/**
 * Stop the timer
 */
static inline void timer_stop(void) {
    TIMER_REG(TIMG0_T0_CONFIG) &= ~TIMG_T0_EN;
}

/**
 * Check if alarm has fired
 */
static inline int timer_alarm_fired(void) {
    return (TIMER_REG(TIMG0_INT_RAW) & TIMG_T0_INT) != 0;
}

/**
 * Clear alarm interrupt
 */
static inline void timer_clear_alarm(void) {
    TIMER_REG(TIMG0_INT_CLR) = TIMG_T0_INT;
}

/**
 * Read current timer value
 */
static inline uint64_t timer_read(void) {
    // Trigger update to latch current value
    TIMER_REG(TIMG0_T0_UPDATE) = 1;
    // Small delay for latch
    __asm__ volatile("nop; nop; nop; nop");
    
    uint32_t lo = TIMER_REG(TIMG0_T0_LO);
    uint32_t hi = TIMER_REG(TIMG0_T0_HI);
    return ((uint64_t)hi << 32) | lo;
}

/**
 * Set alarm value (in ticks, typically μs if divider=40)
 */
static inline void timer_set_alarm(uint64_t ticks) {
    TIMER_REG(TIMG0_T0_ALARMLO) = (uint32_t)ticks;
    TIMER_REG(TIMG0_T0_ALARMHI) = (uint32_t)(ticks >> 32);
}

/**
 * Reload timer to initial value
 */
static inline void timer_reload(void) {
    TIMER_REG(TIMG0_T0_LOAD) = 1;
}

/**
 * Enable alarm interrupt
 */
static inline void timer_enable_interrupt(void) {
    TIMER_REG(TIMG0_INT_ENA) |= TIMG_T0_INT;
}

/**
 * Disable alarm interrupt
 */
static inline void timer_disable_interrupt(void) {
    TIMER_REG(TIMG0_INT_ENA) &= ~TIMG_T0_INT;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_TIMER_HW_H
