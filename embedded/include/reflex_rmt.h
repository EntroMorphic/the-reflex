/**
 * reflex_rmt.h - Bare Metal RMT (Remote Control) for ESP32-C6
 *
 * ZERO DEPENDENCIES. Direct register access only.
 *
 * RMT generates precise pulse patterns from memory:
 *   - 4 channels (2 TX, 2 RX)
 *   - 48 words of memory per channel
 *   - Configurable carrier and pulse timing
 *   - DMA support for long patterns
 *
 * Combined with PCNT, this is the Turing Fabric:
 *   - RMT converts VALUES to PULSES
 *   - PCNT counts PULSES back to VALUES
 *   - Addition happens in hardware!
 */

#ifndef REFLEX_RMT_H
#define REFLEX_RMT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// RMT Register Addresses (from ESP32-C6 TRM)
// ============================================================

#define RMT_BASE                    0x60006000

// TX Channel configuration
#define RMT_CH0_TX_CONF0            (RMT_BASE + 0x0000)
#define RMT_CH1_TX_CONF0            (RMT_BASE + 0x0004)

// RX Channel configuration
#define RMT_CH2_RX_CONF0            (RMT_BASE + 0x0008)
#define RMT_CH2_RX_CONF1            (RMT_BASE + 0x000C)
#define RMT_CH3_RX_CONF0            (RMT_BASE + 0x0010)
#define RMT_CH3_RX_CONF1            (RMT_BASE + 0x0014)

// TX Status
#define RMT_CH0_TX_STATUS           (RMT_BASE + 0x0018)
#define RMT_CH1_TX_STATUS           (RMT_BASE + 0x001C)

// RX Status
#define RMT_CH2_RX_STATUS           (RMT_BASE + 0x0020)
#define RMT_CH3_RX_STATUS           (RMT_BASE + 0x0024)

// Interrupt registers
#define RMT_INT_RAW                 (RMT_BASE + 0x0028)
#define RMT_INT_ST                  (RMT_BASE + 0x002C)
#define RMT_INT_ENA                 (RMT_BASE + 0x0030)
#define RMT_INT_CLR                 (RMT_BASE + 0x0034)

// Carrier configuration
#define RMT_CH0_TX_CARRIER          (RMT_BASE + 0x0038)
#define RMT_CH1_TX_CARRIER          (RMT_BASE + 0x003C)
#define RMT_CH2_RX_CARRIER          (RMT_BASE + 0x0040)
#define RMT_CH3_RX_CARRIER          (RMT_BASE + 0x0044)

// TX Limit
#define RMT_CH0_TX_LIM              (RMT_BASE + 0x0048)
#define RMT_CH1_TX_LIM              (RMT_BASE + 0x004C)

// APB configuration
#define RMT_APB_CONF                (RMT_BASE + 0x0050)

// TX Sync
#define RMT_TX_SIM                  (RMT_BASE + 0x0054)

// Reference clock
#define RMT_REF_CNT_RST             (RMT_BASE + 0x0058)

// Date register (version)
#define RMT_DATE                    (RMT_BASE + 0x00FC)

// Channel memory base
#define RMT_CH0_MEM_BASE            (RMT_BASE + 0x0100)
#define RMT_CH1_MEM_BASE            (RMT_BASE + 0x0100 + 48*4)
#define RMT_CH2_MEM_BASE            (RMT_BASE + 0x0100 + 96*4)
#define RMT_CH3_MEM_BASE            (RMT_BASE + 0x0100 + 144*4)

// Each channel has 48 32-bit words = 192 bytes
#define RMT_MEM_WORDS_PER_CHANNEL   48
#define RMT_MEM_SIZE_PER_CHANNEL    (48 * 4)

// ============================================================
// TX_CONF0 Register Bits
// ============================================================

