# Lincoln Manifold: Ternary Remediation Plan — SYNTHESIS

*Phase 4. The clean cut. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## What Emerged

The system's ternary invariant — every signal position can say "yes," "no," or "I don't know" — is respected by the engine (dot product, blend, VDB, CfC, agreement) but violated by the input encoding (81% binary) and the bias computation (float state and magnitude). The remediation completes the invariant in three phases of increasing risk.

---

## Phase A: Integer Mechanism Path (Zero Risk)

### A1-A2. Integer Bias State and Magnitude

Replace `float bias_f[4]` with `int16_t bias_i[4]` at 10× resolution in both TEST 14 and TEST 14C.

```c
/* Declarations */
int16_t bias_i[TRIX_NUM_PATTERNS] = {0};
#define BIAS_SCALE 10  /* 10× resolution for integer decay */

/* Decay all groups */
for (int p = 0; p < TRIX_NUM_PATTERNS; p++)
    bias_i[p] = (int16_t)(bias_i[p] * 9 / 10);

/* Set bias for predicted pattern (ternary disagree-count) */
if (n_disagree >= 4) {
    bias_i[pred] = 0;
} else {
    int margin = n_agree - n_disagree;
    int b = (margin > 0)
        ? (BASE_GATE_BIAS * BIAS_SCALE * margin + LP_HIDDEN_DIM / 2) / LP_HIDDEN_DIM
        : 0;
    if (b > bias_i[pred]) bias_i[pred] = (int16_t)b;
}

/* Write to ISR */
for (int p = 0; p < TRIX_NUM_PATTERNS; p++)
    gie_gate_bias[p] = (int8_t)(bias_i[p] / BIAS_SCALE);
```

Max intermediate: `15 * 10 * 16 / 16 = 150`. Fits in int16. ISR sees values 0-15, same as before.

### A3. Zero Sequence Trits

In `espnow_encode_input()`, replace the sequence feature encoding (trits [104..119]) with zeros:

```c
/* [104..127] zeroed — sequence counter is not pattern-specific.
 * Previously encoded as binary thermometer + bit extraction.
 * Already masked in classification signatures (commit 5735119).
 * Now silenced at the source. */
memset(&new_input[104], 0, 24);  /* includes [120..127] reserved */
```

Removes 24 noise trits from the GIE dot product. Saves ~661K AND+popcount operations per second. No information lost — these trits were already zero in the signatures.

---

## Phase B: Ternary RSSI Encoding (Low Risk)

### B1. RSSI Dead Zone

Replace the binary RSSI thermometer with a ternary thermometer:

```c
/* [0..15] RSSI ternary thermometer
 * +1 if confidently above threshold (rssi > threshold + RSSI_MARGIN)
 * -1 if confidently below threshold (rssi < threshold - RSSI_MARGIN)
 *  0 if uncertain (within ±RSSI_MARGIN of threshold)
 * RSSI_MARGIN = 2 dBm, based on typical ESP32-C6 RSSI noise. */
#define RSSI_MARGIN 2
for (int i = 0; i < 16; i++) {
    int threshold = -80 + i * 4;
    int diff = st->rssi - threshold;
    new_input[i] = (diff > RSSI_MARGIN) ? T_POS
                 : (diff < -RSSI_MARGIN) ? T_NEG
                 : T_ZERO;
}
```

Expected effect: 2-4 RSSI trits near the signal level become 0 per packet. These are the trits that flip between packets due to noise — exactly the trits that add noise to the dot product. Silencing them should reduce dot product variance without reducing discrimination.

### B1 Validation

1. Build with RSSI dead zone, flash with normal sender (4-pattern cycling)
2. Run Test 11: check signature energy (expect ~2-4 fewer non-zero trits per signature vs baseline)
3. Run Test 14C: check TriX accuracy (expect 100% — the dead zone should not affect well-separated patterns)
4. If TriX accuracy drops: try RSSI_MARGIN = 1 (narrower dead zone)

---

## Phase C: Future (Separate Experiment, Not This Session)

### C1. MTFP RSSI Encoding

Replace the 16-trit thermometer with one or more MTFP values:
- **RSSI level:** 5 trits (sign + 2 exp + 2 mant) relative to -50 dBm reference
- **RSSI variance:** 5 trits encoding the spread of recent RSSI readings
- **Remaining 6 trits:** zeros or additional features

This compresses RSSI from 16 trits to 10 with richer structure. The freed 6 trits join the 24 zeroed sequence trits for a total of 30 available trits for future feature encoding.

### C2. Timing Feature Expansion

Use freed trits [104..119] for:
- Gap variance MTFP (5 trits)
- Burst-count MTFP (5 trits)
- Steady-count MTFP (5 trits)
- Flag trit (1 trit: bursty vs steady)

This gives the system richer temporal discrimination without increasing the input dimension.

---

## Disposition of All Eight Items

| # | Item | Disposition | Phase |
|---|------|------------|-------|
| 1 | Float bias state (0.9 decay) | Integer at 10× resolution | A1 |
| 2 | Float bias magnitude | Integer (folded into A1) | A2 |
| 3 | RSSI binary thermometer | Ternary dead zone (±2 dBm) | B1 |
| 3b | RSSI MTFP encoding | Future enhancement | C1 |
| 4 | Payload binary bits | No change — genuinely binary | — |
| 5 | Sequence binary features | Zero permanently | A3 |
| 6 | Elapsed time float | No change — display only | — |
| 7 | Null flip rate float | No change — diagnostic only | — |
| 8 | Mean Hamming float | No change — reporting only | — |

---

## After Phase A, the Signal Path Is

```
ESP-NOW packet
  → RSSI: ternary thermometer (Phase B) or binary thermometer (Phase A)
  → Pattern ID: ternary one-hot (+1/+1 vs -1/-1)
  → Payload: binary trits (genuinely binary data)
  → Timing: MTFP21 ternary (already implemented)
  → Sequence: zeros (Phase A)
  → GIE: AND + popcount + ISR blend (all integer/ternary)
  → TriX: ISR argmax (integer)
  → LP CfC: assembly (AND + popcount + branch)
  → VDB: assembly (same)
  → Agreement: ternary disagree-count (integer)
  → Bias: integer state, integer decay, integer magnitude
  → ISR threshold: integer subtract + compare
```

No floating point in the mechanism path. No binary encoding of uncertain signals. The ternary invariant is complete from timing through bias, with payload as the documented exception (genuinely binary data, enrollment handles irrelevant bits).

---

## Implementation Order

1. **Phase A (today, if time permits):** A1-A2 (integer bias) + A3 (zero sequence trits). No re-enrollment needed. No behavioral change expected. Can be validated by re-running multi-seed 14C and comparing crossover steps.

2. **Phase B (next session):** B1 (RSSI dead zone). Needs re-enrollment (Test 11) because the signatures will change. Validate with normal sender first, then transition sender.

3. **Phase C (future):** C1-C2 (MTFP RSSI, timing expansion). Separate LMM cycle. Full test suite re-validation.

---

*Zero means "I don't know." The system should say it when it means it.*
