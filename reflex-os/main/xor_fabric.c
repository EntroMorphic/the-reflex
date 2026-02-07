/**
 * xor_fabric.c - CPU-free XOR gate using PCNT level-gated edge counting
 *
 * Proves XOR(A,B) = (A AND NOT B) + (NOT A AND B) using two PCNT channels
 * on a single unit. PARLIO drives A on GPIO4 and B on GPIO5 simultaneously.
 *
 * PCNT Unit 0:
 *   Channel 0: edge=GPIO4(A), level=GPIO5(B)
 *     edge: pos=INCREASE  → counts A rising edges
 *     level: high=HOLD, low=KEEP → only when B is LOW
 *     Result: A AND (NOT B)
 *
 *   Channel 1: edge=GPIO5(B), level=GPIO4(A)
 *     edge: pos=INCREASE  → counts B rising edges
 *     level: high=HOLD, low=KEEP → only when A is LOW
 *     Result: (NOT A) AND B
 *
 *   Unit total = A XOR B (both channels share the same counter)
 *
 * Also configures PCNT Unit 1 as AND gate for comparison:
 *   Channel 0: edge=GPIO4(A), level=GPIO5(B)
 *     edge: pos=INCREASE
 *     level: high=KEEP, low=HOLD → only when B is HIGH
 *     Result: A AND B
 *
 * Pattern encoding (PARLIO 4-bit on GPIO 4-7):
 *   Bit 0 = GPIO4 = A
 *   Bit 1 = GPIO5 = B
 *   Bit 2 = GPIO6 = (unused)
 *   Bit 3 = GPIO7 = (unused)
 *
 *   A=0,B=0 → nibble 0x0  (alternating with 0x0 = no edges)
 *   A=1,B=0 → nibble 0x1  (alternating with 0x0)
 *   A=0,B=1 → nibble 0x2  (alternating with 0x0)
 *   A=1,B=1 → nibble 0x3  (alternating with 0x0)
 *
 * Expected results for 32 pulses:
 *   A=0,B=0 → XOR=0,  AND=0
 *   A=1,B=0 → XOR=32, AND=0
 *   A=0,B=1 → XOR=32, AND=0
 *   A=1,B=1 → XOR=0,  AND=32
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "esp_rom_sys.h"

#define GPIO_A  4
#define GPIO_B  5
#define NUM_PULSES 64  /* 64 bytes, alternating pattern/0x00 = 32 edges */

/* Pattern buffers: 4 XOR input combos */
static uint8_t __attribute__((aligned(4))) pat_00[NUM_PULSES]; /* A=0, B=0 */
static uint8_t __attribute__((aligned(4))) pat_10[NUM_PULSES]; /* A=1, B=0 */
static uint8_t __attribute__((aligned(4))) pat_01[NUM_PULSES]; /* A=0, B=1 */
static uint8_t __attribute__((aligned(4))) pat_11[NUM_PULSES]; /* A=1, B=1 */

static parlio_tx_unit_handle_t parlio = NULL;
static pcnt_unit_handle_t xor_unit = NULL;    /* XOR gate */
static pcnt_unit_handle_t and_unit = NULL;    /* AND gate */
static pcnt_channel_handle_t xor_ch0 = NULL;  /* A AND NOT B */
static pcnt_channel_handle_t xor_ch1 = NULL;  /* NOT A AND B */
static pcnt_channel_handle_t and_ch0 = NULL;  /* A AND B */

static void init_patterns(void) {
    for (int i = 0; i < NUM_PULSES; i++) {
        /* Even bytes = active pattern, odd bytes = 0x00 (creates edges) */
        pat_00[i] = 0x00;                           /* A=0, B=0: no bits */
        pat_10[i] = (i % 2 == 0) ? 0x01 : 0x00;    /* A=1, B=0: bit 0 */
        pat_01[i] = (i % 2 == 0) ? 0x02 : 0x00;    /* A=0, B=1: bit 1 */
        pat_11[i] = (i % 2 == 0) ? 0x03 : 0x00;    /* A=1, B=1: bits 0+1 */
    }
}

static void init_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = 1000000,  /* 1 MHz — slow enough for PCNT */
        .data_width = 4,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 4,
        .max_transfer_size = 256,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++)
        cfg.data_gpio_nums[i] = (i < 4) ? (4 + i) : -1;

    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    parlio_tx_unit_enable(parlio);
}

