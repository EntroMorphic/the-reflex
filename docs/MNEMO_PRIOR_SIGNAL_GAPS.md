# Mnemo: Closing the Prior-Signal Separation Gaps

**The Reflex Project — Cross-Project Analysis**

*Written March 23, 2026.*
*Context: `docs/PRIOR_SIGNAL_SEPARATION.md` describes a five-component architecture for structural*
*hallucination resistance. This document analyzes where Mnemo currently stands against that*
*architecture and specifies concrete changes to complete it.*

---

## Where Mnemo Already Is

Before the gaps: what Mnemo gets right is substantial and worth naming precisely.

**Component 1 (Prior-Holder):** Mnemo has a sophisticated one. `identity_core` is the slow-changing prior — the agent's stable self-model. The `experience` layer is the fast-changing accumulator — what the agent has learned from recent interactions. EWC++ (Elastic Weight Consolidation) applies decay resistance to high-importance experiences, preserving them against temporal erosion while allowing low-importance ones to fade. This is the slow-learner / fast-learner split from Complementary Learning Systems theory, implemented at the agent level. It is not an accident. It is correct architecture.

**Component 2 (Evidence-Reader):** Partially present. The full-text channel (RediSearch) retrieves based on literal terms — no neural processing, no learned relevance rotation. If kept isolated, this is a prior-free evidence source. The graph traversal channel is also partially prior-free: it follows edges in the knowledge graph without embedding-based influence. The semantic channel (Qdrant), however, is prior-influenced — details below.

**Component 3 (Structural Separation Guarantee):** The identity_core wall is the strongest signal that the right problem is being solved. The separation contract — "no write path may place user facts in `identity_core`" — is enforced by both application logic and a cryptographic audit chain (SHA-256 Merkle-style witness). This is meaningfully structural. It enforces the boundary against writes. What it does not yet enforce is the boundary against *reads shaping evidence* — which is where the TinyLoRA gap lives.

**Embryonic Component 4:** Conflict detection in the knowledge graph is already implemented with severity scoring and a resolution queue. When two stored facts contradict each other, Mnemo surfaces it. This is the right instinct. The gap is that it operates on stored-fact vs stored-fact conflicts, not retrieval vs model-prior conflicts — which is where hallucination actually happens.

**Component 5:** Not yet present in any form.

---

## Gap 1: The Query Vector Is Prior-Shaped (Structural Separation)

This is the root gap. Everything else follows from it.

### The Problem

TinyLoRA personalization applies per-`(user, agent)` rank-8 LoRA adapters that "rotate base embeddings toward observed relevance history." This means the query vector sent to Qdrant is not a neutral measurement of the current input. It is the current input *rotated toward what has historically been relevant*. The prior is shaping what the evidence-reader retrieves before the retrieval even begins.

The consequence: when something changes — a user's situation shifts, a fact is superseded, a new pattern appears — the LoRA-rotated query still points toward the old relevance topology. Mnemo retrieves memories that match the prior, not memories that match the current evidence. This is the hallucination mechanism at the retrieval layer. The system finds what it expects to find.

This is not a criticism of TinyLoRA as a feature. For the common case — where the prior is approximately correct — LoRA-based personalization makes retrieval sharper and more useful. The problem is the uncommon case: when the prior is wrong. There is currently no mechanism to detect that the prior is wrong before the retrieval is poisoned by it.

### The Fix: The Literal Retrieval Channel

Mnemo already has a full-text channel in Redis that does not use embeddings. This is the evidence-reader — it just has not been positioned as such.

**Change 1: Separate the retrieval channels at the API boundary.**

Do not fuse semantic + full-text + graph into a single reranked list before returning. Return them as separate, named components in the context response:

```json
{
  "context": "...",
  "retrieval_channels": {
    "literal": {
      "source": "redis_fulltext",
      "lora_applied": false,
      "results": [...]
    },
    "semantic": {
      "source": "qdrant",
      "lora_applied": true,
      "lora_rotation_magnitude": 0.34,
      "results": [...]
    },
    "graph": {
      "source": "graph_traversal",
      "results": [...]
    }
  }
}
```

The `literal` channel is the evidence-reader. It must never have LoRA applied. The `semantic` channel is explicitly the prior-influenced channel. Naming them honestly at the API boundary is the first structural act.

**Change 2: The LoRA Gate.**

TinyLoRA should be gated by an agreement signal between the raw embedding and the LoRA-rotated embedding, analogous to the Reflex's agreement-weighted gate bias:

```
lora_rotation_magnitude = cosine_distance(raw_embedding, lora_rotated_embedding)
agreement = 1.0 - lora_rotation_magnitude  # 1.0 = no rotation, 0.0 = full rotation

effective_lora_scale = BASE_LORA_SCALE * max(0, agreement - LORA_GATE_THRESHOLD)
```

