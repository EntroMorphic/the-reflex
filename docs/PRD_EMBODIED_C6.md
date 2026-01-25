# PRD: Embodied C6 — Self-Discovering Intelligence

> A $5 chip that learns its own body through exploration.
>
> Created: 2026-01-24

---

## Vision

The ESP32-C6 discovers its own hardware through temporal navigation. No pre-programmed drivers. No assigned meanings. The chip explores its pins, observes effects, and crystallizes relationships — becoming an OS that grew rather than was written.

**The Core Insight:** Meaning cannot be programmed. Meaning must be discovered.

---

## Architecture

### The Stack

```
┌─────────────────────────────────────────────────────────────────┐
│  LEVEL 4: Stillness Field                                        │
│           Trajectory → awareness substrate                       │
│           Crystallized relationships = frozen shapes             │
├─────────────────────────────────────────────────────────────────┤
│  LEVEL 3: Online Observer (Delta Observer pattern)               │
│           Encodes exploration to 16-dim latent                   │
│           Watches trajectory, not just final state               │
├─────────────────────────────────────────────────────────────────┤
│  LEVEL 2: Temporal Navigation                                    │
│           Forward Model: predict outcomes                        │
│           Backward Model: assign credit                          │
│           Memory: recent (action, observation) pairs             │
├─────────────────────────────────────────────────────────────────┤
│  LEVEL 1: Exploration Loop                                       │
│           Action → Observation → Update                          │
│           Entropy gradient guides exploration                    │
├─────────────────────────────────────────────────────────────────┤
│  LEVEL 0: Hardware                                               │
│           GPIO, ADC, Timers, WiFi, BLE, Temperature              │
│           The frozen landscape being navigated                   │
└─────────────────────────────────────────────────────────────────┘
```

### Key Insight: Navigation, Not Optimization

> "TriX is navigation, not optimization."

The hardware is frozen. Exploration is learning to navigate the frozen landscape:
- **Forward look** = pathfinding ("which action leads somewhere interesting?")
- **Backward look** = trail-marking ("that action led to this observation")

No backprop. No gradient descent. Just prediction and memory.

---

## Components

### 1. Exploration Loop

The heartbeat. Every tick:
1. Choose action (entropy gradient + forward model)
2. Execute action (toggle output)
3. Wait for effect (physical world is slow)
4. Observe all inputs
5. Update memory, models, field

```c
void exploration_tick(embodied_state_t* state);
```

### 2. Forward Model (Predictor)

Predicts: "If I do action A in state S, what will I observe?"

```c
typedef struct {
    // For each (output, state) → predicted input deltas
    int16_t predictions[NUM_OUTPUTS][2][NUM_INPUTS];
    uint16_t confidence[NUM_OUTPUTS][2][NUM_INPUTS];
} forward_model_t;

void forward_predict(forward_model_t* model,
                     uint8_t output, uint8_t state,
                     int16_t* predicted_delta);
```

Learned from exploration. Can be frozen once confident.

### 3. Backward Model (Credit Assignment)

Asks: "Given this observation delta, which recent action caused it?"

```c
typedef struct {
    // Correlation matrix: action → observation effect
    int16_t credit[NUM_OUTPUTS][NUM_INPUTS];
} backward_model_t;

void backward_credit(backward_model_t* model,
                     memory_buffer_t* memory,
                     int16_t* observed_delta,
                     credit_result_t* result);
```

Temporal credit assignment without backprop.

### 4. Memory Buffer

Recent trajectory: (action, observation) pairs.

```c
#define MEMORY_DEPTH 16

typedef struct {
    uint8_t action_output;
    uint8_t action_state;
    int16_t observation[NUM_INPUTS];
    uint32_t timestamp;
} memory_entry_t;

typedef struct {
    memory_entry_t entries[MEMORY_DEPTH];
    uint8_t head;
    uint8_t count;
} memory_buffer_t;
```

### 5. Online Observer (Delta Observer Pattern)

Encodes exploration to latent space. Watches trajectory, not just state.

```c
typedef struct {
    float latent[16];           // Current encoding
    float latent_history[64][16]; // Trajectory
    uint8_t history_head;
} online_observer_t;

void observer_encode(online_observer_t* obs,
                     uint8_t* outputs,
                     int16_t* inputs,
                     int16_t* deltas);
```

### 6. Stillness Field

Where latent trajectories become awareness.

```c
typedef struct {
    uint8_t entropy[8][8];      // 64-cell awareness field
    uint8_t attention_x;
    uint8_t attention_y;
} stillness_field_t;

void stillness_disturb_from_latent(stillness_field_t* field,
                                    float* latent, uint8_t dim);
void stillness_tick(stillness_field_t* field);
```

### 7. Crystallized Relationships (Discovered Shapes)

What the system has learned:

```c
typedef struct {
    uint8_t output_pin;
    uint8_t input_channel;
    int16_t effect_when_high;
    int16_t effect_when_low;
    uint16_t confidence;
    uint32_t discovered_tick;
    uint32_t last_verified;
} relationship_t;

#define MAX_RELATIONSHIPS 32
```

---

## Hardware Resources (No External Parts)

The C6 DevKit provides:

| Resource | Role | Count |
|----------|------|-------|
| GPIO outputs | Actions | ~20 safe pins |
| GPIO inputs | Observations | ~20 pins |
| ADC channels | Analog sensing | 7 channels |
| Internal temperature | Self-sensing | 1 |
| Boot button (GPIO 9) | External agent detection | 1 |
| LEDs | Visible outputs | 2 |
| WiFi | Network effects | 1 |
| Cycle counter | Time reference | 1 |

