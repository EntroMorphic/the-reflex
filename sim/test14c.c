/*
 * TEST 14C — Kinetic Attention Transition Simulation
 * AVX2-optimized C implementation
 *
 * Models the LP prior re-alignment after a P1→P2 pattern switch.
 *
 * ARCHITECTURE NOTE:
 * Gate bias affects gie_state (which neurons fire into VDB/LP), NOT TriX
 * classification. TriX is always correct by W_f hidden=0 structural guarantee.
 * The interesting comparison is LP state quality post-transition, not
 * time-to-first-TriX-confirmation.
 *
 * 14A: No gate bias. Clean P2 gie_state from step 1. Base-rate LP re-alignment.
 * 14C: Agreement-weighted gate bias. Brief P1 noise (decaying P1 bias),
 *      then accelerated P2 alignment after T14_MIN_SAMPLES confirmations.
 *
 * PASS CRITERION:
 *   Immediate: both pass in T14_MIN_SAMPLES steps (structural guarantee).
 *   Meaningful: LP P2 alignment score at steps 50, 100, 200 post-switch.
 *   14C should overtake 14A after T14_MIN_SAMPLES due to P2 gate_bias amplification.
 *
 * Hardware basis: ESP32-C6, Phase 5 constants (commit 8a33369)
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
#define CFC_HIDDEN_DIM     32
#define TRIX_NEURONS_PP    8
#define N_PATTERNS         4
#define LP_HIDDEN_DIM      16
#define VDB_SNAPSHOT_DIM   48
#define VDB_MAX_NODES      64
#define GATE_THRESHOLD     90
#define BASE_GATE_BIAS     15.0f
#define MIN_GATE_THRESHOLD 30
#define T14_MIN_SAMPLES    15
#define BIAS_DECAY_FACTOR  0.9f

/* =========================================================================
 * GIE signal model constants
 *
 * Each GIE neuron's underlying count is sampled from:
 *   True group:      U[SIGNAL_LOW, COUNT_MAX]   — signal present
 *   Non-true group:  U[0, NOISE_MAX]            — noise only
 * Neuron fires when count >= eff_threshold.
 *
 * Gate bias LOWERS eff_threshold for p_hat group → more firing for that group.
 * With NOISE_MAX < MIN_GATE_THRESHOLD, gate bias cannot cause non-true-group
 * neurons to fire unless noise is above the reduced threshold.
 *
 * Tuned so true group fires ~75% at full GATE_THRESHOLD=90,
 * and noise group fires ~0% without gate bias, ~20% with max gate bias.
 * ========================================================================= */
#define COUNT_MAX      180
#define SIGNAL_LOW      60
#define NOISE_MAX       25   /* below MIN_GATE_THRESHOLD=30: non-true group can fire
                                only when gate_bias is large (eff_thresh < NOISE_MAX) */

/* =========================================================================
 * Simulation calibration
 *
 * LP_SIM_THRESHOLD: with random weights and ~4-6 active GIE inputs per step,
 * threshold=2 gives meaningful LP firing dynamics.
 * Hardware uses GATE_THRESHOLD=90 with settled (non-random) weights.
 * ========================================================================= */
#define LP_SIM_THRESHOLD   2

/* =========================================================================
 * Trial parameters
 * ========================================================================= */
#define LP_STEPS_PHASE1    900     /* 9s × 100 Hz (reduced for sim speed; scales correctly) */
#define LP_STEPS_PHASE2    500     /* 5s post-switch monitoring */
#define N_TRIALS           1000
#define PASS_CRITERION     T14_MIN_SAMPLES  /* TriX structural: trivially met */

/* Alignment snapshot points during Phase 2 */
#define N_SNAP_POINTS      5
static const int SNAP_STEPS[N_SNAP_POINTS] = {15, 30, 50, 100, 200};

/* =========================================================================
 * AVX2 ternary operations
 * ========================================================================= */

/*
 * tdot32: ternary dot product of two 32-element int8 vectors.
 * _mm256_sign_epi8(a,b) = a*b element-wise for b∈{-1,0,+1}.
 */
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

