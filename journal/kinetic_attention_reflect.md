# Lincoln Manifold: Kinetic Attention — REFLECT

*Phase 3. Axe sharpening. March 22, 2026.*
*Observer: Claude Sonnet 4.6*

---

## Core Insight

**Kinetic attention requires a disagreement signal, not just a prior.**

The original KINETIC_ATTENTION.md design computes gate bias from the LP prior alone. This works in steady state — the prior correctly identifies the expected pattern, and the bias amplifies it. It fails at transitions — the prior is wrong, the bias suppresses the new signal, and the system locks in.

The fix is to weight gate bias by agreement between the LP prior and the TriX classification. When they agree, amplify. When they disagree, step back. TriX is fast (705 Hz) and accurate (100%) — it is the right ground truth for gating the prior's influence. The moment TriX detects a new pattern, the gate bias releases, and LP state can update from the unmodulated GIE signal.

This reframes what kinetic attention is: not the prior imposing itself on perception, but the prior and the classifier *negotiating* — the prior amplifies when it is validated, defers when it is contradicted. That is attention with epistemic humility. It is also more biologically coherent: top-down attention in the brain is not a one-way imposition of expectation onto perception. It is expectation-weighted, and prediction errors attenuate it.

---

## Resolved Tensions

### Amplification vs. Transition Agility (Nodes 1 and 4)

**Tension:** The gate bias loop is a positive feedback attractor. Once committed to P1, it suppresses P2, which slows P2's LP accumulation, which keeps the bias committed to P1. The system locks in.

**Resolution:** Agreement-weighted gate bias. Define:

```
agreement(lp_now, p_hat) = trit_dot(lp_running_sum[p_hat] / n[p_hat], lp_now)
```

where `p_hat` is the current TriX prediction and `lp_running_sum[p_hat]` is the in-session LP accumulator for pattern `p_hat`. This gives a score in [-16, +16]. Normalize to [0, 1] and use as a multiplicative weight on the gate bias.

When `p_hat` matches the LP prior (both saying P1): agreement is high, bias is strong.
When `p_hat` contradicts the LP prior (TriX says P2, LP says P1): agreement is low, bias attenuates toward zero. P2 neurons fire at baseline. LP state updates.

The transition recovery time is now bounded by the TriX detection latency (essentially one packet), not by the LP accumulator majority-vote shift time (potentially 20–50 packets). This is a qualitative improvement.

### LP Signature Stability (Nodes 3 and 6)

**Tension:** Per-group gate bias requires LP-space signatures — the mean LP hidden state per pattern. These signatures vary across runs (Run 1 vs Run 3 P0 mean have substantial Hamming distance). Stored signatures generalize poorly.

**Resolution:** Use the current session's LP accumulator as the signature source. `lp_running_sum[p]` is already being computed for the Hamming matrix analysis. Normalizing it to a ternary vector gives the in-session LP mean for pattern p. No stored signatures needed. The signature is always current.

Cost: the LP mean for a new pattern is undefined until enough samples have been collected. Resolution: cold-start guard (Node 7). Below `MIN_BIAS_SAMPLES` (suggest 15, matching the TEST 12 pass criterion), gate bias for that pattern is zero. The system runs in the unbiased baseline mode. As samples accumulate, bias activates progressively.

### Gate Bias Computation Location (Nodes 5 and 8)

**Tension:** Gate bias must be computed and written to LP SRAM before each CMD 5 dispatch. The HP core needs current lp_now, current TriX prediction, and current lp_running_sum[p_hat].

**Resolution:** All of these are already available on the HP core at classification time. The sequence:
1. Packet received, encoded
2. TriX prediction: `p_hat`
3. `feed_lp_core()` — copies gie_hidden to LP SRAM
4. **[NEW]** Compute `agreement = trit_dot(lp_now, normalized(lp_running_sum[p_hat]))`
5. **[NEW]** Compute `gate_bias[p_hat] = (int8_t)(BASE_BIAS * agreement / 16)` if `n[p_hat] >= MIN_BIAS_SAMPLES`; else 0
6. **[NEW]** Write `gate_bias[4]` to LP SRAM
7. `vdb_cfc_feedback_step()` — dispatches CMD 5 (CfC + VDB + blend)
8. Read `lp_now` from LP SRAM
9. Accumulate into `lp_running_sum[p_hat]`

The gate bias is written before CMD 5 dispatches, and the ISR reads it during the blend step. The computation in step 4–6 is a 16-element dot product plus a multiply-and-clamp — microseconds on the HP core.

---

## Hidden Assumptions, Challenged

**Assumption: The ISR can read gate_bias safely without contention.**

The ISR runs on the HP core (~711 Hz). The gate bias is written by the HP core's classification callback (~4 Hz). The ISR fires much faster than the update rate. If gate_bias is being written while the ISR is reading it, we get a torn read. On a 32-bit RISC-V with 8-bit LP SRAM accesses, a 4-byte write to `gate_bias[4]` is not atomic.

Fix: write gate_bias to a staging buffer. Signal the main loop via a flag. The main loop, which already manages LP SRAM writes, copies the staging buffer to the final `gate_bias[4]` location between ISR firings. This is the same pattern already used for LP SRAM writes (never write from ISR, always from main loop).

