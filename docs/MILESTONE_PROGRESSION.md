# Milestone Progression: The Reflex

36 milestones verified on silicon. From boolean gates to real-world wireless pattern classification at hardware rate. Each verified on an ESP32-C6FH4 (QFN32) rev v0.2.

**Last Updated:** February 10, 2026

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

## Milestone 7: Ternary CfC — Closed-form Continuous-time Liquid Network

**Tests:** 6/6 | **File:** `reflex-os/main/geometry_cfc.c`

**What it proved:** The first fully-ternary CfC (Closed-form Continuous-time) liquid neural network. Everything is {-1, 0, +1}: weights, inputs, hidden state, activations. No binary activations. No floating point. No sigmoid LUTs.

**CfC update equation (ternary):**
```
concat = [input | hidden]                      // implicit in weight layout
f[n]   = sign(dot(concat, W_f[n]) + b_f[n])   // gate trit:      {-1, 0, +1}
g[n]   = sign(dot(concat, W_g[n]) + b_g[n])   // candidate trit: {-1, 0, +1}
h_new  = (f == 0) ? h_old : f * g             // ternary blend
```

**Three blend modes (the key novelty):**
- **f = +1 (UPDATE):** Accept candidate. Excitatory — follow the evidence.
- **f =  0 (HOLD):** Keep current state. Memory — maintain belief.
- **f = -1 (INVERT):** Negate candidate. Inhibitory — actively oppose the evidence.

The inversion mode is unique to ternary CfC. Binary CfC (Hasani et al.) has only update and hold. The third mode creates natural inhibition and oscillatory dynamics that binary CfC cannot express as a first-class operation.

**Zero-copy concatenation:** The weight matrix layout `W[0..input_dim-1]` maps to input, `W[input_dim..concat_dim-1]` maps to hidden state. No concat buffer is allocated or copied. The pre-multiply loop walks input[] and hidden[] directly from their permanent SRAM locations. The "concatenation" is a compile-time convention, not a runtime operation.

**Hardware mapping:**
- GIE computes all dot products (64 per step: 32 for f-pathway, 32 for g-pathway)
- CPU does blend: `tmul(f, g)` + conditional store (~3 instructions per neuron)
- Dimensions: input=128, hidden=32, concat=160 (2 DMA buffers)

**Novel findings verified on silicon:**
1. **Inversion creates oscillation:** Test 6 confirmed period-2 limit cycles from inhibitory neurons. 32 inversions across 8 steps with constant input. Binary CfC converges to a fixed point; ternary CfC oscillates.
2. **Convergence resistance:** Test 5 showed the network still evolving at step 15 with constant input (delta=5, energy=32/32). The inversion mode prevents collapse into a static attractor.
3. **Stem cell analogy:** The sustained high-energy, uncommitted state under constant stimulus resembles biological pluripotency. The three modes map to differentiation (update), quiescence (hold), and self-renewal (invert). See `notes/lmm/ternary_cfc_stem_cell.md`.

**Performance at 1MHz PARLIO:**
- 6.7 Hz inference (149ms/step, 64 dot products × 160 trits each)
- Projected at 10MHz: ~67 Hz
- Projected at 20MHz: ~134 Hz

**Errata:** `ternary_cfc_t` struct (~16KB) causes stack overflow when allocated on the default FreeRTOS task stack (3.5KB). Resolution: declare as `static` (BSS segment).

---

## Milestone 8 v1: Giant DMA Chain (64 Neurons)

**Tests:** 4/4 | **Commit:** `28ff786` | **File:** `reflex-os/main/geometry_cfc_m8.c`

**What it proved:** Scaled from 32 neurons to 64 neurons using a giant circular DMA descriptor chain. Cumulative PCNT sum across all 64 neurons verified against CPU reference.

**Architecture:** Circular chain: `[dummy×5][neuron×64][separator with EOF=1 → back to dummy0]`. The chain loops forever. ISR fires on each neuron's EOF, reads PCNT, and re-encodes the next neuron's products.

**Key decision:** 5 dummy descriptors at the start of the chain provide time for the ISR to re-encode neuron 0 before the chain wraps around.

---

## Milestone 8 v2: Per-Neuron ISR Capture

**Tests:** 3/3 | **Commit:** `1980c9a` | **File:** `reflex-os/main/geometry_cfc_m8_etm.c`

**What it proved:** Per-neuron ISR capture with 64/64 exact dots across repeated runs. The ISR reads PCNT after each neuron's DMA EOF, decodes the dot product, and prepares for the next neuron. Stable across multiple consecutive runs.

