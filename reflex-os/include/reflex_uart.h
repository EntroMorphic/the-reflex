/**
 * reflex_uart.h - Bare Metal Serial for ESP32-C6
 *
 * Direct register access. No ESP-IDF. No drivers.
 *
 * ESP32-C6 DevKitC-1 uses USB Serial/JTAG (not UART0) for console.
 * The USB Serial/JTAG peripheral is at 0x6000F000.
 */

#ifndef REFLEX_UART_H
#define REFLEX_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 USB Serial/JTAG Registers (from TRM)
// ============================================================

#define USB_SERIAL_JTAG_BASE    0x6000F000

#define USJ_EP1_REG             (*(volatile uint32_t*)(USB_SERIAL_JTAG_BASE + 0x00))
#define USJ_EP1_CONF_REG        (*(volatile uint32_t*)(USB_SERIAL_JTAG_BASE + 0x04))

// EP1_CONF register bits (from usb_serial_jtag_struct.h)
// Bit 0: wr_done - set to flush TX buffer
// Bit 1: serial_in_ep_data_free - TX FIFO has space (RO)
// Bit 2: serial_out_ep_data_avail - RX FIFO has data (RO)
#define USJ_WR_DONE                  (1 << 0)   // Write flush trigger
#define USJ_SERIAL_IN_EP_DATA_FREE   (1 << 1)   // TX FIFO has space
#define USJ_SERIAL_OUT_EP_DATA_AVAIL (1 << 2)   // RX FIFO has data

// ============================================================
// Core Serial Functions (USB Serial/JTAG)
// ============================================================

/**
 * Check if TX FIFO has space
 */
static inline int uart_tx_ready(void) {
    return (USJ_EP1_CONF_REG & USJ_SERIAL_IN_EP_DATA_FREE) != 0;
}

/**
 * Check if RX FIFO has data
 */
static inline int uart_rx_available(void) {
    return (USJ_EP1_CONF_REG & USJ_SERIAL_OUT_EP_DATA_AVAIL) != 0;
}

/**
 * Write single byte (blocking)
 */
static inline void uart_putc(char c) {
    while (!uart_tx_ready());
    USJ_EP1_REG = (uint8_t)c;
}

/**
 * Flush TX buffer
 */
static inline void uart_flush(void) {
    USJ_EP1_CONF_REG |= USJ_WR_DONE;
    // Wait a bit for USB to transmit
    for (volatile int i = 0; i < 1000; i++);
}

/**
 * Write string (blocking)
 */
static inline void uart_puts(const char* s) {
    while (*s) {
        uart_putc(*s++);
    }
    uart_flush();
}

/**
 * Read single byte (blocking)
 */
static inline char uart_getc(void) {
    while (!uart_rx_available());
    return (char)(USJ_EP1_REG & 0xFF);
}

/**
 * Read single byte (non-blocking)
 * Returns -1 if no data available
 */
static inline int uart_getc_nb(void) {
    if (!uart_rx_available()) return -1;
    return (int)(USJ_EP1_REG & 0xFF);
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

#ifdef __cplusplus
}
#endif

#endif // REFLEX_UART_H
