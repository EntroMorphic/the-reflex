/**
 * substrate_main.c - Substrate Discovery Entry Point
 *
 * Minimal Viable Experiment for The Reflex discovering its own body.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

#include "reflex.h"
#include "reflex_substrate.h"
#include "reflex_fault.h"

static const char* TAG = "SUBSTRATE_MAIN";

// Find a safe RAM address by allocating and freeing a buffer
// This ensures we're not probing firmware's own code/data regions
static uint32_t find_safe_ram_address(void) {
    void* ptr = heap_caps_malloc(256, MALLOC_CAP_INTERNAL);
    if (ptr == NULL) {
        ESP_LOGE(TAG, "Could not allocate test buffer");
        return 0;
    }
    uint32_t addr = (uint32_t)ptr;
    heap_caps_free(ptr);
    // Return address slightly offset into the buffer area
    return addr + 64;
}

// ============================================================
// Minimum Viable Experiment (MVE)
// ============================================================

static void mve_substrate(void) {
    // Use LOGW since default log level is WARNING
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "  MINIMUM VIABLE EXPERIMENT: SUBSTRATE");
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "");

    int pass = 0;

    // Test 1: RAM detection (HP SRAM)
    ESP_LOGW(TAG, "Test 1: RAM detection");
    uint32_t safe_ram = find_safe_ram_address();
    if (safe_ram == 0) {
        ESP_LOGE(TAG, "  SKIP: Could not find safe RAM address");
    } else {
        probe_result_t r1 = substrate_probe(safe_ram);
        bool test1_pass = (r1.type == MEM_RAM);
        if (test1_pass) pass++;
        ESP_LOGW(TAG, "  0x%08lx: type=%s, expect=RAM [%s]",
                 (unsigned long)safe_ram,
                 mem_type_str(r1.type),
                 test1_pass ? "PASS" : "FAIL");
    }

    // Test 2: ROM detection (Flash)
    ESP_LOGW(TAG, "Test 2: ROM detection");
    probe_result_t r2 = substrate_probe(0x42000100);  // Flash
    bool test2_pass = (r2.type == MEM_ROM);
    if (test2_pass) pass++;
    ESP_LOGW(TAG, "  0x42000100: type=%s, expect=ROM [%s]",
             mem_type_str(r2.type),
             test2_pass ? "PASS" : "FAIL");

    // Test 3: Register detection (GPIO)
    ESP_LOGW(TAG, "Test 3: REGISTER detection (read-only probe)");
    probe_result_t r3 = substrate_probe_readonly(0x60004000);  // GPIO base
    bool test3_pass = (r3.read_cycles > 0);
    if (test3_pass) pass++;
    ESP_LOGW(TAG, "  0x60004000: read succeeded [%s]",
             test3_pass ? "PASS" : "FAIL");

    // Test 4: Self detection
    ESP_LOGW(TAG, "Test 4: SELF detection");
    // Mark our stack region as self
    uint32_t stack_addr;
    __asm__ volatile("mv %0, sp" : "=r"(stack_addr));
    substrate_mark_self(stack_addr - 0x1000, stack_addr + 0x1000);

    probe_result_t r4 = substrate_probe(stack_addr);
    bool test4_pass = (r4.type == MEM_SELF);
    if (test4_pass) pass++;
    ESP_LOGW(TAG, "  0x%08lx (stack): type=%s, expect=SELF [%s]",
             (unsigned long)stack_addr,
             mem_type_str(r4.type),
             test4_pass ? "PASS" : "FAIL");

    // Test 5: Fault Recovery (M1 milestone)
    ESP_LOGW(TAG, "Test 5: FAULT RECOVERY (M1)");
    if (!fault_recovery_enabled()) {
        ESP_LOGW(TAG, "  SKIP: Fault recovery not enabled");
    } else {
        // Test: Trigger EBREAK which MUST cause exception (mcause=3)
        ESP_LOGW(TAG, "  Triggering EBREAK instruction...");

        // Disable interrupts and install our handler
        uint32_t mstatus;
        __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(mstatus));

        // Get original MTVEC
        uint32_t orig_mtvec;
        __asm__ volatile("csrr %0, mtvec" : "=r"(orig_mtvec));

        // Set our handler
        extern void fault_vector_entry(void);
        uint32_t our_mtvec = (uint32_t)fault_vector_entry;
        __asm__ volatile("csrw mtvec, %0" :: "r"(our_mtvec));

        // Verify
        uint32_t actual_mtvec;
        __asm__ volatile("csrr %0, mtvec" : "=r"(actual_mtvec));
        ESP_LOGW(TAG, "  MTVEC: orig=0x%08lx, set=0x%08lx, actual=0x%08lx",
                 (unsigned long)orig_mtvec, (unsigned long)our_mtvec, (unsigned long)actual_mtvec);

        // Set up fault guard
        fault_guard_begin(0);

        // Trigger EBREAK - this MUST cause an exception
        __asm__ volatile("ebreak");

        fault_guard_end();

        // Restore
        __asm__ volatile("csrw mtvec, %0" :: "r"(orig_mtvec));
        __asm__ volatile("csrw mstatus, %0" :: "r"(mstatus));

        bool test5_pass = fault_occurred();
        ESP_LOGW(TAG, "  faulted=%d, cause=0x%lx (%s)",
                 fault_occurred(),
                 (unsigned long)fault_cause(),
                 fault_cause_str(fault_cause()));
        ESP_LOGW(TAG, "  Handler calls: %lu", (unsigned long)fault_handler_calls());

        if (test5_pass) {
            pass++;
            ESP_LOGW(TAG, "  >>> EBREAK RECOVERED! Handler works! <<<");
        }
        ESP_LOGW(TAG, "  FAULT RECOVERY: [%s]", test5_pass ? "PASS" : "FAIL");
    }

    // Summary
    int total_tests = fault_recovery_enabled() ? 5 : 4;
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "  MVE RESULT: %d/%d tests passed", pass, total_tests);
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "");

}

// ============================================================
// Full Discovery Sequence
// ============================================================

static void run_discovery(void) {
    memory_map_t map;

    // Try to load existing map
    if (substrate_map_load(&map)) {
        ESP_LOGW(TAG, "Loaded existing map from NVS");
        substrate_map_print(&map);
        ESP_LOGW(TAG, "To re-discover, erase NVS and reboot");
        return;
    }

    // Initialize fresh map
    substrate_map_init(&map);

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "  M2: SUBSTRATE CARTOGRAPHY");
    ESP_LOGW(TAG, "  The Reflex maps its body");
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "");

    // Phase 1: Coarse discovery
    ESP_LOGW(TAG, "--- PHASE 1: Coarse Discovery ---");
    substrate_discover_coarse(&map);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Phase 2: Fine discovery
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "--- PHASE 2: Fine Discovery (4KB stride) ---");
    substrate_discover_fine(&map);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Phase 3: Register discovery
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "--- PHASE 3: Peripheral Census ---");
    substrate_discover_registers(&map);

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "  M2 COMPLETE: SUBSTRATE MAPPED");
    ESP_LOGW(TAG, "===========================================");
    substrate_map_print(&map);
}

// ============================================================
// Streaming (for Rerun visualization)
// ============================================================

// TODO: Add streaming protocol for substrate discovery
// This would send probe results to Rerun in real-time

// ============================================================
// Main Entry Point
// ============================================================

void app_main(void) {
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "  THE REFLEX: SUBSTRATE DISCOVERY");
    ESP_LOGW(TAG, "  The body discovers itself");
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Initialize substrate system
    ESP_LOGW(TAG, "Calling substrate_init()...");
    substrate_init();
    ESP_LOGW(TAG, "substrate_init() complete.");

    // Run MVE first
    ESP_LOGW(TAG, "Starting MVE...");
    mve_substrate();
    ESP_LOGW(TAG, "MVE complete, waiting 1 second...");

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Run full discovery (M1 fault handler now enabled)
    // Flash limited to first 192KB to avoid cache errors on unprogrammed regions
    run_discovery();

    // Keep alive
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Discovery complete. System idle.");
    ESP_LOGW(TAG, "The Reflex now knows its body.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
