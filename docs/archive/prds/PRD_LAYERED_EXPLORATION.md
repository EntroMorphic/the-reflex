# PRD: Layered Exploration on ESP32-C6

> Multiple observers, same reality. Disagreement is signal.
>
> Created: 2026-01-24

---

## Problem Statement

The single-loop exploration architecture degenerates to fixed behavior:
- Entropy collapses to flat
- Tie-breaking picks the same output every time
- No multi-scale awareness
- System gets stuck, can't notice it's stuck

---

## Solution: Layered Exploration

Replace the single perspective with parallel observers at different scales.

```
           ┌→ Layer 1 (slow,  coarse, far)   ─┐
    State ─┼→ Layer 2 (medium, medium, medium) ┼→ Aggregate → Action
           └→ Layer 3 (fast,  fine,   near)  ─┘
```

Each layer sees the same underlying state but through different lenses.
Disagreement between layers drives exploration.

---

## Architecture

### Layer Parameters

| Layer | τ (decay) | Window | Resolution | Role |
|-------|-----------|--------|------------|------|
| L1 | 0.99 | 16 ticks | 4-bit | Slow trends (temperature, drift) |
| L2 | 0.90 | 4 ticks | 8-bit | Medium patterns (correlations) |
| L3 | 0.50 | 1 tick | 12-bit | Immediate response (ADC spikes) |

### Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     SHARED STATE                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  memory_buffer[16]  - Recent (action, observation) pairs │   │
│  │  current_inputs     - GPIO, ADC, temp readings           │   │
│  │  output_states[8]   - Current state of each output       │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│          ┌───────────────────┼───────────────────┐              │
│          ▼                   ▼                   ▼              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │   LAYER 1    │    │   LAYER 2    │    │   LAYER 3    │      │
│  │              │    │              │    │              │      │
│  │ τ = 0.99     │    │ τ = 0.90     │    │ τ = 0.50     │      │
│  │ window = 16  │    │ window = 4   │    │ window = 1   │      │
│  │              │    │              │    │              │      │
│  │ Sees: slow   │    │ Sees: medium │    │ Sees: fast   │      │
│  │ trends       │    │ patterns     │    │ changes      │      │
│  │              │    │              │    │              │      │
│  │ scores[8]    │    │ scores[8]    │    │ scores[8]    │      │
│  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘      │
│         │                   │                   │               │
│         └───────────────────┼───────────────────┘               │
│                             ▼                                   │
│                    ┌──────────────┐                             │
│                    │  AGGREGATOR  │                             │
│                    │              │                             │
│                    │ agreement =  │                             │
│                    │   signal     │                             │
│                    │              │                             │
│                    │ disagreement │                             │
│                    │   = explore  │                             │
│                    │              │                             │
│                    │ final_score  │                             │
│                    └──────┬───────┘                             │
│                           │                                     │
│                           ▼                                     │
│                    ┌──────────────┐                             │
│                    │   ACTION     │                             │
│                    │  (execute)   │                             │
│                    └──────────────┘                             │
└─────────────────────────────────────────────────────────────────┘
```

### Layer Scoring

Each layer computes a score for each possible output:

```c
typedef struct {
    float tau;              // Decay rate (0.99 = slow, 0.50 = fast)
    uint8_t window;         // How many ticks to consider

    // Layer's own model of the world
    float ema_delta[8][13]; // EMA of deltas: [output][input]
    float variance[8][13];  // Variance tracking
    float entropy[8];       // Layer's entropy estimate per output

    // Output: scores for each possible action
    float scores[8];        // Higher = more interesting to explore
} exploration_layer_t;
```

### Aggregation Rules

```c
void aggregate_scores(exploration_layer_t layers[3], float final[8]) {
    for (int o = 0; o < 8; o++) {
        float s1 = layers[0].scores[o];
        float s2 = layers[1].scores[o];
        float s3 = layers[2].scores[o];

        // Agreement: all layers say explore this
        float agreement = min(s1, s2, s3);

        // Disagreement: layers conflict on this output
        float spread = max(s1, s2, s3) - min(s1, s2, s3);

        // High disagreement = interesting = explore more
        final[o] = agreement + 0.5 * spread;
    }
}
```

**Key insight:** When layers disagree, that output becomes MORE interesting, not less. Disagreement means scale ambiguity, which means unexplored territory.

---

## Implementation

### New Data Structures

```c
#define NUM_LAYERS 3
#define NUM_OUTPUTS 8
#define NUM_INPUTS 13  // 8 GPIO + 4 ADC + 1 temp

typedef struct {
    // Layer parameters
    float tau;
    uint8_t window;

    // Layer state
    float ema[NUM_OUTPUTS][NUM_INPUTS];      // Smoothed deltas
    float var[NUM_OUTPUTS][NUM_INPUTS];      // Variance estimates
    float entropy[NUM_OUTPUTS];              // Exploration scores

    // Output
    float scores[NUM_OUTPUTS];
} layer_t;

