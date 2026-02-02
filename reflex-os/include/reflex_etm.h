/**
 * reflex_etm.h - Bare Metal ETM (Event Task Matrix) for ESP32-C6
 *
 * ZERO DEPENDENCIES. Direct register access only.
 *
 * The ETM is a hardware crossbar that routes events to tasks:
 *   - 50 channels
 *   - Each channel: one event → one task
 *   - No CPU involved once configured
 *
 * This is the heart of the autonomous neural fabric.
 */

#ifndef REFLEX_ETM_H
#define REFLEX_ETM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ETM Register Addresses (from ESP32-C6 TRM)
// ============================================================

#define ETM_BASE                    0x60013000

// Channel enable registers (bits 0-31 for channels 0-31)
#define ETM_CH_ENA_AD0              (ETM_BASE + 0x0000)  // Read current state
#define ETM_CH_ENA_AD0_SET          (ETM_BASE + 0x0004)  // Write 1 to enable
#define ETM_CH_ENA_AD0_CLR          (ETM_BASE + 0x0008)  // Write 1 to disable

// Channel enable registers (bits 0-17 for channels 32-49)
#define ETM_CH_ENA_AD1              (ETM_BASE + 0x000C)
#define ETM_CH_ENA_AD1_SET          (ETM_BASE + 0x0010)
#define ETM_CH_ENA_AD1_CLR          (ETM_BASE + 0x0014)

// Channel configuration (event ID and task ID)
// Each channel has two 32-bit registers at offset 0x18 + (ch * 8)
#define ETM_CH_EVT_ID(ch)           (ETM_BASE + 0x0018 + ((ch) * 8))
#define ETM_CH_TASK_ID(ch)          (ETM_BASE + 0x001C + ((ch) * 8))

// ============================================================
// ETM Event IDs (triggers)
// ============================================================

// GPIO events (8 channels, 3 edge types each)
#define ETM_EVT_GPIO_RISE(ch)       (1 + (ch))          // 1-8
#define ETM_EVT_GPIO_FALL(ch)       (9 + (ch))          // 9-16
#define ETM_EVT_GPIO_ANY(ch)        (17 + (ch))         // 17-24

// PCNT events (shared across all units)
#define ETM_EVT_PCNT_THRESH         45      // Count equals threshold
#define ETM_EVT_PCNT_LIMIT          46      // Count equals limit
#define ETM_EVT_PCNT_ZERO           47      // Count equals zero

// Timer events
#define ETM_EVT_TIMER0_ALARM        48      // Timer 0 alarm
#define ETM_EVT_TIMER1_ALARM        49      // Timer 1 alarm
#define ETM_EVT_SYSTIMER_CMP0       50      // Systimer comparator 0
#define ETM_EVT_SYSTIMER_CMP1       51      // Systimer comparator 1
#define ETM_EVT_SYSTIMER_CMP2       52      // Systimer comparator 2

// RMT events
#define ETM_EVT_RMT_TX_END          53      // RMT TX done
#define ETM_EVT_RMT_TX_LOOP         54      // RMT TX loop done
#define ETM_EVT_RMT_RX_END          55      // RMT RX done
#define ETM_EVT_RMT_TX_THRESH       56      // RMT TX threshold
#define ETM_EVT_RMT_RX_THRESH       57      // RMT RX threshold

// GDMA events
#define ETM_EVT_GDMA_IN_DONE(ch)    (138 + (ch))        // 138-140
#define ETM_EVT_GDMA_IN_EOF(ch)     (141 + (ch))        // 141-143
#define ETM_EVT_GDMA_OUT_DONE(ch)   (150 + (ch))        // 150-152
#define ETM_EVT_GDMA_OUT_EOF(ch)    (153 + (ch))        // 153-155

// ============================================================
// ETM Task IDs (actions)
// ============================================================

// GPIO tasks (8 channels, 3 action types each)
#define ETM_TASK_GPIO_SET(ch)       (1 + (ch))          // 1-8
#define ETM_TASK_GPIO_CLR(ch)       (9 + (ch))          // 9-16
#define ETM_TASK_GPIO_TOG(ch)       (17 + (ch))         // 17-24

