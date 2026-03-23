# Kinetic Attention: Closing the Loop from LP Prior to GIE Behavior

**The Reflex Project — Phase 5 Design Document**

*Written March 22, 2026. Motivated by TEST 12/13 results (commit `12aa970`).*

---

## Abstract

Phase 4 (TEST 12/13) proved that the LP core develops pattern-specific internal states from classification history, and that VDB episodic memory is causally necessary for this differentiation — CfC integration alone collapses P1 and P2 to identical representations in 2 of 3 hardware runs. The modulation demonstrated is **potential**: the LP state contains pattern information but does not yet influence what the GIE perceives. Phase 5 closes the remaining gap: LP hidden state biases GIE gate thresholds, making the peripheral hardware compute differently based on recent experience. This converts potential modulation into **kinetic attention** — the sub-conscious layer's accumulated priors actively shape what the perceptual layer fires on.

---

## 1. What Phase 4 Proved and What It Left Open

### 1.1 What Is Now on Silicon

The March 22 session established, with hardware evidence and a paired ablation control:

1. The GIE classifies four transmission patterns at 100% accuracy using peripheral-hardware ternary dot products at 430.8 Hz.
2. With CMD 5 active, the LP core develops statistically distinct internal states per pattern after 90 seconds of live operation.
3. VDB episodic memory is causally necessary: CMD 4 (CfC only, no blend) produces P1=P2 (Hamming=0) in 2 of 3 runs. CMD 5 (CfC + VDB blend) produces Hamming 1–5 for the same pair across all runs.
4. The mechanism is episodic disambiguation: VDB retrieval routes around the CfC's random-projection degeneracy via a pattern-specific second path.
5. Classification accuracy is unchanged (100%) — the memory pathway is architecturally decoupled from the classification pathway via W_f hidden = 0.

### 1.2 What Remains Unbuilt

The LP state contains pattern-specific information. Nothing reads it back into the GIE computation. The modulation loop is:

```
RF → GIE (perceive) → TriX (classify) → VDB insert → LP CMD 5 (remember)
                                                              ↓
                                              lp_hidden contains pattern prior
                                                              ↓
                                              ??? (currently: nothing)
```

The `???` is Phase 5. The LP state needs to write back into the GIE's attention mechanism — changing how the GIE fires based on what the LP core has accumulated. Until that wire exists, the system is a passive learner. Phase 5 makes it an active one.

---

## 2. The Degeneracy Finding and Its Theoretical Significance

### 2.1 The Empirical Observation

The most important result of March 22 is not TEST 12 (CMD 5 diverges) — it is the interaction between TEST 12 and TEST 13. The GIE correctly classifies P1 and P2 at 100%. The LP CfC, with random untrained weights, collapses P1 and P2 to the same LP representation in 2 of 3 independent hardware runs. These two facts coexist without contradiction: the discrimination signal is present at the GIE layer, but the projection from GIE space to LP space is lossy, and the loss eliminates the P1/P2 distinction along the dominant projection direction.

VDB feedback bypasses this bottleneck. Because the VDB query is 67% GIE hidden (which is pattern-distinct), retrieved memories are pattern-appropriate. The retrieved LP-hidden portion carries what the LP state looked like during past P1 or P2 events. Blending it in displaces the LP state from the degenerate attractor toward the pattern-specific region. The bottleneck is routed around, not resolved.

### 2.2 Complementary Learning Systems Theory

