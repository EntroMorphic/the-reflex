/**
 * reflex_gdma.h - Corrected GDMA Register Definitions for ESP32-C6
 * 
 * Based on ESP-IDF v5.4 soc/gdma_reg.h and soc/gdma_struct.h
 * 
 * GDMA Base: 0x60080000
 * Channel 0: IN at +0x70, OUT at +0xd0
 * Channel 1: IN at +0xd0, OUT at +0x130 (stride 0x60)
 * Channel 2: IN at +0x130, OUT at +0x1f0
 * 
 * CRITICAL: Must enable clock in MISC_CONF (0x60080064) bit 3
 */

#ifndef REFLEX_GDMA_H
#define REFLEX_GDMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// GDMA Base and PCR Clock Control (ESP32-C6)
// ============================================================

#define GDMA_BASE                   0x60080000

// PCR (Peripheral Clock Register) for ESP32-C6
// GDMA clock is controlled via PCR, not GDMA internal registers
#define PCR_BASE                    0x60096000
#define PCR_GDMA_CONF_REG           (PCR_BASE + 0xbc)
#define PCR_GDMA_CLK_EN             (1 << 0)    // GDMA clock enable bit (BIT 0 per ESP-IDF)
#define PCR_GDMA_RST_EN             (1 << 1)    // GDMA reset enable bit
#define PCR_CLOCK_GATE_REG          (PCR_BASE + 0xff8)
#define PCR_CLK_EN                  (1 << 0)    // Force clock gating off

// ============================================================
// Channel Register Offsets (from ESP-IDF gdma_reg.h)
// ============================================================
// Per ESP-IDF:
//   Channel 0 IN:  base=0x70,  CONF0=0x70, LINK=0x80, STATE=0x84
//   Channel 0 OUT: base=0xd0,  CONF0=0xd0, LINK=0xe0, STATE=0xe4
//   Channel 1 IN:  base=0x130, CONF0=0x130, LINK=0x140, STATE=0x144
//   Channel 1 OUT: base=0x190, CONF0=0x190, LINK=0x1a0, STATE=0x1a4
//   Channel 2 IN:  base=0x1f0, CONF0=0x1f0, LINK=0x200, STATE=0x204
//   Channel 2 OUT: base=0x250, CONF0=0x250, LINK=0x260, STATE=0x264
// Stride is 0x60 between channel pairs (0x70->0xd0, 0xd0->0x130)

#define GDMA_CH0_IN_BASE            (GDMA_BASE + 0x70)
#define GDMA_CH0_OUT_BASE           (GDMA_BASE + 0xd0)
#define GDMA_CH1_IN_BASE            (GDMA_BASE + 0x130)
#define GDMA_CH1_OUT_BASE           (GDMA_BASE + 0x190)
#define GDMA_CH2_IN_BASE            (GDMA_BASE + 0x1f0)
#define GDMA_CH2_OUT_BASE           (GDMA_BASE + 0x250)

// ============================================================
// IN Channel Registers (RX)
// ============================================================

#define GDMA_IN_CONF0_CH(n)         (GDMA_CH0_IN_BASE + (n) * 0x60)
#define GDMA_IN_CONF1_CH(n)         (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x04)
#define GDMA_IN_FIFO_STATUS_CH(n)   (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x08)
#define GDMA_IN_POP_CH(n)           (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x0c)
#define GDMA_IN_LINK_CH(n)          (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x10)
#define GDMA_IN_STATE_CH(n)         (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x14)
#define GDMA_IN_SUC_EOF_DES_ADDR_CH(n) (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x18)
#define GDMA_IN_ERR_EOF_DES_ADDR_CH(n) (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x1c)
#define GDMA_IN_DSCR_CH(n)          (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x20)
#define GDMA_IN_DSCR_BF0_CH(n)      (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x24)
#define GDMA_IN_DSCR_BF1_CH(n)      (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x28)
#define GDMA_IN_PRI_CH(n)           (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x2c)
#define GDMA_IN_PERI_SEL_CH(n)      (GDMA_CH0_IN_BASE + (n) * 0x60 + 0x30)

// ============================================================
// OUT Channel Registers (TX)
// ============================================================

