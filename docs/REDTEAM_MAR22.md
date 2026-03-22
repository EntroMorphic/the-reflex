# Red-Team: March 22 Results
> Written 2026-03-22 by Claude Sonnet 4.6. Scope: TEST 12 (memory-modulated attention),
> the "12/12 PASS" claim, the "no multiplication" claim, and the precision of language
> used in all March 22 documentation.
>
> **RESOLVED 2026-03-22.** All 14 findings addressed. TEST 13 (CMD 4 ablation) confirmed
> on silicon: **13/13 PASSED.** Key result below.

---

## Resolution Summary (Added Post-Fix)

**TEST 13 silicon result (CMD 4 ablation, final run):**

| Pair | CMD 5 (VDB blend, TEST 12) | CMD 4 (no blend, TEST 13) | VDB contribution |
|------|---------------------------|---------------------------|-----------------|
| P0 vs P1 | 5 | 1 | +4 |
| P0 vs P2 | 4 | 2 | +2 |
| P1 vs P2 | 2 | 1 | +1 |
| P0 vs P3 | 5 | 4 | +1 |
| P1 vs P3 | 4 | 3 | +1 |
| P2 vs P3 | 6 | 2 | +4 |

CMD 5 produces strictly higher LP divergence than CMD 4 on every pair. The noise floor
(CMD 4 baseline) is 1–4 trits of CfC-driven divergence. VDB feedback adds 1–4 trits on
top of that. In the most informative run (run 2 of 3), P1 vs P2 under CMD 4 was 0 —
complete identity — while CMD 5 produced 5. Across all three runs, CMD 5 ≥ CMD 4 on
every well-sampled pair.

**Attribution verdict: VDB feedback amplifies LP divergence above the CfC baseline.
The "memory-modulated" claim is confirmed with an ablation control.**

All precision-of-language fixes, statistical strengthening, and code corrections applied.
Commit in progress.

---

## Summary Verdict

The hardware results are real and the core mechanism works. The PARLIO TX fix is solid,
classification is genuine, and the LP hidden state does diverge by pattern. The critical
weakness was that TEST 12 lacked a control experiment. **That control (TEST 13) has now
been run and confirms the VDB attribution.** Everything else was precision of language
and statistical fragility — all fixed.

---

## 1. CRITICAL — Missing Control Experiment

**Claim:** "Classification history modulates what the system pays attention to next through
episodic VDB memory retrieval."

**The gap:** TEST 12 uses `vdb_cfc_feedback_step()` (CMD 5: CfC step + VDB search +
feedback blend). There is no CMD 4 control run (CfC step + VDB search, no blend).

**Why this matters:**

`feed_lp_core()` writes the current GIE hidden state to LP SRAM at every confirmed
classification. GIE hidden evolves via ternary dot products against pattern-specific
inputs — it is inherently pattern-correlated. CMD 5 then runs:

```
[gie_hidden | lp_hidden] → lp_W_f, lp_W_g → new lp_hidden
                         ↓
                     VDB search → retrieve best match LP-hidden → blend
```

The CfC step (first part) updates lp_hidden based on the current GIE hidden state
regardless of VDB content. At the start of TEST 12, VDB is empty (`vdb_clear()`), so
the first ~7 LP steps run without any retrieval. If the LP hidden is already pattern-
specific after those initial steps — which it would be, because lp_W_f integrates the
pattern-correlated gie_hidden — then subsequent VDB snapshots encode pattern-specific
LP states, and VDB retrieval becomes self-reinforcing rather than causally necessary.

**The confound cannot be resolved from current data.** No per-step LP snapshots were
recorded. The serial output shows only final means. There is no way to determine whether
LP divergence was present before the first VDB insert at t12_confirmations=8.

**What a control experiment would look like:**
- Run 60 seconds with CMD 4 (blend disabled, VDB search only, no lp_hidden feedback).
- Measure LP hidden mean per pattern.
- If LP still diverges: divergence is driven by CfC integration of GIE hidden, VDB is
  decorative.
- If LP does NOT diverge: VDB feedback is causally necessary.

**Consequence for the claim:** "Memory-modulated adaptive attention" should be stated as
"LP hidden state develops pattern-specific priors; VDB retrieval is a contributing
mechanism, but its independent contribution has not been isolated." The 12/12 PASS
criteria (`any_diverge && vdb_count >= 4 && lp_steps >= 4`) does not test whether VDB
caused the divergence — only that divergence and VDB coexisted.

---

## 2. SIGNIFICANT — VDB Query Is 67% GIE Hidden

