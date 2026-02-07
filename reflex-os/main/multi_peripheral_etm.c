/**
 * multi_peripheral_etm.c - Multi-Peripheral ETM Pattern Selection
 *
 * THE CARDS SELECT THE NEXT CARD
 *
 * Architecture:
 *   Multiple peripherals as "punchcards", ETM selects which is active.
 *   PCNT accumulates pulses. When threshold is reached, ETM switches cards.
 *
 *   Pattern A: LEDC CH0 (fast pulses, 10kHz)
 *   Pattern B: LEDC CH1 (slow pulses, 1kHz)
 *   Pattern C: Idle (no pulses)
 *
 *   PCNT threshold (32) → ETM → Disable LEDC CH0, Enable LEDC CH1
 *   PCNT limit (64)     → ETM → Disable LEDC CH1 (idle)
 *   Timer alarm         → ETM → Reset PCNT, Enable LEDC CH0 (restart)
 *
 * Why LEDC?
 *   - LEDC has ETM tasks: SIG_OUT_DIS, TIMER_PAUSE/RESUME
 *   - Multiple LEDC channels can output different frequencies
 *   - GPIO matrix can connect multiple LEDC channels to same GPIO
 *     (highest priority peripheral wins, or we use separate GPIOs)
 *
 * Setup:
 *   GPIO 4 = LEDC CH0 output (Pattern A: fast)
 *   GPIO 5 = LEDC CH1 output (Pattern B: slow)
 *   PCNT CH0 watches GPIO 4
 *   PCNT CH1 watches GPIO 5
 *   Total PCNT = CH0 + CH1 (watches both patterns)
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
#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "MULTI_PERI";

// ============================================================
// ETM Register Definitions
// ============================================================

#define ETM_BASE                    0x600B8000
#define ETM_CH_ENA_SET_REG          (ETM_BASE + 0x04)
#define ETM_CH_ENA_CLR_REG          (ETM_BASE + 0x08)
#define ETM_CH_EVT_ID_REG(n)        (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n)       (ETM_BASE + 0x1C + (n) * 8)
#define ETM_REG(addr)               (*(volatile uint32_t*)(addr))

#define PCR_BASE                    0x60096000
#define PCR_SOC_ETM_CONF            (PCR_BASE + 0x90)

// ============================================================
// ETM Event/Task IDs (from soc_etm_source.h)
// ============================================================

// PCNT Events
#define PCNT_EVT_CNT_EQ_THRESH      45  // PCNT threshold reached
#define PCNT_EVT_CNT_EQ_LMT         46  // PCNT high limit reached
#define PCNT_EVT_CNT_EQ_ZERO        47  // PCNT zero crossing

// PCNT Tasks
#define PCNT_TASK_CNT_RST           87  // Reset PCNT counter

// LEDC Events
#define LEDC_EVT_DUTY_CHNG_END_CH0  25  // LEDC CH0 duty change end
#define LEDC_EVT_DUTY_CHNG_END_CH1  26  // LEDC CH1 duty change end
#define LEDC_EVT_OVF_CNT_PLS_CH0    31  // LEDC CH0 overflow pulse
#define LEDC_EVT_OVF_CNT_PLS_CH1    32  // LEDC CH1 overflow pulse

// LEDC Tasks
#define LEDC_TASK_SIG_OUT_DIS_CH0   41  // Disable LEDC CH0 output
#define LEDC_TASK_SIG_OUT_DIS_CH1   42  // Disable LEDC CH1 output
#define LEDC_TASK_TIMER0_PAUSE      61  // Pause LEDC timer 0
#define LEDC_TASK_TIMER0_RESUME     57  // Resume LEDC timer 0
#define LEDC_TASK_TIMER1_PAUSE      62  // Pause LEDC timer 1
#define LEDC_TASK_TIMER1_RESUME     58  // Resume LEDC timer 1

// Timer Events
#define TIMER0_EVT_CNT_CMP          48  // Timer 0 compare match

// ============================================================
// Configuration
// ============================================================

#define GPIO_PATTERN_A      4   // LEDC CH0 output (fast pulses)
#define GPIO_PATTERN_B      5   // LEDC CH1 output (slow pulses)

// PCNT thresholds
#define THRESHOLD_SWITCH    32   // Switch from Pattern A to Pattern B
#define THRESHOLD_IDLE      64   // Stop all patterns (idle)
#define RESET_PERIOD_MS     1000 // Timer resets every 1 second

// Pattern frequencies
#define PATTERN_A_FREQ_HZ   10000  // Fast: 10kHz
#define PATTERN_B_FREQ_HZ   1000   // Slow: 1kHz

// ============================================================
// Handles
// ============================================================

static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_chan_a = NULL;
static pcnt_channel_handle_t pcnt_chan_b = NULL;
static gptimer_handle_t timer = NULL;

// Statistics
static volatile uint32_t switch_count = 0;
static volatile uint32_t idle_count = 0;
static volatile uint32_t reset_count = 0;

// ============================================================
// Clock Enable
// ============================================================

static void enable_etm_clock(void) {
    volatile uint32_t *etm_conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *etm_conf &= ~(1 << 1);  // Clear reset
    *etm_conf |= (1 << 0);   // Enable clock
    ESP_LOGI(TAG, "ETM clock enabled");
}

// ============================================================
// LEDC ETM Task Enable
// ============================================================

// LEDC base address
#define LEDC_BASE               0x60007000
#define LEDC_EVT_TASK_EN1_REG   (LEDC_BASE + 0x1a4)  // evt_task_en1 register

static void enable_ledc_etm_tasks(void) {
    // Enable ETM tasks for timer0 and timer1 pause/resume
    // evt_task_en1 register bits:
    //   bit 28: task_timer0_pause_resume_en
    //   bit 29: task_timer1_pause_resume_en
    //   bit 24: task_timer0_rst_en
    //   bit 25: task_timer1_rst_en
    
    volatile uint32_t *evt_task_en1 = (volatile uint32_t*)LEDC_EVT_TASK_EN1_REG;
    
    // Enable timer0 and timer1 pause/resume tasks
    *evt_task_en1 |= (1 << 28) | (1 << 29);
    
    ESP_LOGI(TAG, "LEDC ETM tasks enabled:");
    ESP_LOGI(TAG, "  Timer0 pause/resume: enabled");
    ESP_LOGI(TAG, "  Timer1 pause/resume: enabled");
    ESP_LOGI(TAG, "  evt_task_en1 = 0x%08lx", (unsigned long)*evt_task_en1);
}

// ============================================================
// LEDC Setup
// ============================================================

static esp_err_t setup_ledc(void) {
    // Timer 0 for Pattern A (fast)
    ledc_timer_config_t timer0_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = PATTERN_A_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer0_cfg), TAG, "Timer 0 config failed");
    
    // Timer 1 for Pattern B (slow)
    ledc_timer_config_t timer1_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = PATTERN_B_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer1_cfg), TAG, "Timer 1 config failed");
    
    // Channel 0 (Pattern A) on GPIO 4
    ledc_channel_config_t ch0_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = GPIO_PATTERN_A,
        .duty = 128,  // 50% duty
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch0_cfg), TAG, "Channel 0 config failed");
    
    // Channel 1 (Pattern B) on GPIO 5 - initially paused
    ledc_channel_config_t ch1_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = GPIO_PATTERN_B,
        .duty = 128,  // 50% duty
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch1_cfg), TAG, "Channel 1 config failed");
    
    // Initially pause Timer 1 (Pattern B starts disabled)
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1);
    
    ESP_LOGI(TAG, "LEDC configured:");
    ESP_LOGI(TAG, "  Pattern A (CH0): %d Hz on GPIO %d", PATTERN_A_FREQ_HZ, GPIO_PATTERN_A);
    ESP_LOGI(TAG, "  Pattern B (CH1): %d Hz on GPIO %d (paused)", PATTERN_B_FREQ_HZ, GPIO_PATTERN_B);
    
    return ESP_OK;
}

// ============================================================
// PCNT Setup
// ============================================================

static esp_err_t setup_pcnt(void) {
    // PCNT unit with watch points
    pcnt_unit_config_t unit_cfg = {
        .low_limit = -32768,
        .high_limit = THRESHOLD_IDLE,  // Will fire PCNT_EVT_CNT_EQ_LMT at 64
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &pcnt_unit), TAG, "PCNT unit failed");
    
    // Channel A watches GPIO 4 (Pattern A)
    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num = GPIO_PATTERN_A,
        .level_gpio_num = -1,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(pcnt_unit, &chan_a_cfg, &pcnt_chan_a), TAG, "PCNT chan A failed");
    pcnt_channel_set_edge_action(pcnt_chan_a,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    // Channel B watches GPIO 5 (Pattern B)
    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num = GPIO_PATTERN_B,
        .level_gpio_num = -1,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(pcnt_unit, &chan_b_cfg, &pcnt_chan_b), TAG, "PCNT chan B failed");
    pcnt_channel_set_edge_action(pcnt_chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    // Add watch point for threshold (fires event 45)
    pcnt_unit_add_watch_point(pcnt_unit, THRESHOLD_SWITCH);
    
    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_start(pcnt_unit);
    
    ESP_LOGI(TAG, "PCNT configured:");
    ESP_LOGI(TAG, "  Watch point (threshold): %d -> event 45", THRESHOLD_SWITCH);
    ESP_LOGI(TAG, "  High limit: %d -> event 46", THRESHOLD_IDLE);
    
    return ESP_OK;
}

// ============================================================
// Timer Setup
// ============================================================

static esp_err_t setup_timer(void) {
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1MHz
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&cfg, &timer), TAG, "Timer create failed");
    
    gptimer_alarm_config_t alarm = {
        .alarm_count = RESET_PERIOD_MS * 1000,  // Convert ms to us
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action(timer, &alarm);
    gptimer_enable(timer);
    
    ESP_LOGI(TAG, "Timer configured: %d ms period", RESET_PERIOD_MS);
    return ESP_OK;
}

// ============================================================
// ETM Wiring - The Autonomous Logic
// ============================================================

static void setup_etm_wiring(void) {
    ESP_LOGI(TAG, "Wiring ETM for autonomous pattern selection...");
    
    // ===== ETM Channel 0: PCNT threshold -> Pause Timer 0 (stop Pattern A) =====
    ETM_REG(ETM_CH_EVT_ID_REG(0)) = PCNT_EVT_CNT_EQ_THRESH;  // Event 45
    ETM_REG(ETM_CH_TASK_ID_REG(0)) = LEDC_TASK_TIMER0_PAUSE; // Task 61
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 0);
    ESP_LOGI(TAG, "  ETM CH0: PCNT threshold (%d) -> LEDC Timer0 PAUSE", THRESHOLD_SWITCH);
    
    // ===== ETM Channel 1: PCNT threshold -> Resume Timer 1 (start Pattern B) =====
    ETM_REG(ETM_CH_EVT_ID_REG(1)) = PCNT_EVT_CNT_EQ_THRESH;  // Event 45
    ETM_REG(ETM_CH_TASK_ID_REG(1)) = LEDC_TASK_TIMER1_RESUME; // Task 58
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 1);
    ESP_LOGI(TAG, "  ETM CH1: PCNT threshold (%d) -> LEDC Timer1 RESUME", THRESHOLD_SWITCH);
    
    // ===== ETM Channel 2: PCNT limit -> Pause Timer 1 (stop Pattern B, idle) =====
    ETM_REG(ETM_CH_EVT_ID_REG(2)) = PCNT_EVT_CNT_EQ_LMT;     // Event 46
    ETM_REG(ETM_CH_TASK_ID_REG(2)) = LEDC_TASK_TIMER1_PAUSE; // Task 62
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 2);
    ESP_LOGI(TAG, "  ETM CH2: PCNT limit (%d) -> LEDC Timer1 PAUSE (idle)", THRESHOLD_IDLE);
    
    // ===== ETM Channel 3: Timer alarm -> Reset PCNT =====
    ETM_REG(ETM_CH_EVT_ID_REG(3)) = TIMER0_EVT_CNT_CMP;      // Event 48
    ETM_REG(ETM_CH_TASK_ID_REG(3)) = PCNT_TASK_CNT_RST;      // Task 87
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 3);
    ESP_LOGI(TAG, "  ETM CH3: Timer alarm -> PCNT reset");
    
    // ===== ETM Channel 4: Timer alarm -> Resume Timer 0 (restart Pattern A) =====
    ETM_REG(ETM_CH_EVT_ID_REG(4)) = TIMER0_EVT_CNT_CMP;      // Event 48
    ETM_REG(ETM_CH_TASK_ID_REG(4)) = LEDC_TASK_TIMER0_RESUME; // Task 57
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 4);
    ESP_LOGI(TAG, "  ETM CH4: Timer alarm -> LEDC Timer0 RESUME");
    
    // ===== ETM Channel 5: Timer alarm -> Pause Timer 1 (ensure Pattern B off) =====
    ETM_REG(ETM_CH_EVT_ID_REG(5)) = TIMER0_EVT_CNT_CMP;      // Event 48
    ETM_REG(ETM_CH_TASK_ID_REG(5)) = LEDC_TASK_TIMER1_PAUSE; // Task 62
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << 5);
    ESP_LOGI(TAG, "  ETM CH5: Timer alarm -> LEDC Timer1 PAUSE");
    
    ESP_LOGI(TAG, "ETM wiring complete!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Autonomous State Machine:");
    ESP_LOGI(TAG, "  Phase 1: Pattern A active (fast pulses)");
    ESP_LOGI(TAG, "           PCNT counts up...");
    ESP_LOGI(TAG, "  Phase 2: At PCNT=%d, switch to Pattern B (slow pulses)", THRESHOLD_SWITCH);
    ESP_LOGI(TAG, "           PCNT continues counting...");
    ESP_LOGI(TAG, "  Phase 3: At PCNT=%d, idle (no pulses)", THRESHOLD_IDLE);
    ESP_LOGI(TAG, "           System waits for timer...");
    ESP_LOGI(TAG, "  Phase 4: Timer alarm resets PCNT, restarts Pattern A");
    ESP_LOGI(TAG, "           Cycle repeats autonomously!");
}

// ============================================================
// Monitoring Task
// ============================================================

static void monitor_task(void *arg) {
    int prev_count = 0;
    int count;
    int phase = 1;
    
    while (1) {
        pcnt_unit_get_count(pcnt_unit, &count);
        
        // Detect phase transitions
        int new_phase;
        if (count < THRESHOLD_SWITCH) {
            new_phase = 1;  // Pattern A
        } else if (count < THRESHOLD_IDLE) {
            new_phase = 2;  // Pattern B
        } else {
            new_phase = 3;  // Idle
        }
        
        if (new_phase != phase) {
            const char *phase_name = (new_phase == 1) ? "Pattern A (fast)" :
                                     (new_phase == 2) ? "Pattern B (slow)" : "Idle";
            ESP_LOGI(TAG, ">>> PHASE CHANGE: %s at PCNT=%d", phase_name, count);
            phase = new_phase;
        }
        
        // Detect reset
        if (count < prev_count - 10) {
            reset_count++;
            ESP_LOGI(TAG, ">>> RESET #%"PRIu32": PCNT went from %d to %d", reset_count, prev_count, count);
        }
        
        // Periodic status
        static int tick = 0;
        if (++tick % 10 == 0) {
            printf("PCNT=%d  Phase=%d  Resets=%"PRIu32"\n", count, phase, reset_count);
        }
        
        prev_count = count;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    printf("\n\n");
    printf("================================================================\n");
    printf("   MULTI-PERIPHERAL ETM PATTERN SELECTION\n");
    printf("================================================================\n");
    printf("\n");
    printf("   THE CARDS SELECT THE NEXT CARD\n");
    printf("\n");
    printf("   Pattern A: LEDC CH0 @ %d Hz on GPIO %d\n", PATTERN_A_FREQ_HZ, GPIO_PATTERN_A);
    printf("   Pattern B: LEDC CH1 @ %d Hz on GPIO %d\n", PATTERN_B_FREQ_HZ, GPIO_PATTERN_B);
    printf("\n");
    printf("   PCNT threshold %d -> Switch to Pattern B\n", THRESHOLD_SWITCH);
    printf("   PCNT limit %d -> Idle (no output)\n", THRESHOLD_IDLE);
    printf("   Timer alarm (%d ms) -> Reset, restart Pattern A\n", RESET_PERIOD_MS);
    printf("\n");
    printf("================================================================\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize
    enable_etm_clock();
    
    if (setup_ledc() != ESP_OK) {
        ESP_LOGE(TAG, "LEDC setup failed");
        return;
    }
    
    // Enable LEDC ETM tasks (must be after setup_ledc())
    enable_ledc_etm_tasks();
    
    if (setup_pcnt() != ESP_OK) {
        ESP_LOGE(TAG, "PCNT setup failed");
        return;
    }
    
    if (setup_timer() != ESP_OK) {
        ESP_LOGE(TAG, "Timer setup failed");
        return;
    }
    
    setup_etm_wiring();
    
    // Start timer
    gptimer_start(timer);
    ESP_LOGI(TAG, "Timer started - autonomous operation begins!");
    
    // Start monitoring task
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
    
    printf("\n");
    printf("================================================================\n");
    printf("   AUTONOMOUS OPERATION STARTED\n");
    printf("   CPU can now sleep while patterns cycle automatically\n");
    printf("================================================================\n");
    printf("\n");
    printf("\"It's all in the reflexes.\" - Jack Burton\n\n");
    fflush(stdout);
}
