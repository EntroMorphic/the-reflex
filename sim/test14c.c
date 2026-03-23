/*
 * TEST 14C — Kinetic Attention Transition Simulation (v2)
 * AVX2-optimized C implementation
 *
 * Addresses red-team findings from March 23, 2026 session:
 *   - Pass criterion changed from TriX confirmations (trivially met by both
 *     conditions via W_f hidden=0) to LP divergence at t+30s post-switch.
 *   - Phase 1 extended to 9000 steps (90s @ 100Hz, matching hardware spec).
 *   - Three conditions: 14A (no bias), 14C (full bias), 14C-iso (bias Phase 2
 *     only — isolates transition mechanism from prior-building mechanism).
 *   - Three-claim measurement structure mirrors paper structure.
 *   - LP firing rate measured per phase.
 *
 * THREE CLAIMS (tested separately):
 *   Claim 1 (Structural guarantee): TriX always correct after switch.
 *     Measurement: TriX accuracy in first 15 steps of Phase 2.
 *     Expected: 100% all conditions. W_f hidden=0, not a comparison.
 *
 *   Claim 2 (Stale prior extinguishes): gate_bias[P1] decays without refresh
 *     once p_hat switches to P2.
 *     Measurement: gate_bias[P1] trace at steps 1,5,10,15 post-switch (14C).
 *     Expected: faster decay than naive 15*0.9^t prediction (no refresh).
 *
 *   Claim 3 (Agreement mechanism improves LP quality): gate_bias[P2] arms
 *     at T14_MIN_SAMPLES and sustains better LP P2 alignment.
 *     Measurement: lp_delta = LP_align_P2 - LP_align_P1 at steps 15,30,50,
 *     100,200 post-switch. 14C and 14C-iso should exceed 14A.
 *     PASS criterion: lp_delta > 0 at step 30.
 *
 * Build: gcc -O3 -march=native -mavx2 -mpopcnt -std=c11 -o test14c test14c.c -lm
 */

#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* =========================================================================
 * Firmware-exact constants
 * ========================================================================= */
#define CFC_HIDDEN_DIM      32
#define TRIX_NEURONS_PP     8
#define N_PATTERNS          4
#define LP_HIDDEN_DIM       16
#define VDB_SNAPSHOT_DIM    48
#define VDB_MAX_NODES       64
#define GATE_THRESHOLD      90
#define BASE_GATE_BIAS      15.0f
#define MIN_GATE_THRESHOLD  30
#define T14_MIN_SAMPLES     15
#define BIAS_DECAY_FACTOR   0.9f

/* =========================================================================
 * GIE signal model
 *   True group:      count ~ U[SIGNAL_LOW, COUNT_MAX]  fires when >= eff_threshold
 *   Non-true group:  count ~ U[0, NOISE_MAX]           rarely fires
 * Lower eff_threshold (via gate_bias) → more firing for that group.
 * ========================================================================= */
#define COUNT_MAX     180
#define SIGNAL_LOW     60
#define NOISE_MAX      25

/* =========================================================================
 * Simulation calibration (NOT firmware)
 * LP_SIM_THRESHOLD=2: produces meaningful LP firing with random weights.
 * Hardware uses GATE_THRESHOLD=90 with settled weights.
 * BLEND_ALPHA=0.2: soft VDB→LP blend per step.
 * ========================================================================= */
#define LP_SIM_THRESHOLD    2
#define BLEND_ALPHA         0.2f

/* =========================================================================
 * Trial parameters (hardware-matched)
 * ========================================================================= */
#define LP_STEPS_PHASE1     9000   /* 90s × 100Hz — full hardware spec */
#define LP_STEPS_PHASE2     500    /* 5s post-switch monitoring */
#define N_TRIALS            1000

/* =========================================================================
 * Pass criterion (Claim 3): lp_delta > 0 at step LP_DELTA_PASS_STEP
 * lp_delta = LP_align_P2 - LP_align_P1
 * ========================================================================= */
#define LP_DELTA_PASS_STEP  30