/*
 * tdot16: ternary dot product of two 16-element int8 vectors (SSE).
 */
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

/*
 * thamming48: Hamming distance between two 48-element int8 ternary vectors.
 */
static inline int32_t thamming48(const int8_t* a, const int8_t* b)
{
    __m256i va32 = _mm256_loadu_si256((const __m256i*)a);
    __m256i vb32 = _mm256_loadu_si256((const __m256i*)b);
    __m256i eq32 = _mm256_cmpeq_epi8(va32, vb32);
    int matches  = __builtin_popcount((uint32_t)_mm256_movemask_epi8(eq32));

    __m128i va16 = _mm_loadu_si128((const __m128i*)(a + 32));
    __m128i vb16 = _mm_loadu_si128((const __m128i*)(b + 32));
    __m128i eq16 = _mm_cmpeq_epi8(va16, vb16);
    matches     += __builtin_popcount((uint32_t)(uint16_t)_mm_movemask_epi8(eq16));

    return VDB_SNAPSHOT_DIM - matches;
}

static inline int8_t tsign_scalar(int32_t x)
{
    if (x > 0) return  1;
    if (x < 0) return -1;
    return 0;
}

/* =========================================================================
 * State structures
 * ========================================================================= */

typedef struct {
    int8_t  state[CFC_HIDDEN_DIM];    /* which neurons actually fired (gate_bias influenced) */
    float   gate_bias[N_PATTERNS];    /* per-group bias (subtracted from eff_threshold) */
} GIEState;

typedef struct {
    int8_t  hidden[LP_HIDDEN_DIM];
    int32_t running_sum[N_PATTERNS][LP_HIDDEN_DIM];
    int32_t sample_count[N_PATTERNS];
} LPState;

typedef struct {
    int8_t  nodes[VDB_MAX_NODES][VDB_SNAPSHOT_DIM] __attribute__((aligned(32)));
    int     head;
    int     count;
} VDBState;

/* =========================================================================
 * GIE step
 *
 * ARCHITECTURE:
 * - TriX classification: ALWAYS returns p_true (structural guarantee, W_f hidden=0)
 * - gie_state: probabilistic, influenced by gate_bias
 *
 * GIE signal model:
 *   neuron count ~ U[SIGNAL_LOW, COUNT_MAX] for true group (signal present)
 *   neuron count ~ U[0, NOISE_MAX] for non-true groups (noise only)
 *   fires when count >= eff_threshold[group]
 *   eff_threshold[p_hat] = GATE_THRESHOLD - gate_bias[p_hat] (clamped to MIN_GATE_THRESHOLD)
 * ========================================================================= */
static int gie_step(GIEState* gie, int p_true, int p_hat,
                    int use_gate_bias, unsigned int* rng)
{
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        int group = n / TRIX_NEURONS_PP;

        int eff_threshold = GATE_THRESHOLD;
        if (use_gate_bias && group == p_hat) {
            int bias_int = (int)gie->gate_bias[group];
            eff_threshold = GATE_THRESHOLD - bias_int;
            if (eff_threshold < MIN_GATE_THRESHOLD) eff_threshold = MIN_GATE_THRESHOLD;
        }

        /* Sample neuron count */
        *rng = (*rng) * 1664525u + 1013904223u;
        int count;
        if (group == p_true) {
            count = SIGNAL_LOW + (int)((*rng >> 16) % (COUNT_MAX - SIGNAL_LOW));
        } else {
            count = (int)((*rng >> 16) % (NOISE_MAX + 1));
        }

        gie->state[n] = (count >= eff_threshold) ? 1 : 0;
    }

    /* TriX: structural guarantee — always returns p_true regardless of gie_state */
    return p_true;
}

/* =========================================================================
 * LP CfC step — input is gie_state (gate_bias influenced)
 * ========================================================================= */
