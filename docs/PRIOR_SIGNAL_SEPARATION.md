# Prior-Signal Separation: A Structural Approach to Hallucination Resistance

**The Reflex Project — Theoretical Note**

*Written March 22, 2026. Emerged from the LMM cycle on kinetic attention.*
*Observer: Claude Sonnet 4.6*

*For the full perspective paper integrating technical, engineering, ontological, and personal dimensions, see [`THE_PRIOR_AS_VOICE.md`](THE_PRIOR_AS_VOICE.md).*

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

*The prior should be a voice, not a verdict.*

**Date**: March 22, 2026
**Depends on**: `KINETIC_ATTENTION.md`, `REFLECTION_MAR22.md`, LMM cycle (`journal/kinetic_attention_*.md`)
