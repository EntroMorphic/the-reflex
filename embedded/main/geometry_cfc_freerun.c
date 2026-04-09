/*
 * geometry_cfc_freerun.c — Test Harness Orchestrator for the GIE Engine
 *
 * This file contains app_main() and the shared state used across the
 * test suite. Individual tests are in separate files:
 *
 *   test_gie_core.c   — Tests 1-8  (GIE, LP core, VDB, pipeline, feedback)
 *   test_espnow.c     — Tests 9-10 (ESP-NOW receive, live input)
 *   test_live_input.c — Test 11    (pattern classification + enrollment)
 *   test_memory.c     — Tests 12-13 (memory-modulated attention, VDB necessity)
 *   test_kinetic.c    — Tests 14, 14C (kinetic attention, CLS transition)
 *   test_lp_char.c    — LP characterization + dot magnitude diagnostic
 *
 * The GIE engine core is in gie_engine.c with its interface in gie_engine.h.
 *
 * Separated from engine code: April 6, 2026 (audit remediation).
 * Tests extracted to functions: April 7, 2026 (LMM-guided refactor).
 * Tests split to separate files: April 8, 2026 (audit remediation).
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * ESP-IDF: v5.4
 */

#include "test_harness.h"

/* ulp_main_bin_start/end declared in gie_engine.h (included via test_harness.h). */


/* ══════════════════════════════════════════════════════════════════
 *  SHARED STATE — used across test files via test_harness.h
 *
 *  sig[] is populated by Test 11 (enrollment) and read by Tests 12-14C.
 *  drain_buf[] is a scratch buffer used by all ESP-NOW-consuming tests.
 *  t12_* variables carry data from Test 12 to Test 13.
 * ══════════════════════════════════════════════════════════════════ */

int8_t sig[NUM_TEMPLATES][CFC_INPUT_DIM];
espnow_rx_entry_t drain_buf[32];

/* TEST 12 -> TEST 13 handoff (sign-space) */
int8_t  t12_mean1[LP_HIDDEN_DIM];
int8_t  t12_mean2[LP_HIDDEN_DIM];
int     t12_p1p3_result = -1;
int     t12_p1p2_result = -1;
int     t12_n1 = 0, t12_n2 = 0;

/* TEST 12 -> TEST 13 handoff (MTFP-space) */
int8_t  t12_mean1_mtfp[LP_MTFP_DIM];
int8_t  t12_mean2_mtfp[LP_MTFP_DIM];


/* ══════════════════════════════════════════════════════════════════
 *  MAIN — ORCHESTRATOR
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  FREE-RUNNING SUB-CPU NEURAL NETWORK\n");
    printf("  + LP CORE GEOMETRIC PROCESSOR\n");
    printf("  GIE (441 Hz HW) → LP core (100 Hz, 16MHz RISC-V, ~30uA)\n");
    printf("  Three-layer hierarchy: peripheral → geometric → CPU\n");
    printf("============================================================\n\n");

    printf("[INIT] GPIO 4-7...\n");
    gie_init_gpio();
    printf("[INIT] ETM clock...\n");
    gie_init_etm_clk();
    printf("[INIT] PARLIO TX (2-bit, 20MHz, loopback)...\n");
    gie_init_parlio();
    printf("[INIT] PCNT (2 units)...\n");
    gie_init_pcnt();
    printf("[INIT] Timer...\n");
    gie_init_timer();
    printf("[INIT] GDMA channel detection...\n");
    gie_detect_gdma_channel();
    printf("[GDMA] PARLIO owns CH%d\n", gie_get_gdma_channel());
    printf("[INIT] GDMA ISR (free-running mode)...\n");
    gie_init_gdma_isr();
    /* CRITICAL: Load LP binary FIRST, then pack weights.
     * ulp_lp_core_load_binary() zeros the BSS section in LP SRAM,
     * which would wipe out any weights already written there. */
    printf("[INIT] LP core binary (%d bytes)...\n",
           (int)(ulp_main_bin_end - ulp_main_bin_start));
    {
        esp_err_t err = ulp_lp_core_load_binary(ulp_main_bin_start,
                                                (ulp_main_bin_end - ulp_main_bin_start));
        if (err != ESP_OK) {
            printf("[LP] FAILED to load binary: %d\n", err);
        }
    }
    printf("[INIT] LP core weights (after binary load)...\n");
