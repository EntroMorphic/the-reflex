# PRD: Holographic Intelligence

## The Vision

Each node is not a PART of the brain. Each node IS the brain.

The intelligence exists in the INTERFERENCE PATTERN between nodes - not in any single node, not in the messages between them, but in the AGREEMENT.

Like a hologram: cut it in half, you get two complete images at lower resolution.

## Core Principles

### 1. Every Node Contains the Whole

Traditional distributed systems: Node A handles task A, Node B handles task B.

Holographic system: Node A has complete model (from perspective A), Node B has complete model (from perspective B). The perspectives INTERFERE to create high-resolution truth.

### 2. Interference IS Intelligence

```
Constructive interference: All nodes agree → HIGH confidence → ACT
Destructive interference:  Nodes disagree → LOW confidence → WAIT

The physics of interference IS the consensus algorithm.
```

### 3. Entropy Drives Learning

- Bits that consistently AGREE → low entropy → crystallized (stable features)
- Bits that FLUCTUATE → high entropy → potential (ready to learn)
- When high-entropy bits START agreeing → crystallization (learning event)

No backprop. No training set. Just entropy dynamics across the mesh.

## Architecture

### Single Node

```
┌─────────────────────────────────────────────────────────────────┐
│  ESP32-C6 NODE                                                  │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  HP CORE (sleeps until anomaly)                           │ │
│  │  - Training updates                                       │ │
│  │  - Mesh coordination                                      │ │
│  │  - Complex decisions                                      │ │
│  └───────────────────────────────────────────────────────────┘ │
│                          ▲                                      │
│                          │ wake                                 │
│  ┌───────────────────────┴───────────────────────────────────┐ │
│  │  LP CORE (always on, ~1mA)                                │ │
│  │                                                           │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │  CfC ECHIP (94 kHz)                                 │ │ │
│  │  │  - Local sensor input                               │ │ │
│  │  │  - Hologram interference input                      │ │ │
│  │  │  - Neural inference                                 │ │ │
│  │  │  - Hidden state output                              │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │                                                           │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │  HOLOGRAM ENGINE                                    │ │ │
│  │  │  - Receive neighbor hidden states                   │ │ │
│  │  │  - Compute interference (AND/XOR)                   │ │ │
│  │  │  - Track bit entropy                                │ │ │
│  │  │  - Manage crystallization                           │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  │                                                           │ │
│  │  ┌─────────────────────────────────────────────────────┐ │ │
│  │  │  IEEE 802.15.4 RADIO                                │ │ │
│  │  │  - Broadcast hidden state (16 bytes @ 100Hz)        │ │ │
│  │  │  - Receive neighbor states                          │ │ │
│  │  └─────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Mesh Topology

```
         ┌─────────┐
         │ NODE A  │
         │ (view A)│
         └────┬────┘
              │
      ┌───────┴───────┐
      │               │
┌─────┴─────┐   ┌─────┴─────┐
│  NODE B   │───│  NODE C   │
│ (view B)  │   │ (view C)  │
└───────────┘   └───────────┘