#define GDMA_OUT_CONF0_CH(n)        (GDMA_CH0_OUT_BASE + (n) * 0x60)
#define GDMA_OUT_CONF1_CH(n)        (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x04)
#define GDMA_OUT_FIFO_STATUS_CH(n)  (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x08)
#define GDMA_OUT_PUSH_CH(n)         (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x0c)
#define GDMA_OUT_LINK_CH(n)         (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x10)
#define GDMA_OUT_STATE_CH(n)        (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x14)
#define GDMA_OUT_EOF_DES_ADDR_CH(n) (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x18)
#define GDMA_OUT_EOF_BFR_DES_ADDR_CH(n) (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x1c)
#define GDMA_OUT_DSCR_CH(n)         (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x20)
#define GDMA_OUT_DSCR_BF0_CH(n)     (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x24)
#define GDMA_OUT_DSCR_BF1_CH(n)     (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x28)
#define GDMA_OUT_PRI_CH(n)          (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x2c)
#define GDMA_OUT_PERI_SEL_CH(n)     (GDMA_CH0_OUT_BASE + (n) * 0x60 + 0x30)

// Interrupt register offsets (from gdma_reg.h - these are at fixed offsets from GDMA_BASE)
// IN channel 0: INT_RAW=0x0, INT_ST=0x4, INT_ENA=0x8, INT_CLR=0xc
// OUT channel 0: INT_RAW=0x30, INT_ST=0x34, INT_ENA=0x38, INT_CLR=0x3c
#define GDMA_IN_INT_RAW_CH(n)       (GDMA_BASE + (n) * 0x10)
#define GDMA_IN_INT_ST_CH(n)        (GDMA_BASE + 0x4 + (n) * 0x10)
#define GDMA_IN_INT_CLR_CH(n)       (GDMA_BASE + 0xc + (n) * 0x10)
#define GDMA_OUT_INT_RAW_CH(n)      (GDMA_BASE + 0x30 + (n) * 0x10)
#define GDMA_OUT_INT_ST_CH(n)       (GDMA_BASE + 0x34 + (n) * 0x10)
#define GDMA_OUT_INT_CLR_CH(n)      (GDMA_BASE + 0x3c + (n) * 0x10)

// Interrupt status bits
#define GDMA_IN_INT_SUC_EOF         (1 << 1)
#define GDMA_IN_INT_ERR_EOF         (1 << 2)
#define GDMA_IN_INT_DONE            (1 << 0)
#define GDMA_IN_INT_DESC_EMPTY      (1 << 4)
#define GDMA_IN_INT_ERR             (GDMA_IN_INT_ERR_EOF)
#define GDMA_OUT_INT_EOF            (1 << 1)
#define GDMA_OUT_INT_DONE           (1 << 0)
#define GDMA_OUT_INT_ERR            (1 << 2)

// ============================================================
// Register Bit Definitions (from gdma_struct.h for ESP32-C6)
// ============================================================

// IN_CONF0 bits (RX channel) - per gdma_in_conf0_chn_reg_t
#define GDMA_IN_RST                 (1 << 0)   // in_rst
#define GDMA_IN_LOOP_TEST           (1 << 1)   // in_loop_test (reserved)
#define GDMA_INDSCR_BURST_EN        (1 << 2)   // indscr_burst_en - descriptor burst
#define GDMA_IN_DATA_BURST_EN       (1 << 3)   // in_data_burst_en - data burst
#define GDMA_IN_MEM_TRANS_EN        (1 << 4)   // mem_trans_en - M2M mode
#define GDMA_IN_ETM_EN              (1 << 5)   // in_etm_en

// IN_CONF1 bits - per gdma_in_conf1_chn_reg_t
#define GDMA_IN_CHECK_OWNER         (1 << 12)  // in_check_owner

// OUT_CONF0 bits (TX channel) - per gdma_out_conf0_chn_reg_t
#define GDMA_OUT_RST                (1 << 0)   // out_rst
#define GDMA_OUT_LOOP_TEST          (1 << 1)   // out_loop_test (reserved)
#define GDMA_OUT_AUTO_WRBACK        (1 << 2)   // out_auto_wrback
#define GDMA_OUT_EOF_MODE           (1 << 3)   // out_eof_mode
#define GDMA_OUTDSCR_BURST_EN       (1 << 4)   // outdscr_burst_en - descriptor burst
#define GDMA_OUT_DATA_BURST_EN      (1 << 5)   // out_data_burst_en - data burst
#define GDMA_OUT_ETM_EN             (1 << 6)   // out_etm_en

// OUT_CONF1 bits - per gdma_out_conf1_chn_reg_t
#define GDMA_OUT_CHECK_OWNER        (1 << 12)  // out_check_owner