This structure has a direct and non-superficial parallel to the Complementary Learning Systems (CLS) theory of mammalian memory (McClelland, McNaughton & O'Reilly, 1995).

CLS proposes that the brain uses two learning systems in complementary roles:

- **Hippocampus**: Fast-learning, episode-specific, high-fidelity storage. Can bind arbitrary patterns after a single exposure. Does not generalize — each episode is stored as-is.
- **Neocortex**: Slow-learning, distributed, statistical. Extracts shared structure across many experiences. Cannot learn quickly without catastrophic interference.

The tension: new experiences must be encoded immediately (hippocampus), but the slow statistical extractor (neocortex) needs many repetitions to incorporate them. The hippocampus solves the immediate encoding problem; it then "trains" the neocortex through offline replay.

**In the Reflex architecture:**

| Biological | Reflex Analog |
|-----------|---------------|
| Hippocampus | VDB (NSW graph) |
| Neocortex | LP CfC (random weights, slow/never updating) |
| Episode | 48-trit snapshot `[gie_hidden \| lp_hidden]` |
| Retrieval cue | 67% GIE hidden (pattern-correlated) |
| Neocortical learning | Not implemented (weights never update) |

The CfC's inability to separate P1 and P2 is structurally identical to the neocortex's inability to form distinct representations from few exposures — the slow learner hasn't had enough time (or in our case, will never have time, because it doesn't update). The VDB's ability to retrieve the correct episode despite LP ambiguity is structurally identical to hippocampal pattern completion: given a partial cue (the GIE state), retrieve the full episode (including the LP-hidden portion that the CfC can't represent).

**The critical structural difference:** In biological CLS, the hippocampus eventually trains the neocortex through memory replay during sleep, and the episodic memory is gradually consolidated into the slow learner. In the Reflex architecture, this consolidation never happens — the CfC weights are fixed. The VDB therefore permanently compensates for the CfC's degeneracy rather than training it away. This is not a limitation to be fixed; it is a design choice with implications:

- The system's behavior is controlled by episodic memory indefinitely, not just during early learning
- The VDB is not a scaffold to be discarded after consolidation — it is a permanent, load-bearing part of the computation
- Pillar 3 (Hebbian GIE updates) is the path toward biological-style consolidation, where experience eventually changes the weights and the VDB becomes less necessary

### 2.3 Why This Matters for the Phase 5 Paper

Phase 4 demonstrated the hippocampal side: episodic storage and retrieval. Phase 5 demonstrates the attentional consequence: prior experience shapes future perception. The full CLS story in the Reflex is:

> Past experience (VDB) resolves the slow learner's degeneracy (CfC) AND biases what the fast perceptual system (GIE) computes next.

That is the paper claim. Phase 4 proved the first clause. Phase 5 closes the second.

---

## 3. The Architectural Gap: Potential vs. Kinetic Modulation

### 3.1 Current Information Flow (Phase 4)

```
GIE hidden (pattern-correlated)
    → CMD 5 CfC step → lp_hidden (pattern-specific prior)
    → VDB blend → lp_hidden (further differentiated)
    → lp_hidden written to HP SRAM → read by HP core → monitoring only
```

lp_hidden reaches the HP core. The HP core reads it for logging. Nothing feeds it back into the GIE computation path.

### 3.2 The Proposed Phase 5 Loop

```
lp_hidden (pattern-specific prior)
    → HP core maps lp_hidden → gate_bias[4] (one per pattern group)
    → gate_bias written to GIE control register / SRAM
    → ISR reads gate_bias[neuron_group] at each CfC blend step
    → effective_threshold = gate_threshold + gate_bias[group]
    → neurons in "expected" pattern group fire more easily
    → GIE hidden evolves faster toward expected pattern
    → VDB snapshots from this period are more pattern-distinct
    → LP prior reinforced on next CMD 5 step
```

The loop is now closed kinetically. Past experience (encoded in lp_hidden) influences present perception (GIE gate firing). Classification accuracy should remain 100% (W_f hidden = 0 is unchanged; TriX scores are still input-only). The effect should appear in:

- GIE hidden state evolution rate (faster convergence to pattern representation when LP prior is aligned)
- LP hidden divergence (higher Hamming across patterns because each prior actively amplifies its own signal)
- Novelty gate behavior under unexpected input (high LP prior for P1 → unfamiliar packet arrives → P1 neurons fire at lowered threshold → unexpected result → novelty is suppressed or amplified depending on sign)

---

## 4. Implementation Design: Phase 5A Gate Bias

### 4.1 The Minimal Viable Version

