# Synthesis: The Reflex Evolution

> *The Reflexor should be a shape. A very stable shape. But still a shape.*

---

## Architecture: The Unified Anomaly Substrate

```
┌─────────────────────────────────────────────────────────────────────┐
│                    THE EVOLVED REFLEX                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    ENTROPY FIELD                             │    │
│  │                                                              │    │
│  │   Carries TWO signals (interference):                        │    │
│  │   • Silence gradient (absence of expected activity)          │    │
│  │   • Surprise deposits (Reflexor prediction errors)           │    │
│  │                                                              │    │
│  │   High silence + high surprise = contradiction → investigate │    │
│  │   High silence + low surprise  = normal quiet                │    │
│  │   Low silence  + high surprise = novel active pattern        │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                 REFLEXOR-AS-SHAPE                            │    │
│  │                                                              │    │
│  │   ┌──────────┐  ┌──────────┐  ┌──────────┐                  │    │
│  │   │ Reflexor │  │ Reflexor │  │ Reflexor │  (specializations)│    │
│  │   │ τ=fast   │  │ τ=med    │  │ τ=slow   │                  │    │
│  │   │ v=0.94   │  │ v=0.87   │  │ v=0.71   │  (vitality)      │    │
│  │   └──────────┘  └──────────┘  └──────────┘                  │    │
│  │         │              │             │                       │    │
│  │   Subject to echip dynamics:                                 │    │
│  │   • Crystallize from persistent anomaly patterns             │    │
│  │   • Strengthen through successful detection                  │    │
│  │   • Weaken through false positives / misses                  │    │
│  │   • Dissolve when vitality < threshold                       │    │
│  │   • Seed children when vitality > spawn_threshold            │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              HEBBIAN ATTENTION ROUTES                        │    │
│  │                                                              │    │
│  │   Channels ──routes──▶ Reflexors                             │    │
│  │                                                              │    │
│  │   Route weight strengthens when:                             │    │
│  │   • Channel signal correlates with Reflexor activation       │    │
│  │                                                              │    │
│  │   Route weight weakens when:                                 │    │
│  │   • Channel signal uncorrelated with Reflexor activation     │    │
│  │                                                              │    │
│  │   Result: Emergent attention without parameters              │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              ENTROPY-SEEDED PHANTOMS                         │    │
│  │                                                              │    │
│  │   L3 Imagination phantom generation:                         │    │
│  │                                                              │    │
│  │   1. Sample entropy field for high-gradient cells            │    │
│  │   2. Weight by gradient magnitude (more entropy = more vote) │    │
│  │   3. Spawn phantoms exploring high-entropy possibility space │    │
│  │   4. Nightmares (high-surprise phantoms) stay resident       │    │
│  │   5. Boring phantoms evicted                                 │    │
│  │                                                              │    │
│  │   The field guides imagination toward interesting futures    │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              DUAL PREDICTION ERROR                           │    │
│  │                                                              │    │
│  │   Spline: Where will the signal be? (geometric)              │    │
│  │   CfC:    Is the signal behaving normally? (dynamic)         │    │
│  │                                                              │    │
│  │   Combined anomaly = f(spline_error, cfc_error)              │    │
│  │                                                              │    │
│  │   Cases:                                                     │    │
│  │   • Spline correct, CfC wrong: position normal, dynamics off │    │
│  │   • Spline wrong, CfC correct: position off, dynamics normal │    │
│  │   • Both wrong: major anomaly                                │    │
│  │   • Both correct: all clear                                  │    │
│  │                                                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Key Decisions

| Decision | Rationale |
|----------|-----------|
| Reflexor becomes echip shape | Completes the philosophy: all structure emerges from and returns to the substrate |
| Vitality replaces frozen state | Stability through utility, not privilege. Successful detection = survival |
| Prediction error deposits into entropy field | Unifies silence and surprise into single anomaly geography |
| Hebbian routes replace attention parameters | Emergence over mechanism. No training loop needed |
| Entropy gradients seed phantoms | Possibility space guides imagination. High entropy = worth exploring |
| Spline + CfC dual prediction | Orthogonal anomaly axes: geometric and dynamic |

---

## Implementation Spec

### 1. Reflexor Vitality

```c
typedef struct {
    reflex_channel_t base;        // Existing 50-node CfC
    float vitality;               // 0.0 to 1.0
    uint32_t true_positives;      // Successful detections
    uint32_t false_positives;     // Cried wolf
    uint32_t misses;              // Failed to detect known anomaly
    float tau;                    // Time constant (specialization)
} reflexor_shape_t;

