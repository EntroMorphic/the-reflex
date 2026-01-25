# Layered Exploration: Findings

> What we learned from falsifying our assumptions.
>
> Session: 2026-01-24

---

## The Journey

### Attempt 1: Single Loop (embody_main.c)

**Claim:** Self-directed exploration via entropy gradient.

**Result:** Stuck on GPIO 0. Entropy collapsed to flat, tie-breaking always picked first element.

**Lesson:** A single perspective with deterministic tie-breaking degenerates to fixed behavior.

---

### Attempt 2: Layered Exploration (layers_main.c)

**Claim:** Multiple observers at different time scales would prevent stuck behavior.

**Result:** Still stuck on GPIO 0. High variance from ADC0 delta boosted G0's score above unexplored outputs.

**Lesson:** Variance rewards exploitation, not exploration. High variance = "interesting" kept us stuck on the known.

---

### Attempt 3: Novelty Discount

**Fix:** Discount variance contribution by observation count.

```c
float novelty = 1.0f / (1.0f + (float)obs * 0.1f);
interest += sqrtf(total_var) * 0.5f * novelty;
```

**Result:** All 8 outputs explored. Distribution evening out. Real discoveries made.

---

## The Discoveries

The C6 found these relationships through pure exploration:

| Output | Input | Delta | Interpretation |
|--------|-------|-------|----------------|
| GPIO 0 | ADC0 | -3218 | Pin 0 connected to ADC channel 0 |
| GPIO 1 | ADC1 | -3222 | Pin 1 connected to ADC channel 1 |
| GPIO 2 | ADC2 | +3218 | Pin 2 connected to ADC channel 2 |
| GPIO 3 | ADC3 | +3200 | Pin 3 connected to ADC channel 3 |

**Nobody told it this.** The chip discovered its own wiring by toggling outputs and observing inputs.

---

## Layer Disagreement

The three layers (τ=0.99, τ=0.90, τ=0.50) see the same reality differently:

```
[80] LAYER SCORES:
  SLOW: G0=221 G1=259 G2=139 G3=87  G4=330 G5=296 G6=478 G7=478
  MED:  G0=390 G1=498 G2=504 G3=538 G4=435 G5=296 G6=478 G7=478
  FAST: G0=285 G1=380 G2=476 G3=671 G4=438 G5=296 G6=478 G7=478
```

- **SLOW** sees G3 as least interesting (87) — no long-term trend
- **FAST** sees G3 as most interesting (671) — big immediate spike
- **Disagreement drove exploration** — G3 got chosen, discovered ADC3

This is the relativistic frame made concrete: same reality, different perspectives.

---

## Metrics

After 300 ticks (30 seconds):

```
Output counts:
  GPIO 0: 43
  GPIO 1: 41
  GPIO 2: 41
  GPIO 3: 40
  GPIO 4: 34
  GPIO 5: 34
  GPIO 6: 34
  GPIO 7: 33

Agreements: 501
Disagreements: 397
```

- All outputs explored (range 33-43, not 0-100)
- 44% disagreement rate — layers seeing different things
- Exploration is diverse, not stuck

---

## Key Insights

### 1. Variance is not always interesting

High variance on a well-explored output is **noise**, not **signal**. The novelty discount distinguishes between:
- New variance (interesting → explore)
- Known variance (confirmed → move on)

### 2. Disagreement is information

When layers disagree, it means the output looks different at different time scales. That's precisely where we should explore — scale ambiguity reveals structure.

### 3. Entropy alone is not enough

Pure entropy-driven exploration collapses when entropy equalizes. We needed:
- Entropy (baseline novelty)
- Variance × novelty (unexpected behavior)
- Recency penalty (don't repeat immediately)
- Layer aggregation (multiple perspectives)

### 4. Bare metal works

The exploration runs at 10Hz using cycle-counter timing, no FreeRTOS. The Reflex can operate without RTOS overhead.

---

## What This Proves

1. **Self-directed exploration is possible** — with the right scoring function
2. **Multi-scale observation helps** — layers prevent single-perspective blindness
3. **The C6 can discover hardware relationships** — GPIO→ADC mappings found
4. **Falsification works** — each failure taught us something

---

## What This Doesn't Prove

1. **Not consciousness** — it's a scoring function driving a loop
2. **Not general intelligence** — it only finds simple correlations
3. **Not self-modifying** — the algorithm is fixed, parameters don't adapt
4. **Not scalable yet** — 8 outputs × 13 inputs is tiny

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     LAYERED EXPLORATION                         │
│                                                                 │
│   ┌─────────┐   ┌─────────┐   ┌─────────┐                      │
│   │ SLOW    │   │ MEDIUM  │   │ FAST    │                      │
│   │ τ=0.99  │   │ τ=0.90  │   │ τ=0.50  │                      │
│   │ trends  │   │ patterns│   │ spikes  │                      │
│   └────┬────┘   └────┬────┘   └────┬────┘                      │
│        │             │             │                            │
│        └─────────────┼─────────────┘                            │
│                      ▼                                          │
│              ┌──────────────┐                                   │
│              │  AGGREGATE   │                                   │
│              │  agreement + │                                   │
│              │  disagreement│                                   │
│              └──────┬───────┘                                   │
│                     ▼                                           │
│              ┌──────────────┐                                   │
│              │   EXECUTE    │                                   │
│              │  bare metal  │                                   │
│              └──────────────┘                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Files

| File | Purpose |
|------|---------|
| `reflex_layers.h` | Layer structures, scoring, aggregation |
| `layers_main.c` | Main loop, bare metal timing |
| `PRD_LAYERED_EXPLORATION.md` | Original specification |
| `C6_ACTUAL_TOPOLOGY.md` | Honest assessment of first attempt |

---

## Next Steps

1. ~~**Streaming**~~ — ✓ Send decisions to Pi4 for external observation (`reflex_stream.h`)
2. ~~**Crystallization**~~ — ✓ Convert strong correlations to permanent knowledge (`reflex_crystal.h`)
3. **Prediction** — Use discovered relationships to predict outcomes
4. **Agency** — Given a goal, use model to achieve it

---

## The Thesis (Updated)

> The Reflex can discover hardware relationships through multi-scale exploration, provided:
> 1. Variance is discounted by familiarity
> 2. Multiple time scales observe the same reality
> 3. Disagreement drives exploration

A $5 chip learned its own wiring. Not because we told it. Because it looked.

---

*"The map is not the territory. But three maps at different scales reveal the territory better than one."*