The simplest version adds a single new LP SRAM variable: `lp_gate_bias` (int8_t, scalar). The HP core computes this from lp_hidden and writes it before dispatching CMD 5. The ISR reads it and adjusts `gate_threshold` by that amount.

```c
// HP core: compute scalar gate bias from LP hidden state energy
// More "committed" LP state (higher energy) → lower threshold (fires more easily)
int8_t lp_gate_bias = 0;
int lp_energy = 0;
for (int j = 0; j < LP_HIDDEN_DIM; j++) lp_energy += abs(lp_now[j]);
// Map energy [0, 16] to bias [-20, +10]
// High energy (committed state) → lower threshold
// Low energy (uncertain state) → no change or slight increase
lp_gate_bias = (int8_t)(10 - (lp_energy * 30 / 16));
```

This is the most conservative version: it doesn't require the LP core to know anything about pattern groups. It simply says: if the LP state is strongly committed (high energy), make GIE neurons slightly easier to fire overall. If the LP state is ambiguous (low energy, many zeros), leave the threshold unchanged.

### 4.2 The Per-Group Version

The stronger version adds `lp_gate_bias[4]` (one per pattern group). The ISR maps neuron index to group (`group = neuron_idx / 16` for 64 neurons / 4 patterns) and applies the group-specific bias.

```c
// HP core: project lp_hidden onto pattern signatures to compute per-group bias
int8_t lp_gate_bias[4] = {0, 0, 0, 0};
for (int p = 0; p < 4; p++) {
    int alignment = 0;
    for (int j = 0; j < LP_HIDDEN_DIM; j++)
        alignment += tmul(lp_now[j], lp_sig[p][j]);  // dot product with LP signature
    // alignment > 0: LP state matches pattern p → lower threshold (amplify)
    // alignment < 0: LP state opposes pattern p → raise threshold (suppress)
    lp_gate_bias[p] = (int8_t)clamp(alignment * (-2), -30, +30);
}
```

This requires pre-computed LP-space signatures `lp_sig[p][j]` — the mean LP hidden state per pattern from TEST 12/13. These are already available from the TEST 12 results. The HP core can load them at init time and use them to project each new lp_hidden reading onto the four pattern axes.

The ISR change is minimal:

```c
// In ISR, at CfC blend step:
// CFC_HIDDEN_DIM=32, TRIX_NEURONS_PP=8 → 4 groups of 8 neurons each
int group = neuron_idx / TRIX_NEURONS_PP;  // 0→7: P0, 8→15: P1, 16→23: P2, 24→31: P3
int effective_threshold = gate_threshold + (int)lp_gate_bias[group];
effective_threshold = MAX(effective_threshold, 10);  // hard floor
if (f_dot > effective_threshold || f_dot < -effective_threshold) {
    h_new = f * g;  // fire
} else {
    h_new = h_old;  // hold
}
```

The hard floor on effective_threshold prevents the bias from collapsing to zero (which would make every neuron fire and saturate the hidden state).

### 4.3 Stability Considerations

The kinetic loop introduces a positive feedback path:

```
LP prior → lower threshold for pattern P → P neurons fire more →
GIE hidden more P-like → VDB retrieves more P memories →
LP prior more P-specific → threshold even lower → ...
```

This is an attractor dynamic. Without damping, it risks lock-in — the system becomes unable to update when the actual pattern changes.

Three mechanisms resist this:

1. **The ternary HOLD damper** — already demonstrated stable in TEST 8/12. Conflict between current LP state and retrieved memory produces zeros. HOLD preserves zeros. The LP state cannot saturate to all-ones by feedback alone.
2. **The hard floor on effective_threshold** — prevents threshold from dropping to the point where every neuron fires. A floor of `gate_threshold / 3` (e.g., 30 out of 90) ensures at least some discrimination remains.
3. **The novelty gate** — packets with TriX score < 60 don't trigger LP feedback at all. A sudden pattern change will produce a burst of low-confidence packets, temporarily suppressing LP feedback, which gives the LP state time to decay toward neutral before the new pattern's memories overwhelm it.

