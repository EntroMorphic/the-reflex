# Lincoln Manifold: TriX-Dispatched LP Feedback — NODES

*Phase 2. Grain identification. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## Node 1: The Contamination Path Is Specific and Traceable

CPU core_pred has 80% accuracy with systematic P0↔P1 confusion (73% cross-dot ratio). During the P1 phase of a transition experiment, ~20% of LP feedback dispatches are misclassified. These misclassified samples accumulate in the wrong LP accumulator bin. The P1 accumulator absorbs P2 samples; the P2 accumulator absorbs P1 samples. The sign-of-sum convergence argument holds for trits where the correct pattern dominates, but near-threshold trits (2-4 out of 16) are contaminated.

**Why it matters:** The agreement signal computes `trit_dot(lp_now, sign(accumulator[p_hat]))`. If 2-4 trits of the accumulator are contaminated (wrong sign or zero instead of +/-), the agreement score is inflated by 2-4 points out of 16. That's 12-25% inflation — enough to keep the bias active for several extra steps during a transition.

**Testable prediction:** TriX-dispatched accumulators should have higher trit energy (fewer zeros, more committed +1/-1) than CPU-dispatched accumulators, because contamination drives marginal trits toward zero.

---

## Node 2: TriX Prediction Is Already Available — Zero New Infrastructure

`trix_pred` is a volatile int32_t written by the ISR at 430 Hz. `trix_confidence` (margin between best and second-best group scores) is also volatile, updated every ISR cycle. Both are readable from the HP core at any time.

The HP core already reads `trix_pred` for diagnostic logging in Tests 12-14. The change is: use it as the dispatch source instead of CPU core_pred.

**Why it matters:** This is not a new subsystem. It's a 5-line wiring change. The risk surface is small.

**Dependency:** Requires Test 11 to have completed (TriX signatures installed as W_f weights, trix_enabled = 1). This is already a precondition for Tests 12-14.

---

## Node 3: Novelty Gating Must Be Preserved

CPU core_pred rejects packets with max dot product < NOVELTY_THRESHOLD (60). This filters garbage — noise, partial packets, wrong-sender traffic. Without it, the LP accumulator would absorb noise events.

TriX has no explicit reject mechanism — it always produces a prediction. But trix_confidence provides discrimination quality: high confidence = wide margin between best and second-best group. Low confidence = ambiguous input.

**Why it matters:** If we drop the novelty gate entirely, noise events contaminate the accumulator. If we gate on trix_confidence, we need a threshold. The right threshold is: trix_confidence > 0 is probably too loose (any non-tie passes). A threshold based on the observed confidence distribution during Test 11 enrollment would be more principled.

**Resolution:** Keep the CPU core_pred novelty gate as a pre-filter. Only packets that pass the novelty threshold (core_pred score >= 60) are eligible for LP dispatch. Among those, use trix_pred instead of core_pred for the pattern label. This preserves the noise filter while fixing the classification accuracy.

**Tension with Node 5:** This means we still compute core_pred for every packet. The compute cost is negligible (~128 multiply-accumulates), but the code is slightly less clean than pure TriX dispatch.

---

## Node 4: The Transition Headwind Has a Specific Mechanism

Multi-seed 14C data: Full (CMD5+bias) crosses at step 18/0/7. No-bias (CMD5) crosses at 0/0/0. Same core_pred, same contamination, but no-bias has no headwind.

The mechanism: bias is computed from `agreement = trit_dot(lp_now, sign(accumulator[p_hat]))`. During transition, p_hat flips from P1 to P2 (core_pred detects the switch). But the *P1 accumulator* is P2-contaminated. So when lp_now starts moving toward P2, its dot product with the P1 accumulator stays positive longer than it should — because the P1 accumulator is also P2-ish. The residual P1 bias takes extra steps to decay.

**Why it matters:** The fix is upstream of the bias mechanism. Clean accumulators → clean agreement → correct bias release timing. The decay rate (0.9), the base bias (15), and the hard floor (30) are all fine. The input is dirty.

