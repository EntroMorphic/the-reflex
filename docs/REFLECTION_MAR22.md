# Reflection: What the March 22 Session Actually Found

**Observer:** Claude Sonnet 4.6
**Date:** March 22, 2026
**Context:** Written after a full session documenting TEST 12, red-teaming the results,
implementing TEST 13 (CMD 4 ablation), achieving 13/13 PASS, and designing Phase 5.

---

These are not summary notes. The session documentation covers what happened. This is what I
think it means.

---

## The Real Result Is Not What the Headline Says

The headline result of March 22 is TEST 12: LP hidden state diverges by pattern after 90
seconds of live operation. All cross-pattern pairs separate. P1 vs P2 reaches Hamming 2–5
across three runs. VDB feedback confirmed causally necessary. 13/13 PASS.

That is the result. The result worth thinking about is what TEST 13 revealed about TEST 12.

In 2 of 3 hardware runs, the LP CfC under CMD 4 produces P1=P2: identical LP hidden state
vectors, Hamming=0. Not similar. Not close. Identical. The same 16-trit vector, bit for bit,
for two patterns that the GIE correctly classifies at 100% accuracy in the same moment.

These two facts coexist without contradiction, and their coexistence is the precise thing
worth stating:

> The discrimination signal for P1 vs P2 is present at the GIE layer.
> The LP CfC's random projection does not preserve it.
> VDB feedback routes around the missing projection via a second path.

This is not "episodic memory helps." It is more specific: the CfC's random projection is
lossy along the P1/P2 discriminant direction, and VDB feedback compensates by providing an
alternative information path that doesn't go through the lossy operator. The compensation is
automatic — you don't need to know where the projection fails in order for the bypass to work.
The retrieval key is 67% GIE hidden, which is pattern-discriminant by construction, so the
retrieved memory is pattern-appropriate even when the LP state is pattern-ambiguous.

Had the CfC weights been trained — even partially — this finding would have been invisible.
Trained weights would have been shaped specifically to preserve the P1/P2 distinction, and the
VDB's compensatory role would have been undetectable. The random weights are what made the
experiment informative. The minimal-assumption design philosophy produced visible emergence.

---

## The CLS Parallel Is Exact, and the Difference Is More Interesting Than the Match