The key empirical test: is the system capable of updating when Board B switches patterns? TEST 14 should deliberately include pattern switches and measure the LP bias evolution across the switch boundary.

### 4.4 What Stays Unchanged

- TriX classification: W_f hidden = 0, so f_dot is still input-only. Gate bias changes whether a neuron fires but not what score it contributes to TriX argmax. Classification accuracy is expected to remain 100%.
- VDB structure: unchanged. Inserts happen at the same rate and in the same format.
- LP core CMD 5 operation: unchanged. The only new variable is `lp_gate_bias`, which the HP core writes to the GIE control area before dispatching CMD 5.

---

## 5. Proposed TEST 14: Kinetic Attention Verification

### 5.1 Objective

Demonstrate that LP hidden state biases GIE gate firing, producing measurably different GIE hidden state evolution than an unbiased baseline. Specifically: with gate bias enabled, the GIE hidden state should converge faster toward the expected pattern's representation during confirmed-pattern periods, and the LP hidden state should show higher inter-pattern Hamming distances than TEST 12 (potential modulation alone).

### 5.2 Conditions

**TEST 14A — Baseline (Phase 5A scalar bias, conservative):**
- Same 90s + 90s structure as TEST 12/13
- CMD 5 active with scalar `lp_gate_bias` derived from lp_hidden energy
- Measure: per-step GIE hidden state energy by pattern; LP Hamming matrix

**TEST 14B — Per-group bias:**
- CMD 5 with per-group `lp_gate_bias[4]` from LP-space signature projection
- Measure: same as 14A plus pattern-transition response time

**TEST 14C — Bias ablation (CMD 5, bias disabled):**
- Identical to 14A/14B but `lp_gate_bias = 0` always
- This is essentially a replicate of TEST 12, Run 3 — confirms the baseline

### 5.3 What Would Constitute Evidence of Kinetic Attention

**Positive evidence:**
- LP Hamming matrix under TEST 14B exceeds TEST 12's matrix on matched pairs (bias amplified the prior effect)
- GIE hidden state energy by pattern: under bias, the "expected" pattern's neurons fire at a higher rate in the first N packets after a pattern switch (faster convergence)
- LP bias resets on pattern switch: when Board B switches from P1 to P2, the lp_gate_bias[1] (P1 bias) decays and lp_gate_bias[2] (P2 bias) grows over 5–10 packets

**Negative evidence that would require reassessment:**
- LP Hamming same or lower under bias (bias is not amplifying the prior)
- Classification accuracy drops below 100% (bias is interfering with TriX)
- System locks into a single pattern and cannot update after Board B switches (stability failure)
- GIE hidden state saturates (too many neurons firing due to excessive threshold reduction)

### 5.4 The Paper Claim

If TEST 14B succeeds:

> The LP core's accumulated classification history actively biases peripheral-hardware gate firing, producing faster convergence to expected pattern representations and higher LP divergence than the passive (potential modulation only) baseline. The system is capable of kinetic attention: prior experience changes what the perceptual substrate computes, without CPU intervention between classification events, without floating-point arithmetic, and without explicit attentional programming.

This is a qualitatively different claim from TEST 12. TEST 12 showed the LP state reflects experience. TEST 14 would show the LP state shapes perception.

---

## 6. Relationship to the Broader Roadmap

### 6.1 Phase 5A (Gate Bias) as Prerequisite for Pillar 3 (Hebbian Learning)

The ROADMAP.md Pillar 3 proposes Hebbian online learning — modifying GIE weights based on VDB mismatch. Gate bias (Phase 5A) is a softer precursor: rather than modifying weights, it modifies firing thresholds. Both accomplish the same goal (shaping what fires) through different mechanisms. Gate bias is:

- Reversible: setting bias back to zero restores the original threshold
- Non-destructive: the learned signatures (W_f) are unchanged
- Faster to implement: one new SRAM variable, minor ISR modification

