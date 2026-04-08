# Lincoln Manifold: Ternary Remediation Plan — RAW

*Phase 1. Unfiltered. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## The Problem Statement

81% of the input vector is binary. The bias state is float. The bias magnitude is float. Three input encoders produce {+1, -1} but never 0. The system claims to be ternary — and it IS ternary in the engine (dot product, blend, VDB, CfC) — but the data it processes has a binary input stage and a float mechanism stage. The ternary engine is sandwiched between binary inputs and float control.

We just proved that leaving the ternary domain loses information that matters. The float agreement lost trit structure → transition headwind. sign() lost magnitude → P1-P2 degeneracy. These aren't edge cases. They're the main failure modes of the system.

So what happens when 81% of the input is binary? What are we losing?

---

## What Zero Means

In the ternary CfC, zero has a specific meaning in three places:

**In the hidden state:** h[n] = 0 means "this neuron has no opinion." The HOLD blend rule preserves zeros — if f = 0, h_new = h_old. Zeros are inertia. They resist change. A hidden state that's all +1/-1 is maximally committed. A hidden state with zeros is partially committed — it has room to be influenced.

**In the dot product:** A zero trit contributes nothing to the dot. `tmul(0, anything) = 0`. Zeros in the input mean "this dimension doesn't participate in the similarity computation." Zeros in the weight mean "this neuron doesn't care about this input dimension."

**In the VDB:** A zero trit in a stored vector means "the system had no opinion about this dimension at storage time." When the VDB retrieves a nearest neighbor, zeros in the stored vector don't influence the match score. The match is driven by the non-zero positions.

So zero is not "between +1 and -1." Zero is "absent." It's a third state that means "this signal carries no information at this position."

---

## What's Wrong with Binary Input

When the RSSI trit is `(rssi >= threshold) ? +1 : -1`, the system is saying: "at this threshold, the signal is either above or below. There is no uncertainty." But RSSI near the threshold IS uncertain. The measurement noise at -60 dBm is ±3 dBm. A trit at threshold -60 that sees -61 should be 0 (uncertain), not -1 (definitely below).

The payload bits are genuinely binary — a bit is 0 or 1, no ambiguity. But when encoded as +1/-1, every payload trit participates equally in the dot product. If a payload bit is pattern-irrelevant (say, a counter LSB that flips every packet), its trit still contributes ±1 to the dot. It adds noise. A zero trit would silence it.

The sequence features are worse — they encode a monotonic counter that has nothing to do with which pattern is active. They're already masked in the signatures (trits [104..127] zeroed). But they still contribute to the GIE dot products during the free-running loop, before signatures are installed.

---

## The MTFP Approach

MTFP (Multi-Trit Floating Point) already exists in the system for LP dot encoding: 5 trits per value (sign + 2 exponent + 2 mantissa). It encodes magnitude structure that sign() collapses.

The same principle applies to input encoding. Instead of a binary thermometer (16 trits, each +1/-1), RSSI could be encoded as an MTFP value: sign (above/below reference), exponent (order of magnitude of deviation), mantissa (position within scale). Near the reference, the exponent trits are 0 (small deviation — uncertain). Far from the reference, the exponent trits are +1 or -1 (large deviation — committed).

This naturally produces zeros where the signal is uncertain and committed trits where the signal is strong. The ternary domain gets what it needs: information about magnitude and certainty, not just polarity.

---

## The Eight Items

Let me think through each one:

**#1 — bias_f[4] is float with 0.9 decay.** The float state tracks fractional decay. Integer equivalent: start at bias_i = 150 (10× BASE_GATE_BIAS for resolution), decay as `bias_i = bias_i * 9 / 10`. At each step: 150→135→121→109→98→88→79→71→64→57→52→46→42→37→34→30→27→24→22→20→18→16→14→13→12→10... This is slightly different from float due to integer truncation, but the behavioral difference is negligible. The ISR reads `gie_gate_bias[p] = (int8_t)(bias_i / 10)`. Same result. No float.

But wait — is 0.9 decay even the right mechanism now? With ternary disagree-count, the bias zeros immediately on conflict (disagree >= 4). The 0.9 decay handles the gradual case: a pattern is still present but the prior is slowly drifting. Does this case exist? When would agreement gradually decrease instead of suddenly crossing the disagree threshold?

