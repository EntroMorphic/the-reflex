# Nodes: The Next Step-Change for The Reflex

## N1: The Building Loop

Since April 6, the project has followed a repeating cycle:
- Build mechanism (kinetic attention, Hebbian v1, v2, v3)
- Test on silicon
- Find it doesn't improve on VDB baseline
- Diagnose why → LP capacity ceiling
- Propose next mechanism (wider LP, MTFP VDB)

Six days. Four mechanisms. All four converge on: VDB-only is the mechanism. The March 22 TEST 12/13 results (VDB causally necessary, LP divergence from episodic memory) were the finding. Everything since has been confirming — not extending — that finding.

## N2: The VDB Baseline Is Already Strong

- 100% label-free classification (430 Hz, peripheral hardware)
- 8.5-9.7/80 MTFP LP divergence from VDB alone
- Causally necessary (CMD 4 ablation collapses what CMD 5 separates)
- Structural wall verified across all experiments
- CLS parallel is real and testable

This is a complete system with a novel architecture. It's not waiting for an improvement mechanism to become publishable.

## N3: MTFP VDB — What It Actually Changes

Two distinct improvements conflated under one label:

**Search improvement:** 112-trit Hamming instead of 48-trit. Better retrieval. This is real and measurable. Doesn't require LP assembly changes if the blend still uses the sign portion.

**Representation improvement:** This would require the LP dynamics to USE the MTFP information — wider hidden state, magnitude-aware blend. This requires rewriting main.S. This is the "wider LP" proposal in disguise.

The search improvement alone is achievable without assembly changes. The representation improvement is a different project.

## N4: UART Verification Is the Actual Blocker

Every paper says "JTAG attached" in limitations. The ~30 µA claim is a datasheet inference. The "peripheral-autonomous" framing depends on the GIE working without USB-JTAG.

The March 19 session identified a potential Silicon Interlock. It was never tested. This is the one experiment that could invalidate the core claim.

It's been flagged since March 19 — 24 days. It takes an afternoon of bench work. It hasn't been done.

## N5: The Three Papers Are Drafted and Rewritten

- Stratum 1 (April 12 rewrite): VDB temporal context paper. MTFP as headline metric. Negative results reported honestly.
- Stratum 2 (April 12 rewrite): CLS architecture. Transition experiment data missing (needs re-run under label-free conditions).
- Stratum 3 (April 12 update): Structural wall. Most abstract — least dependent on new data.

Stratum 1 and 3 may be submittable after UART verification. Stratum 2 explicitly says "we cannot cite specific alignment traces... until the experiment is re-run" (line 248).

## N6: The Tension Between Building and Shipping

Building more mechanisms has produced diminishing returns. Each cycle confirms the VDB baseline ceiling. But the papers are not in front of anyone.

Shipping creates external pressure (reviewer feedback) that could redirect the project more usefully than internal mechanism-building. A reviewer might ask a question that changes the trajectory. Or confirm that the VDB baseline alone is novel.

The risk of building indefinitely: the papers keep accumulating rewrites without reaching an audience. The knowledge stays inside this repo.

## N7: What Submission Actually Requires

For Stratum 1 (most ready):
1. UART verification (physical bench work)
2. Verify no stale numbers in the paper
3. Format for target venue
4. Submit

For Stratum 2:
1. Everything above, PLUS
2. Re-run TEST 14C under label-free conditions with distinct P2 payload
3. Update transition data in the paper

For Stratum 3:
1. UART verification (less critical — paper is about structure, not power)
2. Format and submit

## N8: The Phase Transition

The project is at a natural phase boundary. The building phase (mechanism exploration) has converged. Every path leads back to VDB-only. The system's identity is clear: it's a peripheral-hardware ternary system with episodic temporal context. The structural wall is the architectural contribution. The CLS parallel is the theoretical contribution. The negative results are the empirical contribution.

The next phase is not another mechanism. It's communication.

## N9: Post-Submission Technical Path

After papers are out:
1. MTFP VDB search (HP-side only, no assembly changes) — test whether richer retrieval improves LP trajectory
2. If that works: wider LP (32 neurons) — full assembly rewrite for more representational capacity
3. SAMA (multi-agent) — requires kinetic attention to be useful, which is currently harmful, so this may need MTFP VDB first

This is a clean dependency chain. But none of it blocks submission.