Hebbian weight updates are:
- Persistent: once a weight changes, the descriptor chain must be re-encoded (non-trivial during a live run)
- Potentially catastrophic: a bad update during a pattern switch could corrupt the signature for multiple neurons
- More powerful: the weight update persists across power cycles if stored in flash

Phase 5A (gate bias) should be implemented and validated before attempting Pillar 3. If gate bias destabilizes the system, Pillar 3 is not ready. If gate bias demonstrates clean amplification without lock-in, Pillar 3 becomes the natural extension.

### 6.2 Phase 5B (Reverse Projection) as Prerequisite for Pillar 2 (SAMA)

ROADMAP.md Pillar 2 proposes treating ESP-NOW packets from other robots as GIE inputs — "wireless ETM." For this to work, the receiving robot's LP state needs to be able to modulate how it responds to the incoming signal. A robot that has been doing P1 tasks should process an incoming P1-style signal differently than one that has been doing P2 tasks. That context-dependent response is exactly what gate bias provides.

Pillar 2 without Pillar 5A is a passive relay: the incoming packet fires neurons and updates LP state, but the robot's prior context doesn't shape the response. Pillar 2 with Pillar 5A is genuine context-sensitive coordination: Robot A's state shapes how it processes Robot B's signal.

### 6.3 The VDB as Permanent Architecture, Not Training Scaffold

The CLS parallel has a practical implication for system design. In biological CLS, the hippocampus is expected to eventually transfer its knowledge to the cortex and become less necessary. In the Reflex architecture, this transfer never happens — the CfC weights don't update. This means:

- The VDB is a permanent, load-bearing component, not a temporary scaffold
- Memory pruning (ROADMAP Pillar 1 — Dynamic Scaffolding) should not remove memories that the CfC can't represent independently; those memories are permanently necessary
- The VDB's capacity limit (64 nodes, 16KB LP SRAM) is a hard constraint on how many distinct context states the system can maintain simultaneously
- Extending the VDB to HP SRAM (relaxing the 16KB limit at the cost of ~15mA search power) is justified not as a scaling experiment but as a structural necessity if the pattern count grows

