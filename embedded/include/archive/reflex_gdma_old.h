/**
 * reflex_gdma.h - Bare Metal GDMA (General DMA) for ESP32-C6
 *
 * ZERO DEPENDENCIES. Direct register access only.
 *
 * THE SILICON GRAIL:
 *   GDMA M2M mode can write to ANY memory address.
 *   Including peripheral registers like RMT memory!
 *
 * This enables:
 *   - ETM triggers GDMA
 *   - GDMA writes pulse pattern to RMT memory
 *   - ETM triggers RMT
 *   - Fully autonomous pattern loading!
 */

#ifndef REFLEX_GDMA_H
#define REFLEX_GDMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// GDMA Register Addresses (from ESP32-C6 TRM)
// ============================================================

#define GDMA_BASE                   0x60080000

// Interrupt registers (shared across channels)
#define GDMA_IN_INT_RAW_CH(n)       (GDMA_BASE + 0x0000 + (n) * 0x10)
#define GDMA_IN_INT_ST_CH(n)        (GDMA_BASE + 0x0004 + (n) * 0x10)
#define GDMA_IN_INT_ENA_CH(n)       (GDMA_BASE + 0x0008 + (n) * 0x10)
#define GDMA_IN_INT_CLR_CH(n)       (GDMA_BASE + 0x000C + (n) * 0x10)

// IN Interrupt bits
#define GDMA_IN_INT_EOF             (1 << 1)    // EOF interrupt
#define GDMA_IN_INT_DSCR_EMPTY      (1 << 2)    // Descriptor empty

#define GDMA_OUT_INT_RAW_CH(n)      (GDMA_BASE + 0x0030 + (n) * 0x10)
#define GDMA_OUT_INT_ST_CH(n)       (GDMA_BASE + 0x0034 + (n) * 0x10)
#define GDMA_OUT_INT_ENA_CH(n)      (GDMA_BASE + 0x0038 + (n) * 0x10)
#define GDMA_OUT_INT_CLR_CH(n)      (GDMA_BASE + 0x003C + (n) * 0x10)

// AHB test (shared)
#define GDMA_AHB_TEST               (GDMA_BASE + 0x0060)
#define GDMA_MISC_CONF              (GDMA_BASE + 0x0064)

// Per-channel configuration
// Channel stride is 0xC0 bytes
#define GDMA_CH_OFFSET(n)           (0x0070 + (n) * 0xC0)

// IN (RX) channel registers
#define GDMA_IN_CONF0_CH(n)         (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x00)
#define GDMA_IN_CONF1_CH(n)         (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x04)
#define GDMA_IN_POP_CH(n)           (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x08)
#define GDMA_IN_LINK_CH(n)          (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x0C)
#define GDMA_IN_STATE_CH(n)         (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x10)
#define GDMA_IN_EOF_DES_ADDR_CH(n)  (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x14)
#define GDMA_IN_DSCR_CH(n)          (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x1C)
#define GDMA_IN_DSCR_BF0_CH(n)      (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x20)
#define GDMA_IN_DSCR_BF1_CH(n)      (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x24)
#define GDMA_IN_PRI_CH(n)           (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x28)
#define GDMA_IN_PERI_SEL_CH(n)      (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x2C)

// OUT (TX) channel registers
#define GDMA_OUT_CONF0_CH(n)        (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x60)
#define GDMA_OUT_CONF1_CH(n)        (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x64)
#define GDMA_OUT_PUSH_CH(n)         (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x68)
#define GDMA_OUT_LINK_CH(n)         (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x6C)
#define GDMA_OUT_STATE_CH(n)        (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x74)
#define GDMA_OUT_EOF_DES_ADDR_CH(n) (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x78)
#define GDMA_OUT_DSCR_CH(n)         (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x84)
#define GDMA_OUT_DSCR_BF0_CH(n)     (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x88)
#define GDMA_OUT_DSCR_BF1_CH(n)     (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x8C)
#define GDMA_OUT_PRI_CH(n)          (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x90)
#define GDMA_OUT_PERI_SEL_CH(n)     (GDMA_BASE + GDMA_CH_OFFSET(n) + 0x94)

// ============================================================
// OUT_CONF0 Register Bits
// ============================================================

#define GDMA_OUT_RST                (1 << 0)    // Reset TX FSM and FIFO
#define GDMA_OUT_LOOP_TEST          (1 << 1)    // Reserved
#define GDMA_OUT_AUTO_WRBACK        (1 << 2)    // Auto writeback
#define GDMA_OUT_EOF_MODE           (1 << 3)    // EOF flag generation mode
#define GDMA_OUTDSCR_BURST_EN       (1 << 4)    // INCR burst for descriptor
#define GDMA_OUT_DATA_BURST_EN      (1 << 5)    // INCR burst for data
#define GDMA_OUT_ETM_EN             (1 << 6)    // ETM trigger mode!

