// Test GDMA using ESP-IDF HAL instead of bare-metal
// This should reveal what we're missing

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_private/gdma.h"
#include "../include/reflex_gdma.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// Test using ESP-IDF API
// ============================================================

void test_esp_idf_gdma(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║       ESP-IDF GDMA TEST                                   ║\n");
    printf("╠═══════════════════════════════════════════════════════╣\n");
    printf("║                                                                ║\n");
    printf("║  Using ESP-IDF gdma_connect() API                    ║\n");
    printf("║                                                                ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    
    // Step 1: Allocate GDMA channel
    printf("\n1. Allocating GDMA channels...\n");
    
    gdma_channel_handle_t tx_chan = NULL;
    gdma_channel_handle_t rx_chan = NULL;
    
    gdma_channel_alloc_config_t tx_alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_TX,
        .sibling_chan = NULL,
    };
    
    gdma_channel_alloc_config_t rx_alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_RX,
        .sibling_chan = &tx_chan,
    };
    
    esp_err_t err = gdma_new_ahb_channel(&tx_alloc_config, &tx_chan);
    if (err != ESP_OK) {
        printf("   ERROR: Failed to allocate TX channel: %s\n", esp_err_to_name(err));
        return;
    }
    printf("   TX channel allocated\n");
    
    err = gdma_new_ahb_channel(&rx_alloc_config, &rx_chan);
    if (err != ESP_OK) {
        printf("   ERROR: Failed to allocate RX channel: %s\n", esp_err_to_name(err));
        return;
    }
    printf("   RX channel allocated\n");
    
    // Step 2: Configure channels for M2M transfer
    printf("\n2. Configuring M2M transfer...\n");
    
    gdma_trigger_t m2m_trigger = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M, 0);
    printf("   M2M trigger: periph=%d, instance=%d, bus=%d\n",
           m2m_trigger.periph, m2m_trigger.instance_id, m2m_trigger.bus_id);
    
    // Connect TX to M2M
    err = gdma_connect(tx_chan, m2m_trigger);
    if (err != ESP_OK) {
        printf("   ERROR: Failed to connect TX: %s\n", esp_err_to_name(err));
        return;
    }
    printf("   TX connected to M2M\n");
    
    // Connect RX to M2M
    err = gdma_connect(rx_chan, m2m_trigger);
    if (err != ESP_OK) {
        printf("   ERROR: Failed to connect RX: %s\n", esp_err_to_name(err));
        return;
    }
    printf("   RX connected to M2M\n");
    
    // Step 3: Prepare buffers
    printf("\n3. Preparing buffers...\n");
    
    static uint32_t test_src __attribute__((aligned(4))) = 0xDEADBEEF;
    static uint32_t test_dst __attribute__((aligned(4))) = 0;
    
    printf("   Source: 0x%08x = 0x%08lx\n", (uint32_t)&test_src, (unsigned long)test_src);
    printf("   Dest:   0x%08x = 0x%08lx\n", (uint32_t)&test_dst, (unsigned long)test_dst);
    
    // Step 4: Create descriptors
    printf("\n4. Creating descriptors...\n");
    
    gdma_descriptor_t tx_desc = {
        .dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF,
        .buffer = &test_src,
        .next = NULL,
    };
    
    gdma_descriptor_t rx_desc = {
        .dw0 = GDMA_DW0_SIZE(4) | GDMA_DW0_LENGTH(4) | GDMA_DW0_OWNER_DMA | GDMA_DW0_SUC_EOF,
        .buffer = &test_dst,
        .next = NULL,
    };
    
    printf("   TX descriptor created\n");
    printf("   RX descriptor created\n");
    
    // Step 5: Start transfer
    printf("\n5. Starting transfer...\n");
    
    int64_t test_start = esp_timer_get_time();
    
    err = gdma_start(tx_chan, (intptr_t)&tx_desc);
    if (err != ESP_OK) {
        printf("   ERROR: Failed to start TX: %s\n", esp_err_to_name(err));
        return;
    }
    printf("   TX channel started\n");
    
    err = gdma_start(rx_chan, (intptr_t)&rx_desc);
    if (err != ESP_OK) {
        printf("   ERROR: Failed to start RX: %s\n", esp_err_to_name(err));
        return;
    }
    printf("   RX channel started\n");
    
    // Step 6: Wait for completion
    printf("\n6. Waiting for completion (10s timeout)...\n");
    
    int timeout = 1000;
    int completed = 0;
    
    while (timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Check if descriptors show completion
        if ((tx_desc.dw0 & GDMA_DW0_OWNER_DMA) == 0 && 
            (rx_desc.dw0 & GDMA_DW0_OWNER_DMA) == 0) {
            completed = 1;
            break;
        }
        
        timeout--;
    }
    
    int64_t test_end = esp_timer_get_time();
    
    printf("   Time: %lld us, timeout: %d\n", test_end - test_start, timeout);
    
    // Step 7: Check result
    printf("\n7. Check Result\n");
    printf("   dst = 0x%08lx (expected 0xDEADBEEF)\n", (unsigned long)test_dst);
    
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    if (completed) {
        if (test_dst == 0xDEADBEEF) {
            printf("║  ESP-IDF GDMA RESULT: SUCCESS - M2M Transfer Works!              ║\n");
        } else {
            printf("║  ESP-IDF GDMA RESULT: FAIL - Data mismatch (0x%08lx)            ║\n", (unsigned long)test_dst);
        }
    } else {
        printf("║  ESP-IDF GDMA RESULT: TIMEOUT                                      ║\n");
    }
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    
    // Cleanup
    gdma_stop(tx_chan);
    gdma_stop(rx_chan);
    gdma_disconnect(tx_chan);
    gdma_disconnect(rx_chan);
}
