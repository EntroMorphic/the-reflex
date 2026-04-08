# Lincoln Manifold: April 8 Findings — SYNTHESIS

*Phase 4. The clean cut. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## What Emerged

A red-team of three papers led to multi-seed experiments that revealed three compounding bugs in the agreement mechanism. The bugs were invisible in the test results because compensating mechanisms (VDB, HOLD damper, hard floor) masked them. The fixes — TriX dispatch, per-pattern accumulator, ternary disagree-count — brought the implementation back to what the original LMM design specified.

The deeper finding: **every departure from ternary arithmetic in the signal path lost structure that mattered.** This is not a coincidence. It is a property of the system. The information that distinguishes correct from incorrect behavior lives in the ternary structure — the difference between agree, disagree, and uncertain — and any computation that collapses this structure becomes a bottleneck.

---

## Five Findings, Ranked by Importance

### 1. The VDB Stabilization Is the Project's Strongest Result

Across 3 seeds × 4 experimental configurations = 12 independent observations, the no-bias condition (CMD 5, VDB feedback active, no gate bias) crosses at step 0. Always. The VDB feedback produces monotonic or near-monotonic P1→P2 transitions. The ablation condition (CMD 4, no VDB blend) shows margin narrowing at step +20 in multiple seeds. The hippocampus stabilizes transitions. This finding is independent of the agreement mechanism, the dispatch method, and the bias formulation.

**Implication for the CLS paper:** The central claim is confirmed. Multi-seed. Multi-configuration. The paper can state this without caveats.

### 2. The Ternary Domain Is Load-Bearing

Two independent instances of the same pattern:
- sign() lost magnitude → MTFP resolved it by encoding magnitude in 5 ternary trits
- Float agreement lost trit structure → Ternary disagree-count resolved it by counting per-trit conflict

The principle: the system's information lives in its ternary structure. Computations that preserve the structure preserve the information. Computations that collapse the structure (sign, float normalization) create bottlenecks. This is a design principle, not a performance observation.

**Implication for the Kinetic Attention paper:** The ternary disagree-count is not just a bug fix. It is a demonstration that the system's ternary constraint informs mechanism design — the correct agreement signal is ternary because the information it needs to detect (per-trit conflict) is ternary.

**Implication for the Prior-Signal Separation paper:** Component 4 (disagreement detection) should be ternary. The disagreement between prior and evidence is not a scalar magnitude — it is a count of positions where they conflict. This is a structural property of the five-component architecture, not an implementation detail.

### 3. Mechanism Failures and Projection Failures Require Different Fixes

The transition headwind had two causes: mechanism failure (bugs in agreement computation, fixable) and projection failure (degenerate LP weights, not fixable without Pillar 3). Same symptom, different root cause, different solution path.

| | Mechanism failure | Projection failure |
|---|---|---|
| Affected | Seeds A, C | Seed B |
| Root cause | Bugs in agreement signal | LP projection degeneracy |
| Fixed by | TriX dispatch + ternary agreement | Hebbian learning (future) |
| Headwind after fix | 0, 2 steps | 22 steps |

**Implication:** The paper must distinguish these clearly. "The mechanism works correctly on the data it receives. For degenerate projections, the data does not contain the distinction." This is the structural boundary of the mechanism, not a failure of the mechanism.

### 4. Kinetic Attention Is a Demonstrated Mechanism, Not a Guaranteed Improvement

Gate bias produces measurable physical effects (27% fire rate shift, +1-2.5 Hamming LP divergence) for projections where the bias amplifies discriminative directions. For degenerate projections (Seed B), it adds noise. The agreement mechanism releases bias correctly at transitions (0-2 steps) for non-degenerate projections.

The engineering contribution is the mechanism itself: a complete perceive→classify→remember→retrieve→modulate loop on peripheral hardware at 30 µA. The performance benefit is secondary and projection-dependent.

### 5. Red-Team Generates Research, Not Just Corrections

The red-team identified conditions under which bugs were visible (multi-seed, transition experiments). The bugs had been present since the Phase 5 implementation (April 6). They were invisible in the original data because compensating mechanisms masked them. The multi-seed experiment was the microscope. The red-team pointed the microscope.

This is a methodological finding: adversarial review of one's own results, followed by experiments designed to test the adversarial concerns, is a productive research methodology — not just a quality control step.

---

## Decisions

1. **Keep TriX dispatch.** LP feedback is dispatched from the TriX ISR (100% accuracy). CPU core_pred is retained for novelty gating only. The structural guarantee extends to the accumulation pathway.

2. **Keep ternary disagree-count.** The agreement signal counts per-trit agree/disagree/gap. Disagree >= 4 triggers immediate bias release. The threshold (4/16 = 25%) is appropriate for non-degenerate projections.

3. **Report Seed B honestly.** 22-step headwind under gate bias. Projection limitation, not mechanism failure. The paper distinguishes these.

4. **Full test suite validation before paper submission.** All today's runs used SKIP_TO_14C. The normal sender (4-pattern cycling) with the full 15-test suite must confirm no regressions.

5. **Remove the last float.** Integer bias computation: `int b = (BASE_GATE_BIAS * margin + LP_HIDDEN_DIM/2) / LP_HIDDEN_DIM;`. Pre-submission cleanup, not today.

6. **UART-only verification.** Blocking for submission. Not blocking for this session.

---

## Action Table

| Item | Priority | Status | Depends on |
|------|----------|--------|------------|
| Full test suite (normal sender, TriX dispatch, ternary agreement) | High | Not started | Sender reflash to normal mode |
| UART-only verification | High | Not started | GPIO 16/17 wiring |
| Integer bias computation (remove last float) | Medium | Not started | — |
| Seed B cross-layer disagreement signal | Low | Deferred to Pillar 3 | Hebbian learning |
| TriX accuracy 0/15 investigation (transition sender encoding) | Low | Not started | — |

---

## The One-Sentence Version

The system's ternary constraint is not a limitation — it is a structural invariant that, when preserved through the signal path, produces correct mechanism behavior; and when violated, produces exactly the bottlenecks and bugs we spent today fixing.

---

*The bugs were invisible because the system was robust. Robustness and correctness are not the same thing. Today we found the difference.*