// ============================================================
// OUT_LINK Register Bits
// ============================================================

#define GDMA_OUTLINK_ADDR_MASK      0xFFFFF     // 20-bit descriptor address
#define GDMA_OUTLINK_STOP           (1 << 20)   // Stop DMA
#define GDMA_OUTLINK_START          (1 << 21)   // Start DMA
#define GDMA_OUTLINK_RESTART        (1 << 22)   // Restart from current
#define GDMA_OUTLINK_PARK           (1 << 23)   // FSM is idle (read-only)

// ============================================================
// IN_LINK Register Bits (same bit positions as OUT_LINK)
// ============================================================

#define GDMA_INLINK_ADDR_MASK       0xFFFFF     // 20-bit descriptor address
#define GDMA_INLINK_STOP            (1 << 20)   // Stop DMA
#define GDMA_INLINK_START           (1 << 21)   // Start DMA
#define GDMA_INLINK_RESTART         (1 << 22)   // Restart from current
#define GDMA_INLINK_PARK            (1 << 23)   // FSM is idle (read-only)

// ============================================================
// Peripheral Select Values (for M2M, use -1 which maps to 63)
// ============================================================

#define GDMA_PERI_SEL_SPI2          0
#define GDMA_PERI_SEL_UHCI0         2
#define GDMA_PERI_SEL_I2S0          3
#define GDMA_PERI_SEL_AES           6
#define GDMA_PERI_SEL_SHA           7
#define GDMA_PERI_SEL_ADC           8
#define GDMA_PERI_SEL_PARLIO        9
#define GDMA_PERI_SEL_M2M           63  // Memory-to-Memory!

// ============================================================
// DMA Descriptor Structure
// ============================================================

/**
 * DMA Linked List Descriptor (12 bytes, 4-byte aligned)
 *
 * dw0 format:
 *   [11:0]  size    - Buffer size in bytes
 *   [23:12] length  - Valid bytes (for RX) or bytes to send (for TX)
 *   [27:24] reserved
 *   [28]    err_eof - Error EOF (RX only)
 *   [29]    reserved
 *   [30]    suc_eof - Success EOF (last descriptor)
 *   [31]    owner   - 0=CPU can access, 1=DMA can access
 */
typedef struct gdma_descriptor_s {
    volatile uint32_t dw0;
    volatile void* buffer;
    volatile struct gdma_descriptor_s* next;
} __attribute__((aligned(4))) gdma_descriptor_t;

// DW0 field macros
#define GDMA_DW0_SIZE(s)            ((s) & 0xFFF)
#define GDMA_DW0_LENGTH(l)          (((l) & 0xFFF) << 12)
#define GDMA_DW0_ERR_EOF            (1 << 28)
#define GDMA_DW0_SUC_EOF            (1 << 30)
#define GDMA_DW0_OWNER_DMA          (1 << 31)
#define GDMA_DW0_OWNER_CPU          (0 << 31)

// ============================================================
// Direct Register Access
// ============================================================

#define GDMA_REG(addr)              (*(volatile uint32_t*)(addr))

// ============================================================
// GDMA API
// ============================================================

/**
 * Reset a GDMA TX channel
 */
static inline void gdma_tx_reset(uint8_t channel) {
    GDMA_REG(GDMA_OUT_CONF0_CH(channel)) |= GDMA_OUT_RST;
    GDMA_REG(GDMA_OUT_CONF0_CH(channel)) &= ~GDMA_OUT_RST;
}

/**
 * Configure GDMA TX channel for M2M mode with ETM trigger
 *
 * @param channel  Channel number (0-2)
 * @param priority Priority (0-15, higher = more priority)
 */
static inline void gdma_tx_init_m2m_etm(uint8_t channel, uint8_t priority) {
    // Reset channel
    gdma_tx_reset(channel);
    
    // Configure for M2M with ETM trigger
    uint32_t conf0 = 0;
    conf0 |= GDMA_OUT_ETM_EN;           // Enable ETM trigger
    conf0 |= GDMA_OUT_EOF_MODE;         // EOF when data popped from FIFO
    conf0 |= GDMA_OUTDSCR_BURST_EN;     // Burst for descriptors
    conf0 |= GDMA_OUT_DATA_BURST_EN;    // Burst for data
    GDMA_REG(GDMA_OUT_CONF0_CH(channel)) = conf0;
    
    // Set M2M peripheral select
    GDMA_REG(GDMA_OUT_PERI_SEL_CH(channel)) = GDMA_PERI_SEL_M2M;
    
    // Set priority
    GDMA_REG(GDMA_OUT_PRI_CH(channel)) = priority & 0xF;
}