/* Alignment snapshot steps during Phase 2 */
#define N_SNAP              5
static const int SNAP[N_SNAP] = {15, 30, 50, 100, 200};

/* Gate bias trace steps (Claim 2) */
#define N_BIAS_TRACE        4
static const int BIAS_TRACE[N_BIAS_TRACE] = {1, 5, 10, 15};

/* =========================================================================
 * Conditions
 * ========================================================================= */
#define COND_14A     0   /* no gate bias, ever */
#define COND_14C     1   /* gate bias Phase 1 + Phase 2 */
#define COND_14C_ISO 2   /* gate bias Phase 2 only — isolates transition */
#define N_COND       3

static const char* COND_NAME[N_COND] = {"14A (no bias)", "14C (full bias)", "14C-iso (P2 bias only)"};

/* =========================================================================
 * AVX2 ternary operations
 * ========================================================================= */

static inline int32_t tdot32(const int8_t* a, const int8_t* b)
{
    __m256i va   = _mm256_loadu_si256((const __m256i*)a);
    __m256i vb   = _mm256_loadu_si256((const __m256i*)b);
    __m256i prod = _mm256_sign_epi8(va, vb);
    __m256i lo16 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(prod));
    __m256i hi16 = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(prod, 1));
    __m256i s16  = _mm256_add_epi16(lo16, hi16);
    __m256i s32lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(s16));
    __m256i s32hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(s16, 1));
    __m256i s32   = _mm256_add_epi32(s32lo, s32hi);
    __m128i s = _mm_add_epi32(_mm256_castsi256_si128(s32),
                               _mm256_extracti128_si256(s32, 1));
    s = _mm_hadd_epi32(s, s);
    s = _mm_hadd_epi32(s, s);
    return _mm_cvtsi128_si32(s);
}

static inline int32_t tdot16(const int8_t* a, const int8_t* b)
{
    __m128i va   = _mm_loadu_si128((const __m128i*)a);
    __m128i vb   = _mm_loadu_si128((const __m128i*)b);
    __m128i prod = _mm_sign_epi8(va, vb);
    __m128i lo16 = _mm_cvtepi8_epi16(prod);
    __m128i hi16 = _mm_cvtepi8_epi16(_mm_srli_si128(prod, 8));
    __m128i s16  = _mm_add_epi16(lo16, hi16);
    __m128i s32lo = _mm_cvtepi16_epi32(s16);
    __m128i s32hi = _mm_cvtepi16_epi32(_mm_srli_si128(s16, 8));
    __m128i s32   = _mm_add_epi32(s32lo, s32hi);
    s32 = _mm_hadd_epi32(s32, s32);
    s32 = _mm_hadd_epi32(s32, s32);
    return _mm_cvtsi128_si32(s32);
}

static inline int32_t thamming48(const int8_t* a, const int8_t* b)
{
    __m256i va32 = _mm256_loadu_si256((const __m256i*)a);
    __m256i vb32 = _mm256_loadu_si256((const __m256i*)b);
    __m256i eq32 = _mm256_cmpeq_epi8(va32, vb32);
    int m = __builtin_popcount((uint32_t)_mm256_movemask_epi8(eq32));
    __m128i va16 = _mm_loadu_si128((const __m128i*)(a + 32));
    __m128i vb16 = _mm_loadu_si128((const __m128i*)(b + 32));
    __m128i eq16 = _mm_cmpeq_epi8(va16, vb16);
    m += __builtin_popcount((uint32_t)(uint16_t)_mm_movemask_epi8(eq16));
    return VDB_SNAPSHOT_DIM - m;
}

static inline int8_t tsign(int32_t x)
{
    return (x > 0) ? 1 : (x < 0) ? -1 : 0;
}

/* =========================================================================
 * State
 * ========================================================================= */

typedef struct {
    int8_t  state[CFC_HIDDEN_DIM];
    float   gate_bias[N_PATTERNS];
} GIEState;