// PCNT tasks
#define ETM_TASK_PCNT_START         83      // Start counting
#define ETM_TASK_PCNT_STOP          84      // Stop counting
#define ETM_TASK_PCNT_INC           85      // Increment count
#define ETM_TASK_PCNT_DEC           86      // Decrement count
#define ETM_TASK_PCNT_RST           87      // Reset count to 0

// Timer tasks
#define ETM_TASK_TIMER0_START       88      // Start timer 0
#define ETM_TASK_TIMER1_START       89      // Start timer 1
#define ETM_TASK_TIMER0_ALARM       90      // Arm timer 0 alarm
#define ETM_TASK_TIMER1_ALARM       91      // Arm timer 1 alarm
#define ETM_TASK_TIMER0_STOP        92      // Stop timer 0
#define ETM_TASK_TIMER1_STOP        93      // Stop timer 1
#define ETM_TASK_TIMER0_RELOAD      94      // Reload timer 0
#define ETM_TASK_TIMER1_RELOAD      95      // Reload timer 1

// RMT tasks
#define ETM_TASK_RMT_TX_START       98      // Start RMT TX
#define ETM_TASK_RMT_TX_STOP        99      // Stop RMT TX
#define ETM_TASK_RMT_RX_DONE        100     // RMT RX done ack
#define ETM_TASK_RMT_RX_START       101     // Start RMT RX

// GDMA tasks
#define ETM_TASK_GDMA_IN_START(ch)  (159 + (ch))        // 159-161
#define ETM_TASK_GDMA_OUT_START(ch) (162 + (ch))        // 162-164

// ULP/PMU tasks
#define ETM_TASK_ULP_WAKEUP         154     // Wake ULP/main CPU

// ============================================================
// Direct Register Access
// ============================================================

#define ETM_REG(addr)               (*(volatile uint32_t*)(addr))

// ============================================================
// ETM API
// ============================================================

/**
 * Enable an ETM channel
 */
static inline void etm_channel_enable(uint8_t channel) {
    if (channel < 32) {
        ETM_REG(ETM_CH_ENA_AD0_SET) = (1 << channel);
    } else {
        ETM_REG(ETM_CH_ENA_AD1_SET) = (1 << (channel - 32));
    }
}

/**
 * Disable an ETM channel
 */
static inline void etm_channel_disable(uint8_t channel) {
    if (channel < 32) {
        ETM_REG(ETM_CH_ENA_AD0_CLR) = (1 << channel);
    } else {
        ETM_REG(ETM_CH_ENA_AD1_CLR) = (1 << (channel - 32));
    }
}

/**
 * Configure an ETM channel: event → task
 */
static inline void etm_channel_config(uint8_t channel, uint8_t event_id, uint8_t task_id) {
    ETM_REG(ETM_CH_EVT_ID(channel)) = event_id;
    ETM_REG(ETM_CH_TASK_ID(channel)) = task_id;
}

/**
 * Configure and enable an ETM channel in one call
 */
static inline void etm_connect(uint8_t channel, uint8_t event_id, uint8_t task_id) {
    etm_channel_config(channel, event_id, task_id);
    etm_channel_enable(channel);
}

/**
 * Check if channel is enabled
 */
static inline int etm_channel_is_enabled(uint8_t channel) {
    if (channel < 32) {
        return (ETM_REG(ETM_CH_ENA_AD0) >> channel) & 1;
    } else {
        return (ETM_REG(ETM_CH_ENA_AD1) >> (channel - 32)) & 1;
    }
}

/**
 * Disable all ETM channels
 */
static inline void etm_disable_all(void) {
    ETM_REG(ETM_CH_ENA_AD0_CLR) = 0xFFFFFFFF;
    ETM_REG(ETM_CH_ENA_AD1_CLR) = 0x0003FFFF;  // 18 bits for ch 32-49
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_ETM_H
