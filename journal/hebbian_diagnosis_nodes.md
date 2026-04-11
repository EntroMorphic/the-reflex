# Nodes of Interest: Diagnosed Hebbian

## Node 1: The Error Space Has Three Regions
| Region | f state | Error source | Fix target |
|---|---|---|---|
| Gate should hold | f != 0, target = 0 | f too far from zero | W_f (push toward zero) |
| Gate should fire | f = 0, target != 0 | f too close to zero | W_f (push away from zero in direction tmul(target, g)) |
| Gate fired, wrong output | f != 0, target != 0, tmul(f,g) != target | f or g in wrong direction | W_f or W_g (diagnose which, fix cheaper) |

Region 3 is where v1 failed. It always fixed W_f. Roughly half the time the error was in g.

## Node 2: The Cost Heuristic — Fix the Smaller Dot
When both pathways could be fixed (Region 3): fix the one with smaller |dot|. Rationale: each weight flip changes the dot by ±2. A dot of ±3 needs ~1-2 flips to reverse. A dot of ±20 needs ~10. Fix the cheaper one.
Why it matters: This prevents wasted flips on hard-to-move pathways while quickly correcting near-threshold ones.

## Node 3: The Flip Direction Is Already Correct
V1's "flip a weight contributing to the current dot direction" pushes the dot toward zero. For Region 3 (wrong direction), this IS the right first move — you have to pass through zero to reverse. Multiple steps complete the reversal. No change needed in the flip-selection logic, only in WHICH MATRIX it operates on.

## Node 4: The g-Pathway Flip Is Structurally Identical to the f-Pathway Flip
```c
// f-pathway flip (existing):
for (int i = 0; i < LP_CONCAT_DIM; i++) {
    if (lp_W_f[n][i] == T_ZERO || concat[i] == T_ZERO) continue;
    int contrib = tmul(lp_W_f[n][i], concat[i]);
    if ((f_dot > 0 && contrib > 0) || (f_dot < 0 && contrib < 0))
        if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
}
if (best_i >= 0) lp_W_f[n][best_i] = -lp_W_f[n][best_i];

// g-pathway flip (new, identical structure):
for (int i = 0; i < LP_CONCAT_DIM; i++) {
    if (lp_W_g[n][i] == T_ZERO || concat[i] == T_ZERO) continue;
    int contrib = tmul(lp_W_g[n][i], concat[i]);
    if ((g_dot > 0 && contrib > 0) || (g_dot < 0 && contrib < 0))
        if (best_i < 0 || (cfc_rand() % 3) == 0) best_i = i;
}
if (best_i >= 0) lp_W_g[n][best_i] = -lp_W_g[n][best_i];
```

Same loop, different matrix and dot value. Copy-paste with s/W_f/W_g/ and s/f_dot/g_dot/.

## Node 5: W_g Repack Must Also Update LP SRAM
Currently only W_f is repacked after flips. If g-pathway flips occur, W_g must also be repacked:
```c
volatile uint32_t *wg_pos = ulp_addr(&ulp_lp_W_g_pos);
volatile uint32_t *wg_neg = ulp_addr(&ulp_lp_W_g_neg);
for (int n = 0; n < LP_HIDDEN_DIM; n++)
    pack_trits_for_lp(lp_W_g[n], LP_CONCAT_DIM, ...);
```
Track whether any g-flips occurred separately from f-flips. Only repack the pathway(s) that changed.

## Node 6: The Structural Wall Still Holds
W_g updates are on LP W_g, not GIE W_g. The GIE candidate pathway is untouched. TriX accuracy (from GIE W_f) is untouched. The structural wall (W_f hidden = 0) is untouched. All three guarantees survive the g-pathway addition.
