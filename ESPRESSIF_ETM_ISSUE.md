# ESP32-C6 ETM Not Working - GitHub Issue Template

## Issue Title
ETM (Event Task Matrix) GPIO toggle task does not execute on ESP32-C6 (revision v0.2)

---

## Environment

- **ESP-IDF Version:** v5.4
- **Chip:** ESP32-C6FH4 (QFN32) revision v0.2
- **Crystal:** 40MHz
- **USB Mode:** USB-Serial/JTAG
- **Platform:** Linux (kernel with cdc_acm driver)

## Problem Description

The ETM peripheral fails to execute GPIO toggle tasks when connected to either:
1. Timer alarm events (TIMER0 alarm -> GPIO toggle)
2. GPIO edge events (GPIO0 rising edge -> GPIO8 toggle)

CPU interrupts work correctly (timer alarm callback executes). GPIO hardware works (direct register writes toggle pins). All visible ETM register configurations appear correct. But ETM task execution never occurs.

## Minimal Reproduction Code

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gpio_etm.h"
#include "esp_etm.h"

void app_main(void)
{
    // Configure input GPIO (GPIO0)
    gpio_config_t input_cfg = {
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = 1ULL << 0,
    };
    gpio_config(&input_cfg);
    gpio_set_level(0, 0);
    
    // Configure output GPIO (GPIO8)
    gpio_config_t output_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << 8,
    };
    gpio_config(&output_cfg);
    
    // Allocate ETM channel
    esp_etm_channel_config_t etm_config = {};
    esp_etm_channel_handle_t etm_channel;
    esp_etm_new_channel(&etm_config, &etm_channel);
    
    // Create GPIO rising edge event on GPIO0
    esp_etm_event_handle_t gpio_event;
    gpio_etm_event_config_t event_cfg = {
        .edge = GPIO_ETM_EVENT_EDGE_POS,
    };
    gpio_new_etm_event(&event_cfg, &gpio_event);
    gpio_etm_event_bind_gpio(gpio_event, 0);
    
    // Create GPIO toggle task on GPIO8
    esp_etm_task_handle_t gpio_task;
    gpio_etm_task_config_t task_cfg = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    gpio_new_etm_task(&task_cfg, &gpio_task);
    gpio_etm_task_add_gpio(gpio_task, 8);
    
    // Connect and enable
    esp_etm_channel_connect(etm_channel, gpio_event, gpio_task);
    esp_etm_channel_enable(etm_channel);
    
    // Generate edges - GPIO8 should toggle but doesn't
    for (int i = 0; i < 10; i++) {
        gpio_set_level(0, 1);  // Rising edge
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        int out = gpio_get_level(8);
        printf("After edge %d: GPIO8 = %d\n", i, out);
    }
    // Result: GPIO8 never changes
}
```

## Register Dump (All Show Correct Values)

```
ETM_CH_ENA_AD0   = 0x00000001  (channel 0 enabled)
ETM_CH0_EVT_ID   = 1           (GPIO_EVT_CH0_ANY_EDGE)
ETM_CH0_TASK_ID  = 17          (GPIO_TASK_CH0_TOGGLE)

GPIO_ETM_EVENT_CH0_CFG:
  sel = 0 (GPIO0)
  en = 1 (enabled)

GPIO_ETM_TASK_P2_CFG (GPIO8-11):
  GPIO8: en = 1, ch = 0

PCR_SOC_ETM_CONF = 0x00000001  (ETM clock enabled)
```

## Additional Findings

### Hidden Enable Bits Discovered (But Insufficient)

1. **TIMG0_REGCLK bit 28 (etm_en):** Found to be 0 after driver initialization. Manually setting to 1 does not fix the issue.

2. **SOC_ETM.clk_en at 0x600131A8:** Found to be 0. Setting to 1 is reset by `esp_etm_new_channel()`. Even setting it after channel allocation does not fix the issue.

### What Works

- Timer alarm CPU interrupts fire correctly
- GPIO output works via direct register writes (W1TS/W1TC)
- ETM register reads/writes succeed
- All API calls return ESP_OK

### What Doesn't Work

- ETM task execution (GPIO never toggles)
- Verified with both timer events and GPIO events
- Problem is not event-source-specific

## Questions for Espressif

1. **Are there known errata for ETM on ESP32-C6 revision v0.2?**

2. **Is there an undocumented clock enable for the GPIO_EXT peripheral?** We found hidden enables for Timer ETM output and ETM internal clock. GPIO_EXT may have one too.

3. **Has ETM -> GPIO functionality been verified on this chip revision?** The ESP-IDF test code comments suggest manual oscilloscope verification ("delay sometime for us to view the waveform"), not programmatic verification.

4. **Is there a way to probe the ETM event bus?** We need to determine if events are being generated but tasks not executed, or if events themselves are not reaching the ETM.

## Suspected Root Cause

We hypothesize there's a missing enable bit somewhere in the signal path:
- Event Generation -> **??? missing gate ???** -> ETM Routing -> **??? missing gate ???** -> Task Execution

The failure point is invisible in documented registers.

## Files

- Minimal reproduction: attached above
- Full test with PCR register scanning: available on request
- Register dumps: shown above

## Project Context

This is part of "The Reflex" project exploring Turing-complete autonomous hardware computation using ETM + PCNT + GDMA + PARLIO while CPU sleeps.

Repository: https://github.com/EntroMorphic/the-reflex

---

## Additional Notes

The CDC-ACM serial driver on our Linux system has issues with RTS control (BrokenPipeError when setting ser.rts), which complicated testing. However, we were able to:
1. Successfully flash firmware (verified by hash)
2. Verify the chip responds to esptool in bootloader mode
3. Confirm the console is configured for USB-Serial-JTAG

The ETM issue is separate from these serial driver issues.