typedef struct {
    // Layers
    layer_t layers[NUM_LAYERS];

    // Shared state
    memory_buffer_t memory;
    int16_t current_gpio[8];
    int16_t current_adc[4];
    int16_t current_temp;
    uint8_t output_states[8];

    // Aggregated decision
    float final_scores[NUM_OUTPUTS];
    uint8_t chosen_output;

    // Statistics
    uint32_t tick;
    uint32_t agreements;      // Times all layers agreed
    uint32_t disagreements;   // Times layers conflicted
} layered_state_t;
```

### Tick Logic

```c
void layered_tick(layered_state_t* state) {
    state->tick++;

    // 1. Read all inputs
    read_all_inputs(state);

    // 2. Each layer analyzes independently
    for (int l = 0; l < NUM_LAYERS; l++) {
        layer_analyze(&state->layers[l], state);
    }

    // 3. Aggregate scores
    aggregate_scores(state);

    // 4. Track agreement/disagreement
    track_layer_consensus(state);

    // 5. Choose action (highest final score)
    state->chosen_output = argmax(state->final_scores);

    // 6. Execute action
    uint8_t pin = OUTPUT_PINS[state->chosen_output];
    uint8_t new_state = !state->output_states[state->chosen_output];
    gpio_write(pin, new_state);
    state->output_states[state->chosen_output] = new_state;

    // 7. Wait for effect
    delay_us(1000);

    // 8. Read inputs again
    int16_t after_gpio[8], after_adc[4], after_temp;
    read_all_inputs_into(after_gpio, after_adc, &after_temp);

    // 9. Compute deltas
    int16_t deltas[NUM_INPUTS];
    compute_deltas(state, after_gpio, after_adc, after_temp, deltas);

    // 10. Update all layers with observation
    for (int l = 0; l < NUM_LAYERS; l++) {
        layer_update(&state->layers[l], state->chosen_output, new_state, deltas);
    }

    // 11. Push to memory
    memory_push(&state->memory, state->chosen_output, new_state, deltas);
}
```

### Layer Analysis

```c
void layer_analyze(layer_t* layer, layered_state_t* state) {
    // For each output, compute "interestingness"
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        float interest = 0.0f;

        // 1. Entropy contribution (unexplored = interesting)
        interest += layer->entropy[o];

        // 2. Variance contribution (unpredictable = interesting)
        for (int i = 0; i < NUM_INPUTS; i++) {
            interest += layer->var[o][i] * 0.1f;
        }

        // 3. Recent delta magnitude (active = interesting)
        memory_entry_t* recent = memory_get(&state->memory, 0);
        if (recent && recent->output_idx == o) {
            // We just explored this, less interesting now
            interest *= 0.5f;
        }

        layer->scores[o] = interest;
    }
}

void layer_update(layer_t* layer, uint8_t output, uint8_t state_val, int16_t* deltas) {
    for (int i = 0; i < NUM_INPUTS; i++) {
        float delta = (float)deltas[i];
        float old_ema = layer->ema[output][i];

        // Update EMA with layer's time constant
        layer->ema[output][i] = layer->tau * old_ema + (1.0f - layer->tau) * delta;

        // Update variance estimate
        float diff = delta - layer->ema[output][i];
        layer->var[output][i] = layer->tau * layer->var[output][i] +
                                 (1.0f - layer->tau) * (diff * diff);
    }

    // Decay entropy for explored output
    layer->entropy[output] *= 0.95f;

    // Boost entropy for unexplored outputs
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        if (o != output) {
            layer->entropy[o] = fminf(layer->entropy[o] * 1.01f, 1000.0f);
        }
    }
}
```

---

## Trace Output

For falsification, we need to see layer disagreement:

```
[100] LAYER SCORES:
  L1 (slow):   G0=120 G1=95  G2=98  G3=102 G4=88  G5=91  G6=105 G7=99
  L2 (medium): G0=45  G1=200 G2=55  G3=60  G4=180 G5=70  G6=50  G7=65
  L3 (fast):   G0=80  G1=150 G2=300 G3=40  G4=90  G5=100 G6=85  G7=95
[100] AGREEMENT:  G3=40 (all layers low)
[100] DISAGREEMENT: G2 (L3=300, L1=98, spread=202) ← INTERESTING
[100] FINAL: G0=142 G1=227 G2=253 G3=81 G4=184 G5=130 G6=132 G7=129
[100] CHOSE: G2 (highest final score, driven by L3 outlier)
```

---

## Success Criteria

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Output diversity | All 8 outputs explored in first 100 ticks | Count unique outputs |
| No stuck behavior | Never same output 10x in a row | Track sequences |
| Layer disagreement | >20% of ticks have spread > 100 | Track disagreements |
| Exploration stability | Runs 1 hour without crash | Uptime |
| Scale sensitivity | L1 finds temp trends, L3 finds ADC spikes | Manual inspection |

---

## Falsification Criteria

The architecture FAILS if:

1. **Layers converge to same scores**: No disagreement means layers are redundant
2. **Still stuck on one output**: Multi-layer didn't fix degenerate behavior
3. **Random walk**: Outputs chosen with no pattern (disagreement is noise, not signal)
4. **Layers disagree on everything**: No agreement means no ground truth
5. **Computational overhead kills loop rate**: Can't maintain 10Hz

---

## Files

```
reflex-os/
├── include/
│   └── reflex_layers.h      # Layer structures and functions
├── main/
│   └── layers_main.c        # Main loop with layered exploration
```

---

## The Hypothesis

> If layers with different time constants observe the same state, their disagreement reveals scale-dependent structure that single-perspective exploration misses.

The C6 isn't learning its body through a single lens. It's learning through an ensemble of observers, each tuned to different temporal scales.

When slow-layer says "nothing happening" but fast-layer says "spike!" — that's where discovery lives.

---

*"The map is not the territory. But three maps at different scales reveal the territory better than one."*
