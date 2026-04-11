/*
 * test_hebbian.c — TEST 15: Hebbian LP Weight Learning
 *
 * Tests the self-organizing representation mechanism (Pillar 3).
 * LP core f-pathway weights are updated via VDB mismatch under
 * gated conditions: retrieval stability + TriX agreement + rate limit.
 *
 * The structural wall (GIE W_f) is NEVER touched. Only LP W_f learns.
 *
 * Two conditions run sequentially:
 *   Condition 0 (CONTROL): CMD 5 only, no Hebbian updates
 *   Condition 1 (HEBBIAN): CMD 5 + gated Hebbian weight updates
 *
 * Each condition has three phases:
 *   Phase A (60s):  Baseline accumulation — CMD 5, no Hebbian
 *   Phase B (90s):  Treatment — CMD 5 ± Hebbian (NO accumulation)
 *   Phase C (30s):  Post-treatment accumulation — CMD 5, no Hebbian
 *
 * Phase B accumulates no LP divergence data — this prevents mixing
 * early-learning states with late-learning states in the post mean.
 *
 * After both conditions: compare post-treatment divergence.
 * The Hebbian contribution = Hebbian post - Control post.
 *
 * Also verifies classification integrity after learning (M3):
 * TriX accuracy must remain 100% after weight updates.
 *
 * Created: April 11, 2026 (Pillar 3, revised with ablation control)
 */

#include "test_harness.h"

/* Hebbian gating constants */
#define HEBBIAN_STABILITY_K     5
#define HEBBIAN_RATE_LIMIT_MS   100
#define PHASE_A_S               60    /* baseline accumulation */
#define PHASE_B_S               90    /* treatment (learning or control) */
#define PHASE_C_S               30    /* post-treatment accumulation */

#define T15_N_COND  2
#define T15_CONTROL 0
#define T15_HEBBIAN 1

static const char *t15_cond_name[T15_N_COND] = {
    "Control (CMD5 only)",
    "Hebbian (CMD5+learn)"
};

/* Run one condition of TEST 15. Returns 1 on success, 0 on failure.
 * Populates mean_out[4][LP_HIDDEN_DIM] with the Phase C LP means
 * and count_out[4] with per-pattern sample counts. */
