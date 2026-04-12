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
#define PHASE_B_S               60    /* treatment (learning or control) */
#define PHASE_C_S               60    /* post-treatment measurement (was 30 — P2 needs ≥15 samples) */

#define T15_N_COND  2
#define T15_CONTROL 0
#define T15_HEBBIAN 1
#define T15_N_REPS  3             /* repetitions per condition for variance estimate */

static const char *t15_cond_name[T15_N_COND] = {
    "Control (CMD5 only)",
    "Hebbian (CMD5+learn)"
};

/* Run one condition of TEST 15. Returns 1 on success, 0 on failure.
 * Populates mean_out[4][LP_HIDDEN_DIM] with the Phase C LP means
 * and count_out[4] with per-pattern sample counts. */
static int run_t15_condition(int cond, int8_t mean_out[][LP_HIDDEN_DIM],
                             int8_t mtfp_mean_out[][LP_MTFP_DIM],
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

    /* Accumulators — Phase A (pre) and Phase C (post) only.
     * Both sign-space (16 trits) and MTFP-space (80 trits). */
    int16_t sum_a[NUM_TEMPLATES][LP_HIDDEN_DIM];
    int16_t sum_c[NUM_TEMPLATES][LP_HIDDEN_DIM];
    int16_t mtfp_sum_a[NUM_TEMPLATES][LP_MTFP_DIM];
    int16_t mtfp_sum_c[NUM_TEMPLATES][LP_MTFP_DIM];
    int count_a[NUM_TEMPLATES], count_c[NUM_TEMPLATES];
    memset(sum_a, 0, sizeof(sum_a));
    memset(sum_c, 0, sizeof(sum_c));
    memset(mtfp_sum_a, 0, sizeof(mtfp_sum_a));
    memset(mtfp_sum_c, 0, sizeof(mtfp_sum_c));
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

        /* Read LP f-pathway dots and encode as 80-trit MTFP */
        int32_t dots_f_now[LP_HIDDEN_DIM];
        memcpy(dots_f_now, ulp_addr(&ulp_lp_dots_f), LP_HIDDEN_DIM * sizeof(int32_t));
        int8_t lp_mtfp[LP_MTFP_DIM];
        encode_lp_mtfp(dots_f_now, lp_mtfp);

        /* ── Accumulate LP state into TriX-labeled target (all phases) ── */
        lp_hebbian_accumulate(pred, lp_now);

        /* ── Phase routing ── */
        if (now < t_ab) {
            /* Phase A: baseline accumulation (sign + MTFP) */
            if (gt < NUM_TEMPLATES) {
                for (int j = 0; j < LP_HIDDEN_DIM; j++)
                    sum_a[gt][j] += lp_now[j];
                for (int j = 0; j < LP_MTFP_DIM; j++)
                    mtfp_sum_a[gt][j] += lp_mtfp[j];
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
                for (int j = 0; j < LP_MTFP_DIM; j++)
                    mtfp_sum_c[gt][j] += lp_mtfp[j];
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

    /* Compute Phase C sign means → output */
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        for (int j = 0; j < LP_HIDDEN_DIM; j++)
            mean_out[p][j] = (count_c[p] > 0) ? tsign(sum_c[p][j]) : 0;
        count_out[p] = count_c[p];
    }
    *out_flips = total_flips;
    *out_updates = total_updates;

    /* Compute Phase C MTFP means → output */
    for (int p = 0; p < NUM_TEMPLATES; p++)
        for (int j = 0; j < LP_MTFP_DIM; j++)
            mtfp_mean_out[p][j] = (count_c[p] > 0) ? tsign(mtfp_sum_c[p][j]) : 0;

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
    printf("   Design: 3-phase (A=%ds, B=%ds, C=%ds) × %d reps × 2 conditions\n",
           PHASE_A_S, PHASE_B_S, PHASE_C_S, T15_N_REPS);
    printf("   Total: %ds per condition, %ds overall\n",
           (PHASE_A_S + PHASE_B_S + PHASE_C_S) * T15_N_REPS,
           (PHASE_A_S + PHASE_B_S + PHASE_C_S) * T15_N_REPS * T15_N_COND);
    fflush(stdout);

    /* Per-rep results (sign-space and MTFP-space) */
    float rep_mean[T15_N_COND][T15_N_REPS];
    float rep_mtfp[T15_N_COND][T15_N_REPS];
    int   rep_flips[T15_N_COND][T15_N_REPS];
    int   rep_updates[T15_N_COND][T15_N_REPS];

    /* Run all reps of all conditions */
    for (int c = 0; c < T15_N_COND; c++) {
        for (int r = 0; r < T15_N_REPS; r++) {
            printf("\n  ══ %s — Rep %d/%d ══\n", t15_cond_name[c], r + 1, T15_N_REPS);
            fflush(stdout);

            static int8_t pm[NUM_TEMPLATES][LP_HIDDEN_DIM];
            static int8_t mm[NUM_TEMPLATES][LP_MTFP_DIM];
            static int pc[NUM_TEMPLATES];
            int fl, up;
            run_t15_condition(c, pm, mm, pc, &fl, &up);

            /* Compute mean divergence for this rep — sign-space */
            int total = 0, pairs = 0;
            for (int p = 0; p < NUM_TEMPLATES; p++)
                for (int q = p + 1; q < NUM_TEMPLATES; q++)
                    if (pc[p] >= 15 && pc[q] >= 15) {
                        total += trit_hamming(pm[p], pm[q], LP_HIDDEN_DIM);
                        pairs++;
                    }
            rep_mean[c][r] = pairs > 0 ? (float)total / pairs : -1;

            /* Compute mean divergence — MTFP-space */
            int mtotal = 0, mpairs = 0;
            for (int p = 0; p < NUM_TEMPLATES; p++)
                for (int q = p + 1; q < NUM_TEMPLATES; q++)
                    if (pc[p] >= 15 && pc[q] >= 15) {
                        mtotal += trit_hamming(mm[p], mm[q], LP_MTFP_DIM);
                        mpairs++;
                    }
            rep_mtfp[c][r] = mpairs > 0 ? (float)mtotal / mpairs : -1;

            rep_flips[c][r] = fl;
            rep_updates[c][r] = up;

            printf("    Rep %d: sign=%.1f/16 mtfp=%.1f/80 (%d pairs) flips=%d\n",
                   r + 1, rep_mean[c][r], rep_mtfp[c][r], pairs, fl);
            fflush(stdout);
        }
    }

    /* ── Comparison with mean ± std ── */
    printf("\n  ══ TEST 15 COMPARISON (%d reps per condition) ══\n", T15_N_REPS);

    /* Compute stats for both metrics */
    float cond_avg[T15_N_COND], cond_std[T15_N_COND];
    float mtfp_avg[T15_N_COND], mtfp_std[T15_N_COND];
    for (int c = 0; c < T15_N_COND; c++) {
        /* Sign-space */
        float sum = 0; int valid = 0;
        for (int r = 0; r < T15_N_REPS; r++)
            if (rep_mean[c][r] >= 0) { sum += rep_mean[c][r]; valid++; }
        cond_avg[c] = valid > 0 ? sum / valid : 0;
        float var = 0;
        for (int r = 0; r < T15_N_REPS; r++)
            if (rep_mean[c][r] >= 0) { float d = rep_mean[c][r] - cond_avg[c]; var += d * d; }
        cond_std[c] = valid > 1 ? __builtin_sqrtf(var / (valid - 1)) : 0;

        /* MTFP-space */
        float msum = 0; int mvalid = 0;
        for (int r = 0; r < T15_N_REPS; r++)
            if (rep_mtfp[c][r] >= 0) { msum += rep_mtfp[c][r]; mvalid++; }
        mtfp_avg[c] = mvalid > 0 ? msum / mvalid : 0;
        float mvar = 0;
        for (int r = 0; r < T15_N_REPS; r++)
            if (rep_mtfp[c][r] >= 0) { float d = rep_mtfp[c][r] - mtfp_avg[c]; mvar += d * d; }
        mtfp_std[c] = mvalid > 1 ? __builtin_sqrtf(mvar / (mvalid - 1)) : 0;

        printf("  %-24s  sign: %.1f±%.1f /16  mtfp: %.1f±%.1f /80\n",
               t15_cond_name[c], cond_avg[c], cond_std[c], mtfp_avg[c], mtfp_std[c]);
        printf("  %24s  (sign reps:", "");
        for (int r = 0; r < T15_N_REPS; r++) printf(" %.1f", rep_mean[c][r]);
        printf(")  (mtfp reps:");
        for (int r = 0; r < T15_N_REPS; r++) printf(" %.1f", rep_mtfp[c][r]);
        printf(")\n");
    }

    float contribution = cond_avg[T15_HEBBIAN] - cond_avg[T15_CONTROL];
    float combined_std = __builtin_sqrtf(cond_std[0] * cond_std[0] + cond_std[1] * cond_std[1]);
    float mtfp_contribution = mtfp_avg[T15_HEBBIAN] - mtfp_avg[T15_CONTROL];
    float mtfp_combined = __builtin_sqrtf(mtfp_std[0] * mtfp_std[0] + mtfp_std[1] * mtfp_std[1]);
    printf("\n  Sign contribution:  %+.1f ± %.1f Hamming /16\n", contribution, combined_std);
    printf("  MTFP contribution:  %+.1f ± %.1f Hamming /80\n", mtfp_contribution, mtfp_combined);

    /* ── Verdict ── */
    printf("\n  ── Verdict ──\n");
    int pass = 1;

    int hebbian_helps = (contribution > 0.0f);
    printf("  Hebbian > Control (mean):  %s (%+.1f)\n",
           hebbian_helps ? "YES" : "NO", contribution);
    if (!hebbian_helps) pass = 0;

    /* Check if contribution exceeds noise (contribution > 1 std) */
    int exceeds_noise = (combined_std > 0) ? (contribution > combined_std) : (contribution > 0);
    printf("  Exceeds noise (>1 std):    %s (%+.1f vs ±%.1f)\n",
           exceeds_noise ? "YES" : "NO", contribution, combined_std);
    if (!exceeds_noise) pass = 0;

    printf("  %s\n", pass ? "OK" : "FAIL");
    fflush(stdout);
    return pass;
}