static void init_pcnt(void) {
    /* ── XOR Unit (Unit 0): two channels, shared counter ── */
    pcnt_unit_config_t xor_cfg = {
        .low_limit = -100,
        .high_limit = 100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&xor_cfg, &xor_unit));

    /* Channel 0: edge=A(GPIO4), level=B(GPIO5)
     * Counts A edges only when B is LOW → A AND NOT B */
    pcnt_chan_config_t xch0_cfg = {
        .edge_gpio_num = GPIO_A,
        .level_gpio_num = GPIO_B,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(xor_unit, &xch0_cfg, &xor_ch0));
    pcnt_channel_set_edge_action(xor_ch0,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,   /* pos edge: count */
        PCNT_CHANNEL_EDGE_ACTION_HOLD);      /* neg edge: ignore */
    pcnt_channel_set_level_action(xor_ch0,
        PCNT_CHANNEL_LEVEL_ACTION_HOLD,      /* B=HIGH: freeze (don't count) */
        PCNT_CHANNEL_LEVEL_ACTION_KEEP);     /* B=LOW: count normally */

    /* Channel 1: edge=B(GPIO5), level=A(GPIO4)
     * Counts B edges only when A is LOW → NOT A AND B */
    pcnt_chan_config_t xch1_cfg = {
        .edge_gpio_num = GPIO_B,
        .level_gpio_num = GPIO_A,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(xor_unit, &xch1_cfg, &xor_ch1));
    pcnt_channel_set_edge_action(xor_ch1,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,   /* pos edge: count */
        PCNT_CHANNEL_EDGE_ACTION_HOLD);      /* neg edge: ignore */
    pcnt_channel_set_level_action(xor_ch1,
        PCNT_CHANNEL_LEVEL_ACTION_HOLD,      /* A=HIGH: freeze */
        PCNT_CHANNEL_LEVEL_ACTION_KEEP);     /* A=LOW: count normally */

    pcnt_unit_enable(xor_unit);
    pcnt_unit_start(xor_unit);

    /* ── AND Unit (Unit 1): one channel ── */
    pcnt_unit_config_t and_cfg = {
        .low_limit = -100,
        .high_limit = 100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&and_cfg, &and_unit));

    /* Channel 0: edge=A(GPIO4), level=B(GPIO5)
     * Counts A edges only when B is HIGH → A AND B */
    pcnt_chan_config_t ach0_cfg = {
        .edge_gpio_num = GPIO_A,
        .level_gpio_num = GPIO_B,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(and_unit, &ach0_cfg, &and_ch0));
    pcnt_channel_set_edge_action(and_ch0,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_channel_set_level_action(and_ch0,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,      /* B=HIGH: count normally (A AND B) */
        PCNT_CHANNEL_LEVEL_ACTION_HOLD);     /* B=LOW: freeze */

    pcnt_unit_enable(and_unit);
    pcnt_unit_start(and_unit);
}

static void run_test(const char *label, uint8_t *pattern, int expect_xor, int expect_and) {
    pcnt_unit_clear_count(xor_unit);
    pcnt_unit_clear_count(and_unit);

    parlio_transmit_config_t tx = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern, NUM_PULSES * 8, &tx);
    parlio_tx_unit_wait_all_done(parlio, 1000);

    /* Small delay for PCNT to settle */
    esp_rom_delay_us(100);

    int xor_count = 0, and_count = 0;
    pcnt_unit_get_count(xor_unit, &xor_count);
    pcnt_unit_get_count(and_unit, &and_count);

    int xor_ok = (xor_count == expect_xor);
    int and_ok = (and_count == expect_and);

    printf("  %s: XOR=%d (expect %d) %s, AND=%d (expect %d) %s\n",
           label, xor_count, expect_xor, xor_ok ? "OK" : "FAIL",
           and_count, expect_and, and_ok ? "OK" : "FAIL");
}

void app_main(void) {
    printf("\n\n");
    printf("========================================\n");
    printf("  XOR FABRIC - CPU-Free Logic Gates\n");
    printf("========================================\n\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));

    printf("[INIT] Patterns...\n"); fflush(stdout);
    init_patterns();

    printf("[INIT] PARLIO (4-bit, GPIO 4-7, loopback)...\n"); fflush(stdout);
    init_parlio();

    printf("[INIT] PCNT XOR gate (Unit 0: 2 channels)...\n");
    printf("[INIT] PCNT AND gate (Unit 1: 1 channel)...\n"); fflush(stdout);
    init_pcnt();

    printf("[INIT] Done.\n\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("TEST: XOR and AND truth table\n");
    printf("  Pattern: 32 pulse pairs per test\n");
    printf("  PARLIO 4-bit: bit0=A(GPIO4), bit1=B(GPIO5)\n\n");
    fflush(stdout);

    int total = 0, passed = 0;

    /*        label      pattern  XOR  AND */
    run_test("A=0,B=0", pat_00,   0,   0); total += 2;
    run_test("A=1,B=0", pat_10,  32,   0); total += 2;
    run_test("A=0,B=1", pat_01,  32,   0); total += 2;
    run_test("A=1,B=1", pat_11,   0,  32); total += 2;

    /* Recount passes from output */
    {
        int xc, ac;
        /* Re-run to count */
        uint8_t *pats[] = {pat_00, pat_10, pat_01, pat_11};
        int exp_xor[] = {0, 32, 32, 0};
        int exp_and[] = {0, 0, 0, 32};
        for (int i = 0; i < 4; i++) {
            pcnt_unit_clear_count(xor_unit);
            pcnt_unit_clear_count(and_unit);
            parlio_transmit_config_t tx = { .idle_value = 0 };
            parlio_tx_unit_transmit(parlio, pats[i], NUM_PULSES * 8, &tx);
            parlio_tx_unit_wait_all_done(parlio, 1000);
            esp_rom_delay_us(100);
            pcnt_unit_get_count(xor_unit, &xc);
            pcnt_unit_get_count(and_unit, &ac);
            if (xc == exp_xor[i]) passed++;
            if (ac == exp_and[i]) passed++;
        }
    }

    printf("\n========================================\n");
    printf("  RESULTS: %d / %d PASSED\n", passed, total);
    if (passed == total)
        printf("  XOR AND AND GATES VERIFIED.\n");
    printf("========================================\n\n");
    fflush(stdout);

    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