#define RMT_TX_START                (1 << 0)   // Start TX
#define RMT_MEM_RD_RST              (1 << 1)   // Reset memory read pointer
#define RMT_APB_MEM_RST             (1 << 2)   // Reset APB memory
#define RMT_TX_CONTI_MODE           (1 << 3)   // Continuous mode
#define RMT_MEM_TX_WRAP_EN          (1 << 4)   // Memory wrap enable
#define RMT_IDLE_OUT_LV             (1 << 5)   // Idle level (0 or 1)
#define RMT_IDLE_OUT_EN             (1 << 6)   // Enable idle output
#define RMT_TX_STOP                 (1 << 7)   // Stop TX
#define RMT_DIV_CNT_SHIFT           8          // Clock divider (bits 15:8)
#define RMT_DIV_CNT_MASK            0xFF
#define RMT_MEM_SIZE_SHIFT          16         // Memory blocks (bits 19:16)
#define RMT_MEM_SIZE_MASK           0xF
#define RMT_CARRIER_EFF_OUT_EN      (1 << 20)  // Carrier on output
#define RMT_CARRIER_EN              (1 << 21)  // Enable carrier
#define RMT_CARRIER_OUT_LV          (1 << 22)  // Carrier level
#define RMT_TX_CONF_UPDATE          (1 << 24)  // Update configuration
#define RMT_MEM_CLK_FORCE_ON        (1 << 25)  // Force memory clock on
#define RMT_MEM_FORCE_PD            (1 << 26)  // Force power down
#define RMT_MEM_FORCE_PU            (1 << 27)  // Force power up

// ============================================================
// Interrupt bits
// ============================================================

#define RMT_CH0_TX_END_INT          (1 << 0)
#define RMT_CH1_TX_END_INT          (1 << 1)
#define RMT_CH2_RX_END_INT          (1 << 2)
#define RMT_CH3_RX_END_INT          (1 << 3)
#define RMT_CH0_TX_ERR_INT          (1 << 4)
#define RMT_CH1_TX_ERR_INT          (1 << 5)
#define RMT_CH2_RX_ERR_INT          (1 << 6)
#define RMT_CH3_RX_ERR_INT          (1 << 7)
#define RMT_CH0_TX_THR_INT          (1 << 8)
#define RMT_CH1_TX_THR_INT          (1 << 9)
#define RMT_CH2_RX_THR_INT          (1 << 10)
#define RMT_CH3_RX_THR_INT          (1 << 11)
#define RMT_CH0_TX_LOOP_INT         (1 << 12)
#define RMT_CH1_TX_LOOP_INT         (1 << 13)

// ============================================================
// RMT Symbol Format
// ============================================================

/**
 * Each RMT symbol is 32 bits:
 *   [14:0]  = duration0 (ticks for level0)
 *   [15]    = level0 (0 or 1)
 *   [30:16] = duration1 (ticks for level1)
 *   [31]    = level1 (0 or 1)
 *
 * A symbol with duration0=0 marks end of transmission.
 */
typedef union {
    struct {
        uint32_t duration0 : 15;
        uint32_t level0    : 1;
        uint32_t duration1 : 15;
        uint32_t level1    : 1;
    };
    uint32_t val;
} rmt_symbol_t;

// ============================================================
// Direct Register Access
// ============================================================

#define RMT_REG(addr)               (*(volatile uint32_t*)(addr))

// ============================================================
// GPIO Matrix for RMT
// ============================================================

// RMT output signals in GPIO matrix
#define RMT_SIG_OUT0                87      // RMT CH0 TX output signal
#define RMT_SIG_OUT1                88      // RMT CH1 TX output signal

// GPIO_FUNC_OUT_SEL registers
#define GPIO_FUNC_OUT_SEL_BASE      0x60091554

// ============================================================
// RMT API
// ============================================================

/**
 * Get channel memory base address
 */
static inline volatile uint32_t* rmt_get_mem(uint8_t channel) {
    return (volatile uint32_t*)(RMT_CH0_MEM_BASE + channel * RMT_MEM_SIZE_PER_CHANNEL);
}

/**
 * Get TX config register address
 */
static inline uint32_t rmt_tx_conf_addr(uint8_t channel) {
    return RMT_CH0_TX_CONF0 + channel * 4;
}

/**
 * Configure RMT TX channel output to GPIO
 */
static inline void rmt_set_gpio(uint8_t channel, uint8_t gpio) {
    // Route RMT output to GPIO via GPIO matrix
    uint32_t sig = RMT_SIG_OUT0 + channel;
    volatile uint32_t* func_out_sel = (volatile uint32_t*)(GPIO_FUNC_OUT_SEL_BASE + gpio * 4);
    *func_out_sel = sig;
    
    // Enable GPIO output
    volatile uint32_t* gpio_enable_w1ts = (volatile uint32_t*)0x60091024;
    *gpio_enable_w1ts = (1 << gpio);
}

/**
 * Initialize RMT TX channel
 *
 * @param channel   Channel number (0 or 1 for TX)
 * @param gpio      GPIO pin for output
 * @param div       Clock divider (1-255, clock = 80MHz / div)
 */