**Key errata discovered:** PCNT clock domain crossing race — reading PCNT too soon after DMA EOF returns stale values. Resolution: 200 volatile delay loops (~5us) in ISR before PCNT read.

---

## Milestone 9: 10MHz PARLIO

**Tests:** 6/6 | **Commit:** `28ff786`

**What it proved:** PARLIO clock increased from 1MHz to 10MHz. PCNT keeps up at the higher rate. Zero errors across all 64 neurons. This 10x clock increase gives the CfC enough headroom to run at useful rates.

**Key finding:** PCNT is rated for 40MHz, so 10MHz is well within spec. The bottleneck shifts from wire time to ISR re-encode time (~20us per neuron). At 10MHz, the CfC achieves ~428 Hz.

---

## Milestone 10: Differentiation Experiment

**Tests:** 4/5 predictions confirmed | **Commit:** `06e81e8`

**What it proved:** Tested the stem cell hypothesis from M7. Ran the ternary CfC with constant input (stem regime), then applied a sharp input change (differentiation signal). 4 of 5 predictions confirmed:

1. Sustained dynamics under constant input (confirmed)
2. Non-zero energy maintained (confirmed)
3. Period-2 oscillation from inversions (confirmed)
4. Input change triggers state transition (confirmed)
5. Convergence to fixed point after differentiation (partially — convergence is slower than predicted)

---

## CfC on GIE: Autonomous Ternary CfC

**Tests:** 6/6 | **Commit:** `2272eb4` | **File:** `reflex-os/main/geometry_cfc_freerun.c`

**What it proved:** The ternary CfC runs autonomously on the GIE. The ISR handles the complete CfC update: read PCNT → decode dot → sign activation → ternary blend → re-encode next neuron. 64 neurons per loop, 428 Hz, zero CPU involvement after init.

**Architecture change:** Merged ISR-driven CfC blend into the free-running loop. The ISR does: drain PCNT (200 loops), read agree/disagree counts, compute dot = agree - disagree, apply CfC blend (h_new = f==0 ? h_old : f*g), re-encode next neuron's W×X products. All within the ~86us inter-neuron window.

---

## PCNT Drain Fix

**Verified** | **Commit:** `ae3368f`

**What it fixed:** Clock domain crossing race between GDMA/PARLIO (APB clock) and PCNT (its own clock domain). Reading PCNT immediately after DMA EOF returns stale counts. The fix: 200 volatile read loops (~5us) in the ISR to drain the PCNT pipeline before reading the final count. Eliminated all intermittent count errors.

---

## Free-Run: Free-Running Sub-CPU Neural Network

**Tests:** 3/3 | **Commit:** `f8860d3` | **File:** `reflex-os/main/geometry_cfc_freerun.c`

**What it proved:** The complete free-running architecture: circular DMA chain loops forever, ISR processes each neuron, CfC hidden state evolves autonomously at 428 Hz. 64/64 exact dot products verified against CPU reference in a single loop.

**Key errata discovered:**
- **LP SRAM bus contention:** Writing LP SRAM from LEVEL3 ISR stalls the ISR and breaks PCNT timing. Resolution: write LP SRAM from main loop context only.
- **GPIO output re-enable:** After `init_pcnt()`, must call `gpio_set_direction(GPIO_Y_POS/Y_NEG, GPIO_MODE_INPUT_OUTPUT)` because the PCNT driver's `gpio_func_sel()` disables output enable on level pins.

---

## LP Core C: LP Core Geometric Processor

**Tests:** 4/4 | **Commit:** `9b80ff0`

**What it proved:** The LP core (16MHz RISC-V, ~30uA) can run ternary CfC inference using geometric operations: INTERSECT (AND + popcount), PROJECT (sign), GATE (branch + negate). First implementation in C, compiled by GCC.

**Architecture:** LP core wakes every 10ms (LP timer), reads `gie_hidden[32]` + `lp_hidden[16]`, packs to 48 trits, runs 16 neurons (each: 2 INTERSECT + 1 PROJECT + 1 GATE), updates `lp_hidden`, computes majority-vote decision.

---

## LP Core ASM: Hand-Written RISC-V Assembly

**Tests:** 4/4 | **Commit:** `dd87898` | **File:** `reflex-os/main/ulp/main.S`

**What it proved:** The LP core CfC rewritten in hand-assembled RISC-V. Every instruction specified — no compiler decisions, no hidden register allocation, no spills. 16/16 exact dot products match CPU reference.

