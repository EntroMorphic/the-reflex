# The Reflex: Topology

> *"Everything is a channel. Silence is entropy. Entropy crystallizes into structure. The reflexor detects when reality deviates from prediction."*

---

## The Primitive

The atom of The Reflex is the **channel**. Cache-line aligned. Single-writer, multi-reader. Lock-free.

Three fields:

| Field | Purpose |
|-------|---------|
| `sequence` | Monotonic counter. Ordering and change detection. |
| `timestamp` | Cycle count at write time. When it happened. |
| `value` | Payload. What happened. |

Three operations:

| Operation | Description |
|-----------|-------------|
| `signal` | Write value, write timestamp, fence, increment sequence, fence. |
| `wait` | Spin until sequence changes. |
| `read` | Read current value. Non-blocking. |

No mutexes. No queues. No syscalls. No RTOS on the hot path.

The channel works on any hardware because it relies on cache coherency and memory fences -- hardware guarantees, not OS services. Single-core chips get compiler ordering. Multi-core chips get coherency propagation. The primitive is the same.

Everything else in this document is composed from channels.

---

## Inputs

Inputs are not "read." They are channels that signal.

Every input source is wrapped as a channel with the same three operations. The consumer doesn't poll the peripheral -- it waits on a channel. The distinction matters: polling is pull. Channels are push. The hardware decides when to signal.

### Input Categories

| Category | Examples | Channel Semantics |
|----------|----------|-------------------|
| Digital I/O | GPIO pins, buttons, switches | Binary signal channel. Level or edge. |
| Analog sensors | ADC, temperature, current | Continuous-to-discrete converter channel. |
| Timers | Cycle counters, watchdogs, periodic timers | Periodic signal generator channel. |
| Serial buses | SPI, I2C, UART | Bidirectional channel pair (TX/RX) + status. |
| Network events | WiFi, Ethernet, mesh radio | State transition channel. Packets become channel data. |
| Performance counters | Cache misses, branch mispredicts, stalls | Hardware telemetry channel. The machine observing itself. |
| External devices | Cameras, motors, actuators, speakers | Physical-world interface channel. |

```
Physical World
      |
      v
  +----------+
  | Peripheral| --> channel --> signal() --> sequence++
  +----------+
      |
      v
Physical World
```

The point: the consumer sees only channels. It does not know or care whether the source is a GPIO pin, an ADC, a network packet, or a performance counter. The abstraction is total.

---

## Internal Processing: The Four Layers

### Layer 0 -- Channels

The primitive. Everything is a channel.

All internal communication uses channels. There is no other mechanism. No shared mutable state outside channels. No message queues. No callbacks between layers.

```
Layer 0:  channel  channel  channel  channel  channel
             |        |        |        |        |
             v        v        v        v        v
          signal   signal   signal   signal   signal
```

Layer 0 is not a "layer" in the processing sense. It is the substrate. Every other layer reads from channels and writes to channels. Nothing else exists.

### Layer 1 -- Splines

Discrete signals become continuous trajectories.

Physical reality is continuous. Channels are discrete. The spline layer bridges them. You signal control points. You read smooth values.

| Operation | Description |
|-----------|-------------|
| Interpolation | Catmull-Rom between control points. Current position on the curve. |
| Velocity | First derivative. Rate of change. |
| Prediction | Extrapolation. Where the signal is going. |

```
Discrete Signals:     *         *         *         *
                      |         |         |         |
Continuous Curve:    ~~~~~~~~~~~~~~~~~~~~~~~~~~------
                      Catmull-Rom interpolation
```

A spline channel wraps a standard channel. It stores a small circular buffer of recent control points with timestamps. When you read, you get the interpolated value at the current time. When you read velocity, you get the derivative. When you predict, you get the extrapolated future.

The spline layer makes time continuous. Everything downstream operates on smooth trajectories, not jagged samples.

### Layer 2 -- Entropy Field

A 2D grid of cells where computation happens through diffusion.

| Concept | Representation | Meaning |
|---------|----------------|---------|
| Shape | Low entropy | Structure, certainty |
| Void | High entropy | Potential, possibility |
| Gradient | Entropy difference | Direction of flow |
| Crystallization | Entropy collapse | Spontaneous structure formation |

