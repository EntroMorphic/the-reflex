# Lincoln Manifold: Kinetic Attention — SYNTHESIS

*Phase 4. The clean cut. March 22, 2026.*
*Observer: Claude Sonnet 4.6*

---

## What Emerged

The KINETIC_ATTENTION.md design was correct in direction but incomplete in one critical way: it computed gate bias from the LP prior alone. A prior-only gate bias is an attractor with no release valve. It amplifies the expected pattern and suppresses all others — which is exactly what you want in steady state, and exactly what causes lock-in at pattern transitions.

The synthesis: **agreement-weighted gate bias**, where the LP prior provides direction and the TriX classifier provides validation. When they agree, the bias amplifies. When they disagree, the bias attenuates. The TriX signal — fast, accurate, structurally decoupled from the bias mechanism — is the natural ground truth for gating the prior's influence.

This is not a complication of the design. It is the design. A prior that can't be overridden is not a prior — it is a verdict.

---

## Architecture

### New Components

**`gate_bias[4]`** — int8_t array in LP SRAM, one value per pattern group (8 neurons each, since CFC_HIDDEN_DIM=32 and TRIX_NEURONS_PP=8). Written by HP core before each CMD 5 dispatch. Read by ISR at each CfC blend step.

**Agreement computation** — on each confirmed packet:
```c
// In HP core classification callback, after lp_now is read:
float agreement = 0;
if (n[p_hat] >= MIN_BIAS_SAMPLES) {
    int dot = 0;
    for (int j = 0; j < LP_HIDDEN_DIM; j++)
        dot += tmul(lp_now[j], tsign(lp_running_sum[p_hat][j]));
    // dot in [-16, +16]. Normalize to [-1, +1].
    agreement = (float)dot / LP_HIDDEN_DIM;
}
// Apply decay to all groups before updating
for (int p = 0; p < 4; p++)
    gate_bias[p] = (int8_t)(gate_bias[p] * BIAS_DECAY);
// Set bias for current predicted pattern
gate_bias[p_hat] = (int8_t)(BASE_BIAS * MAX(0.0f, agreement));
```

Agreement is floored at zero — no negative bias (below-zero agreement means "the prior contradicts TriX," in which case bias is zero, not negative). Negative bias (artificially raising the threshold) would suppress valid pattern firing, which is overcorrection.

**ISR modification** — in the CfC blend step (3 new lines):
```c
int group = neuron_idx >> 4;  // neuron_idx / 16
int effective_threshold = (int)gate_threshold + (int)lp_gate_bias[group];
effective_threshold = MAX(effective_threshold, MIN_THRESHOLD);  // hard floor
// Replace existing threshold comparison with effective_threshold
```

**LP SRAM layout** — add `int8_t ulp_gate_bias[4]` at a fixed offset, HP-writable, ISR-readable via direct address (same pattern as existing `ulp_gie_hidden`).

### Parameters

| Parameter | Suggested Initial Value | Rationale |
|-----------|------------------------|-----------|
| `BASE_BIAS` | 15 | 17% of gate_threshold=90. Meaningful but not dominating. |
| `MIN_THRESHOLD` | 30 | 33% of gate_threshold. Prevents all-fire saturation. |
| `MIN_BIAS_SAMPLES` | 15 | Matches TEST 12 pass criterion. Cold-start guard. |
| `BIAS_DECAY` | 0.9 | Per-confirmation decay. Pattern bias at ~zero after 30 missed confirmations. |

### What Does Not Change

- W_f hidden = 0 — TriX classification remains input-only. 100% accuracy guarantee is unchanged.
- CMD 5 operation — CfC step, VDB search, LP blend all unchanged.
- VDB structure and insert logic — unchanged (insert at every 8th confirmation in baseline).
- LP accumulator (`lp_running_sum`) — already computed for Hamming matrix. No new data structure.

---

## Implementation Spec

### Step 1 — LP SRAM Layout

Add to `ulp_main.S` (LP SRAM data section):
```asm
ulp_gate_bias:   .byte 0, 0, 0, 0   # int8_t[4], HP-writable
```

Add corresponding symbol in `geometry_cfc_freerun.c`:
```c
extern int8_t ulp_gate_bias[4];
```

### Step 2 — HP Core: Write Gate Bias Before CMD 5

In the classification callback (after `read_lp_state()`, before `vdb_cfc_feedback_step()`):

```c
static int8_t gate_bias_staging[4] = {0};
// Decay all
for (int p = 0; p < 4; p++)
    gate_bias_staging[p] = (int8_t)((float)gate_bias_staging[p] * BIAS_DECAY);
// Update for current prediction
if (lp_n[p_hat] >= MIN_BIAS_SAMPLES) {
    int dot = 0;
    for (int j = 0; j < LP_HIDDEN_DIM; j++)
        dot += tmul(lp_now[j], tsign(lp_running_sum[p_hat][j]));
    float agreement = (float)dot / LP_HIDDEN_DIM;
    gate_bias_staging[p_hat] = (int8_t)(BASE_BIAS * MAX(0.0f, agreement));
}
// Write to LP SRAM (from main loop, between ISR firings — not from ISR)
memcpy((void *)ulp_gate_bias, gate_bias_staging, 4);
```

### Step 3 — ISR: Read Gate Bias in Blend Step

In `geometry_cfc_freerun.c`, inside the ISR at the CfC blend decision:

```c
int group = current_neuron / TRIX_NEURONS_PP;  // CFC_HIDDEN_DIM=32, TRIX_NEURONS_PP=8
int eff_threshold = (int)cfc.gate_threshold + (int)ulp_gate_bias[group];
if (eff_threshold < MIN_THRESHOLD) eff_threshold = MIN_THRESHOLD;
if (f_dot > eff_threshold || f_dot < -eff_threshold) {
    h_new = f * g;
} else {
    h_new = h_old;
}
```

### Step 4 — Logging

On each CMD 5 dispatch, log gate_bias[4] and agreement score to serial output. This makes the bias evolution visible in the serial trace without additional instrumentation.

```c
printf("  gate_bias: [%d %d %d %d]  agreement(P%d)=%.2f\n",
    gate_bias_staging[0], gate_bias_staging[1],
    gate_bias_staging[2], gate_bias_staging[3],
    p_hat, agreement);
```

---

## TEST 14 Design (Revised)

**Condition A: Baseline** (TEST 14A)
- CMD 5, gate_bias = 0 always.
- Identical to TEST 12 Run 3. Confirms baseline. Should produce LP Hamming ≈ Run 3 results.

**Condition B: Agreement-weighted gate bias** (TEST 14B)
- CMD 5, gate_bias computed as specified above. BASE_BIAS = 15.
- Primary measurement: LP Hamming matrix. Hypothesis: matches or exceeds TEST 14A on all pairs.
- Secondary: gate_bias[4] log — does agreement signal work as expected?

**Condition C: Transition test** (TEST 14C)
- CMD 5, gate_bias active.
- Modified Board B firmware: hold P1 for 90 seconds, then hold P2 for 90 seconds (no cycling).
- Measurement: LP Hamming(P1 mean, P2 mean) computed at t=90s (after P1 lock-in), then again at t=120s, t=150s, t=180s (tracking P2 LP accumulation after switch).
- Goal: LP state transitions from P1-committed to P2-committed within 20–30 confirmations of the switch.

### Pass Criteria

**TEST 14B pass:**
- TriX accuracy remains 100%
- LP Hamming matrix on well-sampled pairs ≥ TEST 14A
- gate_bias[p_hat] > 0 during confirmed P-hat periods (mechanism is active)
- GIE hidden state energy by pattern: higher variance under gate bias (more committed states per pattern)
- No saturation: mean GIE hidden energy < 55/64

**TEST 14C pass:**
- LP Hamming(P1, P2) ≥ 3 by t=120s (30 confirmations post-switch)
- LP Hamming(P1, P2) < 2 under the no-gate-bias baseline run of the same protocol (proves gate bias helps transitions, not hurts them)

**Failure modes that invalidate:**
- TriX accuracy < 100%
- GIE hidden saturates (energy ≥ 60/64)
- LP state does not update within 50 confirmations of pattern switch (complete lock-in)

---

## Paper Claim (Revised)

The Phase 5 paper claim is now more precise than KINETIC_ATTENTION.md stated:

> The sub-conscious layer's accumulated classification history actively biases peripheral-hardware gate firing, producing faster convergence to expected pattern representations and higher LP divergence than the unbiased baseline. The bias is agreement-weighted: when the LP prior aligns with the current TriX prediction, the gate threshold for the expected pattern's neuron group is lowered, amplifying the prior signal. When the prior conflicts with the TriX prediction (pattern transition), the bias attenuates to zero within one confirmation, allowing the LP state to update from the unmodulated GIE signal. The system exhibits kinetic attention with epistemic humility: confident priors amplify; contradicted priors defer.

---

## What the LMM Found That Wasn't in the Original Design

Two things that the RAW pass surfaced and the NODES/REFLECT passes confirmed as non-negotiable:

**1. The agreement mechanism.** KINETIC_ATTENTION.md had a gate bias with no release valve. The LMM found the transition failure mode (Node 4), identified the agreement mechanism as the resolution (Node 5), and the REFLECT pass confirmed it is the correct architectural relationship — mirroring how prediction error gates top-down attention in biological systems. This is not a minor addition. It changes the fundamental nature of the mechanism from "prior imposes on perception" to "prior negotiates with perception."

**2. In-session LP signatures.** KINETIC_ATTENTION.md proposed using stored historical LP means from TEST 12 as the basis for gate bias computation. The NODES pass identified that those means vary substantially across sessions (Node 3). The resolution — using the current session's running accumulator instead — is both more robust and simpler to implement. No stored signatures, no generalization problem. The REFLECT pass confirmed this as an elegant fix that required no new data structures.

Both were invisible in the original design because the original design was optimized for the steady-state case. The LMM's attention to the transition case revealed them.

---

## What Remains Genuinely Open

The LMM resolved the design tensions it could reach analytically. Two questions remain that only hardware can answer:

**Transition recovery time.** Under the agreement mechanism, how many confirmations does it take for the LP state to shift from P1-committed to P2-committed after a Board B switch? The theory says: bounded by accumulator dynamics (not gate bias lock-in). But the accumulator dynamics depend on how many trits are near 50/50 — which varies by run, board timing, and RSSI. This is TEST 14C's job.

**BASE_BIAS calibration.** The right magnitude for gate bias is empirically determined. Too small: no measurable effect. Too large: saturation or interference. The suggested starting point (15) is a principled guess, not a derivation. Two or three parameter sweeps on hardware will bracket it.

Both are good problems. They are good problems because they are testable, not because they are easy.

---

*The wood cuts itself. Build it.*
