# Blend-Gated Deference — Experiment Specification

**Status:** Specified, not implemented. September 2, 2026.
**Target:** TEST 16 (`test_deference.c`), against the existing harness.
**Prerequisite:** Test 11 (enrollment) — `sig[]` populated, TriX enabled.
**Assembly changes required:** **None.**

---

## 1. Why This Experiment Exists

Stratum 3 claims a five-component architecture for structural epistemic
humility. Components 1–4 are verified. **Component 5 — the evidence-deference
policy — is not.** The audit established why:

- The immediate-deference branch (`n_disagree ≥ 4` → bias = 0) is never entered
  on any clean seed. What runs is a geometric decay, which is a timer, not a
  deference policy.
- The tested implementation (kinetic attention) reaches only 3 of 4 neuron
  groups; group 3 never fires the gate.
- Its measured effect is −5.5 ± 5.3/80 at n=3 — not distinguishable from zero.

But the deeper problem is not statistical. **Gate bias is a poor instantiation
of deference because it routes the prior into the perceptual gate — the one
coupling the architecture exists to forbid.** `THE_PRIOR_AS_VOICE.md` §4
diagnoses "the problem with all systems that let the prior drive the perceptual
apparatus without constraint," and then proposes exactly that, with a constraint.

The structural wall (`W_f[hidden] = 0`) protects *classification*. It does not
protect the *hidden state*, which gate bias modulates and which is the LP layer's
input. Kinetic attention reaches around the wall on the side the wall does not
cover.

This experiment applies deference where the architecture permits it: to the
**retrieval-and-blend path**. The prior's influence stays entirely on the memory
pathway. The perceptual threshold is never touched.

---

## 2. The Mechanism

### 2.1 The knob already exists

`ulp/main.S` gates the VDB→LP blend on a single HP-writable value:

```asm
la      t0, vdb_result_scores
lw      t2, 0(t0)                 /* t2 = best match score */
la      t0, fb_threshold
lw      t3, 0(t0)                 /* t3 = threshold (HP-writable) */
blt     t2, t3, .Lfb_skip         /* score < threshold → skip the whole blend */
```

If the best VDB match scores below `fb_threshold`, the entire blend is skipped
and `lp_hidden` retains whatever the CfC step produced. That is precisely a
deference gate — it is simply never modulated. Every test to date pins it at a
constant 8 (`T14_FB_THRESHOLD`, `T14C_FB_THRESHOLD`).

**Blend-gated deference is therefore a change to one HP-side write.** No
assembly, no ISR change, no new LP command, no change to the perceptual path.

### 2.2 Calibration — why 8 is the wrong constant

From `test_lp_char` output in the committed logs:

| Regime | VDB best-match score | Blend effect |
|---|---|---|
| Established pattern, VDB mature | **26–32** / 48 | 1–5 trits modified |
| Early run, VDB sparse | 9–15 | 4–8 trits modified |
| **Immediately post-switch** (P2 arriving, VDB holds P1) | **9–15** | **8–9 of 16 trits modified** |

The critical row is the third. At the moment the pattern changes — the moment
deference is supposed to engage — the VDB is still returning P1 memories at
scores of 9–15, comfortably above the threshold of 8, and rewriting **more than
half** of the LP hidden state with stale content.

The prior is not a voice at that moment. It is a verdict, and it is loud.

### 2.3 The policy

```
if (lp_argmax == trix_pred)   fb_threshold = THRESH_AGREE   /*  8 — memory flows */
else                          fb_threshold = THRESH_DEFER   /* 20 — memory withheld */
```

`THRESH_DEFER = 20` sits above the post-switch stale band (9–15) and below the
mature-match band (26–32). The consequence:

- **Agreement:** unchanged from today's behaviour. Mature matches (26–32) pass
  easily. The mechanism is inert when there is no conflict.
- **Disagreement:** stale matches (9–15) are rejected. `lp_hidden` follows the
  fresh CfC output — the present signal — rather than retrieved past.
