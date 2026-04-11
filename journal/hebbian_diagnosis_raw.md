# Raw Thoughts: Diagnosed Hebbian — Fix the Right Pathway

## Stream of Consciousness

I found the missing atomic. The diagnosis tells you whether to flip W_f or W_g. But I want to slow down before implementing because I've been wrong twice already on this mechanism and I don't want to be wrong a third time.

Let me trace the diagnosis through concrete examples. The LP CfC blend for neuron n:

```
f = tsign(f_dot)     // from W_f @ concat
g = tsign(g_dot)     // from W_g @ concat
h_new = (f == 0) ? h_old : tmul(f, g)
```

Target from TriX accumulator: target[n] = tsign(accum[pred][n])

Example 1: target = +1, f = +1, g = -1 → h_new = tmul(+1, -1) = -1. Error.
- needed_f_if_g_fixed = tmul(+1, -1) = -1. But actual_f = +1. So f is wrong.
- needed_g_if_f_fixed = tmul(+1, +1) = +1. But actual_g = -1. So g is wrong.
- Both wrong. Fix the easier one (smaller |dot|).

Wait — that can't be right. If target = +1 and the output is -1, we need tmul(f, g) = +1. There are two solutions: (f=+1, g=+1) or (f=-1, g=-1). The current state is (f=+1, g=-1). The cheapest fix is to flip g from -1 to +1 (one pathway change) rather than flip f from +1 to -1 AND g from -1 to -1 (which also works but requires changing both... wait, if f goes to -1 and g stays -1, then tmul(-1, -1) = +1. That works too, only changing f).

So there are TWO single-pathway fixes: flip g OR flip f. Either produces the correct output. The question is which is cheaper (smaller |dot| to reverse).

Let me redo the diagnosis more carefully.

For target T and current (f, g):
- If T = 0: want f = 0 (HOLD). Fix: push f_dot toward zero → flip W_f.
- If T != 0 and f = 0: want f != 0 such that tmul(f, g) = T. Fix: push f_dot away from zero → flip W_f. But wait — we don't know what g will be when f fires. The current g is what g would be if the gate opened. Actually, g is computed regardless of f — the g_dot exists even when the gate holds. So g = tsign(g_dot) is always available. Then: we need tmul(f, g) = T, so f = tmul(T, g) = T * g. If T = +1 and g = +1: need f = +1 (push f_dot positive). If T = +1 and g = -1: need f = -1 (push f_dot negative).