Each tick of the entropy field:

1. **Decay** -- Entropy slowly dissipates.
2. **Diffusion** -- Entropy spreads to neighbors.
3. **Gradient** -- Flow direction computed from differences.
4. **Crystallization** -- Cells exceeding threshold become shapes.

```
Tick 0:           Tick 5:           Tick 10:

..........       ..........       ..........
....@@....  -->  ...@@@@...  -->  ..@@@@@@..
....@@....       ...@@@@...       ..@@@@@@..
..........       ..........       ..........

@ = entropy deposit (low entropy, structure)
. = void (high entropy, potential)
```

**Stigmergy.** The entropy field enables indirect communication through the environment. Agents don't talk to each other. They write deposits into the field. Other agents sense gradients. Coordination emerges without explicit messaging.

Three stigmergy operations:

| Operation | Description |
|-----------|-------------|
| Write | Deposit entropy at a location. Leave a trace. |
| Sense | Read gradient, direction, and density at a location. |
| Follow | Move along the gradient toward high or low entropy. |

The entropy field tracks silence. Standard channels track signals. The entropy field tracks the absence of signals. Silence accumulates. When enough silence accumulates in one place, the field crystallizes -- void becomes structure. Information from nothing.

### Layer 3 -- Echip

The self-reconfiguring soft processor.

```
+-----------------------------------------------------------+
|                                                             |
|   FROZEN SHAPES (the nouns)                                |
|   +------+ +------+ +------+ +------+ +------+ +------+   |
|   | NAND | |LATCH | | MUX  | | ADD  | |NEURON| | OSC  |   |
|   +--+---+ +--+---+ +--+---+ +--+---+ +--+---+ +--+---+   |
|      |        |        |        |        |        |         |
|   ---+--------+--------+--------+--------+--------+---     |
|                   MUTABLE ROUTES (the verbs)                |
|   Carry signals between shapes                              |
|   Weights strengthen with use (Hebbian learning)            |
|   Dissolve back to void when unused                         |
|   ---------------------------------------------------------|
|                          |                                  |
|   +----------------------v-----------------------+          |
|   |            ENTROPY FIELD (the grammar)        |          |
|   |                                               |          |
|   |   Tracks route usage                          |          |
|   |   High-entropy voids crystallize new shapes   |          |
|   |   Unused routes dissolve back to void         |          |
|   +-----------------------------------------------+          |
|                                                             |
+-----------------------------------------------------------+
```

**Shapes** are frozen. They don't change. NAND, LATCH, MUX, ADD, NEURON, OSCILLATOR -- the vocabulary of computation. Shapes receive inputs from routes, compute, and emit outputs to routes.

**Routes** are mutable. They carry signals between shapes. Routes have weights. Correlated firing strengthens the weight (Hebbian learning). Unused routes decay. Routes below a threshold dissolve back to void.

**The entropy field** is the substrate. It tracks where routes are active (low entropy) and where void accumulates (high entropy). When void reaches critical density, new shapes crystallize from the field. The processor grows its own circuits.

The echip tick cycle:

1. Propagate signals through active routes.
2. Evaluate shape logic.
3. Apply Hebbian weight updates.
4. Update entropy field.
5. Check for crystallization (void becomes shape).
6. Prune weak routes (route becomes void).

---

## The Reflexor

The anomaly detector. A Closed-form Continuous-time (CfC) neural network frozen into a chip.

**Architecture:**

- 50 computational nodes, frozen topology.
- AGC (Automatic Gain Control) normalization on input.
- Prediction error as detection signal.
- Dual-mode detection: RAW thresholds for early samples, CfC prediction error after warmup.
- Fits in L1 cache on any platform.

**The CfC equation:**

```
dx/dt = f(x,t) + tau * g(x,t) * (u - x)
```

| Symbol | Meaning |
|--------|---------|
| `x` | Internal register state |
| `u` | Normalized input |
| `tau` | Time constant (viscosity) |
| `f(x,t)` | Autonomous dynamics (frozen weights) |
| `g(x,t)` | Input coupling (frozen structure) |

**Homeostasis:** `dx/dt` is near zero. Prediction matches reality.