typedef struct {
    int8_t  hidden[LP_HIDDEN_DIM];
    int32_t running_sum[N_PATTERNS][LP_HIDDEN_DIM];
    int32_t sample_count[N_PATTERNS];
} LPState;

typedef struct {
    int8_t nodes[VDB_MAX_NODES][VDB_SNAPSHOT_DIM] __attribute__((aligned(32)));
    int    head;
    int    count;
} VDBState;

/* =========================================================================
 * GIE step
 * TriX: always returns p_true (W_f hidden=0 structural guarantee).
 * gie_state: probabilistic, influenced by gate_bias on p_hat group.
 * ========================================================================= */
static int gie_step(GIEState* gie, int p_true, int p_hat,
                    int use_bias, unsigned int* rng)
{
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int group = n / TRIX_NEURONS_PP;
        int eff = GATE_THRESHOLD;
        if (use_bias && group == p_hat) {
            eff = GATE_THRESHOLD - (int)gie->gate_bias[group];
            if (eff < MIN_GATE_THRESHOLD) eff = MIN_GATE_THRESHOLD;
        }
        *rng = (*rng) * 1664525u + 1013904223u;
        int count = (group == p_true)
            ? SIGNAL_LOW + (int)((*rng >> 16) % (COUNT_MAX - SIGNAL_LOW))
            : (int)((*rng >> 16) % (NOISE_MAX + 1));
        gie->state[n] = (count >= eff) ? 1 : 0;
    }
    return p_true; /* TriX: structural guarantee */
}

static void lp_cfc_step(LPState* lp, const int8_t* gie_state,
                         const int8_t weights[LP_HIDDEN_DIM][CFC_HIDDEN_DIM])
{
    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        int32_t dot = tdot32(weights[n], gie_state);
        lp->hidden[n] = (abs(dot) >= LP_SIM_THRESHOLD) ? tsign(dot) : 0;
    }
}

static void vdb_insert(VDBState* vdb, const int8_t* snap)
{
    memcpy(vdb->nodes[vdb->head], snap, VDB_SNAPSHOT_DIM);
    vdb->head = (vdb->head + 1) % VDB_MAX_NODES;
    if (vdb->count < VDB_MAX_NODES) vdb->count++;
}

static const int8_t* vdb_search(const VDBState* vdb, const int8_t* q)
{
    if (!vdb->count) return NULL;
    int best = INT32_MAX, bi = 0;
    for (int i = 0; i < vdb->count; i++) {
        int d = thamming48(q, vdb->nodes[i]);
        if (d < best) { best = d; bi = i; }
    }
    return vdb->nodes[bi];
}

static void vdb_blend(LPState* lp, const int8_t* node, unsigned int* rng)
{
    const int8_t* nlp = node + CFC_HIDDEN_DIM;
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        if (lp->hidden[i] != nlp[i]) {
            *rng = (*rng) * 1664525u + 1013904223u;
            if ((float)((*rng >> 16) & 0xFFFF) / 65535.0f < BLEND_ALPHA)
                lp->hidden[i] = nlp[i];
        }
    }
}

static void make_snap(int8_t* s, const int8_t* g, const int8_t* l)
{
    memcpy(s,                  g, CFC_HIDDEN_DIM);
    memcpy(s + CFC_HIDDEN_DIM, l, LP_HIDDEN_DIM);
}

static float lp_agree(const LPState* lp, int p)
{
    if (lp->sample_count[p] < 2) return 0.0f;
    int8_t sig[LP_HIDDEN_DIM];
    for (int i = 0; i < LP_HIDDEN_DIM; i++)
        sig[i] = tsign(lp->running_sum[p][i]);
    return (float)tdot16(lp->hidden, sig) / LP_HIDDEN_DIM;
}

static void update_bias(GIEState* gie, const LPState* lp, int p_hat)
{
    for (int p = 0; p < N_PATTERNS; p++)
        gie->gate_bias[p] *= BIAS_DECAY_FACTOR;
    if (lp->sample_count[p_hat] >= T14_MIN_SAMPLES) {
        float ag = lp_agree(lp, p_hat);
        float b  = BASE_GATE_BIAS * (ag > 0.0f ? ag : 0.0f);
        if (b > gie->gate_bias[p_hat]) gie->gate_bias[p_hat] = b;
    }
}

