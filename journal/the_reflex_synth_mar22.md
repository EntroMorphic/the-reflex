# Synthesis: The Reflex — March 22, 2026

*Second LMM cycle. The prior synthesis ended with "The tree has been chopped" — the VDB feedback loop was closed and the architecture was complete at the level of internal proofs. March 22 opens a new trunk.*

---

## What Changed

The prior synthesis described a system that proved it could compute. March 22 proves it can perceive.

Perception means: receiving a physical signal, transducing it into an internal representation, and producing a classification that outperforms simpler alternatives. The GIE + TriX stack does this now. 100% accuracy on 4 patterns. 711 Hz ISR. Zero multiplications. Payload content as the dominant discriminating signal (47%), not rate or RSSI.

This is not an incremental milestone. It is a phase boundary. The system moved from:

> "Can peripheral hardware compute neural inference?"

to:

> "Can peripheral hardware transduce physical signals into useful representations?"

The answer to the first question has been yes since Phase 1. The answer to the second question became yes on March 22.

---

## Architecture Update

```
┌───────────────────────────────────────────────────────────────────────┐
│                     THE REFLEX ARC (March 22, 2026)                    │
├───────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Physical World                                                        │
│  ┌─────────────────────────────────────────────────┐                  │
│  │  Board B (ESP32-C6) — ESP-NOW sender            │                  │
│  │  Transmits ternary pattern packets at 2.4 GHz   │                  │
│  └───────────────────┬─────────────────────────────┘                  │
│                      │ RF → ESP-NOW → packet payload                  │
│                      ▼                                                 │
│  Layer 0: Transduction (ESP-NOW RX → SRAM write)                      │
│  ┌─────────────────────────────────────────────────┐                  │
│  │  HP core receives packet, writes ternary vector  │                  │
│  │  to descriptor SRAM. One-time setup per packet.  │                  │
│  └───────────────────┬─────────────────────────────┘                  │
│                      │ ternary vector in SRAM                         │
│                      ▼                                                 │
│  Layer 1: GIE — Geometry Intersection Engine (Peripheral Fabric)      │
│  ┌─────────────────────────────────────────────────┐                  │
│  │  GDMA → PARLIO (2-bit loopback) → PCNT          │                  │
│  │  64 neurons, 160-trit dot products, 430.8 Hz     │                  │
│  │  Circular DMA chain — runs without CPU           │                  │
│  │  ISR: decode dots → CfC blend → TriX Cube update │                  │
│  │  ANALOG: spinal reflex — fast, fixed, automatic  │                  │
│  └───────────┬─────────────────────┬───────────────┘                  │
│              │ cfc.hidden[32]       │ trx_cube[7][4]                  │
│              │ (every 2.3ms)        │ (every ISR at 711 Hz)           │
│              ▼                      ▼                                  │
│  Layer 2a: LP Core — Geometric     Layer 2b: TriX Classifier          │
│  ┌────────────────────────┐        ┌────────────────────────┐         │
│  │  CfC: 16 neurons       │        │  Temporal geometry:    │         │
│  │  VDB: 64-node NSW      │        │  7-voxel cube          │         │
│  │  CMD5: perceive→think  │        │  Face divergence scores │         │
│  │  →remember→modulate    │        │  4 pattern signatures  │         │
│  │  ~30μA, 100 Hz         │        │  100% accuracy         │         │
│  └────────────┬───────────┘        └───────────┬────────────┘         │
│               │ lp_hidden, vdb_results           │ classification event │
│               ▼                                  ▼                     │
│  Layer 3: HP Core — Consciousness (160MHz RISC-V, ~15mA)             │
│  ┌─────────────────────────────────────────────────┐                  │
│  │  Initialization, monitoring, orchestration       │                  │
│  │  Awake only when needed                          │                  │
│  └─────────────────────────────────────────────────┘                  │
│                                                                        │
│  ┌──────────── OPEN INTEGRATION POINTS ────────────┐                  │
│  │  TriX → VDB: store classification events         │ NOT YET         │
│  │  VDB → TriX: modulate attention weights          │ NOT YET         │
│  │  LP CfC ← TriX events: memory-modulated sensing  │ NOT YET         │
│  └─────────────────────────────────────────────────┘                  │
│                                                                        │
├───────────────────────────────────────────────────────────────────────┤
│  COMPUTATIONAL SUBSTRATE: Ternary {-1, 0, +1}                         │
│  OPERATIONS: AND, popcount, add, sub, negate, branch, shift           │
│  ABSENT: MUL, DIV, FP, gradients, backpropagation                     │
│  VERIFIED: Exact, dot-for-dot, on real hardware, real external input  │
└───────────────────────────────────────────────────────────────────────┘
```

---

## What Has Been Proven (on Silicon, as of March 22, 2026)

