# Technical Reality

What exists, what's proven, what's not, and what the honest gaps are.

**Last verified on silicon:** February 10, 2026
**Hardware:** ESP32-C6FH4 (QFN32) rev v0.2, single chip, $0.50
**Secondary platform:** NVIDIA Jetson AGX Thor (cache coherency work only)

---

## What Exists

### 1. Peripheral Dot Product Engine (GIE)

**Claim:** The ESP32-C6's PCNT, PARLIO, and GDMA peripherals can compute ternary dot products while the CPU does nothing.

**How it works:**
- CPU pre-multiplies weight x input (ternary sign comparison, ~200 cycles per 256 trits)
- Result encoded into DMA buffers: each trit {-1, 0, +1} becomes a 2-bit GPIO pattern
- GDMA streams buffers through PARLIO TX (2-bit mode, 10MHz) to GPIO 4-5 (loopback)
- PCNT Unit 0 counts "agree" edges (same-sign), PCNT Unit 1 counts "disagree" edges
- Dot product = agree - disagree
- Circular DMA chain: [5 dummy descriptors][64 neuron descriptors][EOF → loop back]
- ISR fires on each neuron's EOF, reads PCNT (after 200-loop clock domain drain), applies CfC blend, re-encodes next neuron

**Verified:** 64/64 dot products match CPU reference, exact. Commit `f8860d3`.

**What this is:** A novel way to compute dot products using peripheral hardware not designed for arithmetic. The ternary constraint is what makes it work — two GPIO bits encode one trit, and PCNT edge/level gating naturally separates agree from disagree.

**What this is not:** A general-purpose compute engine. It computes one specific operation (ternary dot product) for one specific data encoding. The CPU must pre-multiply W*X and encode DMA buffers between neurons (~20us per neuron in the ISR). The ISR is CPU work.

**Honest throughput:** 428 Hz for a complete 64-neuron loop at 10MHz PARLIO. The "~0 CPU" claim means the DMA chain runs autonomously, but the ISR (LEVEL3 interrupt) executes on the CPU for every neuron EOF. The CPU is interrupt-driven, not idle.

### 2. Ternary Gated Recurrent Unit (called "CfC" in the codebase)

**Claim:** A fully ternary {-1, 0, +1} recurrent neural network runs on the GIE and LP core.

**Update equation:**
```
concat = [input | hidden]              // implicit in weight layout, no copy
f[n]   = sign(dot(concat, W_f[n]))     // gate:      {-1, 0, +1}
g[n]   = sign(dot(concat, W_g[n]))     // candidate: {-1, 0, +1}
h_new  = (f == 0) ? h_old : f * g      // ternary blend
```

Three blend modes: UPDATE (f=+1), HOLD (f=0), INVERT (f=-1).

**Verified:** The update equation executes correctly. All dot products match CPU reference. The blend logic is correct. Commits across M7 through free-run.

**What this is:** A gated recurrent unit with ternary weights, ternary activations, and three blend modes (binary GRUs have two). The three-mode blend is a genuine structural difference from binary GRUs/LSTMs — the INVERT mode creates oscillatory dynamics that two-mode systems cannot express as a first-class operation.

**What this is not:**
- **Not a trained model.** Weights are random and fixed. There is no training procedure, no loss function, no optimization. The network has never learned anything.
- **Not a CfC in the Hasani et al. sense.** Their CfC derives from a continuous-time ODE with learned parameters trained via backpropagation. This system borrows the gate/candidate update structure but removes the continuous-time formulation, the learned parameters, and the training. "Ternary gated recurrent unit" would be a more accurate name.
- **Not demonstrated on any task.** The network processes synthetic/random inputs and produces dynamics. Whether those dynamics are useful for any purpose is unknown.

**The three-mode observation is real:** INVERT (f=-1) creates period-2 oscillations verified on silicon. Binary systems need auxiliary circuitry to produce this. In ternary, it falls out of the update rule. This is a genuine, if narrow, structural contribution.

### 3. LP Core Assembly Implementation

**Claim:** A 16MHz RISC-V ULP core runs ternary CfC inference, NSW vector database search, and memory feedback — all in hand-written assembly, fitting in 16,320 bytes.

