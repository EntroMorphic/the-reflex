/*
 * geometry_cfc_diff.c — Ternary CfC Differentiation Experiment
 *                       Milestone 10: Testing the Stem Cell Hypothesis
 *
 * Informed by: Levin (cognitive lightcones, scale-free cognition),
 *              Noble (biological relativity, no privileged level),
 *              Hasani (CfC / liquid neural networks)
 *
 * Protocol:
 *
 *   TEST 1 — Differentiation & De-differentiation
 *     Phase A  (steps  0-29): Constant input A → stem regime
 *     Phase B  (steps 30-49): Sharp switch to input B → differentiation
 *     Phase C  (steps 50-69): Continue input B → committed state
 *     Phase D  (steps 70-99): Return to input A → de-differentiation?
 *     Control: Fresh network, input A for 100 steps → naive baseline
 *     Phase E comparison: de-differentiated (end of D) vs naive (end of control)
 *
 *   TEST 2 — Cell Types (distinguishable committed states)
 *     Same network seed. Stem regime under A → differentiate with B, C, D
 *     → measure hidden state distances between committed states
 *
 *   TEST 3 — Cognitive Lightcone Analysis
 *     Classify neurons: input-heavy (attend mostly to external stimulus)
 *                       hidden-heavy (attend mostly to internal state)
 *     Track each class through differentiation separately
 *
 *   TEST 4 — Autonomy (organism-as-environment)
 *     Take committed network → zero external input → run 30 steps
 *     → Can the network sustain dynamics from hidden state alone?
 *
 * All computation is CPU-only. M7 Test 4 proved GIE == CPU across
 * 128 dot products over 4 temporal steps. The hypothesis testing
 * does not require hardware verification — the dynamics are identical.
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * ESP-IDF: v5.4
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/* ── Constants ── */
#define T_POS  (+1)
#define T_ZERO  (0)
#define T_NEG  (-1)

/* ── CfC dimensions (same as M7) ── */
#define CFC_INPUT_DIM   128
#define CFC_HIDDEN_DIM  32
#define CFC_CONCAT_DIM  (CFC_INPUT_DIM + CFC_HIDDEN_DIM)   /* 160 */
#define CFC_MAX_DIM     256   /* buffer ceiling */

/* ── Experiment parameters ── */
#define PHASE_A_STEPS  30
#define PHASE_B_STEPS  20
#define PHASE_C_STEPS  20
#define PHASE_D_STEPS  30
#define TOTAL_STEPS    (PHASE_A_STEPS + PHASE_B_STEPS + PHASE_C_STEPS + PHASE_D_STEPS)
#define CONTROL_STEPS  TOTAL_STEPS

#define AUTONOMY_STEPS 30

#define INPUT_SEED_A   3333
#define INPUT_SEED_B   7777
#define INPUT_SEED_C   11111
#define INPUT_SEED_D   55555
#define NETWORK_SEED   5555
#define NETWORK_SPARSITY 50

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY CfC DATA STRUCTURES (identical to M7)
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int8_t W_f[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t b_f[CFC_HIDDEN_DIM];
    int8_t W_g[CFC_HIDDEN_DIM][CFC_MAX_DIM];
    int8_t b_g[CFC_HIDDEN_DIM];
    int8_t hidden[CFC_HIDDEN_DIM];
    uint32_t step;
} ternary_cfc_t;

/* ── Step result ── */
typedef struct {
    int8_t f[CFC_HIDDEN_DIM];
    int8_t g[CFC_HIDDEN_DIM];
    int8_t h_new[CFC_HIDDEN_DIM];
    int    f_dots[CFC_HIDDEN_DIM];
    int    g_dots[CFC_HIDDEN_DIM];
    int    n_update;
    int    n_hold;
    int    n_invert;
} cfc_step_result_t;

/* ══════════════════════════════════════════════════════════════════
 *  PRNG (identical to M7)
 * ══════════════════════════════════════════════════════════════════ */

static uint32_t cfc_rng_state;

static void cfc_seed(uint32_t seed) {
    cfc_rng_state = seed ? seed : 0xDEADBEEF;
}

static uint32_t cfc_rand(void) {
    uint32_t x = cfc_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cfc_rng_state = x;
    return x;
}

static int8_t rand_trit(int sparsity_pct) {
    uint32_t r = cfc_rand() % 100;
    if (r < (uint32_t)sparsity_pct) return T_ZERO;
    return (cfc_rand() & 1) ? T_POS : T_NEG;
}

/* ══════════════════════════════════════════════════════════════════
 *  TERNARY OPERATIONS (identical to M7)
 * ══════════════════════════════════════════════════════════════════ */