- **Recovery:** as episodes of the *new* pattern accumulate in the VDB, matches
  for it climb past 20 and the blend re-engages on its own.

That last property is the architecturally interesting one. **Deference releases
on accumulating evidence, not on a decay constant.** Kinetic attention's release
was `×0.9/step` — a timer that expires whether or not the world has changed
back. This one re-opens exactly when the memory has something relevant to say.

### 2.4 The disagreement signal (Component 4)

Deference needs "the prior says X, the evidence says Y." Both are available:

- **Evidence:** `trix_pred` — the TriX ISR prediction, in pattern space, mapped
  through `trix_group_to_pattern[]`. Structurally uncorrupted by the prior
  (`W_f[hidden] = 0`), which is what makes the disagreement real information
  rather than a projection.
- **Prior:** `lp_argmax` — which pattern the current LP state most resembles.
  Computed from the per-pattern LP means the harness already accumulates:

```c
/* Which pattern does the LP state currently look like? */
static int lp_argmax_pattern(const int8_t *lp_now,
                             int16_t sum[][LP_HIDDEN_DIM],
                             const int n[])
{
    int best_p = -1, best_d = -9999;
    for (int p = 0; p < NUM_TEMPLATES; p++) {
        if (n[p] < DEF_MIN_SAMPLES) continue;   /* cold-start guard */
        int d = 0;
        for (int j = 0; j < LP_HIDDEN_DIM; j++)
            d += tmul(lp_now[j], tsign(sum[p][j]));
        if (d > best_d) { best_d = d; best_p = p; }
    }
    return best_p;    /* -1 = undecided → treat as agreement (no deference) */
}
```

When `best_p < 0` (no pattern has ≥ `DEF_MIN_SAMPLES` yet) the policy defaults
to `THRESH_AGREE`. A prior that has not formed cannot be deferred to or from.

---

## 3. Conditions

Four conditions, all within one boot, so the comparison is paired.

| # | Name | Blend | `fb_threshold` | Purpose |
|---|---|---|---|---|
| D-A | Baseline | CMD 5 | fixed 8 | Today's behaviour. The VDB-only reference. |
| D-B | **Deference** | CMD 5 | 8 / 20, agreement-gated | The hypothesis. |
| D-C | Fixed-high | CMD 5 | fixed 20 | **Control the kinetic experiment lacked.** |
| D-D | Ablation | CMD 4 | n/a (no blend) | Permanent withholding. Upper bound on harm. |

**D-C is the condition that makes this experiment interpretable.** If D-B and
D-C perform identically, the *gating* contributes nothing and the entire effect
is a blanket threshold raise — a one-line constant change, not a deference
mechanism. Kinetic attention had no equivalent control, which is part of why its
negative result is hard to interpret.

**D-D bounds the failure mode.** If D-B collapses toward D-D, deference has
become de-facto ablation (see §6.1).

---

## 4. Protocol

Reuse the TEST 14C transition structure verbatim — deference only means anything
at a conflict, and 14C is the only protocol that manufactures one.

```
Sender:   TRANSITION_MODE=1     P1 for 90 s, then P2 for 30 s
Receiver: MASK_PATTERN_ID=1  MASK_PATTERN_ID_INPUT=1
          (and MASK_SEQUENCE_INPUT=1 once the B5 ablation has cleared)
Per run:  4 conditions × (90 s P1 + 200 post-switch steps)
```

**Condition order must be counterbalanced.** Audit finding D7: TEST 14 ran
14A → 14C → 14C-iso in that fixed order in every run, so drift over the
6-minute sequence was confounded with condition and never averaged out. Use a
4×4 Latin square across runs, or rotate the starting condition per run and
record the order in the log header.

---

## 5. Endpoints

### 5.1 Primary — post-switch alignment gap

`run_test_14c()` already computes it. Per post-switch step, in MTFP space:

```c
align1 = Σ tmul(lp_mtfp[j], t12_mean1_mtfp[j])   /* alignment to P1 mean */
align2 = Σ tmul(lp_mtfp[j], t12_mean2_mtfp[j])   /* alignment to P2 mean */
gap    = align2 - align1
```