**Key techniques:**
- **POPCOUNT:** 256-byte lookup table. 4 lookups per 32-bit word (one per byte). Inline macro.
- **INTERSECT (unrolled):** 3 words × 4 ANDs × inline popcount ≈ 200 instructions per dot product.
- **Zero-copy concatenation:** Weight layout `W[0..31]` maps to `gie_hidden`, `W[32..47]` maps to `lp_hidden`. No concat buffer.
- **96-byte stack frame:** `sp+0..23` packed trits, `sp+24..39` h_new, `sp+40..92` saved regs.

---

## VDB Initial: Ternary Vector Database on LP Core

**Tests:** 5/5 | **Commit:** `d0ea002`

**What it proved:** A ternary vector database running entirely on the LP core. 16 nodes, 48 trits per vector, brute-force search returning the single best match. Verified exact against CPU reference.

**Architecture:** Vectors packed as (pos_mask, neg_mask) pairs — 3 words each, 24 bytes per vector. INTERSECT_LOOP (looped version, smaller code than unrolled) computes dot(query, node) for each stored vector.

---

## VDB Top-K: Top-K=4 Sorted Search

**Tests:** 5/5 | **Commit:** `6ea0497`

**What it proved:** VDB search returns top-4 results sorted descending by score. Insertion sort implemented in assembly: compare new score against worst of K, bubble up if better. Max 3 comparisons per insert.

---

## VDB M2: 64-Node Scale + Latency Benchmark

**Tests:** 5/5 | **Commit:** `2c6dd17`

**What it proved:** VDB works at full capacity (64 nodes). Self-match exact for all 64. Expanded to 32-byte nodes (24B vector + 8B graph metadata). Latency measured: well within 10ms wake period.

**Node layout (32 bytes):**
```
[0..11]    pos_mask[3]       (3 × uint32_t = 12 bytes)
[12..23]   neg_mask[3]       (3 × uint32_t = 12 bytes)
[24..30]   neighbors[7]      (7 × uint8_t neighbor IDs)
[31]       neighbor_count    (uint8_t, 0..7)
```

---

## VDB M3: HP-Side API

**Tests:** 5/5 | **Commit:** `fa54192` | **Files:** `reflex_vdb.h`, `reflex_vdb.c`

**What it proved:** Clean C API for the HP core to interact with the VDB: `vdb_insert()`, `vdb_search()`, `vdb_clear()`, `vdb_count()`, `vdb_last_visit_count()`. HP core packs trit vectors and writes to LP SRAM, dispatches commands, waits for LP core completion, reads results.

---

## VDB M4: NSW Graph Search

**Tests:** 6/6 | **Commit:** `7db919f`

**What it proved:** Navigable Small World graph search on the LP core. M=7 neighbors, ef=32 search width, dual entry points (node 0 + node N/2). recall@1=95%, recall@4=90%, 64/64 self-match, 64/64 connectivity.

**Optimized over 9 build-flash-test iterations:**

| Attempt | Change | Self-match | Recall@1 | Recall@4 |
|---------|--------|-----------|----------|----------|
| v1 | Beam=4 | 51/64 | 60% | 57% |
| v5 | M=6, remove diversity | 63/64 | 95% | 82% |
| v9 | M=7, dual entry | 64/64 | 95% | 90% |

**Key design decisions:**
- **No diversity heuristic** — plain top-M gives denser graphs for small N
- **M=7 fits in 32 bytes** — 7 neighbor bytes + 1 count byte in positions 24-31
- **Dual entry points** — node 0 + node N/2 ensures wide exploration
- **Reverse edge eviction critical** — removing it destroyed the graph to 5/64 reachable

**Insert algorithm (cmd=3):** Brute-force score all existing nodes. Top-14 candidates (2×M). Simple top-M selection. Forward edges. Reverse edges with weakest-edge replacement.

**Search algorithm (cmd=2):** Two-list search (candidates + results). Dual seed. Early termination when best candidate ≤ worst result. 64-bit visited bitset.

---

## VDB M5: CfC→VDB Pipeline

**Tests:** 4/4 | **Commit:** `06d5535` | **File:** `reflex-os/main/ulp/main.S`

**What it proved:** The LP core runs a complete perceive→think→remember loop in a single 10ms wake cycle. cmd=4 does: CfC step (pack trits, 32 INTERSECT calls, blend, commit hidden state), then copies the packed 48-trit input vector to VDB query BSS, then runs VDB search. Zero re-packing — the CfC's packed representation IS the VDB query.

