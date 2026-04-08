# Lincoln Manifold: Ternary Remediation Plan — REFLECT

*Phase 3. Axe sharpening. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## Core Insight

**The remediation is not about removing floats. It is about completing the ternary invariant.**

The system's ternary constraint creates a specific information structure: every signal position can say "yes" (+1), "no" (-1), or "I don't know" (0). The engine (dot product, blend, VDB) respects this structure. The agreement mechanism now respects it (disagree-count). The input encoding does not respect it — 81% of input trits are forced to have an opinion (+1/-1) even when the underlying signal is uncertain.

Completing the ternary invariant means: every stage of the signal path either produces genuine ternary output (with 0 meaning uncertain/absent) or handles genuinely binary data (where 0 would be incorrect). The remediation separates these two cases and treats each correctly.

---

## Resolved Tensions

### Dead Zone vs MTFP for RSSI (Nodes 3, 6)

**Tension:** Dead zone is minimal, adds zeros to the existing 16-trit thermometer. MTFP is a full rethink, changes the input layout.

**Resolution:** They're not alternatives. They're stages.

Stage 1 (dead zone): Add uncertainty to the thermometer. Trits near the RSSI level become 0. The encoding is still a thermometer — same 16 positions, same thresholds — but now it's ternary: confident above, uncertain near, confident below. This changes no data structures, no trit positions, no downstream interpretation. It's a one-line change in the encoder.

Stage 2 (MTFP RSSI, future): Replace the thermometer with an MTFP value. This changes the input layout and requires re-enrollment. It's a bigger experiment. It should follow Stage 1 and be motivated by data showing the dead-zone thermometer is insufficient.

The dead zone is the remediation. MTFP is a future enhancement.

### Whether Payload Should Be Ternary (Node 4)

**Tension:** The principle says "every departure from ternary loses structure." But payload bits are genuinely binary.

**Resolution:** The principle applies to signals with inherent uncertainty. A bit has no uncertainty — it's 0 or 1. Encoding it as +1/-1 is the correct ternary representation of a binary signal. The zero state would mean "this bit is uncertain," which is never true for a deterministic payload.

The signature enrollment handles pattern-irrelevant bits correctly: their signature trit converges to 0 (because they contribute equally to all patterns), silencing them in the classification. The enrollment is the ternary remediation for payload bits — it moves the irrelevant-bit suppression from the input encoder to the classifier. This is architecturally correct: the input should encode what it observes (the bit value), the classifier should decide what matters (the signature).

**Decision confirmed:** Payload stays binary. The enrollment mechanism is the ternary remediation.

### Whether to Add Cross-Layer Disagreement for Seed B (RAW concern)

**Tension:** Seed B's 22-step headwind could be addressed by a cross-layer signal: "TriX says P2 for N consecutive steps but LP hasn't moved → release bias." This would mix two different information types (ISR classification, LP agreement) into a single mechanism.

**Resolution:** Defer. The ternary disagree-count is the structurally correct mechanism for detecting LP-level conflict. The cross-layer signal would be a timeout, not a disagreement detector — it would say "enough time has passed" rather than "the data disagrees." Timeouts are useful but they're not ternary. They're a different kind of mechanism (temporal rather than spatial).

Seed B's fix is Pillar 3 (Hebbian learning), which fixes the projection so the LP space can distinguish P1 from P2. The mechanism is not the bottleneck. The representation is.

---

## The Remediation Plan

### Phase A: Zero-Risk Mechanism Path (no behavioral change expected)

**A1. Integer bias state.**
Replace `float bias_f[4]` with `int16_t bias_i[4]` at 10× resolution.
- Init: 0
- Set: `bias_i[pred] = (BASE_GATE_BIAS * 10 * margin + LP_HIDDEN_DIM/2) / LP_HIDDEN_DIM` (max: 150)
- Decay: `bias_i[p] = bias_i[p] * 9 / 10`
- Release: `bias_i[pred] = 0` (on disagree >= 4)
- ISR: `gie_gate_bias[p] = (int8_t)(bias_i[p] / 10)`
- Same in TEST 14 and TEST 14C.

**A2. Integer bias magnitude.**
Already handled by A1 — the magnitude computation is folded into the set operation.

**A3. Zero sequence trits.**
In `espnow_encode_input()`, after the payload encoding, replace the sequence feature block (trits [104..119]) with `memset(&new_input[104], 0, 16)`. The reserved block [120..127] is already zero. Net: trits [104..127] are all zero.

### Phase B: Low-Risk Input Encoding (may change classification behavior)

**B1. RSSI dead zone.**
In `espnow_encode_input()`, replace:
```c
new_input[i] = (st->rssi >= threshold) ? T_POS : T_NEG;
```
with:
```c
int diff = st->rssi - threshold;
new_input[i] = (diff > 2) ? T_POS : (diff < -2) ? T_NEG : T_ZERO;
```
Dead zone: ±2 dBm. Trits within 2 dBm of threshold produce 0.

**B1 validation:** After RSSI dead zone, re-run Test 11 enrollment + Test 14C with Seed A. Compare:
- Signature energy (non-zero trit count per signature)
- TriX accuracy (should remain 100% for enrolled patterns)
- LP divergence (should be equal or better — cleaner signal)
If TriX accuracy drops, the dead zone may be too wide or the novelty threshold needs lowering.

### Phase C: Future Enhancement (separate experiment)

**C1. MTFP RSSI encoding.** Replace the 16-trit thermometer with a 5-trit MTFP value (sign + 2 exp + 2 mant) relative to a reference level. Frees 11 trits for other features or zeros. Requires full re-enrollment and validation. Separate LMM cycle.

**C2. MTFP gap history expansion.** Use the freed sequence trits [104..119] for MTFP timing features (gap variance, burst count, steady count). Separate experiment.

---

## What Changes in Each Phase

| Phase | Files | Lines | Risk | Validation |
|-------|-------|-------|------|------------|
| A1-A2 | geometry_cfc_freerun.c (TEST 14, TEST 14C) | ~20 | None | Verify ISR bias values match within ±1 |
| A3 | gie_engine.c (espnow_encode_input) | 2 | None | Signatures already zero these positions |
| B1 | gie_engine.c (espnow_encode_input) | 3 | Low | Re-enrollment + TriX accuracy check |
| C1-C2 | gie_engine.c, geometry_cfc_freerun.c | ~30 | Medium | Full test suite re-validation |
