# Raw Thoughts: Step-Changes for The Reflex

*April 7, 2026. Three papers drafted, 15/15 PASS (normal sender), TEST 14C on silicon. What's next?*

## Stream of Consciousness

The papers are the immediate priority — they freeze the claims and establish the intellectual territory. But the system is at a natural plateau: 4 patterns, 32 GIE neurons, 16 LP neurons (80 MTFP), 64 VDB nodes, fixed weights, single chip. Every future direction pushes against one of these walls.

The ROADMAP defined three pillars. Let me think about each honestly.

**Pillar 1: Dynamic Scaffolding (VDB sliding window).** The VDB fills in ~3 minutes at current insert rate (1 per 8 confirmations). After 64 nodes, no more inserts. The system can still operate — it searches existing nodes — but it can't encode new states. If the environment changes after the VDB is full, the system has no way to store the new experience. Dynamic scaffolding prunes redundant nodes to free space for novel ones. The pruning criterion from the ROADMAP: prune if Hamming distance from pattern mean ≤ 1 (redundant), retain if ≥ 3 (distinctive), retain if high betweenness (graph connectivity). This is well-defined. It's maybe 100 lines of LP assembly (CMD 6). The LP core already has the mean vectors (from the accumulators). The challenge is doing the comparison AND the graph edge reconnection within one wake cycle.

This is the right next engineering step. It directly addresses the most immediate scaling wall (VDB capacity) without changing any mechanism. The system stays fixed-weight, stays ternary, stays peripheral. It just gets a longer memory.

**Pillar 2: SAMA (Substrate-Aware Multi-Agent).** Two or more C6 chips sharing GIE state via ESP-NOW. Board A's classification event triggers Board B's GIE to classify Board A's state through its own lens. Cross-agent episodic memory: "when I received state X from my neighbor." This is fascinating but it's a different paper entirely. The synchronization problem (all robots locked to P1 while P2 goes unobserved) is identified but unsolved. And it requires at least 3 boards (2 receivers + 1 sender, or 2 boards talking to each other). The hardware exists (3 C6 boards) but the firmware is nontrivial.

My gut says SAMA is premature. The single-chip architecture isn't fully exploited yet. Dynamic scaffolding and MTFP GIE encoding are cheaper and teach more about the single-agent system. Multi-agent work should come after the single-agent papers are submitted and the architecture is stable.

**Pillar 3: Hebbian GIE (Silicon Learning).** The LP core generates a weight-update signal from VDB mismatch and applies it to the GDMA descriptor chain in-situ. The GIE weights update from experience. This is the biggest step-change and the most dangerous. It breaks the fixed-weight property that makes the CLS analogy sharp. It introduces non-reversible state changes. It could improve accuracy or it could corrupt signatures. The ROADMAP correctly says this is last — all preceding pillars provide diagnostic infrastructure to detect good vs. bad updates.

But there's a deeper question the LMM on the full project surfaced: **should the CfC learn?** The Stratum 2 paper's thesis is that the hippocampus is permanently necessary BECAUSE the neocortex doesn't learn. If you add learning, the hippocampus might become redundant for learned patterns — and you lose the most novel finding. The fixed-weight system is theoretically cleaner. The learning system is practically better. Which do you optimize for?

My instinct: Pillar 3 is a separate research track, not a continuation of the current work. The current work is "what happens when the neocortex can't learn." Pillar 3 is "what happens when it can." Both are interesting. They're different questions.

**MTFP GIE encoding.** The predicted next MTFP application: encode GIE dot magnitudes as multi-trit values instead of signs. The LP core currently sees 32 sign trits from the GIE. With MTFP, it would see 160 trits (5 per neuron). This gives the LP projection richer input without changing the GIE hardware.

The concern from the REFLECT phase: LP input goes from 48 to 192 trits. The VDB snapshot would stay at 48 (sign-space, per Option B). But the LP CfC would need weight matrices for 192-trit input × 16 neurons × 2 pathways = huge. The current LP SRAM budget can't support this. Either the LP neurons shrink (fewer neurons, wider input) or the MTFP is HP-side only (like LP dot MTFP).

