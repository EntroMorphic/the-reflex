# Lincoln Manifold: MTFP21 Timing Encoding — NODES

*Phase 2. Grain identification. April 7, 2026.*
*Observer: Claude Opus 4.6*

---

## Node 1: The Scale Boundaries Need Tuning

The spec uses geometric spacing: 0, 16, 50, 100, 200, 350, 501, 1000. The actual sender gaps cluster at ~50ms (P1 burst), ~100ms (P0/P3), and ~500ms (P1 pause / P2). The boundaries should sit between clusters, not at their edges.

If a boundary sits at 100ms and P0 sends with ±10ms jitter, the exponent oscillates between scale 2 (50-99) and scale 3 (100-199). The signature captures oscillation instead of steadiness. The discrimination between P0 and P1-burst (both near 50-100ms) becomes noisy.

**Resolution:** Collapse the fast range into one scale. Three scales matter: fast (0-149ms), moderate (150-349ms), slow (350+ms). The boundary between "fast" and "slow" should be at ~250ms — well above P0/P3 (100ms) and well below P2 (500ms). P1 burst (50ms) and P0 (100ms) are in the same "fast" scale — they don't need intra-scale timing discrimination because their pattern ID and payload already distinguish them.

Revised scale table (still 8 scales, but redistributed):

| Scale | Range | Exponent | What lives here |
|:---:|---:|:---:|---|
| 0 | 0-24ms | (-1,-1) | Noise |
| 1 | 25-74ms | (-1, 0) | P1 burst |
| 2 | 75-149ms | (-1,+1) | P0/P3 steady |
| 3 | 150-249ms | (0,-1) | No pattern (gap) |
| 4 | 250-399ms | (0, 0) | No pattern (gap) |
| 5 | 400-599ms | (0,+1) | P1 pause, P2 steady |
| 6 | 600-999ms | (+1,-1) | Slow |
| 7 | 1000+ms | (+1, 0) | Timeout |

This puts P1 burst (50ms) cleanly in scale 1 and P0/P3 (100ms) cleanly in scale 2. No boundary jitter. P2 (500ms) in scale 5, well-separated.

**Tension with Node 5:** Tuning to known patterns makes the encoding brittle to new senders. But the alternative (geometric spacing) is already tuned — just poorly. Any finite set of boundaries is pattern-dependent. The honest approach: tune to the known patterns, document the tuning, and note that new patterns may require re-tuning.

---

## Node 2: The Gap History State Must Be Visible to the Test Harness

The spec puts `gap_history[5]` as a static in `gie_engine.c`. The test harness needs to reset it at the start of each condition (`memset(gap_history, 0, ...)`). If it's static in the engine, the test harness can't access it.

**Resolution:** Either (a) expose it via `gie_engine.h` as an extern, or (b) add a `gie_reset_gap_history()` function. Option (b) is cleaner — the test harness doesn't need to know the buffer's structure, just that it can be reset.

---

## Node 3: The Two Encoding Functions Must Share Gap History

`espnow_encode_input()` and `espnow_encode_rx_entry()` both write timing trits. The spec shows `gap_history` as file-scoped static, shared between both functions. But `espnow_encode_input()` computes the gap from `esp_timer_get_time()`, while `espnow_encode_rx_entry()` uses the packet's real arrival timestamp. They'll produce different gap values for the same packet.

In practice, TEST 12-14 use only `espnow_encode_rx_entry()` (the drain API). TEST 10 uses `espnow_encode_input()` (the legacy poll API). They're never mixed within a single test condition. The gap history is reset between conditions. No conflict.

**Resolution:** Keep as-is. File-scoped static is correct. Document that the two functions share state and should not be mixed within a condition.

---

## Node 4: The Mantissa May Introduce More Noise Than Signal

With 8 scales, the exponent pair (2 trits) captures the order of magnitude. The mantissa (1 trit) provides intra-scale resolution — lower, middle, or upper third of the range. But the natural jitter of gap values means the mantissa oscillates on consecutive packets even when the true gap is constant.

P0 at 100ms with ±15ms jitter: gaps range from 85-115ms. In scale 2 (75-149ms), the range is 74ms. Position varies from (85-75)/74 = 13% to (115-75)/74 = 54%. Mantissa oscillates between -1 (lower third, <33%) and 0 (middle third, 33-67%). The signature's mantissa trit for this position is sign-of-sum of {-1, 0, -1, 0, ...} ≈ -1 or 0. Either way, it's a weak signal.

For P2 at 500ms with ±20ms jitter: gaps 480-520ms in scale 5 (400-599ms), range 199ms. Position varies from (480-400)/199 = 40% to (520-400)/199 = 60%. Always in the middle third. Mantissa is consistently 0. Stable.

**Resolution:** Keep the mantissa. It's 1 trit per gap (5 trits total). In the worst case it's noisy and contributes ±5 to the dot product — less than the ±10 from exponent differences. In the best case (P2) it's a stable discriminator. The cost is low, the upside is real for future patterns.

---

## Node 5: Transition Contamination Is Bounded

At P1→P2 transitions, the gap history contains P1 gaps for the first 5 P2 packets. These P2 packets are encoded with a mixed history and attributed to P2 in the signature accumulator. This contaminates sig[P2] with P1 gap patterns.

With 20 P2 samples in the observation window and ~2 transitions (entering and leaving P2), that's ~4-6 contaminated samples out of 20, or 20-30% contamination. The sign-of-sum accumulator is robust to this — 70% majority preserves the correct sign on most trits.

But it's worse than the thermometer case. With the thermometer, transition contamination affects at most 1 packet (the first packet after the switch, which has the old pattern's gap). With gap history, contamination persists for 5 packets after a transition. Each of those 5 packets has a partially wrong history.

**Resolution:** Accept the contamination. 20-30% is within the accumulator's robust region. The alternative — resetting gap history at transitions — requires detecting transitions, which requires classification, which is exactly the thing that's broken. Circular dependency. Let the accumulator handle it.

---

## Node 6: The 16th Trit Is Wasted

The spec zeros `cfc.input[103]`. 15 trits for 5 MTFP21 values, 1 spare. Options for the spare trit:

1. Zero (current spec). No information.
2. Gap variance flag: +1 if the 5 gaps span more than 2 scales, -1 if all same scale, 0 if adjacent scales. Directly encodes "burst-pause" (+1) vs "steady" (-1).
3. Mean gap direction: +1 if mean gap > 200ms, -1 if < 200ms, 0 if middle. Coarse overall rate indicator.

Option 2 is the most discriminative. P1 always has variance flag +1 (spans scales 1 and 5). P2 always has -1 (all scale 5). P0 always has -1 (all scale 2). This single trit directly separates burst-pause from steady patterns.

**Resolution:** Use option 2 (gap variance flag). It's one comparison across 5 exponent values — trivial to compute. And it provides exactly the discrimination that the thermometer was failing at: "is this a bursty pattern or a steady one?"

---

## Key Tensions

| Tension | Nodes | Status |
|---------|-------|--------|
| Scale tuning vs generality | 1, 5 | Tune to known patterns, document |
| Gap history access | 2, 3 | Reset function in engine API |
| Mantissa noise vs signal | 4, 1 | Keep — bounded noise, future-proof |
| Transition contamination | 5, 1 | Accept — 20-30% within accumulator tolerance |
| 16th trit usage | 6 | Gap variance flag |