- If T != 0 and f != 0: output = tmul(f, g) != T.
  - Option A: keep g, fix f. Need f' such that tmul(f', g) = T. f' = tmul(T, g).
  - Option B: keep f, fix g. Need g' such that tmul(f, g') = T. g' = tmul(T, f).
  
  If f' != f: f-pathway needs correction. Cost ∝ |f_dot| (big dot = hard to flip).
  If g' != g: g-pathway needs correction. Cost ∝ |g_dot|.
  
  Pick the cheaper option: if |f_dot| < |g_dot|, fix f. Else fix g.
  
  But wait — if f' == f, then f is already correct and we MUST fix g. And vice versa. Let me check:
  
  Case: T = +1, f = +1, g = -1. Output = -1 (wrong).
  Option A: f' = tmul(+1, -1) = -1. f' != f (+1 → -1). Needs f flip. Cost: |f_dot|.
  Option B: g' = tmul(+1, +1) = +1. g' != g (-1 → +1). Needs g flip. Cost: |g_dot|.
  Both options require a flip. Pick the cheaper one.
  
  Case: T = +1, f = -1, g = +1. Output = tmul(-1, +1) = -1 (wrong).
  Option A: f' = tmul(+1, +1) = +1. f' != f (-1 → +1). Cost: |f_dot|.
  Option B: g' = tmul(+1, -1) = -1. g' != g (+1 → -1). Cost: |g_dot|.
  Both need a flip. Pick cheaper.
  
  Case: T = -1, f = +1, g = +1. Output = +1 (wrong).
  Option A: f' = tmul(-1, +1) = -1. f' != f (+1 → -1). Cost: |f_dot|.
  Option B: g' = tmul(-1, +1) = -1. g' != g (+1 → -1). Cost: |g_dot|.
  Both need a flip.
  
  Actually — is there EVER a case where one option doesn't need a flip?
  
  If T = tmul(f, g) already: no error. We don't reach this code.
  If T != tmul(f, g): then tmul(f', g) = T requires f' = tmul(T, g). Is f' ever equal to f?
  f' = tmul(T, g). f = current f. f' = f only if tmul(T, g) = f, which means T = tmul(f, g) — but that's the no-error case. So in every error case, BOTH options require a flip. We always choose the cheaper one.

This simplifies the implementation. When there's an error and f != 0:
1. Compute cost_f = |f_dot|
2. Compute cost_g = |g_dot|
3. If cost_f <= cost_g: flip a contributing W_f weight (push f_dot toward f' = tmul(T, g))
4. Else: flip a contributing W_g weight (push g_dot toward g' = tmul(T, f))

For the f == 0 case (gate held, should have fired):
1. Determine desired f direction: f_desired = tmul(T, g) where g = tsign(g_dot)
2. Flip a W_f weight to push f_dot in the direction of f_desired

For the T == 0 case (should hold but fired):
1. Push f_dot toward zero → flip a W_f weight that contributed to current f_dot direction (same as homeostatic)

Now — what about the flip DIRECTION for g-pathway?

When fixing W_g (option B), we need g' = tmul(T, f). If g' = +1, we need g_dot > 0 → flip a W_g weight that contributed negatively (same logic as the W_f flip, but applied to W_g and g_dot).

The weight-flip logic is identical for both pathways. The only difference is:
- W_f pathway: find weight where tmul(W_f[n][i], concat[i]) agrees with current f_dot direction, flip it
- W_g pathway: find weight where tmul(W_g[n][i], concat[i]) agrees with current g_dot direction, flip it

Wait — but flipping should push the dot TOWARD the desired direction, not just away from the current direction. Let me be precise.

If we want f' = +1 (need f_dot > 0) and current f_dot < 0:
- We need to INCREASE f_dot
- Flip a weight that currently contributes NEGATIVELY to f_dot
- tmul(W_f[n][i], concat[i]) < 0 means this weight is pulling f_dot down
- Flipping W_f[n][i] changes its contribution by +2, increasing f_dot

If we want f' = -1 (need f_dot < 0) and current f_dot > 0:
- We need to DECREASE f_dot
- Flip a weight that currently contributes POSITIVELY
- tmul(W_f[n][i], concat[i]) > 0
- Flipping changes contribution by -2, decreasing f_dot

So the rule is: to push a dot toward direction D:
- If D > 0 (want positive dot): flip a weight with negative contribution
- If D < 0 (want negative dot): flip a weight with positive contribution
- If D = 0 (want zero dot): flip a weight with contribution matching the current dot sign (same as homeostatic)

This is the OPPOSITE of what v1 did. V1 flipped weights that contributed to the CURRENT direction (pushing the dot toward zero). But the goal isn't always toward zero — it's toward the DESIRED direction. Sometimes the dot is positive and should be negative, or the dot is zero and should be positive.

WAIT. Let me re-read v1's logic:

```c
if (f_dot > 0 && contrib > 0) {
    if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
} else if (f_dot < 0 && contrib < 0) {
    if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
}
```

V1 flips weights that contribute in the SAME direction as f_dot. Flipping such a weight pushes f_dot TOWARD ZERO. This is the homeostatic logic: reduce |f_dot|.

But for Hebbian learning, we don't always want f_dot toward zero. We want it toward the DESIRED direction. If the desired direction is opposite to the current, we DO want to reduce current |f_dot| first (flipping same-direction contributors) and then increase it in the opposite direction.

Actually — a single flip changes the dot by ±2. If f_dot = +5 and we want f_dot < 0, flipping one same-direction weight gives f_dot = +3. Still positive. Many flips needed. If instead we flip an OPPOSITE-direction weight (currently contributing negatively), we change it by +2... wait, that increases f_dot further. Wrong.

No — flipping a weight that contributes +1 to f_dot changes its contribution to -1, so f_dot changes by -2. That's the right direction.

V1's logic: "flip a weight whose contribution matches the current f_dot sign" → reduces |f_dot| → correct WHEN we want f_dot closer to zero or reversed.

But the issue is: v1 always applied this to W_f. When the error was in g, it was pointlessly reducing f_dot (the correct pathway) which INCREASED error.

OK I think the diagnosis is the key missing piece, and the flip direction logic within each pathway is actually fine — "flip a weight that contributed to the current dot direction" pushes the dot toward zero, which is the right first move when the dot is in the wrong direction. Multiple flips over multiple steps eventually reverse the dot.

The REAL fix is just: diagnose f vs g, then apply the existing flip logic to the CORRECT pathway.

There's a subtlety about the desired direction though. When flipping W_g, the target g' might be +1 but current g is -1 (g_dot < 0). Flipping a negative-contribution weight pushes g_dot toward zero, then positive. That's correct — multiple steps to reverse. Same logic applies.

But what if the current dot is in the CORRECT direction and we need to flip it to the OPPOSITE? That shouldn't happen — if the dot is in the correct direction, the pathway is correct and we should fix the OTHER pathway. The diagnosis handles this.

Let me also think about a scenario I haven't considered:

What if f = +1 and g = +1 and target = +1? Output = tmul(+1, +1) = +1 = target. No error. No learning. Good.

What if f = +1 and g = +1 and target = -1? Output = +1, wrong.
- Option A: f' = tmul(-1, +1) = -1. Need f_dot < 0. Current f_dot > 0. Flip a positive-contrib W_f weight. Cost: |f_dot|.
- Option B: g' = tmul(-1, +1) = -1. Need g_dot < 0. Current g_dot > 0. Flip a positive-contrib W_g weight. Cost: |g_dot|.
- Pick the cheaper one.

This makes sense. Both pathways are equally "wrong" (both +1, both need to be opposite). Fix the one that's easier to flip.

What if f = +1 and g = -1 and target = +1? Output = -1, wrong.
- Option A: f' = tmul(+1, -1) = -1. Need f_dot < 0. Current f_dot > 0. Fix f. Cost: |f_dot|.
- Option B: g' = tmul(+1, +1) = +1. Need g_dot > 0. Current g_dot < 0. Fix g. Cost: |g_dot|.
- Pick cheaper.

In this case, both are equally "wrong" again. But the cheaper flip is whichever dot is closer to zero.

I think this is solid. Let me summarize and move to NODES.

## Questions Arising

- Is there a case where the diagnosis says "fix W_g" but g_dot = 0? If g_dot = 0, g = 0, and the output would be... wait, g is always computed, g = tsign(g_dot). If g_dot = 0, g = 0, and tmul(f, 0) = 0. So the output would be 0 (since tmul with zero input = zero). But actually the CfC blend doesn't use tmul on g — it uses g directly. Let me re-check the assembly. The blend is: if f > 0, h_new = g (a1). If f < 0, h_new = -g. If f = 0, h_new = h_old.

So if f = +1 and g = 0: h_new = 0 (the zero g). If target = +1: error. But which pathway? g is the problem (it's zero when it should be nonzero). Flip a W_g weight to push g_dot away from zero. The "desired g direction" is: whatever sign we need. Since f = +1 and we want target = +1: g' must be +1 → push g_dot positive → flip a negative-contributing W_g weight.

This case works with the diagnosis.

- What if both |f_dot| and |g_dot| are large? Then both pathways are "hard" to flip. One single weight flip (-2 contribution) on a dot of ±20 barely moves it. Learning is slow. But that's OK — the rate limit means we flip once per 100ms, and over many steps the dot slowly moves. 

- Should we ever flip BOTH f and g in one step? The synthesis said "one flip per neuron per update." If we diagnose g as the problem, we flip one W_g weight. If we diagnose f, one W_f weight. Never both. This is conservative but safe.

## First Instincts

- The diagnosis is simple: when there's an error and f != 0, compare |f_dot| and |g_dot|, fix the cheaper one.
- The flip direction logic from v1 is fine — "flip a weight contributing to the current dot direction" pushes the dot toward zero, which is the correct first step when the dot needs to reverse.
- The g-pathway flip is identical to the f-pathway flip but operates on W_g instead of W_f. Copy the loop, change the weight matrix.
- This should fix the ~50% counterproductive-flip problem that made v1 net-negative.
- The real test is: does this produce a positive contribution under MASK_PATTERN_ID_INPUT=1?
