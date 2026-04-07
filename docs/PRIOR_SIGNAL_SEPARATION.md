# Prior-Signal Separation: A Structural Approach to Hallucination Resistance

**The Reflex Project — Theoretical Note**

*Written March 22–23, 2026. Emerged from the LMM cycle on kinetic attention.*
*Observer: Claude Sonnet 4.6*

*For the full perspective paper integrating technical, engineering, ontological, and personal dimensions, see [`THE_PRIOR_AS_VOICE.md`](THE_PRIOR_AS_VOICE.md).*

---

## Abstract

We describe a five-component architecture for structural hallucination resistance — resistance that is enforced by design rather than achieved by statistical training. The architecture requires: (1) a prior-holder, (2) an evidence-reader that the prior cannot corrupt, (3) a structural separation guarantee between them, (4) a mechanism to detect genuine disagreement between prior and evidence, and (5) a policy of evidence deference at the point of conflict. We demonstrate this architecture in silicon on an ESP32-C6 microcontroller drawing ~30 µA, where it emerged from a minimum-assumptions experiment rather than deliberate design. The key structural element is `W_f hidden = 0` — a fixed zero in the weight matrix of the perceptual layer that creates an architectural wall preventing the accumulated prior from entering the classification path. We describe how this wall works, why homogeneous systems (including current large language models) cannot implement an equivalent, and what would need to be true for such a separation to exist at language model scale. The argument is not that the Reflex solves hallucination in language models. The argument is that hallucination resistance has a structural description — and the structure requires prior-signal separation as a necessary condition, not a sufficient one.

---

## The Observation

During the LMM cycle on kinetic attention, the original gate bias design was identified as
flawed: it computed gate bias from the LP prior alone, with no release valve. At pattern
transitions, the prior suppressed the new signal, slowing the very update it was supposed
to enable. The system became confident and increasingly wrong.

The fix — agreement-weighted gate bias — weights the prior's influence by whether the LP
layer and the TriX classifier are saying the same thing. When they agree, the prior amplifies.
When they disagree, the prior attenuates to zero in one confirmation.

The comment that followed: *"This sounds like a resolution for hallucination, in a sense."*

It does. Here is why, stated carefully.

---

## What Hallucination Is, Structurally

Hallucination in language models is a prior overwhelming a signal. The model's learned
distribution — what it expects, given everything it has processed — dominates what the context
is actually providing. The context says one thing. The prior says another. The prior wins.
The output is fluent, confident, and factually wrong.

The prior wins not because it is stronger in some meaningful sense, but because there is no
structural mechanism to let the signal override it. The prior and the classifier are the same
weights. The model that reads evidence is the same model that holds expectations about what
that evidence will say. When they conflict, there is no independent arbiter. The system
resolves the conflict internally, and the resolution favors the prior — because the prior is
what has been reinforced across billions of training examples.

This is not a bug in any particular model. It is a structural consequence of homogeneous
systems where prior and perception share the same substrate.

---

## What the Agreement Mechanism Does

In the Reflex, the agreement mechanism is:

> When the LP prior (slow, accumulated, sub-conscious) and TriX (fast, immediate, peripheral)
> agree about what pattern is present — amplify the prior's influence on GIE gate firing.
> When they disagree — release. The prior attenuates to zero. The raw signal reaches the
> sub-conscious layer unmodulated.

This is structurally the opposite of hallucination. Instead of the prior winning when it
conflicts with the signal, the prior yields. The signal dominates at the moment of conflict.
The prior is not discarded — it persists, accumulates, waits. But it does not override
immediate evidence.

The mechanism produces a system that is confident when it should be and open when it
shouldn't be. Confidence is not a global property of the system. It is a local property of
the agreement between layers at this moment, for this input.

---

## Why It Works Here: The Architectural Wall

The agreement mechanism works in the Reflex because of a structural guarantee: `W_f hidden = 0`.

This means the GIE's f_dot computation — the signal that drives TriX classification — is
entirely input-driven. The LP prior does not enter the f_dot computation. Gate bias changes
which neurons fire (affecting gie_hidden) but not the f_dot values and thus not the TriX
scores. TriX cannot be corrupted by the LP prior.