// After each detection cycle
void reflexor_update_vitality(reflexor_shape_t* r, detection_result_t result) {
    switch(result) {
        case TRUE_POSITIVE:  r->vitality += 0.01; r->true_positives++; break;
        case FALSE_POSITIVE: r->vitality -= 0.05; r->false_positives++; break;
        case MISS:           r->vitality -= 0.03; r->misses++; break;
        case TRUE_NEGATIVE:  /* no change */ break;
    }
    r->vitality = clamp(r->vitality, 0.0, 1.0);
}

#define VITALITY_DISSOLVE_THRESHOLD  0.2
#define VITALITY_SPAWN_THRESHOLD     0.95
```

### 2. Unified Entropy Field

```c
typedef struct {
    float silence;      // Accumulated absence of expected signal
    float surprise;     // Accumulated prediction error deposits
    float gradient_x;   // Computed each tick
    float gradient_y;
} entropy_cell_t;

// Interference score for anomaly geography
float interference_score(entropy_cell_t* cell) {
    // High silence AND high surprise = contradiction
    return cell->silence * cell->surprise;
}
```

### 3. Hebbian Attention Routes

```c
typedef struct {
    uint8_t channel_id;
    uint8_t reflexor_id;
    float weight;           // Strengthens with correlation
    float recent_channel;   // Rolling average of channel activity
    float recent_reflexor;  // Rolling average of reflexor activation
} attention_route_t;

void hebbian_update(attention_route_t* route, float channel_val, float reflexor_val) {
    // Correlation-based update
    float correlation = channel_val * reflexor_val;
    route->weight += HEBBIAN_RATE * (correlation - route->weight * DECAY);
    route->weight = clamp(route->weight, 0.0, 1.0);
}
```

### 4. Entropy-Seeded Phantom Generation

```c
phantom_t* spawn_phantom(entropy_field_t* field) {
    // Find high-gradient cells
    entropy_cell_t* candidates[MAX_CANDIDATES];
    int n = find_high_gradient_cells(field, candidates, MAX_CANDIDATES);
    
    // Weight by gradient magnitude
    float total_weight = 0;
    for (int i = 0; i < n; i++) {
        total_weight += gradient_magnitude(candidates[i]);
    }
    
    // Probabilistic selection
    float r = random_float() * total_weight;
    entropy_cell_t* seed = select_weighted(candidates, n, r);
    
    // Spawn phantom exploring that region
    return phantom_from_entropy_cell(seed);
}
```

---

## Success Criteria

- [ ] Reflexor vitality tracks detection accuracy over time
- [ ] Low-vitality Reflexors dissolve, high-vitality ones spawn children
- [ ] Entropy field carries both silence and surprise signals
- [ ] Interference score identifies contradiction regions
- [ ] Hebbian routes emerge without explicit attention training
- [ ] Phantoms preferentially explore high-entropy regions
- [ ] Spline + CfC dual prediction detects orthogonal anomaly types
- [ ] Sub-microsecond performance preserved (vitality update is O(1))

---

## The Emergence

What emerges from these changes:

1. **Self-maintaining detection**: Reflexors that work survive. Those that don't, dissolve. No manual tuning.

2. **Automatic specialization**: Child Reflexors inherit parent's tau but can drift. Population evolves toward useful timescales.

3. **Anomaly memory**: The entropy field remembers where trouble happened. New Reflexors crystallize at trouble spots.

4. **Attention without training**: Channels that matter get strong routes. Channels that don't, fade. Hebbian dynamics replace gradient descent.

5. **Guided imagination**: Phantoms explore where the field says possibility lives. Nightmares persist. Boring futures evict.

The system becomes self-organizing. Structure grows where it's needed and dissolves where it's not. The observer is part of the observed.

---

*The wood cuts itself.*