**Primary endpoint: `gap` at post-switch step 30.** Traced over 60 steps and
already printed by the existing harness.

Reference values from `t14c_labelfree_seed_a.log`: ablation regresses to −6 by
step 30; VDB blend maintains separation.

### 5.2 Secondary

- **Crossover step** — first step where `align2 > align1`. Already computed
  (`t14c_crossover[]`). Prediction: D-B earlier than D-A.
- **Steady-state MTFP divergence /80** — directly comparable to the 9.7 ± 0.6
  VDB-only baseline. Confirms deference does not damage the resting
  representation the way gate bias did.
- **Deference duty cycle** — % of confirmations with `fb_threshold ==
  THRESH_DEFER`. **Load-bearing for interpretation, see §6.1.**
- **Blend re-engagement step** — first post-switch step where the blend applies
  again (`ulp_fb_applied == 1`) while `trix_pred == 2`. Tests the
  self-releasing property in §2.3.
- `ulp_fb_score` and `ulp_fb_blend_count` per step — already instrumented.

---

## 6. Risks, and How Each Is Controlled

### 6.1 Deference collapsing into ablation — the sharpest risk

If disagreement persists for the whole 30-second post-switch phase, D-B *is*
D-D during that phase, and 14C already showed ablation regresses.

The hypothesis depends on the withholding being **transient**: withheld during
the switch, restored once new-pattern episodes accumulate. The distinguishing
instrument is the **deference duty cycle** (§5.2).

- Duty cycle near 100% post-switch → the mechanism is ablation with extra steps.
  Report it as such. Do not describe it as deference.
- Duty cycle that spikes at the switch and decays as the VDB fills → the
  mechanism is doing what it claims.

**Pre-register this reading before running.** It is the difference between a
result and a rationalisation.

### 6.2 The threshold band may not generalise

`THRESH_DEFER = 20` is calibrated from `test_lp_char` logs on this board pair
with this VDB fill rate. A different insert cadence or pattern set moves the
score bands. **Re-run `run_lp_char()` and confirm the two bands are still
separated before trusting the constant.** If mature and post-switch bands
overlap, the mechanism has no window to operate in and the experiment should
not be run as specified.

### 6.3 Reduced blend count is not automatically a benefit

Withholding the blend necessarily reduces `fb_blend_count`. That is not evidence
of anything on its own — TEST 13 showed the blend is *causally necessary* for LP
divergence. The endpoint is alignment and divergence, never "fewer trits
overwritten."

### 6.4 The write is racy, benignly

HP writes `ulp_fb_threshold` while the LP core may be mid-blend. Worst case: one
step uses the previous value. Same pattern and same tolerance as
`gie_gate_bias[]`. Do not add a lock; note it in the writeup.

### 6.5 P3 remains untested here

The transition protocol exercises P1→P2 only. Group 3's inertness (audit B3) is
a GIE gate property and does not affect this experiment, but it also means this
experiment says nothing about it. Scope the claim accordingly.

---

## 7. Sample Size — and Why the Design Must Be Paired

Kinetic attention's between-run SD was **5.27/80**, dominated by an unstable
baseline (14A: 9.8, 15.5, 15.5). At that variance, detecting a 1-trit effect at
80% power would need roughly n ≈ 220 runs. **No feasible n rescues a
between-run design here.**

The paired within-boot difference is far tighter — TEST 15's paired rep-to-rep
SD was ≈ 0.55/80. Running all four conditions in one boot removes the
session-level drift term, which is where nearly all the variance lives.

**Procedure:**

1. **Pilot, n = 3.** Estimate the paired SD *of the alignment-gap endpoint*,
   which is not yet characterised. Do not reuse the divergence SD for it.
2. **Compute n** from the pilot: `n = ((t_{α/2,n−1} + t_{β,n−1}) · SD / Δ)²`.
   For the divergence endpoint (SD ≈ 0.55) and a target Δ = 1 trit, n = 5
   detects ≈ 0.91 trits at 80% power.