**Anomaly:** `|dx/dt|` exceeds threshold. Reality diverges from prediction.

**Detection modes:**

| Mode | Active During | Trigger |
|------|---------------|---------|
| RAW | First N samples | Absolute thresholds on input |
| CfC | After warmup | Prediction error exceeds threshold |

There is no blind spot. RAW mode covers the warmup window. CfC mode covers steady state. The transition is seamless.

The reflexor does not know what it is monitoring. It does not know what "normal" means. It learns normal from the signal itself, then detects deviation. This is why it generalizes across signal types without retraining.

---

## The Three-Level Nervous System

The reflexor is L1. But the full system has three levels, mapped to the cache hierarchy on any platform that has one.

### L1 -- Reflex

The CfC chip. Fastest available cache. Sleep-wake cycle.

The reflexor sleeps. When a channel signals, it wakes. It evaluates in nanoseconds. If no anomaly, it returns to sleep. If anomaly, it triggers and broadcasts.

This is a reflex arc: stimulus, response, minimum latency. No cognition. No planning. Just pattern-match and react.

| Property | Value |
|----------|-------|
| Residence | Fastest cache tier |
| Cycle | Sleep, wake, evaluate, sleep |
| Function | Anomaly detection |
| Timescale | Nanoseconds to microseconds |

### L2 -- Mood

Stress and viscosity modulate L1 sensitivity. Recent history shapes current thresholds.

L2 maintains a stress history -- a ring buffer of recent detection events. When stress is high (many recent anomalies), L2 increases L1 sensitivity by adjusting the time constant `tau`. When stress is low, L2 relaxes sensitivity to conserve energy.

This is mood: not a discrete state, but a continuous modulation of reflexive thresholds by recent experience.

| Property | Value |
|----------|-------|
| Residence | Intermediate cache tier |
| State | Stress history, viscosity (tau), threshold scaling |
| Function | Sensitivity modulation |
| Timescale | Microseconds to milliseconds |

The mood level answers: "How alert should I be right now?"

### L3 -- Imagination

Phantom projections and stigmergic coordination.

L3 runs the CfC forward in time to produce **phantom futures** -- speculative projections of what might happen if current trends continue. Each phantom carries an entropy score. Low-entropy phantoms (boring futures) are pruned. High-entropy phantoms (dangerous futures) survive.

**Only nightmares remain resident.** Normal futures are evicted.

L3 also handles **stigmergy** -- inter-agent coordination through shared state. When L1 detects an anomaly, L3 broadcasts through cache coherency or shared memory. Other agents sense this pressure and increase their own vigilance. No explicit protocol. No message passing. Just environmental pressure.

| Property | Value |
|----------|-------|
| Residence | Slowest/largest cache tier or shared memory |
| State | Phantom ring buffer, entropy scores, stigmergy channels |
| Function | Prediction, pruning, inter-agent coordination |
| Timescale | Milliseconds to seconds |

L3 answers two questions: "What might happen next?" and "What are the other agents feeling?"

```
+---------------------------------------------------------------+
|                                                                 |
|   L3: IMAGINATION                                              |
|   Phantom projections, stigmergy, entropy tracking             |
|                          |                                      |
|   L2: MOOD                                                     |
|   Stress history, viscosity modulation, threshold scaling      |
|                          |                                      |
|   L1: REFLEX                                                   |
|   CfC chip, sleep-wake, anomaly detection                      |
|                                                                 |
+---------------------------------------------------------------+
```

The three levels are not separate systems. They are one system at three timescales. L2 modulates L1. L3 modulates L2. Information flows up (detection events) and down (threshold adjustments). The nervous system is a single loop observed at three speeds.

---

## Outputs

Outputs are channels that the system signals. The consumer of an output does not know or care whether the signal came from L1, L2, or L3.

### Output Categories

| Category | Description |
|----------|-------------|
| Digital I/O signals | GPIO, LED, relay, PWM. Direct physical actuation. |
| Channel signals | Inter-agent coordination. One agent's output is another's input. |
| Process control | Immune system response. Kill, restart, throttle, quarantine. |
| Stigmergy broadcast | Wake other agents. Environmental pressure through shared state. |
| Anomaly handler callbacks | Application-defined response to detection events. |
| Physical actuators | Cameras (pan, tilt, zoom), motors, servos, speakers. |