static inline void rmt_init_tx(uint8_t channel, uint8_t gpio, uint8_t div) {
    // Configure GPIO output
    rmt_set_gpio(channel, gpio);
    
    // Configure channel
    uint32_t conf = 0;
    conf |= (div << RMT_DIV_CNT_SHIFT);     // Clock divider
    conf |= (1 << RMT_MEM_SIZE_SHIFT);       // 1 memory block (48 words)
    conf |= RMT_IDLE_OUT_EN;                 // Enable idle output
    conf |= RMT_IDLE_OUT_LV;                 // Idle level = 0 (clear bit for low)
    conf &= ~RMT_IDLE_OUT_LV;                // Idle low
    conf |= RMT_MEM_CLK_FORCE_ON;            // Keep memory clock on
    
    RMT_REG(rmt_tx_conf_addr(channel)) = conf;
    
    // Reset memory pointer
    RMT_REG(rmt_tx_conf_addr(channel)) |= RMT_MEM_RD_RST;
    RMT_REG(rmt_tx_conf_addr(channel)) &= ~RMT_MEM_RD_RST;
}

/**
 * Write symbols to RMT channel memory
 *
 * @param channel   Channel number
 * @param symbols   Array of RMT symbols
 * @param count     Number of symbols (max 48 per block)
 */
static inline void rmt_write_symbols(uint8_t channel, const rmt_symbol_t* symbols, int count) {
    volatile uint32_t* mem = rmt_get_mem(channel);
    for (int i = 0; i < count && i < RMT_MEM_WORDS_PER_CHANNEL; i++) {
        mem[i] = symbols[i].val;
    }
}

/**
 * Write a simple pulse pattern: N pulses of specified duration
 *
 * @param channel     Channel number
 * @param num_pulses  Number of pulses to generate
 * @param high_ticks  Duration of high level in ticks
 * @param low_ticks   Duration of low level in ticks
 */
static inline void rmt_write_pulses(uint8_t channel, int num_pulses, uint16_t high_ticks, uint16_t low_ticks) {
    volatile uint32_t* mem = rmt_get_mem(channel);
    int i;
    for (i = 0; i < num_pulses && i < RMT_MEM_WORDS_PER_CHANNEL - 1; i++) {
        rmt_symbol_t sym = {
            .duration0 = high_ticks,
            .level0 = 1,
            .duration1 = low_ticks,
            .level1 = 0
        };
        mem[i] = sym.val;
    }
    // End marker
    mem[i] = 0;
}

/**
 * Start RMT transmission
 */
static inline void rmt_start_tx(uint8_t channel) {
    // Reset memory read pointer
    RMT_REG(rmt_tx_conf_addr(channel)) |= RMT_MEM_RD_RST;
    RMT_REG(rmt_tx_conf_addr(channel)) &= ~RMT_MEM_RD_RST;
    
    // Update config and start
    RMT_REG(rmt_tx_conf_addr(channel)) |= RMT_TX_CONF_UPDATE;
    RMT_REG(rmt_tx_conf_addr(channel)) |= RMT_TX_START;
}

/**
 * Stop RMT transmission
 */
static inline void rmt_stop_tx(uint8_t channel) {
    RMT_REG(rmt_tx_conf_addr(channel)) |= RMT_TX_STOP;
    RMT_REG(rmt_tx_conf_addr(channel)) &= ~RMT_TX_STOP;
}

/**
 * Check if TX is done
 */
static inline int rmt_tx_done(uint8_t channel) {
    return (RMT_REG(RMT_INT_RAW) >> channel) & 1;
}

/**
 * Clear TX done interrupt
 */
static inline void rmt_clear_tx_done(uint8_t channel) {
    RMT_REG(RMT_INT_CLR) = (1 << channel);
}

/**
 * Wait for TX to complete (blocking)
 */
static inline void rmt_wait_tx_done(uint8_t channel) {
    while (!rmt_tx_done(channel)) {
        __asm__ volatile("nop");
    }
    rmt_clear_tx_done(channel);
}

/**
 * Send N pulses and wait for completion
 */
static inline void rmt_send_pulses(uint8_t channel, int num_pulses, uint16_t high_ticks, uint16_t low_ticks) {
    rmt_write_pulses(channel, num_pulses, high_ticks, low_ticks);
    rmt_start_tx(channel);
    rmt_wait_tx_done(channel);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_RMT_H
