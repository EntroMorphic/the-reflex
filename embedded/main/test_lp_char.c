/*
 * test_lp_char.c — LP Characterization + Dot Magnitude Diagnostic
 *
 * Split from geometry_cfc_freerun.c: April 8, 2026 (audit remediation).
 * run_lp_char():    LP dynamics baseline measurement (Path A vs B).
 * run_lp_dot_diag(): Answers whether P1 and P2 produce different LP
 *                    dot magnitudes (they do — sign() is the bottleneck).
 *
 * These are diagnostics, not pass/fail tests. They run after the
 * main test suite and are not counted in the pass/fail tally.
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * ESP-IDF: v5.4
 */

#include "test_harness.h"

void run_lp_char(void) {
    /* ══════════════════════════════════════════════════════════════
     *  LP CHARACTERIZATION — LP Dynamics Baseline Measurement
     *
     *  Answers the root question from open_risks_synth.md:
     *  "What is the dominant LP update path on hardware?"
     *
     *  Path A (CfC firing):  LP neuron updates when |dot_f| > 0.
     *                        Rate = mean non-zero lp_dots_f entries per step.
     *  Path B (VDB blend):   VDB nearest-neighbor blended into lp_hidden.
     *                        Rate = mean fb_blend_count per step.
     *
     *  Protocol (no gate bias — 14A baseline condition):
     *    Phase 1: gie_hidden = P1_synthetic (LC_P1_STEPS steps, ~5s)
     *    Phase 2: gie_hidden = P2_synthetic (LC_P2_STEPS steps, ~1s)
     *    Repeat LC_REPS times. VDB seeded with P1 snapshots every
     *    LC_INSERT_EVERY steps.
     *
     *  Decision criteria (per open_risks_synth.md):
     *    Path A dominant:   mean lp_cfc_fired > 2 neurons/step
     *    Implied alpha:     mean(fb_blend_count) / LP_HIDDEN_DIM per step
     *    Path B convergence: lp_hidden change profile over Phase 2 steps 1-15
     *
     *  Not a pass/fail test — diagnostic only.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- LP CHAR: LP Dynamics Characterization --\n");
    printf("   (diagnostic — not counted in pass/fail)\n");
    fflush(stdout);
    {
        #define LC_P1_STEPS     500   /* ~5s at 100 Hz LP step rate */
        #define LC_P2_STEPS     100   /* ~1s: captures LP response to P1→P2 switch */
        #define LC_REPS           3   /* repetitions for consistency check */
        #define LC_INSERT_EVERY  25   /* VDB insert every N P1 steps */
        #define LC_WIN_PRE       20   /* switch-window steps before switch */
        #define LC_WIN_POST      30   /* switch-window steps after switch */
        #define LC_WIN_TOTAL     (LC_WIN_PRE + LC_WIN_POST)

        /* Synthetic GIE hidden states: two orthogonal 32-dim ternary vectors.
         * P1: first half = +1, second half = 0
         * P2: first half =  0, second half = +1
         * Maximally orthogonal — dot(P1,P2) = 0 by construction. */
        int8_t lc_gie_p1[LP_GIE_HIDDEN];
        int8_t lc_gie_p2[LP_GIE_HIDDEN];
        memset(lc_gie_p1, 0, LP_GIE_HIDDEN);
        memset(lc_gie_p2, 0, LP_GIE_HIDDEN);
        for (int i = 0; i < LP_GIE_HIDDEN / 2; i++) lc_gie_p1[i] = 1;
        for (int i = LP_GIE_HIDDEN / 2; i < LP_GIE_HIDDEN; i++) lc_gie_p2[i] = 1;

        /* Per-rep accumulators */
        float rep_fires_p1[LC_REPS], rep_fires_p2[LC_REPS];
        float rep_blend_p1[LC_REPS], rep_blend_p2[LC_REPS];
        memset(rep_fires_p1, 0, sizeof(rep_fires_p1));
        memset(rep_fires_p2, 0, sizeof(rep_fires_p2));
        memset(rep_blend_p1, 0, sizeof(rep_blend_p1));
        memset(rep_blend_p2, 0, sizeof(rep_blend_p2));

        /* Switch-window log from last rep (stack allocation) */
        int8_t  win_lp_h[LC_WIN_TOTAL][LP_HIDDEN_DIM];
        int     win_fires[LC_WIN_TOTAL];
        int     win_blend[LC_WIN_TOTAL];
        int     win_score[LC_WIN_TOTAL];
        int     win_applied[LC_WIN_TOTAL];
        memset(win_lp_h,   0, sizeof(win_lp_h));
        memset(win_fires,  0, sizeof(win_fires));
        memset(win_blend,  0, sizeof(win_blend));
        memset(win_score,  0, sizeof(win_score));
        memset(win_applied,0, sizeof(win_applied));

        for (int rep = 0; rep < LC_REPS; rep++) {
            printf("  Rep %d/%d:\n", rep + 1, LC_REPS);
            fflush(stdout);

            /* Reset LP and VDB state for clean observation */
            vdb_clear();
            memset(ulp_addr(&ulp_lp_hidden), 0, LP_HIDDEN_DIM);
            ulp_fb_total_blends = 0;
            /* Low threshold: apply feedback for any positive VDB match score.
             * Maximises Path B observability during characterisation. */
            ulp_fb_threshold = 1;

            int64_t fires_p1 = 0, fires_p2 = 0;
            int64_t blend_p1 = 0, blend_p2 = 0;

            /* ── Phase 1: P1 synthetic for LC_P1_STEPS steps ── */
            for (int step = 0; step < LC_P1_STEPS; step++) {
                memcpy(ulp_addr(&ulp_gie_hidden), lc_gie_p1, LP_GIE_HIDDEN);
                vdb_result_t lc_r;
                if (vdb_cfc_feedback_step(&lc_r) != 0) continue;

                /* Path A: count non-zero f-pathway dots */
                int32_t dots_f[LP_HIDDEN_DIM];
                memcpy(dots_f, ulp_addr(&ulp_lp_dots_f),
                       LP_HIDDEN_DIM * sizeof(int32_t));
                int fires = 0;
                for (int n = 0; n < LP_HIDDEN_DIM; n++)
                    if (dots_f[n] != 0) fires++;

                /* Path B: VDB blend count and score */
                int blend   = (int)ulp_fb_blend_count;
                int score   = (int)ulp_fb_score;
                int applied = (int)ulp_fb_applied;

                fires_p1 += fires;
                blend_p1 += blend;

                /* Periodic VDB seeding: build episodic memory of P1 state */
                if ((step % LC_INSERT_EVERY) == 0 &&
                    vdb_count() < VDB_MAX_NODES) {
                    int8_t snap[VDB_TRIT_DIM];
                    memcpy(snap, lc_gie_p1, LP_GIE_HIDDEN);
                    memcpy(snap + LP_GIE_HIDDEN,
                           ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
                    vdb_insert(snap);
                }

                /* Record switch window: last LC_WIN_PRE steps of Phase 1 */
                int wi = step - (LC_P1_STEPS - LC_WIN_PRE);
                if (wi >= 0 && wi < LC_WIN_PRE) {
                    memcpy(win_lp_h[wi], ulp_addr(&ulp_lp_hidden),
                           LP_HIDDEN_DIM);
                    win_fires[wi]   = fires;
                    win_blend[wi]   = blend;
                    win_score[wi]   = score;
                    win_applied[wi] = applied;
                }

                if (step % 100 == 0) {
                    printf("    P1 step %3d: fires=%2d blend=%2d score=%3d vdb=%d\n",
                           step, fires, blend, score, vdb_count());
                    fflush(stdout);
                }
            }

            printf("    P1 done: vdb_nodes=%d\n", vdb_count());
            fflush(stdout);

            /* ── Phase 2: switch to P2 synthetic for LC_P2_STEPS steps ── */
            for (int step = 0; step < LC_P2_STEPS; step++) {
                memcpy(ulp_addr(&ulp_gie_hidden), lc_gie_p2, LP_GIE_HIDDEN);
                vdb_result_t lc_r;
                if (vdb_cfc_feedback_step(&lc_r) != 0) continue;

                int32_t dots_f[LP_HIDDEN_DIM];
                memcpy(dots_f, ulp_addr(&ulp_lp_dots_f),
                       LP_HIDDEN_DIM * sizeof(int32_t));
                int fires = 0;
                for (int n = 0; n < LP_HIDDEN_DIM; n++)
                    if (dots_f[n] != 0) fires++;

                int blend   = (int)ulp_fb_blend_count;
                int score   = (int)ulp_fb_score;
                int applied = (int)ulp_fb_applied;

                fires_p2 += fires;
                blend_p2 += blend;

                /* Record switch window: first LC_WIN_POST steps of Phase 2 */
                int wi = LC_WIN_PRE + step;
                if (wi < LC_WIN_TOTAL) {
                    memcpy(win_lp_h[wi], ulp_addr(&ulp_lp_hidden),
                           LP_HIDDEN_DIM);
                    win_fires[wi]   = fires;
                    win_blend[wi]   = blend;
                    win_score[wi]   = score;
                    win_applied[wi] = applied;
                }

                printf("    P2 step %2d: fires=%2d blend=%2d score=%3d applied=%d\n",
                       step, fires, blend, score, applied);
                fflush(stdout);
            }

            rep_fires_p1[rep] = (float)fires_p1 / LC_P1_STEPS;
            rep_blend_p1[rep] = (float)blend_p1 / LC_P1_STEPS;
            rep_fires_p2[rep] = (float)fires_p2 / LC_P2_STEPS;
            rep_blend_p2[rep] = (float)blend_p2 / LC_P2_STEPS;

            printf("    P1 mean: fires/step=%.1f  blend/step=%.1f  "
                   "implied_alpha=%.3f\n",
                   rep_fires_p1[rep], rep_blend_p1[rep],
                   rep_blend_p1[rep] / LP_HIDDEN_DIM);
            printf("    P2 mean: fires/step=%.1f  blend/step=%.1f  "
                   "implied_alpha=%.3f\n\n",
                   rep_fires_p2[rep], rep_blend_p2[rep],
                   rep_blend_p2[rep] / LP_HIDDEN_DIM);
            fflush(stdout);
        }

        /* ── Switch-window trajectory (from last rep) ── */
        printf("  Switch window (rep %d, last %d P1 → first %d P2):\n",
               LC_REPS, LC_WIN_PRE, LC_WIN_POST);
        printf("  step | fires | blend | score | appl | lp_hidden\n");
        printf("  -----|-------|-------|-------|------|----------\n");
        for (int i = 0; i < LC_WIN_TOTAL; i++) {
            int rel = i - LC_WIN_PRE;
            char lp_str[LP_HIDDEN_DIM + 1];
            for (int n = 0; n < LP_HIDDEN_DIM; n++) {
                int8_t v = win_lp_h[i][n];
                lp_str[n] = (v > 0) ? '+' : (v < 0) ? '-' : '0';
            }
            lp_str[LP_HIDDEN_DIM] = '\0';
            printf("  %+4d | %5d | %5d | %5d | %4d | %s\n",
                   rel, win_fires[i], win_blend[i],
                   win_score[i], win_applied[i], lp_str);
        }
        fflush(stdout);

        /* ── Overall summary ── */
        float mean_fires_p1 = 0, mean_blend_p1 = 0;
        float mean_fires_p2 = 0, mean_blend_p2 = 0;
        for (int r = 0; r < LC_REPS; r++) {
            mean_fires_p1 += rep_fires_p1[r];
            mean_blend_p1 += rep_blend_p1[r];
            mean_fires_p2 += rep_fires_p2[r];
            mean_blend_p2 += rep_blend_p2[r];
        }
        mean_fires_p1 /= LC_REPS;
        mean_blend_p1 /= LC_REPS;
        mean_fires_p2 /= LC_REPS;
        mean_blend_p2 /= LC_REPS;

        float implied_alpha_p1 = mean_blend_p1 / LP_HIDDEN_DIM;

        printf("\n  SUMMARY (%d reps, P1=%d steps, P2=%d steps):\n",
               LC_REPS, LC_P1_STEPS, LC_P2_STEPS);
        printf("  P1 steady: fires/step=%.1f  blend/step=%.1f  "
               "implied_alpha=%.3f\n",
               mean_fires_p1, mean_blend_p1, implied_alpha_p1);
        printf("  P2 phase:  fires/step=%.1f  blend/step=%.1f  "
               "implied_alpha=%.3f\n",
               mean_fires_p2, mean_blend_p2,
               mean_blend_p2 / LP_HIDDEN_DIM);
        printf("  REGIME: %s\n",
               mean_fires_p1 > 2.0f
                   ? "Path A dominant (CfC fires > 2/step)"
               : mean_fires_p1 > 0.5f
                   ? "Mixed (0.5 < fires/step <= 2)"
               : implied_alpha_p1 > 0.1f
                   ? "Path B dominant (VDB blend primary)"
                   : "Both paths weak — extend steps or check VDB seeding");
        printf("  Sim calibration: LP_SIM_THRESHOLD should match Path A "
               "threshold; BLEND_ALPHA = %.3f\n\n", implied_alpha_p1);
        fflush(stdout);
    }
}