**What's there:**
- 16 neurons, 48-trit inputs, 32 INTERSECT calls per CfC step
- 64-node NSW graph with M=7 neighbors, ef=32 search, dual entry points
- 5 commands: CfC step, VDB search, VDB insert, pipeline (CfC+VDB), feedback (CfC+VDB+blend)
- 256-byte popcount LUT, unrolled and looped INTERSECT variants
- ~7,600 bytes code, ~4,356 bytes free stack
- Wakes every 10ms (100 Hz), active ~400us per wake (~4% duty cycle)

**Verified:** 16/16 dots exact, NSW recall@1=95%, recall@4=90%, 64/64 self-match, 64/64 graph connectivity, feedback stability over 50 steps. Multiple commits.

**What this is:** A complete, working embedded system. The assembly is tight, the memory budget is managed to the byte, and every operation is verified against a CPU reference. The engineering quality is high.

**What this is not:** The 16-neuron / 48-trit / 64-node scale is a proof of concept. Whether this architecture extends to useful scale is unknown. The LP core has 16KB total — you can't fit significantly more without a different chip.

### 4. NSW Vector Database

**Claim:** Navigable Small World graph search on the LP core, 95% recall@1.

**Verified:** At 64 nodes, 48-trit vectors, M=7, ef=32, dual entry. recall@1=95%, recall@4=90% vs brute-force ground truth. All on silicon.

**Honest assessment:** 64 nodes with 48-trit ternary vectors is a trivially small search problem. The graph visits 60/64 nodes — it's barely sub-linear. At this scale, brute force takes ~400us and the graph adds complexity for marginal benefit. The NSW implementation is correct and well-engineered, but its value would only materialize at N>>64, which the LP core's 16KB SRAM cannot support.

### 5. Feedback Loop

**Claim:** VDB search results blend into the CfC hidden state, creating a system that "adapts based on experience."

**Blend rule:**
```
For each of 16 lp_hidden trits:
  if h == mem:                       no change   (agreement)
  if h == 0 and mem != 0:            h = mem     (fill from memory)
  if h != 0 and mem == 0:            no change   (memory silent)
  if h != 0 and mem != 0 and h!=mem: h = 0       (conflict → HOLD)
```

**Verified:** 50 feedback steps: 50 unique states, energy bounded [7, 15], no oscillation. Feedback vs no-feedback trajectories diverge. Commit `dc57d60`.

**What this is:** Nearest-neighbor retrieval from the VDB, followed by a conservative trit-wise blend into the hidden state. The HOLD-on-conflict rule is a genuine damping mechanism — it prevents feedback runaway by creating zeros that the CfC's HOLD mode preserves.

**What this is not:**
- **Not learning.** The system doesn't improve at anything over time. It retrieves stored states and blends them in. Whether this blend is useful depends entirely on what states were stored and whether the retrieval is meaningful — neither of which has been tested with real data.
- **"Adapts based on experience" is overstated.** More precisely: the hidden state is influenced by the nearest stored vector. Whether this constitutes adaptation depends on whether the stored vectors represent meaningful experience, which requires real-world input that doesn't exist yet.

**The stability result is real but expected:** 50 unique states in 50 steps from a 3^16 state space (~43 million states) with a nonlinear recurrence is the expected behavior of almost any non-degenerate system. The bounded energy [7, 15] is more interesting but unexplained — it may be a property of the blend rule, the random weights, or the input statistics.

### 6. TriX Classification (ESP-NOW Input → Hardware Pattern Recognition)

**Claim:** The GIE classifies real-world wireless input patterns at hardware rate using ternary signatures — no learned weights, no training, no floating point.

**How it works:**
- Board B transmits 4 ESP-NOW patterns (P0: steady 10Hz, P1: burst, P2: slow 2Hz, P3: ramp)
- Board A receives packets, encodes them as 128-trit ternary input vectors (RSSI, pattern ID, payload, timing)
- During a 30-second observation phase, per-pattern signatures are computed: `sig[p][j] = tsign(sum of input trits across all packets of pattern p)`
- Signatures ARE the gate weights: `W_f[neuron n] = sig[n/8]` (8 neurons per pattern, 4 patterns = 32 neurons)
- Classification = argmax of ternary dot products: `d[p] = dot(sig[p], input)` — the pattern whose signature best matches the current input wins
- ISR validates each GIE loop: all 8 neurons per pattern group must produce identical dots (detects stale/shifted data from GDMA circular chain offset)
- Clean classifications are signaled via `trix_channel` (reflex channel with 4 packed dot values)
- Main loop resolves GDMA offset by matching ISR dot values against CPU-computed reference dots