// IN_LINK bits - per gdma_in_link_chn_reg_t
#define GDMA_INLINK_ADDR_MASK       0xFFFFF  // bits [19:0]
#define GDMA_INLINK_AUTO_RET        (1 << 20)  // bit 20: auto return on error
#define GDMA_INLINK_STOP            (1 << 21)  // bit 21: stop
#define GDMA_INLINK_START           (1 << 22)  // bit 22: start
#define GDMA_INLINK_RESTART         (1 << 23)  // bit 23: restart
#define GDMA_INLINK_PARK            (1 << 24)  // bit 24: park (idle state)

// OUT_LINK bits - per gdma_out_link_chn_reg_t
#define GDMA_OUTLINK_ADDR_MASK      0xFFFFF  // bits [19:0]
#define GDMA_OUTLINK_STOP           (1 << 20)  // bit 20: stop
#define GDMA_OUTLINK_START          (1 << 21)  // bit 21: start
#define GDMA_OUTLINK_RESTART        (1 << 22)  // bit 22: restart
#define GDMA_OUTLINK_PARK           (1 << 23)  // bit 23: park (idle state)

// Peripheral select values (from ESP-IDF gdma_channel.h)
#define GDMA_PERI_SEL_M2M           0xFFFFFFFF  // Memory-to-Memory (SOC_GDMA_TRIG_PERIPH_M2M0 = -1)

#include "soc/lldesc.h"

// ============================================================
// DMA Descriptor Structure - ESP-IDF lldesc_t Compatible
// ============================================================
// Note: GDMA uses lldesc_t format (see esp_rom_lldesc.h)
// Structure layout (bit positions from LSB):
//   [0:11]   size   - buffer size
//   [12:23]  length - transfer length  
//   [24:28]  offset - reserved offset
//   [29]     sosf   - start of sub-frame
//   [30]     eof    - end of frame
//   [31]     owner  - 0=SW, 1=HW

// For M2M transfer, we need:
//   - owner = 1 (HW owned)
//   - eof = 1 (end of transfer)
//   - length = buffer size
//   - size = buffer size
//   - buf = buffer address
//   - qe = next descriptor (NULL for single transfer)

// ============================================================
// Register Access Macro
// ============================================================

#define GDMA_REG(addr)              (*(volatile uint32_t*)(addr))

// ============================================================
// Clock Enable (CRITICAL - must call first!)
// ============================================================

static inline void gdma_enable_clock(void) {
    // ESP32-C6: Enable GDMA clock via PCR (Peripheral Clock Register)
    // Register: 0x600960bc, Bit 0: CLK_EN
    // Also need to clear clock gating and reset per ESP-IDF clk_gate_ll.h
    
    // Step 1: Clear clock gate to allow peripheral clock enable
    GDMA_REG(PCR_CLOCK_GATE_REG) &= ~PCR_CLK_EN;
    
    // Step 2: Clear reset bit to take GDMA out of reset
    GDMA_REG(PCR_GDMA_CONF_REG) &= ~PCR_GDMA_RST_EN;
    
    // Step 3: Enable GDMA clock (Bit 0 per ESP-IDF)
    GDMA_REG(PCR_GDMA_CONF_REG) |= PCR_GDMA_CLK_EN;
}

// ============================================================
// Channel Reset
// ============================================================

static inline void gdma_in_reset(uint8_t channel) {
    GDMA_REG(GDMA_IN_CONF0_CH(channel)) |= GDMA_IN_RST;
    GDMA_REG(GDMA_IN_CONF0_CH(channel)) &= ~GDMA_IN_RST;
    // Clear FIFO after reset
    volatile uint32_t tmp = GDMA_REG(GDMA_IN_FIFO_STATUS_CH(channel));
    (void)tmp;
    GDMA_REG(GDMA_IN_POP_CH(channel)) = 0;
}

static inline void gdma_out_reset(uint8_t channel) {
    GDMA_REG(GDMA_OUT_CONF0_CH(channel)) |= GDMA_OUT_RST;
    GDMA_REG(GDMA_OUT_CONF0_CH(channel)) &= ~GDMA_OUT_RST;
    // Clear FIFO after reset
    volatile uint32_t tmp = GDMA_REG(GDMA_OUT_FIFO_STATUS_CH(channel));
    (void)tmp;
    GDMA_REG(GDMA_OUT_PRI_CH(channel)) = 0;  // OUT PRI can clear FIFO
}

// lldesc_t is already included from soc/lldesc.h

// ============================================================
// M2M Transfer Functions using lldesc_t
// ============================================================