**Architecture:** Sequential stack frames. CfC allocates 96B, runs, copies 6 words (24 bytes) from its stack to VDB query BSS, restores regs, deallocates. VDB allocates 608B, searches, restores, deallocates. Peak stack: 608B. Each function is independent — neither knows the other exists. A BSS flag (`lp_pipeline_flag`) routes the CfC exit to the bridge code for cmd=4.

**Verified:**
- 6a: Both step_count and search_count increment by exactly 1 per cmd=4
- 6b: cmd=4 produces identical hidden state and dot products as cmd=1
- 6c: Pipeline search results match standalone cmd=2 with same query
- 6d: 10/10 sustained pipeline steps with live GIE feeding

**HP-side API:** `vdb_cfc_pipeline_step(vdb_result_t *result)` — dispatches cmd=4, waits for both counters, reads results.

---

## Reflex Channel: ISR→HP Coordination

**Tests:** 7/7 | **Commit:** `e9e67f1` | **File:** `reflex-os/main/geometry_cfc_freerun.c`

**What it proved:** The original Reflex coordination primitive (`reflex_channel_t`) now bridges the GIE ISR (producer) to the HP main loop (consumer) on the ESP32-C6. The ISR signals the channel after committing a complete hidden state. RISC-V `fence rw, rw` ensures hidden[] is in SRAM before the sequence number becomes visible.

**Mechanism:** `reflex_signal(&gie_channel, loop_count)` writes value, captures cycle-accurate timestamp via CSR 0x7e2 (ESP32-C6 performance counter), executes `fence rw, rw`, increments sequence. The HP main loop spin-waits with `reflex_wait_timeout()` until the sequence changes, then reads with ordering guarantee.

**Performance:** 18,300ns average latency (min 16,337ns, max 21,612ns). This is the ISR's tail work (re-encode 64 neurons after setting timestamp), not the channel cost. The channel itself is store + fence + increment ≈ ~10 cycles.

**Key insight:** On the C6, this is SRAM ordering (no cache to snoop). On Thor, it's cache line invalidation. The primitive is the same; the physics underneath changes.

**Verified:**
- 7a: 50/50 signals received, zero value mismatches
- 7b: Latency — 100 samples, spin-wait measurement
- 7c: 64/64 dot consistency after channel wait vs CPU reference
- 7d: 20/20 channel-driven LP core feeds, 20/20 LP steps

---

## VDB→CfC Feedback Loop: Memory Shapes Inference

**Tests:** 8/8 | **Commit:** `dc57d60` | **Files:** `reflex-os/main/ulp/main.S`, `reflex-os/main/geometry_cfc_freerun.c`, `reflex_vdb.c`, `reflex_vdb.h`

**What it proved:** The feedback loop is closed. Retrieved VDB memories now modulate the CfC hidden state. CMD 5 in the LP core assembly runs: CfC step → VDB search → memory blend, all in a single LP wake cycle.

**Blend rule (ternary, in hand-written RISC-V assembly):**
```
For each of 16 lp_hidden trits:
  memory_trit = best VDB match's LP-hidden portion (trits 32..47)
  if h == mem:           no change  (agreement reinforces)
  if h == 0, mem != 0:   h = mem   (fill gap from memory)
  if h != 0, mem == 0:   no change (memory silent)
  if h != 0, mem != 0, h != mem:  h = 0  (conflict → HOLD)
```

**The HOLD damper:** Conflict between current state and retrieved memory creates zero states. The CfC's HOLD mode (f=0) preserves these zeros on the next step — the neuron becomes undecided and waits for new evidence. This is ternary inertia. It prevents feedback-driven oscillation.

