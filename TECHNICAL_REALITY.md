# Technical Reality

What exists, what's proven, what's not, and what the honest gaps are.

**Last verified on silicon:** February 9, 2026
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

### 6. Reflex Channel (Cache Coherency Coordination)

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
| The system is a "neural network" | **Overstated** | Untrained random weights, no task |
| The system "learns" or "adapts" | **Overstated** | No learning signal, no demonstrated improvement |
| CfC dynamics are "useful" | **Unproven** | No task, no comparison, no real input |
| The architecture scales beyond 64 nodes / 16 neurons | **Unproven** | Constrained by 16KB SRAM |
| Ternary GIE outperforms conventional approaches | **Unproven** | No comparison exists |
| Feedback creates meaningful adaptation | **Unproven** | Tested only with internal state, no external input |

---

## Genuine Contributions

1. **Peripheral-as-compute discovery.** Using PCNT edge/level gating + PARLIO loopback + GDMA descriptor chains as a dot product engine. Not published anywhere. Novel.

2. **Three-mode ternary blend.** The INVERT mode (f=-1) in the gated recurrent unit creates first-class oscillatory dynamics that binary GRUs cannot express. Narrow but real.

3. **Three-layer power hierarchy.** Peripherals (~0 active CPU) / ULP core (~30uA) / full CPU (~15mA) as tiered compute layers with ternary operations native to each. Original embedded architecture pattern.

4. **ESP32-C6 peripheral errata catalog.** 12+ undocumented hardware behaviors discovered, documented, and resolved. Standalone value for any ESP32-C6 bare-metal developer.

5. **HOLD-as-damper in feedback.** The observation that ternary HOLD mode naturally damps feedback oscillation by creating persistent zero states. Specific to ternary recurrent systems with nearest-neighbor feedback.

---

## Known Gaps

### Critical (block any utility claim)

1. **No real-world input.** The GIE processes synthetic patterns. The CfC runs on its own internal state. The VDB stores states from that internal processing. The entire system is a closed loop with no connection to the physical world.

2. **No task.** There is no classification, prediction, control, or decision-making. Nothing the system does has been measured against an objective.

3. **No comparison.** No A/B test against a PID controller, a lookup table, a binary neural network (TFLite Micro), or any conventional approach on the same hardware.

4. **No training.** Weights are random. The system has never optimized anything. The VDB accumulates states but doesn't select for better performance.

### Significant (limit credibility of architecture claims)

5. **Scale is proof-of-concept only.** 16 neurons, 48 trits, 64 VDB nodes. Useful systems typically need 10-100x this scale.

6. **ISR is CPU work.** The "~0 CPU" framing for the GIE is misleading. The DMA chain is autonomous but the ISR (which runs on every neuron EOF) is CPU execution. More accurate: "interrupt-driven CPU with autonomous DMA between interrupts."

7. **Documentation overstates results.** 27 milestones is an inflated count of ~5 conceptual steps. Biological analogies (spinal cord, brainstem, cortex, stem cells) add narrative but not substance. "CfC" borrows credibility from Hasani et al. without inheriting the properties that made their work significant.

### Minor (worth noting)

8. **Test 2 (GIE hidden state evolves) is flaky.** Sometimes passes on second reset. Pre-existing.

9. **Single chip only.** Multi-chip coordination is discussed but not implemented.

10. **The 155 markdown files / 45,000 lines of documentation are mostly archaeological.** Documents from earlier project phases describe systems that no longer exist or make claims that were later corrected.

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
| Device | /dev/esp32c6 (udev symlink for 303a:1001) |
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
python -m esptool --chip esp32c6 --port /dev/esp32c6 -b 460800 \
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

## What Would Make This Useful

Listed in order of impact:

1. **Connect a sensor.** An IMU, ADC, or even a button sequence feeding the GIE. This would answer: does the ternary CfC produce different outputs for different real-world inputs?

2. **Define a task.** Orientation classification, anomaly detection, sequence recognition — anything with a measurable objective.

3. **Compare against a baseline.** Same task, same chip: a lookup table, a threshold detector, a TFLite Micro binary network. Show where the ternary GIE wins or loses.

4. **Train the weights.** Even offline, even crudely — evolutionary search, random search with selection, exhaustive search at this scale. Show that optimized weights outperform random weights on the defined task.

5. **Characterize the dynamics.** Attractor analysis, Lyapunov exponents, input-output mutual information. Turn "it produces interesting dynamics" into "the dynamics have these specific measurable properties."

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

---

*This document describes what exists on the chip as of February 9, 2026. It was written to be read by someone who has never seen the project and is looking for reasons to dismiss it. The things that survive that reading are the things that matter.*