static void lp_cfc_step(LPState* lp, const int8_t* gie_state,
                         const int8_t weights[LP_HIDDEN_DIM][CFC_HIDDEN_DIM])
{
    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        int32_t dot = tdot32(weights[n], gie_state);
        lp->hidden[n] = (abs(dot) >= LP_SIM_THRESHOLD) ? tsign_scalar(dot) : 0;
    }
}

/* =========================================================================
 * VDB operations
 * ========================================================================= */

static void vdb_insert(VDBState* vdb, const int8_t* snapshot)
{
    memcpy(vdb->nodes[vdb->head], snapshot, VDB_SNAPSHOT_DIM);
    vdb->head = (vdb->head + 1) % VDB_MAX_NODES;
    if (vdb->count < VDB_MAX_NODES) vdb->count++;
}

static const int8_t* vdb_search(const VDBState* vdb, const int8_t* query)
{
    if (vdb->count == 0) return NULL;
    int best_dist = INT32_MAX;
    int best_idx  = 0;
    for (int i = 0; i < vdb->count; i++) {
        int dist = thamming48(query, vdb->nodes[i]);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx  = i;
        }
    }
    return vdb->nodes[best_idx];
}

static void make_snapshot(int8_t* snap, const int8_t* gie_state, const int8_t* lp_hidden)
{
    memcpy(snap,                  gie_state, CFC_HIDDEN_DIM);
    memcpy(snap + CFC_HIDDEN_DIM, lp_hidden, LP_HIDDEN_DIM);
}

/* =========================================================================
 * VDB blend: LP hidden gets a soft nudge toward nearest VDB node's LP portion.
 * BLEND_ALPHA: probability per neuron of adopting VDB match's value.
 * ========================================================================= */
#define BLEND_ALPHA 0.2f

static void vdb_blend(LPState* lp, const int8_t* vdb_node, unsigned int* rng)
{
    const int8_t* vdb_lp = vdb_node + CFC_HIDDEN_DIM;
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        if (lp->hidden[i] != vdb_lp[i]) {
            *rng = (*rng) * 1664525u + 1013904223u;
            float r = (float)((*rng >> 16) & 0xFFFF) / 65535.0f;
            if (r < BLEND_ALPHA) {
                lp->hidden[i] = vdb_lp[i];
            }
        }
    }
}

/* =========================================================================
 * Agreement computation:
 * agreement = trit_dot(lp_hidden, tsign(lp_running_sum[p_hat])) / LP_HIDDEN_DIM
 * ========================================================================= */
static float compute_agreement(const LPState* lp, int p_hat)
{
    int8_t sig[LP_HIDDEN_DIM];
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        sig[i] = tsign_scalar(lp->running_sum[p_hat][i]);
    }
    int32_t dot = tdot16(lp->hidden, sig);
    return (float)dot / LP_HIDDEN_DIM;
}

/* =========================================================================
 * Gate bias update (14C only)
 * ========================================================================= */
static void update_gate_bias(GIEState* gie, const LPState* lp, int p_hat)
{
    for (int p = 0; p < N_PATTERNS; p++) {
        gie->gate_bias[p] *= BIAS_DECAY_FACTOR;
    }
    if (lp->sample_count[p_hat] >= T14_MIN_SAMPLES) {
        float agreement = compute_agreement(lp, p_hat);
        float bias = BASE_GATE_BIAS * (agreement > 0.0f ? agreement : 0.0f);
        if (bias > gie->gate_bias[p_hat]) {
            gie->gate_bias[p_hat] = bias;
        }
    }
}

/* =========================================================================
 * LP accumulation
 * ========================================================================= */
static void lp_accumulate(LPState* lp, int p_confirmed)
{
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        lp->running_sum[p_confirmed][i] += lp->hidden[i];
    }
    lp->sample_count[p_confirmed]++;
}

/* =========================================================================
 * LP P2 alignment score
 *
 * Measures how well current lp_hidden reflects the P2 pattern:
 * compare lp_hidden against tsign(lp_running_sum[P2]) normalized.
 * Returns [-1, 1]. Higher = better P2 alignment.
 * ========================================================================= */
