# Synthesis: TriX-Output-Based Hebbian Learning

## The Clean Cut

Same flip rule. Different target. The target comes from the structurally guaranteed classifier, not from the label-infected memory.

---

## Specification

### Data Structures (HP-side BSS)

```c
int16_t lp_hebbian_accum[NUM_TEMPLATES][LP_HIDDEN_DIM];  /* 128 bytes */
int     lp_hebbian_accum_n[NUM_TEMPLATES];                /* 16 bytes  */
#define HEBBIAN_ACCUM_MIN  50   /* minimum samples before learning */
```

### Accumulation (every CMD 5 step with TriX confirmation)

```c
int pred = trix_pred;
if (pred >= 0 && pred < NUM_TEMPLATES) {
    int8_t lp_now[LP_HIDDEN_DIM];
    memcpy(lp_now, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM);
    for (int j = 0; j < LP_HIDDEN_DIM; j++)
        lp_hebbian_accum[pred][j] += lp_now[j];
    lp_hebbian_accum_n[pred]++;
}
```

### Modified lp_hebbian_step()

Replace the VDB target extraction:

```c
/* OLD: target from VDB best match (label-dependent) */
// int source_id = (int)ulp_fb_source_id;
// uint32_t target_pos = nodes[node_word_off + 2];
// ...decode 16 trits from VDB node...

/* NEW: target from TriX-labeled accumulator (label-free) */
int pred = trix_pred;
if (pred < 0 || pred >= NUM_TEMPLATES) return 0;
if (lp_hebbian_accum_n[pred] < HEBBIAN_ACCUM_MIN) return 0;

int8_t target[LP_HIDDEN_DIM];
for (int i = 0; i < LP_HIDDEN_DIM; i++)
    target[i] = tsign(lp_hebbian_accum[pred][i]);
```

Everything downstream (error comparison, weight flip selection, repack) is unchanged.

### Gating (unchanged from v1, plus accumulator depth)

1. **Retrieval stability:** same VDB top-1 for K=5 consecutive CMD 5s
2. **TriX agreement:** `trix_pred == ground_truth` (for test validation; in deployment, only TriX is available)
3. **Rate limit:** 100ms between updates
4. **Accumulator depth:** `lp_hebbian_accum_n[pred] >= 50` (NEW)

### Test Protocol

Run TEST 15 with `-DMASK_PATTERN_ID=1 -DMASK_PATTERN_ID_INPUT=1`:
- Phase A (60s): Baseline accumulation + Hebbian accumulator warm-up
- Phase B (90s): Treatment (Control or Hebbian)
- Phase C (30s): Post-treatment measurement

Compare: `Hebbian post mean - Control post mean` = genuine label-free Hebbian contribution.

### Success Criteria

- Hebbian contribution > 0 (learning helps, not hurts)
- Control post mean ≈ 3.3/16 (reproduces the H2 baseline)
- Classification accuracy remains 100% post-learning (structural wall intact)

### Failure Diagnostics

| Outcome | Interpretation | Next step |
|---|---|---|
| Contribution > +1.0 | Learning works label-free. Ship it. | Replicate (H3), then integrate. |
| Contribution +0.1 to +1.0 | Learning helps weakly. Accumulator or gate may need tuning. | Check per-pair: which pairs improved, which degraded? |
| Contribution ≈ 0 | Learning has no effect. The f-pathway flips are not moving weights in useful directions. | Try g-pathway updates (M2 from red-team). |
| Contribution < 0 | Learning hurts again. The target itself is wrong — even the population mean doesn't point toward a useful LP state. | Fundamental LP projection limitation. Need wider LP or different architecture. |

---

## What Surprised Me

The synthesis is small. The change is three code blocks: one accumulator update, one target extraction swap, one gate condition. The infrastructure (flip rule, gating, test design) is already built and validated. The LMM found no new tensions — all the tensions were in the previous VDB-mismatch design, and the TriX-accumulator target resolves them.

The wood was already cut. We just needed to change which tree we were aiming at.

---

*Lincoln Manifold Method deployed on TriX-output-based Hebbian learning. April 11, 2026. The grain was clear from the start: same mechanism, different target. The sharpening confirmed the structural guarantee extends to the learning loop. The clean cut is three code blocks and a build flag.*