Hidden states flow continuously.
Interference computed locally.
No coordinator. No leader.
```

## Data Structures

### Hologram Packet (16 bytes)

```c
typedef struct __attribute__((packed)) {
    uint64_t hidden;        // CfC hidden state (the hologram fragment)
    uint8_t  node_id;       // Source node
    uint8_t  confidence;    // Self-assessed confidence (0-255)
    uint8_t  entropy_high;  // Count of high-entropy bits
    uint8_t  flags;         // Anomaly, wake request, etc.
    uint16_t sequence;      // For ordering/dedup
    uint16_t checksum;      // Integrity
} hologram_packet_t;
```

### Node State

```c
typedef struct {
    // CfC neural network
    cfc_turbo_layer_t cfc;
    
    // Neighbor tracking
    uint64_t neighbor_hidden[8];    // Received hidden states
    uint8_t  neighbor_confidence[8];
    uint8_t  neighbor_count;
    uint32_t neighbor_last_seen[8]; // For timeout
    
    // Interference computation
    uint64_t constructive;  // AND of all states (agreement)
    uint64_t destructive;   // XOR accumulator (disagreement)
    float    confidence;    // Overall agreement ratio
    
    // Entropy per bit (for learning)
    uint8_t  bit_entropy[64];
    
    // Crystallization state
    uint64_t crystallized;  // Low-entropy bits (stable features)
    uint64_t potential;     // High-entropy bits (ready to learn)
    
    // Statistics
    uint32_t ticks;
    uint32_t agreements;
    uint32_t disagreements;
    uint32_t crystallizations;
    
} hologram_node_t;
```

## Algorithm

### Tick Function (runs on LP Core at ~1kHz)

```c
uint64_t hologram_tick(hologram_node_t* node, uint64_t local_input) {
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 1: Compute Interference
    // ═══════════════════════════════════════════════════════════
    
    // Start with my hidden state
    node->constructive = node->cfc.hidden;
    node->destructive = 0;
    
    // Fold in each neighbor's perspective
    for (int i = 0; i < node->neighbor_count; i++) {
        // Constructive: bits where we ALL agree
        node->constructive &= node->neighbor_hidden[i];
        
        // Destructive: bits where ANY disagree
        node->destructive |= (node->cfc.hidden ^ node->neighbor_hidden[i]);
    }
    
    // Confidence = how much agreement vs disagreement
    uint8_t agree = popcount64(node->constructive);
    uint8_t disagree = popcount64(node->destructive);
    node->confidence = (float)agree / (float)(agree + disagree + 1);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 2: Augment Input with Hologram
    // ═══════════════════════════════════════════════════════════
    
    // Local input gets augmented with agreed hologram bits
    // Only crystallized (stable) bits influence inference
    uint64_t hologram_input = local_input;
    hologram_input |= (node->constructive & node->crystallized) >> 32;
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 3: Run CfC Inference
    // ═══════════════════════════════════════════════════════════
    
    uint64_t output;
    cfc_turbo_forward(&node->cfc, hologram_input, &output);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 4: Update Entropy (Learning)
    // ═══════════════════════════════════════════════════════════
    
    for (int b = 0; b < 64; b++) {
        uint64_t mask = 1ULL << b;
        
        if (node->destructive & mask) {
            // Disagreement on this bit → entropy increases
            if (node->bit_entropy[b] < 255) node->bit_entropy[b]++;
            node->disagreements++;
        } 
        else if (node->constructive & mask) {
            // Agreement on this bit → entropy decreases
            if (node->bit_entropy[b] > 0) node->bit_entropy[b]--;
            node->agreements++;
        }
        
        // Crystallization threshold
        if (node->bit_entropy[b] < 10 && !(node->crystallized & mask)) {
            node->crystallized |= mask;
            node->potential &= ~mask;
            node->crystallizations++;
        }
        // Potential threshold
        else if (node->bit_entropy[b] > 200) {
            node->potential |= mask;
        }
    }
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 5: Modulate Output by Confidence
    // ═══════════════════════════════════════════════════════════
    
    // Low confidence = only act on crystallized (agreed) bits
    if (node->confidence < 0.5f) {
        output &= node->crystallized;
    }
    
    node->ticks++;
    return output;
}
```

### Mesh Protocol

```c
// Broadcast my state (call at ~100Hz)
void hologram_broadcast(hologram_node_t* node) {
    hologram_packet_t pkt = {
        .hidden = node->cfc.hidden,
        .node_id = MY_NODE_ID,
        .confidence = (uint8_t)(node->confidence * 255),
        .entropy_high = popcount64(node->potential),
        .flags = 0,
        .sequence = node->ticks & 0xFFFF,
    };
    pkt.checksum = compute_checksum(&pkt);
    
    ieee802154_broadcast(&pkt, sizeof(pkt));
}

