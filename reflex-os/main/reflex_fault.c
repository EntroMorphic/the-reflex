/**
 * reflex_fault.c - Recoverable Exception Handling Implementation
 *
 * Provides fault recovery for memory probing by hooking the exception
 * vector and modifying MEPC to skip faulting instructions.
 *
 * IMPORTANT: This is experimental. ESP-IDF's exception handling is not
 * designed for recovery. We use a direct vector hook to gain control
 * before ESP-IDF's panic handler runs.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#include "reflex_fault.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "riscv/rv_utils.h"

static const char* TAG = "FAULT";

// Track if fault recovery is enabled
static bool s_fault_recovery_enabled = false;

// ============================================================
// Global Fault State
// ============================================================

fault_state_t g_fault_state = {
    .active = false,
    .faulted = false,
    .addr = 0,
    .cause = 0,
    .tval = 0,
};

// ============================================================
// Fault Cause Strings
// ============================================================

const char* fault_cause_str(uint32_t cause) {
    switch (cause) {
        case 0:  return "Instruction misaligned";
        case 1:  return "Instruction access fault";
        case 2:  return "Illegal instruction";
        case 3:  return "Breakpoint";
        case 4:  return "Load address misaligned";
        case 5:  return "Load access fault";
        case 6:  return "Store address misaligned";
        case 7:  return "Store access fault";
        case 8:  return "Environment call (U-mode)";
        case 9:  return "Environment call (S-mode)";
        case 11: return "Environment call (M-mode)";
        case 12: return "Instruction page fault";
        case 13: return "Load page fault";
        case 15: return "Store page fault";
        case 25: return "Cache error";  // ESP-specific
        default: return "Unknown";
    }
}

// ============================================================
// Original Vector Table (for chaining)
// ============================================================

static uint32_t s_original_mtvec = 0;

// ============================================================
// Custom Exception Handler (Assembly Interface)
// ============================================================

// Debug counter to verify handler is being called
static volatile uint32_t s_handler_calls = 0;

// This is the C handler called from our assembly stub
// It runs with interrupts disabled and minimal stack
void IRAM_ATTR fault_handler_c(void) {
    s_handler_calls++;

    // Read MEPC
    uint32_t mepc;
    __asm__ volatile("csrr %0, mepc" : "=r"(mepc));

    // Always skip 4 bytes (assume 32-bit instruction for now)
    // EBREAK is 32-bit: 0x00100073
    __asm__ volatile("csrw mepc, %0" :: "r"(mepc + 4));

    // If we're in guarded mode, record the fault
    if (g_fault_state.active) {
        uint32_t mcause, mtval;
        __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
        __asm__ volatile("csrr %0, mtval" : "=r"(mtval));

        g_fault_state.faulted = true;
        g_fault_state.cause = mcause;
        g_fault_state.tval = mtval;
        g_fault_state.active = false;
    }

    // Return - mret will be executed by assembly stub
}

// Get handler call count for debugging
uint32_t fault_handler_calls(void) {
    return s_handler_calls;
}

// ============================================================
// Assembly Exception Entry Point
// ============================================================

// Vector table entry for exception handling
// Must be aligned to 256 bytes for proper vectored mode operation
// In vectored mode, CPU jumps to (MTVEC & ~3) for synchronous exceptions
__asm__(
    ".section .iram1,\"ax\"\n"
    ".global fault_vector_entry\n"
    ".align 8\n"  // 256-byte alignment for vector table
    "fault_vector_entry:\n"
    // This is where exceptions land in vectored mode
    // Save caller-saved registers we'll use
    "    addi sp, sp, -16\n"
    "    sw ra, 0(sp)\n"
    "    sw t0, 4(sp)\n"
    "    sw t1, 8(sp)\n"
    "    sw t2, 12(sp)\n"

    // Call C handler
    "    call fault_handler_c\n"

    // Restore registers
    "    lw t2, 12(sp)\n"
    "    lw t1, 8(sp)\n"
    "    lw t0, 4(sp)\n"
    "    lw ra, 0(sp)\n"
    "    addi sp, sp, 16\n"

    // Return from exception
    "    mret\n"
);

extern void fault_vector_entry(void);

// ============================================================
// Initialization
// ============================================================

void fault_init(void) {
    ESP_LOGW(TAG, "Initializing fault handling system...");

    // Reset state
    g_fault_state.active = false;
    g_fault_state.faulted = false;
    g_fault_state.addr = 0;
    g_fault_state.cause = 0;
    g_fault_state.tval = 0;

    // Save original MTVEC (don't install hook yet - do it per-probe)
    __asm__ volatile("csrr %0, mtvec" : "=r"(s_original_mtvec));
    ESP_LOGW(TAG, "Original MTVEC: 0x%08lx", (unsigned long)s_original_mtvec);

    // Fault recovery is available (we install/restore per-probe)
    s_fault_recovery_enabled = true;
    ESP_LOGW(TAG, "Fault recovery: ENABLED (per-probe MTVEC hooking)");

    ESP_LOGW(TAG, "Fault handler initialized");
}

// ============================================================
// Critical Section for Fault Recovery
// ============================================================

// Install our exception handler (call with interrupts disabled!)
static inline void fault_hook_install(void) {
    uint32_t new_mtvec = (uint32_t)fault_vector_entry;
    __asm__ volatile("csrw mtvec, %0" :: "r"(new_mtvec));
}

// Restore original exception handler
static inline void fault_hook_restore(void) {
    __asm__ volatile("csrw mtvec, %0" :: "r"(s_original_mtvec));
}

// ============================================================
// Safe Probe Wrappers
// ============================================================

// These provide a safer way to probe without relying on exception recovery

// Debug: track MTVEC changes
static volatile uint32_t s_last_mtvec_set = 0;
static volatile uint32_t s_last_mtvec_read = 0;

/**
 * Attempt a guarded read. Returns true if read succeeded.
 * Disables interrupts and hooks MTVEC for fault recovery.
 */
