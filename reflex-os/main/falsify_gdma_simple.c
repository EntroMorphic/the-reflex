/**
 * falsify_gdma_simple.c - Minimal GDMA M2M Test
 * 
 * Tests GDMA in 2 stages:
 * 1. SRAM to SRAM (should definitely work)
 * 2. SRAM to RMT RAM (the claim)
 * 
 * If stage 1 fails, our GDMA implementation is fundamentally wrong.
 * If stage 1 passes but stage 2 fails, peripheral access is the issue.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Bare metal headers
#include "reflex_gdma.h"

#define TEST_PATTERN    0xDEADBEEF
#define GDMA_CH         0

static uint32_t src_buf __attribute__((aligned(4))) = TEST_PATTERN;
static uint32_t dst_buf __attribute__((aligned(4))) = 0;
static gdma_descriptor_t desc __attribute__((aligned(4)));

// Test 1: Simplest possible GDMA transfer (SRAM to SRAM)
static int test_sram_to_sram(void) {
    printf("\n=== TEST 1: GDMA M2M SRAM to SRAM ===\n");
    printf("Source: 0x%08x = 0x%08lx\n", (unsigned)&src_buf, (unsigned long)src_buf);
    printf("Dest:   0x%08x = 0x%08lx\n", (unsigned)&dst_buf, (unsigned long)dst_buf);
    
    // Clear destination
    dst_buf = 0;
    
    // Initialize GDMA channel 0 for M2M
    printf("\n1. Initializing GDMA...\n");
    gdma_tx_reset(GDMA_CH);
    
    uint32_t conf0 = GDMA_OUT_ETM_EN | GDMA_OUT_EOF_MODE;
    GDMA_REG(GDMA_OUT_CONF0_CH(GDMA_CH)) = conf0;
    GDMA_REG(GDMA_OUT_PERI_SEL_CH(GDMA_CH)) = GDMA_PERI_SEL_M2M;
    GDMA_REG(GDMA_OUT_PRI_CH(GDMA_CH)) = 15;
    printf("   Configured CH%d for M2M, priority 15\n", GDMA_CH);
    
    // Build descriptor
    printf("2. Building descriptor...\n");
    desc.dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF;
    desc.buffer = &src_buf;
    desc.next = NULL;
    printf("   dw0: 0x%08lx, buffer: 0x%08x\n", (unsigned long)desc.dw0, (unsigned)desc.buffer);
    
    // Set descriptor address
    printf("3. Setting descriptor...\n");
    uint32_t addr = ((uint32_t)&desc) & GDMA_OUTLINK_ADDR_MASK;
    GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) = addr;
    printf("   Descriptor at: 0x%08x (masked: 0x%08lx)\n", (unsigned)&desc, (unsigned long)addr);
    
    // Read back to verify
    uint32_t readback = GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) & GDMA_OUTLINK_ADDR_MASK;
    printf("   Read back: 0x%08lx (should match)\n", (unsigned long)readback);
    if (readback != addr) {
        printf("   ERROR: Register write failed!\n");
        return 0;
    }
    
    // Start transfer
    printf("4. Starting transfer...\n");
    int64_t start = esp_timer_get_time();
    GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) |= GDMA_OUTLINK_START;
    
    // Wait for completion
    printf("5. Waiting for completion...\n");
    int timeout = 10000;
    while (timeout > 0) {
        if (GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) & GDMA_OUTLINK_PARK) {
            break;
        }
        timeout--;
    }
    int64_t end = esp_timer_get_time();
    
    printf("   Time: %lld us, timeout remaining: %d\n", end - start, timeout);
    
    if (timeout == 0) {
        printf("   ERROR: Timeout! Transfer did not complete.\n");
        return 0;
    }
    
    // For M2M, data should be in dst_buf via IN channel
    // But wait - in M2M mode, where does the data go?
    printf("6. Checking result...\n");
    printf("   dst_buf = 0x%08lx (expected 0x%08x)\n", (unsigned long)dst_buf, TEST_PATTERN);
    
    if (dst_buf == TEST_PATTERN) {
        printf("   SUCCESS! GDMA M2M works for SRAM to SRAM.\n");
        return 1;
    } else {
        printf("   FAIL: Data not transferred.\n");
        printf("   Note: In M2M mode, need to check IN channel status too.\n");
        return 0;
    }
}

// Test 2: SRAM to peripheral (RMT RAM)
static int test_sram_to_peripheral(void) {
    printf("\n=== TEST 2: GDMA M2M SRAM to Peripheral ===\n");
    printf("Target: 0x60006100 (RMT RAM)\n");
    
    // Clear RMT RAM
    volatile uint32_t* rmt_ram = (uint32_t*)0x60006100;
    *rmt_ram = 0;
    
    // Reset GDMA
    gdma_tx_reset(GDMA_CH);
    
    // Configure for M2M
    uint32_t conf0 = GDMA_OUT_ETM_EN | GDMA_OUT_EOF_MODE;
    GDMA_REG(GDMA_OUT_CONF0_CH(GDMA_CH)) = conf0;
    GDMA_REG(GDMA_OUT_PERI_SEL_CH(GDMA_CH)) = GDMA_PERI_SEL_M2M;
    GDMA_REG(GDMA_OUT_PRI_CH(GDMA_CH)) = 15;
    
    // Build descriptor pointing to RMT RAM
    // NOTE: In M2M mode, the data goes to the IN channel's buffer
    // The OUT channel reads from src_buf and pushes to FIFO
    // The IN channel (same CH) pops from FIFO and writes to its buffer
    desc.dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF;
    desc.buffer = (void*)0x60006100;  // IN channel writes here!
    desc.next = NULL;
    
    printf("Descriptor: buffer = 0x%08x (RMT RAM)\n", (unsigned)desc.buffer);
    
    // Set OUT descriptor (source)
    gdma_descriptor_t out_desc;
    out_desc.dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF;
    out_desc.buffer = &src_buf;
    out_desc.next = NULL;
    
    uint32_t out_addr = ((uint32_t)&out_desc) & GDMA_OUTLINK_ADDR_MASK;
    GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) = out_addr;
    
    // Set IN descriptor (destination)
    uint32_t in_addr = ((uint32_t)&desc) & GDMA_INLINK_ADDR_MASK;
    GDMA_REG(GDMA_IN_LINK_CH(GDMA_CH)) = in_addr;
    
    // Enable IN channel EOF interrupt
    GDMA_REG(GDMA_IN_INT_ENA_CH(GDMA_CH)) = GDMA_IN_INT_EOF;
    
    // Start IN first
    GDMA_REG(GDMA_IN_LINK_CH(GDMA_CH)) |= GDMA_INLINK_START;
    // Then OUT
    GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) |= GDMA_OUTLINK_START;
    
    // Wait
    int timeout = 10000;
    while (timeout > 0) {
        // Check both OUT and IN are idle
        int out_idle = (GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)) & GDMA_OUTLINK_PARK) != 0;
        int in_idle = (GDMA_REG(GDMA_IN_LINK_CH(GDMA_CH)) & GDMA_INLINK_PARK) != 0;
        if (out_idle && in_idle) break;
        timeout--;
    }
    
    if (timeout == 0) {
        printf("ERROR: Timeout!\n");
        return 0;
    }
    
    // Check result
    printf("RMT RAM = 0x%08lx (expected 0x%08x)\n", (unsigned long)*rmt_ram, TEST_PATTERN);
    
    if (*rmt_ram == TEST_PATTERN) {
        printf("SUCCESS! GDMA can write to peripheral.\n");
        return 1;
    } else {
        printf("FAIL: Peripheral write did not work.\n");
        return 0;
    }
}

void app_main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║       GDMA M2M DEBUG - Minimal Test Suite                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    src_buf = TEST_PATTERN;
    
    int test1 = test_sram_to_sram();
    int test2 = test_sram_to_peripheral();
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    FINAL RESULTS                           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  SRAM to SRAM:        %s                             ║\n", test1 ? "✓ PASS" : "✗ FAIL");
    printf("║  SRAM to Peripheral:  %s                             ║\n", test2 ? "✓ PASS" : "✗ FAIL");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    if (test1 && test2) {
        printf("║  Status: ALL TESTS PASSED - GDMA M2M WORKS!               ║\n");
    } else if (!test1) {
        printf("║  Status: FUNDAMENTAL GDMA ISSUE - Implementation broken   ║\n");
    } else {
        printf("║  Status: Peripheral access issue - Check address/mode     ║\n");
    }
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
