/**
 * single_ledc_etm_test.c - Simple ETM→LEDC Test
 *
 * Minimal test to verify ETM can pause/resume LEDC timer.
 *
 * Setup:
 *   - LEDC Timer 0 generates pulses on GPIO 4
 *   - PCNT watches GPIO 4, counts pulses
 *   - At PCNT HIGH LIMIT (32): ETM pauses LEDC Timer 0
 *   - At timer alarm (500ms): ETM resumes LEDC Timer 0, resets PCNT
 *
 * Key insight: Using PCNT HIGH LIMIT event (46) instead of threshold event (45)
 * because high limit is simpler - fires when count reaches the limit and
 * automatically resets the counter.
 *
 * Expected behavior:
 *   1. PCNT counts up quickly (10kHz = ~3.2ms to reach 32)
 *   2. ETM pauses LEDC → PCNT stops at ~32
 *   3. PCNT stays at ~32 until timer alarm
 *   4. Timer alarm resets PCNT, resumes LEDC → cycle repeats
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/gptimer.h"
#include "driver/gptimer_etm.h"
#include "driver/gpio.h"
#include "driver/gpio_etm.h"
#include "esp_etm.h"
#include "esp_check.h"

static const char *TAG = "ETM_TEST";

// ============================================================
// Register Definitions
// ============================================================

#define ETM_BASE                    0x60013000
#define ETM_CH_ENA_SET_REG          (ETM_BASE + 0x04)
#define ETM_CH_ENA_CLR_REG          (ETM_BASE + 0x08)
#define ETM_CH_EVT_ID_REG(n)        (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n)       (ETM_BASE + 0x1C + (n) * 8)
#define ETM_REG(addr)               (*(volatile uint32_t*)(addr))

#define PCR_BASE                    0x60096000
#define PCR_SOC_ETM_CONF            (PCR_BASE + 0x98)

#define LEDC_BASE                   0x60007000
#define LEDC_EVT_TASK_EN1_REG       (LEDC_BASE + 0x1a4)

// ETM Event/Task IDs (from soc_etm_source.h)
#define PCNT_EVT_CNT_EQ_THRESH      45  // Watch point (threshold) event
#define PCNT_EVT_CNT_EQ_LMT         46  // High/Low limit event  
#define PCNT_EVT_CNT_EQ_ZERO        47  // Zero crossing event
#define PCNT_TASK_CNT_RST           87  // Reset counter task
#define LEDC_TASK_TIMER0_PAUSE      61
#define LEDC_TASK_TIMER0_RESUME     57
#define TIMER0_EVT_CNT_CMP          48

// PCNT base address for register access (ESP32-C6)
#define PCNT_BASE                   0x60012000
#define PCNT_U0_CONF0_REG           (PCNT_BASE + 0x00)
#define PCNT_U0_CONF1_REG           (PCNT_BASE + 0x04)
#define PCNT_U0_CONF2_REG           (PCNT_BASE + 0x08)
#define PCNT_U0_CNT_REG             (PCNT_BASE + 0x30)
#define PCNT_INT_RAW_REG            (PCNT_BASE + 0x40)
#define PCNT_U0_STATUS_REG          (PCNT_BASE + 0x50)

// Configuration
#define GPIO_OUT            4
#define GPIO_INDICATOR      5      // GPIO to toggle when PCNT event fires
#define LEDC_FREQ_HZ        1000   // 1kHz - slower so we can see what happens
#define PCNT_HIGH_LIMIT     32     // Use this as the trigger point
#define PCNT_LOW_LIMIT      -100   // Must be negative per PCNT requirements
#define TIMER_PERIOD_US     500000 // 500ms

// GPIO ETM registers (ESP32-C6)
// GPIO_ETM struct is at 0x60091f60 (from peripherals.ld)
// etm_task_pn_cfg is at offset 0x40 within the struct
#define GPIO_ETM_BASE               0x60091f60
#define GPIO_ETM_TASK_P0_CFG_REG    (GPIO_ETM_BASE + 0x40)  // GPIOs 0-3
#define GPIO_ETM_TASK_P1_CFG_REG    (GPIO_ETM_BASE + 0x44)  // GPIOs 4-7

// Handles
static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static gptimer_handle_t timer = NULL;

// ============================================================
// Setup Functions
// ============================================================

static void enable_clocks(void) {
    // Enable ETM clock
    volatile uint32_t *etm_conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    uint32_t before = *etm_conf;
    *etm_conf &= ~(1 << 1);  // Clear reset
    *etm_conf |= (1 << 0);   // Enable clock
    uint32_t after = *etm_conf;
    printf("ETM PCR conf: 0x%08lx -> 0x%08lx (addr=0x%08lx)\n", 
           (unsigned long)before, (unsigned long)after, (unsigned long)PCR_SOC_ETM_CONF);
}

static void enable_ledc_etm_tasks(void) {
    volatile uint32_t *evt_task_en1 = (volatile uint32_t*)LEDC_EVT_TASK_EN1_REG;
    uint32_t before = *evt_task_en1;
    
    // Enable timer0 pause/resume ETM task (bit 28)
    *evt_task_en1 |= (1 << 28);
    
    uint32_t after = *evt_task_en1;
    printf("LEDC evt_task_en1: 0x%08lx -> 0x%08lx\n", (unsigned long)before, (unsigned long)after);
}

static esp_err_t setup_ledc(void) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "Timer config failed");
    
    ledc_channel_config_t chan_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = GPIO_OUT,
        .duty = 128,  // 50% duty
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_cfg), TAG, "Channel config failed");
    
    printf("LEDC: %d Hz on GPIO %d\n", LEDC_FREQ_HZ, GPIO_OUT);
    return ESP_OK;
}

static void dump_pcnt_regs(const char *label) {
    printf("\n=== PCNT Registers (%s) ===\n", label);
    printf("  CONF0: 0x%08lx (bits 11-15 = event enables)\n", 
           (unsigned long)*(volatile uint32_t*)PCNT_U0_CONF0_REG);
    printf("  CONF1: 0x%08lx (thres0/thres1 values)\n", 
           (unsigned long)*(volatile uint32_t*)PCNT_U0_CONF1_REG);
    printf("  CONF2: 0x%08lx (high/low limits)\n", 
           (unsigned long)*(volatile uint32_t*)PCNT_U0_CONF2_REG);
    printf("  CNT:   0x%08lx\n", 
           (unsigned long)*(volatile uint32_t*)PCNT_U0_CNT_REG);
    printf("  INT_RAW: 0x%08lx\n", 
           (unsigned long)*(volatile uint32_t*)PCNT_INT_RAW_REG);
    printf("  STATUS: 0x%08lx\n", 
           (unsigned long)*(volatile uint32_t*)PCNT_U0_STATUS_REG);
    
    // Decode CONF0 event enable bits
    uint32_t conf0 = *(volatile uint32_t*)PCNT_U0_CONF0_REG;
    printf("  Event enables: zero=%d h_lim=%d l_lim=%d thres0=%d thres1=%d\n",
           (int)((conf0 >> 11) & 1), (int)((conf0 >> 12) & 1), (int)((conf0 >> 13) & 1),
           (int)((conf0 >> 14) & 1), (int)((conf0 >> 15) & 1));
}

static esp_err_t setup_pcnt(void) {
    pcnt_unit_config_t unit_cfg = {
        .low_limit = PCNT_LOW_LIMIT,
        .high_limit = PCNT_HIGH_LIMIT,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &pcnt_unit), TAG, "PCNT unit failed");
    
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = GPIO_OUT,
        .level_gpio_num = -1,
        .flags.io_loop_back = true,  // Enable internal loopback for same GPIO
    };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(pcnt_unit, &chan_cfg, &pcnt_chan), TAG, "PCNT chan failed");
    
    pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    // Disable glitch filter to ensure we catch all pulses
    pcnt_unit_set_glitch_filter(pcnt_unit, NULL);
    
    // Add watch point for HIGH LIMIT - this fires when count reaches high_limit
    // The high limit event (46) should be more reliable than threshold event (45)
    pcnt_unit_add_watch_point(pcnt_unit, PCNT_HIGH_LIMIT);
    
    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_start(pcnt_unit);
    
    dump_pcnt_regs("After PCNT setup");
    
    printf("PCNT: watching GPIO %d, high_limit=%d\n", GPIO_OUT, PCNT_HIGH_LIMIT);
    return ESP_OK;
}

static esp_err_t setup_timer(void) {
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&cfg, &timer), TAG, "Timer failed");
    
    gptimer_alarm_config_t alarm = {
        .alarm_count = TIMER_PERIOD_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action(timer, &alarm);
    gptimer_enable(timer);
    
    printf("Timer: %d us period\n", TIMER_PERIOD_US);
    return ESP_OK;
}

static void setup_etm_wiring(void) {
    printf("\nWiring ETM channels (base=0x%08lx):\n", (unsigned long)ETM_BASE);
    
    // ========================================================
    // Configure GPIO 5 for ETM task (toggle on PCNT event)
    // ========================================================
    // GPIO ETM task configuration:
    // - GPIO_ETM_TASK_P1_CFG_REG controls GPIOs 4-7
    // - Each GPIO has 8 bits: [enable:1][sel:3][reserved:4]
    // - GPIO 5 is at bits [8:15] in P1 register (gpio_num % 4 = 1)
    // - sel = 0 means task channel 0
    // - Task channel 0 with TOGGLE action = ETM task ID 17
    
    // First set GPIO 5 as output
    gpio_reset_pin(GPIO_INDICATOR);
    gpio_set_direction(GPIO_INDICATOR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_INDICATOR, 0);
    
    // Configure GPIO 5 to respond to ETM task channel 0
    // P1 register covers GPIOs 4-7, GPIO 5 is at index 1 (bits 8-15)
    // Format: [enable:1][sel:3] at bits [8] and [9:11]
    volatile uint32_t *task_p1 = (volatile uint32_t*)GPIO_ETM_TASK_P1_CFG_REG;
    uint32_t before = *task_p1;
    
    // Clear GPIO 5 config bits, then set:
    // - Bit 8 = enable (1)
    // - Bits 9-11 = channel select (0 = channel 0)
    *task_p1 = (*task_p1 & ~(0xFF << 8)) | (0x01 << 8);  // Enable GPIO5, channel 0
    
    uint32_t after = *task_p1;
    printf("GPIO ETM task P1 config: 0x%08lx -> 0x%08lx\n", 
           (unsigned long)before, (unsigned long)after);
    printf("  GPIO %d bound to ETM task channel 0 (TOGGLE task ID = 17)\n", GPIO_INDICATOR);
    
    // ========================================================
    // ETM Channel Configuration
    // ========================================================
    
    // ETM CH0: PCNT HIGH LIMIT event (46) → GPIO CH0 toggle (task 17)
    #define GPIO_TASK_CH0_TOGGLE 17
    printf("  CH0: PCNT high limit (evt=%d) -> GPIO%d toggle (task=%d)\n", 
           PCNT_EVT_CNT_EQ_LMT, GPIO_INDICATOR, GPIO_TASK_CH0_TOGGLE);
    ETM_REG(ETM_CH_EVT_ID_REG(0)) = PCNT_EVT_CNT_EQ_LMT;  // Use limit event!
    ETM_REG(ETM_CH_TASK_ID_REG(0)) = GPIO_TASK_CH0_TOGGLE;
    
    // ETM CH1: Timer alarm → LEDC Timer0 RESUME
    printf("  CH1: Timer alarm (evt=%d) -> LEDC resume (task=%d)\n",
           TIMER0_EVT_CNT_CMP, LEDC_TASK_TIMER0_RESUME);
    ETM_REG(ETM_CH_EVT_ID_REG(1)) = TIMER0_EVT_CNT_CMP;
    ETM_REG(ETM_CH_TASK_ID_REG(1)) = LEDC_TASK_TIMER0_RESUME;
    
    // ETM CH2: Timer alarm → PCNT reset
    printf("  CH2: Timer alarm (evt=%d) -> PCNT reset (task=%d)\n",
           TIMER0_EVT_CNT_CMP, PCNT_TASK_CNT_RST);
    ETM_REG(ETM_CH_EVT_ID_REG(2)) = TIMER0_EVT_CNT_CMP;
    ETM_REG(ETM_CH_TASK_ID_REG(2)) = PCNT_TASK_CNT_RST;
    
    // ETM CH3: Timer alarm → GPIO CH0 toggle (to verify GPIO task works)
    // This is a sanity check - if this toggles, GPIO task is configured correctly
    printf("  CH3: Timer alarm (evt=%d) -> GPIO%d toggle (task=%d) [SANITY CHECK]\n",
           TIMER0_EVT_CNT_CMP, GPIO_INDICATOR, GPIO_TASK_CH0_TOGGLE);
    ETM_REG(ETM_CH_EVT_ID_REG(3)) = TIMER0_EVT_CNT_CMP;
    ETM_REG(ETM_CH_TASK_ID_REG(3)) = GPIO_TASK_CH0_TOGGLE;
    
    // ETM CH4: PCNT HIGH LIMIT event (46) → LEDC Timer0 PAUSE (61)
    // This is our main test - does PCNT event trigger LEDC pause?
    printf("  CH4: PCNT high limit (evt=%d) -> LEDC pause (task=%d)\n",
           PCNT_EVT_CNT_EQ_LMT, LEDC_TASK_TIMER0_PAUSE);
    ETM_REG(ETM_CH_EVT_ID_REG(4)) = PCNT_EVT_CNT_EQ_LMT;
    ETM_REG(ETM_CH_TASK_ID_REG(4)) = LEDC_TASK_TIMER0_PAUSE;
    
    printf("\nETM wiring configured (NOT yet enabled).\n");
    
    // ========================================================
    // TEST: Use ESP-IDF API for GPIO ETM task to see if it works
    // ========================================================
    printf("\n*** Testing ESP-IDF GPIO ETM API on GPIO 6 ***\n");
    
    // Configure GPIO 6 as output
    gpio_reset_pin(6);
    gpio_set_direction(6, GPIO_MODE_OUTPUT);
    gpio_set_level(6, 0);
    
    // Create GPIO ETM task using ESP-IDF API
    esp_etm_task_handle_t gpio6_task = NULL;
    gpio_etm_task_config_t gpio_task_config = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    esp_err_t err = gpio_new_etm_task(&gpio_task_config, &gpio6_task);
    if (err == ESP_OK) {
        err = gpio_etm_task_add_gpio(gpio6_task, 6);
        if (err == ESP_OK) {
            printf("  GPIO 6 ETM task created via ESP-IDF API\n");
            
            // Create timer ETM event using ESP-IDF API
            esp_etm_event_handle_t timer_event = NULL;
            gptimer_etm_event_config_t timer_evt_cfg = {
                .event_type = GPTIMER_ETM_EVENT_ALARM_MATCH,
            };
            err = gptimer_new_etm_event(timer, &timer_evt_cfg, &timer_event);
            if (err == ESP_OK) {
                printf("  Timer ETM event created via ESP-IDF API\n");
                
                // Create ETM channel and connect
                esp_etm_channel_handle_t etm_ch = NULL;
                esp_etm_channel_config_t etm_ch_cfg = {};
                err = esp_etm_new_channel(&etm_ch_cfg, &etm_ch);
                if (err == ESP_OK) {
                    err = esp_etm_channel_connect(etm_ch, timer_event, gpio6_task);
                    if (err == ESP_OK) {
                        err = esp_etm_channel_enable(etm_ch);
                        if (err == ESP_OK) {
                            printf("  ESP-IDF ETM channel enabled: Timer -> GPIO6 toggle\n");
                        }
                    }
                }
            }
        }
    }
    if (err != ESP_OK) {
        printf("  ESP-IDF GPIO ETM test failed: %s\n", esp_err_to_name(err));
    }
}

static void enable_etm_channels(void) {
    printf("\nEnabling ETM channels...\n");
    
    // Clear any pending PCNT interrupt/event status first
    *(volatile uint32_t*)(PCNT_BASE + 0x4c) = 0xF;  // PCNT_INT_CLR_REG - clear all
    
    // Now enable all 5 ETM channels at once
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);
    
    // Verify
    printf("ETM channels enabled. Reading back:\n");
    for (int ch = 0; ch < 5; ch++) {
        uint32_t evt = ETM_REG(ETM_CH_EVT_ID_REG(ch));
        uint32_t task = ETM_REG(ETM_CH_TASK_ID_REG(ch));
        printf("  CH%d: evt=%lu, task=%lu\n", ch, (unsigned long)evt, (unsigned long)task);
    }
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    printf("\n\n");
    printf("========================================\n");
    printf("   SINGLE LEDC ETM TEST (v2 - High Limit)\n");
    printf("========================================\n");
    printf("\n");
    printf("Using PCNT HIGH LIMIT event (46) instead of threshold (45)\n");
    printf("Expected: PCNT reaches ~%d then stops\n", PCNT_HIGH_LIMIT);
    printf("          until timer resumes LEDC\n");
    printf("\n");
    
    // Setup
    enable_clocks();
    
    if (setup_ledc() != ESP_OK) return;
    
    enable_ledc_etm_tasks();
    
    if (setup_pcnt() != ESP_OK) return;
    if (setup_timer() != ESP_OK) return;
    
    // FIRST: Test basic LEDC->PCNT BEFORE ETM wiring
    printf("\n=== Testing basic LEDC->PCNT BEFORE ETM ===\n");
    dump_pcnt_regs("Before test");
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait 100ms for LEDC to generate pulses
    int test_count;
    pcnt_unit_get_count(pcnt_unit, &test_count);
    printf("After 100ms with LEDC running (no ETM): PCNT=%d (expect ~100 at 1kHz)\n", test_count);
    
    if (test_count < 5) {
        printf("ERROR: LEDC not generating pulses properly!\n");
        printf("Check GPIO %d connection to PCNT.\n", GPIO_OUT);
        return;
    }
    
    printf("LEDC effective frequency: ~%d Hz\n", test_count * 10);
    
    // Clear PCNT before enabling ETM
    pcnt_unit_clear_count(pcnt_unit);
    printf("Basic test PASSED! PCNT cleared.\n");
    
    // Configure ETM wiring (but don't enable yet)
    setup_etm_wiring();
    
    dump_pcnt_regs("After ETM wiring, before enable");
    
    // Clear PCNT and prepare for test
    pcnt_unit_clear_count(pcnt_unit);
    
    // Start timer FIRST (so resume will work)
    gptimer_start(timer);
    printf("Timer started!\n");
    
    // Read LEDC status
    volatile uint32_t *ledc_timer0_conf = (volatile uint32_t*)(LEDC_BASE + 0xa0);
    printf("LEDC Timer0 conf: 0x%08lx\n", (unsigned long)*ledc_timer0_conf);
    printf("LEDC evt_task_en1: 0x%08lx\n", (unsigned long)*(volatile uint32_t*)LEDC_EVT_TASK_EN1_REG);
    
    // NOW enable ETM channels
    enable_etm_channels();
    
    dump_pcnt_regs("After ETM enable");
    
    // Small delay then clear PCNT to start fresh
    printf("\nClearing PCNT and starting monitoring...\n");
    pcnt_unit_clear_count(pcnt_unit);
    
    // Monitor - show PCNT value, LEDC pause status, GPIO indicator
    volatile uint32_t *ledc_t0_conf = (volatile uint32_t*)(LEDC_BASE + 0xa0);
    volatile uint32_t *gpio_out = (volatile uint32_t*)(0x60091000 + 0x04);  // GPIO_OUT_REG
    volatile uint32_t *gpio_enable = (volatile uint32_t*)(0x60091000 + 0x20);  // GPIO_ENABLE_REG
    
    // Debug: Check GPIO ETM task configuration
    printf("\nGPIO ETM Debug:\n");
    printf("  GPIO_ENABLE_REG: 0x%08lx (bit %d=%d)\n", 
           (unsigned long)*gpio_enable, GPIO_INDICATOR, (int)((*gpio_enable >> GPIO_INDICATOR) & 1));
    printf("  GPIO_OUT_REG: 0x%08lx (bit %d=%d)\n", 
           (unsigned long)*gpio_out, GPIO_INDICATOR, (int)((*gpio_out >> GPIO_INDICATOR) & 1));
    printf("  GPIO_ETM_TASK_P1: 0x%08lx\n", 
           (unsigned long)*(volatile uint32_t*)GPIO_ETM_TASK_P1_CFG_REG);
    
    int prev_count = -1;
    int toggle_count5 = 0;
    int toggle_count6 = 0;
    int prev_gpio5 = 0;
    int prev_gpio6 = 0;
    
    for (int i = 0; ; i++) {
        int count;
        pcnt_unit_get_count(pcnt_unit, &count);
        uint32_t t0_conf = *ledc_t0_conf;
        int ledc_paused = (t0_conf >> 23) & 1;  // Bit 23 is TIMER0_PAUSE
        int gpio5_level = (*gpio_out >> GPIO_INDICATOR) & 1;
        
        int gpio6_level = (*gpio_out >> 6) & 1;
        
        // Count GPIO toggles
        if (gpio5_level != prev_gpio5) {
            toggle_count5++;
        }
        prev_gpio5 = gpio5_level;
        if (gpio6_level != prev_gpio6) {
            toggle_count6++;
        }
        prev_gpio6 = gpio6_level;
        
        // Always print to see what's happening
        printf("[%d] PCNT=%d LEDC=%s G5=%d(%d) G6=%d(%d)", 
               i, count, ledc_paused ? "PAUSED" : "run", 
               gpio5_level, toggle_count5, gpio6_level, toggle_count6);
        
        if (count < prev_count - 5) {
            printf(" *** RESET ***");
        } else if (count == prev_count && prev_count >= 0) {
            printf(" (cnt paused)");
        } else if (count >= PCNT_HIGH_LIMIT - 5) {
            printf(" NEAR LIMIT!");
        }
        printf("\n");
        fflush(stdout);
        
        prev_count = count;
        
        // Fast sampling for first 20, then slow
        if (i < 30) {
            vTaskDelay(pdMS_TO_TICKS(20));  // 20ms sampling initially
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));  // 100ms sampling after
        }
    }
}
