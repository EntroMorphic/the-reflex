/**
 * falsify_gdma_v2.c - GDMA M2M Test with Corrected Register Addresses
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "reflex_gdma.h"

#define TEST_PATTERN    0xDEADBEEF
#define GDMA_CH         0

static uint32_t src_buf __attribute__((aligned(4))) = TEST_PATTERN;
static uint32_t dst_buf __attribute__((aligned(4))) = 0;
static gdma_descriptor_t out_desc __attribute__((aligned(4)));
static gdma_descriptor_t in_desc __attribute__((aligned(4)));

void app_main(void)
{
    printf("\n");
    printf("GDMA M2M TEST V2 - Corrected Register Addresses\n");
    printf("================================================\n");
    printf("\n");
    
    printf("STEP 1: Enable GDMA Clock\n");
    printf("  GDMA_MISC_CONF before: 0x%08lx\n", (unsigned long)GDMA_REG(GDMA_MISC_CONF));
    gdma_enable_clock();
    printf("  GDMA_MISC_CONF after: 0x%08lx\n", (unsigned long)GDMA_REG(GDMA_MISC_CONF));
    printf("  Clock enabled: %s\n", 
           (GDMA_REG(GDMA_MISC_CONF) & GDMA_MISC_CLK_EN) ? "YES" : "NO");
    printf("\n");
    
    printf("STEP 2: Initialize GDMA Channel %d for M2M\n", GDMA_CH);
    gdma_m2m_init(GDMA_CH);
    printf("  Channel initialized\n");
    printf("  OUT_CONF0: 0x%08lx\n", (unsigned long)GDMA_REG(GDMA_OUT_CONF0_CH(GDMA_CH)));
    printf("  IN_CONF0:  0x%08lx\n", (unsigned long)GDMA_REG(GDMA_IN_CONF0_CH(GDMA_CH)));
    printf("\n");
    
    printf("STEP 3: Build Descriptors\n");
    printf("  Source: 0x%08x = 0x%08lx\n", (unsigned)&src_buf, (unsigned long)src_buf);
    printf("  Dest:   0x%08x = 0x%08lx\n", (unsigned)&dst_buf, (unsigned long)dst_buf);
    
    out_desc.dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF;
    out_desc.buffer = &src_buf;
    out_desc.next = NULL;
    
    in_desc.dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF;
    in_desc.buffer = &dst_buf;
    in_desc.next = NULL;
    
    printf("  OUT desc: dw0=0x%08lx, buf=0x%08x\n", (unsigned long)out_desc.dw0, (unsigned)out_desc.buffer);
    printf("  IN desc:  dw0=0x%08lx, buf=0x%08x\n", (unsigned long)in_desc.dw0, (unsigned)in_desc.buffer);
    printf("\n");
    
    printf("STEP 4: Start Transfer\n");
    int64_t start = esp_timer_get_time();
    gdma_m2m_start(GDMA_CH, &out_desc, &in_desc);
    printf("  Transfer started\n");
    printf("  OUT_LINK: 0x%08lx\n", (unsigned long)GDMA_REG(GDMA_OUT_LINK_CH(GDMA_CH)));
    printf("  IN_LINK:  0x%08lx\n", (unsigned long)GDMA_REG(GDMA_IN_LINK_CH(GDMA_CH)));
    printf("\n");
    
    printf("STEP 5: Wait for Completion\n");
    int timeout = 10000;
    while (timeout > 0) {
        if (gdma_m2m_done(GDMA_CH)) {
            break;
        }
        timeout--;
    }
    int64_t end = esp_timer_get_time();
    
    printf("  Time: %lld us\n", end - start);
    printf("  Timeout remaining: %d\n", timeout);
    printf("  OUT_STATE: 0x%08lx\n", (unsigned long)GDMA_REG(GDMA_OUT_STATE_CH(GDMA_CH)));
    printf("  IN_STATE:  0x%08lx\n", (unsigned long)GDMA_REG(GDMA_IN_STATE_CH(GDMA_CH)));
    printf("\n");
    
    printf("STEP 6: Check Result\n");
    printf("  dst_buf = 0x%08lx (expected 0x%08x)\n", (unsigned long)dst_buf, TEST_PATTERN);
    printf("\n");
    
    printf("================================================\n");
    if (timeout == 0) {
        printf("RESULT: TIMEOUT - Transfer did not complete\n");
    } else if (dst_buf == TEST_PATTERN) {
        printf("RESULT: SUCCESS! GDMA M2M works!\n");
    } else {
        printf("RESULT: FAIL - Data not transferred\n");
    }
    printf("================================================\n");
    printf("\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
