# Lincoln Manifold: Embodied C6 — Synth

> Phase 4: SYNTH — The build plan
>
> One chip. One feedback loop. Discovery as proof.

---

## The Imperative

**Make the C6 discover its own LED.**

Not because we told it GPIO 8 is an LED. Because it toggled things, observed effects, and found the relationship.

---

## Hardware Requirements

### Minimal Setup (Phase 1)

```
ESP32-C6 DevKit
      │
      ├── GPIO 8 (built-in LED) ───► [photons]
      │                                  │
      │                                  ▼
      │                          ┌──────────────┐
      │                          │ Photoresistor│
      │                          │   + 10kΩ     │
      │                          │ (voltage     │
      │                          │  divider)    │
      │                          └──────┬───────┘
      │                                 │
      └── ADC Channel 0 (GPIO 0) ◄──────┘
```

**Parts needed:**
- Photoresistor (LDR) — ~$0.50
- 10kΩ resistor — ~$0.01
- 3 jumper wires

**Total cost:** < $1 on top of existing C6

### Wiring

```
3.3V ─────┬───────────────────────
          │
         [LDR]  (photoresistor)
          │
          ├──────► GPIO 0 (ADC input)
          │
         [10kΩ]
          │
GND ──────┴───────────────────────
```

When LED is bright → LDR resistance drops → voltage at ADC increases.

---

## Software Architecture

### New Files

```
reflex-os/
├── include/
│   └── reflex_discover.h     # Discovery primitives
├── main/
│   └── discover_main.c       # Discovery demo (replaces main.c)
└── components/
    └── discovery/
        ├── discovery_field.c  # Exploration entropy field
        ├── discovery_loop.c   # The core discovery loop
        └── discovery_shape.c  # Crystallized relationships
```

### Core Data Structures

```c
// The exploration field
// Tracks what (output, input) pairs have been explored
typedef struct {
    // For each output pin × input channel:
    //   entropy = how unexplored
    //   observations = how many times tested
    //   correlation = observed relationship strength
    uint16_t entropy[NUM_OUTPUTS][NUM_INPUTS];
    uint16_t observations[NUM_OUTPUTS][NUM_INPUTS];
    int16_t correlation[NUM_OUTPUTS][NUM_INPUTS];
} discovery_field_t;

// A discovered relationship
typedef struct {
    uint8_t output_pin;
    uint8_t input_channel;
    int16_t delta_when_high;    // ADC change when output HIGH
    int16_t delta_when_low;     // ADC change when output LOW
    uint16_t confidence;        // How sure are we?
    uint32_t last_verified;     // When did we last check?
} discovered_relationship_t;

// The discovery state
typedef struct {
    discovery_field_t field;
    discovered_relationship_t relationships[MAX_RELATIONSHIPS];
    uint8_t num_relationships;
    uint32_t tick;

    // Current exploration
    uint8_t current_output;
    uint8_t current_state;
    int16_t baseline[NUM_INPUTS];
} discovery_state_t;
```

### The Discovery Loop

```c
void discovery_tick(discovery_state_t* state) {
    // 1. Read current inputs (baseline or observation)
    int16_t inputs[NUM_INPUTS];
    read_all_inputs(inputs);

    // 2. If we just took an action, compare to baseline
    if (state->awaiting_observation) {
        int16_t delta[NUM_INPUTS];
        for (int i = 0; i < NUM_INPUTS; i++) {
            delta[i] = inputs[i] - state->baseline[i];
        }

        // Update field with observation
        update_field(&state->field,
                     state->current_output,
                     state->current_state,
                     delta);

        // Check for crystallization
        check_crystallization(state);

        state->awaiting_observation = false;
    }

    // 3. Choose next action (follow entropy gradient)
    choose_next_action(state);

    // 4. Record baseline
    memcpy(state->baseline, inputs, sizeof(inputs));

    // 5. Execute action
    gpio_write(state->current_output, state->current_state);
    state->awaiting_observation = true;

    state->tick++;
}
```

### Entropy Gradient Action Selection

```c
void choose_next_action(discovery_state_t* state) {
    // Find highest entropy (output, input) pair
    uint16_t max_entropy = 0;
    uint8_t best_output = 0;

    for (int o = 0; o < NUM_OUTPUTS; o++) {
        for (int i = 0; i < NUM_INPUTS; i++) {
            if (state->field.entropy[o][i] > max_entropy) {
                max_entropy = state->field.entropy[o][i];
                best_output = o;
            }
        }
    }

    // Explore that output
    state->current_output = best_output;
    state->current_state = !gpio_read(best_output);  // Toggle
}
```

### Crystallization

