# Milestone Progression: Geometry Intersection Engine

Six milestones verified on silicon. Each built on the last. Each was verified on an ESP32-C6FH4 (QFN32) rev v0.2.

---

## Milestone 1: Sub-CPU ALU

**Tests:** 59/59 | **Commit:** `f41d5ea` | **File:** `reflex-os/main/alu_fabric.c`

**What it proved:** PCNT level-gated edge counting can implement boolean logic gates. AND, OR, XOR, NOT, SHL/SHR, 2-bit ADD, 2-bit MUL, NAND, NOR — all computed by PCNT counting edges from PARLIO while a gate signal controls the level input.

**Signal path:** CPU triggers PARLIO TX → GPIO 4-7 (4-bit mode) → PCNT counts edges gated by level → CPU reads result.

**Architecture:** PARLIO 4-bit mode. Each nibble drives GPIO 4-7 simultaneously. PCNT counts rising edges on one GPIO gated by the level of another. CPU orchestrates each operation.

**Key learning:** The peripheral fabric can compute. PCNT + PARLIO + GPIO form a boolean evaluation unit.

---

## Milestone 2: Autonomous Computation Fabric

**Tests:** 5/5 | **Commit:** `5b2f62d` | **File:** `reflex-os/main/raid_etm_fabric.c`

**What it proved:** The peripheral loop can run without CPU intervention. ETM crossbar wires Timer → GDMA → PARLIO → GPIO → PCNT → ETM feedback. 5004 state transitions in 500ms with CPU in NOP loop.

**Errata discovered:** Used incorrect ETM register addresses (0x600B8000 instead of correct 0x60013000). The "autonomous" transitions actually came from PCNT ISR callbacks, not true ETM crossbar routing. Corrected in Milestone 3.

**Key learning:** Autonomous loop concept is valid even though the implementation had a bug. The architecture is right; the addresses were wrong.

---

## Milestone 3: Autonomous ALU

**Tests:** 9/9 | **Commit:** `59d0bba` | **File:** `reflex-os/main/autonomous_alu.c`

**What it proved:** CPU configures peripherals, loads pattern buffers into SRAM as DMA descriptors, kicks a timer, enters a NOP loop. All gate evaluation runs in hardware. GDMA descriptor chains execute gate sequences autonomously — the chain test (XOR then AND) proves instruction sequencing without CPU.

**Signal path:** Timer alarm → ETM → GDMA start → PARLIO TX → GPIO 4,5 → PCNT level-gated counting → PCNT threshold → ISR callback.

**Errata discovered:**
- ETM base address is 0x60013000, not 0x600B8000
- IDF startup disables ETM bus clock (must re-enable via PCR)
- GDMA LINK_START leaks data despite ETM_EN (~10-17 stray edges)
- PARLIO nibble boundaries create PCNT glitch counts (~6-17 stray)
- GDMA with ETM_EN won't auto-follow linked list descriptors
- LEDC timer cannot be resumed after ETM-triggered pause

**Key learning:** Six hardware errata discovered and documented. The ETM crossbar works but requires precise clock enable sequencing and correct addresses. GDMA descriptor chaining requires normal mode (no ETM_EN).

---

## Milestone 4: Ternary TMUL

**Tests:** 9/9 | **Commit:** `66469ce` | **File:** `reflex-os/main/ternary_alu.c`

**What it proved:** Transition from boolean gates to ternary arithmetic. A trit value {-1, 0, +1} is represented by two GPIO lines: X_pos and X_neg. PARLIO 2-bit mode drives these lines. Two PCNT units count "agree" (same-sign) and "disagree" (opposite-sign) edges, gated by static Y levels on GPIO 6,7. The ternary multiply result is agree - disagree.

**Architecture change:** Switched from 4-bit PARLIO (M1-3) to 2-bit PARLIO. This eliminated the nibble-boundary glitch problem entirely — with 2-bit mode, each dibit maps directly to one (X_pos, X_neg) state with no cross-nibble transitions.

**Signal path:** GDMA → PARLIO TX (2-bit) → GPIO 4 (X_pos), GPIO 5 (X_neg) → PCNT0 (agree), PCNT1 (disagree).

**Key decision:** Two PCNT units for TMUL (agree + disagree) rather than one. A single PCNT unit has only 2 channels; handling all 4 gating combinations (X_pos×Y_pos, X_neg×Y_neg, X_pos×Y_neg, X_neg×Y_pos) requires 4 channels across 2 units.

---

## Milestone 5: 256-Trit Dot Product

**Tests:** 10/10 | **File:** `reflex-os/main/geometry_dot.c`

**What it proved:** Scaled from single-trit operations to 128-256 trit vector dot products via DMA descriptor chains. PCNT accumulates across multiple DMA buffers without reset — the count at the end is the dot product of the entire vector.

**Zero-interleave encoding:** Each trit occupies 2 dibit slots: (value, then 00). This guarantees exactly one clean rising edge per non-zero trit, regardless of surrounding trit values. 64 bytes = 128 trits per buffer. Chains of 2-4 buffers give 256-512 trits.