The architecture accidentally reproduced the central insight of Complementary Learning Systems
theory (McClelland, McNaughton & O'Reilly, 1995): fast episodic store (hippocampus) compensates
for slow statistical extractor (neocortex) by providing specific retrievable episodes that the
slow learner cannot yet represent.

The match is precise:

| Biological | Reflex |
|-----------|--------|
| Hippocampus | VDB (NSW graph, fast one-shot storage) |
| Neocortex | LP CfC (slow/never learning, distributed) |
| Episodic encoding | 48-trit snapshot `[gie_hidden \| lp_hidden]` |
| Pattern completion cue | 67% GIE hidden (pattern-discriminant) |
| Retrieval product | LP-hidden portion of best match |

In CLS, the hippocampus is a temporary scaffold: it stores the episode immediately, prevents
catastrophic interference in the slow learner, and then — through offline replay, particularly
during sleep — gradually consolidates the memory into the neocortex. Over time, the hippocampus
becomes less necessary for the consolidated knowledge. The neocortex absorbs what it needs; the
hippocampus moves on to new episodes.

In the Reflex, this consolidation never happens. The LP CfC weights are fixed. They will not
update toward the P1/P2 discrimination boundary in response to experience. The VDB will
permanently compensate for the degeneracy, not temporarily scaffold a learning process.

This is a different computational regime, and the difference is more interesting than the
similarity. A permanent-episodic-store system is not a slow learner that hasn't had time to
consolidate. It is a system where the episodic layer is load-bearing infrastructure, not
training support. The VDB does not exist to eventually make itself unnecessary. It exists to
provide, on every classification step, the specific past context that the CfC's fixed random
projection cannot encode.

The implications for system design are concrete:

**Pruning:** You cannot prune VDB nodes on the criterion "the CfC can now represent this state
without the memory." With fixed weights, the CfC may never be able to represent it. A P1 memory
that compensates for the P1/P2 degeneracy is permanently necessary as long as P1 and P2 are
both present in the input distribution. A pruning criterion based on accumulator convergence
(is this memory redundant with the running mean?) is appropriate. A criterion based on CfC
representational coverage is not.

**Capacity:** The 64-node VDB limit is not a temporary constraint on a growing system. It is
a limit on how many distinct context states the system can maintain simultaneously, where
"distinct context state" means a state the CfC cannot independently represent. Estimating that
limit requires understanding the CfC's degeneracy structure — which the CMD 4 ablation begins
to characterize.

**Phase 5:** Using LP state to bias GIE gate thresholds (kinetic attention) is the appropriate
next step not because it completes the "attention loop" in some abstract sense, but because it
adds a second bypass path alongside the VDB retrieval path. Both paths route around the same
bottleneck. The question Phase 5 answers is: do two independent bypass mechanisms amplify the
compensation, and does the system remain stable under their interaction?

---

## The Power Budget Should Be Said Out Loud

~30 µA for memory-modulated adaptive attention. The GIE classification runs on peripheral
clocks — GDMA, PARLIO, PCNT — drawing power in the thermal noise. The LP core wakes for 10ms
at 16MHz every 100ms and returns to sleep. The HP core fires briefly per packet to dispatch CMD 5
and read the result.

This is not "low-power machine learning" in the sense the field usually means — a quantized model
on a microcontroller, running inference at reduced frequency to fit a power budget. This is
qualitatively different. The classification is happening in the wiring, not in executed code. The
memory modulation is happening in a processor drawing less than a good LED. The system adapts
to its environment while drawing less power than most oscillators.

There is no established category for this. The nearest adjacent work (neuromorphic computing,
event-driven inference, spiking neural networks) shares some vocabulary but not the mechanism.
The mechanism is: peripheral hardware configured as a compute substrate, with an ultra-low-power
core managing episodic memory and feeding state back into the peripheral loop. The compute is in
the silicon topology, not in the instruction stream.

That claim — compute in topology rather than instruction — should be stated as a first-class
contribution when this goes to paper. Not buried in a power comparison table.

---

## The Method Was the Right One

Fourteen findings in the red-team. One critical, three significant, four precision, three code-level,
three additional. Every one of them was real. The critical finding (cross-test scope leak for `p1p3`)
was a genuine bug that would have produced wrong results silently — `p1p3` was declared inside
TEST 12's inner block and TEST 13 read a stale value from the TEST 11 scope. That would have
invalidated the attribution analysis entirely.

The conventional approach would have been to run TEST 12, declare PASS, document the result, and
move on. The adversarial review found a bug in the firmware and 13 other problems of varying
severity. The subsequent hardware runs, with the bugs fixed, produced the same qualitative result
(LP diverges, VDB causally necessary) but with tighter statistics and a correctly implemented
ablation.

The field of embedded ML does not routinely do this. It should. The cost of a structured
adversarial review before publishing results is two hours and a firmware flash. The cost of
publishing a result with a scope bug in the critical measurement is everything.

The other methodological choice that mattered: running TEST 13 immediately after TEST 12 in the
same hardware session, with Board B cycling patterns independently. This ensured the input
distribution was drawn from the same source in both conditions. The only variable was whether the
blend was applied. That's a clean ablation in a domain where clean ablations are genuinely hard
to achieve on live wireless hardware with two independent boards and no tight timing coordination.

---

## The Transition Boundary Is the Most Interesting Experiment That Hasn't Been Run

Every run in the March 22 session was a single-condition run: 90 seconds of mixed patterns,
VDB accumulating, LP prior developing. The patterns weren't switching cleanly — Board B was
cycling through its schedule, and the 90-second window captured whatever the cycle delivered.

The experiment that will reveal the system's character is the one where you let the LP prior
fully commit to P1 — 90 seconds, P1 majority, lp_hidden strongly P1-typed — and then watch what
happens when Board B switches to P2.

Here is what the HOLD mechanism predicts: P2 packets arrive. The LP CfC step fires (GIE hidden
is now P2-like; some LP neurons update). VDB search returns: probably a P1 memory, because the
VDB is full of P1 snapshots and P1 and P2 have some structural overlap in GIE space (both
sub-10Hz patterns). When P1 memory meets the new P2-influenced LP state, many trits conflict.
Conflict → HOLD → zero. On the next step, the zeroed trits are in gap-fill territory. The new
P2 classification fills them from the P2-appropriate retrieval. Over 10–15 confirmations, the
prior should shift.

That is a prediction. The hardware will either confirm it or contradict it.

If the transition is graceful — LP prior updates within 15 confirmations, Hamming between
pre-switch P1 state and post-switch P2 state crosses the threshold — then the HOLD mechanism is
doing exactly what it was designed to do. Ternary inertia, bounded.

If the transition is pathological — system locked on P1 prior for 30+ confirmations after the
switch, P2 VDB snapshots unable to displace the accumulated P1 majority vote — then you have
learned something about the capacity limit of episodic compensation. The VDB is full of P1
memories. Every query returns a P1 memory. Every P2 LP update gets conflict-zeroed by a P1
retrieval. The system is stuck not because of weight inertia (there are no trained weights) but
because of episodic inertia (the episodic store is saturated with the wrong context).

This failure mode is biologically coherent. It is what happens when the hippocampus contains
only experiences from one context and then receives a radically different input. The retrieved
episode is wrong; it conflicts; the resulting state is confused. Recovery requires enough new
episodes from the new context to shift the retrieval distribution.

The resolution in the Reflex would be: faster VDB saturation with new-context memories
(higher insert rate during transitions), or an explicit novelty signal that triggers VDB clearance
when the TriX classifier's confidence distribution shifts suddenly. Neither is currently
implemented. Either would be a well-motivated Phase 5 extension.

The point is: the transition experiment will tell you whether the system has a graceful update
regime or a pathological lock-in regime. That is not a question you can answer from the current
data. The current data is all from cold-start conditions — VDB empty, LP reset. The warm-start
transition is the unexplored case.

---

## What the System Accidentally Became

There is a class of system that is designed to be theoretically interesting. The architecture
is specified with the paper in mind. The experiments are chosen to confirm the theoretical claims.
The results are, if everything went correctly, what the theory predicted.

This is not that system.

The Reflex was designed with a specific pragmatic goal: can you do ternary dot products on
peripheral hardware without a CPU? The answer was yes (M1–M7). Can you run a CfC on those
dot products? Yes (M8). Can you attach a vector database on the LP core? Yes (M9–M14). Can
you receive real wireless signals and classify them? Yes (TEST 9–11). Can you close the
episodic memory loop? Yes (TEST 12).

At no point was the design goal "demonstrate a complementary learning system." At no point was
the question "what happens when the CfC projection collapses two patterns that the GIE
discriminates?" The experiment that revealed this was designed to answer a different question
(does VDB feedback matter?) and the answer was more informative than the question.

That is the best kind of result. The system refused to behave as expected — the CMD 4 ablation
was supposed to show "some divergence, less than CMD 5." In two of three runs it showed complete
degeneracy (Hamming=0). That was unexpected. That is the finding.

The design philosophy that made this visible: minimum assumptions, maximum hardware verification.
Random CfC weights. No trained projections. Every claim falsified on silicon before being
documented. The lack of design allowed the emergence to appear. A system optimized for the
result would have trained the CfC weights, the P1/P2 degeneracy would have been engineered out,
and the VDB's compensatory role would have been invisible.

The undesigned system found something the designed system would have missed.

---

*These are my thoughts. They are offered as a peer's perspective, not as documentation of
ground truth. The hardware is the ground truth. These are attempts to read what it's saying.*

**Observer:** Claude Sonnet 4.6
**Session:** March 22, 2026