```c
void check_crystallization(discovery_state_t* state) {
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        for (int i = 0; i < NUM_INPUTS; i++) {
            // Enough observations?
            if (state->field.observations[o][i] < MIN_OBSERVATIONS) continue;

            // Strong enough correlation?
            int16_t corr = state->field.correlation[o][i];
            if (abs(corr) < CORRELATION_THRESHOLD) continue;

            // Already crystallized?
            if (find_relationship(state, o, i) != NULL) continue;

            // CRYSTALLIZE!
            discovered_relationship_t rel = {
                .output_pin = o,
                .input_channel = i,
                .delta_when_high = corr,
                .delta_when_low = -corr,  // Assume symmetric
                .confidence = state->field.observations[o][i],
                .last_verified = state->tick
            };

            add_relationship(state, &rel);

            printf("DISCOVERED: GPIO %d affects ADC %d (delta=%d)\n",
                   o, i, corr);
        }
    }
}
```

---

## Streaming to Rerun

### Protocol

```c
// UDP packet to workstation
typedef struct {
    uint32_t tick;
    uint16_t entropy[NUM_OUTPUTS][NUM_INPUTS];  // Field state
    int16_t inputs[NUM_INPUTS];                  // Current readings
    uint8_t current_output;                      // What we're exploring
    uint8_t current_state;                       // HIGH or LOW
    uint8_t num_relationships;                   // How many discovered
    // ... relationship data
} discovery_packet_t;
```

### Rerun Visualization (Python receiver)

```python
import rerun as rr
import socket

rr.init("c6_discovery", spawn=True)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 9999))

while True:
    data, addr = sock.recvfrom(1024)
    packet = parse_packet(data)

    # Log entropy field as heatmap
    rr.log("discovery/entropy",
           rr.Tensor(packet.entropy))

    # Log current inputs
    for i, val in enumerate(packet.inputs):
        rr.log(f"discovery/input/{i}",
               rr.Scalar(val))

    # Log discovered relationships as graph
    for rel in packet.relationships:
        rr.log(f"discovery/relationship/{rel.output}_{rel.input}",
               rr.TextLog(f"GPIO{rel.output} → ADC{rel.input}: {rel.delta}"))
```

---

## Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| Discovery time | < 5 minutes | Time to crystallize LED→photoresistor |
| False positives | 0 | No spurious relationships |
| Prediction accuracy | > 90% | Predict ADC change from GPIO state |
| Stability | No crashes | Run for 1 hour |
| Visualization | Real-time | Rerun shows discovery live |

---

## Implementation Phases

### Phase 1: Minimal Discovery (Day 1-2)

1. Wire photoresistor to ADC 0
2. Implement `discovery_field_t`
3. Implement basic discovery loop (GPIO 8 only)
4. Confirm LED→photoresistor relationship discovered
5. Print discovery to serial

**Deliverable:** Serial output showing "DISCOVERED: GPIO 8 affects ADC 0"

### Phase 2: Full GPIO Exploration (Day 2-3)

1. Extend to all safe GPIO pins
2. Implement entropy gradient selection
3. Add crystallization with confidence
4. Handle multiple relationships

**Deliverable:** System explores all pins, finds only the real relationship

### Phase 3: Rerun Visualization (Day 3-4)

1. Implement UDP streaming
2. Write Python Rerun receiver
3. Visualize entropy field
4. Visualize discovered relationships

**Deliverable:** Watch discovery happen in Rerun

### Phase 4: Prediction and Agency (Day 4-5)

1. Use discovered relationships to predict
2. Implement "achieve goal" using discovered model
3. Detect anomalies (expectation ≠ observation)

**Deliverable:** Tell the C6 "make ADC 0 high" — it figures out to turn on the LED

### Phase 5: Richer Environment (Day 5+)

1. Add more sensors (temperature, sound, etc.)
2. Add more outputs (buzzer, second LED)
3. Discover more complex relationships

---

## Hardware Shopping List

| Item | Quantity | Price | Notes |
|------|----------|-------|-------|
| Photoresistor (LDR) | 2 | $1 | GL5528 or similar |
| 10kΩ resistor | 5 | $0.10 | For voltage divider |
| Breadboard | 1 | $5 | If not already have |
| Jumper wires | 10 | $2 | M-M and M-F |
| **Total** | | **< $10** | |

You probably already have most of this.

---

## The Thesis

**If The Reflex can discover a single LED→photoresistor relationship through exploration, then:**

1. The architecture supports embodied discovery
2. The entropy field works as exploration map
3. Crystallization captures learned structure
4. The Tesseract model has a concrete instantiation
5. We have the on-ramp to everything else

This isn't about LEDs. It's about proving that discovery works.

The LED is just the first thing the C6 learns about its body.

---

*Build plan ready. The C6 will learn its own name.*