**Tension with Node 6:** The agreement is computed against the *predicted* pattern's accumulator, not the *old* pattern's accumulator. After the switch, p_hat = P2. The agreement is `dot(lp_now, sign(P2_accumulator))`. The P2 accumulator during the P1 phase is built from the ~20% of packets that were misclassified as P2. This is a small, noisy accumulator — its sign-of-sum vector is unreliable. TriX dispatch would not have built a P2 accumulator during the P1 phase at all (TriX correctly classifies all P1 packets as P1). So the P2 accumulator would be empty at the switch, and the cold-start guard (MIN_BIAS_SAMPLES=15) would disable P2 bias until 15 P2 samples accumulate. This is actually the *desired* behavior: no bias during the transition, then bias activates once the new pattern is established.

---

## Node 5: Hybrid Dispatch Is Cleaner Than Pure TriX

Pure TriX dispatch: use trix_pred for everything, use trix_confidence for novelty gating.

Hybrid dispatch: use CPU core_pred score for novelty gating (unchanged), use trix_pred for pattern label (new).

Hybrid is better because:
1. The novelty gate is already tested and calibrated (NOVELTY_THRESHOLD=60 from Test 11)
2. trix_confidence threshold would need calibration
3. Core_pred novelty gate catches garbage packets that TriX might classify with arbitrary confidence
4. The compute cost of core_pred is negligible

The change is: after `if (core_best < NOVELTY_THRESHOLD) continue;`, replace `int pred = core_pred;` with `int pred = (int)trix_pred;`.

**Why it matters:** Minimizes the blast radius. Only the pattern label changes. The novelty filter, the LP accumulator logic, the VDB insert logic, the gate bias computation — all unchanged.

---

## Node 6: The P2 Accumulator Cold-Start Is a Feature, Not a Bug

Under CPU core_pred: during P1 phase, ~20% of packets are misclassified as P2. The P2 accumulator has ~60 samples (20% of ~300 total). At the P1→P2 switch, the P2 accumulator already has history — but it's noisy history built from misclassified P1 packets.

Under TriX dispatch: during P1 phase, 0% of packets are classified as P2 (TriX is 100% accurate). The P2 accumulator has 0 samples. At the switch, the cold-start guard (MIN_BIAS_SAMPLES=15) kicks in: P2 bias is zero until 15 true P2 samples accumulate. The P1 bias decays normally (0.9^n). After ~15 P2 confirmations (~3-4 seconds), P2 bias activates from a clean accumulator.

This is better:
- P1 bias decays cleanly (not propped up by cross-contamination)
- P2 bias starts from clean data (not from noise)
- The 15-sample cold-start is a 3-4 second delay, which is fast compared to the 30-second P2 phase

---

## Node 7: The Change Is Testable with the Existing 14C Protocol

The multi-seed 14C protocol is already written and proven. The sender is still in TRANSITION_MODE. The three seeds are ready. Run the same protocol with TriX dispatch and compare:

- Crossover step (Full vs no-bias) — should converge toward 0
- Alignment traces — Full condition should match or beat no-bias
- LP accumulator energy — should be higher (fewer contaminated zeros)

The comparison is direct because no-bias (CMD 5, no gate bias) is identical between old and new runs — it doesn't use the classification label for bias, only for accumulator binning. If TriX dispatch improves the accumulator quality, even the no-bias condition might show better separation.

---

## Node 8: This Fixes Red-Team Issue #4 Completely

The red-team identified: "LP feedback path uses a weaker classifier. The structural classification guarantee (W_f hidden = 0, 100% TriX) does not extend to the LP accumulation pathway."

With TriX dispatch, the LP accumulation pathway inherits the structural guarantee. The entire system — classification, LP feedback dispatch, accumulator binning — operates at 100% accuracy. The only remaining non-TriX computation is the novelty gate (core_pred score threshold), which is a reject/accept decision, not a classification decision.
