# Lincoln Manifold: Ternary Remediation Plan — NODES

*Phase 2. Grain identification. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## Node 1: Zero Means "Absent," Not "Between"

In ternary arithmetic, 0 is a third state, not a midpoint. It means "this position carries no information." In the dot product, `tmul(0, x) = 0` — a zero trit is silent. In the CfC blend, `f = 0` → HOLD — a zero gate preserves the current state. In the VDB, a zero trit doesn't influence the match score.

Binary encoding (+1/-1, never 0) forces every input position to have an opinion. This is correct for signals that are genuinely binary (a bit is 0 or 1). It is incorrect for signals that have uncertainty (RSSI near a threshold, a counter that has no pattern relevance).

**Why it matters:** The system uses zeros structurally — as inertia, as silence, as "no opinion." An input that never produces zeros denies the downstream computation access to this state. The engine is ternary; the input should be ternary.

---

## Node 2: The Bias Path Has Two Floats, Both Removable

**Float 1: bias state and decay.** `float bias_f[4]` decayed by `*= 0.9f`. Integer equivalent: `int16_t bias_i[4]` at 10× resolution (range 0-150), decayed by `bias_i = bias_i * 9 / 10`. ISR reads `gie_gate_bias[p] = (int8_t)(bias_i[p] / 10)`. Behavior is identical within ±1 of the int8 output. The integer truncation produces slightly faster decay in the tail (values 1-9 map to ISR bias 0 instead of 0.1-0.9 in float), which is correct — sub-unit bias should be zero.

**Float 2: bias magnitude.** `BASE_GATE_BIAS * (float)margin / LP_HIDDEN_DIM`. Integer: `(BASE_GATE_BIAS * margin + LP_HIDDEN_DIM/2) / LP_HIDDEN_DIM`. Max intermediate: 15 × 16 = 240. Fits in int16. Result 0-15, fits in int8.

**Why it matters:** These are the last floats in the mechanism path. Removing them completes the "no floating point" claim for the entire signal chain: input encoding → GIE dot product → ISR blend → LP CfC → VDB → agreement → bias → ISR threshold. All integer/ternary.

---

## Node 3: RSSI Thermometer Needs a Dead Zone

Current: 16 thresholds, each `(rssi >= threshold) ? +1 : -1`. A signal at -61 dBm and threshold -60 dBm produces -1 (definitely below). But RSSI measurement noise is ±2-3 dBm. The -61 reading could be a -58 signal. The trit should be 0 (uncertain).

**Dead zone approach:** For each threshold, define a noise margin (±2 dBm). If `rssi >= threshold + margin`: +1 (confidently above). If `rssi <= threshold - margin`: -1 (confidently below). Otherwise: 0 (uncertain).

Effect: RSSI trits near the signal's actual level become 0. Trits well above and well below remain committed. The RSSI encoding becomes a ternary thermometer with an uncertain region — exactly the information that the downstream ternary engine can use.

**Impact on classification:** The signatures are computed from `sign(mean(observations))`. With a dead zone, the mean will include zeros. `sign(mean)` may produce 0 for some RSSI trits. This reduces the signature energy (fewer non-zero trits), which reduces the classification dot product magnitude. The novelty threshold may need adjustment. But the signatures that survive are more reliable — they represent confident observations, not noise.

**Tension with Node 5:** The dead zone width is a parameter. Too narrow (±1) and it barely adds zeros. Too wide (±5) and most RSSI trits become 0, losing discrimination. ±2 is a principled starting point based on typical RSSI noise, but should be validated on silicon.

---

## Node 4: Payload Bits Are Genuinely Binary — Leave Them

Payload bytes contain the sender's pattern-specific content. A bit IS +1 or -1. There's no uncertainty at the bit level. The binary encoding is correct.

Pattern-irrelevant bits (counter LSBs, etc.) produce noise, but this is handled by signature enrollment: the signature for irrelevant trits converges to 0 via `sign(mean)` when the bit flips roughly equally often for all patterns. The signature silences them.

**Decision:** Payload trits stay binary (+1/-1). No remediation needed.