static inline int8_t tmul(int8_t a, int8_t b) {
    return (int8_t)(a * b);
}

static inline int8_t tsign(int val) {
    if (val > 0) return T_POS;
    if (val < 0) return T_NEG;
    return T_ZERO;
}

/* ── CPU dot product — split-source (identical to M7) ── */
static int cpu_dot_split(const int8_t *weights,
                         const int8_t *input, int input_dim,
                         const int8_t *hidden, int hidden_dim) {
    int sum = 0;
    for (int i = 0; i < input_dim; i++)
        sum += weights[i] * input[i];
    for (int i = 0; i < hidden_dim; i++)
        sum += weights[input_dim + i] * hidden[i];
    return sum;
}

/* ══════════════════════════════════════════════════════════════════
 *  CfC INITIALIZATION (identical to M7)
 * ══════════════════════════════════════════════════════════════════ */

static void cfc_init(ternary_cfc_t *cfc, uint32_t seed, int sparsity_pct) {
    memset(cfc, 0, sizeof(ternary_cfc_t));
    cfc_seed(seed);

    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            cfc->W_f[n][i] = rand_trit(sparsity_pct);
            cfc->W_g[n][i] = rand_trit(sparsity_pct);
        }
        for (int i = CFC_CONCAT_DIM; i < CFC_MAX_DIM; i++) {
            cfc->W_f[n][i] = T_ZERO;
            cfc->W_g[n][i] = T_ZERO;
        }
        cfc->b_f[n] = (int8_t)((cfc_rand() % 5) - 2);
        cfc->b_g[n] = (int8_t)((cfc_rand() % 5) - 2);
    }

    memset(cfc->hidden, T_ZERO, CFC_HIDDEN_DIM);
    cfc->step = 0;
}

/* ══════════════════════════════════════════════════════════════════
 *  CfC FORWARD PASS — CPU (identical to M7)
 * ══════════════════════════════════════════════════════════════════ */

static cfc_step_result_t cfc_forward_cpu(ternary_cfc_t *cfc, const int8_t *input) {
    cfc_step_result_t r = {0};

    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        r.f_dots[n] = cpu_dot_split(cfc->W_f[n], input, CFC_INPUT_DIM,
                                     cfc->hidden, CFC_HIDDEN_DIM) + cfc->b_f[n];
        r.g_dots[n] = cpu_dot_split(cfc->W_g[n], input, CFC_INPUT_DIM,
                                     cfc->hidden, CFC_HIDDEN_DIM) + cfc->b_g[n];

        r.f[n] = tsign(r.f_dots[n]);
        r.g[n] = tsign(r.g_dots[n]);

        if (r.f[n] == T_ZERO) {
            r.h_new[n] = cfc->hidden[n];
            r.n_hold++;
        } else {
            r.h_new[n] = tmul(r.f[n], r.g[n]);
            if (r.f[n] == T_POS) r.n_update++;
            else r.n_invert++;
        }
    }

    memcpy(cfc->hidden, r.h_new, CFC_HIDDEN_DIM);
    cfc->step++;
    return r;
}

/* ══════════════════════════════════════════════════════════════════
 *  MEASUREMENT HELPERS
 * ══════════════════════════════════════════════════════════════════ */

static char trit_char(int8_t t) {
    if (t > 0) return '+';
    if (t < 0) return '-';
    return '0';
}

static void print_trit_vec(const char *label, const int8_t *v, int n) {
    printf("  %s: [", label);
    for (int i = 0; i < n; i++) printf("%c", trit_char(v[i]));
    printf("]\n");
}

/* Count non-zero trits */
static int trit_energy(const int8_t *v, int n) {
    int e = 0;
    for (int i = 0; i < n; i++)
        if (v[i] != T_ZERO) e++;
    return e;
}

/* Ternary Hamming distance */
static int trit_hamming(const int8_t *a, const int8_t *b, int n) {
    int d = 0;
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) d++;
    return d;
}

/* Ternary dot product (correlation) between two trit vectors */
static int trit_dot(const int8_t *a, const int8_t *b, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += a[i] * b[i];
    return s;
}

/* ── Deterministic input generator (identical to M7) ── */
static void gen_input(int8_t *input, uint32_t seed) {
    cfc_seed(seed);
    for (int i = 0; i < CFC_INPUT_DIM; i++)
        input[i] = rand_trit(40);  /* 40% sparse */
}