static void lp_accum(LPState* lp, int p)
{
    for (int i = 0; i < LP_HIDDEN_DIM; i++)
        lp->running_sum[p][i] += lp->hidden[i];
    lp->sample_count[p]++;
}

/* =========================================================================
 * Trial result
 * ========================================================================= */
typedef struct {
    /* Claim 1: structural */
    int   trix_correct_15;          /* P2 TriX confirmations in first 15 Phase 2 steps */

    /* Claim 2: stale prior extinguishes */
    float gbias_p1[N_BIAS_TRACE];   /* gate_bias[P1] at BIAS_TRACE steps post-switch */
    float gbias_p2[N_BIAS_TRACE];   /* gate_bias[P2] at BIAS_TRACE steps post-switch */

    /* Claim 3: LP quality */
    float lp_delta[N_SNAP];         /* LP_align_P2 - LP_align_P1 at SNAP steps */
    int   pass_lp_delta;            /* lp_delta > 0 at LP_DELTA_PASS_STEP */

    /* Diagnostics */
    float lp_fire_phase1;           /* avg LP neurons fired/step in Phase 1 */
    float lp_fire_phase2;           /* avg LP neurons fired/step in Phase 2 */
} TrialResult;

/* =========================================================================
 * Run one trial
 * cond: COND_14A / COND_14C / COND_14C_ISO
 * ========================================================================= */
static TrialResult run_trial(int cond, unsigned int* rng)
{
    TrialResult r;
    memset(&r, 0, sizeof(r));

    /* Random LP weights */
    int8_t W[LP_HIDDEN_DIM][CFC_HIDDEN_DIM];
    for (int n = 0; n < LP_HIDDEN_DIM; n++)
        for (int d = 0; d < CFC_HIDDEN_DIM; d++) {
            *rng = (*rng) * 1664525u + 1013904223u;
            W[n][d] = (int8_t)((int)((*rng >> 16) % 3) - 1);
        }

    GIEState gie; memset(&gie, 0, sizeof(gie));
    LPState  lp;  memset(&lp,  0, sizeof(lp));
    VDBState vdb; memset(&vdb, 0, sizeof(vdb));
    int p_hat = 0;

    int p1_bias = (cond == COND_14C);   /* Phase 1: bias only for full 14C */
    int p2_bias = (cond != COND_14A);   /* Phase 2: bias for 14C and 14C-iso */

    /* ---- Phase 1: 9000 steps of P1 ---- */
    long lp_fires_p1 = 0;
    for (int s = 0; s < LP_STEPS_PHASE1; s++) {
        p_hat = gie_step(&gie, 0, p_hat, p1_bias, rng);
        int8_t snap[VDB_SNAPSHOT_DIM];
        make_snap(snap, gie.state, lp.hidden);
        const int8_t* nb = vdb_search(&vdb, snap);
        if (nb) vdb_blend(&lp, nb, rng);
        lp_cfc_step(&lp, gie.state, W);
        for (int n = 0; n < LP_HIDDEN_DIM; n++) lp_fires_p1 += (lp.hidden[n] != 0);
        lp_accum(&lp, p_hat);
        vdb_insert(&vdb, snap);
        if (p1_bias) update_bias(&gie, &lp, p_hat);
    }
    r.lp_fire_phase1 = (float)lp_fires_p1 / LP_STEPS_PHASE1;

    /* ---- Phase 2: switch to P2 ---- */
    long lp_fires_p2 = 0;
    int snap_idx = 0, btrace_idx = 0;

    for (int s = 0; s < LP_STEPS_PHASE2; s++) {
        p_hat = gie_step(&gie, 1, p_hat, p2_bias, rng);

        /* Claim 1: count TriX correct in first 15 steps */
        if (s < T14_MIN_SAMPLES && p_hat == 1) r.trix_correct_15++;

        int8_t snap[VDB_SNAPSHOT_DIM];
        make_snap(snap, gie.state, lp.hidden);
        const int8_t* nb = vdb_search(&vdb, snap);
        if (nb) vdb_blend(&lp, nb, rng);
        lp_cfc_step(&lp, gie.state, W);
        for (int n = 0; n < LP_HIDDEN_DIM; n++) lp_fires_p2 += (lp.hidden[n] != 0);
        lp_accum(&lp, p_hat);
        vdb_insert(&vdb, snap);
        if (p2_bias) update_bias(&gie, &lp, p_hat);

        int step1 = s + 1;

        /* Claim 2: gate_bias trace */
        if (btrace_idx < N_BIAS_TRACE && step1 == BIAS_TRACE[btrace_idx]) {
            r.gbias_p1[btrace_idx] = gie.gate_bias[0];
            r.gbias_p2[btrace_idx] = gie.gate_bias[1];
            btrace_idx++;
        }

        /* Claim 3: LP delta */
        if (snap_idx < N_SNAP && step1 == SNAP[snap_idx]) {
            float a2 = lp_agree(&lp, 1); /* align with P2 */
            float a1 = lp_agree(&lp, 0); /* align with P1 */
            r.lp_delta[snap_idx] = a2 - a1;
            if (step1 == LP_DELTA_PASS_STEP)
                r.pass_lp_delta = (r.lp_delta[snap_idx] > 0.0f) ? 1 : 0;
            snap_idx++;
        }
    }
    r.lp_fire_phase2 = (float)lp_fires_p2 / LP_STEPS_PHASE2;

    return r;
}

