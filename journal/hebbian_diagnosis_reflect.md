# Reflections: Diagnosed Hebbian

## Core Insight

The fix is the diagnosis, not the flip. Everything else stays the same — the TriX accumulator target, the gating, the flip-selection loop, the repack. The only change: before flipping, ask "is this an f-error or a g-error?" and operate on the correct matrix.

## Resolved Tensions

### Is the flip direction logic correct?
Yes. "Flip a weight contributing to the current dot direction" pushes the dot toward zero. When the dot needs to reverse sign, passing through zero is the correct trajectory. Multiple rate-limited flips complete the reversal over several steps. The logic was never wrong — it was applied to the wrong matrix.

### Can both f and g be wrong simultaneously?
Yes, always, when there's an error and f != 0 (Node 1, Region 3). The diagnosis doesn't find THE wrong pathway — it finds the CHEAPER one to fix. This is a greedy heuristic, not an optimal solution. But with rate-limited single flips, greedy is appropriate: fix the easy thing first, re-evaluate next step.

### Will this produce fewer total flips?
Not necessarily — the flip count may be similar. But the flips will be PRODUCTIVE instead of counterproductive. v1 had 250 flips in 90s, half of which were breaking the correct pathway. v2 should have a similar number of flips but each one moves the system toward the target, not away from it.

## Hidden Assumptions Challenged

### "g-pathway updates are a v2 feature"
No. They're essential for v1 correctness. Without g-pathway updates, ~50% of errors are unfixable. The "v2" framing was wrong — it wasn't an enhancement, it was the missing half of the mechanism.

### "The learning rate is too high"
Maybe, but the rate isn't the primary issue. 250 flips that are 50% productive / 50% counterproductive ≈ 0 net effect minus noise. 250 flips that are ~100% productive ≈ 250 units of progress. The rate might need tuning (currently 100ms between updates), but the diagnosis fixes the direction, which is the dominant error source.

### "The f_dot == 0 case needs a desired-direction computation"
Yes. When f = 0 and target != 0, we need f to fire. The desired direction is f' = tmul(target, g). If g = +1 and target = +1: need f' = +1 → push f_dot positive. If g = -1 and target = +1: need f' = -1 → push f_dot negative. The v1 code flipped a random weight in this case. The fix: compute the desired f direction from target and g, then flip a weight with contribution OPPOSITE to the desired direction (so the flip pushes the dot toward the desired sign).

Wait — that's the opposite of the Region 3 logic. In Region 3 (dot in wrong direction), we flip same-direction contributors to push toward zero. In Region 2 (dot at zero, want it nonzero), we need to push the dot AWAY from zero. Flip a weight with contribution opposite to the desired direction? No — flip a weight with contribution in the desired direction... no.

Let me be precise. Current f_dot = 0. Desired f' = +1 (want f_dot > 0). I need to INCREASE f_dot. Each weight contributes tmul(W_f[n][i], concat[i]) to f_dot. To increase f_dot, I should flip a weight that currently contributes NEGATIVELY (flipping it adds +2). To decrease f_dot, flip a positive contributor.

So for Region 2: flip a weight whose contribution is OPPOSITE to the desired direction. This pushes the dot toward the desired sign.

For Region 3: flip a weight whose contribution MATCHES the current (wrong) direction. This pushes the dot toward zero (first step toward reversal).

These are different! The flip-selection logic needs the DESIRED direction, not just the current direction.

Unified rule:
- Compute the direction we want to PUSH the dot: 
  - Region 2 (f=0, want nonzero): push toward desired_f sign
  - Region 3 (f wrong): push toward zero (then eventually toward desired)
  - Region 1 (f nonzero, want zero): push toward zero
- Select a weight whose contribution is OPPOSITE to the push direction
- Flip it (contribution changes sign, pushing the dot in the push direction)

Wait — I'm confusing myself. Let me be very precise.

A weight with contribution +1 adds +1 to f_dot. Flipping it changes the contribution to -1, so f_dot changes by -2. Flipping a +1 contributor DECREASES f_dot.

A weight with contribution -1 adds -1. Flipping → +1. f_dot changes by +2. Flipping a -1 contributor INCREASES f_dot.

So:
- To INCREASE f_dot: flip a NEGATIVE contributor
- To DECREASE f_dot: flip a POSITIVE contributor
- To push f_dot toward zero when f_dot > 0: DECREASE it → flip POSITIVE contributor
- To push f_dot toward zero when f_dot < 0: INCREASE it → flip NEGATIVE contributor

v1's rule: "flip a weight contributing in the same direction as f_dot" = when f_dot > 0, flip positive contributor → DECREASE f_dot → push toward zero. ✓

For Region 2 (f_dot = 0, want f_dot in direction D):
- If D = +1 (want f_dot > 0): flip NEGATIVE contributor → INCREASE f_dot ✓
- If D = -1 (want f_dot < 0): flip POSITIVE contributor → DECREASE f_dot ✓

So the rule for Region 2: "flip a weight contributing OPPOSITE to the desired direction" → increases |f_dot| in the desired direction.

And v1's rule for Regions 1 and 3: "flip a weight contributing in the SAME direction as the current f_dot" → decreases |f_dot|.

These are different selection criteria. The unified rule:
- Compute `push_dir`: the direction we want to move the dot
  - Region 1 (want zero): push_dir = -sign(f_dot) (toward zero)
  - Region 2 (want nonzero): push_dir = desired_f (away from zero toward target)
  - Region 3 (want reversal): push_dir = -sign(f_dot) (toward zero, then past)
- Select a weight with contribution OPPOSITE to push_dir
- Flip it

Actually Regions 1 and 3 have the same push_dir: -sign(f_dot). So it's just:
- If f_dot != 0 (Regions 1, 3): push_dir = -sign(f_dot). Select same-direction contributor (=opposite of push_dir). This is v1's logic.
- If f_dot == 0 (Region 2): push_dir = desired_f. Select contributor opposite to push_dir. This is new.

For Region 2, v1 used: "flip any non-zero contributing weight" (random direction). The fix: "flip a weight with contribution opposite to the desired f direction."

Let me verify: contribution -1, desired = +1. Flip → contribution becomes +1. f_dot increases by +2. f_dot goes from 0 to +2 → f = +1 = desired. ✓

OK. The Region 2 fix is precise. And it only applies when f_dot = 0, which is a subset of the error cases.

The same logic applies to g-pathway flips when the diagnosis says "fix g."

## What I Now Understand

The implementation changes are:

1. Read g_dot from LP SRAM (add `memcpy(dots_g, ulp_addr(&ulp_lp_dots_g), ...)`)
2. Per-neuron diagnosis: is this an f-error or g-error? (compare |f_dot| vs |g_dot|)
3. For the diagnosed pathway, determine push direction
4. Select a weight with contribution opposite to push direction, flip it
5. Track f-flips and g-flips separately, repack only the changed matrix