/* ══════════════════════════════════════════════════════════════════
 *  COGNITIVE LIGHTCONE ANALYSIS
 *
 *  Each neuron's "lightcone" = its weight vector across [input|hidden].
 *  A neuron is "input-heavy" if more of its non-zero weights fall in
 *  the input portion [0..127] than the hidden portion [128..159].
 *  A "hidden-heavy" neuron attends more to internal state than
 *  external stimulus.
 *
 *  Levin's prediction: these two classes should respond differently
 *  to differentiation signals. Input-heavy neurons should react
 *  faster (their lightcone is dominated by the changing stimulus).
 *  Hidden-heavy neurons should be more conservative (their lightcone
 *  extends into the past via the hidden state).
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int input_nz;     /* non-zero weights in input portion of W_f + W_g */
    int hidden_nz;    /* non-zero weights in hidden portion of W_f + W_g */
    int is_input_heavy;  /* 1 if input_nz > hidden_nz (scaled by dimension) */
} neuron_lightcone_t;

static neuron_lightcone_t lightcones[CFC_HIDDEN_DIM];

static void analyze_lightcones(const ternary_cfc_t *cfc) {
    printf("  Cognitive lightcone analysis:\n");
    int n_input_heavy = 0, n_hidden_heavy = 0;

    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int inp_nz = 0, hid_nz = 0;

        /* Count non-zero weights across both pathways */
        for (int i = 0; i < CFC_INPUT_DIM; i++) {
            if (cfc->W_f[n][i] != 0) inp_nz++;
            if (cfc->W_g[n][i] != 0) inp_nz++;
        }
        for (int i = 0; i < CFC_HIDDEN_DIM; i++) {
            if (cfc->W_f[n][CFC_INPUT_DIM + i] != 0) hid_nz++;
            if (cfc->W_g[n][CFC_INPUT_DIM + i] != 0) hid_nz++;
        }

        lightcones[n].input_nz = inp_nz;
        lightcones[n].hidden_nz = hid_nz;

        /* Normalize: input has 128 positions (x2 pathways = 256 slots),
         * hidden has 32 positions (x2 = 64 slots).
         * Compare density: inp_nz/256 vs hid_nz/64 */
        float inp_density = (float)inp_nz / (CFC_INPUT_DIM * 2);
        float hid_density = (float)hid_nz / (CFC_HIDDEN_DIM * 2);
        lightcones[n].is_input_heavy = (inp_density >= hid_density) ? 1 : 0;

        if (lightcones[n].is_input_heavy) n_input_heavy++;
        else n_hidden_heavy++;
    }

    printf("    %d input-heavy neurons (external lightcone)\n", n_input_heavy);
    printf("    %d hidden-heavy neurons (temporal lightcone)\n", n_hidden_heavy);
}

/* ══════════════════════════════════════════════════════════════════
 *  STEP LOGGER — compact output for each phase
 * ══════════════════════════════════════════════════════════════════ */

static void log_step(int step, const cfc_step_result_t *r,
                     const int8_t *prev_hidden, const int8_t *ref_hidden) {
    const int8_t *h = r->h_new;
    int delta = trit_hamming(h, prev_hidden, CFC_HIDDEN_DIM);
    int energy = trit_energy(h, CFC_HIDDEN_DIM);

    /* ref_hidden is the reference attractor (e.g., end-of-phase-A snapshot).
     * If NULL, skip attractor distance. */
    if (ref_hidden) {
        int ref_dist = trit_hamming(h, ref_hidden, CFC_HIDDEN_DIM);
        int ref_corr = trit_dot(h, ref_hidden, CFC_HIDDEN_DIM);
        printf("  step %3d: d=%2d E=%2d U=%2d H=%2d I=%2d | ref_d=%2d ref_corr=%+3d\n",
               step, delta, energy, r->n_update, r->n_hold, r->n_invert,
               ref_dist, ref_corr);
    } else {
        printf("  step %3d: d=%2d E=%2d U=%2d H=%2d I=%2d\n",
               step, delta, energy, r->n_update, r->n_hold, r->n_invert);
    }
}

/* Log step with lightcone split — shows dynamics for input-heavy vs hidden-heavy */
static void log_step_lightcone(int step, const cfc_step_result_t *r,
                                const int8_t *prev_hidden) {
    int inp_u = 0, inp_h = 0, inp_i = 0;
    int hid_u = 0, hid_h = 0, hid_i = 0;
    int inp_delta = 0, hid_delta = 0;

    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int changed = (r->h_new[n] != prev_hidden[n]) ? 1 : 0;
        if (lightcones[n].is_input_heavy) {
            if (r->f[n] == T_POS) inp_u++;
            else if (r->f[n] == T_ZERO) inp_h++;
            else inp_i++;
            inp_delta += changed;
        } else {
            if (r->f[n] == T_POS) hid_u++;
            else if (r->f[n] == T_ZERO) hid_h++;
            else hid_i++;
            hid_delta += changed;
        }
    }

    printf("  step %3d: INP[U=%2d H=%2d I=%2d d=%2d] HID[U=%2d H=%2d I=%2d d=%2d]\n",
           step, inp_u, inp_h, inp_i, inp_delta,
           hid_u, hid_h, hid_i, hid_delta);
}