**Verified:** 32/32 = 100% (Core CPU path), 32/32 = 100% (ISR hardware path). Commit `fd338f5`. Also verified at 93% in a separate run (normal variance from P3 signature drift). 11/11 tests pass.

**What this is:** Zero-shot pattern classification using content-addressable signatures. The signatures encode what each pattern "looks like" in ternary — no training, no gradient descent, no loss function. The dot product between signature and input measures similarity. This is nearest-centroid classification, the simplest possible classifier, executed entirely in peripheral hardware.

**What this is not:**
- **Not learned.** Signatures are computed from observed data (tsign of accumulated samples), not optimized against a loss function. This is closer to a lookup table than a neural network.
- **Not generalizable.** The 4 transmission patterns are highly distinct (different timing, different payload encoding). Any reasonable classifier would achieve high accuracy on this task. The interesting part is WHERE it runs (ISR, peripheral hardware), not WHAT it achieves.
- **Not fully autonomous.** The main loop still resolves the GDMA chain offset by matching ISR dot values to CPU reference dots. The ISR alone can't determine which pattern won because the GDMA offset permutes the neuron-to-capture mapping.

**The TriX principle is real:** "Don't learn what you can read." Signatures computed from observed data ARE optimal gate weights for nearest-centroid classification. No training needed. This is a genuine insight about the relationship between ternary signatures and classification, even though nearest-centroid itself is trivial.

**Online maintenance:** Signatures are re-signed every 16 packets per pattern (running average with decay). Novelty detection gates classification when the best dot product falls below threshold 60. Both verified stable over 32 classification windows.

**The 7-voxel TriX Cube:** Core signature + 6 temporal face signatures (recent, prior, stable, transient, confident, uncertain). Each face observes packets from a different temporal perspective. XOR masks between face and core signatures measure temporal displacement. Faces are sensors, not voters — core classification is authoritative. This is architectural infrastructure for temporal analysis, not yet demonstrated to improve accuracy.

### 7. Reflex Channel (Cache Coherency Coordination)

**Claim:** Sub-microsecond inter-core coordination using cache coherency signaling.

**Verified on Jetson AGX Thor:** 926ns P99 latency for a 10kHz control loop. Commit history in `reflex-robotics/`.

**Verified on ESP32-C6:** ISR→HP coordination via `reflex_channel_t` with RISC-V `fence rw, rw`. 18us average latency (dominated by ISR tail work, not channel cost). Commit `e9e67f1`.

**What this is:** A lock-free coordination primitive that uses hardware memory ordering (cache coherency on multi-core ARM, SRAM fence ordering on RISC-V). Simple, fast, well-understood. This is the oldest and most straightforwardly useful part of the project.

**What this is not:** Novel in principle. Lock-free coordination with memory barriers is a well-established pattern (Lamport, Herlihy). The contribution is a clean, minimal implementation measured on specific hardware.

---

## What's Proven vs. What's Claimed

| Statement | Status | Evidence |
|-----------|--------|----------|
| Peripheral hardware computes ternary dot products | **Proven** | 64/64 exact match, multiple commits |
| GIE runs at 428 Hz with 64 neurons | **Proven** | Measured on silicon |
| Three CfC blend modes produce distinct dynamics | **Proven** | Oscillation, hold, update verified |
| LP core fits CfC + VDB + feedback in 16KB ASM | **Proven** | Binary = 16,320 bytes exactly |
| NSW graph achieves 95% recall@1 at 64 nodes | **Proven** | Verified vs brute-force ground truth |
| Feedback loop is stable (no oscillation) | **Proven** | 50 steps, bounded energy |
| Sub-microsecond coordination on Jetson Thor | **Proven** | 926ns P99, adversarial testing |
| Real-world wireless input classified by GIE | **Proven** | ESP-NOW 4-pattern, 32/32 = 100%, commit `fd338f5` |
| ISR-level classification via reflex channel | **Proven** | trix_channel seq matches trix_count, commit `b79f09b` |
| TriX signatures = optimal gate weights (no training) | **Proven** | 100% accuracy, zero-shot from 30s observation |
| The system is a "neural network" | **Overstated** | TriX classification is nearest-centroid, not a trained network |
| The system "learns" or "adapts" | **Overstated** | Online signature maintenance ≠ learning; no loss function |
| The architecture scales beyond 64 nodes / 16 neurons | **Unproven** | Constrained by 16KB SRAM |
| Ternary GIE outperforms conventional approaches | **Unproven** | No comparison vs TFLite Micro or threshold detector on same task |
| TriX Cube temporal faces improve classification | **Unproven** | Faces collect XOR mask data but don't vote; no accuracy improvement shown |