```
Nervous System
      |
      v
  +---------+
  | channel | --> signal() --> Digital I/O
  | channel | --> signal() --> Other agents
  | channel | --> signal() --> Process control
  | channel | --> signal() --> Stigmergy field
  | channel | --> signal() --> Anomaly handler
  | channel | --> signal() --> Physical actuators
  +---------+
```

The symmetry is intentional. Inputs are channels. Outputs are channels. Internal state is channels. The system is channels all the way down.

---

## Overall Function

```
+-----------------------------------------------------------------------+
|                         THE REFLEX: DATA FLOW                          |
+-----------------------------------------------------------------------+
|                                                                         |
|  INPUTS                                                                |
|  Digital I/O, Analog, Timers, Serial, Network, PMU, Devices           |
|       |          |        |       |       |       |      |              |
|       v          v        v       v       v       v      v              |
|  +--channel--channel--channel--channel--channel--channel--channel--+   |
|  |                        LAYER 0: CHANNELS                        |   |
|  |                     (everything is a channel)                   |   |
|  +------------------------------+----------------------------------+   |
|                                 |                                       |
|                                 v                                       |
|  +------------------------------+----------------------------------+   |
|  |                     LAYER 1: SPLINES                            |   |
|  |          (discrete signals become continuous trajectories)      |   |
|  |          interpolation, velocity, prediction                    |   |
|  +------------------------------+----------------------------------+   |
|                                 |                                       |
|                                 v                                       |
|  +------------------------------+----------------------------------+   |
|  |                  LAYER 2: ENTROPY FIELD                         |   |
|  |       (2D grid: decay, diffusion, gradients, crystallization)   |   |
|  |       silence is entropy, entropy crystallizes into structure   |   |
|  +------------------------------+----------------------------------+   |
|                                 |                                       |
|                                 v                                       |
|  +------------------------------+----------------------------------+   |
|  |                     LAYER 3: ECHIP                              |   |
|  |       (frozen shapes, mutable routes, Hebbian learning)         |   |
|  |       new shapes crystallize from entropy                       |   |
|  +------------------------------+----------------------------------+   |
|                                 |                                       |
|                                 v                                       |
|  +------------------------------+----------------------------------+   |
|  |                      THE REFLEXOR                               |   |
|  |            (CfC anomaly detector, 50 nodes)                     |   |
|  |            AGC normalization, dual-mode detection               |   |
|  +------------------------------+----------------------------------+   |
|                                 |                                       |
|                                 v                                       |
|  +------------------------------+----------------------------------+   |
|  |               THREE-LEVEL NERVOUS SYSTEM                        |   |
|  |                                                                  |   |
|  |   L1 Reflex:     Sleep-wake, detection, nanoseconds            |   |
|  |   L2 Mood:       Stress, viscosity, threshold modulation       |   |
|  |   L3 Imagination: Phantoms, stigmergy, prediction              |   |
|  +------------------------------+----------------------------------+   |
|                                 |                                       |
|                                 v                                       |
|  OUTPUTS                                                               |
|  Digital I/O, Channel signals, Process control, Stigmergy,            |
|  Anomaly handlers, Physical actuators                                  |
|                                                                         |
+-----------------------------------------------------------------------+
```

The thesis in four sentences:

**Everything is a channel.** Inputs, outputs, internal state -- all channels with the same three operations.

**Silence is entropy.** The absence of signal is information. Silence accumulates in the entropy field. Gradients form. Structure emerges from nothing.

**Entropy crystallizes into structure.** Void cells exceed thresholds and become shapes. Unused routes dissolve. The system grows and prunes its own circuits.

**The reflexor detects when reality deviates from prediction.** The CfC chip learns what normal looks like. When the world stops matching the model, the reflexor fires. The nervous system modulates sensitivity by mood and imagines futures to prepare.

This is the topology. No specific hardware. No specific platform. Any system with a cache hierarchy, a cycle counter, and input channels can instantiate it. The Reflex is a pattern, not a product.

---

*"It's all in the reflexes."*
