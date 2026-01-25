# PRD: Crystallization

> Converting discovered correlations into permanent knowledge.

## Problem

The layered exploration discovers relationships (GPIO 0 → ADC0 delta of -3200), but this knowledge exists only in floating-point EMA/variance arrays. On reset, everything is forgotten. The C6 must re-explore.

## Goal

When a correlation is strong enough and consistent enough, **crystallize** it into a permanent association that:
1. Survives reset (stored in NVS flash)
2. Guides future exploration (don't re-explore the known)
3. Enables prediction ("if I toggle GPIO0, ADC0 will change by ~3200")

## Crystallization Criteria

A correlation qualifies for crystallization when:

```
confidence = consistency × magnitude × observations

where:
  consistency = 1.0 - (variance / mean²)    # Low variance = consistent
  magnitude   = |mean_delta| / max_delta    # Large effect = meaningful
  observations = min(count, 100) / 100      # Enough samples
```

**Threshold:** `confidence > 0.8` triggers crystallization.

## Data Structure

```c
typedef struct __attribute__((packed)) {
    uint8_t  output_idx;      // Which output (0-7)
    uint8_t  input_idx;       // Which input (0-12)
    int16_t  expected_delta;  // Mean observed delta
    uint8_t  confidence;      // 0-255 scaled
    uint8_t  direction;       // 0=negative, 1=positive correlation
    uint16_t observations;    // Times confirmed
} crystal_t;

#define MAX_CRYSTALS 32
```

Total: 8 bytes × 32 = 256 bytes in NVS.

## API

```c
// Check if correlation is crystallized
crystal_t* crystal_lookup(uint8_t output, uint8_t input);

// Attempt to crystallize a correlation
bool crystal_try(uint8_t output, uint8_t input, float mean, float var, uint32_t count);

// Confirm/update existing crystal
void crystal_confirm(crystal_t* c, int16_t observed_delta);

// Load from NVS on boot
void crystal_load(void);

// Save to NVS when crystallization occurs
void crystal_save(void);
```

## Integration with Exploration

In `layer_analyze()`:

```c
// If this output→input is crystallized, reduce exploration priority
crystal_t* c = crystal_lookup(output, input);
if (c && c->confidence > 200) {
    // Known relationship - less interesting to explore
    interest *= 0.5f;
}
```

In `layer_update()`:

```c
// After observing delta, try to crystallize
for (int i = 0; i < NUM_INPUTS; i++) {
    crystal_try(output, i, l->ema[output][i], l->var[output][i], obs);
}
```

## Prediction (Bonus)

Once crystallized, predictions become possible:

```c
int16_t predict_delta(uint8_t output, uint8_t input) {
    crystal_t* c = crystal_lookup(output, input);
    if (c) return c->expected_delta;
    return 0;  // Unknown
}
```

## Falsification

1. **Does crystallization happen?** Run 500 ticks, check if any crystals formed.
2. **Are crystals accurate?** Compare predicted vs observed deltas.
3. **Does it survive reset?** Reboot, verify crystals loaded from NVS.
4. **Does it guide exploration?** After crystallization, exploration should shift to unknowns.

## Success Metrics

| Metric | Target |
|--------|--------|
| Crystals formed (GPIO→ADC) | ≥4 (the known connections) |
| Prediction accuracy | >90% within ±500 of expected |
| Post-reset crystal count | Same as pre-reset |
| Exploration shift | 50% reduction in re-exploring crystallized pairs |

## Files

| File | Purpose |
|------|---------|
| `reflex_crystal.h` | Crystal structures, NVS interface |
| Update `reflex_layers.h` | Integration with exploration |
| Update `layers_main.c` | Init, periodic save |

## Non-Goals

- Complex inference (just direct correlations)
- Multi-step reasoning (GPIO0 → ADC0 → something else)
- Forgetting (once crystallized, permanent)

---

*"Knowledge isn't just observation. It's observation that persists."*