#ifndef LP_SEED
#define LP_SEED 0xCAFE1234
#endif
    init_lp_core_weights(LP_SEED);
    printf("[INIT] LP seed: 0x%08lX\n", (unsigned long)LP_SEED);
    start_lp_core();
    printf("[INIT] ESP-NOW receiver...\n");
    {
        esp_err_t err = espnow_receiver_init();
        if (err != ESP_OK) {
            printf("[ESPNOW] FAILED to init: %d\n", err);
        } else {
            printf("[ESPNOW] Listening on channel 1 (no AP connection)\n");
        }
    }
    printf("[INIT] Done.\n\n");
    fflush(stdout);

    prime_pipeline();

    /* ── Run all tests ── */
    int test_count = 0, pass_count = 0;

#ifdef SKIP_TO_14C
    /* Multi-seed 14C sweep: skip Tests 1-10,12-14. Only run Test 11
     * (signature enrollment — required for classification) and Test 14C. */
    printf("[SWEEP] SKIP_TO_14C: running Test 11 (enrollment) + Test 14C only\n\n");
    test_count++; pass_count += run_test_11();
    test_count++; pass_count += run_test_14c();
#else
    test_count++; pass_count += run_test_1();
    test_count++; pass_count += run_test_2();
    test_count++; pass_count += run_test_3();
    test_count++; pass_count += run_test_4();
    test_count++; pass_count += run_test_5();
    test_count++; pass_count += run_test_6();
    test_count++; pass_count += run_test_7();
    test_count++; pass_count += run_test_8();
    test_count++; pass_count += run_test_9();
    test_count++; pass_count += run_test_10();
    test_count++; pass_count += run_test_11();
    test_count++; pass_count += run_test_12();
    test_count++; pass_count += run_test_13();
    test_count++; pass_count += run_test_14();
    test_count++; pass_count += run_test_14c();
#endif

    /* ── Summary ── */
    printf("============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    if (pass_count == test_count) {
        printf("\n  *** FULL SYSTEM VERIFIED ***\n");
        printf("  Layer 1: GIE (GDMA+PARLIO+PCNT) — 428 Hz, ~0 CPU\n");
        printf("  Layer 2: LP core geometric processor — 100 Hz, ~30uA\n");
        printf("  Layer 2b: LP core ternary VDB — NSW graph search (M=%d)\n", VDB_M);
        printf("  Layer 2c: CfC -> VDB pipeline — perceive+think+remember\n");
        printf("  Layer 2d: VDB -> CfC feedback — memory shapes inference\n");
        printf("  Layer 3: HP core — reflex channel coordination\n");
        printf("  Layer 4: ESP-NOW -> GIE live input + pattern classification\n");
        printf("  Layer 5: TriX -> VDB -> LP feedback — memory-modulated attention\n");
        printf("  Layer 6: CMD 4 ablation — VDB contribution isolated from CfC baseline\n");
        printf("  All ternary. No floating point. No multiplication.\n");
        printf("  Perceive → classify → remember → retrieve → modulate.\n");
        printf("  Attribution: CMD 5 vs CMD 4 Hamming delta quantifies VDB contribution.\n\n");
    } else {
        printf("\n  SOME TESTS FAILED — see details above.\n\n");
    }
    fflush(stdout);

    /* ── Diagnostics (not counted in pass/fail) ── */
    run_lp_char();
    run_lp_dot_diag();

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