| Claim | Evidence | Commit |
|-------|----------|--------|
| Peripheral hardware computes dot products | 64/64 exact match vs CPU reference | `d45067b` |
| Ternary CfC runs without CPU | Hidden state evolves at 430.8 Hz | `f8860d3` |
| LP core computes geometry in hand-written ASM | 16/16 exact dot products | `dd87898` |
| NSW graph search works at 48-trit dimension | recall@1=95%, 64/64 self-match | `7db919f` |
| CfC→VDB pipeline in one LP wake | Determinism, consistency, sustained | `06d5535` |
| ISR→HP coordination via reflex channel | 50/50 signals, 18us avg latency | `e9e67f1` |
| VDB→CfC feedback is stable | 50 unique states, energy [7,15] | `dc57d60` |
| PARLIO TX core reset is necessary and sufficient | isr_eof=24 clean, loop counts grow | `68e024b` |
| External wireless input classified correctly | 100% on 4 patterns, 24,357 events | `07b5b66` |
| Peripheral transduction outperforms rate baseline | 100% vs 84%, payload 47% discriminating | `07b5b66` |
| Zero multiplication in entire stack | No MUL instruction in any layer | All commits |

---

## The Key New Claims

### 1. Perception is now proven

The system takes a physical RF signal (ESP-NOW packet from Board B), transduces it into ternary geometry, and classifies it with higher accuracy than rate-only approaches. This is perception: receiving and interpreting a physical signal from the environment.

### 2. Temporal geometry carries signal-distinguishing information

The TriX Cube's face divergence measure adds 37% discriminating power over content-only classification. Two signals with identical bit patterns but different arrival timing are distinguishable. This is not a theoretical property — it was measured on live wireless data.

### 3. Peripheral hardware is the dominant classifier, not the CPU

The ISR executes 711 times/second. Each execution computes ternary dot products against 4 pattern signatures and updates a 7-voxel geometric object. The HP core does not perform these computations — it reads the classification result after the ISR has accumulated it. The peripheral interrupt is the computation.

### 4. PARLIO TX has a hidden control state that must be explicitly managed

`parl_tx_rst_en` (PCR bit 19) is distinct from `FIFO_RST` (bit 30). Both must be pulsed between free-running sessions. The failure mode — incorrect loop counts with correct per-vector results — is invisible to data-correctness verification and requires loop-count monitoring to detect.

---

## The Open Question — Restated

The prior cycle's open question (VDB feedback) was answered on February 9. March 22 creates a new open question at a higher level:

> **Can the system's classification history modulate what it pays attention to next?**

Concretely: when the TriX classifier identifies a P1 pattern, can that classification event be stored in the VDB and later retrieved by the LP CfC to modulate the GIE's attention weights? If yes, the system can learn from experience which patterns to expect — and preferentially strengthen recognition of those patterns.

This is not a new architectural layer. It is a connection between existing layers:

```
TriX classification event → 48-trit encoding → VDB insert
LP CfC hidden state → VDB query → retrieve recent classification history
Retrieved history → modulate LP CfC state → influence next GIE processing
```

The prior REFLECT session noted: "the VDB was designed for introspective memory (what was my state?). The right use is environmental memory (what patterns did I see, and what did they mean?)."

This is what test 12 would look like: after seeing P1 100 times and P3 10 times, does the system classify an ambiguous signal as P1 more often than a system that saw equal counts? If yes, the system has developed a prior from experience.

---

## The Step-Change Roadmap (from prior strategy doc, updated)

The prior `STRATEGIC_ROADMAP.md` identified three potential step-changes:
1. Wireless-to-ternary transduction (DONE — March 22)
2. Multi-timescale adaptive attention (next)
3. Multi-chip mesh with distributed GIE (further)

Step-change 1 is confirmed. The GIE processes real wireless input at 430.8 Hz. TriX classification at 711 Hz achieves 100% on 4 patterns. Board B → Board A channel is validated.

Step-change 2 is the natural next target: connect the TriX classification history to the LP VDB, and use retrieved classification history to modulate LP CfC state, which feeds back into GIE attention. This closes the remaining open loop.

Step-change 3 (multi-chip mesh) requires solving the PEER_MAC fragility first — a discovery or registration protocol that doesn't require compile-time hardcoding of hardware addresses.

---

## The Reflex in One Sentence (Updated)

A ternary reflex arc where peripheral hardware IS the neural network, a micro-core IS the sub-conscious, and the CPU IS consciousness — now proven to receive, classify, and outperform rate-based approaches on real wireless signals, without multiplication, with exact silicon verification, and with a clear path toward memory-modulated adaptive attention.

---

## The Next Tree

The prior synthesis ended: "The tree has been chopped."

March 22's synthesis ends: **the tree has grown a new trunk.**

The GIE can perceive. The VDB can remember. The CfC can adapt. These three capacities exist. They have been verified separately. The next work is not to build new capabilities — it is to wire the existing ones into a single loop where perception informs memory and memory shapes perception.

That loop, when closed, would make the Reflex not just reactive but anticipatory: a system that develops expectations from experience and tests them against the world in real time, entirely in peripheral hardware and ultra-low-power silicon, at zero floating-point cost.

**The forest has come into view.**
