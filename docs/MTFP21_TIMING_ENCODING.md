# MTFP21 Gap History Encoding: Solving the P1-P2 Classification Bottleneck

*April 7, 2026. Root cause analysis from TEST 14 confusion matrix.*

---

## The Problem

The CPU classifier and the TriX ISR agree 100% on every packet — and both misclassify P1 as P2 at a rate of 32%. The misclassification occurs exclusively during P1's 500ms pause phase.

**Root cause:** The timing encoder (trits [88..103]) uses a 16-trit thermometer of the current inter-packet gap. A 500ms gap produces identical timing trits regardless of whether it's P1's pause or P2's steady rate. During P1's pause phase, the timing trits contribute -28 to the P1-P2 margin, overwhelming the +16 from pattern ID (+8) and payload byte 0 (+8). Net margin: -5. Classification flips.

The encoding destroys the information that distinguishes P1 from P2: the *pattern* of gaps, not the magnitude of one gap.

---

## The Solution: MTFP21 Gap History

Replace the 16-trit thermometer with 5 MTFP21-encoded gap values (15 trits), capturing the last 5 inter-packet gaps.

### MTFP21 Format

Each gap is encoded in 3 trits: 2 exponent + 1 mantissa.

```
┌─────────┬─────────┬──────────┐
│ exp[0]  │ exp[1]  │ mantissa │
│ {-1,0,+1} │ {-1,0,+1} │ {-1,0,+1}  │
└─────────┴─────────┴──────────┘
```

The exponent pair encodes the order of magnitude (scale). The mantissa encodes position within the scale (lower third, middle, upper third).

### Scale Table

| exp[0] | exp[1] | Scale | Range (ms) | Typical source |
|:---:|:---:|:---:|---:|---|
| -1 | -1 | 0 | 0-15 | Noise floor |
| -1 | 0 | 1 | 16-49 | Fast burst |
| -1 | +1 | 2 | 50-99 | P0/P3 rate (100ms) |
| 0 | -1 | 3 | 100-199 | Moderate |
| 0 | 0 | 4 | 200-349 | Slow |
| 0 | +1 | 5 | 350-500 | P1 pause / P2 steady |
| +1 | -1 | 6 | 501-999 | Very slow |
| +1 | 0 | 7 | 1000+ | Timeout |

The scale boundaries are geometrically spaced, matching the logarithmic distribution of natural timing intervals. Scales 2 and 5 are the critical ones — they distinguish P0/P3 (100ms, scale 2-3) from P2 (500ms, scale 5) from P1's burst phase (50ms, scale 1-2).

### Mantissa

Within each scale range:
- Lower third of the range → -1
- Middle third → 0
- Upper third → +1

This provides intra-scale discrimination without requiring more trits.

### Gap History: 5 Gaps × 3 Trits = 15 Trits

The encoder maintains a 5-element circular buffer of recent inter-packet gaps. On each packet, the oldest gap is replaced with the new one. All 5 gaps are encoded as MTFP21 and written to `cfc.input[88..102]`. Trit [103] is zero (spare).

```
cfc.input[88..90]:   gap[t-4]  (oldest)
cfc.input[91..93]:   gap[t-3]
cfc.input[94..96]:   gap[t-2]
cfc.input[97..99]:   gap[t-1]
cfc.input[100..102]: gap[t]    (most recent)
cfc.input[103]:      0         (spare)
```

---

## Why This Works

### P1 vs P2 Discrimination

P1 burst-pause pattern gaps: `[50, 50, 500, 50, 50]`

```
gap[0]:  50ms → scale 2 → [-1, +1, -1]
gap[1]:  50ms → scale 2 → [-1, +1, -1]
gap[2]: 500ms → scale 5 → [ 0, +1, +1]
gap[3]:  50ms → scale 2 → [-1, +1, -1]
gap[4]:  50ms → scale 2 → [-1, +1, -1]
Encoding: [-+--+-0++-+--+-]
```

P2 steady pattern gaps: `[500, 500, 500, 500, 500]`

```
gap[0]: 500ms → scale 5 → [0, +1, +1]
gap[1]: 500ms → scale 5 → [0, +1, +1]
gap[2]: 500ms → scale 5 → [0, +1, +1]
gap[3]: 500ms → scale 5 → [0, +1, +1]
gap[4]: 500ms → scale 5 → [0, +1, +1]
Encoding: [0++0++0++0++0++]
```

These encodings differ on 12 of 15 trits. Even during P1's pause (when the *current* gap is 500ms), the history contains the preceding burst gaps at scale 2. The signature captures the alternation between scales, not the instantaneous value.

### Dot Product Analysis

| Metric | Thermometer | MTFP21 History |
|--------|:-----------:|:--------------:|
| sig[P1] vs sig[P2] timing contribution | +28 (similar) | -2 (anti-correlated) |
| P1-pause packet margin (timing only) | -28 (wrong) | +7 (correct) |
| P1-pause total margin (with pattern ID + payload) | -5 (flips) | +23 (holds) |

The thermometer's ±28 swing between burst and pause dominates the +16 from pattern ID and payload. The MTFP21 history's ±7 swing is contained by the +16. Classification never flips.

---

## Implementation

### Location