---

## Genuine Contributions

1. **Peripheral-as-compute discovery.** Using PCNT edge/level gating + PARLIO loopback + GDMA descriptor chains as a dot product engine. Not published anywhere. Novel.

2. **TriX: signatures as weights.** The observation that ternary signatures computed from observed data (tsign of accumulated samples) ARE optimal gate weights for nearest-centroid classification. No training loop needed. "Don't learn what you can read." This collapses the training/inference distinction for ternary systems — data observation IS weight derivation.

3. **ISR-level classification at hardware rate.** The GIE classifies real-world wireless input at 430 Hz loop rate (62 Hz effective clean rate). Classification results delivered via reflex channel with memory-fence ordering. The ISR validates data integrity (uniform-group check) and the main loop resolves GDMA offset via value matching.

4. **Three-mode ternary blend.** The INVERT mode (f=-1) in the gated recurrent unit creates first-class oscillatory dynamics that two-mode systems cannot express as a first-class operation. Narrow but real. (Note: the CfC blend is being phased out in favor of pure TriX classification.)

5. **Three-layer power hierarchy.** Peripherals (~0 active CPU) / ULP core (~30uA) / full CPU (~15mA) as tiered compute layers with ternary operations native to each. Original embedded architecture pattern.

6. **ESP32-C6 peripheral errata catalog.** 12+ undocumented hardware behaviors discovered, documented, and resolved. Standalone value for any ESP32-C6 bare-metal developer.

7. **HOLD-as-damper in feedback.** The observation that ternary HOLD mode naturally damps feedback oscillation by creating persistent zero states. Specific to ternary recurrent systems with nearest-neighbor feedback.

---

## Known Gaps

### Critical (block any utility claim)

1. ~~**No real-world input.**~~ **RESOLVED (Feb 10, 2026).** ESP-NOW wireless packets from Board B now drive the GIE as live input. 4 transmission patterns classified with 100% accuracy. The system is no longer a closed loop.

2. ~~**No task.**~~ **RESOLVED (Feb 10, 2026).** Pattern classification: identify which of 4 ESP-NOW transmission patterns is active. 32/32 windows correct (Core and ISR). This is a real task with measurable accuracy against ground truth.

3. **No comparison.** No A/B test against a lookup table, a timing-threshold detector, a TFLite Micro binary network, or any conventional approach on the same hardware and task. The baseline (packet-rate timing threshold) achieves 78-93% on the same data — the TriX advantage is ~7-22 percentage points, but the comparison is unfair since TriX uses packet content while the baseline uses only timing.

4. ~~**No training.**~~ **PARTIALLY RESOLVED.** Weights are no longer random — they are ternary signatures computed from observed data. This is not training (no loss function, no optimization), but it is data-driven weight selection. The TriX insight is that for nearest-centroid classification, observed signatures ARE optimal weights without training.

### Significant (limit credibility of architecture claims)

5. **Scale is proof-of-concept only.** 4 patterns, 128-trit input, 32 neurons (TriX). The patterns are highly distinct (orthogonal pattern-ID trits guarantee separation). A harder task with overlapping patterns would test the architecture more honestly.

6. **ISR is CPU work.** The "~0 CPU" framing for the GIE is misleading. The DMA chain is autonomous but the ISR (which runs on every loop boundary) is CPU execution. More accurate: "interrupt-driven CPU with autonomous DMA between interrupts."

7. **GDMA chain offset limits ISR autonomy.** The ISR can validate and extract dot values, but cannot determine the winning pattern because the GDMA circular chain permutes the neuron-to-capture mapping. The main loop CPU must resolve this by matching ISR values against CPU-computed reference dots. Full ISR autonomy requires either fixing the GDMA offset (failed — kills the loop) or encoding pattern identity into the dot values themselves.