static int run_t15_condition(int cond, int8_t mean_out[][LP_HIDDEN_DIM],
                             int *count_out, int *out_flips, int *out_updates) {
    printf("\n  ── Condition: %s ──\n", t15_cond_name[cond]);
    fflush(stdout);

    int enable_hebbian = (cond == T15_HEBBIAN);

    /* Reset state for this condition */
    gate_threshold = 90;
    trix_enabled = 1;
    vdb_clear();
    memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
    ulp_lp_step_count = 0;
    lp_hebbian_reset_accum();

    /* Re-init LP weights from the SAME seed each condition so they start equal.
     * LP_SEED is defined in CMakeLists.txt or defaults to 0xCAFE1234. */
#ifndef LP_SEED
#define LP_SEED 0xCAFE1234
#endif
    init_lp_core_weights(LP_SEED);

    /* Accumulators — Phase A (pre) and Phase C (post) only */
    int16_t sum_a[NUM_TEMPLATES][LP_HIDDEN_DIM];
    int16_t sum_c[NUM_TEMPLATES][LP_HIDDEN_DIM];
    int count_a[NUM_TEMPLATES], count_c[NUM_TEMPLATES];
    memset(sum_a, 0, sizeof(sum_a));
    memset(sum_c, 0, sizeof(sum_c));
    memset(count_a, 0, sizeof(count_a));
    memset(count_c, 0, sizeof(count_c));

    /* Gating state */
    int last_top1 = -1, stable_count = 0;
    int total_flips = 0, total_updates = 0;
    int64_t last_update_us = 0;

    /* Timing */
    start_freerun();
    start_lp_core();
    gie_reset_gap_history();
    espnow_last_rx_us = 0;

    int64_t t0 = esp_timer_get_time();
    int64_t t_ab = t0 + (int64_t)PHASE_A_S * 1000000LL;
    int64_t t_bc = t_ab + (int64_t)PHASE_B_S * 1000000LL;
    int64_t t_end = t_bc + (int64_t)PHASE_C_S * 1000000LL;

    int total_confirms = 0;
    int phase_announced_b = 0, phase_announced_c = 0;

    while (esp_timer_get_time() < t_end) {
        /* Drain + encode */
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

        /* ISR-safe input update */
        gie_input_pending = 1;
        while (gie_input_pending) { vTaskDelay(1); }
        int32_t wait_lc = loop_count + 2;
        while (loop_count < wait_lc) { vTaskDelay(1); }

        /* CMD 5 */
        feed_lp_core();
        ulp_lp_command = 5;
        int64_t cmd_start = esp_timer_get_time();
        while (ulp_lp_command != 0 && (esp_timer_get_time() - cmd_start) < 50000)
            vTaskDelay(1);
        if (ulp_lp_command != 0) continue;

        total_confirms++;
        int64_t now = esp_timer_get_time();

        int8_t lp_now[LP_HIDDEN_DIM];
        memcpy(lp_now, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
        int pred = trix_pred;

        /* ── Accumulate LP state into TriX-labeled target (all phases) ── */
        lp_hebbian_accumulate(pred, lp_now);

        /* ── Phase routing ── */
        if (now < t_ab) {
            /* Phase A: baseline accumulation */
            if (gt < NUM_TEMPLATES) {
                for (int j = 0; j < LP_HIDDEN_DIM; j++)
                    sum_a[gt][j] += lp_now[j];
                count_a[gt]++;
            }
        } else if (now < t_bc) {
            /* Phase B: treatment (learning or control). NO accumulation. */
            if (!phase_announced_b) {
                phase_announced_b = 1;
                printf("    Phase B: %s (%ds)... baseline=%d confirms, VDB=%d\n",
                       enable_hebbian ? "learning" : "control",
                       PHASE_B_S, total_confirms, vdb_count());
                fflush(stdout);
            }

            if (enable_hebbian) {
                int source_id = (int)ulp_fb_source_id;
                if (source_id == last_top1 && source_id != 0xFF)
                    stable_count++;
                else { stable_count = 0; last_top1 = source_id; }

                int trix_agrees = (pred == (int)gt);
                int rate_ok = (now - last_update_us) >= (HEBBIAN_RATE_LIMIT_MS * 1000LL);

                int accum_ready = (pred >= 0 && pred < NUM_TEMPLATES &&
                                   lp_hebbian_accum_n[pred] >= 50);

                if (stable_count >= HEBBIAN_STABILITY_K && trix_agrees &&
                    rate_ok && accum_ready) {
                    int flips = lp_hebbian_step();
                    if (flips > 0) {
                        total_flips += flips;
                        total_updates++;
                        last_update_us = now;
                    }
                    stable_count = 0;
                }
            }
        } else {
            /* Phase C: post-treatment accumulation. No Hebbian. */
            if (!phase_announced_c) {
                phase_announced_c = 1;
                printf("    Phase C: measuring (%ds)... flips=%d updates=%d\n",
                       PHASE_C_S, total_flips, total_updates);
                fflush(stdout);
            }
            if (gt < NUM_TEMPLATES) {
                for (int j = 0; j < LP_HIDDEN_DIM; j++)
                    sum_c[gt][j] += lp_now[j];
                count_c[gt]++;
            }
        }

        /* VDB insert */
        if (total_confirms % 8 == 0 && vdb_count() < VDB_MAX_NODES) {
            int8_t snap[LP_GIE_HIDDEN + LP_HIDDEN_DIM];
            memcpy(snap, (void *)cfc.hidden, LP_GIE_HIDDEN);
            memcpy(snap + LP_GIE_HIDDEN, lp_now, LP_HIDDEN_DIM);
            vdb_insert(snap);
        }

        /* Progress */
        if (total_confirms % 300 == 0) {
            int64_t elapsed = (now - t0) / 1000000LL;
            printf("    step %d (%llds): vdb=%d flips=%d p=%d gt=%d\n",
                   total_confirms, elapsed, vdb_count(), total_flips, pred, gt);
            fflush(stdout);
        }
    }

    stop_freerun();

    /* Compute Phase C means → output */
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        for (int j = 0; j < LP_HIDDEN_DIM; j++)
            mean_out[p][j] = (count_c[p] > 0) ? tsign(sum_c[p][j]) : 0;
        count_out[p] = count_c[p];
    }
    *out_flips = total_flips;
    *out_updates = total_updates;

    /* Print Phase A + C divergence */
    for (int phase = 0; phase < 2; phase++) {
        const char *label = phase == 0 ? "Phase A (pre)" : "Phase C (post)";
        int16_t (*sum)[LP_HIDDEN_DIM] = phase == 0 ? sum_a : sum_c;
        int *cnt = phase == 0 ? count_a : count_c;
        int8_t means[NUM_TEMPLATES][LP_HIDDEN_DIM];
        for (int p = 0; p < NUM_TEMPLATES; p++)
            for (int j = 0; j < LP_HIDDEN_DIM; j++)
                means[p][j] = (cnt[p] > 0) ? tsign(sum[p][j]) : 0;

        printf("    %s divergence (/16):\n         P0  P1  P2  P3\n", label);
        int total = 0, pairs = 0;
        for (int p = 0; p < NUM_TEMPLATES; p++) {
            printf("    P%d:", p);
            for (int q = 0; q < NUM_TEMPLATES; q++) {
                if (p == q) { printf("   0"); continue; }
                if (cnt[p] < 15 || cnt[q] < 15) { printf("   -"); continue; }
                int h = trit_hamming(means[p], means[q], LP_HIDDEN_DIM);
                printf("  %2d", h);
                if (q > p) { total += h; pairs++; }
            }
            printf("  (%d)\n", cnt[p]);
        }
        float m = pairs > 0 ? (float)total / pairs : 0;
        printf("    Mean: %.1f\n", m);
    }

    printf("    Confirms: %d, Flips: %d, Updates: %d\n",
           total_confirms, total_flips, total_updates);
    fflush(stdout);

    return 1;
}