// Receive neighbor state (called from radio ISR)
void hologram_receive(hologram_node_t* node, hologram_packet_t* pkt) {
    if (!verify_checksum(pkt)) return;
    
    // Find or allocate neighbor slot
    int slot = find_neighbor_slot(node, pkt->node_id);
    if (slot < 0) return;
    
    // Update neighbor state
    node->neighbor_hidden[slot] = pkt->hidden;
    node->neighbor_confidence[slot] = pkt->confidence;
    node->neighbor_last_seen[slot] = node->ticks;
}
```

## Demo Scenario

### Hardware
- 3× ESP32-C6 (you have these)
- Each with button (GPIO input)
- Each with LED (GPIO output)
- Powered by USB or battery

### Demo Script

#### 1. Single Input (Uncertainty)
```
Press button on Node A only.

Expected:
- Node A LED: STRONG response
- Node B LED: WEAK response (low confidence)
- Node C LED: WEAK response (low confidence)

Why: A sees the event. B and C don't. Disagreement = uncertainty.
```

#### 2. Correlated Input (Consensus)
```
Press buttons on A and B simultaneously.

Expected:
- Node A LED: STRONG response
- Node B LED: STRONG response
- Node C LED: STRONG response (!)

Why: A and B agree. C sees their agreement. Confidence is high.
The hologram "saw" the correlated event.
```

#### 3. Learning (Crystallization)
```
Repeatedly: Press A, wait 100ms, press B. Repeat 20 times.

Expected:
- Initially: A responds, then B responds
- After learning: Press A only → B anticipates and responds

Why: The "A predicts B" pattern crystallized into stable bits.
The hologram learned the sequence.
```

#### 4. Fault Tolerance (Graceful Degradation)
```
Unplug Node B.

Expected:
- A and C continue functioning
- Confidence drops (fewer perspectives)
- Responses still correct, just less certain

Plug B back in:
- Full confidence restored
- No reconfiguration needed
```

## Success Metrics

| Metric | Target | Notes |
|--------|--------|-------|
| Inference rate | >10 kHz | On LP Core |
| Mesh update rate | 100 Hz | Hidden state broadcast |
| Interference latency | <1 ms | From receive to incorporate |
| Power per node | <5 mA avg | LP Core + radio bursts |
| Crystallization time | <1000 ticks | For repeated patterns |
| Fault recovery | <100 ms | When node returns |

## Implementation Plan

### Phase 1: LP Core CfC (Day 1)
- [ ] Port cfc_turbo to LP Core
- [ ] Verify 94kHz on LP Core
- [ ] HP Core sleep/wake on anomaly

### Phase 2: Hologram Engine (Day 1-2)
- [ ] Implement hologram_node_t
- [ ] Implement hologram_tick()
- [ ] Test interference computation
- [ ] Test entropy tracking

### Phase 3: Mesh Protocol (Day 2)
- [ ] IEEE 802.15.4 initialization
- [ ] Packet broadcast/receive
- [ ] Neighbor management
- [ ] Test with 2 nodes

### Phase 4: Integration (Day 3)
- [ ] Full 3-node demo
- [ ] Button/LED integration
- [ ] Learning demo (crystallization)
- [ ] Fault tolerance demo

### Phase 5: Polish (Day 4)
- [ ] Serial monitor visualization
- [ ] Statistics reporting
- [ ] Documentation
- [ ] Video demo

## Files to Create

```
reflex-os/include/
├── reflex_hologram.h      # Core hologram structures and tick
├── reflex_lp_cfc.h        # CfC on LP Core
├── reflex_mesh.h          # IEEE 802.15.4 protocol
└── reflex_hologram_demo.h # Demo helpers

reflex-os/main/
├── hologram_demo.c        # Main demo entry point
└── lp_core/
    └── lp_hologram.c      # LP Core firmware
```

## The Punchline

Three $5 chips.

Each one IS the brain, not a part of it.

The interference pattern between them is the intelligence.

Cut one out: the brain still works (lower resolution).
Add more: the brain gets sharper (higher resolution).
No training: entropy dynamics learn from experience.
No coordinator: interference IS consensus.

This is not distributed computing.
This is a holographic mind.

---

*"The image isn't on the film. The image isn't in the laser.
The image is in the interference of the light."*
