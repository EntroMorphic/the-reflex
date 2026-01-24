/**
 * reflex_gpio.h - GPIO as Channels for ESP32-C6
 *
 * Every GPIO pin is a channel:
 * - Input pin: hardware writes (external world), we read
 * - Output pin: we write, hardware reads (drives external world)
 *
 * Direct register access for zero overhead on HP core.
 */

#ifndef REFLEX_GPIO_H
#define REFLEX_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 GPIO Register Addresses (from TRM)
// ============================================================

#define GPIO_BASE           0x60091000

#define GPIO_OUT_REG        (GPIO_BASE + 0x0004)  // Output data
#define GPIO_OUT_W1TS_REG   (GPIO_BASE + 0x0008)  // Output set (write 1 to set)
#define GPIO_OUT_W1TC_REG   (GPIO_BASE + 0x000C)  // Output clear (write 1 to clear)
#define GPIO_ENABLE_REG     (GPIO_BASE + 0x0020)  // Output enable
#define GPIO_ENABLE_W1TS_REG (GPIO_BASE + 0x0024) // Enable set
#define GPIO_ENABLE_W1TC_REG (GPIO_BASE + 0x0028) // Enable clear
#define GPIO_IN_REG         (GPIO_BASE + 0x003C)  // Input data

// Pin function configuration base
#define IO_MUX_BASE         0x60090000
#define GPIO_FUNC_OUT_SEL_BASE (GPIO_BASE + 0x0554)

// ============================================================
// Direct Register Access Macros
// ============================================================

#define REG_WRITE(addr, val)  (*(volatile uint32_t*)(addr) = (val))
#define REG_READ(addr)        (*(volatile uint32_t*)(addr))
#define REG_SET_BIT(addr, bit) REG_WRITE(addr, REG_READ(addr) | (bit))
#define REG_CLR_BIT(addr, bit) REG_WRITE(addr, REG_READ(addr) & ~(bit))

// ============================================================
// GPIO Channel API
// ============================================================

/**
 * Configure pin as output (we write, world reads)
 */
static inline void gpio_set_output(uint8_t pin) {
    // Enable output
    REG_WRITE(GPIO_ENABLE_W1TS_REG, 1 << pin);

    // Set GPIO function (simple output, function 128 = GPIO)
    volatile uint32_t* func_sel = (volatile uint32_t*)(GPIO_FUNC_OUT_SEL_BASE + pin * 4);
    *func_sel = 128;  // GPIO matrix: simple GPIO output

    // Configure IO_MUX for GPIO function
    volatile uint32_t* io_mux = (volatile uint32_t*)(IO_MUX_BASE + 4 + pin * 4);
    *io_mux = (*io_mux & ~0x7) | 1;  // Function 1 = GPIO
}

/**
 * Configure pin as input (world writes, we read)
 */
static inline void gpio_set_input(uint8_t pin) {
    // Disable output
    REG_WRITE(GPIO_ENABLE_W1TC_REG, 1 << pin);

    // Configure IO_MUX for GPIO input
    volatile uint32_t* io_mux = (volatile uint32_t*)(IO_MUX_BASE + 4 + pin * 4);
    *io_mux = (*io_mux & ~0x7) | 1;  // Function 1 = GPIO
    *io_mux |= (1 << 9);  // Enable input
}

/**
 * Configure pin with pullup
 */
static inline void gpio_set_pullup(uint8_t pin) {
    volatile uint32_t* io_mux = (volatile uint32_t*)(IO_MUX_BASE + 4 + pin * 4);
    *io_mux |= (1 << 8);  // Pull-up enable
}

/**
 * Configure pin with pulldown
 */
static inline void gpio_set_pulldown(uint8_t pin) {
    volatile uint32_t* io_mux = (volatile uint32_t*)(IO_MUX_BASE + 4 + pin * 4);
    *io_mux |= (1 << 7);  // Pull-down enable
}

// ============================================================
// GPIO Output Channels (we signal to the world)
// ============================================================

/**
 * Write to output pin (signal the world)
 * This is the fastest possible GPIO write: 1-2 cycles
 */
static inline void gpio_write(uint8_t pin, bool value) {
    if (value) {
        REG_WRITE(GPIO_OUT_W1TS_REG, 1 << pin);
    } else {
        REG_WRITE(GPIO_OUT_W1TC_REG, 1 << pin);
    }
}

/**
 * Set output pin high
 */
static inline void gpio_set(uint8_t pin) {
    REG_WRITE(GPIO_OUT_W1TS_REG, 1 << pin);
}

/**
 * Set output pin low
 */
static inline void gpio_clear(uint8_t pin) {
    REG_WRITE(GPIO_OUT_W1TC_REG, 1 << pin);
}

/**
 * Toggle output pin
 */
static inline void gpio_toggle(uint8_t pin) {
    REG_WRITE(GPIO_OUT_REG, REG_READ(GPIO_OUT_REG) ^ (1 << pin));
}

/**
 * Write all output pins at once (32 pins max)
 */
static inline void gpio_write_all(uint32_t value) {
    REG_WRITE(GPIO_OUT_REG, value);
}

// ============================================================
// GPIO Input Channels (world signals us)
// ============================================================

/**
 * Read input pin (receive signal from world)
 * This is the fastest possible GPIO read: 1-2 cycles
 */
static inline bool gpio_read(uint8_t pin) {
    return (REG_READ(GPIO_IN_REG) >> pin) & 1;
}

/**
 * Read all input pins at once
 */
static inline uint32_t gpio_read_all(void) {
    return REG_READ(GPIO_IN_REG);
}

// ============================================================
// GPIO as Reflex Channels (wrapped with sequence semantics)
// ============================================================

#include "reflex.h"

/**
 * GPIO output channel - wraps a pin with channel semantics
 * Use for control plane, not hot path
 */
typedef struct {
    reflex_channel_t channel;
    uint8_t pin;
} gpio_output_channel_t;

static inline void gpio_output_init(gpio_output_channel_t* ch, uint8_t pin) {
    ch->pin = pin;
    ch->channel.sequence = 0;
    ch->channel.value = 0;
    ch->channel.timestamp = 0;
    ch->channel.flags = 0;
    gpio_set_output(pin);
}

static inline void gpio_output_signal(gpio_output_channel_t* ch, bool value) {
    gpio_write(ch->pin, value);
    reflex_signal(&ch->channel, value ? 1 : 0);
}

/**
 * GPIO input channel - wraps a pin with channel semantics
 * Use for monitoring, not hot path
 */
typedef struct {
    reflex_channel_t channel;
    uint8_t pin;
    bool last_value;
} gpio_input_channel_t;

static inline void gpio_input_init(gpio_input_channel_t* ch, uint8_t pin) {
    ch->pin = pin;
    ch->last_value = false;
    ch->channel.sequence = 0;
    ch->channel.value = 0;
    ch->channel.timestamp = 0;
    ch->channel.flags = 0;
    gpio_set_input(pin);
}

/**
 * Poll input and signal channel if changed
 * Returns true if value changed
 */
static inline bool gpio_input_poll(gpio_input_channel_t* ch) {
    bool value = gpio_read(ch->pin);
    if (value != ch->last_value) {
        ch->last_value = value;
        reflex_signal(&ch->channel, value ? 1 : 0);
        return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_GPIO_H
