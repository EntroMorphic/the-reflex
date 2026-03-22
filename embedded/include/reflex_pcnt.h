/**
 * reflex_pcnt.h - Bare Metal PCNT (Pulse Counter) for ESP32-C6
 *
 * ZERO DEPENDENCIES. Direct register access only.
 *
 * PCNT counts pulses on GPIO pins. This is HARDWARE ADDITION:
 *   - Each pulse increments (or decrements) a counter
 *   - 4 independent units, 2 channels each
 *   - 16-bit signed counter per unit
 *   - Threshold events for ETM triggering
 *
 * This is half of the Turing Fabric's compute engine.
 * (The other half is RMT pulse generation)
 */

#ifndef REFLEX_PCNT_H
#define REFLEX_PCNT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// PCNT Register Addresses (from ESP32-C6 TRM)
// ============================================================

#define PCNT_BASE                   0x60017000

// Per-unit registers (unit 0-3)
// Unit N base = PCNT_BASE + N * 0x0C (for conf0/conf1/conf2)
// But the actual layout is more complex...

// Unit 0
#define PCNT_U0_CONF0               (PCNT_BASE + 0x0000)
#define PCNT_U0_CONF1               (PCNT_BASE + 0x0004)
#define PCNT_U0_CONF2               (PCNT_BASE + 0x0008)

// Unit 1
#define PCNT_U1_CONF0               (PCNT_BASE + 0x000C)
#define PCNT_U1_CONF1               (PCNT_BASE + 0x0010)
#define PCNT_U1_CONF2               (PCNT_BASE + 0x0014)

// Unit 2
#define PCNT_U2_CONF0               (PCNT_BASE + 0x0018)
#define PCNT_U2_CONF1               (PCNT_BASE + 0x001C)
#define PCNT_U2_CONF2               (PCNT_BASE + 0x0020)

// Unit 3
#define PCNT_U3_CONF0               (PCNT_BASE + 0x0024)
#define PCNT_U3_CONF1               (PCNT_BASE + 0x0028)
#define PCNT_U3_CONF2               (PCNT_BASE + 0x002C)

// Count value registers (read-only)
#define PCNT_U0_CNT                 (PCNT_BASE + 0x0030)
#define PCNT_U1_CNT                 (PCNT_BASE + 0x0034)
#define PCNT_U2_CNT                 (PCNT_BASE + 0x0038)
#define PCNT_U3_CNT                 (PCNT_BASE + 0x003C)

// Interrupt registers
#define PCNT_INT_RAW                (PCNT_BASE + 0x0040)
#define PCNT_INT_ST                 (PCNT_BASE + 0x0044)
#define PCNT_INT_ENA                (PCNT_BASE + 0x0048)
#define PCNT_INT_CLR                (PCNT_BASE + 0x004C)

// Status registers
#define PCNT_U0_STATUS              (PCNT_BASE + 0x0050)
#define PCNT_U1_STATUS              (PCNT_BASE + 0x0054)
#define PCNT_U2_STATUS              (PCNT_BASE + 0x0058)
#define PCNT_U3_STATUS              (PCNT_BASE + 0x005C)

// Control register
#define PCNT_CTRL                   (PCNT_BASE + 0x0060)

// ============================================================
// CONF0 Register Bits
// ============================================================

// Filter threshold (bits 9:0)
#define PCNT_FILTER_THRES_SHIFT     0
#define PCNT_FILTER_THRES_MASK      0x3FF

// Filter enable (bit 10)
#define PCNT_FILTER_EN              (1 << 10)

// Threshold 0 enable (bit 11) - fires event when count == thresh0
#define PCNT_THR_THRES0_EN          (1 << 11)

// Threshold 1 enable (bit 12) - fires event when count == thresh1
#define PCNT_THR_THRES1_EN          (1 << 12)

// H_LIM enable (bit 13) - fires event when count == high limit
#define PCNT_THR_H_LIM_EN           (1 << 13)

// L_LIM enable (bit 14) - fires event when count == low limit
#define PCNT_THR_L_LIM_EN           (1 << 14)

// Zero threshold enable (bit 15) - fires event when count == 0
#define PCNT_THR_ZERO_EN            (1 << 15)

// ============================================================
// CONF1 Register Bits (Threshold values)
// ============================================================

// Threshold 0 (bits 15:0)
#define PCNT_CNT_THRES0_SHIFT       0
#define PCNT_CNT_THRES0_MASK        0xFFFF

// Threshold 1 (bits 31:16)
#define PCNT_CNT_THRES1_SHIFT       16
#define PCNT_CNT_THRES1_MASK        0xFFFF