This is not a soft separation. It is an architectural wall. The prior lives in LP SRAM. The
classifier lives in PCNT/PARLIO/GDMA peripheral hardware. The wall between them is the fixed
zero in the W_f weight matrix. They share a common LP SRAM communication channel, but the
classifier's read path is one-way: it reads GIE hidden to produce snapshots for the VDB.
It does not read the LP prior to produce TriX scores.

Because of this wall, when TriX and the LP prior disagree, the disagreement is real
information. TriX is not echoing the LP prior back at it. TriX is reporting what the
peripheral hardware measured about the physical signal — independently, at 705 Hz, without
prior influence. The disagreement cannot be a product of circular reasoning. It means
something in the world changed.

The LP prior can trust that signal. And when it receives that signal — prior ≠ TriX — it
can step back without self-contradiction.

---

## Why Homogeneous Systems Cannot Do This

In a language model, the system that holds priors (the weights, trained on the full corpus)
and the system that reads evidence (the attention mechanism, operating on the context window)
are the same weights. The prior is not separate from the perceptual apparatus. It is the
perceptual apparatus.

This means:

1. **There is no independent signal source.** The model's "perception" of a context token is
   mediated by the same weights that encode its prior expectations about what should follow
   that token. The prior shapes what the model sees. What the model sees reinforces the prior.
   The loop is closed at the weight level.

2. **Disagreement cannot be detected as real information.** If the prior says "Paris" and the
   context token is "Berlin," the model's processing of "Berlin" is already shaped by its
   expectation of "Paris." The conflict is not cleanly surfaced as a disagreement between two
   independent measurements. It is a weighted competition within the same representational
   substrate.

3. **No architectural wall exists.** There is no W_f hidden = 0 equivalent. No part of the
   system is structurally prevented from carrying prior influence. Attention mechanisms attend
   to context, but attention weights are themselves learned — they encode, implicitly, prior
   expectations about which parts of context matter.

The result: when prior and evidence conflict, the resolution is learned, not structural. The
model has learned, across training, how to weight prior vs. evidence. In most cases, the prior
wins — because most training examples were generated by systems (humans writing about what
they know) where prior knowledge correctly predicted the next token. The model is well-trained
for the common case. The common case is not hallucination.

Hallucination is the uncommon case: the prior is confidently wrong about this specific context.
And there is no structural mechanism to detect that "wrong" before the output is produced.

---

## The Architectural Principle

The Reflex instantiates, perhaps accidentally, a design principle with broader applicability:

> **Hallucination resistance requires structural separation between the system that holds
> priors and the system that reads evidence — such that the evidence-reader cannot be
> corrupted by the prior, and the disagreement between them is real information.**

In the Reflex:
- Prior holder: LP CfC (lp_hidden, accumulated over the session)
- Evidence reader: GIE peripheral hardware (GDMA → PARLIO → PCNT, reporting raw dot products)
- Structural separation: W_f hidden = 0 (architectural wall preventing prior from entering the classifier)
- Disagreement detection: TriX prediction vs. LP alignment (agreement signal)
- Resolution: prior defers to evidence at the point of conflict

For a system to resist hallucination structurally — not just statistically — something
analogous to all five of these components must exist. A prior. An evidence reader that the
prior cannot corrupt. A structural guarantee enforcing the separation. A mechanism to detect
disagreement. And a policy that lets the evidence win when they conflict.

---

## What This Does Not Claim

This is not a claim that the Reflex solves hallucination in language models. The architectures
are not directly comparable. Language models operate over discrete token sequences with
autoregressive generation; the Reflex operates over continuous wireless signals with peripheral
hardware classification. The mechanisms are different in almost every detail.

The claim is narrower: the Reflex provides a concrete, silicon-verified example of a system
where prior-signal conflict is detected structurally and resolved in favor of the signal. The
architectural principle behind that — separation, structural guarantee, disagreement detection,
evidence deference — is worth examining as a design pattern for systems where hallucination
is a concern.

A language model with a structurally separate "prior holder" and "evidence reader" — where the
evidence reader's output cannot be shaped by the prior, and where disagreement between them
triggers evidence deference — would resist hallucination by design rather than by statistical
training. Whether such an architecture is feasible at the scale of large language models is an
open question. Whether it is the right question is not.

