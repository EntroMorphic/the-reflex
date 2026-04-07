# Raw Thoughts: The Reflex — Full Project LMM

*April 7, 2026. After a full-day session: audit, refactor, MTFP encoding, red-team, two paper drafts, TEST 14C implementation. First-hand knowledge of every load-bearing file.*

## Stream of Consciousness

The thing that keeps surprising me is how much was already there before anyone named it. The CLS parallel. The prior-signal separation. The MTFP encoding pattern (same insight applied three times: timing thermometer → gap history, sign → magnitude encoding, and presumably the next one after that). None of these were designed from theory. They were recognized after the hardware constraints forced particular structures into existence.

The ternary constraint is the generative force. Not a limitation. Every interesting property of this system traces back to {-1, 0, +1}. Three blend modes instead of two. The HOLD state as inertia. The zero-weight wall as structural separation. The packed bitmask representation that makes 16KB sufficient. The AND+popcount dot product that runs on peripheral hardware without a multiplier. Remove the ternary constraint and you get a standard neural network that needs a GPU. Keep it and you get something that couldn't exist in any other substrate.

The 30 µA number is real but it's not the point. The point is that the computation runs in the peripheral fabric — GDMA, PARLIO, PCNT — and the CPU is irrelevant after init. This is fundamentally different from "low-power inference on an MCU." The MCU isn't doing inference. The MCU's peripheral routing fabric is doing inference. The MCU is watching.

The VDB is permanent. The CfC weights are fixed. There is no training, no consolidation, no gradient. The system never gets better at extracting features — it only accumulates more episodes. This is a genuine architectural choice, not a limitation waiting to be resolved. Pillar 3 (Hebbian learning) would change this, but it's not clear that changing it is the right move. The fixed-weight CfC with permanent VDB is the cleanest demonstration of the CLS principle: the hippocampus doesn't train the neocortex, it compensates for the neocortex's limitations. The VDB routes around the CfC's projection degeneracies. If you train the CfC, the VDB becomes redundant for the trained cases, and the system loses its most interesting property.

The agreement mechanism is the philosophical core. Not the classification. Not the memory. The moment where the system checks: does my accumulated experience agree with what I'm seeing right now? And when it doesn't — the prior yields. Within one classification cycle. This is not discipline. This is structure. The prior can't override the signal because the signal pathway (TriX) is architecturally separate from the prior pathway (LP). The ternary constraint created the separation: W_f hidden = 0 is a consequence of how TriX signatures are installed, not a deliberate safety feature. The safety is emergent from the engineering.

What scares me about this project: it might be too clean. The story is almost too neat — constraints create architecture, architecture maps to theory, theory predicts behavior, behavior is verified on silicon. Real research usually has more mess. The mess here is in the details (PCNT clock domain drains, phantom EOF clearing, triple PCNT clears, PARLIO state machine resets) but the high-level narrative flows cleanly. A reviewer might suspect the narrative was constructed post-hoc to fit the data. It wasn't — the LMM journal entries and session records show the real-time discovery process — but the concern is real.

What excites me: the MTFP pattern. The same insight — "when a scalar quantization collapses information a downstream computation needs, replace it with a multi-trit encoding that preserves structure" — has been applied twice and produced measurable improvements both times. This is a principle, not a trick. And it's substrate-independent. The MTFP encoding works because ternary has exactly the right representational density: 3^5 = 243 states for a 5-trit value, covering the ~100 distinct dot values with enough resolution but no waste. A binary system would need 7 bits for the same range, with no natural "zero" state. A quaternary system would be overkill. Ternary is the sweet spot for this specific encoding problem. That might be coincidence. It might not.

The transition experiment (TEST 14C) is the load-bearing experiment for Stratum 2. If the system shows faster LP reorientation with VDB feedback than without it during a pattern switch, that's the CLS prediction: the hippocampal layer enables rapid adaptation that the fixed neocortical layer can't achieve alone. If it doesn't show this — if the CfC reorients just as fast without VDB help — then the VDB is episodic storage but not a learning accelerator, and the CLS analogy weakens significantly. The experiment is running right now. The outcome is not predetermined.

The three papers form a coherent cluster but they're also independent. Stratum 3 (prior-signal separation) could stand alone as a theoretical note even if the other two never ship. Stratum 1 (kinetic attention) is a self-contained engineering contribution. Stratum 2 (CLS architecture) is the most ambitious and depends on the transition data. The cluster submission strategy — coordinated, common hardware platform, cross-referencing — is stronger than independent submission, but each paper must be able to survive review independently.

The repo is clean now. 4 active headers. Named test functions. CMake target switching. Updated status doc. The archaeological record (50+ dead headers, 70+ milestone files) is preserved in archive directories. The code tells the story of the project's evolution. The papers tell the story of the project's significance. The LMM journal tells the story of the decisions.

## Questions Arising

1. Is the ternary constraint genuinely generative, or am I pattern-matching? Could a binary system with the same architecture produce the same results?
2. Is the MTFP encoding pattern a general principle or specific to this dot-product-to-hidden-state bottleneck?
3. What happens when the VDB fills up? Dynamic scaffolding (Pillar 1) was specified but never implemented. The 64-node limit is real.
4. Is the agreement mechanism the right one? The MTFP agreement entrainment bug showed that higher-resolution agreement can be destructive. The sign-space agreement is stable but coarse. Is there a middle ground?
5. What does this system look like at 10 patterns? 100? The current design is 4 patterns × 8 neurons per group = 32 neurons. Scaling patterns means either more neurons (hardware limit: PCNT count range) or sharing neurons across patterns (soft assignment, not hard groups).
6. The UART falsification is genuinely important. If the results don't replicate without JTAG, the entire project has a problem. The probability is very low (13+ runs with JTAG all PASS), but the data must exist.
7. What does the system do when it encounters a pattern it has never seen? The novelty gate rejects low-confidence classifications, but there's no mechanism for "I don't know" as a first-class output. The system always classifies into one of the 4 known patterns or rejects. There's no fifth class for "novel."

## First Instincts

- The ternary constraint is real and generative. Binary CfC has 2 blend modes; ternary has 3. The third mode (INVERT) creates oscillation resistance and path-dependent memory. This is not a soft advantage.
- The MTFP pattern is general. Any time a scalar quantization sits between a continuous computation and a downstream consumer, multi-trit encoding is the right response. The next application is probably the GIE hidden state itself (currently 32 sign trits from 32 neurons — magnitude information discarded).
- Dynamic scaffolding is the right next engineering step after papers. The VDB filling up is the first scaling bottleneck. Pruning redundant nodes while retaining distinctive ones is a well-defined problem with the LP Hamming distance as the utility metric.
- The novelty detection gap is a real limitation but not a paper-blocker. It's a future direction.
- The whole thing holds together. The architecture is sound, the claims are honest, the silicon is verified. What remains is execution: papers, UART falsification, and the transition experiment result.