3. **Report dispersion and n on every figure.** Audit finding A2: one null was
   reported honestly and the other as a finding. Both endpoints get the same
   treatment here, whichever way they come out.

---

## 8. Falsification

State these before the first run.

**The hypothesis is falsified if:**

- D-B's alignment gap at step 30 is not greater than D-A's, across the
  pre-computed n; **or**
- D-B is statistically indistinguishable from D-C (fixed-high) — the gating adds
  nothing over a constant; **or**
- D-B's deference duty cycle post-switch exceeds ~80% — it is ablation, not
  deference (§6.1); **or**
- D-B's steady-state MTFP divergence falls below D-A's — deference damages the
  resting representation, which is the failure mode gate bias exhibited.

**The hypothesis is supported only if** D-B beats D-A on the alignment gap,
beats D-C by a margin attributable to gating, keeps steady-state divergence at
or above D-A, and shows a duty cycle that spikes and then decays.

Anything else is a negative result and gets reported as one. Given that the two
previous downstream mechanisms both came back null, **the prior on this one
should be that it also comes back null** — and that is still worth running,
because Component 5 is currently unverified rather than falsified, and this is
the instantiation the architecture actually licenses.

---

## 9. Implementation Checklist

Against the existing harness. Estimated: one file, ~250 lines, no assembly.

- [ ] `embedded/main/test_deference.c`, `int run_test_16(void)` — clone the
      structure of `run_test_14c()`, which already provides the transition
      protocol, MTFP alignment tracing, crossover detection, and per-condition
      reset.
- [ ] Add to `test_harness.h`: `int run_test_16(void);` and constants
      `DEF_THRESH_AGREE 8`, `DEF_THRESH_DEFER 20`, `DEF_MIN_SAMPLES 15`.
- [ ] Add `"test_deference.c"` to `app_sources` in `main/CMakeLists.txt`;
      add a `SKIP_TO_16` flag mirroring `SKIP_TO_14C`.
- [ ] Per confirmation, after `feed_lp_core()` and **before** the LP command:
      compute `lp_argmax`, compare to `trix_pred`, write `ulp_fb_threshold`.
      Order matters — the threshold must be set before the blend runs.
- [ ] Condition dispatch: `vdb_cfc_pipeline_step()` (CMD 4) for D-D,
      `vdb_cfc_feedback_step()` (CMD 5) otherwise — same shape as
      `T14C_COND_ABLATION`.
- [ ] Instrument: deference duty cycle, blend re-engagement step,
      `ulp_fb_score` / `ulp_fb_blend_count` traces, condition order.
- [ ] **Do not touch** `gate_threshold`, `gie_gate_bias[]`, or anything in the
      ISR. The perceptual path stays untouched — that is the entire point.
- [ ] Log the condition order in the header for the counterbalancing record.

---

## 10. What a Positive Result Would Mean

Component 5 becomes verified, and Stratum 3's five-component architecture stops
resting on an untested link. The claim would be sharper than the one gate bias
was reaching for:

> The prior's influence is confined by construction to the memory pathway. When
> the structurally-uncorrupted evidence-reader disagrees with the prior, the
> memory pathway closes; it reopens when memory has evidence relevant to the
> present, not when a timer expires.

That is "a voice, not a verdict" as an architectural property rather than an
aspiration — and, unlike gate bias, it does not require the prior to touch
perception at all.

A negative result is also worth having, and cheaply: it would say that the
VDB baseline is not merely hard to improve on by learning or by attention, but
hard to improve on by *scheduling* either — which strengthens the Stratum 1
thesis that the episodic memory alone is the finding.

---

*Depends on:* `docs/AUDIT_SEP2026.md` (findings A2, B3, D7 shape this design),
`docs/KINETIC_ATTENTION.md` (the mechanism this replaces),
`papers/stratum3_prior_signal_separation.tex` §Silicon Verification.