Actually, wait. The LP core reads gie_hidden[32] as bytes from LP SRAM. If I encode GIE dots as MTFP on the HP side and write the 160-trit result to LP SRAM... the LP core's CfC would need to read 160 trits instead of 32. Its weight matrices would need to be 160+16=176 trits wide. At 3 words per 48 trits, 176 trits = 11 words per mask = 22 words per neuron per pathway = 16 × 2 × 22 × 4 = 5,632 bytes. Currently 1,536 bytes. That's a 3.7× increase in weight storage. LP SRAM can't hold it. Not feasible for LP-core-side computation.

HP-side only then: encode GIE dots as MTFP on the HP core, use the 160-trit representation for agreement computation and LP Hamming measurement, but keep the LP core reading 32 sign trits. Same pattern as LP dot MTFP. The LP core never knows MTFP exists. The HP core sees more.

This is a quick win — maybe 20 lines of code (reuse the existing MTFP encoder). The question is whether it adds meaningful information beyond what LP dot MTFP already provides. The LP dots are a function of GIE hidden (they're the projection of GIE state through LP weights). If GIE dot magnitudes carry pattern-specific information that survives the LP projection, then GIE MTFP helps. If the LP projection collapses the GIE magnitude information (which it might — it's a random projection), then GIE MTFP is redundant with LP dot MTFP.

**Novel pattern detection.** The system has no "I don't know" output. Everything is classified into one of 4 known patterns or rejected by the novelty gate (core_best < NOVELTY_THRESHOLD). The novelty gate is a threshold on the best dot product — it catches "nothing matches well" but not "this matches P1 but it's actually a new pattern P5 that looks like P1." A dedicated novelty detector would compare the current input's signature against all known signatures and flag when the best match is below a learned threshold. This is a feature, not a step-change. Important for deployment, not important for papers.

**Multi-seed validation.** All results use seed `0xCAFE1234`. Different seeds produce different projection degeneracies. To claim the architecture is robust (not just this particular weight matrix), at least 3 seeds are needed. This is trivial to implement — change one constant and reflash. The test suite runs in ~12 minutes. Three seeds = 36 minutes plus flash time. Should do this before Stratum 1 submission.

**UART falsification.** Hardware session. Wire GPIO 16/17 to a UART bridge. Redirect stdout. Battery power. Run full suite. Probably 2 hours of bench time. Should do this but it's not a step-change — it's verification.

**Nucleo APU expansion.** The L4R5ZI-P adds VDB search acceleration (SPI at 40 MHz) and MTFP21 inference (QSPI at 160 Mbps). This is the path to real-time MTFP inference on the LP core — the Nucleo encodes MTFP and writes the result to LP SRAM, replacing the HP core's role. Power budget goes from ~30 µA to ~10-50 mA. Different operating mode, different papers, different venue. Future track.

## Questions Arising

1. What's the highest-leverage single change I could make in one session?
2. Is multi-seed validation blocking for paper submission?
3. Should MTFP GIE encoding come before or after dynamic scaffolding?
4. Is there a way to test Pillar 3 (Hebbian) in simulation before touching the assembly?
5. What's the minimum viable SAMA demo — 2 boards with cross-classification?
6. When does the project stop being research and start being product?

## First Instincts

- Multi-seed validation is the highest-leverage single action. Trivial to implement, directly strengthens both Stratum 1 and 2 papers.
- Dynamic scaffolding (Pillar 1) is the right next engineering step after papers.
- GIE MTFP encoding is a quick win but might be redundant with LP dot MTFP. Test empirically.
- Pillar 3 is a separate research track. Don't mix it with the current paper cycle.
- SAMA requires stable single-agent architecture. Premature before Pillar 1.
- UART falsification should happen this week. It's blocking peace of mind if not papers.