Answer: during slow LP drift. The LP state changes by at most 1 trit per CfC step (HOLD-on-conflict). If the LP state is drifting away from the accumulator mean, the disagree count increases by 0-1 per step. With 16 trits, it takes at least 4 steps to go from 0 disagree to 4 disagree. During those 4 steps, the 0.9 decay brings the bias from 15 to 15 × 0.9⁴ ≈ 10. So the decay provides a gentle ramp-down before the hard cutoff. That's reasonable. Keep the decay, make it integer.

**#2 — float bias magnitude.** `float b = BASE_GATE_BIAS * (float)margin / LP_HIDDEN_DIM`. Integer: `int b = (BASE_GATE_BIAS * margin + LP_HIDDEN_DIM/2) / LP_HIDDEN_DIM`. With rounding. Produces values 0-15 for margins 0-16. No precision loss. Trivial.

**#3 — RSSI thermometer is binary.** 16 thresholds, each producing +1/-1. Option A: add a dead zone around each threshold. If |rssi - threshold| < noise_margin (say, 2 dBm), output 0. Otherwise +1/-1. This makes the encoding properly ternary: committed above, committed below, uncertain near threshold.

Option B: MTFP encoding. Encode the RSSI deviation from a reference as a multi-trit value. Sign = above/below reference. Exponent = magnitude of deviation. Near the reference = small exponent = zeros. Far = large exponent = committed trits. This is richer but changes the input dimensionality.

Option A is simpler and doesn't change the trit count. Option B is more principled but requires rethinking the input layout.

**#4 — Payload bits are binary.** Bits are genuinely binary. But not all bits are equally informative. A pattern-irrelevant bit (counter LSB, checksum bit) contributes noise. Two approaches:

Option A: Map to ternary with a confidence signal. If the bit has been stable (same value for last N packets), encode as +1/-1. If it's been flipping, encode as 0. This requires per-bit state tracking — 64 bits × N-packet history = memory cost.

Option B: Use the enrollment phase. After enrollment, compute per-trit discriminability: trits that differ across patterns get +1/-1; trits that are the same across all patterns get 0. This is a static mask applied once, not a per-packet computation. Simpler. But it uses enrollment data to make the decision, which is a form of learned encoding.

Option C: Leave payload binary. The payload bits ARE the pattern content — the sender puts different data in the payload for each pattern. The +1/-1 encoding is actually correct for information that is genuinely binary and pattern-specific. The issue is only with pattern-irrelevant bits, which are already handled by signature masking (the signature for irrelevant bits converges to 0 during enrollment, so they contribute nothing to classification).

I'm leaning toward Option C: payload is genuinely binary, and the signature enrollment already handles the noise trits. The binary encoding is correct here.

**#5 — Sequence features are binary.** These encode a monotonic counter. They're already masked in signatures (trits [104..127] zeroed). They contribute nothing to classification. But they still occupy 24 trits of the 128-trit input vector. Options:

Option A: Zero them permanently. 24 trits of zeros in the input. Reduces the effective input from 128 to 104 trits. The GIE dot products become slightly cheaper (24 fewer non-zero trits to process). The VDB vectors become sparser. No information lost — they were already masked.

Option B: Replace with something useful. 24 trits could encode packet-level metadata: inter-packet timing variance, RSSI trend, gap pattern. This is a feature engineering decision, not a ternary remediation.

Option A is the clean remediation. The trits are noise; make them silent.

**#6 — Elapsed time float.** Display only. Not in the signal path. Leave it. Floats are fine for printf formatting.

**#7 — Null flip rate float.** Diagnostic only. Same as #6.

**#8 — Mean Hamming display float.** Reporting only. Same as #6.

---

## The Remediation Has Two Tiers

**Tier 1 — Mechanism path (signal-affecting, should be fixed):**
1. Integer bias state (bias_f → bias_i, × 10 scaling, integer decay)
2. Integer bias magnitude (remove float division)
3. RSSI dead zone (add 0 near threshold)
5. Sequence trits zeroed permanently

**Tier 2 — Input encoding (richer ternary, higher impact, higher risk):**
3b. RSSI MTFP encoding (full rethink of RSSI representation)
4. Payload ternary confidence (per-bit stability tracking)

Tier 1 is safe — it removes floats and adds zeros where signals are uncertain. Tier 2 changes the input representation, which means re-enrollment and potentially different classification behavior. Tier 2 needs its own LMM cycle.

**#4 (payload) probably stays binary.** The payload bits are genuinely binary pattern content. The signature enrollment handles noise trits. The binary encoding is correct for genuinely binary data.

**#6, #7, #8 (display floats) stay float.** They're printf formatting. They don't affect the signal path.