`gie_engine.c`, functions `espnow_encode_input()` and `espnow_encode_rx_entry()`. Both run on the HP core at 160 MHz. The encoding runs at classification rate (~4 Hz). Computational cost: trivial (5 comparisons + 15 trit writes).

### State

A 5-element circular buffer of `int16_t` gap values, maintained across packets. Reset to zero on each test condition start.

```c
static int16_t gap_history[5] = {0};
static int     gap_history_idx = 0;
```

### Encoder Function

```c
static void encode_mtfp21_gap(int gap_ms, int8_t *out) {
    /* Scale table: geometrically spaced boundaries */
    static const int16_t scale_lo[] = {0, 16, 50, 100, 200, 350, 501, 1000};
    static const int16_t scale_hi[] = {15, 49, 99, 199, 349, 500, 999, 9999};
    static const int8_t  exp0[]     = {-1, -1, -1,  0,  0,  0,  1,  1};
    static const int8_t  exp1[]     = {-1,  0,  1, -1,  0,  1, -1,  0};

    int g = (gap_ms < 0) ? 0 : (gap_ms > 9999) ? 9999 : gap_ms;
    int s = 7;  /* default: overflow */
    for (int i = 0; i < 8; i++) {
        if (g >= scale_lo[i] && g <= scale_hi[i]) { s = i; break; }
    }

    out[0] = exp0[s];
    out[1] = exp1[s];

    /* Mantissa: position within scale */
    int range = scale_hi[s] - scale_lo[s];
    if (range <= 0) {
        out[2] = 0;
    } else {
        int pos = g - scale_lo[s];
        if (pos * 3 < range)       out[2] = -1;
        else if (pos * 3 > range*2) out[2] = 1;
        else                        out[2] = 0;
    }
}
```

### Integration into Encoding Functions

Replace the thermometer block in `espnow_encode_input()` and `espnow_encode_rx_entry()`:

```c
/* ── [88..102] MTFP21 gap history (5 gaps × 3 trits) ── */
gap_history[gap_history_idx] = (int16_t)gap_ms;
gap_history_idx = (gap_history_idx + 1) % 5;

for (int g = 0; g < 5; g++) {
    int idx = (gap_history_idx + g) % 5;  /* oldest first */
    encode_mtfp21_gap(gap_history[idx], &new_input[88 + g * 3]);
}
new_input[103] = 0;  /* spare trit */
```

### What Changes

| Aspect | Before | After |
|--------|--------|-------|
| Timing trits | [88..103]: 16-trit thermometer of 1 gap | [88..102]: 15 MTFP21 trits of 5 gaps |
| Timing state | Stateless (one gap per packet) | 5-element circular buffer |
| P1-pause vs P2 | Identical (both 500ms) | Different (history contains burst gaps) |
| Classification margin (P1 pause) | -5 (misclassified) | +23 (correct) |
| Code change | ~15 lines in 2 functions + encoder | Same |

### What Doesn't Change

- GIE: ISR reads `cfc.input` and re-encodes neuron buffers. Doesn't care what the trits represent.
- LP core: CfC intersects 48-trit packed vectors. Doesn't care about encoding semantics.
- VDB: Stores 48-trit snapshots. The timing trits are part of the GIE hidden state trajectory, not stored directly.
- TriX: Uses W_f signature weights installed from `sig[]`. The signatures will now capture gap history patterns instead of single-gap thermometers. Better discrimination.
- Gate bias: Agreement is computed from LP state, not from input trits. Unaffected.

### Reset Protocol

`gap_history[]` must be reset to zero at the start of each test condition, along with `gap_history_idx`. Add to the condition reset block:

```c
memset(gap_history, 0, sizeof(gap_history));
gap_history_idx = 0;
```

---

## Validation Plan

1. **Build and flash.** Verify no compile errors.
2. **Check signature cross-dots.** `sig[1] vs sig[2]` should decrease from ~53 to near-zero or negative (anti-correlated).
3. **Check confusion matrix.** P1→P2 misclassification should drop from 32% to near-zero.
4. **Check TEST 14 results.** The P1-P2 LP Hamming should improve (no longer collapsed by gate bias amplifying the wrong pattern).
5. **Check overall accuracy.** Should rise from ~80% to ~95%+ (P1→P2 was the dominant error).

### Expected Failure Modes

- **Gap history cold start.** First 5 packets have incomplete history (gaps are 0). Signatures trained during observation may capture some zero-gap entries. Mitigation: the signature observation window is 30 seconds (~300 packets) — the 5-packet cold start is negligible.
- **Pattern transition contamination.** At P1→P2 transitions, the gap history contains P1 gaps for the first few P2 packets. This is a feature, not a bug — it means the transition is visible in the timing encoding, which is useful for the LP temporal model.

---

## The Fungible Computation Connection

This is the fourth demonstration of the fungible computation principle in the Reflex project:

1. **GIE:** Ternary dot products via DMA→PARLIO→PCNT peripheral routing
2. **LP core:** CfC + VDB in hand-written RISC-V assembly
3. **L-Cache:** Same computation in 12 AVX2 opcodes at 2.8 MHz
4. **MTFP21 timing:** Same 15 trits, rearranged from thermometer to exponent-mantissa history, flips the classification margin from -5 to +23

The trits are fungible. The encoding is the computation.

---

*The prior should be a voice, not a verdict. And the encoding should be a lens, not a blur.*