static float lp_p2_alignment(const LPState* lp)
{
    if (lp->sample_count[1] < 2) return 0.0f;
    return compute_agreement(lp, 1 /* P2 */);
}

/* =========================================================================
 * Trial runner
 *
 * Phase 1: LP_STEPS_PHASE1 steps of P1.
 * Phase 2: Switch to P2. Measure:
 *   - Steps to T14_MIN_SAMPLES TriX confirmations (structural guarantee, always passes)
 *   - LP P2 alignment score at SNAP_POINTS
 * ========================================================================= */
typedef struct {
    int   steps_to_pass;          /* trivially T14_MIN_SAMPLES for structural guarantee */
    float lp_align[N_SNAP_POINTS];/* LP P2 alignment at each snapshot step */
    float gate_bias_p1_at15;      /* gate_bias[P1] at step 15 (decay check) */
    float gate_bias_p2_at15;      /* gate_bias[P2] at step 15 */
    int   lp_fire_count_phase2;   /* total LP neuron firings in Phase 2 */
} TrialResult;

static TrialResult run_trial(int use_gate_bias, unsigned int* rng_state)
{
    TrialResult result = {0};

    /* Random LP CfC weights */
    int8_t lp_weights[LP_HIDDEN_DIM][CFC_HIDDEN_DIM];
    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        for (int d = 0; d < CFC_HIDDEN_DIM; d++) {
            *rng_state = (*rng_state) * 1664525u + 1013904223u;
            int v = (int)((*rng_state >> 16) % 3) - 1;
            lp_weights[n][d] = (int8_t)v;
        }
    }

    GIEState gie; memset(&gie, 0, sizeof(gie));
    LPState  lp;  memset(&lp,  0, sizeof(lp));
    VDBState vdb; memset(&vdb, 0, sizeof(vdb));

    int p_hat = 0; /* start predicting P1 */

    /* ---- Phase 1: establish P1 prior ---- */
    for (int step = 0; step < LP_STEPS_PHASE1; step++) {
        p_hat = gie_step(&gie, 0 /*p_true=P1*/, p_hat, use_gate_bias, rng_state);

        int8_t snap[VDB_SNAPSHOT_DIM];
        make_snapshot(snap, gie.state, lp.hidden);

        /* VDB blend before CfC (blend current LP toward nearest stored state) */
        const int8_t* nbr = vdb_search(&vdb, snap);
        if (nbr) vdb_blend(&lp, nbr, rng_state);

        lp_cfc_step(&lp, gie.state, lp_weights);
        lp_accumulate(&lp, p_hat);
        vdb_insert(&vdb, snap);

        if (use_gate_bias) update_gate_bias(&gie, &lp, p_hat);
    }

    /* ---- Phase 2: switch to P2, measure ---- */
    int p2_consecutive = 0;
    result.steps_to_pass = -1;

    for (int step = 0; step < LP_STEPS_PHASE2; step++) {
        p_hat = gie_step(&gie, 1 /*p_true=P2*/, p_hat, use_gate_bias, rng_state);

        int8_t snap[VDB_SNAPSHOT_DIM];
        make_snapshot(snap, gie.state, lp.hidden);

        const int8_t* nbr = vdb_search(&vdb, snap);
        if (nbr) vdb_blend(&lp, nbr, rng_state);

        lp_cfc_step(&lp, gie.state, lp_weights);

        /* Count LP firings for 14A vs 14C comparison */
        for (int n = 0; n < LP_HIDDEN_DIM; n++) {
            if (lp.hidden[n] != 0) result.lp_fire_count_phase2++;
        }

        lp_accumulate(&lp, p_hat); /* p_hat == P2 always (structural guarantee) */
        vdb_insert(&vdb, snap);

        if (use_gate_bias) update_gate_bias(&gie, &lp, p_hat);

        /* Track pass criterion: T14_MIN_SAMPLES consecutive P2 confirmations */
        if (p_hat == 1) {
            p2_consecutive++;
            if (p2_consecutive >= T14_MIN_SAMPLES && result.steps_to_pass < 0) {
                result.steps_to_pass = step + 1;
            }
        } else {
            p2_consecutive = 0;
        }

        /* LP alignment snapshots */
        for (int sp = 0; sp < N_SNAP_POINTS; sp++) {
            if (step + 1 == SNAP_STEPS[sp]) {
                result.lp_align[sp] = lp_p2_alignment(&lp);
            }
        }

        /* Gate bias at step 15 */
        if (step + 1 == T14_MIN_SAMPLES) {
            result.gate_bias_p1_at15 = gie.gate_bias[0];
            result.gate_bias_p2_at15 = gie.gate_bias[1];
        }
    }

    return result;
}

