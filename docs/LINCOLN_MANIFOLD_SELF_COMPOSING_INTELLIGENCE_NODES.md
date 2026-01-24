# Lincoln Manifold: Self-Composing Intelligence

> Phase 2: NODES - Key concepts extracted

---

## Core Nodes

### 1. **Frozen Shape**
Static computational element. Has type (NAND, LATCH, NEURON, etc.), position, ports. Does not change its function. The atoms of computation.

### 2. **Mutable Route**
Dynamic connection between shapes. Has source, destination, weight, activity. Changes based on use. The bonds between atoms.

### 3. **Entropy Field**
Substrate that tracks activity patterns. Accumulates entropy in silence. Enables crystallization. The medium in which shapes exist.

### 4. **Hebbian Learning**
"Fire together, wire together." Routes carrying correlated signals strengthen. The mechanism of association.

### 5. **Activity Decay**
Unused routes weaken over time. The mechanism of forgetting.

### 6. **Pruning**
Routes below activity threshold dissolve back to void. The mechanism of efficiency.

### 7. **Crystallization**
High-entropy voids spawn new shapes. The mechanism of growth.

### 8. **Self-Composition**
The combination of all mechanisms: the chip configures itself through use.

### 9. **Weight**
Route strength. Determines signal amplification/attenuation. Changes through learning.

### 10. **Activity Counter**
Tracks recent route usage. Drives pruning decisions.

### 11. **Threshold**
Entropy level or activity level that triggers state transitions.

### 12. **Shape Evaluation**
Computing output from inputs based on shape type. The actual computation.

### 13. **Signal Propagation**
Moving values through routes. The flow of information.

### 14. **Port**
Input or output connection point on a shape. The interface.

### 15. **Tick**
One cycle of the processor. Propagate, evaluate, learn, evolve.

---

## Relationship Map

```
                    ┌─────────────────┐
                    │   INPUTS        │
                    └────────┬────────┘
                             │
                             ▼
┌──────────────────────────────────────────────────────────────┐
│                    FROZEN SHAPES                              │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐ │
│  │  NAND  │  │ LATCH  │  │  ADD   │  │ NEURON │  │  ...   │ │
│  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘ │
│      │           │           │           │           │       │
└──────┼───────────┼───────────┼───────────┼───────────┼───────┘
       │           │           │           │           │
       └───────────┴───────────┴─────┬─────┴───────────┘
                                     │
                    ┌────────────────▼─────────────────┐
                    │         MUTABLE ROUTES           │
                    │                                  │
                    │  weight ──► signal strength      │
                    │  activity ──► survival           │
                    │                                  │
                    │      HEBBIAN          DECAY      │
                    │     (strengthen)    (weaken)     │
                    │          │              │        │
                    │          └──────┬───────┘        │
                    │                 │                │
                    │           ┌─────▼─────┐          │
                    │           │  PRUNING  │          │
                    │           │ (dissolve)│          │
                    │           └─────┬─────┘          │
                    └─────────────────┼────────────────┘
                                      │
                    ┌─────────────────▼────────────────┐
                    │         ENTROPY FIELD            │
                    │                                  │
                    │   silence ──► entropy            │
                    │   entropy ──► crystallization    │
                    │   crystallization ──► NEW SHAPE  │
                    │                                  │
                    └─────────────────┬────────────────┘
                                      │
                    ┌─────────────────▼────────────────┐
                    │         SELF-COMPOSITION         │
                    │                                  │
                    │   The chip reconfigures itself   │
                    │   based on what it computes      │
                    │                                  │
                    └──────────────────────────────────┘
```

---

## Concept Clusters

### Structure
- Frozen shapes
- Shape types
- Ports (input/output)
- Positions

### Dynamics
- Mutable routes
- Weights
- Activity
- Signal propagation

### Learning
- Hebbian strengthening
- Activity decay
- Correlation detection

### Morphogenesis
- Entropy accumulation
- Crystallization threshold
- Shape type determination
- Pruning

### Execution
- Tick cycle
- Shape evaluation
- Route propagation
- External I/O

---

## Key Metrics

| Metric | Meaning |
|--------|---------|
| num_shapes | Current structure complexity |
| num_routes | Current connectivity |
| shapes_created | Crystallization events |
| shapes_dissolved | Death events |
| routes_created | New connections |
| routes_dissolved | Pruning events |
| signals_propagated | Computational activity |
| total_entropy | Potential for growth |

---

## Emergent Properties

1. **Adaptation**: Structure changes to match input patterns
2. **Learning**: Associations form through correlation
3. **Efficiency**: Unused resources reclaimed
4. **Growth**: New capacity emerges from void
5. **Specialization**: Chip becomes good at what it does
6. **Resilience**: Can reroute around damage

---

*15 nodes identified. Relationships mapped. Ready for REFLECT.*