---

## Node 5: Sequence Trits Should Be Zeroed or Replaced

Trits [104..127] encode a monotonic sequence counter. They're already zeroed in the signatures (commit `5735119`). They contribute nothing to classification. But they're still +1/-1 in the input, contributing to GIE dot products during the free-running loop.

**Option A: Zero them permanently.** Set `new_input[104..127] = 0` in `espnow_encode_input()`. Saves 24 trit computations per GIE neuron per loop (24 × 64 neurons × 430 Hz = 661K fewer AND+popcount ops/second). Trivial.

**Option B: Replace with MTFP timing features.** The MTFP21 gap history already occupies trits [88..103] (16 trits). Trits [104..119] could hold additional timing features: gap variance MTFP, burst-count MTFP, steady-count MTFP. This adds timing discrimination without the monotonic counter noise.

**Decision for now:** Option A (zero permanently). Option B is a feature engineering change that should be validated separately. Zeroing is safe and immediate.

---

## Node 6: MTFP Encoding Is the Full-Ternary Approach for Continuous Signals

The MTFP encoding (sign + exponent + mantissa) naturally produces zeros for small magnitudes and committed trits for large magnitudes. It preserves the magnitude structure that binary thermometers collapse.

For RSSI, an MTFP encoding relative to a reference level (-50 dBm, typical for close-range ESP-NOW) would be:
- Sign: above/below reference (+1/-1)
- Exponent (2 trits): magnitude of deviation (0-3 dBm → both 0; 4-8 → one committed; 9-15 → both committed; etc.)
- Mantissa (1-2 trits): position within scale

This produces 4-5 trits per RSSI reading instead of 16. The remaining trits could encode RSSI variance, trend, or be left as zeros.

**Why this is Tier 2:** MTFP RSSI changes the input dimensionality layout. Trits [0..15] become trits [0..4] (one MTFP value) or trits [0..9] (two: level + variance). This requires re-enrollment, changes the GIE hidden state interpretation, and potentially affects the LP CfC dynamics. It's a bigger change than a dead zone.

**Tension with Node 3:** The dead zone (#3) is a minimal fix that adds zeros to the existing thermometer. MTFP (#6) is a full rethink. Both are valid. The dead zone should come first (low risk, immediate benefit). MTFP can follow if the dead zone is insufficient.

---

## Node 7: The Remediation Has Clear Risk Tiers

| Item | Risk | Impact | Change |
|------|------|--------|--------|
| #1 Integer bias state | None | No float in mechanism | Arithmetic only |
| #2 Integer bias magnitude | None | No float in mechanism | Arithmetic only |
| #5 Zero sequence trits | None | Reduces noise | 2 lines |
| #3 RSSI dead zone | Low | Adds ternary uncertainty | Parameter: margin width |
| #3b RSSI MTFP | Medium | Full ternary RSSI | Input layout change |
| #4 Payload ternary | Low | Not needed | Enrollment handles it |
| #6-8 Display floats | None | Not in signal path | Leave as-is |

Items 1, 2, 5 can be done immediately with zero risk. Item 3 is low risk but needs a novelty threshold check. Item 3b is a separate experiment.

---

## Node 8: The "No Floating Point" Claim Becomes True

After items #1 and #2, the mechanism path is:

```
ESP-NOW packet (bytes)
  → espnow_encode_input (integer thresholds → ternary trits)
  → GIE (GDMA→PARLIO→PCNT → ISR: AND+popcount+branch)
  → TriX (ISR: per-group sum → argmax → int32 pred)
  → LP CfC (assembly: AND+popcount+add+sub+branch)
  → VDB (assembly: same primitives)
  → Agreement (HP: ternary tmul → int disagree count)
  → Bias (HP: integer multiply+divide → int8 gate_bias)
  → ISR threshold (int subtract + int compare)
```

No float anywhere. The only floats remaining are in printf formatting (display) and elapsed-time measurement (diagnostic). The paper can state: "No floating-point operation anywhere in the perception-classification-memory-bias signal path."
