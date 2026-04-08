# Lincoln Manifold: April 8 Findings — NODES

*Phase 2. Grain identification. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## Node 1: Three Bugs Compounded Into One Symptom

CPU contamination (80% classifier) + hardcoded P1 accumulator + float agreement collapse = transition headwind of 7-18 steps. Each bug alone might have been tolerable. Together they created a systematic failure that looked like a design parameter problem (decay rate too slow) rather than implementation errors.

**Why it matters:** The compounding was invisible because the symptom (slow transition) had an obvious alternative explanation (0.9 decay rate). The red-team didn't flag these three specific bugs — it flagged the CPU classifier accuracy and the multi-seed gap. The bugs surfaced from following the data across seeds, not from code review.

**Tension with Node 5:** If the bugs were invisible in the results, were they actually bugs? The system passed 14/14. The papers were honest about the data. The bugs only matter if the agreement mechanism matters — and the no-bias condition (which bypasses agreement entirely) was always the cleaner result.

---

## Node 2: Leaving the Ternary Domain Loses Structure

Two independent instances of the same failure pattern:

| Left ternary | Lost | Resolution |
|---|---|---|
| sign() on LP dots | Magnitude (P1-P2 have same sign, different magnitude) | MTFP 5-trit encoding |
| float on agreement | Per-trit structure (agree vs disagree vs gap) | Ternary disagree-count |

In both cases, the lossy compression discarded exactly the information needed for the task. sign() discarded the information needed to separate P1 from P2. Float agreement discarded the information needed to detect conflict vs uncertainty.

**Why it matters:** This is not a coincidence. The system is ternary. Its signals carry information in the ternary structure — the difference between +1 and -1, the meaning of 0 as "no opinion." Every computation that collapses this structure is a potential information bottleneck.

**Dependency on Node 4:** The ternary-preservation principle only helps for signals where the ternary structure carries decision-relevant information. Not all computations in the system benefit from staying ternary. The GIE fire count (an integer), the VDB node count (an integer), the pattern cycle timing (microseconds) are all legitimately non-ternary.

---

## Node 3: Mechanism Failures vs Projection Failures Require Different Fixes

| | Mechanism failure | Projection failure |
|---|---|---|
| Symptoms | Bias doesn't release at transition | Bias doesn't release at transition |
| Seeds affected | A, C (good projections) | B (degenerate projection) |
| Root cause | Agreement signal is broken | LP weights can't separate P1/P2 |
| Fix | TriX dispatch + ternary agreement | Hebbian learning (Pillar 3) |
| Fixed by today's work | Yes | No |

**Why it matters:** Same symptom, different cause. If we had only tested Seed B, we would have concluded "the agreement mechanism doesn't work." If we had only tested Seed A, we would have concluded "it works perfectly." Three seeds revealed both failure modes simultaneously, which is what made diagnosis possible.

**Tension with Node 1:** The mechanism failure was fixable in one session. The projection failure requires a fundamentally different approach (learned weights). The temptation is to keep tuning the mechanism (lower disagree threshold, faster decay) to fix Seed B. But Seed B's problem is not the mechanism — it's the data the mechanism operates on.

---

## Node 4: The VDB Stabilization Finding Is Robust

Across every experimental condition tested today — CPU dispatch, TriX dispatch, float agreement, ternary agreement, three seeds — the no-bias condition crosses at step 0. Always. The VDB feedback (CMD 5) produces monotonic or near-monotonic P1→P2 transitions in every seed.

The ablation condition (CMD 4, no VDB blend) shows margin narrowing at step +20 in Seeds A and B. The CfC's fixed projection creates attractor basins that can temporarily recapture the LP state during transitions. The VDB prevents this by injecting P2-specific episodic content.

**Why it matters:** This is the one finding that survived every change today. It didn't depend on the agreement mechanism, the dispatch method, or the bias formulation. It is a property of the VDB→CfC feedback loop, not of the kinetic attention mechanism layered on top. The CLS paper's central claim — stabilization, not acceleration — is confirmed across 3 seeds × 4 experimental configurations = 12 independent observations.

**Tension with Node 5:** The stabilization finding doesn't need kinetic attention. It doesn't need agreement. It doesn't need gate bias. The entire Phase 5 mechanism could be removed and the CLS paper would still stand. This raises the question: does kinetic attention actually add value, or is it complexity on top of a system that works fine without it?

---

## Node 5: Does Kinetic Attention Add Value?

The evidence:

**For:** TEST 14 (normal sender, 4-pattern cycling) shows +1.0 to +2.5 Hamming point improvement in LP divergence under gate bias, within-seed, across 3 runs. The mechanism produces measurable per-group fire rate shifts (27% for G0). The prior is physically changing what the GIE computes.

**Against:** The improvement is projection-dependent (helps 2/3 seeds, hurts 1/3). The transition headwind, while now mostly fixed, revealed that the mechanism's most interesting property (epistemic humility at transitions) was broken for months and the system still passed all tests. The no-bias condition is simpler, more robust, and produces the strongest transition results.

**The honest answer:** Kinetic attention is a demonstrated mechanism with a real physical effect. It is not a demonstrated improvement. The engineering is sound — the ISR reads the bias, lowers the threshold, neurons fire differently. The question is whether the downstream effect (LP divergence) is reliably beneficial, and the answer across seeds is "usually yes, sometimes no."

**Why it matters:** The Stratum 1 paper needs to present this honestly. The mechanism works. The benefit is not universal. The engineering contribution is the mechanism itself (agreement-weighted gate bias in peripheral hardware at 30 µA), not the guarantee that it improves outcomes for every projection.

---

## Node 6: The System Is Self-Correcting in Some Ways but Not Others

The VDB is self-correcting: bad retrievals produce conflicts, HOLD damps them, and the next retrieval is independent. The accumulator is self-correcting: misclassifications are washed out by majority voting over hundreds of samples (now with clean TriX labels).

The gate bias is NOT self-correcting: once the bias locks onto the wrong pattern (Seed B during transition), the positive feedback loop (bias → more firing → more P1-like GIE hidden → LP stays P1 → bias holds) has no escape mechanism other than the disagree-count threshold. If the projection is degenerate, the disagree count never reaches threshold, and the bias holds indefinitely.

**Why it matters:** The ternary disagree-count is the right escape mechanism for non-degenerate projections. For degenerate projections, a second escape mechanism is needed — either a timeout ("if TriX has been saying P2 for N consecutive steps but LP agreement with P1 hasn't dropped, release anyway") or a cross-layer signal ("if TriX prediction ≠ argmax(LP accumulator counts), release").

**Tension with Node 5:** Adding a second escape mechanism to fix Seed B adds complexity for a marginal case. The simpler alternative is to acknowledge the projection limitation honestly and wait for Pillar 3 (Hebbian learning) to fix the root cause.

---

## Node 7: Red-Team as Research Methodology

Today's findings — the three bugs, the ternary insight, the multi-seed replication — all emerged from a red-team that was supposed to evaluate papers, not generate new results. The red-team identified:
- Issue #2 (N=1 on CLS) → led to multi-seed 14C → led to discovering the transition headwind
- Issue #4 (80% CPU classifier) → led to TriX dispatch → led to discovering the accumulator bug
- Issue #5 (multi-seed mixed results) → already known but became interpretable in light of the mechanism vs projection distinction

The red-team didn't find the bugs directly. It found the conditions under which the bugs were visible. The multi-seed experiment was the microscope. The red-team pointed the microscope.

**Why it matters:** This is the LMM's value proposition in action. The RAW pass (red-team) surfaced concerns. The concerns motivated experiments. The experiments revealed structure that wasn't visible in the original single-seed data. The structure led to diagnosis. The diagnosis led to fixes. The fixes produced cleaner data that strengthened the original claims.

---

## Node 8: The Float Is the Last Foreign Body

The bias computation still uses a float:
```c
float b = (margin > 0) ? BASE_GATE_BIAS * (float)margin / LP_HIDDEN_DIM : 0.0f;
```

This could be integer: `int b = (margin > 0) ? (BASE_GATE_BIAS * margin) / LP_HIDDEN_DIM : 0;`

BASE_GATE_BIAS (15) × max margin (16) = 240. Divided by LP_HIDDEN_DIM (16) = 15. The result fits in int8. No overflow. No float needed. The only consequence is that the bias values become discrete steps: 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9 for margins 0-16. The discretization is finer than the int8 storage, so no resolution is lost.

**Why it matters:** Not much, functionally — the float produces the same result (rounded to int8 anyway). But philosophically: the system claims "no floating point" in the ISR and LP core. The HP-side bias computation is the exception. Removing it completes the claim. All ternary. All integer. No floating point anywhere in the mechanism path.

**Tension:** This is cleanup, not a fix. The float isn't causing a bug. Changing it now adds a diff to the commit history without adding scientific value. It might be best left as a known item for the final pre-submission cleanup.
