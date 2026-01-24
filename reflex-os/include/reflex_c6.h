/**
 * reflex_c6.h - The Reflex Becomes the ESP32-C6
 *
 * This header IS the C6. Every peripheral is a channel.
 * The hardware already thinks in signals. We're just listening.
 *
 * Include this one header to access the entire chip as channels.
 */

#ifndef REFLEX_C6_H
#define REFLEX_C6_H

// Core primitive
#include "reflex.h"

// Hardware channels
#include "reflex_gpio.h"
#include "reflex_timer.h"
#include "reflex_adc.h"
#include "reflex_spline.h"

// Future:
// #include "reflex_spi.h"
// #include "reflex_uart.h"
// #include "reflex_wifi.h"
// #include "reflex_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 Identity
// ============================================================

#define REFLEX_CHIP_NAME    "ESP32-C6"
#define REFLEX_CHIP_FREQ    160000000   // 160 MHz
#define REFLEX_CYCLE_NS     6           // ~6.25 ns per cycle

// ============================================================
// Pin Assignments (ESP32-C6-DevKitC-1)
// ============================================================

// Onboard LED (directly controllable)
#define PIN_LED             8

// USB Serial/JTAG (directly controllable)
#define PIN_USB_DN          12
#define PIN_USB_DP          13

// Boot button
#define PIN_BOOT            9

// ============================================================
// Quick Setup Macros
// ============================================================

/**
 * Initialize minimal hardware for reflex operation
 */
static inline void reflex_c6_init(void) {
    // Nothing required - peripherals are memory-mapped
    // Just configure pins as needed
}

/**
 * Setup the onboard LED as output channel
 */
static inline void reflex_led_init(void) {
    gpio_set_output(PIN_LED);
}

/**
 * Toggle the onboard LED
 */
static inline void reflex_led_toggle(void) {
    gpio_toggle(PIN_LED);
}

/**
 * Set the onboard LED
 */
static inline void reflex_led_set(bool on) {
    gpio_write(PIN_LED, on);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_C6_H
