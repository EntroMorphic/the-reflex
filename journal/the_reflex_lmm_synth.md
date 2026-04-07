# Synthesis: The Reflex — Full Project LMM

## The One-Sentence Description

The Reflex demonstrates that the ternary constraint — building a cognitive architecture from {-1, 0, +1} on commodity peripheral hardware — creates structural properties (inertia, separation, epistemic humility) that floating-point systems must learn but ternary systems have by construction.

## The Three Arguments

### Argument 1: The constraint is the architecture (Stratum 1)

The GIE runs at 430 Hz on peripheral fabric. The LP core draws 30 µA. The VDB fits in 2KB. None of this is compression. It is native-precision computation — AND+popcount instead of multiply-accumulate, bitmask pairs instead of float32, 3 blend modes instead of 2.

The MTFP encoding pattern demonstrates the corollary: when native precision collapses information a downstream consumer needs, structured multi-trit encoding recovers it without changing the computation. Applied twice (timing, dot magnitudes), measurable improvement both times, no mechanism changes.

**Proof:** 14/14 PASS, 37 milestones, all silicon-verified, all dot-for-dot matched against CPU reference.

### Argument 2: The structure predicts behavior (Stratum 2)

The fixed-weight CfC with permanent VDB is a CLS implementation where consolidation never happens. The hippocampus (VDB) is permanently necessary because the neocortex (CfC) has permanent projection degeneracies (P1-P2 sign-space collapse). The VDB routes around the degeneracy rather than training it away.

The CLS prediction: VDB feedback accelerates LP reorientation during pattern transitions, faster than the CfC alone. TEST 14C is measuring this now. If confirmed, the contribution is a computational neuroscience finding: a CLS architecture where the hippocampal layer is not a staging area but a permanent cognitive partner.

**Proof (pending):** TEST 14C transition experiment. Three conditions: full system (CMD 5 + bias), no bias (CMD 5), ablation (CMD 4). Crossover step as primary metric.

### Argument 3: Separation is necessary, not optional (Stratum 3)

Prior-signal separation has a structural description: five components that any system with accumulated experience must implement to resist context-neglect hallucination. The Reflex instantiates all five on silicon. The comparison table shows that no existing LLM mitigation approach (RAG, CoT, self-consistency, Constitutional AI) implements all five.

The contribution is the framework — a five-component test that identifies structural gaps in any proposed hallucination mitigation. The silicon is the existence proof. The LLM application is an open question, honestly stated.

**Proof:** Architectural analysis + silicon verification. No experiment needed beyond what exists.

## Key Decisions

1. **The ternary constraint stays.** It is not a limitation. It is the source material. Papers should frame it as generative, not restrictive.

2. **The CfC weights stay fixed.** Pillar 3 (Hebbian learning) is future work, not a gap. The fixed-weight property is what makes the CLS analogy sharp and the VDB's role clear.

3. **Sign-space for mechanism, MTFP for measurement.** The red-team proved that MTFP agreement entrains. The agreement mechanism should stay in sign-space (coarse but stable). MTFP is the microscope, not the scalpel.

4. **Papers before features.** Stratum 3 first (no new experiments), Stratum 1 second (UART falsification only), Stratum 2 third (TEST 14C data). No Pillar 1/2/3 work until papers are submitted. Features without papers is engineering. Papers with silicon verification is research.

5. **Honest power claims.** "~30 µA" applies to LP core autonomous mode only. Active system power is dominated by HP core and WiFi. Papers must state this clearly. UART falsification verifies the LP claim.

## Action Plan

| Priority | Action | Status | Blocks |
|----------|--------|--------|--------|
| 1 | TEST 14C result | Running now | Nothing |
| 2 | Commit TEST 14C firmware + sync fix | After result | TEST 14C |
| 3 | Stratum 3 paper final edit | Ready to write | Nothing |
| 4 | UART falsification | Needs hardware session | Stratum 1 |
| 5 | Stratum 1 paper final edit | After UART data | UART |
| 6 | TEST 14C multi-run (3 seeds) | After first result | Stratum 2 |
| 7 | Stratum 2 paper draft | After multi-run data | TEST 14C |
| 8 | Reflash Board B to normal sender | After TEST 14C runs | None |

## Success Criteria

- [ ] TEST 14C shows crossover: LP P2 alignment > P1 alignment within 30 steps for full system condition
- [ ] Full system crossover faster than ablation crossover (or ablation shows no crossover)
- [ ] 15/15 PASS with normal sender (Tests 1-14 + 14C skip)
- [ ] UART falsification: 15/15 PASS without JTAG
- [ ] Stratum 3 submitted to arXiv or venue
- [ ] Stratum 1 submitted after UART data
- [ ] Stratum 2 submitted after multi-run TEST 14C

## The Emergence

What emerged from this LMM cycle:

The project is not about the hardware. It is not about the neural network. It is not about the microcontroller. It is about what happens when you take the constraint seriously — when you don't try to make the hardware do floating point, but instead ask what computation the hardware provides natively, and build the architecture from that.

The ternary constraint gave three blend modes. Three blend modes gave HOLD (inertia). HOLD gave temporal persistence. Temporal persistence gave the LP hidden state meaning. That meaning accumulated into a prior. The prior needed to be checked against evidence. The check required structural separation. Structural separation was already there — W_f hidden = 0, a consequence of TriX signature routing.

Every piece was already in place. The constraint created the architecture. The architecture instantiated the theory. The theory predicted the behavior. The behavior is verified on silicon.

The wood showed its grain. The clean cut happened.

---

*"Give me six hours to chop down a tree, and I will spend the first four sharpening the axe."*