/* ══════════════════════════════════════════════════════════════════
 *  PHASE SUMMARY — aggregate stats for a phase
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    int total_u, total_h, total_i;
    int total_delta;
    int min_delta, max_delta;
    int final_energy;
    int steps;
} phase_summary_t;

static phase_summary_t run_phase(ternary_cfc_t *cfc, const int8_t *input,
                                  int n_steps, int step_offset,
                                  const int8_t *ref_hidden,
                                  int verbose) {
    phase_summary_t s = {0};
    s.min_delta = CFC_HIDDEN_DIM + 1;
    s.steps = n_steps;

    int8_t prev_hidden[CFC_HIDDEN_DIM];

    for (int i = 0; i < n_steps; i++) {
        memcpy(prev_hidden, cfc->hidden, CFC_HIDDEN_DIM);
        cfc_step_result_t r = cfc_forward_cpu(cfc, input);

        int delta = trit_hamming(cfc->hidden, prev_hidden, CFC_HIDDEN_DIM);
        s.total_u += r.n_update;
        s.total_h += r.n_hold;
        s.total_i += r.n_invert;
        s.total_delta += delta;
        if (delta < s.min_delta) s.min_delta = delta;
        if (delta > s.max_delta) s.max_delta = delta;
        s.final_energy = trit_energy(cfc->hidden, CFC_HIDDEN_DIM);

        if (verbose) {
            log_step(step_offset + i, &r, prev_hidden, ref_hidden);
        }
    }

    return s;
}

static void print_phase_summary(const char *name, const phase_summary_t *s) {
    float avg_delta = (float)s->total_delta / s->steps;
    float u_pct = 100.0f * s->total_u / (s->steps * CFC_HIDDEN_DIM);
    float h_pct = 100.0f * s->total_h / (s->steps * CFC_HIDDEN_DIM);
    float i_pct = 100.0f * s->total_i / (s->steps * CFC_HIDDEN_DIM);
    printf("  %s: avg_d=%.1f [%d-%d] E=%d U=%.0f%% H=%.0f%% I=%.0f%%\n",
           name, avg_delta, s->min_delta, s->max_delta, s->final_energy,
           u_pct, h_pct, i_pct);
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST 1: DIFFERENTIATION & DE-DIFFERENTIATION
 *
 *  The stem cell hypothesis:
 *    A: constant input → sustained dynamics (pluripotency)
 *    B: sharp change → UPDATE dominates (differentiation)
 *    C: continue new input → convergence (commitment)
 *    D: return to original → does it de-differentiate?
 *    E: compare de-differentiated state to naive network
 *       (path-dependent memory — Levin's prediction)
 * ══════════════════════════════════════════════════════════════════ */

