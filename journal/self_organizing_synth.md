# Synthesis: Self-Organizing Representation — Engineering Specification

## The Clean Cut

Learn in the LP core. Leave the GIE frozen. The structural wall stays.

---

## Architecture Decision: LP Hebbian, Not GIE Hebbian

The ROADMAP's Pillar 3 proposed updating GIE W_f weights via VDB mismatch. **This is wrong.** GIE W_f holds the TriX classification signatures. Modifying W_f breaks the structural guarantee (W_f hidden = 0, 100% label-free accuracy). The prior-signal separation — the project's core principle — depends on that wall.

**Corrected design:** Hebbian updates apply to LP core weights (W_f_lp, W_g_lp) only. These are architecturally separate from the TriX classifier. Learning improves the temporal context extraction, which improves gate bias quality, which improves GIE selectivity — without ever touching the classification weights.

**What this preserves:** TriX 100% accuracy, W_f hidden = 0 guarantee, prior-signal separation.

**What this changes:** LP hidden states become experience-dependent not just from VDB feedback blend (CMD 5) but from learned weight projections. The random-weight Seed B headwind is fixable because the LP projection axis learns to separate whatever patterns the VDB consistently retrieves.

---

## Implementation: CMD 6 (Hebbian Weight Update)

### Data Flow

```
CMD 5 (existing):
  CfC step → VDB search → retrieve best match → blend into lp_hidden
  NEW: store per-trit error mask (retrieved_lp XOR current_lp)

CMD 6 (new):
  For each LP neuron n (0..15):
    Load error_mask[n_trit]  — did this output trit disagree with VDB?
    If no error for this neuron's output → skip (already correct)
    Find the input trit i whose weight contributed most to the error
    Flip lp_W_f[n][i] (pos↔neg in the packed bitmask)
    Repack the affected word
  Write lp_data_ready flag
```

### Error Signal Extraction (CMD 5 modification)

Add to CMD 5, after the VDB search returns the best match:

```asm
# retrieved_lp is in the best-match node at offset 32..47
# current lp_hidden is at lp_hidden (16 bytes)
# XOR the two to get per-trit disagreement

lw   t0, 0(best_node + 32)    # retrieved LP packed trits, words 0-3
lw   t1, 0(lp_hidden)          # current LP packed trits
xor  t2, t0, t1                # disagreement mask
sw   t2, 0(lp_error_mask)      # store for CMD 6
# repeat for remaining words
```

New LP SRAM variable: `lp_error_mask` — 16 bytes (4 words). Cost: 16 bytes of BSS.

### Hebbian Update Rule (CMD 6 core loop)

For each LP neuron n:

1. Check if output trit `lp_hidden[n]` disagrees with `retrieved_lp[n]` (single bit check from error mask)
2. If no disagreement → skip neuron (correct output, no update needed)
3. Compute `f_dot_lp[n]` sign: this is the gate direction the neuron chose
4. For each input trit i (0..47):
   - Compute `contrib = W_f_lp[n][i] * concat[i]` (ternary: same-sign = +1, diff-sign = -1)
   - If contrib has the same sign as f_dot (contributed to the error direction) → candidate for flip
5. Among candidates, select the first (or pseudo-random among first K)
6. Flip: in the packed bitmask, swap the corresponding bit between `lp_W_f_pos[n]` and `lp_W_f_neg[n]`

**Operations used:** lw, sw, and, xor, srl, sll, bnez, beq. No multiply. No float. Exact same operation set as CfC and VDB.

**Estimated code size:** ~200-300 bytes (16 neurons × ~15 instructions per neuron + loop overhead + error mask load).

**Estimated execution time:** ~100µs at 16 MHz. Well within the 10ms wake budget.

### Memory Budget Impact

| New item | Size | Source |
|---|---|---|
| CMD 6 code (.text) | ~300 B | Heap toward stack |
| `lp_error_mask` (.bss) | 16 B | BSS |
| `lp_update_count` (.bss) | 4 B | Diagnostic counter |
| **Total** | **~320 B** | **Free space: 4,400 → 4,080 B** |

No budget crisis. Stack peak (608B for VDB search) still fits with margin.

---

## Safety Architecture

### Gate 1: Retrieval Stability (K-of-N)

Only issue CMD 6 when the same VDB top-1 node has been retrieved for K consecutive CMD 5 calls. This filters transient noise and transition periods.

**Proposed K=5** (50ms). In 5 seconds of pattern exposure, ~100 CMD 5 calls occur, ~20 trigger CMD 6 (every 5th is the start of a new K-window). Each trigger flips at most 16 weights (one per neuron). Total: ~320 weight flips per 5-second pattern exposure.

**HP-side implementation:** Track `last_vdb_top1` and `stable_count` in the HP core's feedback loop. When `stable_count >= K`, issue CMD 6 instead of CMD 5. Reset `stable_count` when top-1 changes.