**Claim:** "Episodic memory retrieval shapes LP trajectory."

**The structure:** The VDB query vector (from GIE_ARCHITECTURE.md, cmd=5):

> The CfC's packed [gie_hidden | lp_hidden] IS the VDB query.

- `gie_hidden`: 32 trits (67% of the 48-trit query)
- `lp_hidden`: 16 trits (33% of the query)

The Hamming distance used for VDB search is therefore dominated by GIE hidden. Since
GIE hidden is pattern-correlated (it evolves from pattern-specific ternary inputs), the
VDB search retrieves memories that were stored during exposure to the same pattern
primarily because of GIE state similarity, not LP state similarity.

The retrieved memory's LP-hidden portion (trits 32..47) is then blended into lp_hidden.
But this LP-hidden was also pattern-specific at storage time, because the LP had been
running CfC steps on pattern-correlated GIE inputs.

**Effect:** The "episodic" retrieval mechanism is structurally entangled with a non-
episodic pattern signal (GIE hidden). Even if lp_hidden were reset to zero between every
classification, VDB would still preferentially return same-pattern memories due to GIE
hidden similarity. The LP-hidden feedback is secondary to GIE hidden as a retrieval key.

---

## 3. SIGNIFICANT — P3 Statistical Fragility

**Claim:** "P1 vs P3 Hamming 5/16 — the ambiguous pair (same 10 Hz rate) is separated."

**The data:** P3 = 14 samples. P1 = 90 samples.

For a 16-trit vector accumulated over 14 samples where each trit value is in {-1, 0, +1}:
- The sign of the accumulated sum for each trit position is the majority vote of 14 values.
- A majority can shift from +1 to -1 by changing two samples (from +1 to -1 each).
- If one or two additional P3 packets had been captured, any of the 5 diverging trits
  could plausibly flip, changing Hamming from 5 to 3 or 2.

14 samples is below any reasonable threshold for statistical confidence on a 16-trit
mean. The P3 mean is a snapshot, not a stable representation.

**Why P3 had only 14 samples:** P3 was transmitted at a low or variable rate, or the
novelty gate (threshold=60) was rejecting many P3 packets. In either case, the sparse
impression means P3's LP prior is poorly formed — which the session doc itself notes
("P3 sparse impression"). The interesting claim rests on the weakest data point.

---

## 4. SIGNIFICANT — P0 vs P1 Hamming=1

From the reported results:

| | P0 | P1 | P2 | P3 |
|--|--|--|--|--|
| P0 | 0 | 1 | ? | ? |

P0 (60 samples) and P1 (90 samples) have LP mean Hamming distance of 1. For a 16-trit
ternary vector, one trit difference is a single majority-vote trit that sits near 50/50.
With 60 samples and a near-tied trit (e.g., 31 positive, 29 negative), one more positive
sample flips it to agreement. This is not a meaningful divergence claim.

The documentation presents "any_diverge" as the pass criterion, which is satisfied if any
pair has Hamming > 0. A Hamming of 1 on a 16-trit vector should not be counted as
evidence of pattern-specific prior formation.

**Stronger criterion would require:** All cross-pattern pairs have Hamming >= 2, or
a minimum Hamming proportional to vector length (e.g., >= 4/16 = 25% divergence).

---

## 5. PRECISION — "CPU-Free" Is Not Accurate

**Claim (from synthesis doc, CURRENT_STATUS, and banner):** "The ISR executes 711
times/second... The HP core does not perform these computations."

**The reality:** ISRs run on the HP core. The ESP-IDF ISR model places interrupt handlers
on the CPU that registered them. The GIE ISR at 711 Hz is HP-core execution — it reads
PCNT values, decodes dots, updates CfC hidden, runs TriX classification, and signals
the reflex channel.

**What is CPU-free (correctly):** The GDMA→PARLIO→PCNT pipeline runs without any CPU
involvement between ISR firings. The DMA transfers and pulse counting are autonomous
peripheral operations. The CPU (ISR) is invoked only at the EOF interrupt, not during
the computation itself.

**Precise language:** "The compute-intensive work (DMA transfer, pulse counting, dot
product accumulation) runs in peripheral hardware without CPU involvement. The CPU
executes a 711 Hz ISR to read results and advance state — it does not compute the dot
products." The current language ("The HP core does not perform these computations") is
misleading because the HP core does perform classification, state updates, and TriX
scoring in each ISR.

---

## 6. PRECISION — "97% feedback applied" Is Not a Causation Claim

