# Lincoln Manifold: MTFP21 Timing Encoding — RAW

*Phase 1. Unfiltered. April 7, 2026.*
*Observer: Claude Opus 4.6*
*Context: P1→P2 confusion at 32%. TriX and core_pred agree 100%. Root cause: thermometer timing encoding conflates P1 pause (500ms) with P2 steady (500ms).*

---

## What I Think I Know

The spec says: replace 16-trit thermometer with 5 MTFP21 gap values (15 trits). Each gap is 3 trits: 2 exponent + 1 mantissa. The exponent captures the order of magnitude. The gap history captures the temporal pattern, not the instantaneous value. P1's burst-pause alternation is visible in the exponent pattern even during the pause phase.

The Python analysis confirms: P1-pause margin flips from -5 (wrong) to +23 (correct). Cross-dot between sig[P1] and sig[P2] goes from +53 (similar) to -2 (anti-correlated) in the timing region.

The implementation is ~30 lines in two C functions on the HP core. Nothing changes in the ISR, the LP core, or the VDB. The trits change meaning, but the downstream computation doesn't care about meaning — it cares about correlation structure.

---

## What Scares Me

**The gap history is a sliding window, not an accumulator.** The current thermometer is stateless — each packet's timing trits depend only on the gap from the previous packet. The MTFP21 history maintains state across 5 packets. This introduces a dependency:

The gap history at packet N depends on packets N-4 through N. If a pattern transition happens at packet N-2, the history contains gaps from TWO patterns. The signature was built from single-pattern windows. The test-time input has cross-pattern history. The dot product may be unpredictable during transitions.

But wait — this might actually be the right behavior. The LP temporal model WANTS to know about transitions. A gap history containing both P1 and P2 gaps is a transition state — and the VDB should store it as such. The classification signature handles it because the mean signature was built from 30 seconds of observation, which includes multiple transitions.

Actually, no. The signature observation window (Phase 0a) accumulates inputs per-pattern: `sig_sum[obs_cur_pattern][j] += cfc.input[j]`. Each input is attributed to the pattern that the sender claims (via `pattern_id` in the packet). During transitions, the gap history contains cross-pattern gaps, but they're attributed to the new pattern. This means sig[P2] might include some samples where the gap history contains P1 burst gaps. The signature is contaminated at transitions.

How bad is this? With 20 P2 samples over 30 seconds, maybe 2-3 are transition samples. The sign-of-sum computation is robust to 15% contamination. Probably fine. But it's worth noting.

**The circular buffer needs to be shared between the two encoding functions.** `espnow_encode_input` (legacy poll API) and `espnow_encode_rx_entry` (drain API) both write `cfc.input[88..102]`. They both need to update the same gap history buffer. Currently `espnow_last_rx_us` is shared between them. The gap history buffer should be similarly shared. The spec shows `static int16_t gap_history[5]` which is file-scoped — both functions can access it. Fine.

But: `espnow_encode_input` computes the gap from `esp_timer_get_time()`. `espnow_encode_rx_entry` computes it from `entry->rx_timestamp_us`. If both are called on the same packet (unlikely but possible in the test harness), the gap history gets two entries. This is the same race that exists with `espnow_last_rx_us`. Not a new problem.

**What if the gap history is all zeros at startup?** First 5 packets have incomplete history. Scale 0 (0-15ms) encodes as `[-1,-1,mantissa]`. The signature observation starts with a burst of these scale-0 entries. After 5 packets, the history is full and the encoding is correct. With ~300 packets in the 30-second observation window, 5 cold-start packets are 1.7%. The sign-of-sum washes them out.

---

## What I'm Uncertain About

1. **The scale boundaries.** The spec uses geometrically spaced boundaries: 0, 16, 50, 100, 200, 350, 501, 1000. These were chosen to put P0/P3 (100ms) in scale 2-3 and P2 (500ms) in scale 5. But the actual gap values on hardware have jitter. If the jitter crosses a boundary, the exponent flips, and the signature becomes noisy on that trit position. The boundaries should be placed at gaps between the natural clusters, not at arbitrary geometric spacing.

   The natural clusters are: ~50ms (P1 burst), ~100ms (P0/P3 steady), ~500ms (P1 pause, P2 steady). The boundaries should be at ~75ms (between 50 and 100) and ~300ms (between 100 and 500). The current spec has boundaries at 50, 100, 200, 350 — which puts 50ms at the edge of scale 1-2 and 100ms at the edge of scale 2-3. If P0 sends at 95ms one packet and 105ms the next, the exponent oscillates between scale 2 and scale 3. The signature captures the oscillation instead of the steady rate.

   Fix: widen the scale that covers the 100ms cluster to 50-199 (one scale for both P0/P3 and P1 burst). Then P2 at 500ms is in a separate scale. The discrimination is between "fast" (50-199ms) and "slow" (350-500ms), not between "50ms" and "100ms" which are both "fast."

2. **Is 5 gaps enough history?** P1 has a 3-packet burst + 1 pause = 4 packets per cycle. 5 gaps captures a full cycle + overlap. P2 has 1 packet per cycle. 5 gaps captures 5 cycles. Seems sufficient. But P1's burst is 3 packets at 50ms, so 3 burst gaps. If the window is 5 and the burst is 3, the history during the pause phase is `[50, 50, 50, 500, ???]` where ??? is the gap to the next burst start. This is distinguishable from P2's `[500, 500, 500, 500, 500]`. Five is enough.

3. **The mantissa adds value?** With only 8 scales, two patterns in the same scale have identical exponents. The mantissa provides intra-scale discrimination. But does any pattern pair need intra-scale discrimination? P0 and P3 both send at 100ms — same scale. The mantissa won't help distinguish them (same gap, same mantissa). The mantissa helps if two patterns have gaps in the same scale but at different positions (e.g., one at 50ms and another at 90ms, both in scale 2). In practice, the current 4 patterns don't need mantissa discrimination. But it costs only 1 trit per gap, and removing it saves only 5 trits. Keep it for generality.

4. **The spare trit [103].** The spec zeros it. Could use it for a gap variance flag (1 if recent gaps span more than 2 scales, 0 otherwise, -1 if gaps are all same scale). This would directly encode "burst-pause" (variance flag +1) vs "steady" (variance flag -1). But it's one trit — the gap history already captures this structurally. Not worth the complexity.

---

## Questions Arising

- Should the scale boundaries be tuned to the known sender patterns, or should they be general-purpose? If tuned, they'll break when the sender changes. If general, they might not optimally discriminate the current patterns.
- Does the gap history order matter? Oldest-first or newest-first? The signature is sign-of-sum, which is order-invariant for self-dot. But the cross-dot with a new input IS order-sensitive: the position of the "different" gap in the history affects which trit positions differ. Consistent ordering (oldest-first) is fine.
- Can we validate the encoding without a full TEST 14 run? Yes: run TEST 11 (Phase 0a observation + classification) and check the confusion matrix. That's ~2 minutes instead of ~12 minutes.