The pruning criterion from ROADMAP Pillar 1 — "prune nodes when the CfC can represent the state without them" — needs to be revised. With fixed CfC weights, the CfC may never be able to represent some states independently. A better pruning criterion: prune nodes whose LP-hidden portion is within Hamming 1 of the current LP mean for their pattern (the memory is redundant with what the LP state already knows), and retain nodes whose LP-hidden portion is an outlier (the memory encodes a rare state that the accumulator hasn't absorbed).

---

## 7. Open Questions

### 7.1 Will Gate Bias Amplify or Confuse?

The gate bias mechanism assumes that a P1 LP prior correctly identifies which neuron groups should fire more easily during P1. This requires that the mapping from LP-space to GIE-group is stable — that the P1 LP prior reliably predicts "P1 neurons should fire." This mapping is mediated by the LP-space signatures `lp_sig[p]`, derived from TEST 12. If the LP state distribution shifts significantly across runs (which it does — compare Run 1 vs Run 3 LP vectors), the stored signatures may not generalize.

The scalar bias version (Section 4.1) avoids this problem entirely by not requiring a pattern-to-group mapping. Its downside is less specificity — it amplifies all patterns indiscriminately. The per-group version (Section 4.2) is more expressive but requires stable LP-space signatures.

### 7.2 What Is the Right Bias Scale?

The gate_threshold is currently 90 (out of a maximum f_dot of ~32 for 64 neurons × 2 groups × ternary). A bias of ±30 moves the threshold by 33% — substantial. A bias of ±5 is conservative (~6%). The right scale is unknown and will need to be tuned empirically.

The risk of too-large bias:
- Too-low threshold: all neurons fire → GIE hidden saturates → LP state becomes all-ones → VDB retrieval degrades → feedback loop breaks down
- Too-high threshold: no neurons fire → GIE hidden goes to zero → system stops responding

The risk of too-small bias:
- Effect is below noise floor → TEST 14 shows no difference from TEST 12

Suggested initial range: ±10 (11% of threshold), scanning to ±20 if no effect is observed.

### 7.3 How Fast Should LP Prior Decay During Pattern Switches?

The LP hidden state accumulates over the entire run via the sign-of-sum computation. After 90 seconds with 150 P1 confirmations, the LP state is strongly P1-committed. If Board B switches to P2, the LP state doesn't immediately flip — it takes N new P2 classifications to shift the majority vote on each trit.

For kinetic attention, this inertia is both a feature and a risk:
- Feature: the system doesn't immediately abandon its prior on the first outlier (robust to noise)
- Risk: the system may lag significantly behind Board B's pattern switch, firing at reduced threshold for P1 neurons even during a P2 session (confused attention)

One mitigation: introduce a decay mechanism where `lp_sum[j]` is multiplied by a decay factor (e.g., 0.99) on each step. This limits the effective lookback window without requiring explicit timestamp tracking. In ternary: decay by dropping the sign of sum[j] when |sum[j]| drops below a threshold, reverting to zero. This is computationally compatible with the existing accumulator structure.

### 7.4 Does This Interact with the P3 Novelty-Gate Problem?

P3 has low and variable novelty-gate pass rate due to its incrementing payload (see TEST 12 methodology). If gate bias is active and P3 receives a prior from a previous session where P3 was well-observed, the lower gate threshold for P3 neurons might actually increase P3's novelty-gate pass rate — the GIE hidden state would show stronger P3-pattern firing, pushing the TriX score for P3 above the novelty threshold more reliably.

This would be an interesting secondary effect: LP prior improves the robustness of the novelty gate for rare patterns. This could be tested directly by comparing P3 sample counts in TEST 14 vs. TEST 12.

---

## 8. Summary of Phase 5 Deliverables

| Deliverable | Description | Prerequisite |
|-------------|-------------|--------------|
| `lp_gate_bias` SRAM variable | LP SRAM scalar or array [4], written by HP core | None |
| LP-space signature computation | HP core projects lp_hidden onto 4 pattern axes | TEST 12 LP means (available) |
| ISR bias read | `effective_threshold = gate_threshold + bias[group]` | `lp_gate_bias` in SRAM |
| TEST 14A | Scalar bias, 90s run, LP Hamming matrix | ISR change |
| TEST 14B | Per-group bias, 90s run, LP Hamming + pattern-switch response | LP signatures |
| TEST 14C | Bias=0 (baseline replicate) | None |
| Analysis | LP Hamming comparison 14A/14B vs 12; GIE energy by pattern; stability | TEST 14 data |

---

## 9. The Paper Outline (Phase 5)

Working title: **"Kinetic Attention in a Ternary Reflex Arc: Sub-conscious Priors Shape Peripheral Computation"**

1. **Introduction** — the potential/kinetic modulation distinction; CLS parallel
2. **Background** — Reflex architecture, Phase 4 results (brief), the degeneracy finding
3. **Complementary Learning Systems in Ternary Hardware** — detailed theoretical treatment of VDB-as-hippocampus, CfC-as-neocortex, and the permanent (non-consolidating) VDB
4. **Phase 5 Architecture** — gate bias mechanism, per-group projection, ISR modification
5. **TEST 14: Experimental Design** — conditions, measurement approach, pass criteria
6. **Results** — LP Hamming comparison, GIE hidden state evolution, stability analysis
7. **Analysis** — does the bias amplify or confuse? Pattern-switch response. P3 novelty gate interaction.
8. **Limitations** — LP signature stability across runs, bias scale tuning, no weight update
9. **Conclusion** — the complete attention loop: perceive → classify → remember → bias → perceive differently

---

*The claim: a $0.50 microcontroller, drawing under 100 µA, whose prior experience changes what its perceptual hardware computes next. All ternary. No floating point. No training. No CPU between classification events.*

---

**Date**: March 22, 2026
**Depends on**: commit `12aa970`, TEST 12/13 results
**Next firmware target**: `geometry_cfc_freerun.c` — add `lp_gate_bias[4]` to LP SRAM layout, modify ISR blend step