---

## The Connection to Complementary Learning Systems

This is also the deeper significance of the CLS parallel (documented in `REFLECTION_MAR22.md`
and `KINETIC_ATTENTION.md`). The hippocampus and neocortex are not just two memory systems
with different timescales. They are two systems with structural separation between them — the
hippocampus encodes episodes without the neocortex's interference, and the neocortex extracts
statistics without the hippocampus's interference.

The hippocampus provides exactly what a hallucination-resistant system needs: a fast,
high-fidelity record of what actually happened, stored independently of the slow learner's
prior expectations. When the neocortex's expectation conflicts with what actually happened,
the hippocampus holds the ground truth.

In the Reflex, the VDB plays this role. The VDB stores what the system's state actually was
at each classification moment — independent of the LP CfC's current trajectory. When the LP
prior conflicts with the GIE signal (a pattern switch), the VDB's old-pattern content slows
the update, but the agreement mechanism lets the raw GIE signal through regardless.

The lesson from biology and from the Reflex is the same: systems that need to remain honest
about new evidence while retaining accumulated knowledge need structural separation between
the two, not just a statistical balance between them.

---

## The Research Program

The five-component architecture identifies what would need to be true for a language model to
resist hallucination structurally:

**Component 1 — Prior-holder:** In a language model, this is the trained weights plus any
persistent state (KV cache, retrieved documents). The prior is what the system expects, built
from the training distribution and from the current context processed so far.

**Component 2 — Evidence-reader:** A component that reads the current input token (or token
sequence) with measurements that the prior cannot corrupt. In the Reflex, this is the GIE
peripheral hardware — a physics-level measurement of the wireless signal. In a language model,
this would be some component of the attention mechanism whose activations cannot be shaped by
weight-encoded prior expectations. This is the hardest component to specify at LLM scale,
because attention weights are themselves trained — they encode, implicitly, prior expectations
about which context positions matter.

**Component 3 — Structural separation guarantee:** In the Reflex, this is `W_f hidden = 0`.
No value in the LP prior can reach the f_dot computation through the weight matrix; the path
is architecturally closed. In a language model, an equivalent would be a component whose
forward pass is provably prior-free — perhaps a separate, untrained encoder that reads input
embeddings without passing through any of the trained MLP layers, or an attention head whose
query and key matrices are identity-fixed. The guarantee must be structural: not "the prior
influences this component weakly" but "the prior cannot reach this component at all."

**Component 4 — Disagreement detection:** In the Reflex, this is the comparison between
TriX (the evidence-reader's classification) and the LP alignment (the prior's prediction).
The disagreement is a real number: the trit-dot between the current LP state and the LP
signature of the TriX-predicted pattern. When this number is negative, prior and evidence
conflict. In a language model, an equivalent would be a comparison between what the evidence-
reader predicts will follow and what the prior-holder predicts will follow, measured in a
common representational space. Entropy of their combined distribution, or disagreement in
predicted token probability, are candidate signals.

**Component 5 — Evidence-deference policy:** When components 2 and 4 agree that the evidence
says something the prior didn't expect, the system routes the evidence through without
amplification or suppression by the prior. In the Reflex, this is the agreement-weighted gate
bias: `gate_bias = BASE_GATE_BIAS * max(0, agreement)`. When agreement is zero, gate_bias is
zero, and the GIE computation proceeds on raw inputs only. In a language model, this would be
a gating mechanism that reduces the influence of the prior-holder on the output token
distribution when the evidence-reader and prior-holder disagree. Concretely: when
disagreement is high, weight the evidence-reader's predictions more heavily.

### What Would Need to Exist

A language model with this architecture would not look like current transformer architectures.
It would have at least two distinct processing paths: a prior path (the trained weights,
processing the full context through learned attention) and an evidence path (an evidence-
reader, processing the current token through an untrained or lightly-trained encoder). These
paths would converge at a gating layer that computes their disagreement and adjusts the output
mixture accordingly.

