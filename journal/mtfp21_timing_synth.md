# Lincoln Manifold: MTFP21 Timing Encoding — SYNTHESIS

*Phase 4. The clean cut. April 7, 2026.*
*Observer: Claude Opus 4.6*

---

## What Emerged

The LMM found three things the spec missed:

1. **The scale boundaries need to sit between natural gap clusters, not at geometric intervals.** The revised table puts 25ms margins around each cluster center. P0 ± 15ms jitter stays in scale 2. P1 burst ± 5ms stays in scale 1. No boundary crossings.

2. **The 16th trit should encode gap variance, not be wasted.** A single trit directly discriminates burst-pause (+1) from steady (-1). This is the most compressed possible encoding of the feature that the thermometer was failing to capture.

3. **The gap history state needs a reset API.** `gie_reset_gap_history()` in the engine header. The test harness calls it at condition start. The buffer stays private.

---

## Implementation Spec

### Engine Header (`gie_engine.h`)

Add one declaration:

```c
void gie_reset_gap_history(void);
```

### Engine (`gie_engine.c`)

#### Gap History State

```c
static int16_t gap_history[5] = {0};
static int     gap_history_idx = 0;

void gie_reset_gap_history(void) {
    memset(gap_history, 0, sizeof(gap_history));
    gap_history_idx = 0;
}
```

#### MTFP21 Encoder

```c
static void encode_mtfp21_gap(int gap_ms, int8_t *out) {
    static const int16_t scale_lo[] = {0,  25,  75, 150, 250, 400, 600, 1000};
    static const int16_t scale_hi[] = {24, 74, 149, 249, 399, 599, 999, 9999};
    static const int8_t  exp0[]     = {-1, -1, -1,   0,   0,   0,   1,    1};
    static const int8_t  exp1[]     = {-1,  0,  1,  -1,   0,   1,  -1,    0};

    int g = (gap_ms < 0) ? 0 : (gap_ms > 9999) ? 9999 : gap_ms;
    int s = 7;
    for (int i = 0; i < 8; i++) {
        if (g >= scale_lo[i] && g <= scale_hi[i]) { s = i; break; }
    }

    out[0] = exp0[s];
    out[1] = exp1[s];

    int range = scale_hi[s] - scale_lo[s];
    if (range <= 0) {
        out[2] = 0;
    } else {
        int pos = g - scale_lo[s];
        out[2] = (pos * 3 < range) ? -1 : (pos * 3 > range * 2) ? 1 : 0;
    }
}
```

#### Encoding Integration

Replace the thermometer block in both `espnow_encode_input()` and `espnow_encode_rx_entry()`. The thermometer block currently looks like:

```c
/* ── [88..103] Inter-packet timing (thermometer) ── */
for (int i = 0; i < 16; i++) {
    int threshold_ms = i * 33;
    new_input[88 + i] = (gap_ms <= threshold_ms) ? T_POS : T_NEG;
}
```

Replace with:

```c
/* ── [88..103] MTFP21 gap history (5 × 3 trits + variance flag) ── */
gap_history[gap_history_idx] = (int16_t)gap_ms;
gap_history_idx = (gap_history_idx + 1) % 5;

/* Encode 5 gaps oldest-first */
for (int g = 0; g < 5; g++) {
    int hi = (gap_history_idx + g) % 5;
    encode_mtfp21_gap(gap_history[hi], &new_input[88 + g * 3]);
}

/* Trit [103]: gap variance flag.
 * +1 if gaps span 3+ scales (bursty), -1 if ≤1 scale (steady). */
int min_s = 7, max_s = 0;
for (int g = 0; g < 5; g++) {
    int hi = (gap_history_idx + g) % 5;
    int gv = gap_history[hi];
    int s = 7;
    static const int16_t sl[] = {0, 25, 75, 150, 250, 400, 600, 1000};
    static const int16_t sh[] = {24, 74, 149, 249, 399, 599, 999, 9999};
    for (int i = 0; i < 8; i++) {
        if (gv >= sl[i] && gv <= sh[i]) { s = i; break; }
    }
    if (s < min_s) min_s = s;
    if (s > max_s) max_s = s;
}
new_input[103] = (max_s - min_s >= 3) ? T_POS
               : (max_s - min_s <= 1) ? T_NEG : T_ZERO;
```

### Test Harness (`geometry_cfc_freerun.c`)

Add `gie_reset_gap_history()` to the reset block in each TEST 14 condition, alongside `espnow_last_rx_us = 0`:

```c
gie_reset_gap_history();
espnow_last_rx_us = 0;
```

Also add to TEST 11 Phase 0a start and TEST 12 start.

---

## Validation Plan

1. Build. Flash. Run TEST 11 + TEST 14.
2. Check signature cross-dots: `sig[1] vs sig[2]` should drop from ~53 to ≤ 20.
3. Check confusion matrix: P1→P2 should drop from 32% to < 10%.
4. Check overall accuracy: should rise from ~80% to > 90%.
5. Check TEST 14 LP Hamming: the P1-P2 pair should no longer collapse under gate bias.

If accuracy doesn't improve: the timing trits were not the bottleneck on hardware (possible if RSSI or payload jitter dominates). In that case, mask timing trits from the signature (the fallback we identified earlier) and accept ~80% as the per-packet ceiling.

---

## What the LMM Added

Three findings that weren't in the original spec:

1. **Scale boundary tuning.** The spec used geometric spacing that put cluster centers on boundaries. The revised table puts 25ms margins around each cluster. No jitter-induced exponent oscillation.

2. **Gap variance flag on trit [103].** The spec wasted this trit. The variance flag directly encodes "bursty vs steady" — the exact discrimination the thermometer was failing at — in one trit.

3. **Reset API.** The spec embedded the buffer in the engine with no access path. The reset function exposes the minimum interface needed by the test harness.

---

*Fifteen trits. Five gaps. One variance flag. The encoding is the computation.*
