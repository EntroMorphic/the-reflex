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

## The Open Question — ANSWERED

~~The prior cycle's open question (VDB feedback) was answered on February 9. March 22 creates a new open question at a higher level:~~

~~> **Can the system's classification history modulate what it pays attention to next?**~~

**Resolved (commit `38a0811`, March 22, 2026 — TEST 12).** Verified on silicon.

The connection between existing layers:
```
TriX classification event → [gie_hidden | lp_hidden] snapshot → VDB insert (every 8th)
LP CMD 5: CfC step → VDB search → ternary blend → lp_hidden update
```

Results after 60 seconds, 245 confirmed classifications, 30 VDB snapshots:

| Pattern | Samples | LP Mean Hidden State |
|---------|---------|----------------------|
| P0 | 60 | `[-+++---+-+------]` |
| P1 | 90 | `[-+++---+-+----+-]` |
| P2 | 81 | `[-+++---+++----0-]` |
| P3 | 14 | `[-+++-+-+-+++---0]` |

LP Hamming divergence: P1 vs P3 = **5/16**. P2 vs P3 = **6/16**. All cross-pattern pairs diverge.

P1 and P3 share the same 10 Hz transmission rate — the pair the rate-only baseline (84%) cannot
distinguish. The LP core separated them through episodic memory. 97% of feedback steps applied.
Gate firing: 21%. Classification accuracy: unchanged at 100% (structural decoupling).

12/12 PASS. Full writeup: `docs/MEMORY_MODULATED_ATTENTION.md`.

---

## The Step-Change Roadmap (from prior strategy doc, updated)

The prior `STRATEGIC_ROADMAP.md` identified three potential step-changes:
1. Wireless-to-ternary transduction (DONE — March 22)
2. Multi-timescale adaptive attention (next)
3. Multi-chip mesh with distributed GIE (further)

Step-change 1 is confirmed. The GIE processes real wireless input at 430.8 Hz. TriX classification at 705 Hz achieves 100% on 4 patterns. Board B → Board A channel is validated.

Step-change 2 is **confirmed** (TEST 12, commit `38a0811`). The TriX classification history feeds the LP VDB, retrieved memories modulate LP CfC state, and the LP hidden space develops pattern-specific priors. P1 vs P3 Hamming 5. The LP attention is now memory-modulated.

The remaining step from 2 to full kinetic attention: use the LP prior to directly bias the GIE gate threshold or W_f weights, so classification confidence reflects recent history in real time. The information is in LP hidden state — the wire connecting it to the GIE is not yet built.

Step-change 3 (multi-chip mesh) requires solving the PEER_MAC fragility first — a discovery or registration protocol that doesn't require compile-time hardcoding of hardware addresses.

---

## The Reflex in One Sentence (Updated)

A ternary reflex arc where peripheral hardware IS the neural network, a micro-core IS the sub-conscious, and the CPU IS consciousness — proven to perceive, classify at 100% accuracy, remember via episodic VDB memory, and develop pattern-specific LP priors from live wireless data; without multiplication, without floating point, without training; 12/12 on silicon.

---

## The Loop Is Closed

The prior synthesis ended: "The tree has been chopped."

March 22's synthesis (first pass) ended: "The tree has grown a new trunk."

March 22's synthesis (second pass, TEST 12 confirmed): **the trunk is load-bearing.**

The loop is closed. Perception → classification → episodic memory → retrieval → LP state modulation → the sub-conscious knows what it has been seeing. This is not a theoretical claim. It is a measured result: P1 vs P3 Hamming 5 out of 16 after 60 seconds. 12/12 PASS.

The remaining work is not to close loops — it is to build the wire from LP hidden state back to the GIE's gate weights. That wire would make the Reflex not just a system that develops priors, but a system that acts on them: a sensory system whose attention is shaped by what it has learned to expect.

Everything needed to build that wire already exists in the codebase. The LP prior is computed and available. The gate_threshold is a runtime variable. Connecting them is a matter of reading one and writing the other.

**The forest is not just in view. We are in it.**