/**
 * Set descriptor address for TX channel
 *
 * @param channel     Channel number
 * @param descriptor  Pointer to first descriptor (must be 4-byte aligned)
 */
static inline void gdma_tx_set_descriptor(uint8_t channel, gdma_descriptor_t* descriptor) {
    // Write descriptor address (20 bits)
    uint32_t addr = ((uint32_t)descriptor) & GDMA_OUTLINK_ADDR_MASK;
    GDMA_REG(GDMA_OUT_LINK_CH(channel)) = addr;
}

/**
 * Start GDMA TX channel (software trigger)
 */
static inline void gdma_tx_start(uint8_t channel) {
    GDMA_REG(GDMA_OUT_LINK_CH(channel)) |= GDMA_OUTLINK_START;
}

/**
 * Stop GDMA TX channel
 */
static inline void gdma_tx_stop(uint8_t channel) {
    GDMA_REG(GDMA_OUT_LINK_CH(channel)) |= GDMA_OUTLINK_STOP;
}

/**
 * Check if GDMA TX channel is idle
 */
static inline int gdma_tx_is_idle(uint8_t channel) {
    return (GDMA_REG(GDMA_OUT_LINK_CH(channel)) & GDMA_OUTLINK_PARK) != 0;
}

/**
 * Build a simple M2M descriptor
 *
 * @param desc   Descriptor to fill
 * @param src    Source buffer
 * @param dst    Destination address (can be peripheral register!)
 * @param size   Size in bytes
 * @param eof    Is this the last descriptor?
 * @param next   Next descriptor (NULL if eof)
 */
static inline void gdma_build_descriptor(
    gdma_descriptor_t* desc,
    const void* src,
    void* dst,
    uint16_t size,
    int eof,
    gdma_descriptor_t* next
) {
    desc->dw0 = GDMA_DW0_SIZE(size) | 
                GDMA_DW0_LENGTH(size) | 
                GDMA_DW0_OWNER_DMA |
                (eof ? GDMA_DW0_SUC_EOF : 0);
    desc->buffer = (void*)src;
    // For M2M, we need separate mechanism for dst... 
    // Actually, ESP32-C6 GDMA M2M uses paired IN/OUT channels
    desc->next = next;
}

// ============================================================
// IN Channel Configuration (for M2M destination)
// ============================================================

// IN_CONF0 register bits
#define GDMA_IN_RST                 (1 << 0)
#define GDMA_IN_ETM_EN              (1 << 6)

/**
 * Reset a GDMA RX (IN) channel
 */
static inline void gdma_rx_reset(uint8_t channel) {
    GDMA_REG(GDMA_IN_CONF0_CH(channel)) |= GDMA_IN_RST;
    GDMA_REG(GDMA_IN_CONF0_CH(channel)) &= ~GDMA_IN_RST;
}

/**
 * Configure GDMA RX channel for M2M destination
 * 
 * @param channel   Channel number (0-2, must pair with TX channel)
 * @param dst_addr  Destination address (can be peripheral!)
 */
static inline void gdma_rx_init_m2m(uint8_t channel, void* dst_addr) {
    // Reset channel
    gdma_rx_reset(channel);
    
    // Configure for M2M
    uint32_t conf0 = 0;
    conf0 |= GDMA_IN_ETM_EN;  // Enable ETM trigger if needed
    GDMA_REG(GDMA_IN_CONF0_CH(channel)) = conf0;
    
    // Set M2M peripheral select
    GDMA_REG(GDMA_IN_PERI_SEL_CH(channel)) = GDMA_PERI_SEL_M2M;
    
    // For M2M, the destination is actually set via the descriptor buffer field
    // The IN channel reads from internal FIFO and writes to its buffer address
    // We use the buffer field as the destination address
}

// ============================================================
// Paired M2M Transfer
// ============================================================

/**
 * M2M transfer configuration
 * 
 * ESP32-C6 GDMA M2M mode requires PAIRED channels:
 *   - OUT channel reads from source (SRAM) → pushes to FIFO
 *   - IN channel reads from FIFO → writes to destination
 *
 * The "link" between them is automatic - both use channel number
 * and the hardware connects them via internal FIFO.
 */
typedef struct {
    uint8_t out_channel;    // OUT channel number (0-2)
    uint8_t in_channel;     // IN channel number (typically same as out)
    uint8_t priority;       // Priority (0-15)
} gdma_m2m_config_t;

/**
 * Build M2M OUT descriptor (source side)
 */
static inline void gdma_m2m_out_descriptor(
    gdma_descriptor_t* desc,
    const void* src,
    uint16_t size,
    int eof,
    gdma_descriptor_t* next
) {
    desc->dw0 = GDMA_DW0_SIZE(size) | 
                GDMA_DW0_LENGTH(size) | 
                GDMA_DW0_OWNER_DMA |
                (eof ? GDMA_DW0_SUC_EOF : 0);
    desc->buffer = (void*)src;  // Source address
    desc->next = next;
}

