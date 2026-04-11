# Synthesis: Diagnosed Hebbian — Fix the Right Pathway

## The Clean Cut

Three changes to `lp_hebbian_step()`:
1. Read g_dot alongside f_dot
2. Diagnose f vs g per neuron
3. Flip the correct pathway, with direction-aware selection

---

## Per-Neuron Decision Tree

```
Given: target[n], lp_h[n], f_dot[n], g_dot[n], f = tsign(f_dot), g = tsign(g_dot)

If target == lp_h → skip (no error)
If target == 0    → skip (accumulator undecided, not actionable)

If f == 0:
    # Region 2: gate held, should have fired
    desired_f = tmul(target, g)   # what f needs to be, given current g
    → flip W_f, push direction = sign of desired_f
    → select weight with contribution OPPOSITE to desired_f
    
Else:
    # Region 3: gate fired, output wrong
    # Both f and g could be fixed. Pick the cheaper one.
    if |f_dot| <= |g_dot|:
        → flip W_f, push f_dot toward zero
        → select weight with contribution SAME as sign(f_dot)
    else:
        → flip W_g, push g_dot toward zero
        → select weight with contribution SAME as sign(g_dot)
```

## Weight Selection (unified for both pathways)

```c
/* Select a weight to flip in matrix W (either W_f or W_g).
 * push_toward_zero = 1: flip same-direction contributor (push dot→0)
 * push_toward_zero = 0: flip opposite-direction contributor (push dot away from 0)
 * desired_sign: the direction to push when push_toward_zero = 0 */

int select_flip(int8_t W[][LP_CONCAT_DIM], int n,
                const int8_t *concat, int dot, int push_toward_zero,
                int desired_sign) {
    int best_i = -1;
    for (int i = 0; i < LP_CONCAT_DIM; i++) {
        if (W[n][i] == T_ZERO || concat[i] == T_ZERO) continue;
        int contrib = tmul(W[n][i], concat[i]);
        
        int want;
        if (push_toward_zero) {
            /* Flip same-direction to push dot toward zero */
            want = (dot > 0) ? 1 : -1;
        } else {
            /* Flip opposite-direction to push dot toward desired_sign */
            want = -desired_sign;
        }
        
        if (contrib == want) {
            if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
        }
    }
    return best_i;
}
```

Wait — I need to be more careful. "Flip opposite-direction contributor" means: flip a weight whose contribution is OPPOSITE to where I want the dot to go. That's confusing. Let me re-derive.

To push f_dot POSITIVE (increase it): flip a weight with contribution -1 → becomes +1 → f_dot += 2. So I want to find contrib = -1. That's "select weight with contribution = -desired_sign" where desired_sign = +1.

To push f_dot NEGATIVE: flip contrib = +1 → becomes -1 → f_dot -= 2. Select contrib = -desired_sign where desired_sign = -1. contrib = +1. ✓.

To push f_dot toward zero when f_dot > 0: flip contrib = +1 → f_dot -= 2. Select contrib matching sign(f_dot). ✓ (v1 logic).

Unified: **select a weight whose contribution we want to NEGATE.**
- Push toward zero: negate a same-direction contributor
- Push toward desired_sign: negate a -desired_sign contributor

The selection criterion is: `contrib == select_target` where:
- `select_target = sign(dot)` if pushing toward zero (Region 1, 3)
- `select_target = -desired_sign` if pushing away from zero (Region 2)

Actually let me just collapse this:
- Region 2 (f_dot=0, want desired_f): select contrib == -desired_f, flip it
- Region 3 (f_dot wrong): select contrib == sign(f_dot), flip it
- g-pathway (Region 3, cheaper): select contrib == sign(g_dot), flip it

These are each one line of selection logic. No need for a unified helper.

## Implementation Pseudocode

