# Lincoln Manifold: TriX-Dispatched LP Feedback — SYNTHESIS

*Phase 4. The clean cut. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## What Emerged

The transition headwind — gate bias consistently hurting P1→P2 crossover speed in all 3 seeds — is not a bias mechanism problem. It is a data quality problem. The LP accumulators are contaminated by CPU core_pred's 20% misclassification rate (systematic P0↔P1 confusion). The agreement signal reads contaminated accumulators and produces artificially elevated agreement during transitions, delaying bias release.

The fix: dispatch LP feedback from the TriX ISR classification (100% accuracy, structural guarantee) instead of CPU core_pred (80% accuracy, systematic bias). Keep CPU core_pred for novelty gating only (reject/accept decision). The classification label — which pattern accumulator receives this sample — comes from TriX.

This is not a new mechanism. It is finishing the wiring that the agreement-weighted gate bias design assumed from the start.

---

## Implementation Spec

### The Change (per test function)

In `geometry_cfc_freerun.c`, in `run_test_12()`, `run_test_13()`, `run_test_14()`, and `run_test_14c()`:

**Before:**
```c
/* CPU classification */
int core_best = -9999, core_pred = 0;
for (int p = 0; p < NUM_TEMPLATES; p++) {
    int d = 0;
    for (int j = 0; j < CFC_INPUT_DIM; j++) {
        if (sig[p][j] != T_ZERO && cfc.input[j] != T_ZERO)
            d += tmul(sig[p][j], cfc.input[j]);
    }
    if (d > core_best) { core_best = d; core_pred = p; }
}
if (core_best < NOVELTY_THRESHOLD) continue;

int pred = core_pred;
```

**After:**
```c
/* CPU classification (novelty gate only) */
int core_best = -9999, core_pred = 0;
for (int p = 0; p < NUM_TEMPLATES; p++) {
    int d = 0;
    for (int j = 0; j < CFC_INPUT_DIM; j++) {
        if (sig[p][j] != T_ZERO && cfc.input[j] != T_ZERO)
            d += tmul(sig[p][j], cfc.input[j]);
    }
    if (d > core_best) { core_best = d; core_pred = p; }
}
if (core_best < NOVELTY_THRESHOLD) continue;

/* Pattern label from TriX ISR (100% accuracy, W_f hidden = 0) */
int pred = (int)trix_pred;
```

### What Changes

One line per test function: `int pred = core_pred;` → `int pred = (int)trix_pred;`

### What Does Not Change

- Novelty gate (core_pred score >= NOVELTY_THRESHOLD) — preserved as reject/accept filter
- LP accumulator logic — unchanged, receives cleaner labels
- Gate bias computation — unchanged, reads cleaner accumulators
- VDB insert — unchanged
- ISR / GIE / peripheral hardware — unchanged
- LP core assembly — unchanged
- W_f hidden = 0 — unchanged

### Parameters

No new parameters. No parameter changes. The existing NOVELTY_THRESHOLD (60), BASE_GATE_BIAS (15), MIN_GATE_THRESHOLD (30), BIAS_DECAY (0.9), and MIN_BIAS_SAMPLES (15) are all retained.

---

## Verification Plan

### Step 1: Multi-seed TEST 14C with TriX dispatch

Same protocol as the runs we just completed:
- Sender: TRANSITION_MODE (P1 90s → P2 30s)
- Seeds: 0xCAFE1234, 0xDEAD5678, 0xBEEF9ABC
- Mode: SKIP_TO_14C (Test 11 enrollment + Test 14C only)

**Pass criteria:**
1. Full condition crossover step ≤ no-bias crossover step in ≥ 2 of 3 seeds (bias no longer hurts transitions)
2. No-bias alignment margin (P2 - P1) ≥ CPU-dispatch baseline at step +20 (cleaner accumulators help even without bias)
3. TriX accuracy 15/15 in all conditions with valid signatures (P1 and P2 enrolled)

**Comparison data (CPU core_pred dispatch, for reference):**

| Seed | Full crossover | No-bias crossover | Ablation crossover |
|------|:-:|:-:|:-:|
| A (0xCAFE1234) | 18 | 0 | 0 |
| B (0xDEAD5678) | 0 | 0 | 0 |
| C (0xBEEF9ABC) | 7 | 0 | 0 |

The target: Full crossover ≤ no-bias crossover in all seeds. Ideally Full = 0 (no headwind).

### Step 2: Full test suite with normal sender

After 14C verification, rebuild with the normal sender (4-pattern cycling) and run the full test suite (no SKIP_TO_14C) with seed 0xCAFE1234. Verify:
- 15/15 PASS
- TriX dispatch doesn't regress any existing test
- TEST 12 LP divergence ≥ CPU-dispatch baseline

### Step 3: Paper updates

If verification passes:
- Remove Limitation #7 from PAPER_KINETIC_ATTENTION.md
- Update Section 2.2 to state TriX dispatch
- Update PAPER_CLS_ARCHITECTURE.md with multi-seed 14C data
- Remove "multi-seed 14C replication pending" caveat

---

## What the LMM Found

The RAW pass identified the contamination mechanism: CPU core_pred's systematic P0-P1 confusion pollutes the LP accumulators, which inflates the agreement signal, which delays bias release during transitions.

The NODES pass traced the contamination path precisely: 2-4 trits out of 16 are affected, producing 12-25% agreement inflation — enough to keep the bias active for several extra steps.

The REFLECT pass resolved the key tension: the novelty gate and the classification label are different questions that don't need the same answer source. Hybrid dispatch (CPU novelty gate + TriX label) preserves the noise filter while fixing the accuracy.

The critical insight from REFLECT: the P2 accumulator cold-start under TriX dispatch is a *feature*. Under CPU core_pred, the cold-start guard was being bypassed by ~60 misclassified samples that pre-loaded the P2 accumulator with noise. Under TriX dispatch, the cold-start guard activates correctly, producing the optimal transition behavior: no bias during reorientation (which the data shows is the fastest mode), then clean bias activation once the new pattern is established.

The agreement-weighted gate bias was designed for clean input. We've been feeding it dirty input and blaming the mechanism. The fix is the input, not the mechanism.

---

*The bias decay rate was never the problem. The accumulator was.*