### Gate 2: TriX Agreement

Only issue CMD 6 when `trix_pred == pattern_of(vdb_top1)`. If the TriX classifier and the VDB retrieval agree on the current pattern, the error signal is trustworthy. If they disagree, the system is in an ambiguous state — suppress learning.

**Why this works:** TriX is 100% accurate (structural guarantee). Using it as a filter for the LP learning signal means the learning signal inherits the structural guarantee. The prior (LP weights) is trained only when the measurement (TriX classification) is unambiguous. This is the prior-signal separation principle applied to the learning rule itself.

### Gate 3: Rate Limiting

Maximum one weight flip per neuron per N wake cycles. Prevents runaway modification during pathological input sequences.

**Proposed N=10** (100ms). Combined with Gate 1 (K=5), effective update rate is: one flip per neuron per max(50ms, 100ms) = 100ms = 10 updates/second/neuron. At 48 weights per neuron, full weight rotation takes ~4.8 seconds. This is fast enough to adapt within a single pattern exposure (5s in cycling mode) but slow enough to be conservative.

### Gate 4: VDB Pruning (Pillar 1 coupling)

Pillar 1 (dynamic scaffolding) must run alongside Pillar 3. When LP weights update, old VDB nodes become stale (their LP portion was computed under old weights). Pillar 1 prunes nodes whose LP portion is within Hamming 1 of the current LP mean — which will naturally target stale nodes after weight updates (they cluster at the old projection point while new insertions cluster at the new one).

**Coupling mechanism:** No explicit coupling needed. Pillar 1's pruning criterion (LP-mean Hamming distance) inherently prefers to evict stale nodes because they have the highest Hamming from the current LP state. The two pillars are self-synchronizing.

---

## Test Plan: TEST 15 (Hebbian LP Learning)

### TEST 15A: Convergence Under Fixed Input

1. Build with a fixed seed (e.g., 0xDEAD5678 — the "headwind" seed)
2. Run the cycling sender (4 patterns, 5s each)
3. Run CMD 5 for 60s (build VDB and LP baseline)
4. Enable CMD 6 (Hebbian updates) for 120s
5. Measure: LP divergence matrix before and after learning

**Pass criterion:** LP P1-P2 sign-space Hamming increases from ≤1 (random projection) to ≥2 (learned separation) without regression on other pairs.

### TEST 15B: No-Learning Ablation

Same as 15A but with CMD 6 disabled (CMD 5 only). Confirms that any improvement is from weight learning, not from VDB accumulation alone.

### TEST 15C: Classification Integrity

After 120s of CMD 6, verify TriX classification accuracy is still 100% (label-free). The structural wall must hold — learning in the LP core must not affect GIE classification.

### TEST 15D: Robustness Under Pattern Switch

1. Run CMD 6 for 60s on P1
2. Switch to P2 (cycling or transition sender)
3. Measure: does the system adapt to P2 without catastrophic forgetting of P1?

**Pass criterion:** LP divergence for P2 improves within 30s. LP divergence for P1 doesn't degrade by more than 1 Hamming point.

---

## Corrected Dependency Graph

```
Phase 5: Kinetic Attention (DONE)
    │
    ├── Pillar 1 + Pillar 3 (CO-DEVELOP)
    │       LP Hebbian learning + VDB pruning
    │       (pruning keeps VDB current with learned representation)
    │
    └── Pillar 2: SAMA (AFTER Pillar 1+3)
            (multi-agent requires stable learned representations)
```

Pillar 1 and Pillar 3 are no longer sequential. They are coupled systems that need to be developed and validated together.

---

## What Surprised Me

The ROADMAP's Pillar 3 description was updating the wrong weights. GIE W_f IS the TriX classifier. Modifying it for Hebbian learning would erase the structural guarantee that the project's entire epistemic framing depends on. The fact that this wasn't caught until now suggests the ROADMAP was written before the structural wall's importance was fully internalized.

The correct locus of learning — the LP core — was hiding in plain sight. The LP already has its own weight matrices, its own CfC, its own data pathway. It's architecturally isolated from the classifier. And the signal it needs (VDB mismatch) is already being computed every 10ms. The infrastructure for self-organizing representation is already built. The missing piece is ~300 bytes of assembly and two safety gates.

The wood was already cut. We just needed to see the grain.

---

*Lincoln Manifold Method deployed on the engineering of self-organizing representation. April 11, 2026. The first chop revealed a fundamental architectural tension (classification guarantee vs. learning). The grain showed that learning belongs in the LP temporal context layer, not the GIE perceptual layer. The sharpening resolved the cascade problem (couple Pillar 1 pruning with Pillar 3 learning). The clean cut: CMD 6, ~300 bytes of assembly, two safety gates, and the structural wall stays.*
