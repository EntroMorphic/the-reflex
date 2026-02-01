/**
 * reflex_uart.h - Bare Metal UART for ESP32-C6
 *
 * Direct register access. No ESP-IDF. No drivers.
 * Just bits on a wire.
 *
 * UART0 is connected to USB-Serial on DevKitC-1.
 * Default: 115200 baud, 8N1 (configured by ROM bootloader)
 */

#ifndef REFLEX_UART_H
#define REFLEX_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 UART0 Register Addresses (from TRM Ch. 26)
// ============================================================

#define UART0_BASE              0x60000000

#define UART_FIFO_REG           (UART0_BASE + 0x00)   // TX/RX FIFO
#define UART_INT_RAW_REG        (UART0_BASE + 0x04)   // Raw interrupt status
#define UART_INT_ST_REG         (UART0_BASE + 0x08)   // Masked interrupt status
#define UART_INT_ENA_REG        (UART0_BASE + 0x0C)   // Interrupt enable
#define UART_INT_CLR_REG        (UART0_BASE + 0x10)   // Interrupt clear
#define UART_CLKDIV_REG         (UART0_BASE + 0x14)   // Clock divider
#define UART_STATUS_REG         (UART0_BASE + 0x1C)   // Status register
#define UART_CONF0_REG          (UART0_BASE + 0x20)   // Configuration 0
#define UART_CONF1_REG          (UART0_BASE + 0x24)   // Configuration 1

// Status register bits
#define UART_TXFIFO_CNT_SHIFT   16
#define UART_TXFIFO_CNT_MASK    (0x3FF << UART_TXFIFO_CNT_SHIFT)
#define UART_TX_DONE_BIT        (1 << 14)
#define UART_RXFIFO_CNT_SHIFT   0
#define UART_RXFIFO_CNT_MASK    (0x3FF << UART_RXFIFO_CNT_SHIFT)

// FIFO sizes
#define UART_FIFO_SIZE          128

// ============================================================
// Register Access
// ============================================================

#define UART_REG(offset)        (*(volatile uint32_t*)(UART0_BASE + (offset)))
#define UART_READ(offset)       UART_REG(offset)
#define UART_WRITE(offset, val) (UART_REG(offset) = (val))

// ============================================================
// Core UART Functions
// ============================================================

/**
 * Check if TX FIFO has space
 */
static inline int uart_tx_ready(void) {
    uint32_t status = UART_READ(0x1C);
    uint32_t fifo_cnt = (status >> 16) & 0x3FF;
    return fifo_cnt < UART_FIFO_SIZE;
}

/**
 * Check if RX FIFO has data
 */
static inline int uart_rx_available(void) {
    uint32_t status = UART_READ(0x1C);
    return (status & 0x3FF) > 0;
}

/**
 * Write single byte (blocking)
 */
static inline void uart_putc(char c) {
    // Wait for space in TX FIFO
    while (!uart_tx_ready());
    UART_WRITE(0x00, c);
}

/**
 * Write string (blocking)
 */
static inline void uart_puts(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');  // CR+LF
        uart_putc(*s++);
    }
}

/**
 * Read single byte (blocking)
 */
static inline char uart_getc(void) {
    while (!uart_rx_available());
    return (char)(UART_READ(0x00) & 0xFF);
}

/**
 * Read single byte (non-blocking)
 * Returns -1 if no data available
 */
static inline int uart_getc_nb(void) {
    if (!uart_rx_available()) return -1;
    return (int)(UART_READ(0x00) & 0xFF);
}

// ============================================================
// Formatted Output (minimal, no printf dependency)
// ============================================================

/**
 * Print hex nibble
 */
static inline void uart_put_nibble(uint8_t n) {
    uart_putc(n < 10 ? '0' + n : 'a' + n - 10);
}

/**
 * Print 8-bit hex
 */
static inline void uart_puthex8(uint8_t val) {
    uart_put_nibble((val >> 4) & 0xF);
    uart_put_nibble(val & 0xF);
}

/**
 * Print 32-bit hex
 */
static inline void uart_puthex32(uint32_t val) {
    for (int i = 28; i >= 0; i -= 4) {
        uart_put_nibble((val >> i) & 0xF);
    }
}

/**
 * Print unsigned decimal
 */
static inline void uart_putu(uint32_t val) {
    char buf[10];
    int i = 0;
    
    if (val == 0) {
        uart_putc('0');
        return;
    }
    
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

/**
 * Print signed decimal
 */
static inline void uart_puti(int32_t val) {
    if (val < 0) {
        uart_putc('-');
        val = -val;
    }
    uart_putu((uint32_t)val);
}

// ============================================================
// Convenience Macros
// ============================================================

// Print with newline
#define uart_println(s) do { uart_puts(s); uart_puts("\n"); } while(0)

// Print label: value
#define uart_print_val(label, val) do { \
    uart_puts(label); \
    uart_puts(": "); \
    uart_putu(val); \
    uart_puts("\n"); \
} while(0)

// Print label: hex value
#define uart_print_hex(label, val) do { \
    uart_puts(label); \
    uart_puts(": 0x"); \
    uart_puthex32(val); \
    uart_puts("\n"); \
} while(0)

// ============================================================
// Flush / Sync
// ============================================================

/**
 * Wait for TX to complete (all data sent)
 */
static inline void uart_flush(void) {
    // Wait for TX FIFO empty and TX done
    while (1) {
        uint32_t status = UART_READ(0x1C);
        uint32_t fifo_cnt = (status >> 16) & 0x3FF;
        if (fifo_cnt == 0 && (status & UART_TX_DONE_BIT)) break;
    }
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_UART_H