bool fault_try_read32(uint32_t addr, uint32_t *value) {
    // Disable interrupts
    uint32_t mstatus;
    __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(mstatus));  // Clear MIE bit

    // Install our exception handler
    fault_hook_install();

    // DEBUG: Verify MTVEC was set
    uint32_t actual_mtvec;
    __asm__ volatile("csrr %0, mtvec" : "=r"(actual_mtvec));
    s_last_mtvec_set = (uint32_t)fault_vector_entry;
    s_last_mtvec_read = actual_mtvec;

    // Set up fault guard
    fault_guard_begin(addr);

    // Attempt the read
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    *value = *ptr;

    // Memory barrier
    __asm__ volatile("fence r,r" ::: "memory");

    fault_guard_end();

    // Restore original exception handler
    fault_hook_restore();

    // Restore interrupts
    __asm__ volatile("csrw mstatus, %0" :: "r"(mstatus));

    return !fault_occurred();
}

// Debug accessors
uint32_t fault_debug_mtvec_set(void) { return s_last_mtvec_set; }
uint32_t fault_debug_mtvec_read(void) { return s_last_mtvec_read; }

/**
 * Attempt a guarded write. Returns true if write succeeded.
 * Disables interrupts and hooks MTVEC for fault recovery.
 */
bool fault_try_write32(uint32_t addr, uint32_t value) {
    // Disable interrupts
    uint32_t mstatus;
    __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(mstatus));  // Clear MIE bit

    // Install our exception handler
    fault_hook_install();

    // DEBUG: Verify MTVEC was set
    uint32_t actual_mtvec;
    __asm__ volatile("csrr %0, mtvec" : "=r"(actual_mtvec));
    s_last_mtvec_set = (uint32_t)fault_vector_entry;
    s_last_mtvec_read = actual_mtvec;

    // Set up fault guard
    fault_guard_begin(addr);

    // Attempt the write
    volatile uint32_t *ptr = (volatile uint32_t *)addr;
    *ptr = value;

    // Full memory barrier to force write completion
    __asm__ volatile("fence iorw,iorw" ::: "memory");

    fault_guard_end();

    // Restore original exception handler
    fault_hook_restore();

    // Restore interrupts
    __asm__ volatile("csrw mstatus, %0" :: "r"(mstatus));

    return !fault_occurred();
}

/**
 * Check if fault recovery is enabled.
 */
bool fault_recovery_enabled(void) {
    return s_fault_recovery_enabled;
}
