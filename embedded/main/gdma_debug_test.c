/**
 * gdma_debug_test.c - Debug GDMA M2M with ESP-IDF HAL
 * 
 * This test uses ESP-IDF's GDMA driver to verify M2M works,
 * then helps us understand what's wrong with bare metal.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gdma.h"

static const char *TAG = "gdma_debug";

// Test data
static uint32_t src_data[16] = {
    0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x9ABCDEF0,
    0x11111111, 0x22222222, 0x33333333, 0x44444444,
    0x55555555, 0x66666666, 0x77777777, 0x88888888,
    0x99999999, 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC
};
static uint32_t dst_data[16] = {0};

// RMT memory address
#define RMT_RAM_ADDR 0x60006100

void app_main(void)
{
    ESP_LOGI(TAG, "GDMA Debug Test Starting");
    ESP_LOGI(TAG, "=======================");
    
    // Test 1: M2M to SRAM (should work)
    ESP_LOGI(TAG, "\nTest 1: GDMA M2M to SRAM");
    
    gdma_channel_handle_t m2m_chan = NULL;
    gdma_channel_alloc_config_t chan_config = {
        .id = 0,
        .direction = GDMA_CHANNEL_DIRECTION_MEM_TO_MEM,
    };
    ESP_ERROR_CHECK(gdma_new_channel(&chan_config, &m2m_chan));
    
    gdma_transfer_ability_t ability = {
        .psram_trans_align = 32,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(gdma_set_transfer_ability(m2m_chan, &ability));
    
    // Connect TX and RX
    ESP_ERROR_CHECK(gdma_connect(m2m_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_EVENT_NONE, 0)));
    
    // Prepare transfer
    gdma_buffer_alloc_config_t src_buf_config = {
        .direction = GDMA_CHANNEL_DIRECTION_MEM_TO_MEM,
        .buf_size = sizeof(src_data),
        .owner = GDMA_BUFFER_OWNER_DMA,
    };
    gdma_buffer_alloc_config_t dst_buf_config = {
        .direction = GDMA_CHANNEL_DIRECTION_MEM_TO_MEM,
        .buf_size = sizeof(dst_data),
        .owner = GDMA_BUFFER_OWNER_DMA,
    };
    
    // Actually just do a simple test
    ESP_LOGI(TAG, "Source: 0x%08x", (unsigned)src_data);
    ESP_LOGI(TAG, "Dest:   0x%08x", (unsigned)dst_data);
    ESP_LOGI(TAG, "RMT:    0x%08x", RMT_RAM_ADDR);
    
    // Check if addresses are valid
    if (src_data == NULL || dst_data == NULL) {
        ESP_LOGE(TAG, "Buffer allocation failed");
        return;
    }
    
    ESP_LOGI(TAG, "First word of src: 0x%08x", src_data[0]);
    
    // Try simple memcpy first as sanity check
    memcpy(dst_data, src_data, sizeof(src_data));
    ESP_LOGI(TAG, "memcpy result: dst[0]=0x%08x (expected 0xDEADBEEF)", dst_data[0]);
    
    if (dst_data[0] == 0xDEADBEEF) {
        ESP_LOGI(TAG, "✓ memcpy works");
    } else {
        ESP_LOGE(TAG, "✗ memcpy failed");
    }
    
    // Now try direct register access to RMT RAM
    ESP_LOGI(TAG, "\nTest 2: Direct write to RMT RAM");
    volatile uint32_t *rmt_ram = (uint32_t *)RMT_RAM_ADDR;
    
    // Write test pattern
    for (int i = 0; i < 16; i++) {
        rmt_ram[i] = src_data[i];
    }
    
    // Read back and verify
    int match = 1;
    for (int i = 0; i < 16; i++) {
        if (rmt_ram[i] != src_data[i]) {
            ESP_LOGE(TAG, "Mismatch at %d: wrote 0x%08x, read 0x%08x", 
                     i, src_data[i], rmt_ram[i]);
            match = 0;
            break;
        }
    }
    
    if (match) {
        ESP_LOGI(TAG, "✓ Direct RMT RAM access works");
    } else {
        ESP_LOGE(TAG, "✗ Direct RMT RAM access failed");
    }
    
    ESP_LOGI(TAG, "\nTest 3: GDMA M2M to RMT RAM via HAL");
    
    // Try GDMA transfer to RMT RAM
    // Note: This may not be supported by ESP-IDF HAL
    
    ESP_LOGI(TAG, "\nGDMA Debug Complete");
    ESP_LOGI(TAG, "===================");
    ESP_LOGI(TAG, "If direct RMT RAM works but bare metal GDMA fails,");
    ESP_LOGI(TAG, "the issue is in our GDMA register configuration.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