/**
 * Build M2M IN descriptor (destination side)
 * 
 * For IN channel, buffer = destination address (can be peripheral!)
 */
static inline void gdma_m2m_in_descriptor(
    gdma_descriptor_t* desc,
    void* dst,              // Destination (can be RMT RAM!)
    uint16_t size,
    int eof,
    gdma_descriptor_t* next
) {
    desc->dw0 = GDMA_DW0_SIZE(size) | 
                GDMA_DW0_LENGTH(size) | 
                GDMA_DW0_OWNER_DMA |
                (eof ? GDMA_DW0_SUC_EOF : 0);
    desc->buffer = dst;  // Destination address (0x60006100 for RMT!)
    desc->next = next;
}

/**
 * Initialize paired M2M channels for peripheral write
 * 
 * @param out_ch    OUT channel (0-2)
 * @param in_ch     IN channel (0-2, can be same as out_ch)
 * @param priority  Transfer priority (0-15)
 */
static inline void gdma_m2m_init_peripheral(
    uint8_t out_ch,
    uint8_t in_ch, 
    uint8_t priority
) {
    // Configure OUT channel (source reader)
    gdma_tx_reset(out_ch);
    uint32_t out_conf0 = GDMA_OUT_ETM_EN | GDMA_OUT_EOF_MODE;
    GDMA_REG(GDMA_OUT_CONF0_CH(out_ch)) = out_conf0;
    GDMA_REG(GDMA_OUT_PERI_SEL_CH(out_ch)) = GDMA_PERI_SEL_M2M;
    GDMA_REG(GDMA_OUT_PRI_CH(out_ch)) = priority & 0xF;
    
    // Configure IN channel (destination writer)
    gdma_rx_reset(in_ch);
    uint32_t in_conf0 = GDMA_IN_ETM_EN;
    GDMA_REG(GDMA_IN_CONF0_CH(in_ch)) = in_conf0;
    GDMA_REG(GDMA_IN_PERI_SEL_CH(in_ch)) = GDMA_PERI_SEL_M2M;
    GDMA_REG(GDMA_IN_PRI_CH(in_ch)) = priority & 0xF;
    
    // Enable IN channel EOF interrupt (for completion detection)
    GDMA_REG(GDMA_IN_INT_ENA_CH(in_ch)) = GDMA_IN_INT_EOF;
}

/**
 * Start M2M transfer
 * 
 * Order matters: IN channel must be ready before OUT starts
 */
static inline void gdma_m2m_start(uint8_t out_ch, uint8_t in_ch,
                                   gdma_descriptor_t* out_desc,
                                   gdma_descriptor_t* in_desc) {
    // Set IN descriptor (destination)
    uint32_t in_addr = ((uint32_t)in_desc) & GDMA_INLINK_ADDR_MASK;
    GDMA_REG(GDMA_IN_LINK_CH(in_ch)) = in_addr;
    
    // Set OUT descriptor (source)
    uint32_t out_addr = ((uint32_t)out_desc) & GDMA_OUTLINK_ADDR_MASK;
    GDMA_REG(GDMA_OUT_LINK_CH(out_ch)) = out_addr;
    
    // Start IN first (it will wait for data from OUT)
    GDMA_REG(GDMA_IN_LINK_CH(in_ch)) |= GDMA_INLINK_START;
    
    // Then start OUT (triggers the transfer)
    GDMA_REG(GDMA_OUT_LINK_CH(out_ch)) |= GDMA_OUTLINK_START;
}

/**
 * Check if IN channel is idle (for M2M completion)
 */
static inline int gdma_rx_is_idle(uint8_t channel) {
    return (GDMA_REG(GDMA_IN_LINK_CH(channel)) & GDMA_INLINK_PARK) != 0;
}

/**
 * Check if M2M transfer is complete
 * Must check BOTH channels - data flows OUT->FIFO->IN
 */
static inline int gdma_m2m_is_complete(uint8_t out_ch, uint8_t in_ch) {
    return gdma_tx_is_idle(out_ch) && gdma_rx_is_idle(in_ch);
}

// ============================================================
// RMT Memory Address (target for M2M)
// ============================================================

#define RMT_CH0_RAM_ADDR            0x60006100  // RMT channel 0 memory
#define RMT_CH1_RAM_ADDR            0x600061C0  // RMT channel 1 memory
#define RMT_RAM_SIZE_PER_CH         192         // 48 words × 4 bytes

#ifdef __cplusplus
}
#endif

#endif // REFLEX_GDMA_H