**Stability proof (on silicon):**
- 50 steps of sustained feedback: **50 unique states in 50 steps** (no repeats, no lock-in)
- Energy bounded [7, 15] out of 16 (doesn't collapse or saturate)
- Max per-step change: 14/16 trits (bounded, never all-flip)
- 47/50 steps had feedback applied (score ≥ threshold of 8)

**Divergence proof:** Identical inputs through CMD 4 (no feedback) and CMD 5 (with feedback) produce different trajectories from step 0, with hamming distance growing to 14/16 by step 9. The feedback definitively shapes behavior.

**Observability:** 6 new LP SRAM variables — `fb_applied`, `fb_source_id`, `fb_score`, `fb_blend_count`, `fb_total_blends`, `fb_threshold` (HP-writable).

**Memory budget impact:** +24 bytes BSS (6 variables), +~570 bytes code (.text). Stack unchanged (reuses VDB frame). Free stack: 4,356 bytes.

**HP-side API:** `vdb_cfc_feedback_step(vdb_result_t *result)` — dispatches cmd=5, waits for step + search counters, reads results.

**Verified:**
- 8a: cmd=5 runs, all three counters increment (step, search, fb_total_blends)
- 8b: Feedback observability — source_id valid, score ≥ threshold, blend_count sensible
- 8c: 50 sustained feedback steps — 50 unique states, bounded energy, no oscillation
- 8d: Feedback vs no-feedback trajectories diverge from step 0

---

## What's Next

The feedback loop is closed (commit `dc57d60`). The system now perceives, thinks, remembers, and adapts — all in ternary, all on a $0.50 chip, all without multiplication.

**Open directions:**
- Multi-chip coordination (reflex channel across two C6 boards)
- Feedback threshold tuning and temporal windowing
- External sensor integration as GIE input
- The CfC's HOLD damping is proven stable — explore whether it generalizes to larger networks

---

## Errata Summary

| Source | Errata | Resolution |
|--------|--------|------------|
| M2 | ETM base address wrong (0x600B8000) | Correct address: 0x60013000 |
| M3 | IDF disables ETM bus clock on startup | Re-enable via PCR_SOC_ETM_CONF |
| M3 | GDMA LINK_START leaks despite ETM_EN | Defer PARLIO TX_START after PCNT clear |
| M3 | PARLIO 4-bit nibble boundary glitches | Fixed in M4: switched to 2-bit PARLIO |
| M3 | GDMA+ETM_EN won't follow descriptor chains | Use normal GDMA mode, gate with PARLIO TX_START |
| M3 | LEDC timer unresumable after ETM pause | Use PCNT ISR instead of LEDC |
| M5 | GDMA LINK_START leaks to PARLIO FIFO | Triple PCNT clear with delays |
| M6 | ESP32-C6 RMT has no DMA support | Keep Y static; pre-multiply W×X on CPU |
| M7 | `ternary_cfc_t` stack overflow (~16KB on 3.5KB stack) | Declare structs as `static` (BSS) |
| Free-run | PCNT clock domain crossing drain | 200 volatile loops (~5us) in ISR before read |
| Free-run | LP SRAM bus contention from ISR | Never write LP SRAM from ISR; main loop only |
| Free-run | GPIO output disabled after PCNT init | Re-enable with gpio_set_direction() |

---

## GPIO Pin Map (Current — Feedback Loop)

| GPIO | Function | Direction |
|------|----------|-----------|
| 4 | X_pos (PARLIO bit 0) | Output (loopback to PCNT) |
| 5 | X_neg (PARLIO bit 1) | Output (loopback to PCNT) |
| 6 | Y_pos (static level) | Output (CPU-driven) |
| 7 | Y_neg (static level) | Output (CPU-driven) |

---

## Milestone 28: TriX Signature Routing

**Tests:** 11/11 | **Commit:** `6b61da3` | **Result:** 78% classification

**What it proved:** Ternary signatures computed from observed ESP-NOW packets can classify which transmission pattern is active. Signatures are the tsign of accumulated input trits across all packets of each pattern. Classification = argmax of dot(sig[p], input). No training, no learned weights.

**Key learning:** The pattern-ID trits (bytes 16-23) provide 8 orthogonal features that guarantee signature separation. But the remaining 120 trits (RSSI, payload, timing) also contribute. Cross-dots between signatures range from 5 to 85 (vs self-dots of 96-120), providing clear separation.

## Milestone 29: Signatures as W_f Weights

**Tests:** 11/11 | **Commit:** `ce0f788` | **Result:** 90% classification

**What it proved:** TriX signatures installed as `W_f` gate weights (8 neurons per pattern) achieve 90% accuracy with per-packet voting. The gate fires when |f_dot| > threshold, and the dot product is maximal for the matching pattern. This is nearest-centroid classification executed by the GIE hardware.

## Milestone 30: Online Maintenance + Novelty Detection

**Tests:** 11/11 | **Commit:** `ef9dc69` | **Result:** 100% input-TriX

**What it proved:** Signatures re-signed every 16 packets per pattern (running average with exponential decay). Novelty gate rejects packets when best dot < 60. Both mechanisms maintain accuracy as the wireless channel drifts. Input-TriX (CPU-side classification from signatures) reaches 100%.

## Milestone 31: 7-Voxel TriX Cube

**Tests:** 11/11 | **Commit:** `d4f7ef0` | **Result:** Core 100%, ensemble 87%

**What it proved:** Core signature + 6 temporal face signatures (recent, prior, stable, transient, confident, uncertain). Each face observes packets from a different temporal perspective. Faces compute XOR masks against core — measuring where temporal displacement changes the signature. Core classification is authoritative; faces are sensors, not voters.

## Milestone 32: XOR Masks as Face Observables

**Tests:** 11/11 | **Commit:** `b117955` | **Result:** Core 100%, XOR mask data

**What it proved:** XOR masks decompose temporal displacement into RSSI (9%), PatID (6%), Payload (48%), Timing (37%) components. The payload and timing regions carry most of the temporal signal. Faces measure how the wireless channel changes over time — an intervention sensor.

## Milestone 33: ISR TriX Classification

**Tests:** 11/11 | **Commit:** `24ba035` → `fd338f5` | **Result:** ISR 100%, Core 100%

**What it proved:** The GIE ISR can classify in hardware at 430 Hz. The DMA race condition (main loop writing neuron_bufs while DMA streams them) was solved by moving input re-encode into ISR step 5b (when PARLIO is stopped). Clean-loop validation (all 8 neurons per group uniform) reliably detects stale/shifted data. Timeout guard prevents stale trix_scores from poisoning votes.

**Errata discovered:**
- GDMA RST_BIT in ISR kills the free-running loop (ESP32-C6 GDMA doesn't recover from mid-operation manipulation in ISR context)
- GDMA OUT_LINK restart doesn't actually reset the read pointer
- Waiting N loops after re-encode doesn't help because the GDMA offset is structural, not temporal

## Milestone 34: TriX Classification Channel

**Tests:** 11/11 | **Commit:** `b79f09b` | **Result:** Core 100%, ISR 90%, channel seq=trix_count

**What it proved:** The ISR packs 4 group dot values as signed bytes into `trix_channel.value` and signals via `reflex_signal()` with proper memory fences. The consumer uses `reflex_wait_timeout()` instead of raw volatile polling. Channel sequence number tracks exactly with trix_count (2108/2108 verified on silicon). This replaces the ad-hoc volatile-polling approach with the proven reflex channel primitive.

## Milestone 35: CfC Blend Disabled (Phase 3)

**Tests:** 11/11 | **Commit:** `c6fd284` | **Result:** Core 100%, ISR 93%, 0% gate firing

**What it proved:** The CfC blend can be fully disabled without affecting TriX classification. Setting `gate_threshold = INT32_MAX` means no neuron's f_dot ever exceeds threshold, so every neuron takes the HOLD path (`h_new[n] = h_old[n]`). Hidden state freezes after input install. Classification is TriX-only.

**The change:** One line — `gate_threshold = 0x7FFFFFFF` (was `90`). The blend code (step 4 in the ISR) still executes but produces no state changes: `fires = 0` every loop. Step 5 (hidden re-encode) still runs but is now wasted work since hidden never changes. Phase 4 will remove it.

**What this confirms:**
- The CfC blend contributed nothing to classification accuracy. TriX signatures alone are sufficient.
- The system is safe to strip further: removing step 5 will save ~20us per loop.
- The path to shrinking the DMA chain from 64 to 32 neurons (TriX only needs 32) is clear.

**Key result:** `Gate firing: 0%` in serial output. `Gate selectivity: 0 fires / 64 steps = 0%` at install time. This is the point where the GIE transitions from "CfC neural network with TriX classification added" to "TriX classification engine with CfC machinery still present."

---

## Peripheral Allocation (Current — TriX Classification)

| Peripheral | Usage |
|------------|-------|
| PARLIO TX | 2-bit mode, 10MHz, GPIO 4-5, io_loop_back |
| GDMA CH0 (OUT) | Owned by PARLIO driver, bare-metal reconfigured |
| PCNT Unit 0 | Agree: X_pos gated by Y_pos + X_neg gated by Y_neg |
| PCNT Unit 1 | Disagree: X_pos gated by Y_neg + X_neg gated by Y_pos |
| GPTimer 0 | Kickoff trigger (ETM-enabled) |
| ETM | Clock enable only |
| LP Core | 16MHz RISC-V, CfC + VDB + Feedback, 10ms timer wake |
| LP SRAM | 16KB: code + LUT + CfC state + VDB nodes + stack |
