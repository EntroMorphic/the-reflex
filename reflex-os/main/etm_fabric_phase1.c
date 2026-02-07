/**
 * etm_fabric_phase1.c - Phase 1: GDMA M2M to Peripheral Verification
 *
 * Tests the critical claim: GDMA can write to RMT memory at 0x60006100
 *
 * Requirements:
 *   - REQ-GDMA-01: Write test pattern via GDMA, read back and verify
 *   - REQ-GDMA-02: Paired IN/OUT channels for M2M
 *   - REQ-GDMA-03: ETM-triggerable (next phase)
 *
 * Success: 100% match on 100 test patterns
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Bare metal headers
#include "reflex_gdma.h"
#include "reflex_rmt.h"

// Test configuration
#define TEST_ITERATIONS     100
#define TEST_PATTERN_SIZE   48  // RMT words
#define GDMA_OUT_CH         0
#define GDMA_IN_CH          0   // Same channel for M2M pairing

// Test patterns
static const uint32_t test_patterns[8][TEST_PATTERN_SIZE] = {
    // Pattern 0: All zeros
    {0},
    // Pattern 1: All ones
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    // Pattern 2: Alternating
    {0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555,
     0xAAAAAAAA, 0x55555555, 0xAAAAAAAA, 0x55555555},
    // Pattern 3: Incremental
    {0x00000000, 0x00000001, 0x00000002, 0x00000003,
     0x00000004, 0x00000005, 0x00000006, 0x00000007,
     0x00000008, 0x00000009, 0x0000000A, 0x0000000B,
     0x0000000C, 0x0000000D, 0x0000000E, 0x0000000F,
     0x00000010, 0x00000011, 0x00000012, 0x00000013,
     0x00000014, 0x00000015, 0x00000016, 0x00000017,
     0x00000018, 0x00000019, 0x0000001A, 0x0000001B,
     0x0000001C, 0x0000001D, 0x0000001E, 0x0000001F,
     0x00000020, 0x00000021, 0x00000022, 0x00000023,
     0x00000024, 0x00000025, 0x00000026, 0x00000027,
     0x00000028, 0x00000029, 0x0000002A, 0x0000002B,
     0x0000002C, 0x0000002D, 0x0000002E, 0x0000002F},
    // Pattern 4: DEADBEEF
    {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x9ABCDEF0,
     0x0F1E2D3C, 0x4B5A6978, 0xAABBCCDD, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344,
     0x55667788, 0x99AABBCC, 0xDDEEFF00, 0x11223344},
    // Pattern 5: Random-ish
    {0x3F8B29C1, 0x7E4D6102, 0x9C2F853E, 0x5A1B7C4D,
     0xB8E392A7, 0xF6C4D583, 0x24A7E9F1, 0x63D5B8C2,
     0x91E7F3A4, 0xD2B8C695, 0x48F3E7A1, 0x87C5B4D2,
     0x35A9F7E3, 0x74D6B8C5, 0xC2F9E8A7, 0xA1D7B6C4,
     0x5F8E93B2, 0x9E7C61D4, 0xDC4B83A5, 0xBA2F79C6,
     0x18E5D3B7, 0x56C9F8A2, 0x94E7D6B1, 0xD3A8F5C7,
     0x21B6E9D4, 0x60F8C3A9, 0xB5E7D2F8, 0xF4C9A6B3,
     0x42D8F7E5, 0x81B6C9D4, 0xCF5E8B7A, 0xAE7D9C6B,
     0x4C8F5E3D, 0x8B6A4C7E, 0xE9D7B8A5, 0xD8B5F4E1,
     0x26C9A7F6, 0x65E4B8C9, 0xA3F7D6E4, 0xC2B8A9F5,
     0x50E6D4B3, 0x9FB7C8A6, 0xDDA8F7E5, 0xBC95E3D7,
     0x3AF8C6B4, 0x79E6B9C8, 0xB7D5A8F6, 0xF6C4E9B8,
     0x44D7B6A5, 0x83C9F8B7, 0xC1E8D7A6, 0xA0F9C8E7},
    // Pattern 6: Pulse-like (for RMT)
    {0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00050005,
     0x00050005, 0x00050005, 0x00050005, 0x00000000},
    // Pattern 7: Fibonacci-ish
    {0x00000001, 0x00000001, 0x00000002, 0x00000003,
     0x00000005, 0x00000008, 0x0000000D, 0x00000015,
     0x00000022, 0x00000037, 0x00000059, 0x00000090,
     0x000000E9, 0x00000179, 0x00000262, 0x000003DB,
     0x0000063D, 0x00000A18, 0x00001055, 0x00001A6D,
     0x00002AC2, 0x0000452F, 0x00006FF1, 0x0000B520,
     0x00012511, 0x0001DA31, 0x0002FF42, 0x0004D973,
     0x0007D8B5, 0x000CB228, 0x00148ADD, 0x00213D05,
     0x0035C7E2, 0x005704E7, 0x008CCCC9, 0x00E3D1B0,
     0x01709E79, 0x02547029, 0x03C50EA2, 0x06197ECB,
     0x09DE8D6D, 0x0FF80C38, 0x19D699A5, 0x29CEA5DD,
     0x43A53F82, 0x6D73E55F, 0xB11924E1, 0x00000000}
};

// Source SRAM buffer
static uint32_t src_buffer[TEST_PATTERN_SIZE] __attribute__((aligned(4)));

// GDMA descriptors
static gdma_descriptor_t out_desc __attribute__((aligned(4)));
static gdma_descriptor_t in_desc __attribute__((aligned(4)));

// ============================================================
// Test Functions
// ============================================================

static int test_gdma_m2m_basic(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║          TEST 1: Basic GDMA M2M Write to RMT RAM               ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    // Initialize GDMA M2M
    printf("Initializing GDMA M2M channels (OUT=%d, IN=%d)...\n", GDMA_OUT_CH, GDMA_IN_CH);
    gdma_m2m_init_peripheral(GDMA_OUT_CH, GDMA_IN_CH, 15);
    printf("  ✓ GDMA initialized\n\n");
    
    int passed = 0;
    int failed = 0;
    
    for (int test = 0; test < 8; test++) {
        // Copy pattern to source buffer
        memcpy(src_buffer, test_patterns[test], TEST_PATTERN_SIZE * 4);
        
        // Clear RMT RAM before test
        volatile uint32_t* rmt_ram = (uint32_t*)RMT_CH0_RAM_ADDR;
        for (int i = 0; i < TEST_PATTERN_SIZE; i++) {
            rmt_ram[i] = 0x00000000;
        }
        
        // Build descriptors
        gdma_m2m_out_descriptor(&out_desc, src_buffer, TEST_PATTERN_SIZE * 4, 1, NULL);
        gdma_m2m_in_descriptor(&in_desc, (void*)RMT_CH0_RAM_ADDR, TEST_PATTERN_SIZE * 4, 1, NULL);
        
        // Start transfer
        gdma_m2m_start(GDMA_OUT_CH, GDMA_IN_CH, &out_desc, &in_desc);
        
        // Wait for completion
        int timeout = 10000;
        while (!gdma_m2m_is_complete(GDMA_OUT_CH) && timeout > 0) {
            timeout--;
        }
        
        if (timeout == 0) {
            printf("  Test %d: TIMEOUT - Transfer did not complete\n", test);
            failed++;
            continue;
        }
        
        // Verify transfer
        int match = 1;
        for (int i = 0; i < TEST_PATTERN_SIZE; i++) {
            if (rmt_ram[i] != test_patterns[test][i]) {
                match = 0;
                if (failed < 3) {  // Only show first few failures
                    printf("  Test %d: MISMATCH at word %d: expected 0x%08x, got 0x%08x\n",
                           test, i, test_patterns[test][i], rmt_ram[i]);
                }
                break;
            }
        }
        
        if (match) {
            passed++;
            printf("  Test %d: PASS (pattern %d)\n", test, test);
        } else {
            failed++;
        }
    }
    
    printf("\n  Results: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? 1 : 0;
}

static int test_gdma_m2m_stress(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║          TEST 2: Stress Test (100 iterations)                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    int passed = 0;
    int failed = 0;
    int64_t total_time = 0;
    int64_t min_time = 1000000;
    int64_t max_time = 0;
    
    // Use pattern 4 (DEADBEEF) for stress test
    memcpy(src_buffer, test_patterns[4], TEST_PATTERN_SIZE * 4);
    
    for (int iter = 0; iter < TEST_ITERATIONS; iter++) {
        // Clear RMT RAM
        volatile uint32_t* rmt_ram = (uint32_t*)RMT_CH0_RAM_ADDR;
        for (int i = 0; i < TEST_PATTERN_SIZE; i++) {
            rmt_ram[i] = 0x00000000;
        }
        
        // Build descriptors
        gdma_m2m_out_descriptor(&out_desc, src_buffer, TEST_PATTERN_SIZE * 4, 1, NULL);
        gdma_m2m_in_descriptor(&in_desc, (void*)RMT_CH0_RAM_ADDR, TEST_PATTERN_SIZE * 4, 1, NULL);
        
        // Time the transfer
        int64_t start = esp_timer_get_time();
        
        // Start transfer
        gdma_m2m_start(GDMA_OUT_CH, GDMA_IN_CH, &out_desc, &in_desc);
        
        // Wait for completion
        while (!gdma_m2m_is_complete(GDMA_OUT_CH)) {
            // Spin
        }
        
        int64_t end = esp_timer_get_time();
        int64_t elapsed = end - start;
        total_time += elapsed;
        if (elapsed < min_time) min_time = elapsed;
        if (elapsed > max_time) max_time = elapsed;
        
        // Verify
        int match = 1;
        for (int i = 0; i < TEST_PATTERN_SIZE; i++) {
            if (rmt_ram[i] != test_patterns[4][i]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            passed++;
        } else {
            failed++;
            if (failed <= 3) {
                printf("  Iteration %d: FAIL\n", iter);
            }
        }
        
        // Progress every 10 iterations
        if ((iter + 1) % 10 == 0) {
            printf("  Progress: %d/%d\r", iter + 1, TEST_ITERATIONS);
            fflush(stdout);
        }
    }
    
    printf("\n\n  Results: %d passed, %d failed\n", passed, failed);
    printf("  Timing:\n");
    printf("    Min: %lld µs\n", min_time);
    printf("    Max: %lld µs\n", max_time);
    printf("    Avg: %lld µs\n", total_time / TEST_ITERATIONS);
    
    return (failed == 0) ? 1 : 0;
}

static int test_gdma_latency(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║          TEST 3: Transfer Latency Measurement                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    // Use a single pulse pattern for latency test
    src_buffer[0] = 0xDEADBEEF;
    for (int i = 1; i < TEST_PATTERN_SIZE; i++) {
        src_buffer[i] = 0x00000000;
    }
    
    // Build descriptors (transfer just 4 bytes)
    gdma_m2m_out_descriptor(&out_desc, src_buffer, 4, 1, NULL);
    gdma_m2m_in_descriptor(&in_desc, (void*)RMT_CH0_RAM_ADDR, 4, 1, NULL);
    
    // Warm up
    for (int i = 0; i < 10; i++) {
        gdma_m2m_start(GDMA_OUT_CH, GDMA_IN_CH, &out_desc, &in_desc);
        while (!gdma_m2m_is_complete(GDMA_OUT_CH)) {}
    }
    
    // Measure
    int64_t times[100];
    for (int i = 0; i < 100; i++) {
        int64_t start = esp_timer_get_time();
        gdma_m2m_start(GDMA_OUT_CH, GDMA_IN_CH, &out_desc, &in_desc);
        while (!gdma_m2m_is_complete(GDMA_OUT_CH)) {}
        int64_t end = esp_timer_get_time();
        times[i] = end - start;
    }
    
    // Stats
    int64_t total = 0;
    int64_t min = times[0];
    int64_t max = times[0];
    for (int i = 0; i < 100; i++) {
        total += times[i];
        if (times[i] < min) min = times[i];
        if (times[i] > max) max = times[i];
    }
    
    printf("  Transfer 4 bytes (1 word):\n");
    printf("    Min latency: %lld µs\n", min);
    printf("    Max latency: %lld µs\n", max);
    printf("    Avg latency: %lld µs\n", total / 100);
    printf("\n  Requirement: < 10 µs\n");
    printf("  Status: %s\n", (max < 10) ? "PASS" : "FAIL");
    
    return (max < 10) ? 1 : 0;
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║     ETM FABRIC - PHASE 1: GDMA M2M TO PERIPHERAL              ║\n");
    printf("║              Verification Test Suite                           ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Target: ESP32-C6 @ 160 MHz\n");
    printf("RMT RAM: 0x%08x\n", RMT_CH0_RAM_ADDR);
    printf("Pattern size: %d words (%d bytes)\n", TEST_PATTERN_SIZE, TEST_PATTERN_SIZE * 4);
    printf("\n");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    int results[3];
    
    results[0] = test_gdma_m2m_basic();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    results[1] = test_gdma_m2m_stress();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    results[2] = test_gdma_latency();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Final report
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    PHASE 1 FINAL REPORT                        ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  REQ-GDMA-01: Write pattern, read back .......... %s        ║\n", 
           results[0] ? "✓ PASS" : "✗ FAIL");
    printf("║  REQ-GDMA-02: Paired IN/OUT channels ............ ✓ IMPL     ║\n");
    printf("║  REQ-GDMA-03: ETM triggerable ................... ○ TODO      ║\n");
    printf("║                                                                ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  Phase 1 Status: %s                                    ║\n",
           (results[0] && results[1] && results[2]) ? "✓ COMPLETE" : "✗ INCOMPLETE");
    printf("║                                                                ║\n");
    printf("║  Next: Phase 2 - Timer Race Implementation                     ║\n");
    printf("║                                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
