# Lincoln Manifold: MTFP21 Timing Encoding — REFLECT

*Phase 3. Axe sharpening. April 7, 2026.*
*Observer: Claude Opus 4.6*

---

## Core Insight

**The encoding is the computation.**

The thermometer encodes one number (gap magnitude) into 16 trits. The MTFP21 history encodes five numbers (gap sequence) into 15 trits. Same budget. Completely different information. The ternary dot product doesn't know what the trits mean — it measures correlation structure. The thermometer creates high correlation between P1-pause and P2-steady because they're the same number. The MTFP21 history creates low correlation because they're different sequences.

The computation hasn't changed. The neural network hasn't changed. The weights haven't changed. The ISR hasn't changed. The LP core hasn't changed. The only change is how 15 trits in a 128-trit vector are assigned. This is the fungible computation principle applied to encoding rather than substrate.

---

## Resolved Tensions

### Scale Boundaries (Node 1)

The geometric spacing puts boundaries at cluster edges. The revised table puts boundaries between clusters. Three critical regions:

- **Scale 1 (25-74ms):** Contains P1 burst gaps. Clean separation from P0/P3.
- **Scale 2 (75-149ms):** Contains P0/P3 steady gaps. 25ms margin on each side.
- **Scale 5 (400-599ms):** Contains P1 pause and P2 steady. Both here — discrimination comes from the history pattern, not the individual scale.

P0 at 100ms ± 15ms stays in scale 2 (75-149ms). No boundary crossings. P1 burst at 50ms ± 5ms stays in scale 1 (25-74ms). P2 at 500ms ± 20ms stays in scale 5 (400-599ms). Stable.

### Gap History Access (Node 2)

Add `void gie_reset_gap_history(void)` to `gie_engine.h`. The test harness calls it at condition start. The gap history buffer stays static inside `gie_engine.c`. The API is one function. Clean.

### The 16th Trit (Node 6)

Gap variance flag. Compute as: find min and max scale indices across the 5 gaps. If `max_scale - min_scale >= 3`: trit = +1 (bursty). If `max_scale - min_scale <= 1`: trit = -1 (steady). Otherwise: trit = 0 (moderate variance).

This single trit is the most direct encoding of "burst-pause vs steady" possible in one trit. P1 always has variance ≥ 3 (scale 1 to scale 5 = gap of 4). P2 always has variance 0 (all scale 5). P0 always has variance 0 (all scale 2).

---

## What Simplicity Looks Like

The implementation is:

1. **One encoder function** (`encode_mtfp21_gap`): 15 lines. Takes gap_ms, writes 3 trits.
2. **One reset function** (`gie_reset_gap_history`): 3 lines. Zeros the buffer.
3. **Replace the thermometer block** in two encoding functions: ~10 lines each.
4. **Add the variance trit**: 8 lines.

Total new code: ~45 lines. Replaces ~10 lines of thermometer encoding. Net change: +35 lines.

No new files. No header changes beyond one new function declaration. No ISR changes. No LP core changes. No VDB changes.

---

## What the Hardware Will Tell Us

The Python analysis predicts the P1-pause margin flips from -5 to +23. The hardware may differ because:

1. RSSI jitter is real (Python used constant -50 dBm)
2. Packet timing jitter is real (Python used exact 50/500ms)
3. The gap history during transitions is contaminated
4. The signature observation window captures a specific sender phase

The prediction: classification accuracy rises from ~80% to ~95%+. The P1→P2 confusion drops from 32% to <5%. The remaining errors are transition packets with contaminated gap history (which is acceptable — the LP accumulator handles them).

If accuracy doesn't improve: the timing trits were never the bottleneck, and something else is wrong. But the dot product decomposition is exact — we know the timing swing is ±28 points and the stable signal is +16 points. Eliminating the swing should fix it.