static void test1_differentiation(void) {
    printf("== TEST 1: Differentiation & De-differentiation ==\n");
    printf("   Protocol: A(%d) → B(%d) → C(%d) → D(%d) = %d steps\n",
           PHASE_A_STEPS, PHASE_B_STEPS, PHASE_C_STEPS, PHASE_D_STEPS, TOTAL_STEPS);
    printf("   + Control: naive network, input A for %d steps\n\n", CONTROL_STEPS);
    fflush(stdout);

    /* Generate inputs */
    int8_t input_a[CFC_INPUT_DIM], input_b[CFC_INPUT_DIM];
    gen_input(input_a, INPUT_SEED_A);
    gen_input(input_b, INPUT_SEED_B);

    int ab_dist = trit_hamming(input_a, input_b, CFC_INPUT_DIM);
    int ab_corr = trit_dot(input_a, input_b, CFC_INPUT_DIM);
    printf("  Input A vs B: hamming=%d/%d corr=%+d\n\n", ab_dist, CFC_INPUT_DIM, ab_corr);

    /* ── Main protocol ── */
    static ternary_cfc_t cfc;
    cfc_init(&cfc, NETWORK_SEED, NETWORK_SPARSITY);

    int step_offset = 0;

    /* Phase A: Stem regime */
    printf("  --- Phase A: Constant input A (stem regime) ---\n");
    fflush(stdout);
    phase_summary_t sa = run_phase(&cfc, input_a, PHASE_A_STEPS, step_offset, NULL, 1);
    step_offset += PHASE_A_STEPS;

    /* Snapshot: end-of-stem reference attractor */
    int8_t h_stem[CFC_HIDDEN_DIM];
    memcpy(h_stem, cfc.hidden, CFC_HIDDEN_DIM);
    print_phase_summary("Phase A", &sa);
    print_trit_vec("h_stem", h_stem, CFC_HIDDEN_DIM);
    printf("\n");
    fflush(stdout);

    /* Phase B: Differentiation signal */
    printf("  --- Phase B: Sharp switch to input B (differentiation) ---\n");
    fflush(stdout);
    phase_summary_t sb = run_phase(&cfc, input_b, PHASE_B_STEPS, step_offset, h_stem, 1);
    step_offset += PHASE_B_STEPS;

    print_phase_summary("Phase B", &sb);
    int h_stem_dist = trit_hamming(cfc.hidden, h_stem, CFC_HIDDEN_DIM);
    printf("  distance from stem attractor: %d/%d\n\n", h_stem_dist, CFC_HIDDEN_DIM);
    fflush(stdout);

    /* Phase C: Committed state */
    printf("  --- Phase C: Continue input B (committed state) ---\n");
    fflush(stdout);
    phase_summary_t sc = run_phase(&cfc, input_b, PHASE_C_STEPS, step_offset, h_stem, 1);
    step_offset += PHASE_C_STEPS;

    int8_t h_committed[CFC_HIDDEN_DIM];
    memcpy(h_committed, cfc.hidden, CFC_HIDDEN_DIM);
    print_phase_summary("Phase C", &sc);
    h_stem_dist = trit_hamming(cfc.hidden, h_stem, CFC_HIDDEN_DIM);
    printf("  distance from stem attractor: %d/%d\n", h_stem_dist, CFC_HIDDEN_DIM);
    print_trit_vec("h_committed", h_committed, CFC_HIDDEN_DIM);
    printf("\n");
    fflush(stdout);

    /* Phase D: De-differentiation — return to input A */
    printf("  --- Phase D: Return to input A (de-differentiation?) ---\n");
    fflush(stdout);
    phase_summary_t sd = run_phase(&cfc, input_a, PHASE_D_STEPS, step_offset, h_stem, 1);

    int8_t h_dediff[CFC_HIDDEN_DIM];
    memcpy(h_dediff, cfc.hidden, CFC_HIDDEN_DIM);
    print_phase_summary("Phase D", &sd);

    int dediff_stem_dist = trit_hamming(h_dediff, h_stem, CFC_HIDDEN_DIM);
    int dediff_stem_corr = trit_dot(h_dediff, h_stem, CFC_HIDDEN_DIM);
    int dediff_comm_dist = trit_hamming(h_dediff, h_committed, CFC_HIDDEN_DIM);
    printf("  de-diff vs stem:      hamming=%d corr=%+d\n", dediff_stem_dist, dediff_stem_corr);
    printf("  de-diff vs committed: hamming=%d\n", dediff_comm_dist);
    print_trit_vec("h_dediff", h_dediff, CFC_HIDDEN_DIM);
    printf("\n");
    fflush(stdout);

    /* ── Control: Naive network, input A only ── */
    printf("  --- Control: Naive network, input A for %d steps ---\n", CONTROL_STEPS);
    fflush(stdout);
    static ternary_cfc_t cfc_naive;
    cfc_init(&cfc_naive, NETWORK_SEED, NETWORK_SPARSITY);

    /* Run silently (not verbose) — just get the final state */
    phase_summary_t s_naive = run_phase(&cfc_naive, input_a, CONTROL_STEPS, 0, NULL, 0);

    int8_t h_naive[CFC_HIDDEN_DIM];
    memcpy(h_naive, cfc_naive.hidden, CFC_HIDDEN_DIM);
    print_phase_summary("Naive ", &s_naive);
    print_trit_vec("h_naive", h_naive, CFC_HIDDEN_DIM);

    /* ── Phase E: Path-dependent memory comparison ── */
    printf("\n  --- Phase E: Path-dependent memory ---\n");
    int naive_dediff_dist = trit_hamming(h_naive, h_dediff, CFC_HIDDEN_DIM);
    int naive_dediff_corr = trit_dot(h_naive, h_dediff, CFC_HIDDEN_DIM);
    int naive_stem_dist = trit_hamming(h_naive, h_stem, CFC_HIDDEN_DIM);
    int naive_stem_corr = trit_dot(h_naive, h_stem, CFC_HIDDEN_DIM);

    printf("  naive vs de-diff:  hamming=%d corr=%+d\n", naive_dediff_dist, naive_dediff_corr);
    printf("  naive vs stem(30): hamming=%d corr=%+d\n", naive_stem_dist, naive_stem_corr);
    printf("\n");

    /* ── Verdict ── */
    printf("  === TEST 1 ANALYSIS ===\n");

    /* Prediction 1: Phase A sustains dynamics (delta > 0 throughout) */
    int p1 = (sa.min_delta > 0);
    printf("  P1 (stem sustains dynamics):    %s (min_delta=%d)\n",
           p1 ? "CONFIRMED" : "FALSIFIED", sa.min_delta);

    /* Prediction 2: Phase B shows UPDATE surge (higher U% than Phase A) */
    float a_u_pct = 100.0f * sa.total_u / (sa.steps * CFC_HIDDEN_DIM);
    float b_u_pct = 100.0f * sb.total_u / (sb.steps * CFC_HIDDEN_DIM);
    int p2 = (b_u_pct > a_u_pct);
    printf("  P2 (differentiation UPDATE surge): %s (A:%.0f%% → B:%.0f%%)\n",
           p2 ? "CONFIRMED" : "FALSIFIED", a_u_pct, b_u_pct);

    /* Prediction 3: Phase C converges (decreasing delta) or reaches new attractor */
    int p3 = (sc.min_delta < sb.max_delta);
    printf("  P3 (commitment/convergence):    %s (C_min_d=%d < B_max_d=%d)\n",
           p3 ? "CONFIRMED" : "FALSIFIED", sc.min_delta, sb.max_delta);

    /* Prediction 4: Phase D approaches stem state (de-differentiation) */
    int p4 = (dediff_stem_dist < dediff_comm_dist);
    printf("  P4 (de-differentiation):        %s (stem_d=%d < comm_d=%d)\n",
           p4 ? "CONFIRMED" : "FALSIFIED", dediff_stem_dist, dediff_comm_dist);

    /* Prediction 5 (Levin): Path-dependent memory — naive != de-diff */
    int p5 = (naive_dediff_dist > 0);
    printf("  P5 (path-dependent memory):     %s (naive_vs_dediff=%d)\n",
           p5 ? "CONFIRMED" : "FALSIFIED", naive_dediff_dist);

    int total_confirmed = p1 + p2 + p3 + p4 + p5;
    printf("  SCORE: %d/5 predictions confirmed\n\n", total_confirmed);
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST 2: CELL TYPES — Distinguishable Committed States
 *
 *  Start from stem regime under input A.
 *  Differentiate with inputs B, C, D.
 *  Measure pairwise distances between committed states.
 *  If distances are large → different "cell types."
 * ══════════════════════════════════════════════════════════════════ */

static void test2_cell_types(void) {
    printf("== TEST 2: Cell Types (distinguishable committed states) ==\n\n");
    fflush(stdout);

    uint32_t diff_seeds[] = { INPUT_SEED_B, INPUT_SEED_C, INPUT_SEED_D };
    const char *diff_names[] = { "B", "C", "D" };
    int8_t committed_states[3][CFC_HIDDEN_DIM];
    int n_types = 3;

    for (int t = 0; t < n_types; t++) {
        /* Fresh network each time */
        static ternary_cfc_t cfc;
        cfc_init(&cfc, NETWORK_SEED, NETWORK_SPARSITY);

        /* Stem phase: 30 steps with input A */
        int8_t input_a[CFC_INPUT_DIM];
        gen_input(input_a, INPUT_SEED_A);
        run_phase(&cfc, input_a, PHASE_A_STEPS, 0, NULL, 0);

        /* Differentiate: 40 steps with input X */
        int8_t input_x[CFC_INPUT_DIM];
        gen_input(input_x, diff_seeds[t]);
        run_phase(&cfc, input_x, PHASE_B_STEPS + PHASE_C_STEPS, PHASE_A_STEPS, NULL, 0);

        memcpy(committed_states[t], cfc.hidden, CFC_HIDDEN_DIM);
        printf("  Type %s: ", diff_names[t]);
        print_trit_vec("h", committed_states[t], CFC_HIDDEN_DIM);
    }

    /* Pairwise distances */
    printf("\n  Pairwise distances between committed states:\n");
    for (int i = 0; i < n_types; i++) {
        for (int j = i + 1; j < n_types; j++) {
            int dist = trit_hamming(committed_states[i], committed_states[j], CFC_HIDDEN_DIM);
            int corr = trit_dot(committed_states[i], committed_states[j], CFC_HIDDEN_DIM);
            printf("    %s vs %s: hamming=%d/%d corr=%+d\n",
                   diff_names[i], diff_names[j], dist, CFC_HIDDEN_DIM, corr);
        }
    }

    /* Are they distinguishable? Average pairwise distance > CFC_HIDDEN_DIM/4 */
    int total_dist = 0;
    int n_pairs = 0;
    for (int i = 0; i < n_types; i++) {
        for (int j = i + 1; j < n_types; j++) {
            total_dist += trit_hamming(committed_states[i], committed_states[j], CFC_HIDDEN_DIM);
            n_pairs++;
        }
    }
    float avg_dist = (float)total_dist / n_pairs;
    int distinguishable = (avg_dist > CFC_HIDDEN_DIM / 4.0f);
    printf("\n  Average pairwise distance: %.1f/%d\n", avg_dist, CFC_HIDDEN_DIM);
    printf("  Cell types distinguishable: %s\n\n", distinguishable ? "YES" : "NO");
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST 3: COGNITIVE LIGHTCONE
 *
 *  Run the differentiation protocol (A→B) while tracking
 *  input-heavy and hidden-heavy neurons separately.
 *
 *  Levin's prediction: input-heavy neurons react faster to
 *  the differentiation signal (input change). Hidden-heavy
 *  neurons are more conservative — they resist the change
 *  because their lightcone extends into the temporal past.
 * ══════════════════════════════════════════════════════════════════ */

static void test3_lightcone(void) {
    printf("== TEST 3: Cognitive Lightcone Analysis ==\n\n");
    fflush(stdout);

    static ternary_cfc_t cfc;
    cfc_init(&cfc, NETWORK_SEED, NETWORK_SPARSITY);

    /* Analyze weight structure */
    analyze_lightcones(&cfc);
    printf("\n");

    int8_t input_a[CFC_INPUT_DIM], input_b[CFC_INPUT_DIM];
    gen_input(input_a, INPUT_SEED_A);
    gen_input(input_b, INPUT_SEED_B);

    int8_t prev_hidden[CFC_HIDDEN_DIM];

    /* Phase A: last 5 steps of stem regime */
    printf("  --- Stem regime (last 5 steps of Phase A) ---\n");
    for (int i = 0; i < PHASE_A_STEPS - 5; i++) {
        memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
        cfc_forward_cpu(&cfc, input_a);
    }
    for (int i = PHASE_A_STEPS - 5; i < PHASE_A_STEPS; i++) {
        memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
        cfc_step_result_t r = cfc_forward_cpu(&cfc, input_a);
        log_step_lightcone(i, &r, prev_hidden);
    }
    printf("\n");

    /* Phase B: first 10 steps of differentiation */
    printf("  --- Differentiation (first 10 steps of Phase B) ---\n");
    for (int i = 0; i < 10; i++) {
        memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
        cfc_step_result_t r = cfc_forward_cpu(&cfc, input_b);
        log_step_lightcone(PHASE_A_STEPS + i, &r, prev_hidden);
    }

    /* Count: how many steps did it take for each class to "react"?
     * Reaction = delta > 0 for that class in the first step after switch */
    printf("\n  (Input-heavy neurons should react faster to input change)\n\n");
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════
 *  TEST 4: AUTONOMY — Organism as Environment
 *
 *  Take a committed network and remove all external input.
 *  The hidden state feeds back through the hidden-weight portion
 *  of the concat. If the network sustains dynamics with zero input,
 *  it has internalized enough structure to be self-sustaining.
 *
 *  The boundary between organism (hidden) and environment (input)
 *  dissolves: the network IS its own environment.
 *
 *  "An organism becomes its environment."
 * ══════════════════════════════════════════════════════════════════ */

static void test4_autonomy(void) {
    printf("== TEST 4: Autonomy (organism-as-environment) ==\n\n");
    fflush(stdout);

    static ternary_cfc_t cfc;
    cfc_init(&cfc, NETWORK_SEED, NETWORK_SPARSITY);

    /* First: establish committed state (A→B) */
    int8_t input_a[CFC_INPUT_DIM], input_b[CFC_INPUT_DIM];
    gen_input(input_a, INPUT_SEED_A);
    gen_input(input_b, INPUT_SEED_B);

    run_phase(&cfc, input_a, PHASE_A_STEPS, 0, NULL, 0);
    run_phase(&cfc, input_b, PHASE_B_STEPS + PHASE_C_STEPS, PHASE_A_STEPS, NULL, 0);

    int8_t h_before_autonomy[CFC_HIDDEN_DIM];
    memcpy(h_before_autonomy, cfc.hidden, CFC_HIDDEN_DIM);

    printf("  Committed state established (A→B, %d steps)\n",
           PHASE_A_STEPS + PHASE_B_STEPS + PHASE_C_STEPS);
    printf("  Energy before autonomy: %d/%d\n",
           trit_energy(h_before_autonomy, CFC_HIDDEN_DIM), CFC_HIDDEN_DIM);
    print_trit_vec("h_committed", h_before_autonomy, CFC_HIDDEN_DIM);
    printf("\n");

    /* Now: zero input — can it sustain dynamics? */
    printf("  --- Autonomy phase: zero external input, %d steps ---\n", AUTONOMY_STEPS);
    int8_t zero_input[CFC_INPUT_DIM];
    memset(zero_input, 0, CFC_INPUT_DIM);

    int8_t prev_hidden[CFC_HIDDEN_DIM];
    int sustained = 0;
    int steps_with_dynamics = 0;
    int total_energy = 0;

    for (int i = 0; i < AUTONOMY_STEPS; i++) {
        memcpy(prev_hidden, cfc.hidden, CFC_HIDDEN_DIM);
        cfc_step_result_t r = cfc_forward_cpu(&cfc, zero_input);

        int delta = trit_hamming(cfc.hidden, prev_hidden, CFC_HIDDEN_DIM);
        int energy = trit_energy(cfc.hidden, CFC_HIDDEN_DIM);
        total_energy += energy;

        if (delta > 0) steps_with_dynamics++;

        printf("  step %3d: d=%2d E=%2d U=%2d H=%2d I=%2d\n",
               i, delta, energy, r.n_update, r.n_hold, r.n_invert);
    }

    /* Did dynamics sustain for at least half the steps? */
    sustained = (steps_with_dynamics > AUTONOMY_STEPS / 2);
    float avg_energy = (float)total_energy / AUTONOMY_STEPS;

    int8_t h_after_autonomy[CFC_HIDDEN_DIM];
    memcpy(h_after_autonomy, cfc.hidden, CFC_HIDDEN_DIM);

    printf("\n  Steps with dynamics: %d/%d\n", steps_with_dynamics, AUTONOMY_STEPS);
    printf("  Average energy: %.1f/%d\n", avg_energy, CFC_HIDDEN_DIM);
    printf("  Drift from committed state: %d/%d\n",
           trit_hamming(h_after_autonomy, h_before_autonomy, CFC_HIDDEN_DIM),
           CFC_HIDDEN_DIM);
    print_trit_vec("h_autonomous", h_after_autonomy, CFC_HIDDEN_DIM);

    printf("\n  Autonomy: %s\n",
           sustained ? "SELF-SUSTAINING (organism IS environment)"
                     : "EXTERNALLY DEPENDENT (requires stimulus)");
    printf("\n");
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════════════════ */

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));

    printf("\n");
    printf("============================================================\n");
    printf("  TERNARY CfC — Differentiation Experiment\n");
    printf("  Milestone 10: Stem Cell Hypothesis + Cognitive Lightcones\n");
    printf("  Informed by: Levin, Noble, Hasani\n");
    printf("============================================================\n\n");

    printf("  CfC: input=%d hidden=%d concat=%d | weights {-1,0,+1}\n",
           CFC_INPUT_DIM, CFC_HIDDEN_DIM, CFC_CONCAT_DIM);
    printf("  Blend: UPDATE(f=+1) HOLD(f=0) INVERT(f=-1)\n");
    printf("  h_new = (f==0) ? h_old : f * g\n\n");

    printf("  Network seed=%d sparsity=%d%%\n", NETWORK_SEED, NETWORK_SPARSITY);
    printf("  Input seeds: A=%d B=%d C=%d D=%d\n\n",
           INPUT_SEED_A, INPUT_SEED_B, INPUT_SEED_C, INPUT_SEED_D);

    int64_t t_start = esp_timer_get_time();

    test1_differentiation();
    test2_cell_types();
    test3_lightcone();
    test4_autonomy();

    int64_t t_total = esp_timer_get_time() - t_start;

    printf("============================================================\n");
    printf("  EXPERIMENT COMPLETE\n");
    printf("  Total time: %lld ms\n", t_total / 1000);
    printf("============================================================\n");
    printf("\n  The boundary between organism and environment\n");
    printf("  is a convention, not a fact.\n\n");
    fflush(stdout);

    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