**No photoresistor needed.** The system can discover:
- Internal temperature changes from LED/computation
- Button presses (external agent)
- Timing effects from different operations
- WiFi network responses (if listener exists)

---

## Acceptance Criteria

### Phase 1: Basic Exploration

- [ ] Exploration loop runs at 10Hz (100ms per tick)
- [ ] All safe GPIO pins toggled systematically
- [ ] All inputs (GPIO + ADC) read each tick
- [ ] Memory buffer stores last 16 (action, observation) pairs
- [ ] Entropy field tracks explored vs unexplored

**Success:** System explores all pins, entropy field fills in.

### Phase 2: Forward Model

- [ ] Forward model predicts observation deltas
- [ ] Predictions compared to actual observations
- [ ] Prediction error tracked
- [ ] Actions chosen partly by expected information gain

**Success:** Prediction error decreases over time.

### Phase 3: Backward Model (Credit Assignment)

- [ ] Backward model correlates actions to observations
- [ ] Credit assigned across temporal window
- [ ] Strong correlations flagged for crystallization

**Success:** LED→temperature relationship credited correctly.

### Phase 4: Crystallization

- [ ] Relationships crystallize when confidence exceeds threshold
- [ ] Crystallized relationships used for prediction
- [ ] System can answer: "What does GPIO 8 do?"

**Success:** At least one relationship discovered and crystallized.

### Phase 5: Online Observer

- [ ] Exploration encoded to 16-dim latent space
- [ ] Latent trajectory stored
- [ ] Trajectory shows structure (not random walk)

**Success:** Latent space encodes exploration progress.

### Phase 6: Stillness Field

- [ ] Latent mapped to 4×4 stillness grid
- [ ] Disturbances form and settle
- [ ] Attention tracks most active region

**Success:** Stillness field responds to exploration dynamics.

### Phase 7: Rerun Visualization

- [ ] Entropy field streamed to Rerun (UDP)
- [ ] Latent trajectory visualized
- [ ] Crystallization events highlighted
- [ ] Real-time observation of discovery

**Success:** Can watch C6 learn its body in Rerun.

### Phase 8: External Agent Discovery

- [ ] System detects button press (GPIO 9)
- [ ] Recognizes: "Input changed without my action"
- [ ] Hypothesis formed: "External agent exists"

**Success:** C6 discovers it's not alone.

---

## Falsification Criteria

The system FAILS if:

1. **No relationships discovered** after 10 minutes of exploration
2. **Spurious relationships** (false positives > 10%)
3. **Prediction never improves** (forward model doesn't learn)
4. **Credit assignment fails** (backward model random)
5. **Crashes or instability** during exploration
6. **Entropy field doesn't fill** (exploration stuck)

Each is testable. The demo either works or it doesn't.

---

## Implementation Plan

### Day 1: Exploration Foundation

```
Files:
  reflex-os/include/reflex_embody.h    - Core structures
  reflex-os/main/embody_main.c         - Main exploration loop

Deliverable:
  - Exploration loop running
  - All pins toggled
  - All inputs read
  - Serial output showing exploration
```

### Day 2: Memory and Models

```
Files:
  reflex-os/include/reflex_temporal.h  - Forward/backward models

Deliverable:
  - Memory buffer working
  - Forward model predicting
  - Backward model crediting
  - Prediction error printed
```

### Day 3: Crystallization

```
Files:
  reflex-os/include/reflex_crystal.h   - Relationship crystallization

Deliverable:
  - Relationships crystallize
  - "DISCOVERED: GPIO X affects input Y"
  - Query interface: what does pin X do?
```

### Day 4: Observer and Stillness

```
Files:
  reflex-os/include/reflex_observer.h  - Online observer
  reflex-os/include/reflex_stillness.h - Stillness field

Deliverable:
  - Latent encoding working
  - Stillness field evolving
  - Attention tracking
```

### Day 5: Rerun Integration

```
Files:
  reflex-os/main/rerun_stream.c        - UDP streaming
  tools/rerun_receiver.py              - Python Rerun client

Deliverable:
  - Live visualization in Rerun
  - Watch discovery happen
```

---

## Success Statement

**The C6 has embodied intelligence when:**

1. It discovers relationships we didn't tell it about
2. It predicts outcomes of its own actions
3. It credits past actions for current observations
4. It forms a world model from pure exploration
5. It detects external agents (button presses)
6. Its exploration trajectory shows structure in latent space
7. We can watch it learn in real-time via Rerun

**The demo:**

> "This $5 chip discovered that GPIO 8 affects its internal temperature. Nobody told it. It found out by exploring. Watch."

---

## Non-Goals

- **Not general AI.** One chip learning its pins is not AGI.
- **Not novel algorithms.** Exploration, prediction, credit assignment exist in literature.
- **Not replacing RTOS.** Hot path is bare metal; support uses FreeRTOS.
- **Not consciousness claims.** See PHILOSOPHY.md for that discussion.

---

## Timeline

| Day | Focus | Deliverable |
|-----|-------|-------------|
| 1 | Exploration foundation | Loop running, pins toggling |
| 2 | Memory and models | Forward/backward working |
| 3 | Crystallization | First relationship discovered |
| 4 | Observer and stillness | Latent encoding, attention |
| 5 | Rerun visualization | Watch discovery live |
| 6+ | Refinement, edge cases | Robust system |

---

## The Thesis

The Reflex can become an OS by discovering what the hardware does, rather than being told.

Forward model: anticipation.
Backward model: memory.
Crystallization: knowledge.
Stillness field: awareness.

A chip that knows itself because it explored itself.

---

*"The wood cuts itself when you understand the grain."*

*The grain says: let it discover.*