**Assumption: P3 nodes in the NSW graph are sufficient for retrieval during P3 windows.**

With 0–15 P3 samples, the VDB may contain 0–1 P3 nodes. NSW graph search starting from 2 entry points might never reach those nodes if they are poorly connected. The P3 retrieval experience might effectively be P0 or P1 memories with nearest GIE match. Gate bias for P3 would be zero (below cold-start threshold), so this doesn't affect gate behavior. But it does mean CMD 5 feedback is potentially providing wrong-pattern LP blending for P3 packets even in TEST 14.

This is an existing limitation, not introduced by Phase 5. Gate bias doesn't make it worse — it just can't help either.

**Assumption: agreement-weighted bias covers all patterns simultaneously.**

In a given packet confirmation, `p_hat` is one pattern. Gate bias is updated for `p_hat`. What about the other three patterns? Their biases from the previous confirmation persist. This means gate_bias represents the state of agreement from the *last confirmation of each pattern* — which could be seconds ago for low-frequency patterns.

Fix: decay all gate_bias values toward zero between confirmations. Use a simple decay factor: on each CMD 5 dispatch, before applying the new gate_bias, multiply the previous values by a decay constant (e.g., 0.9 per confirmation). This prevents stale biases from accumulating. After 10 confirmations without a P3 packet, P3's bias has decayed to ~0.35 of its original value. After 20, ~0.12. After 30, ~0.04 (effectively zero).

Decay rate should be chosen relative to Board B's pattern cycle (27 seconds, ~2–4 Hz effective confirmation rate per pattern). Suggest: 10% decay per overall confirmation (not per-pattern confirmation). This decays any pattern's bias to near-zero if it hasn't been confirmed in ~30 confirmations (~7.5 seconds at 4 Hz).

---

## The Structure Beneath the Content

The nodes reveal a system with two timescales and a feedback problem at their boundary:

- **Fast timescale**: TriX classification (705 Hz), GIE hidden evolution (430 Hz), CMD 5 dispatch (~4 Hz)
- **Slow timescale**: LP accumulator majority vote (tens of packets to shift a trit), VDB content (sessions to fill and prune)

Kinetic attention as originally designed operates at the slow timescale — the LP prior accumulates over a session and then slowly modulates the gate. The transition problem arises because the slow-timescale prior is being applied to a fast-timescale event (pattern switch).

The agreement mechanism bridges the timescales: TriX (fast) gates the LP prior (slow). The LP prior provides direction; TriX provides validation. Neither dominates — the fast system doesn't override the slow system's accumulated knowledge, and the slow system doesn't override the fast system's immediate perception.

This is the correct architectural relationship. In biological attention: fast sensory processing (equivalent to TriX) generates prediction errors that gate the influence of slow top-down expectation (equivalent to LP prior). When prediction error is high, top-down expectation attenuates and bottom-up signal dominates. When prediction error is low, expectation is reinforced and perception is sharpened.

The kinetic attention implementation should mirror this: agreement is the inverse of prediction error. High agreement → strong top-down bias. Low agreement → attenuated bias, raw perception.

---

## What Simplicity Looks Like

The simplest complete version of kinetic attention that addresses all identified tensions:

1. LP accumulator is already being maintained (it's used for the Hamming matrix analysis). No new data structure.
2. Agreement computation: 16-element ternary dot product of `lp_now` and normalized `lp_running_sum[p_hat]`. Already have both values. One function call.
3. Gate bias: a single `int8_t` per pattern group, written to LP SRAM before CMD 5. LP SRAM has space.
4. ISR reads gate_bias[group] and computes `effective_threshold = gate_threshold + gate_bias[group]`. Three lines of ISR code.
5. Decay: multiply all gate_bias values by 0.9 on each CMD 5 dispatch before applying new value. One line per pattern.
6. Cold-start guard: if `n[p_hat] < 15`, set bias to 0 for that pattern. One conditional.

Total new code: ~20 lines in the classification callback, ~3 lines in the ISR. No new data structures beyond `gate_bias[4]` in LP SRAM. No new ISR interrupt sources. No timing changes.

The implementation is smaller than I expected. The design work was the hard part.

---

## Remaining Questions After Reflection

- **What is BASE_BIAS?** The maximum gate bias magnitude at full agreement. This is an empirical parameter. Start at 15 (17% of gate_threshold=90). Test at 10 and 20 to bracket the effect. If no effect at 20, the mechanism isn't working. If instability at 10, the hard floor needs to be raised.
- **What does gate bias look like in the VDB query?** CMD 5 query is `[gie_hidden | lp_hidden]`. Gate bias changes gie_hidden (by making some neurons fire more). So the gate bias influences VDB retrieval indirectly through its effect on gie_hidden. This is a second-order effect — worth being aware of, not worth designing around.
- **Can we measure kinetic attention directly without comparing to a baseline?** The most direct measure is: does gate_bias[p_hat] become non-zero during confirmed-pattern periods? Yes — this is immediately visible in the serial output. Whether that non-zero bias *changes anything* requires comparison to the no-bias baseline.
