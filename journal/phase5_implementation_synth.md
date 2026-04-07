# Lincoln Manifold: Phase 5 Implementation — SYNTHESIS

*Phase 4. The clean cut. April 6, 2026.*
*Observer: Claude Opus 4.6*
*Depends on: Audit commit `c815869` (13/13 PASS), March 22 LMM design cycle, LP CHAR data.*

---

## What Emerged

The implementation is 135 lines of new code, no new files, no new data structures beyond one 4-byte volatile array. The design was solved in March. The refactor was completed today. What remains is mechanical — but the calibration is the make-or-break.

Three findings from this LMM cycle that the March 22 cycle missed:

1. **Sign error in the synthesis spec.** `threshold + bias` should be `threshold - bias`. The sim got it right. The spec didn't. Fixed here.

2. **Gate bias belongs in HP BSS, not LP SRAM.** Both the writer (test harness) and the reader (ISR) run on the HP core. LP SRAM adds bus contention for no benefit.

3. **The sim is miscalibrated 3× on blend rate.** Hardware blends 10/16 trits per step; the sim uses alpha=0.2 (expecting ~3/16). Recalibration may collapse the predicted effect. Must check before writing firmware.

---

## Implementation Spec

### Step 0: Sim Recalibration (30 minutes)

Before touching firmware:

1. In `sim/test14c.c`, change:
   - `LP_SIM_THRESHOLD` from 2 to 1 (targeting ~13/16 fires per step)
   - `BLEND_ALPHA` from 0.2 to 0.6 (targeting ~10/16 trits modified per step)

2. Rebuild and run: `make -C sim && ./sim/test14c`

3. Read the output:
   - If lp_delta > 0 at step 30 for 14C and/or 14C-iso: **proceed to Step 1**
   - If lp_delta ≈ 0: try BASE_GATE_BIAS = 30, then 45. If still null at 45: proceed to Step 1 anyway — the sim can't model RF dynamics; hardware is the real test

### Step 1: Engine Mechanism (gie_engine.c + gie_engine.h)

**gie_engine.h** — add to the ISR state section:

```c
/* Phase 5: per-group gate bias (agreement-weighted kinetic attention) */
#define BASE_GATE_BIAS      15      /* max bias magnitude at full agreement */
#define MIN_GATE_THRESHOLD   30     /* hard floor: 33% of gate_threshold=90 */
extern volatile int8_t gie_gate_bias[TRIX_NUM_PATTERNS];
extern volatile int32_t gie_gate_fires_per_group[TRIX_NUM_PATTERNS];
```

**gie_engine.c** — add to BSS declarations:

```c
volatile int8_t gie_gate_bias[TRIX_NUM_PATTERNS] = {0};
volatile int32_t gie_gate_fires_per_group[TRIX_NUM_PATTERNS] = {0};
```

**gie_engine.c** — modify `isr_loop_boundary()` step 4, replace the inner loop body:

```c
/* Current code (step 4): */
int8_t f;
if (thresh > 0) {
    f = (f_dot > thresh || f_dot < -thresh) ? tsign(f_dot) : T_ZERO;
} else {
    f = tsign(f_dot);
}

/* Phase 5 replacement: */
int group = n / TRIX_NEURONS_PP;
int32_t eff = thresh - (int32_t)gie_gate_bias[group];
if (eff < MIN_GATE_THRESHOLD) eff = MIN_GATE_THRESHOLD;
int8_t f;
if (eff > 0) {
    f = (f_dot > eff || f_dot < -eff) ? tsign(f_dot) : T_ZERO;
} else {
    f = tsign(f_dot);
}
/* Per-group fire counter */
if (f != T_ZERO) gie_gate_fires_per_group[group]++;
```

**Validation:** Before implementing the agreement computation, test the mechanism manually. In app_main, after engine init:
```c
gie_gate_bias[0] = 15;  /* group 0 fires more easily */
gie_gate_bias[1] = 0;
gie_gate_bias[2] = 0;
gie_gate_bias[3] = 0;
/* Run for 1s, check gie_gate_fires_per_group[0] > others */
```

If group 0 fires more: mechanism works. If not: debug before proceeding.

### Step 2: Agreement Computation (geometry_cfc_freerun.c)

In the TEST 14 function, after reading `lp_now` from LP SRAM:

```c
/* Agreement-weighted gate bias update */
#define BIAS_DECAY_FACTOR  0.9f
#define T14_MIN_SAMPLES    15

static float gate_bias_f[TRIX_NUM_PATTERNS] = {0};

/* Decay all groups */
for (int p = 0; p < TRIX_NUM_PATTERNS; p++)
    gate_bias_f[p] *= BIAS_DECAY_FACTOR;

/* Update for current predicted pattern */
if (lp_sample_count[p_hat] >= T14_MIN_SAMPLES) {
    int8_t lp_mean[LP_HIDDEN_DIM];
    for (int j = 0; j < LP_HIDDEN_DIM; j++)
        lp_mean[j] = tsign(lp_running_sum[p_hat][j]);

    int dot = 0;
    for (int j = 0; j < LP_HIDDEN_DIM; j++)
        dot += tmul(lp_now[j], lp_mean[j]);

    float agreement = (float)dot / LP_HIDDEN_DIM;
    float bias = BASE_GATE_BIAS * (agreement > 0.0f ? agreement : 0.0f);
    if (bias > gate_bias_f[p_hat]) gate_bias_f[p_hat] = bias;
}

/* Write to engine (atomic per-byte on RISC-V) */
for (int p = 0; p < TRIX_NUM_PATTERNS; p++)
    gie_gate_bias[p] = (int8_t)gate_bias_f[p];
```

Note: `if (bias > gate_bias_f[p_hat])` — the bias only goes UP from agreement, never down except through decay. This prevents a single low-agreement confirmation from slamming the bias to zero. The decay handles the gradual reduction.

### Step 3: TEST 14 Conditions (geometry_cfc_freerun.c)

One test function, three conditions:

```
TEST 14A: baseline (gie_gate_bias = {0,0,0,0} always, 120s)
TEST 14C: full agreement-weighted bias (120s)  
TEST 14C-iso: bias Phase 2 only (no bias during P1 accumulation, 120s)
```

Each condition:
1. Reset: cfc_init, vdb_clear, LP hidden zero, accumulators zero, gate_bias zero, per-group fire counters zero
2. Run: 120s classification loop with appropriate bias policy
3. Capture: LP Hamming matrix, gate_bias trace at 5 snapshot points, per-group fire rates, lp_delta at transition points

Pass criteria (from the sim + March 22 synthesis):
- TriX accuracy remains 100% (structural guarantee, but measure)
- LP Hamming matrix: 14C matches or exceeds 14A on ≥ 4 of 6 pairs
- Gate bias activates: `max(gie_gate_bias) > 0` during confirmed periods
- No saturation: mean GIE hidden energy < 55/64
- Per-group fire rate: biased group fires more than unbiased groups

### Step 4: Logging

On each classification callback iteration, log (periodic, not every packet):

```c
if (step % 100 == 0) {
    printf("  step %d: bias=[%d %d %d %d] agree=%.2f p_hat=%d fires=[%d %d %d %d]\n",
           step, gie_gate_bias[0], gie_gate_bias[1],
           gie_gate_bias[2], gie_gate_bias[3],
           agreement, p_hat,
           gie_gate_fires_per_group[0], gie_gate_fires_per_group[1],
           gie_gate_fires_per_group[2], gie_gate_fires_per_group[3]);
}
```

---

## Execution Order

| Step | Action | Time | Gate |
|------|--------|------|------|
| 0 | Recalibrate sim, re-run | 30 min | Effect survives? |
| 1 | Add `gie_gate_bias[4]` + ISR modification | 15 min | Manual validation: group 0 fires more |
| 2 | Agreement computation in test harness | 15 min | — |
| 3 | TEST 14 conditions + logging | 30 min | — |
| 4 | Build, flash, run TEST 14 | 15 min | 14/14 PASS? |
| 5 | Interpret results | — | Paper-ready? |

Total: ~2 hours of implementation + 1 run on silicon.

---

## What the LMM Found That Wasn't Obvious

1. **The sign error.** Three documents, three different sign conventions. The spec was wrong. The sim was right. Without tracing through all three, this would have been a silent firmware bug that reversed the mechanism.

2. **BSS vs LP SRAM.** The March 22 synthesis assumed LP SRAM because "the ISR reads it" — conflating the LP core's execution context with the ISR's. The ISR runs on the HP core. It reads HP BSS. This eliminates 13,760 LP SRAM reads per second.

3. **The sim calibration gap.** Hardware fires 15/16 LP trits per step. The sim fires ~5/16. The mechanism was validated at unrealistic sparsity. Recalibration is the honest prerequisite.

4. **Natural cycling is a harder test.** The sim uses a controlled 90s-hold-then-switch. The hardware sender cycles every 27 seconds. If the mechanism works under continuous cycling, the result is stronger than the sim predicted. If it doesn't, we need the controlled sender mode — but we try the harder test first.

---

## Success Criteria

The implementation is complete when:
- `RESULTS: 14 / 14 PASSED` (or `N / 14` with only known-failing tests)
- Gate bias values are non-zero during confirmed-pattern periods
- Per-group fire rate differs between biased and unbiased groups
- LP Hamming matrix under 14C ≥ 14A on majority of pairs

The paper is ready to draft when:
- TEST 14C shows lp_delta > 0 at a measured transition point, OR
- Per-group gate firing rate provides the evidence that LP Hamming cannot

---

*The design was the hard part. The refactor cleared the path. What remains is the honest work of making the silicon demonstrate what the theory predicts — or show us why it can't.*