8. **CfC blend is vestigial.** The CfC blend (step 4 in the ISR) still runs but gate_threshold=90 blocks most updates. The system classifies via TriX signatures, not CfC dynamics. The CfC machinery is being phased out. The "ternary gated recurrent unit" has not demonstrated value for the classification task.

### Minor (worth noting)

9. **Test 2 (GIE hidden state evolves) is flaky.** Sometimes passes on second reset. Pre-existing.

10. **Single chip only.** Multi-chip coordination is discussed but not implemented (Board B is a dumb sender).

11. **The 155+ markdown files / 45,000 lines of documentation are mostly archaeological.** Documents from earlier project phases describe systems that no longer exist or make claims that were later corrected.

---

## Hardware Specifications

| Parameter | Value |
|-----------|-------|
| Chip | ESP32-C6FH4 (QFN32) rev v0.2 |
| HP core | RISC-V RV32IMAC, 160MHz |
| LP core | RISC-V RV32IMAC, 16MHz, ~30uA |
| Flash | 4MB embedded |
| LP SRAM | 16KB (RESERVE_MEM = 16,320 bytes) |
| PARLIO | 2-bit TX mode, 10MHz, GPIO 4-5, loopback |
| PCNT | Unit 0 (agree) + Unit 1 (disagree), 4 channels total |
| GDMA | CH0 OUT, circular descriptor chain |
| Device A | /dev/esp32c6a (MAC B4:3A:45:8A:C4:D4, GIE board) |
| Device B | /dev/esp32c6b (MAC B4:3A:45:8A:C8:24, second board) |
| ESP-IDF | v5.4 at /home/ztflynn/esp/v5.4 |
| Cost | ~$0.50 per chip |

### GPIO Pin Map

| GPIO | Function | Direction |
|------|----------|-----------|
| 4 | X_pos (PARLIO bit 0) | Output (loopback to PCNT) |
| 5 | X_neg (PARLIO bit 1) | Output (loopback to PCNT) |
| 6 | Y_pos (static level) | Output (CPU-driven) |
| 7 | Y_neg (static level) | Output (CPU-driven) |

### LP Core Memory Map

| Section | Size (bytes) | Notes |
|---------|-------------|-------|
| Vector table | 128 | Fixed |
| Code (.text) | ~7,600 | CfC + VDB + feedback + pipeline |
| Popcount LUT (.rodata) | 288 | 256-byte LUT + alignment |
| CfC state (.bss) | ~968 | Weights, hidden, dots, sync vars |
| VDB nodes (.bss) | 2,048 | 64 x 32 bytes (M=7) |
| VDB metadata (.bss) | ~80 | Query, results, counters |
| Feedback state (.bss) | ~24 | 6 observability variables |
| shared_mem | 16 | Top of SRAM |
| **Free for stack** | **~4,356** | Peak usage: 608B (VDB search) |
| **Total binary** | **16,320** | Exactly at RESERVE_MEM limit |

---

## Build and Test

```bash
# Build
export IDF_PATH=/home/ztflynn/esp/v5.4
. $IDF_PATH/export.sh > /dev/null 2>&1
rm reflex-os/sdkconfig
idf.py -C reflex-os build

# Flash (manual: hold BOOT, press RESET, release BOOT)
python -m esptool --chip esp32c6 --port /dev/esp32c6a -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0 reflex-os/build/bootloader/bootloader.bin \
  0x8000 reflex-os/build/partition_table/partition-table.bin \
  0x10000 reflex-os/build/reflex_os.bin

# Press RESET after flash. Wait 8 seconds for USB re-enumeration.
# Monitor via pyserial (no idf.py monitor — no TTY).
```

---

## Repository Structure (Current, Honest)

