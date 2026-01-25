# Lincoln Manifold: Embodied C6 — Nodes

> Phase 2: NODES — Extracted ideas, prioritized by importance

---

## Core Nodes

### Node 1: The Newborn Model ★★★★★

**The idea:** A newborn doesn't know it has hands. It discovers them through random action and observed effect. The C6 can do the same with its pins.

**Why it matters:** This reframes the entire project. Not programming the hardware. Teaching the hardware to discover itself.

**Key insight:** Discovery requires a feedback loop. Output → physical coupling → input. Without feedback, no learning.

---

### Node 2: Environment vs Hardware ★★★★★

**The idea:** Two levels of discovery:
- Level 1: What hardware do I have? (Knowable from datasheet, not interesting)
- Level 2: What is my hardware connected to? (Requires exploration, very interesting)

**Why it matters:** We're not rediscovering the chip's capabilities. We're discovering the chip's embedding in the physical world.

**The shift:** "I have GPIO 8" → "GPIO 8 affects something that I can sense on ADC 0"

---

### Node 3: Entropy Field as Exploration Map ★★★★★

**The idea:** Map the possibility space as an entropy field:
- Unexplored (output, input) pairs = high entropy
- Explored but no relationship = low entropy
- Confirmed relationship = crystallized shape

**Why it matters:** The entropy field isn't metaphor here. It's the literal data structure tracking what's known and unknown.

**Gradient:** Exploration follows entropy gradient toward the unexplored.

---

### Node 4: Minimal Feedback Loop ★★★★☆

**The idea:** The simplest discoverable environment:
- LED on pin 8 (already on devkit)
- Photoresistor → ADC channel

**Why it matters:** This is the minimum viable embodiment. One output that affects one input. Enough for discovery.

**Extension:** Each additional sensor/actuator adds discoverable structure.

---

### Node 5: The Bootstrap Problem ★★★★☆

**The idea:** Can't start from nothing. Need minimal givens:
- Can execute instructions
- Can read/write to addresses
- Know safe registers to explore (GPIO, ADC)

**Why it matters:** Defines the boundary between "discovered" and "given."

**Design choice:** Don't discover the ISA. Discover the environment.

---

### Node 6: Credit Assignment ★★★★☆

**The idea:** If multiple things change, which output caused which input change?

**Solutions:**
- Change one thing at a time (systematic)
- Statistical correlation over many trials (probabilistic)
- Require multiple confirmations before crystallization (robust)

**Why it matters:** This is the hard part of any learning system. Bad credit assignment = bad learning.

---

### Node 7: Discovered Relationships as Shapes ★★★★☆

**The idea:** A crystallized relationship is an echip shape:
```
{output_pin, output_state, input_channel, expected_delta, confidence}
```

**Why it matters:** The echip architecture applies directly. Learning = shape formation.

**Capability unlocked:** Prediction, planning, anomaly detection.

---

### Node 8: The Tesseract Instantiated ★★★★☆

**The idea:** The exploration process IS the Tesseract:
- Outer (invariant): The hardware itself
- Inner (disturbance): Exploration activity
- Innermost (perception): Discovered model

**Why it matters:** The consciousness architecture isn't abstract anymore. It's running on the chip.

---

### Node 9: Rerun as Discovery Visualization ★★★☆☆

**The idea:** Visualize discovery in real-time:
- Entropy field heatmap (what's explored?)
- Relationship graph (what's connected to what?)
- Crystallization events (aha moments)

**Why it matters:** Makes the invisible visible. "Watch the C6 learn its own body."

---

### Node 10: Swarm Discovery ★★★☆☆

**The idea:** Multiple C6s discovering each other:
- C6_A output → C6_B input
- Each discovers: "something I do affects something outside me"

**Why it matters:** Extends embodiment to distributed systems. Social learning.

---

### Node 11: Discovery Timescales ★★★☆☆

**The idea:** Different phenomena operate at different timescales:
- Immediate: LED toggle → photoresistor response (microseconds)
- Slow: Temperature drift (minutes/hours)
- Cyclic: Day/night patterns (hours)

**Why it matters:** A complete self-model includes temporal structure.

---

### Node 12: From Discovery to Agency ★★★☆☆

**The idea:** Once relationships are known:
1. Predict: "If I do X, Y should happen"
2. Plan: "I want Y, so I should do X"
3. Detect: "I expected Y but got Z — something changed"

**Why it matters:** This is the bridge from passive learning to active agency.

---

### Node 13: Grounded Symbol Formation ★★★☆☆

**The idea:** The LED isn't "GPIO 8" because we named it. It's "the thing that affects my sensor" because discovery grounded the relationship.

**Why it matters:** Meaning arises from interaction, not assignment. This is what embodied cognition people have been talking about.

---

### Node 14: Safety Constraints ★★☆☆☆

**The idea:** Constrain exploration to safe space:
- Only GPIO and ADC registers (not flash controller, not clock config)
- Watchdog recovery from crashes
- Persistent memory for discoveries

**Why it matters:** Exploration shouldn't brick the chip permanently.

---

### Node 15: Surprise as Signal ★★☆☆☆

**The idea:** When expectation differs from observation:
- High surprise = model is wrong or environment changed
- Low surprise = model is accurate

**Why it matters:** Surprise drives re-exploration. Keeps the model updated.

---

## Priority Ranking

| Rank | Node | Rationale |
|------|------|-----------|
| 1 | Minimal Feedback Loop | Can't do anything without this hardware |
| 2 | Environment vs Hardware | Defines what we're actually discovering |
| 3 | Entropy Field as Map | The core data structure |
| 4 | Credit Assignment | Makes learning actually work |
| 5 | Bootstrap Problem | Defines starting conditions |
| 6 | Relationships as Shapes | Connects to echip architecture |
| 7 | Tesseract Instantiated | Philosophical grounding |
| 8 | Rerun Visualization | Seeing is understanding |
| 9 | Discovery to Agency | The payoff |
| 10 | Grounded Symbols | The deep implication |

---

## Critical Path

```
1. Build minimal feedback hardware (LED → photoresistor → ADC)
2. Implement entropy field for (output, input) pairs
3. Implement discovery loop (action → observe → update)
4. Implement crystallization (confirmed relationships → shapes)
5. Stream to Rerun (visualize learning)
6. Add more environment (more sensors/actuators)
7. Add prediction/planning (use discovered knowledge)
8. Add swarm (multiple C6s discovering each other)
```

---

## Key Dependencies

```
Minimal Feedback Hardware
         │
         ▼
    Entropy Field ◄────── Bootstrap Constraints
         │
         ▼
   Discovery Loop
         │
         ├─────────────────┐
         ▼                 ▼
  Credit Assignment    Observation
         │                 │
         └────────┬────────┘
                  ▼
          Crystallization
                  │
                  ▼
         Discovered Shapes
                  │
         ┌───────┴───────┐
         ▼               ▼
    Prediction       Rerun Viz
         │
         ▼
       Agency
```

---

*End of NODES phase. Moving to REFLECT.*