/* =========================================================================
 * Main
 * ========================================================================= */
int main(void)
{
    printf("TEST 14C v2 — Kinetic Attention Transition Simulation\n");
    printf("======================================================\n");
    printf("Firmware: GATE_THRESHOLD=%d, BASE_GATE_BIAS=%.1f, "
           "MIN_GATE_THRESHOLD=%d, T14_MIN_SAMPLES=%d, DECAY=%.2f\n",
           GATE_THRESHOLD, BASE_GATE_BIAS, MIN_GATE_THRESHOLD,
           T14_MIN_SAMPLES, BIAS_DECAY_FACTOR);
    printf("GIE model:  signal=U[%d,%d], noise=U[0,%d]\n",
           SIGNAL_LOW, COUNT_MAX, NOISE_MAX);
    printf("LP sim:     LP_SIM_THRESHOLD=%d, BLEND_ALPHA=%.2f\n",
           LP_SIM_THRESHOLD, BLEND_ALPHA);
    printf("Trial:      %d steps P1 + %d steps P2 | %d trials\n",
           LP_STEPS_PHASE1, LP_STEPS_PHASE2, N_TRIALS);
    printf("Pass (Claim 3): lp_delta > 0 at step %d\n\n",
           LP_DELTA_PASS_STEP);

    unsigned int rng = (unsigned int)time(NULL) ^ 0xCAFEBABEu;

    /* Per-condition accumulators */
    long   pass_lp[N_COND]    = {0};
    long   trix_ok[N_COND]    = {0};
    double lp_delta_sum[N_COND][N_SNAP];
    double gbias_p1_sum[N_COND][N_BIAS_TRACE];
    double gbias_p2_sum[N_COND][N_BIAS_TRACE];
    double fire_p1_sum[N_COND] = {0};
    double fire_p2_sum[N_COND] = {0};
    memset(lp_delta_sum,  0, sizeof(lp_delta_sum));
    memset(gbias_p1_sum,  0, sizeof(gbias_p1_sum));
    memset(gbias_p2_sum,  0, sizeof(gbias_p2_sum));

    double wall[N_COND];

    for (int c = 0; c < N_COND; c++) {
        printf("Running %s...\n", COND_NAME[c]);
        clock_t t0 = clock();
        for (int t = 0; t < N_TRIALS; t++) {
            TrialResult r = run_trial(c, &rng);
            pass_lp[c]   += r.pass_lp_delta;
            trix_ok[c]   += (r.trix_correct_15 == T14_MIN_SAMPLES) ? 1 : 0;
            for (int sp = 0; sp < N_SNAP;       sp++) lp_delta_sum[c][sp] += r.lp_delta[sp];
            for (int bt = 0; bt < N_BIAS_TRACE; bt++) {
                gbias_p1_sum[c][bt] += r.gbias_p1[bt];
                gbias_p2_sum[c][bt] += r.gbias_p2[bt];
            }
            fire_p1_sum[c] += r.lp_fire_phase1;
            fire_p2_sum[c] += r.lp_fire_phase2;
        }
        wall[c] = (double)(clock() - t0) / CLOCKS_PER_SEC;
    }

    /* =========================================================
     * CLAIM 1: Structural guarantee
     * ========================================================= */
    printf("\n=== CLAIM 1: Structural Guarantee (W_f hidden=0) ===\n");
    printf("TriX correct all %d steps of Phase 2 first %d steps:\n",
           T14_MIN_SAMPLES, T14_MIN_SAMPLES);
    for (int c = 0; c < N_COND; c++) {
        printf("  %-26s %ld / %d (%.1f%%)\n",
               COND_NAME[c], trix_ok[c], N_TRIALS,
               100.0 * trix_ok[c] / N_TRIALS);
    }
    printf("Expected: 100%% all conditions. Not a comparison — confirmation.\n");

    /* =========================================================
     * CLAIM 2: Stale prior extinguishes
     * ========================================================= */
    printf("\n=== CLAIM 2: Stale P1 Gate Bias Extinguishes (14C only) ===\n");
    printf("Naive prediction: gate_bias[P1] = 15 * 0.9^t (no refresh after switch)\n");
    printf("Agreement prediction: faster decay (agreement stops refreshing P1 bias)\n\n");
    printf("  step | naive expected | gate_bias[P1] mean | gate_bias[P2] mean\n");
    printf("  -----|----------------|--------------------|-----------------\n");
    int c14c = COND_14C;
    for (int bt = 0; bt < N_BIAS_TRACE; bt++) {
        float naive  = BASE_GATE_BIAS * powf(BIAS_DECAY_FACTOR, BIAS_TRACE[bt]);
        float actual = (float)(gbias_p1_sum[c14c][bt] / N_TRIALS);
        float p2val  = (float)(gbias_p2_sum[c14c][bt] / N_TRIALS);
        printf("  %4d | %14.3f | %18.3f | %17.3f%s\n",
               BIAS_TRACE[bt], naive, actual, p2val,
               actual < naive - 0.1f ? "  <-- extinguished faster" : "");
    }

    /* =========================================================
     * CLAIM 3: LP quality post-transition
     * ========================================================= */
    printf("\n=== CLAIM 3: LP Quality Post-Transition ===\n");
    printf("lp_delta = LP_align_P2 - LP_align_P1  (positive = LP shifting toward P2)\n");
    printf("PASS: lp_delta > 0 at step %d\n\n", LP_DELTA_PASS_STEP);

    printf("  step |   14A    |   14C    | 14C-iso  | delta(14C-14A) | delta(iso-14A)\n");
    printf("  -----|----------|----------|----------|----------------|---------------\n");
    for (int sp = 0; sp < N_SNAP; sp++) {
        double a = lp_delta_sum[COND_14A][sp]     / N_TRIALS;
        double c = lp_delta_sum[COND_14C][sp]     / N_TRIALS;
        double i = lp_delta_sum[COND_14C_ISO][sp] / N_TRIALS;
        printf("  %4d | %+.5f | %+.5f | %+.5f | %+.5f%s     | %+.5f%s\n",
               SNAP[sp], a, c, i,
               c - a, (SNAP[sp] == LP_DELTA_PASS_STEP && c > a) ? "*" : " ",
               i - a, (SNAP[sp] == LP_DELTA_PASS_STEP && i > a) ? "*" : " ");
    }

    printf("\nPass rate (lp_delta > 0 at step %d):\n", LP_DELTA_PASS_STEP);
    for (int c = 0; c < N_COND; c++) {
        printf("  %-26s %ld / %d (%.1f%%)\n",
               COND_NAME[c], pass_lp[c], N_TRIALS,
               100.0 * pass_lp[c] / N_TRIALS);
    }

    /* =========================================================
     * LP firing rate
     * ========================================================= */
    printf("\n=== LP FIRING RATE (neurons/step, out of %d) ===\n", LP_HIDDEN_DIM);
    printf("  %-26s | Phase 1  | Phase 2\n", "Condition");
    printf("  ---------------------------|----------|--------\n");
    for (int c = 0; c < N_COND; c++) {
        printf("  %-26s | %6.2f   | %6.2f\n",
               COND_NAME[c],
               fire_p1_sum[c] / N_TRIALS,
               fire_p2_sum[c] / N_TRIALS);
    }

    /* =========================================================
     * Wall time
     * ========================================================= */
    printf("\n=== WALL TIME ===\n");
    for (int c = 0; c < N_COND; c++)
        printf("  %-26s %.2fs\n", COND_NAME[c], wall[c]);

    /* =========================================================
     * Verdict
     * ========================================================= */
    printf("\n=== VERDICT ===\n");

    int claim1 = 1;
    for (int c = 0; c < N_COND; c++)
        if (trix_ok[c] < N_TRIALS * 99 / 100) claim1 = 0;

    double a30 = lp_delta_sum[COND_14A][1]     / N_TRIALS;
    double c30 = lp_delta_sum[COND_14C][1]     / N_TRIALS;
    double i30 = lp_delta_sum[COND_14C_ISO][1] / N_TRIALS;

    int claim3_14c     = (pass_lp[COND_14C]     > pass_lp[COND_14A]);
    int claim3_14c_iso = (pass_lp[COND_14C_ISO] > pass_lp[COND_14A]);

    printf("Claim 1 (structural guarantee):        %s\n",
           claim1 ? "CONFIRMED" : "FAILED");
    printf("Claim 2 (P1 bias self-extinguishes):   %s\n",
           gbias_p1_sum[COND_14C][3] / N_TRIALS
               < BASE_GATE_BIAS * powf(BIAS_DECAY_FACTOR, 15) - 0.1
               ? "CONFIRMED (faster than naive)" : "NOT CONFIRMED");
    printf("Claim 3 — 14C vs 14A at step %d:      lp_delta %+.5f vs %+.5f  %s\n",
           LP_DELTA_PASS_STEP, c30, a30,
           claim3_14c ? "14C AHEAD" : "NOT CONFIRMED");
    printf("Claim 3 — 14C-iso vs 14A at step %d:  lp_delta %+.5f vs %+.5f  %s\n",
           LP_DELTA_PASS_STEP, i30, a30,
           claim3_14c_iso ? "14C-iso AHEAD" : "NOT CONFIRMED");

    if (claim3_14c && !claim3_14c_iso)
        printf("\nNOTE: 14C leads but 14C-iso does not — Phase 1 gate_bias contributes "
               "to the P1 prior quality, not just Phase 2 gate_bias[P2].\n");
    if (!claim3_14c && claim3_14c_iso)
        printf("\nNOTE: 14C-iso leads but 14C does not — Phase 1 gate_bias may be "
               "building a stronger P1 prior that takes longer to displace.\n");
    if (!claim3_14c && !claim3_14c_iso)
        printf("\nNOTE: Neither 14C condition leads 14A. LP firing rate may be too low "
               "for gate_bias to produce measurable LP quality difference.\n"
               "Consider: lower LP_SIM_THRESHOLD, higher BASE_GATE_BIAS, or "
               "this is the correct HOLD-dominated null result for hardware parameters.\n");

    return 0;
}
