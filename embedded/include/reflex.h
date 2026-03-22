/**
 * reflex.h - The Reflex OS for ESP32-C6
 *
 * This is the entire operating system primitive.
 * 50 lines. No scheduler. No queues. Just channels.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#ifndef REFLEX_H
#define REFLEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reflex Channel - The universal coordination primitive
 *
 * 32-byte aligned for cache line efficiency.
 * Sequence number provides ordering and change detection.
 */
typedef struct {
    volatile uint32_t sequence;      // Monotonic counter (writer increments)
    volatile uint32_t timestamp;     // Cycle count when written
    volatile uint32_t value;         // Primary payload
    volatile uint32_t flags;         // Application-defined flags
    uint32_t _pad[4];                // Pad to 32 bytes
} __attribute__((aligned(32))) reflex_channel_t;

/**
 * RISC-V memory fence
 * Ensures all prior reads/writes complete before subsequent ones.
 */
#define REFLEX_FENCE() __asm__ volatile("fence rw, rw" ::: "memory")

/**
 * Compiler-only barrier (no hardware fence)
 */
#define REFLEX_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/**
 * Read cycle counter
 * ESP32-C6 runs at 160MHz, so 1 cycle = 6.25ns
 *
 * BARE METAL: Direct CSR read, no ESP-IDF dependency.
 *
 * ESP32-C6 uses a custom performance counter CSR (0x7e2), NOT the
 * standard RISC-V mcycle (0xB00). This CSR is accessible from
 * application code without requiring M-mode.
 *
 * Reference: ESP-IDF components/riscv/include/riscv/rv_utils.h
 */

// ESP32-C6 performance counter CSR (PCCR_MACHINE)
#define REFLEX_CSR_PCCR  0x7e2

static inline uint32_t reflex_cycles(void) {
    uint32_t cycles;
    __asm__ volatile("csrr %0, %1" : "=r"(cycles) : "i"(REFLEX_CSR_PCCR));
    return cycles;
}

/**
 * Signal: Write value and increment sequence
 *
 * The producer calls this to publish a new value.
 * Fences ensure ordering: value is visible before sequence increments.
 */
static inline void reflex_signal(reflex_channel_t* ch, uint32_t val) {
    ch->value = val;
    ch->timestamp = reflex_cycles();
    REFLEX_FENCE();
    ch->sequence++;
    REFLEX_FENCE();
}

/**
 * Signal with explicit timestamp
 */
static inline void reflex_signal_ts(reflex_channel_t* ch, uint32_t val, uint32_t ts) {
    ch->value = val;
    ch->timestamp = ts;
    REFLEX_FENCE();
    ch->sequence++;
    REFLEX_FENCE();
}

/**
 * Wait: Spin until sequence changes
 *
 * The consumer calls this to wait for new data.
 * Returns the new sequence number.
 */
static inline uint32_t reflex_wait(reflex_channel_t* ch, uint32_t last_seq) {
    while (ch->sequence == last_seq) {
        __asm__ volatile("nop");  // Reduce power, hint to CPU
    }
    REFLEX_FENCE();
    return ch->sequence;
}

/**
 * Wait with timeout
 *
 * Returns new sequence if signaled, 0 if timeout.
 * timeout_cycles is in CPU cycles (160MHz = 6.25ns per cycle)
 */
static inline uint32_t reflex_wait_timeout(reflex_channel_t* ch,
                                            uint32_t last_seq,
                                            uint32_t timeout_cycles) {
    uint32_t start = reflex_cycles();
    while (ch->sequence == last_seq) {
        if ((reflex_cycles() - start) > timeout_cycles) {
            return 0;  // Timeout
        }
        __asm__ volatile("nop");
    }
    REFLEX_FENCE();
    return ch->sequence;
}

/**
 * Try wait: Non-blocking check
 *
 * Returns new sequence if changed, 0 if unchanged.
 */
static inline uint32_t reflex_try_wait(reflex_channel_t* ch, uint32_t last_seq) {
    REFLEX_COMPILER_BARRIER();
    if (ch->sequence != last_seq) {
        REFLEX_FENCE();
        return ch->sequence;
    }
    return 0;
}

/**
 * Read value from channel (after wait)
 */
static inline uint32_t reflex_read(reflex_channel_t* ch) {
    return ch->value;
}

/**
 * Read timestamp from channel (after wait)
 */
static inline uint32_t reflex_read_timestamp(reflex_channel_t* ch) {
    return ch->timestamp;
}

/**
 * Calculate latency in cycles between signal and read
 */
static inline uint32_t reflex_latency(reflex_channel_t* ch) {
    return reflex_cycles() - ch->timestamp;
}

/**
 * Utility: Cycles to nanoseconds (at 160MHz)
 */
static inline uint32_t reflex_cycles_to_ns(uint32_t cycles) {
    // 160MHz = 6.25ns per cycle = 25/4 ns per cycle
    return (cycles * 25) / 4;
}

/**
 * Utility: Nanoseconds to cycles (at 160MHz)
 */
static inline uint32_t reflex_ns_to_cycles(uint32_t ns) {
    // 160MHz = 0.16 cycles per ns = 4/25 cycles per ns
    return (ns * 4) / 25;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_H