```c
int lp_hebbian_step(void) {
    // 1. Get target from TriX accumulator [unchanged]
    // 2. Read lp_h, f_dots, g_dots, concat [add g_dots]
    
    int f_flips = 0, g_flips = 0;
    
    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        if (target[n] == lp_h[n]) continue;
        if (target[n] == T_ZERO) continue;
        
        int f_dot = dots_f[n], g_dot = dots_g[n];
        int f = tsign(f_dot), g = tsign(g_dot);
        int best_i = -1;
        
        if (f == 0) {
            // Region 2: gate held, should fire
            int desired_f = tmul(target[n], g);
            // Push f_dot toward desired_f sign
            // Select contrib OPPOSITE to desired_f → flipping it pushes dot toward desired
            int want = (desired_f > 0) ? -1 : 1;  // want to flip a -1 contrib to push positive, etc.
            for (int i = 0; i < LP_CONCAT_DIM; i++) {
                if (lp_W_f[n][i] == T_ZERO || concat[i] == T_ZERO) continue;
                if (tmul(lp_W_f[n][i], concat[i]) == want)
                    if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
            }
            if (best_i >= 0) { lp_W_f[n][best_i] = -lp_W_f[n][best_i]; f_flips++; }
            
        } else {
            // Region 3: gate fired, output wrong
            // Diagnose: fix f or g? Pick smaller |dot|.
            if (abs(f_dot) <= abs(g_dot)) {
                // Fix f: push f_dot toward zero
                int want = (f_dot > 0) ? 1 : -1;
                for (int i = 0; i < LP_CONCAT_DIM; i++) {
                    if (lp_W_f[n][i] == T_ZERO || concat[i] == T_ZERO) continue;
                    if (tmul(lp_W_f[n][i], concat[i]) == want)
                        if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
                }
                if (best_i >= 0) { lp_W_f[n][best_i] = -lp_W_f[n][best_i]; f_flips++; }
            } else {
                // Fix g: push g_dot toward zero
                int want = (g_dot > 0) ? 1 : -1;
                for (int i = 0; i < LP_CONCAT_DIM; i++) {
                    if (lp_W_g[n][i] == T_ZERO || concat[i] == T_ZERO) continue;
                    if (tmul(lp_W_g[n][i], concat[i]) == want)
                        if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
                }
                if (best_i >= 0) { lp_W_g[n][best_i] = -lp_W_g[n][best_i]; g_flips++; }
            }
        }
    }
    
    // Repack changed matrices
    if (f_flips > 0) repack_lp_wf();
    if (g_flips > 0) repack_lp_wg();
    
    return f_flips + g_flips;
}
```

## What Surprised Me

The Region 2 fix (f_dot = 0, gate should fire) needs a DIFFERENT selection criterion than Region 3 (f_dot wrong, gate fired). Region 2 selects the OPPOSITE-direction contributor (to push dot away from zero). Region 3 selects the SAME-direction contributor (to push dot toward zero). V1 used the same selection for all cases, which was correct for Region 3 but random for Region 2.

The per-region selection:
- **Region 2 (dot at zero, want nonzero):** select `contrib = -desired_sign` → flip adds +2 or -2 in the desired direction
- **Region 3 (dot wrong direction):** select `contrib = sign(current_dot)` → flip adds -2 toward zero

Both are deterministic, both are direction-aware, both operate on the diagnosed pathway only.

## Success Criteria

Run TEST 15 under `MASK_PATTERN_ID=1 + MASK_PATTERN_ID_INPUT=1`:
- Hebbian contribution > 0 (net positive, label-free)
- Control ≈ 3.3/16 (reproduces baseline)
- If contribution > +1.0: the diagnosis fixed the mechanism
- If contribution ≈ 0: the flip rate needs tuning (fewer flips)
- If contribution < 0: deeper issue in the learning rule

---

*The grain was the diagnosis. The missing atomic was asking "f or g?" before swinging. The wood was already scored from two previous attempts — the first cut (VDB mismatch) showed label dependency, the second (TriX accumulator) showed pathway confusion. This third cut has the right target AND the right pathway. If the wood doesn't yield now, the limitation is in the grain itself.*