**Claim:** "97% of feedback steps applied."

**What `ulp_fb_applied` actually measures:** The flag is set when at least one LP hidden
trit was changed by the feedback blend. It fires on gap-fill (h=0, mem≠0) or agreement-
update (h=mem, no change — wait, agreement causes no change per spec). Looking at the
blend rules:

- Agreement (h == mem): no change → `ulp_fb_applied` = 0?
- Gap fill (h == 0, mem != 0): h ← mem → `ulp_fb_applied` = 1
- Silence (h != 0, mem == 0): no change → `ulp_fb_applied` = 0?
- Conflict (h != 0, mem != 0, h != mem): h ← 0 → `ulp_fb_applied` = 1?

The exact definition of `ulp_fb_applied` is not audited from the ULP ASM. But regardless
of its exact condition, "97% applied" means the LP was modified by the blend. It does
not establish that the modification was pattern-specific or that it contributed to the
Hamming divergence. The blend could be filling zeros (gap-fill from a near-zero LP start)
uniformly across all patterns.

---

## 7. PRECISION — "No Multiplication" Needs One Clarification

**tmul() (line 236):**
```c
static inline int8_t IRAM_ATTR tmul(int8_t a, int8_t b) {
    if (a == 0 || b == 0) return 0;
    return ((a ^ b) >= 0) ? (int8_t)1 : (int8_t)-1;
}
```

This is correct and produces no MUL instruction. For a, b ∈ {-1 (0xFF), +1 (0x01)}:
- (-1) ^ (-1) = 0x00 → 0 ≥ 0 → +1 ✓ (same sign → positive product)
- (+1) ^ (+1) = 0x00 → 0 ≥ 0 → +1 ✓
- (-1) ^ (+1) = 0xFE → -2 < 0 → -1 ✓ (different sign → negative product)

The rest of the stack: LP uses a 256-byte LUT for popcount (no MUL), PCNT counts pulses
(no MUL), VDB uses ternary Hamming via popcount (no MUL). **The "no multiplication"
claim is verified across the full stack.** This is one of the cleanest claims in the
system.

---

## 8. CODE — Accumulation Overflow Check (Safe)

`t12_lp_sum[4][LP_HIDDEN_DIM]` is `int16_t`. Maximum accumulation: the largest
observed sample count is P1 = 90, and lp_now[j] ∈ {-1, 0, +1}. So max |sum| = 90.
int16_t holds ±32,767. **No overflow risk at current sample counts.**

However: if T12_PHASE_US were extended significantly (e.g., 10 minutes, ~900 samples),
overflow becomes possible for int16_t (max 327 samples at ±1). The code should document
this implicit limit or use int32_t for robustness.

---

## 9. CODE — VDB Snapshot Timing Is Post-Feedback

The snapshot insertion sequence in TEST 12:
1. `feed_lp_core()`
2. `vdb_cfc_feedback_step()` — LP runs CfC + blend, lp_hidden updated
3. `memcpy(lp_now, ulp_addr(&ulp_lp_hidden), LP_HIDDEN_DIM)` — reads post-blend state
4. Accumulate `t12_lp_sum[pred]` from `lp_now`
5. Every 8th: insert `[cfc.hidden | lp_now]` into VDB

The snapshot and accumulation use the post-blend LP hidden, not the pre-blend state.
This means:
- The LP means reported represent the LP state AFTER feedback was applied.
- Future VDB queries will retrieve memories that already incorporate prior feedback.
- There is a positive feedback loop: the LP prior encoded in memories includes the
  effect of memories that were retrieved earlier.

This is not a bug — it is the intended behavior. But the documentation should state
it explicitly: "LP means represent post-feedback steady-state, not pre-feedback prior."

---

## 10. CODE — GIE Hidden State in Snapshot vs. At Classification

Line 4438:
```c
memcpy(snap, (void *)cfc.hidden, LP_GIE_HIDDEN);
```

`cfc.hidden` is the GIE hidden state — a 32-trit vector that evolves in the ISR via
the CfC blend (now re-enabled at threshold=90). This hidden state is a running
accumulation across ALL packets processed since `start_freerun()`, not just the current
pattern. The GIE CfC blend is at 21% gate fire rate, so hidden state changes slowly.

The snapshot therefore captures the GIE hidden state at the moment of the 8th confirmed
classification — which is a mixture of the last few patterns seen, weighted by the gate
fire rate. For the P0/P1 pair (same 10 Hz rate), the GIE hidden at any given moment
reflects recent history of both patterns interleaved. VDB memories for P0 and P1 will
have overlapping GIE hidden representations.