// ============================================================
// CONF2 Register Bits (Limits)
// ============================================================

// High limit (bits 15:0) - typically 32767
#define PCNT_CNT_H_LIM_SHIFT        0
#define PCNT_CNT_H_LIM_MASK         0xFFFF

// Low limit (bits 31:16) - typically -32768
#define PCNT_CNT_L_LIM_SHIFT        16
#define PCNT_CNT_L_LIM_MASK         0xFFFF

// ============================================================
// CTRL Register Bits
// ============================================================

// Pause counting (per unit)
#define PCNT_CNT_PAUSE_U(n)         (1 << ((n) * 2))

// Reset counter (per unit) - write 1 to reset, auto-clears
#define PCNT_PULSE_CNT_RST_U(n)     (1 << ((n) * 2 + 1))

// ============================================================
// Channel Configuration (within CONF0)
// 
// Each unit has 2 channels (CH0 and CH1).
// Channel config is encoded in CONF0 bits 16-31.
// ============================================================

// Channel 0 edge action on positive edge (bits 17:16)
// 0 = no change, 1 = increment, 2 = decrement
#define PCNT_CH0_POS_MODE_SHIFT     16
#define PCNT_CH0_POS_MODE_MASK      0x3

// Channel 0 edge action on negative edge (bits 19:18)
#define PCNT_CH0_NEG_MODE_SHIFT     18
#define PCNT_CH0_NEG_MODE_MASK      0x3

// Channel 0 level action when control high (bits 21:20)
#define PCNT_CH0_HCTRL_MODE_SHIFT   20
#define PCNT_CH0_HCTRL_MODE_MASK    0x3

// Channel 0 level action when control low (bits 23:22)
#define PCNT_CH0_LCTRL_MODE_SHIFT   22
#define PCNT_CH0_LCTRL_MODE_MASK    0x3

// Channel 1 similar, offset by 8 bits (bits 24-31)
#define PCNT_CH1_POS_MODE_SHIFT     24
#define PCNT_CH1_NEG_MODE_SHIFT     26
#define PCNT_CH1_HCTRL_MODE_SHIFT   28
#define PCNT_CH1_LCTRL_MODE_SHIFT   30

// Mode values
#define PCNT_MODE_KEEP      0   // Keep current count
#define PCNT_MODE_INC       1   // Increment
#define PCNT_MODE_DEC       2   // Decrement

// ============================================================
// GPIO Matrix for PCNT
// ============================================================

// PCNT signal input is routed via GPIO matrix
// GPIO_FUNC_IN_SEL_CFG registers at 0x60091154 + signal*4
#define GPIO_FUNC_IN_SEL_BASE       0x60091154

// PCNT signal numbers (for GPIO matrix input selection)
#define PCNT_SIG_CH0_IN0            39      // Unit 0, Channel 0 signal input
#define PCNT_SIG_CH0_IN1            40      // Unit 0, Channel 1 signal input
#define PCNT_SIG_CH1_IN0            41      // Unit 1, Channel 0 signal input
#define PCNT_SIG_CH1_IN1            42      // Unit 1, Channel 1 signal input
#define PCNT_SIG_CH2_IN0            43      // Unit 2, Channel 0 signal input
#define PCNT_SIG_CH2_IN1            44      // Unit 2, Channel 1 signal input
#define PCNT_SIG_CH3_IN0            45      // Unit 3, Channel 0 signal input
#define PCNT_SIG_CH3_IN1            46      // Unit 3, Channel 1 signal input

// Control signal numbers
#define PCNT_CTRL_CH0_IN0           47      // Unit 0, Channel 0 control input
// ... etc

// ============================================================
// Direct Register Access
// ============================================================

#define PCNT_REG(addr)              (*(volatile uint32_t*)(addr))

// ============================================================
// PCNT Unit Addresses Helper
// ============================================================

static inline uint32_t pcnt_conf0_addr(uint8_t unit) {
    return PCNT_BASE + unit * 0x0C;
}

static inline uint32_t pcnt_conf1_addr(uint8_t unit) {
    return PCNT_BASE + unit * 0x0C + 0x04;
}

static inline uint32_t pcnt_conf2_addr(uint8_t unit) {
    return PCNT_BASE + unit * 0x0C + 0x08;
}

static inline uint32_t pcnt_cnt_addr(uint8_t unit) {
    return PCNT_U0_CNT + unit * 0x04;
}

static inline uint32_t pcnt_status_addr(uint8_t unit) {
    return PCNT_U0_STATUS + unit * 0x04;
}

// ============================================================
// PCNT API
// ============================================================