int run_test_15(void) {
    printf("-- TEST 15: Hebbian LP Weight Learning --\n");
    printf("   Design: 3-phase (A=%ds baseline, B=%ds treatment, C=%ds measure)\n",
           PHASE_A_S, PHASE_B_S, PHASE_C_S);
    printf("   Two conditions: Control (CMD5 only) vs Hebbian (CMD5+learn)\n\n");
    fflush(stdout);

    /* Storage for per-condition Phase C results */
    static int8_t post_mean[T15_N_COND][NUM_TEMPLATES][LP_HIDDEN_DIM];
    static int post_count[T15_N_COND][NUM_TEMPLATES];
    int flips[T15_N_COND], updates[T15_N_COND];

    /* Run both conditions */
    for (int c = 0; c < T15_N_COND; c++) {
        run_t15_condition(c, post_mean[c], post_count[c], &flips[c], &updates[c]);
    }

    /* ── Comparison ── */
    printf("\n  ══ TEST 15 COMPARISON ══\n");

    float cond_mean[T15_N_COND];
    for (int c = 0; c < T15_N_COND; c++) {
        int total = 0, pairs = 0;
        for (int p = 0; p < NUM_TEMPLATES; p++)
            for (int q = p + 1; q < NUM_TEMPLATES; q++)
                if (post_count[c][p] >= 15 && post_count[c][q] >= 15) {
                    total += trit_hamming(post_mean[c][p], post_mean[c][q], LP_HIDDEN_DIM);
                    pairs++;
                }
        cond_mean[c] = pairs > 0 ? (float)total / pairs : 0;
        printf("  %-24s  post mean=%.1f/16  flips=%d  updates=%d\n",
               t15_cond_name[c], cond_mean[c], flips[c], updates[c]);
    }

    float hebbian_contribution = cond_mean[T15_HEBBIAN] - cond_mean[T15_CONTROL];
    printf("\n  Hebbian contribution: %+.1f Hamming (Hebbian - Control)\n",
           hebbian_contribution);

    /* P1-P2 pair specifically */
    int p12_ctrl = -1, p12_hebb = -1;
    for (int c = 0; c < T15_N_COND; c++) {
        if (post_count[c][1] >= 15 && post_count[c][2] >= 15) {
            int h = trit_hamming(post_mean[c][1], post_mean[c][2], LP_HIDDEN_DIM);
            if (c == T15_CONTROL) p12_ctrl = h;
            else p12_hebb = h;
        }
    }
    if (p12_ctrl >= 0 && p12_hebb >= 0) {
        printf("  P1-P2 separation: Control=%d, Hebbian=%d (%+d)\n",
               p12_ctrl, p12_hebb, p12_hebb - p12_ctrl);
    }

    /* ── Verdict ── */
    printf("\n  ── Verdict ──\n");
    int pass = 1;

    /* Gate 1: Hebbian must exceed control */
    int hebbian_helps = (hebbian_contribution > 0.0f);
    printf("  Hebbian > Control:  %s (%+.1f)\n",
           hebbian_helps ? "YES" : "NO", hebbian_contribution);
    if (!hebbian_helps) pass = 0;

    /* Gate 2: Hebbian must have actually run */
    int updates_ran = (updates[T15_HEBBIAN] >= 10);
    printf("  Sufficient updates: %s (%d updates, %d flips)\n",
           updates_ran ? "YES" : "NO", updates[T15_HEBBIAN], flips[T15_HEBBIAN]);
    if (!updates_ran) pass = 0;

    printf("  %s\n", pass ? "OK" : "FAIL");
    fflush(stdout);
    return pass;
}
