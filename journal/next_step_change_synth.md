# Synthesis: The Next Step-Change for The Reflex

## The Decision

The next step-change is not a mechanism. It's the transition from building to shipping.

The project has spent 21 days confirming that VDB episodic memory is the mechanism. Every downstream mechanism (kinetic attention, Hebbian v1-v3) converged on the same answer: the VDB baseline is the ceiling at 16 LP neurons, and it's already producing novel results. Continuing to build mechanisms at this LP dimensionality will keep hitting the same wall. The building loop has converged.

## Action Plan

### Phase 1: UART Falsification (gates everything)

Wire GPIO 16/17 to a secondary serial bridge. Power Board A from battery or dumb USB. Run the full test suite. Measure current with INA219 or bench DMM.

This is the one experiment that can invalidate the core claim. It takes an afternoon. It's been identified as a blocker since March 19. Do it first.

**If PASS:** The ~30 µA claim becomes a measurement. The papers gain a data point.
**If FAIL:** The "peripheral-autonomous" framing needs revision. The computation is still real (GDMA→PARLIO→PCNT), but the power claim and the embedded-systems venue targeting change. Better to know now than after submission.

### Phase 2: Label-Free TEST 14C Re-Run (parallel with paper verification)

The Stratum 2 paper has an explicit data hole at line 248. The firmware is already correct. The sender is ready. This is a ~35-minute silicon run:

```
Sender: TRANSITION_MODE=1 (P1 90s → P2 30s)
Receiver: MASK_PATTERN_ID=1, MASK_PATTERN_ID_INPUT=1
3 seeds × 3 conditions
```

Capture the data, fill the transition-experiment section of the CLS paper. This closes the last empirical gap.

### Phase 3: Paper Verification Pass (parallel with TEST 14C)

Systematic grep of all three papers against data/apr11_2026/SUMMARY.md and data/apr9_2026/SUMMARY.md. Every cited number must have a traceable source. The April 12 rewrites were substantial — verify they're complete.

Check:
- All MTFP numbers match authoritative dataset
- All test references cite correct commits
- No stale crossover-step language remains (PAPER_READINESS.md has 5 occurrences — is that current?)
- Limitations sections match current state

### Phase 4: Submit Stratum 1 and Stratum 3

These two papers don't have data holes. Stratum 1 (engineering, VDB temporal context) targets embedded systems venues. Stratum 3 (structural hallucination resistance) targets AI/ML venues. Different audiences, no overlap, can submit simultaneously.

Stratum 2 (CLS architecture) submits after the TEST 14C data fills the gap — probably one session after Phase 2.

## Success Criteria

1. UART verification produces a PASS/FAIL result with current measurement data
2. Label-free TEST 14C data exists for 3 seeds × 3 conditions under current firmware config
3. Every number in the three papers traces to a committed data file
4. Stratum 1 and 3 are submitted to venues

## Post-Submission Technical Path (not blocking)

1. **MTFP VDB search** (HP-side only): Change VDB node format from 48 to 112 trits. Search uses 112-trit Hamming. Blend still uses 16-trit sign portion. No assembly changes. Test whether richer retrieval improves LP trajectory.

2. **Wider LP** (if MTFP VDB shows retrieval is the bottleneck): 32 LP neurons, assembly rewrite, VDB dimension change. The real capacity step-change.

3. **SAMA** (multi-agent): Requires kinetic attention to be useful or MTFP VDB to provide context-sensitive response. Post-LP-expansion.

## Explicit Handling of Major Tensions

**"But the papers need MTFP VDB results to be compelling"** — No. The causal necessity finding (CMD 4 vs CMD 5) is qualitative and binary. The MTFP baseline (9.7/80) provides a quantitative measurement. The negative results (kinetic attention, Hebbian) are empirical contributions in their own right. A reviewer asking "is 9.7 good?" can be answered with the ablation: "without VDB, P1=P2; with VDB, Hamming 9/80."

**"UART might fail"** — Then it's better to know before submitting a paper that says "~30 µA" in the abstract. The experiment is cheap. The information is high-value. This is exactly the kind of uncertainty the LMM says to chase: the path that produces new information, not the safe path.

**"Submitting three papers simultaneously is aggressive"** — They target different venues with different audiences. Stratum 1 is engineering (ISCAS, DATE, ISLPED). Stratum 3 is AI/ML (NeurIPS workshop, AAAI). No reviewer overlap. Coordinated cluster submission is more visible than sequential.

**"What about the DO_THIS_NEXT items?"** — The test verdict logic is now fixed (this session). UART is Phase 1 above. The paper rewrites are done (April 12). The RSSI dead zone is post-submission. The sender robustification is documented (option A, FLASH_GUIDE). The DO_THIS_NEXT is substantially resolved — it needs updating to reflect current state, but it's no longer a wall of blockers.

---

*The mechanism works. The data is honest. The building loop has converged. The next step-change is letting the work leave the building.*
