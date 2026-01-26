/**
 * reflex_fault.h - Recoverable Exception Handling for Substrate Discovery
 *
 * Enables safe probing of potentially unmapped memory by catching
 * access faults and recovering without crashing.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#ifndef REFLEX_FAULT_H
#define REFLEX_FAULT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Fault Cause Codes (RISC-V mcause)
// ============================================================

#define FAULT_CAUSE_LOAD_MISALIGN   4   // Load address misaligned
#define FAULT_CAUSE_LOAD_ACCESS     5   // Load access fault
#define FAULT_CAUSE_STORE_MISALIGN  6   // Store address misaligned
#define FAULT_CAUSE_STORE_ACCESS    7   // Store access fault
#define FAULT_CAUSE_CACHE_ERROR     25  // 0x19 - Cache error (ESP-specific)

// ============================================================
// Fault State Structure
// ============================================================

typedef struct {
    volatile bool active;       // True if we're in a guarded probe
    volatile bool faulted;      // True if fault occurred
    volatile uint32_t addr;     // Address being probed
    volatile uint32_t cause;    // mcause value if faulted
    volatile uint32_t tval;     // mtval value if faulted (faulting address)
} fault_state_t;

// Global fault state (one per system - not thread-safe)
extern fault_state_t g_fault_state;

// ============================================================
// API Functions
// ============================================================

/**
 * Initialize fault handling system.
 * Must be called before any guarded probing.
 * Installs custom exception handler.
 */
void fault_init(void);

/**
 * Check if fault recovery is enabled.
 * Returns true if exception vector hooking is active.
 */
bool fault_recovery_enabled(void);

/**
 * Begin a guarded memory access region.
 * Call this before attempting a potentially faulting access.
 *
 * @param addr The address about to be accessed
 */
static inline void fault_guard_begin(uint32_t addr) {
    g_fault_state.addr = addr;
    g_fault_state.faulted = false;
    g_fault_state.cause = 0;
    g_fault_state.tval = 0;
    // Memory barrier to ensure state is visible before access
    __asm__ volatile("fence rw,rw" ::: "memory");
    g_fault_state.active = true;
    __asm__ volatile("fence rw,rw" ::: "memory");
}

/**
 * End a guarded memory access region.
 * Call this after the potentially faulting access completes.
 */
static inline void fault_guard_end(void) {
    __asm__ volatile("fence rw,rw" ::: "memory");
    g_fault_state.active = false;
}

/**
 * Check if the last guarded access faulted.
 *
 * @return true if fault occurred, false otherwise
 */
static inline bool fault_occurred(void) {
    return g_fault_state.faulted;
}

/**
 * Get the cause of the last fault.
 * Only valid if fault_occurred() returns true.
 *
 * @return mcause value
 */
static inline uint32_t fault_cause(void) {
    return g_fault_state.cause;
}

/**
 * Get the faulting address of the last fault.
 * Only valid if fault_occurred() returns true.
 *
 * @return mtval value (faulting address)
 */
static inline uint32_t fault_tval(void) {
    return g_fault_state.tval;
}

/**
 * Check if a cause code represents a memory access fault.
 *
 * @param cause mcause value
 * @return true if it's a recoverable memory fault
 */
static inline bool fault_is_memory_fault(uint32_t cause) {
    return (cause == FAULT_CAUSE_LOAD_ACCESS ||
            cause == FAULT_CAUSE_STORE_ACCESS ||
            cause == FAULT_CAUSE_CACHE_ERROR ||
            cause == FAULT_CAUSE_LOAD_MISALIGN ||
            cause == FAULT_CAUSE_STORE_MISALIGN);
}

/**
 * Get human-readable name for fault cause.
 *
 * @param cause mcause value
 * @return String description
 */
const char* fault_cause_str(uint32_t cause);

/**
 * Attempt a guarded read. Returns true if read succeeded.
 * If exception recovery is working, this will catch faults.
 */
bool fault_try_read32(uint32_t addr, uint32_t *value);

/**
 * Attempt a guarded write. Returns true if write succeeded.
 */
bool fault_try_write32(uint32_t addr, uint32_t value);

/**
 * Get number of times fault handler was called (for debugging).
 */
uint32_t fault_handler_calls(void);

/**
 * Debug: Get last MTVEC value we tried to set.
 */
uint32_t fault_debug_mtvec_set(void);

/**
 * Debug: Get last MTVEC value actually read back.
 */
uint32_t fault_debug_mtvec_read(void);

#ifdef __cplusplus
}
#endif

#endif // REFLEX_FAULT_H