static inline void gdma_m2m_init(uint8_t ch) {
    // Enable clock first
    gdma_enable_clock();

    // Reset both IN and OUT sides
    GDMA_REG(GDMA_IN_CONF0_CH(ch)) |= GDMA_IN_RST;
    GDMA_REG(GDMA_IN_CONF0_CH(ch)) &= ~GDMA_IN_RST;
    GDMA_REG(GDMA_OUT_CONF0_CH(ch)) |= GDMA_OUT_RST;
    GDMA_REG(GDMA_OUT_CONF0_CH(ch)) &= ~GDMA_OUT_RST;

    // Configure OUT side (TX - source reader)
    // CONF0: auto_wrback(2), eof_mode(3), dscr_burst(4), data_burst(5)
    // Note: M2M doesn't need OUT_MEM_TRANS_EN - that's only for IN side!
    uint32_t out_conf0 = GDMA_OUT_AUTO_WRBACK | GDMA_OUT_EOF_MODE |
                         GDMA_OUTDSCR_BURST_EN | GDMA_OUT_DATA_BURST_EN;
    GDMA_REG(GDMA_OUT_CONF0_CH(ch)) = out_conf0;

    // CONF1: check_owner(12)
    GDMA_REG(GDMA_OUT_CONF1_CH(ch)) = GDMA_OUT_CHECK_OWNER;

    // Peripheral select: M2M mode
    GDMA_REG(GDMA_OUT_PERI_SEL_CH(ch)) = GDMA_PERI_SEL_M2M;
    GDMA_REG(GDMA_OUT_PRI_CH(ch)) = 0;

    // Configure IN side (RX - destination writer)
    // CONF0: dscr_burst(2), data_burst(3), mem_trans_en(4) - M2M mode!
    uint32_t in_conf0 = GDMA_INDSCR_BURST_EN | GDMA_IN_DATA_BURST_EN | GDMA_IN_MEM_TRANS_EN;
    GDMA_REG(GDMA_IN_CONF0_CH(ch)) = in_conf0;

    // CONF1: check_owner(12)
    GDMA_REG(GDMA_IN_CONF1_CH(ch)) = GDMA_IN_CHECK_OWNER;

    // Peripheral select: M2M mode
    GDMA_REG(GDMA_IN_PERI_SEL_CH(ch)) = GDMA_PERI_SEL_M2M;
    GDMA_REG(GDMA_IN_PRI_CH(ch)) = 0;
}

static inline void gdma_m2m_start(uint8_t ch, lldesc_t* out_desc, lldesc_t* in_desc) {
    // Set IN descriptor address (destination) first
    GDMA_REG(GDMA_IN_LINK_CH(ch)) = ((uint32_t)in_desc) & GDMA_INLINK_ADDR_MASK;

    // Set OUT descriptor address (source)
    GDMA_REG(GDMA_OUT_LINK_CH(ch)) = ((uint32_t)out_desc) & GDMA_OUTLINK_ADDR_MASK;

    // Clear any existing START bits first
    GDMA_REG(GDMA_IN_LINK_CH(ch)) &= ~GDMA_INLINK_START;
    GDMA_REG(GDMA_OUT_LINK_CH(ch)) &= ~GDMA_OUTLINK_START;

    // Start IN channel (destination writer) FIRST - matches ESP-IDF pattern
    GDMA_REG(GDMA_IN_LINK_CH(ch)) |= GDMA_INLINK_START;

    // Then start OUT channel (source reader)
    GDMA_REG(GDMA_OUT_LINK_CH(ch)) |= GDMA_OUTLINK_START;
}

static inline int gdma_m2m_done(uint8_t ch) {
    int out_idle = (GDMA_REG(GDMA_OUT_LINK_CH(ch)) & GDMA_OUTLINK_PARK) != 0;
    int in_idle = (GDMA_REG(GDMA_IN_LINK_CH(ch)) & GDMA_INLINK_PARK) != 0;
    return out_idle && in_idle;
}

static inline uint32_t gdma_in_get_intr_status(uint8_t ch) {
    return GDMA_REG(GDMA_IN_INT_ST_CH(ch));
}

static inline uint32_t gdma_out_get_intr_status(uint8_t ch) {
    return GDMA_REG(GDMA_OUT_INT_ST_CH(ch));
}

static inline void gdma_clear_all_intr(uint8_t ch) {
    GDMA_REG(GDMA_IN_INT_CLR_CH(ch)) = 0x7F;
    GDMA_REG(GDMA_OUT_INT_CLR_CH(ch)) = 0x3F;
}

// ============================================================
// RMT Memory Address
// ============================================================

#define RMT_CH0_RAM_ADDR            0x60006100

#ifdef __cplusplus
}
#endif

#endif // REFLEX_GDMA_H