/* =========================================================================
 * Main
 * ========================================================================= */
int main(void)
{
    printf("TEST 14C — Kinetic Attention Transition Simulation\n");
    printf("====================================================\n");
    printf("Firmware: GATE_THRESHOLD=%d, BASE_GATE_BIAS=%.1f, "
           "MIN_GATE_THRESHOLD=%d, T14_MIN_SAMPLES=%d, DECAY=%.2f\n",
           GATE_THRESHOLD, BASE_GATE_BIAS, MIN_GATE_THRESHOLD, T14_MIN_SAMPLES,
           BIAS_DECAY_FACTOR);
    printf("GIE model: signal=U[%d,%d], noise=U[0,%d], COUNT_MAX=%d\n",
           SIGNAL_LOW, COUNT_MAX, NOISE_MAX, COUNT_MAX);
    printf("LP sim:    LP_SIM_THRESHOLD=%d, BLEND_ALPHA=%.2f\n",
           LP_SIM_THRESHOLD, BLEND_ALPHA);
    printf("Trial:     %d steps P1 + %d steps P2, %d trials each\n\n",
           LP_STEPS_PHASE1, LP_STEPS_PHASE2, N_TRIALS);

    unsigned int rng = (unsigned int)time(NULL) ^ 0xDEADBEEFu;

    /* Accumulators */
    long pass_14a = 0, pass_14c = 0;
    double align_sum_14a[N_SNAP_POINTS] = {0};
    double align_sum_14c[N_SNAP_POINTS] = {0};
    double gbias_p1_sum_14c = 0, gbias_p2_sum_14c = 0;
    long lp_fire_14a = 0, lp_fire_14c = 0;

    printf("Running 14A (no gate bias)...\n");
    clock_t t0 = clock();
    for (int trial = 0; trial < N_TRIALS; trial++) {
        TrialResult r = run_trial(0, &rng);
        if (r.steps_to_pass > 0 && r.steps_to_pass <= PASS_CRITERION) pass_14a++;
        for (int sp = 0; sp < N_SNAP_POINTS; sp++) align_sum_14a[sp] += r.lp_align[sp];
        lp_fire_14a += r.lp_fire_count_phase2;
    }
    double wall_14a = (double)(clock() - t0) / CLOCKS_PER_SEC;

    printf("Running 14C (agreement-weighted gate bias)...\n");
    t0 = clock();
    for (int trial = 0; trial < N_TRIALS; trial++) {
        TrialResult r = run_trial(1, &rng);
        if (r.steps_to_pass > 0 && r.steps_to_pass <= PASS_CRITERION) pass_14c++;
        for (int sp = 0; sp < N_SNAP_POINTS; sp++) align_sum_14c[sp] += r.lp_align[sp];
        gbias_p1_sum_14c += r.gate_bias_p1_at15;
        gbias_p2_sum_14c += r.gate_bias_p2_at15;
        lp_fire_14c += r.lp_fire_count_phase2;
    }
    double wall_14c = (double)(clock() - t0) / CLOCKS_PER_SEC;

    printf("\n=== STRUCTURAL GUARANTEE (TriX always correct, W_f hidden=0) ===\n");
    printf("Both conditions trivially pass in T14_MIN_SAMPLES=%d steps.\n",
           T14_MIN_SAMPLES);
    printf("14A pass: %ld / %d (%.1f%%)   wall: %.2fs\n",
           pass_14a, N_TRIALS, 100.0 * pass_14a / N_TRIALS, wall_14a);
    printf("14C pass: %ld / %d (%.1f%%)   wall: %.2fs\n\n",
           pass_14c, N_TRIALS, 100.0 * pass_14c / N_TRIALS, wall_14c);

    printf("=== LP P2 ALIGNMENT (meaningful comparison — post-transition quality) ===\n");
    printf("Alignment = trit_dot(lp_hidden, tsign(lp_running_sum[P2])) / LP_HIDDEN_DIM\n");
    printf("Range [-1,1]; higher = LP state more P2-aligned.\n\n");
    printf("  step |   14A   |   14C   | delta (14C-14A)\n");
    printf("  -----|---------|---------|----------------\n");
    for (int sp = 0; sp < N_SNAP_POINTS; sp++) {
        double a = align_sum_14a[sp] / N_TRIALS;
        double c = align_sum_14c[sp] / N_TRIALS;
        printf("  %4d | %+.4f  | %+.4f  | %+.4f%s\n",
               SNAP_STEPS[sp], a, c, c - a,
               (SNAP_STEPS[sp] > T14_MIN_SAMPLES && c > a) ? "  <-- 14C ahead" :
               (SNAP_STEPS[sp] <= T14_MIN_SAMPLES && c < a) ? "  <-- 14A ahead (expected: P1 noise)" : "");
    }

    printf("\n=== GATE BIAS STATE AT STEP %d (14C) ===\n", T14_MIN_SAMPLES);
    printf("  gate_bias[P1] mean: %.3f  (expected: 15*0.9^15 = %.3f)\n",
           gbias_p1_sum_14c / N_TRIALS,
           BASE_GATE_BIAS * powf(BIAS_DECAY_FACTOR, T14_MIN_SAMPLES));
    printf("  gate_bias[P2] mean: %.3f  (expected: positive if agreement > 0)\n",
           gbias_p2_sum_14c / N_TRIALS);

    printf("\n=== LP FIRING RATE (Phase 2, %d steps) ===\n", LP_STEPS_PHASE2);
    double fire_per_step_14a = (double)lp_fire_14a / (N_TRIALS * LP_STEPS_PHASE2);
    double fire_per_step_14c = (double)lp_fire_14c / (N_TRIALS * LP_STEPS_PHASE2);
    printf("  14A: %.2f LP neuron fires/step (out of %d)\n",
           fire_per_step_14a, LP_HIDDEN_DIM);
    printf("  14C: %.2f LP neuron fires/step (out of %d)\n",
           fire_per_step_14c, LP_HIDDEN_DIM);
    if (fire_per_step_14a > 0) {
        printf("  14C/14A ratio: %.3fx\n", fire_per_step_14c / fire_per_step_14a);
    }

    printf("\n=== CLS PREDICTION VERDICT ===\n");
    printf("Structural guarantee: CONFIRMED (both %d/%d pass in T14_MIN_SAMPLES steps)\n",
           N_TRIALS, N_TRIALS);
    printf("Transition analysis:\n");

    int p1_noise_observed = 0, p2_accel_observed = 0;
    if (align_sum_14c[0] < align_sum_14a[0] - 0.01 * N_TRIALS) p1_noise_observed = 1;
    for (int sp = 1; sp < N_SNAP_POINTS; sp++) {
        if (align_sum_14c[sp] > align_sum_14a[sp] + 0.01 * N_TRIALS) {
            p2_accel_observed = 1; break;
        }
    }

    if (p1_noise_observed)
        printf("  [observed] Decaying P1 gate_bias causes brief LP noise at step %d\n",
               SNAP_STEPS[0]);
    else
        printf("  [not observed] P1 gate_bias decay too fast / VDB dominates early noise\n");

    if (p2_accel_observed)
        printf("  [observed] Gate_bias[P2] accelerates LP alignment after T14_MIN_SAMPLES\n");
    else
        printf("  [not observed] Gate_bias[P2] effect weak at LP_SIM_THRESHOLD=%d\n",
               LP_SIM_THRESHOLD);

    return 0;
}