This is not a fundamentally new idea. Retrieval-augmented generation (RAG) is a partial
implementation: the retrieved document is an evidence source that the prior cannot corrupt (it
was retrieved from outside the model's weights). But RAG lacks components 3 and 4: there is
no architectural wall preventing the model's attention from down-weighting the retrieved
document when it conflicts with the prior, and there is no explicit disagreement detection
mechanism that triggers evidence deference. The result is that RAG reduces hallucination
statistically but does not prevent it structurally. The prior can still win.

A complete implementation would require:
1. A frozen or lightly-trained encoder that produces evidence representations without prior
   influence.
2. An architectural wall (analogous to `W_f hidden = 0`) preventing prior-path activations
   from reaching the evidence encoder during inference.
3. A disagreement signal computed from the evidence encoder's prediction vs. the prior path's
   prediction, measured before the output is generated.
4. A gating layer that attenuates the prior path's influence when disagreement is high.

Whether this is feasible at current LLM scale — with hundreds of billions of parameters and
complex, entangled representations — is an open engineering question. The Reflex does not
answer it. What the Reflex provides is the clearest possible statement of what would need to
be true: not better training, not larger scale, not improved prompting — but a different
architecture with structural separation as a design invariant.

### What This Does Not Require

Structural prior-signal separation does not require:
- A separate model (the evidence-reader could be a subset of the existing architecture with
  constrained weight updates)
- Zero prior influence at all times (the prior is valuable; the goal is preventing it from
  overriding evidence, not eliminating it)
- Verification at the token level (disagreement could be detected at sentence or paragraph
  level, depending on what the evidence-reader can resolve)
- Hardware changes (the Reflex's implementation on peripheral hardware is one instantiation;
  the principle is substrate-independent)

What it does require is the structural wall. Without a guarantee that the evidence-reader's
measurements cannot be corrupted by the prior — not weakly influenced, but architecturally
unreachable — the agreement signal in component 4 is not reliable information. It may be
prior influence all the way down.

---

## Conclusion

Hallucination in language models is not primarily a training problem or a data problem. It is
a structural problem: the system that holds priors and the system that reads evidence are the
same weights, and when they conflict, there is no independent arbiter.

The Reflex demonstrates, on fifty cents of hardware drawing thirty microamps, that structural
separation is achievable — not by eliminating the prior (the LP CfC accumulates and uses its
prior), but by building the evidence-reader in a different substrate (the GIE peripheral
hardware) with a fixed architectural wall between them (`W_f hidden = 0`). The wall is what
makes the agreement signal in component 4 real information: when TriX and the LP prior
disagree, the disagreement cannot be circular, because TriX cannot access the LP prior.

The five-component architecture is the abstraction: prior-holder, evidence-reader, structural
separation guarantee, disagreement detection, evidence-deference policy. The Reflex is one
instantiation. The research program is to find others — and to ask whether a language model
with structural prior-signal separation would behave differently from one without.

The answer is almost certainly yes. The question is how to build it.

*The prior should be a voice, not a verdict.*

---

**Date**: March 22–23, 2026. Updated April 7, 2026.
**Hardware basis**: ESP32-C6, 14/14 PASS (commit `f510f9a`), ablation-controlled, red-team remediated
**Key commits**: `12aa970` (TEST 12/13), `429ce38` (Phase 5), `98800a9` (MTFP dot encoding), `f510f9a` (red-team fixes)

**Silicon verification of the five components:**
1. Prior-holder: LP CfC hidden state. Pattern-specific after 90s. VDB causally necessary (TEST 13 ablation).
2. Evidence-reader: GIE peripheral hardware. 430 Hz, 100% TriX accuracy, zero prior influence on classification.
3. Structural separation: `W_f hidden = 0`. Verified: TriX ISR and CPU classification agree at 100% (`0b09f69`).
4. Disagreement detection: Agreement score drives gate bias. MTFP-space (80 trits) provides 5× measurement resolution.
5. Evidence-deference policy: gate_bias ≤ 15, hard floor 30. Red-teamed: MTFP agreement caused entrainment (runaway feedback); reverted to sign-space agreement for stability. The deference mechanism works because the agreement signal is conservative.

**Depends on**: `KINETIC_ATTENTION.md`, `PAPER_READINESS.md`
**See also**: `THE_PRIOR_AS_VOICE.md` (full perspective paper)
**Status**: Ready for submission. All five components silicon-verified under confound-controlled conditions.