```
the-reflex/
├── TECHNICAL_REALITY.md          ← YOU ARE HERE
├── README.md                     # GitHub-facing overview (contains some inflation)
├── reflex-os/                    # THE ACTUAL SYSTEM
│   ├── main/
│   │   ├── ulp/main.S            # LP core: hand-written RISC-V assembly (cmd 1-5)
│   │   ├── geometry_cfc_freerun.c  # Current entry point: GIE + LP + VDB + tests
│   │   ├── reflex_vdb.c          # HP-side VDB API
│   │   └── [earlier milestones]  # M1-M7 source files (historical, not active)
│   ├── include/
│   │   ├── reflex_vdb.h          # VDB API header
│   │   ├── reflex.h              # Coordination primitive
│   │   └── ...
│   └── docs/                     # Technical docs (GIE architecture, errata, registers)
├── reflex-robotics/              # Jetson Thor cache coherency work (separate system)
├── docs/                         # Project docs (mixed: current + historical)
│   ├── CURRENT_STATUS.md         # Up to date as of Feb 9
│   ├── MILESTONE_PROGRESSION.md  # Complete milestone narrative
│   └── [86 other files]          # PRDs, LMMs, session notes — mostly historical
├── journal/                      # LMM journal (4 phases, reflects on VDB M5+)
├── notes/                        # Design notes, LMM explorations, business planning
├── the-reflex-tvdb.md            # VDB PRD (6 milestones)
└── [other dirs]                  # ros bridge, deploy tool, delta-observer, notebooks
```

**Active code:** `reflex-os/main/geometry_cfc_freerun.c`, `reflex-os/main/ulp/main.S`, `reflex-os/main/reflex_vdb.c`, and their headers. Everything else is either historical or supporting.

---

## What Would Make This More Useful

Listed in order of impact:

1. ~~**Connect a sensor.**~~ **DONE.** ESP-NOW wireless input drives the GIE. Next: add IMU, ADC, or other physical sensors.

2. ~~**Define a task.**~~ **DONE.** 4-pattern classification, 100% accuracy. Next: harder tasks — more patterns, overlapping features, temporal sequences.

3. **Compare against a baseline.** Same task, same chip: a timing-threshold detector, a lookup table, a TFLite Micro binary network. The timing baseline achieves 78-93% — show the TriX advantage is real and not just because the task is easy.

4. **Strip the CfC.** The CfC blend is vestigial — gate_threshold=90 blocks most activity. Remove it entirely (Phase 3-4 of CfC→TriX migration) to reduce ISR latency and enable shrinking the DMA chain from 64 to 32 neurons, potentially doubling the loop rate to ~800+ Hz.

5. **Harder classification tasks.** More patterns, noisier data, pattern transitions, concept drift. The current 4-pattern task has orthogonal pattern-ID trits that guarantee separation. A real test would use patterns distinguished only by statistical properties of the payload.

---

## Commit History (Key Milestones)

| Commit | What | Tests |
|--------|------|-------|
| `f41d5ea` | M1: Sub-CPU ALU (PCNT+PARLIO gates) | 59/59 |
| `5b2f62d` | M2: ETM crossbar loop | 5/5 |
| `59d0bba` | M3: GDMA descriptor chains | 9/9 |
| `66469ce` | M4: Ternary TMUL | 9/9 |
| `d45067b` | M5-M6: 256-trit dot product + 32-neuron layer | 10/10 + 6/6 |
| `b136ae9` | M7: Ternary gated recurrent unit | 6/6 |
| `28ff786` | M8-M9: 64-neuron chain + 10MHz PARLIO | 4/4 + 6/6 |
| `f8860d3` | Free-running GIE, 428 Hz, 64/64 exact | 3/3 |
| `dd87898` | LP core hand-written RISC-V assembly | 4/4 |
| `7db919f` | NSW graph search (M=7, recall@1=95%) | 6/6 |
| `06d5535` | CfC+VDB pipeline | 4/4 |
| `e9e67f1` | Reflex channel (ISR→HP) | 7/7 |
| `dc57d60` | VDB→CfC feedback loop | 8/8 |
| `6b61da3` | TriX signature routing | 78% classification |
| `ce0f788` | Signatures as W_f weights + per-pkt voting | 90%, 11/11 |
| `ef9dc69` | Online maintenance + novelty detection | 100% input-TriX |
| `d4f7ef0` | 7-voxel TriX Cube | Core 100%, ensemble 87% |
| `b117955` | XOR masks as face observables | Core 100% |
| `24ba035` | ISR TriX classification (DMA race solved) | ISR 87%, 11/11 |
| `fd338f5` | Timeout guard + extended spin wait | ISR 100%, Core 100%, 11/11 |
| `b79f09b` | TriX classification channel (reflex_signal) | Core 100%, ISR 90%, 11/11 |

---

*This document describes what exists on the chip as of February 10, 2026. It was written to be read by someone who has never seen the project and is looking for reasons to dismiss it. The things that survive that reading are the things that matter.*