**Key errata resolution:** GDMA with ETM_EN won't follow descriptor chains (M3 errata). Solution: run GDMA in normal mode (no ETM_EN), use PARLIO TX_START as the gate. DMA fills PARLIO FIFO immediately on LINK_START, but data doesn't reach GPIOs until TX_START is set. This allows: arm DMA → clear PCNT → set TX_START → data flows → DMA EOF.

**Triple PCNT clear:** After GDMA LINK_START, some data leaks through PARLIO FIFO to GPIOs before TX_START is set. Three consecutive PCNT clears with delays are needed to ensure a clean zero baseline before the real data flows.

**Performance:**
- 128 trits (1 buffer): 1013 us
- 256 trits (2 buffers): 1525 us
- 512 trits (4 buffers): 2550 us

**Key learning:** Descriptor chain accumulation works. The PCNT counter is the dot product accumulator. Silence (dibit 00) is structure — zero trits produce no edges, contributing nothing to the count. Sparsity is native.

---

## Milestone 6: Multi-Neuron Layer Evaluation

**Tests:** 6/6 | **File:** `reflex-os/main/geometry_layer.c`

**What it proved:** Full neural network layer evaluation. N neurons, each with weight vector W and shared input X. CPU pre-computes P[i] = W[i] × X[i] (ternary multiply = sign flip, ~200 cycles for 256 trits), encodes P into DMA buffers, hardware sums via PCNT. Ternary activation function: sign(dot) → {-1, 0, +1}.

**2-layer feedforward network:** 8→4 neurons (dim=128), verified end-to-end against CPU reference. Layer 1 output feeds layer 2 input. All results match.

**Architecture decision — RMT has no DMA on ESP32-C6:** The original plan (from the GIE synthesis notes) was to use RMT TX for Y geometry, both X and Y DMA-driven. Research revealed ESP32-C6 RMT has no DMA support (unlike ESP32-S3). RMT uses 48-word ping-pong buffers refilled by ISR — CPU in the loop. Decision: keep Y as static CPU-driven levels. The CPU's pre-multiply of P[i] = W[i] × X[i] embeds the Y information into the X stream. The hardware only needs to sum.

**Performance (32 neurons, dim=256):**
- 425 neurons/s
- 108.8K trit-MACs/s
- 1525 us/neuron (hardware eval)
- 2353 us/neuron (total including CPU prep)
- Hardware utilization: 65% (1525/2353)

**Key learning:** The CPU pre-multiply is cheap (~200 cycles) relative to the 1ms hardware eval. The bottleneck is PARLIO clock speed (1 MHz). Increasing to 10-20 MHz (PCNT rated for 40 MHz) would give 10-20x throughput with no architectural change.

---

## What's Next

### Milestone 7: Self-Sequencing Fabric

REGDMA advances descriptor chain pointers between neuron evaluations. ETM topology reconfigures between phases. LP core manages inter-layer sequencing. Main CPU sleeps during inference.

**Blocking unknown:** Can REGDMA target GDMA descriptor pointer registers? Can it be triggered by ETM?

### Milestone 8: Performance Optimization

Increase PARLIO clock to 10-20 MHz. Verify PCNT edge counting at higher rates. Target: 1M+ trit-MACs/s.

---

## Errata Summary by Milestone

| Milestone | Errata | Resolution |
|-----------|--------|------------|
| M2 | ETM base address wrong (0x600B8000) | Correct address: 0x60013000 |
| M3 | IDF disables ETM bus clock on startup | Re-enable via PCR_SOC_ETM_CONF |
| M3 | GDMA LINK_START leaks despite ETM_EN | Defer PARLIO TX_START after PCNT clear |
| M3 | PARLIO 4-bit nibble boundary glitches | Threshold above noise floor; eliminated in M4 by switching to 2-bit |
| M3 | GDMA+ETM_EN won't follow descriptor chains | Use normal GDMA mode, gate with PARLIO TX_START |
| M3 | LEDC timer unresumable after ETM pause | Use PCNT ISR instead of LEDC for result detection |
| M5 | GDMA LINK_START leaks to PARLIO FIFO | Triple PCNT clear with delays |
| M6 | ESP32-C6 RMT has no DMA support | Keep Y static; pre-multiply W×X on CPU |

---

## GPIO Pin Map (Milestone 6)

| GPIO | Function | Direction |
|------|----------|-----------|
| 4 | X_pos (PARLIO bit 0) | Output (loopback to PCNT) |
| 5 | X_neg (PARLIO bit 1) | Output (loopback to PCNT) |
| 6 | Y_pos (static level) | Output (CPU-driven) |
| 7 | Y_neg (static level) | Output (CPU-driven) |
| 8 | Classification: positive | Output |
| 9 | Classification: negative | Output |

## Peripheral Allocation (Milestone 6)

| Peripheral | Usage |
|------------|-------|
| PARLIO TX | 2-bit mode, 1MHz, GPIO 4-5, io_loop_back |
| GDMA CH0 (OUT) | Owned by PARLIO driver, bare-metal reconfigured |
| PCNT Unit 0 | Agree: X_pos gated by Y_pos + X_neg gated by Y_neg |
| PCNT Unit 1 | Disagree: X_pos gated by Y_neg + X_neg gated by Y_pos |
| GPTimer 0 | Kickoff trigger (ETM-enabled) |
| ETM | Clock enable only (no ETM_EN on GDMA in M5-6) |
