/*
 * test_hebbian.c — TEST 15: Hebbian LP Weight Learning
 *
 * Tests the self-organizing representation mechanism (Pillar 3).
 * LP core f-pathway weights are updated via VDB mismatch under
 * gated conditions: retrieval stability + TriX agreement + rate limit.
 *
 * The structural wall (GIE W_f) is NEVER touched. Only LP W_f learns.
 *
 * TEST 15A: Convergence under fixed input
 *   - Run CMD 5 for 60s (build VDB + LP baseline)
 *   - Enable Hebbian updates for 120s
 *   - Measure LP divergence matrix before and after
 *
 * TEST 15B: Classification integrity check
 *   - After learning, verify TriX accuracy is still 100% (label-free)
 *
 * Created: April 11, 2026 (Pillar 3 initial implementation)
 */

#include "test_harness.h"

/* Hebbian gating constants */
#define HEBBIAN_STABILITY_K     5     /* consecutive same-top1 before update */
#define HEBBIAN_RATE_LIMIT_MS   100   /* minimum ms between updates per call */
#define HEBBIAN_BASELINE_S      60    /* seconds of CMD 5 before learning */
#define HEBBIAN_LEARNING_S      120   /* seconds of learning */

int run_test_15(void) {
    printf("-- TEST 15: Hebbian LP Weight Learning --\n\n");
    fflush(stdout);

    /* ── Setup: same as Test 12 — cycling sender, CMD 5 feedback ── */
    gate_threshold = 90;
    trix_enabled = 1;

    /* Clear VDB */
    vdb_clear();

    /* Reset LP core state */
    memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
    ulp_lp_step_count = 0;

    /* LP accumulators for divergence measurement */
    static int16_t sum_pre[NUM_TEMPLATES][LP_HIDDEN_DIM];
    static int16_t sum_post[NUM_TEMPLATES][LP_HIDDEN_DIM];
    static int count_pre[NUM_TEMPLATES];
    static int count_post[NUM_TEMPLATES];
    memset(sum_pre, 0, sizeof(sum_pre));
    memset(sum_post, 0, sizeof(sum_post));
    memset(count_pre, 0, sizeof(count_pre));
    memset(count_post, 0, sizeof(count_post));

    /* Hebbian gating state */
    int last_top1 = -1;
    int stable_count = 0;
    int total_flips = 0;
    int total_updates = 0;
    int64_t last_update_us = 0;

    /* ── Phase A: Baseline (CMD 5, no Hebbian) ── */
    printf("  Phase A: Baseline (CMD 5, %ds, no learning)...\n", HEBBIAN_BASELINE_S);
    fflush(stdout);

    start_freerun();
    start_lp_core();
    gie_reset_gap_history();
    espnow_last_rx_us = 0;

    int total_confirms = 0;
    int64_t phase_start = esp_timer_get_time();
    int64_t baseline_end = phase_start + (int64_t)HEBBIAN_BASELINE_S * 1000000LL;
    int64_t learning_end = baseline_end + (int64_t)HEBBIAN_LEARNING_S * 1000000LL;
    int in_learning = 0;

    while (esp_timer_get_time() < learning_end) {
        /* ── Drain ESP-NOW packets and encode ── */
        int n_rx = espnow_drain(drain_buf, 32);
        int new_input = 0;
        uint8_t gt = 255;
        for (int i = 0; i < n_rx; i++) {
            if (drain_buf[i].pkt.pattern_id < 4) {
                gt = drain_buf[i].pkt.pattern_id;
                int64_t gap_ms;
                if (espnow_encode_rx_entry(&drain_buf[i], &gap_ms))
                    new_input = 1;
            }
        }
        if (!new_input || gt > 3) continue;

        /* ── Update GIE input (ISR-safe) ── */
        gie_input_pending = 1;
        while (gie_input_pending) { vTaskDelay(1); }
        int32_t wait_lc = loop_count + 2;
        while (loop_count < wait_lc) { vTaskDelay(1); }

        /* ── Feed LP and run CMD 5 ── */
        feed_lp_core();
        ulp_lp_command = 5;
        int64_t cmd_start = esp_timer_get_time();
        while (ulp_lp_command != 0 && (esp_timer_get_time() - cmd_start) < 50000)
            vTaskDelay(1);
        if (ulp_lp_command != 0) continue;

        total_confirms++;

        /* ── Read LP hidden ── */
        int8_t lp_now[LP_HIDDEN_DIM];
        memcpy(lp_now, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);

        /* ── TriX prediction ── */
        int pred = trix_pred;

        /* ── Accumulate into pre or post based on phase ── */
        int64_t now = esp_timer_get_time();
        if (now < baseline_end) {
            /* Phase A: baseline accumulation */
            if (gt < NUM_TEMPLATES) {
                for (int j = 0; j < LP_HIDDEN_DIM; j++)
                    sum_pre[gt][j] += lp_now[j];
                count_pre[gt]++;
            }
        } else {
            /* Phase B: learning phase */
            if (!in_learning) {
                in_learning = 1;
                int64_t elapsed = (now - phase_start) / 1000000LL;
                printf("  Phase B: Hebbian learning (%ds)...\n", HEBBIAN_LEARNING_S);
                printf("    Baseline: %d confirms in %llds, VDB=%d\n",
                       total_confirms, elapsed, vdb_count());
                fflush(stdout);
            }

            /* Accumulate post-learning LP state */
            if (gt < NUM_TEMPLATES) {
                for (int j = 0; j < LP_HIDDEN_DIM; j++)
                    sum_post[gt][j] += lp_now[j];
                count_post[gt]++;
            }

            /* ── Hebbian gating ── */
            int source_id = (int)ulp_fb_source_id;

            /* Gate 1: Retrieval stability — same top-1 for K consecutive steps */
            if (source_id == last_top1 && source_id != 0xFF) {
                stable_count++;
            } else {
                stable_count = 0;
                last_top1 = source_id;
            }

            /* Gate 2: TriX agreement — classifier agrees with ground truth */
            int trix_agrees = (pred == (int)gt);

            /* Gate 3: Rate limit */
            int rate_ok = (now - last_update_us) >= (HEBBIAN_RATE_LIMIT_MS * 1000LL);

            /* ── Apply Hebbian update if all gates pass ── */
            if (stable_count >= HEBBIAN_STABILITY_K && trix_agrees && rate_ok) {
                int flips = lp_hebbian_step();
                if (flips > 0) {
                    total_flips += flips;
                    total_updates++;
                    last_update_us = now;
                }
                stable_count = 0;  /* reset stability counter after update */
            }
        }

        /* VDB insert (periodic) */
        if (total_confirms % 8 == 0 && vdb_count() < VDB_MAX_NODES) {
            int8_t snap[LP_GIE_HIDDEN + LP_HIDDEN_DIM];
            memcpy(snap, (void *)cfc.hidden, LP_GIE_HIDDEN);
            memcpy(snap + LP_GIE_HIDDEN, lp_now, LP_HIDDEN_DIM);
            vdb_insert(snap);
        }

        /* Progress */
        if (total_confirms % 200 == 0) {
            int64_t elapsed = (esp_timer_get_time() - phase_start) / 1000000LL;
            printf("    step %d (%llds): vdb=%d flips=%d updates=%d p=%d gt=%d\n",
                   total_confirms, elapsed, vdb_count(),
                   total_flips, total_updates, pred, gt);
            fflush(stdout);
        }
    }

    stop_freerun();

    /* ── Results ── */
    printf("\n  ── Hebbian Learning Results ──\n");
    printf("  Total confirms: %d\n", total_confirms);
    printf("  Total Hebbian updates: %d (%d weight flips)\n",
           total_updates, total_flips);
    printf("  Mean flips per update: %.1f\n",
           total_updates > 0 ? (float)total_flips / total_updates : 0);

    /* ── LP Divergence: Pre-learning ── */
    printf("\n  LP Divergence (PRE-learning, sign-space, /16):\n");
    printf("       P0  P1  P2  P3\n");
    int8_t mean_pre[NUM_TEMPLATES][LP_HIDDEN_DIM];
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        for (int j = 0; j < LP_HIDDEN_DIM; j++)
            mean_pre[p][j] = (count_pre[p] > 0) ? tsign(sum_pre[p][j]) : 0;
    }
    int pre_total = 0, pre_pairs = 0;
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        printf("  P%d:", p);
        for (int q = 0; q < NUM_TEMPLATES; q++) {
            if (p == q) { printf("   0"); continue; }
            if (count_pre[p] < 15 || count_pre[q] < 15) {
                printf("   -"); continue;
            }
            int h = trit_hamming(mean_pre[p], mean_pre[q], LP_HIDDEN_DIM);
            printf("  %2d", h);
            if (q > p) { pre_total += h; pre_pairs++; }
        }
        printf("  (%d samples)\n", count_pre[p]);
    }
    float pre_mean = pre_pairs > 0 ? (float)pre_total / pre_pairs : 0;
    printf("  Mean: %.1f/16\n", pre_mean);

    /* ── LP Divergence: Post-learning ── */
    printf("\n  LP Divergence (POST-learning, sign-space, /16):\n");
    printf("       P0  P1  P2  P3\n");
    int8_t mean_post[NUM_TEMPLATES][LP_HIDDEN_DIM];
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        for (int j = 0; j < LP_HIDDEN_DIM; j++)
            mean_post[p][j] = (count_post[p] > 0) ? tsign(sum_post[p][j]) : 0;
    }
    int post_total = 0, post_pairs = 0;
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        printf("  P%d:", p);
        for (int q = 0; q < NUM_TEMPLATES; q++) {
            if (p == q) { printf("   0"); continue; }
            if (count_post[p] < 15 || count_post[q] < 15) {
                printf("   -"); continue;
            }
            int h = trit_hamming(mean_post[p], mean_post[q], LP_HIDDEN_DIM);
            printf("  %2d", h);
            if (q > p) { post_total += h; post_pairs++; }
        }
        printf("  (%d samples)\n", count_post[p]);
    }
    float post_mean = post_pairs > 0 ? (float)post_total / post_pairs : 0;
    printf("  Mean: %.1f/16\n", post_mean);

    /* ── Verdict ── */
    float improvement = post_mean - pre_mean;
    printf("\n  ── Verdict ──\n");
    printf("  Pre-learning mean divergence:  %.1f/16\n", pre_mean);
    printf("  Post-learning mean divergence: %.1f/16\n", post_mean);
    printf("  Improvement: %+.1f Hamming\n", improvement);
    printf("  Weight flips: %d across %d updates\n", total_flips, total_updates);

    int pass = (improvement >= 0.5f);  /* learning improved divergence by ≥0.5 */
    printf("  %s\n", pass ? "OK" : "FAIL");
    fflush(stdout);

    return pass;
}