When the LoRA rotation is large (prior is pulling the query strongly away from the raw signal), the effective LoRA scale drops toward zero. The query reverts to the raw embedding — closer to the literal evidence. When the rotation is small (prior and evidence are aligned), LoRA amplifies as designed.

This is the direct analog of `gate_bias = BASE_GATE_BIAS * max(0, agreement)` from the Reflex's kinetic attention design. The mechanism is: **confidence when prior and evidence agree; openness when they diverge.**

The `lora_rotation_magnitude` should be included in the context response (it is currently not). It is a signal, not just an implementation detail.

---

## Gap 2: No Disagreement Detection (Component 4)

### The Problem

When Mnemo's semantic channel and literal channel return different facts about the same query subject, that disagreement is currently consumed by the fusion math (RRF, MMR, or GNN reranking). The output is a single blended list. The disagreement signal is discarded before the agent ever sees it.

Mnemo already detects contradictions between stored facts. The missing step is detecting contradictions between *what retrieval channels say*, which is where the conflict with the agent's trained prior manifests.

### The Fix: `retrieval_disagreement_score`

After retrieval, before context assembly, compare the top-N results from the literal channel against the top-N results from the semantic channel. If they involve the same entities but return conflicting facts (different values for the same edge label, or facts with overlapping validity windows that contradict each other), compute a disagreement score:

```
retrieval_disagreement_score = f(
    entity_overlap(literal_top_N, semantic_top_N),
    fact_contradiction_count(literal_top_N, semantic_top_N),
    lora_rotation_magnitude
)
```

Where:
- `entity_overlap` — how many of the same entities appear in both channels (high overlap = channels are talking about the same things)
- `fact_contradiction_count` — how many of those entities have conflicting facts between channels (uses the existing conflict detection logic)
- `lora_rotation_magnitude` — how much the prior is pulling the semantic query away from the raw signal

The resulting `retrieval_disagreement_score` (0.0 to 1.0) is returned in the context response:

```json
{
  "context": "...",
  "retrieval_disagreement_score": 0.73,
  "disagreement_details": {
    "conflicting_entities": ["Kendra's shoe preference"],
    "literal_says": "Kendra switched to Nike (valid_at: Feb 2025)",
    "semantic_says": "Kendra prefers Adidas (valid_at: Aug 2024, invalidated)",
    "lora_rotation_magnitude": 0.41
  }
}
```

This surfaces what would otherwise be silent. The agent — and the human reading the response — can now see that a conflict exists and where it came from.

The existing conflict detection infrastructure already knows how to identify contradicting edges. The new work is: (1) running that detection across channel outputs rather than just stored facts, and (2) exposing the result in the response payload.

---

## Gap 3: No Evidence-Deference Policy (Component 5)

### The Problem

When retrieval disagrees with the model's prior, there is currently no mechanism to ensure the evidence wins. The agent receives a context window containing (potentially contradicting) information and resolves the conflict internally — using its trained weights, which favor the prior. Mnemo has no say in that resolution.

### The Fix: `deference_policy` with Structured Context Assembly

Add a `deference_policy` parameter to the context endpoint:

```json
POST /api/v1/users/:id/context
{
  "messages": [...],
  "deference_policy": "guided"
}
```

Three modes:

**`none` (current behavior):** Channels are fused and returned as a single ranked list. No deference annotation. Default for backward compatibility.

**`guided` (soft deference):** When `retrieval_disagreement_score` exceeds a threshold (e.g., 0.5), context assembly changes in two ways:

1. The literal channel results are promoted to the front of the assembled context, labeled explicitly as authoritative.
2. A deference annotation is prepended to the context block:

```
[MEMORY AUTHORITY]
The following facts were retrieved from direct storage records. They supersede what
you would predict from prior context. Where they conflict with your expectations,
defer to what is written here.

[LITERAL RETRIEVAL]
Kendra switched to Nike shoes (confirmed Feb 2025).

[SEMANTIC RETRIEVAL — lower authority when contradicting above]
Kendra historically preferred Adidas (valid Aug 2024 – Feb 2025, now superseded).
```

This is a soft wall. It relies on the LLM following the instruction. It is not mathematical. But it is far better than the current situation, where the LLM receives conflicting signals with no guidance on which to trust — and defaults to the prior by training.

**`strict` (hard deference):** When `retrieval_disagreement_score` exceeds the threshold, only the literal channel results are included in the assembled context. The semantic channel results are suppressed entirely. The agent sees only what direct measurement provided.

This is the closest software can come to `W_f hidden = 0` without modifying the model architecture: structurally prevent the prior-influenced retrieval from reaching the generation path when the channels disagree. The agent cannot be misled by the semantic channel because the semantic channel is not in the context.

The trade-off: `strict` mode sacrifices recall when the prior happens to be right (the common case). `guided` mode preserves recall while signaling the conflict. The right choice depends on the application's risk profile — medical or legal agents should default to `strict`; conversational agents can use `guided`.