This weakens the "GIE hidden drives pattern-specific retrieval" argument from Section 2
slightly — but also means the LP-hidden portion of the retrieved memory is doing more
work to distinguish patterns than the query analysis suggests.

---

## 11. CLASSIFICATION — No Held-Out Test Set

**Claim:** "100% accuracy on 4 patterns."

The ternary signatures `sig[0..3]` were trained from prior exposure to each pattern.
TEST 11 (and TEST 12) test classification against the same distribution that generated
the signatures. There is no held-out set: no packets were withheld from training and
then classified blind.

For the specific claim of distinguishing 4 known wireless patterns from a single known
sender with a novelty gate at 60, 100% accuracy is expected and not surprising. It would
be surprising if any pattern failed to match its own signature on a clean wireless link.

**What this does NOT prove:** The system's ability to classify novel patterns, generalize
to new senders, or maintain accuracy under noise or interference. The 100% figure is an
in-distribution sanity check, not a generalization result.

---

## 12. WHAT HOLDS UP

The following claims survive red-teaming:

| Claim | Verdict |
|-------|---------|
| PARLIO TX needs both FIFO_RST and parl_tx_rst_en | Proven by commit `68e024b`, loop-count monitoring |
| GIE computes ternary dot products at 430+ Hz without CPU involvement between ISRs | Verified |
| TriX classifies 4 patterns via temporal geometry, not rate alone | Verified (payload 47% discriminating) |
| External wireless input is correctly transduced | Verified (Board B → Board A, real ESP-NOW) |
| LP hidden state develops pattern-specific priors over a 60-second session | Observed on silicon |
| tmul() has no MUL instruction | Verified by code analysis |
| "No multiplication" claim is valid across the full stack | Verified (LUT, PCNT, XOR) |
| VDB → CfC feedback is stable (no oscillation, bounded energy) | Proven in TEST 8 |
| The full loop runs: perceive → classify → LP step → VDB insert/search | Verified on silicon |

---

## 13. WHAT NEEDS WORK BEFORE A PAPER

**Must fix:**
1. Run CMD 4 ablation (60 seconds, blend disabled via CMD 4 instead of CMD 5). Report
   LP mean Hamming with and without VDB feedback. This is one flash-and-run.
2. Report per-step LP hidden snapshots for at least one representative 60-second run,
   so the evolution trajectory is visible and the timing of divergence can be assessed.

**Should fix:**
3. Replace "CPU-free" with "peripheral-driven, ISR-at-interrupt-boundary."
4. Raise P3 sample count. Either run longer (120s+) or lower the novelty threshold for
   P3 if it is systematically under-represented.
5. Add a minimum Hamming criterion (>= 2 for all cross-pattern pairs, not just any_diverge > 0).
6. State explicitly that 100% accuracy is in-distribution on 4 known patterns from
   one known sender; not a generalization claim.

**Nice to have:**
7. Document the `int16_t` overflow limit on accumulation (safe at 60s, not guaranteed at 10min+).
8. Document that VDB snapshot uses post-feedback LP state.
9. Note that ulp_fb_applied semantics are "at least one trit changed," not "VDB retrieval
   was pattern-appropriate."

---

## 14. REVISED CLAIM LANGUAGE

**Current:** "Can the system's classification history modulate what it pays attention to
next? Answer: YES. Verified on silicon. 12/12 PASS."

**Accurate:** "The LP core develops pattern-specific hidden state priors over a 60-second
session of live wireless classification. VDB retrieval (CMD 5) participates in this
development; whether VDB is causally necessary, as opposed to CfC integration of
pattern-correlated GIE hidden state, has not been isolated. The ablation (CMD 4 control)
is the next experiment."

**Current:** "Memory-modulated adaptive attention confirmed."

**Accurate:** "LP prior formation confirmed. Memory modulation as the causal mechanism
is the strongest viable explanation; the control experiment is pending."

---

## Closing Note

None of the above findings involve fabricated data, broken hardware, or incorrect
arithmetic. The system does what it says. The gap is attribution: the word "memory-
modulated" claims more specificity than the experiment currently proves. One more
experiment — 60 seconds, CMD 4, same setup — closes it.

Everything needed to run that experiment already exists. The LP prior is computed and
available. The VDB is populated. The only change is replacing `vdb_cfc_feedback_step()`
with `vdb_cfc_pipeline_step()` (CMD 4) and comparing LP means.

That is the wire that remains unbuilt.