void run_lp_dot_diag(void) {
    /* ══════════════════════════════════════════════════════════════
     *  LP DOT MAGNITUDE DIAGNOSTIC
     *
     *  Answers: do P1 and P2 produce different LP dot MAGNITUDES
     *  even when sign(dot) is the same? If yes, the LP degeneracy
     *  is in sign() quantization, and MTFP per-neuron encoding
     *  (5 trits/neuron) would resolve it. If no, the degeneracy is
     *  in the projection itself, and more dimensions are needed.
     *
     *  Uses cpu_lp_reference() — pure HP-side computation,
     *  no LP core involvement.
     * ══════════════════════════════════════════════════════════════ */
    {
        printf("\n── LP DOT MAGNITUDE DIAGNOSTIC ──\n");
        printf("  (CPU reference, not LP core)\n\n");

        /* Use the GIE hidden means from TEST 14 condition 14A.
         * Feed each pattern's mean GIE state + zero LP state through
         * the LP weights and compare dot magnitudes. */
        int8_t zero_lp[LP_HIDDEN_DIM];
        memset(zero_lp, 0, LP_HIDDEN_DIM);

        /* Build synthetic GIE states from the TEST 11 signatures.
         * sig[p] is the mean input for pattern p. The GIE hidden
         * state from that input is not directly available, but we
         * can use a deterministic GIE hidden: seed 42, run one
         * CfC step from each sig[p] as input. */
        int dots_f[4][LP_HIDDEN_DIM];
        int dots_g[4][LP_HIDDEN_DIM];

        for (int p = 0; p < 4; p++) {
            /* Use sig[p] directly as a 32-trit "GIE hidden proxy"
             * (first 32 trits of the 128-trit signature). This is
             * not the real GIE hidden, but it's pattern-specific
             * and deterministic — sufficient to test whether the
             * LP projection separates patterns in magnitude. */
            int8_t gie_proxy[LP_GIE_HIDDEN];
            for (int j = 0; j < LP_GIE_HIDDEN; j++)
                gie_proxy[j] = sig[p][j];

            cpu_lp_reference(gie_proxy, zero_lp, dots_f[p], dots_g[p]);
        }

        /* Print raw f-pathway dots for each pattern */
        printf("  LP f-pathway dots (per neuron, 4 patterns):\n");
        printf("       ");
        for (int n = 0; n < LP_HIDDEN_DIM; n++) printf(" n%02d", n);
        printf("\n");
        for (int p = 0; p < 4; p++) {
            printf("  P%d: ", p);
            for (int n = 0; n < LP_HIDDEN_DIM; n++)
                printf(" %+3d", dots_f[p][n]);
            printf("\n");
        }

        /* Signs */
        printf("\n  LP f-pathway SIGNS:\n");
        printf("       ");
        for (int n = 0; n < LP_HIDDEN_DIM; n++) printf("  n%02d", n);
        printf("\n");
        for (int p = 0; p < 4; p++) {
            printf("  P%d: ", p);
            for (int n = 0; n < LP_HIDDEN_DIM; n++)
                printf("   %c ", trit_char(tsign(dots_f[p][n])));
            printf("\n");
        }

        /* P1 vs P2: sign agreement + magnitude difference */
        printf("\n  P1 vs P2 comparison (f-pathway):\n");
        int sign_agree = 0, sign_differ = 0;
        int mag_diff_sum = 0;
        for (int n = 0; n < LP_HIDDEN_DIM; n++) {
            int s1 = tsign(dots_f[1][n]);
            int s2 = tsign(dots_f[2][n]);
            int m1 = dots_f[1][n] > 0 ? dots_f[1][n] : -dots_f[1][n];
            int m2 = dots_f[2][n] > 0 ? dots_f[2][n] : -dots_f[2][n];
            int md = m1 > m2 ? m1 - m2 : m2 - m1;
            printf("    n%02d: P1=%+3d P2=%+3d  sign=%s  |mag_diff|=%d\n",
                   n, dots_f[1][n], dots_f[2][n],
                   (s1 == s2) ? "SAME" : "DIFF", md);
            if (s1 == s2) sign_agree++;
            else sign_differ++;
            mag_diff_sum += md;
        }
        printf("  Signs: %d same, %d different\n", sign_agree, sign_differ);
        printf("  Mean |magnitude difference|: %.1f\n",
               (float)mag_diff_sum / LP_HIDDEN_DIM);
        printf("  If signs are mostly SAME but magnitudes differ:\n");
        printf("    → 5-trit MTFP per neuron would resolve the degeneracy\n");
        printf("  If signs AND magnitudes are similar:\n");
        printf("    → need more projection directions (wider LP)\n\n");
        fflush(stdout);
    }
}