/**
 * Reset a PCNT unit's counter to 0
 */
static inline void pcnt_reset(uint8_t unit) {
    PCNT_REG(PCNT_CTRL) |= PCNT_PULSE_CNT_RST_U(unit);
    // Auto-clears, but small delay may be needed
    __asm__ volatile("nop; nop; nop; nop");
}

/**
 * Pause counting on a unit
 */
static inline void pcnt_pause(uint8_t unit) {
    PCNT_REG(PCNT_CTRL) |= PCNT_CNT_PAUSE_U(unit);
}

/**
 * Resume counting on a unit
 */
static inline void pcnt_resume(uint8_t unit) {
    PCNT_REG(PCNT_CTRL) &= ~PCNT_CNT_PAUSE_U(unit);
}

/**
 * Read the current count value
 */
static inline int16_t pcnt_read(uint8_t unit) {
    return (int16_t)(PCNT_REG(pcnt_cnt_addr(unit)) & 0xFFFF);
}

/**
 * Clear and read (atomic get and reset)
 */
static inline int16_t pcnt_read_and_clear(uint8_t unit) {
    int16_t val = pcnt_read(unit);
    pcnt_reset(unit);
    return val;
}

/**
 * Set threshold values
 */
static inline void pcnt_set_thresholds(uint8_t unit, int16_t thresh0, int16_t thresh1) {
    PCNT_REG(pcnt_conf1_addr(unit)) = 
        ((uint16_t)thresh0 << PCNT_CNT_THRES0_SHIFT) |
        ((uint16_t)thresh1 << PCNT_CNT_THRES1_SHIFT);
}

/**
 * Enable threshold events (for ETM)
 */
static inline void pcnt_enable_thresholds(uint8_t unit, int enable_thresh0, int enable_thresh1) {
    uint32_t conf0 = PCNT_REG(pcnt_conf0_addr(unit));
    if (enable_thresh0) conf0 |= PCNT_THR_THRES0_EN;
    else conf0 &= ~PCNT_THR_THRES0_EN;
    if (enable_thresh1) conf0 |= PCNT_THR_THRES1_EN;
    else conf0 &= ~PCNT_THR_THRES1_EN;
    PCNT_REG(pcnt_conf0_addr(unit)) = conf0;
}

/**
 * Set limits (high and low)
 */
static inline void pcnt_set_limits(uint8_t unit, int16_t low, int16_t high) {
    PCNT_REG(pcnt_conf2_addr(unit)) = 
        ((uint16_t)high << PCNT_CNT_H_LIM_SHIFT) |
        ((uint16_t)low << PCNT_CNT_L_LIM_SHIFT);
}

/**
 * Configure channel 0 to count rising edges on a GPIO
 * 
 * @param unit   PCNT unit (0-3)
 * @param gpio   GPIO pin number to count
 */
static inline void pcnt_config_count_rising(uint8_t unit, uint8_t gpio) {
    // Route GPIO to PCNT signal input via GPIO matrix
    uint32_t sig = PCNT_SIG_CH0_IN0 + unit * 2;
    volatile uint32_t* func_in_sel = (volatile uint32_t*)(GPIO_FUNC_IN_SEL_BASE + sig * 4);
    *func_in_sel = gpio | (1 << 7);  // bit 7 = signal enable
    
    // Configure CONF0: count up on positive edge, no change on negative
    uint32_t conf0 = PCNT_REG(pcnt_conf0_addr(unit));
    conf0 &= 0x0000FFFF;  // Clear channel config bits
    conf0 |= (PCNT_MODE_INC << PCNT_CH0_POS_MODE_SHIFT);  // Inc on rising
    conf0 |= (PCNT_MODE_KEEP << PCNT_CH0_NEG_MODE_SHIFT); // Keep on falling
    PCNT_REG(pcnt_conf0_addr(unit)) = conf0;
    
    // Set default limits
    pcnt_set_limits(unit, -32768, 32767);
    
    // Reset counter
    pcnt_reset(unit);
}

/**
 * Full PCNT unit initialization for pulse counting
 */
static inline void pcnt_init_counter(uint8_t unit, uint8_t gpio, int16_t thresh0, int16_t thresh1) {
    // Configure to count rising edges
    pcnt_config_count_rising(unit, gpio);
    
    // Set thresholds
    pcnt_set_thresholds(unit, thresh0, thresh1);
    
    // Enable threshold events
    pcnt_enable_thresholds(unit, 1, 1);
    
    // Reset and start
    pcnt_reset(unit);
    pcnt_resume(unit);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_PCNT_H