---

## The Honest Limit

It is worth being precise about what these changes achieve and what they cannot.

**What they achieve:** Independence of the evidence-reader up to the context window boundary. When `deference_policy: strict` is active and `retrieval_disagreement_score` is high, the context window contains only what direct measurement (the literal channel) provided. The semantic channel — the prior-influenced path — has been excluded from the generation input. The LLM generates from evidence, not from prior-shaped retrieval.

**What they cannot achieve:** The LLM's trained weights still encode prior expectations. Even with a perfectly prior-free context, the model's generation is shaped by its full training distribution. There is no software-level equivalent of a fixed zero in a weight matrix. The wall that the Reflex achieves mathematically — `W_f hidden = 0`, a permanent architectural barrier — requires either modifying the model architecture (a separate processing path with frozen or lightly-trained weights) or accepting that the generation step operates over the full prior regardless.

The practical implication: these changes make Mnemo the best available software instantiation of the five-component architecture. They do not make it equivalent to silicon-level structural separation. The remaining gap lives inside the model, not inside Mnemo.

Closing that gap fully requires what the prior-signal separation note describes as the open research program: a model with a structurally separate evidence-reader path — a component whose activations during inference cannot be shaped by the same weights that encode prior expectations. That is a model architecture question. Mnemo cannot solve it alone. But Mnemo can make the context the model receives as evidence-first as software allows — which is a genuine and meaningful contribution.

---

## Summary of Proposed Changes

| Change | Addresses | Complexity |
|--------|-----------|------------|
| Separate retrieval channels in API response (`literal` / `semantic` / `graph`) | Structural separation visibility | Low — output format change |
| LoRA gate: suppress LoRA when rotation magnitude is high | Structural separation (query construction) | Medium — modify query path |
| `lora_rotation_magnitude` in context response | Transparency | Low — instrument existing code |
| `retrieval_disagreement_score` computed post-retrieval | Component 4 (Disagreement Detection) | Medium — cross-channel comparison |
| `disagreement_details` in context response | Component 4 (Transparency) | Low — format existing data |
| `deference_policy: guided` — literal channel promoted, deference annotation prepended | Component 5 (Soft Deference) | Medium — context assembly logic |
| `deference_policy: strict` — semantic channel suppressed when disagreement high | Component 5 (Hard Deference) | Low once guided is implemented |

### What Already Exists and Just Needs Positioning

- Full-text Redis channel is already a prior-free evidence-reader — it needs to be kept isolated and labeled as authoritative
- Knowledge graph conflict detection already works — extend it to run on channel outputs, not just stored facts
- Temporal reranking diagnostics already in the response — `retrieval_disagreement_score` follows the same pattern

---

## Connection to the Five-Component Architecture

After these changes, Mnemo would instantiate all five components:

| Component | Current Mnemo | After Changes |
|-----------|--------------|---------------|
| 1. Prior-Holder | `identity_core` + `experience` + EWC++ | Unchanged — already strong |
| 2. Evidence-Reader | Partial (full-text exists but fused with prior-influenced channels) | Isolated `literal` channel, LoRA-gated |
| 3. Structural Separation Guarantee | Cryptographic write-wall on `identity_core` (strong); query construction (missing) | LoRA gate closes the query construction gap |
| 4. Disagreement Detection | Knowledge graph contradiction detection (stored facts only) | `retrieval_disagreement_score` extends to retrieval-time conflicts |
| 5. Evidence-Deference Policy | None | `deference_policy` with three modes |

The resulting system would be the first production software implementation of the complete prior-signal separation architecture — not because it achieves the mathematical guarantee of silicon, but because it implements all five components at software scale, with the explicit acknowledgment of where the soft wall lives and why.

---

## A Note on TinyLoRA's Role After These Changes

TinyLoRA does not need to be removed. It becomes explicitly what it is: the prior-holder's influence on retrieval. With the LoRA gate in place, TinyLoRA amplifies retrieval quality when prior and evidence agree, and steps back when they diverge. This is kinetic attention at the retrieval layer — the same mechanism as the Reflex's gate bias, applied to embedding rotation rather than gate threshold adjustment.

The Reflex's formulation: `gate_bias = BASE_GATE_BIAS * max(0, agreement)`

The Mnemo equivalent: `effective_lora_scale = BASE_LORA_SCALE * max(0, agreement - threshold)`

The mechanism is the same. The substrate is different. Fungible computation — the computation is not bound to its substrate; it is an algorithm expressed in available structure.

---

**Date**: March 23, 2026
**Addresses**: `docs/PRIOR_SIGNAL_SEPARATION.md` (five-component architecture)
**System analyzed**: `github.com/anjaustin/mnemo` (v0.5.5, as of March 19, 2026)
**Author**: Claude Sonnet 4.6, with The Reflex Project
