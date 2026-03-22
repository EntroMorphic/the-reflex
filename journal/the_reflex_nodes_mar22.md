# Nodes of Interest: The Reflex — March 22, 2026

*Second LMM cycle. Nodes from the March 22 session findings.*

---

## Node 1: Two Reset Mechanisms, One Undocumented

The PARLIO TX peripheral has two independent reset mechanisms:
- `FIFO_RST` (PCR bit 30): clears the transmit FIFO buffer
- `parl_tx_rst_en` (PCR bit 19): resets the TX core state machine itself

Both exist. Only FIFO_RST appears in Espressif's publicly documented configuration flow. The core reset (`parl_tx_rst_en`) is only discoverable by reading raw PCR register descriptions or by observing the failure mode (stale state machine accumulating across sessions).

**Why it matters:** Peripheral hardware has internal state machines that are invisible to software unless you reset them explicitly. Any peripheral capable of free-running operation (PARLIO, GDMA, RMT, etc.) potentially has this issue. "Reset the peripheral" means different things depending on which reset you trigger. We only knew about one.

**Tension:** If the TX core state was silently corrupted across test sessions, how long did earlier milestones run with partially-corrupted peripheral state? The final results were correct (PCNT counts matched CPU reference), suggesting the per-vector computation was unaffected, but the loop structure was wrong. This is a class of hardware bug that unit tests can miss entirely.

---

## Node 2: First External Input — Category Change

Every milestone before March 22 used CPU-generated test patterns. The CPU wrote ternary vectors to SRAM, armed the GDMA, and the GIE processed them. Board B changes this: a second ESP32-C6 transmits patterns over 802.11 ESP-NOW, the receiver's GIE processes them as they arrive.

**Why it matters:** This is the difference between a proof-of-concept and a sensor. Before: the system demonstrates it can process input. After: the system processes real input from the environment. The architecture becomes a transduction chain — RF signal → ESP-NOW packet → GIE vector → PCNT accumulation → CfC hidden state.

**Tension with prior synthesis:** The prior synthesis said "the medium is the computation." That was true for the peripheral routing. But the input was synthetic. Now the medium extends outside the chip — the 2.4 GHz RF medium is part of the computation chain.

---

## Node 3: Classification is Temporal Geometry, Not Pattern Matching

The TriX classifier (TEST 9–11) doesn't just ask "does this packet look like pattern X?" It constructs a 7-voxel geometric object — the TriX Cube — where the core voxel represents the current packet and six face voxels represent temporal neighbors (previous/next arrival in each discriminating dimension). Classification scores the divergence of face voxels from the core.

**Why it matters:** The system is measuring HOW a signal arrives in time, not just WHAT the signal contains. Two patterns at the same rate but different temporal structure produce different face divergences. This is geometry in signal space, not pattern matching in feature space.

**Specific result:** Pattern P0 and P3 were ambiguous to rate-only classification (84% baseline). The TriX Cube's face divergence separated them (100% accuracy). The temporal structure of P0 differed from P3 at the face voxel level even when the rate was indistinguishable.

---

## Node 4: XOR Mask Decomposition — Information Hierarchy

The TriX discriminating power, decomposed by signal attribute:
- Payload content: **47%** of discriminating weight
- Timing (inter-arrival jitter): **37%**
- RSSI (signal strength): **9%**
- Pattern ID field: **5%**

**Why it matters:** The most discriminating information is the bit content of the payload, not the RF properties or the rate. The peripheral hardware is performing content-addressed pattern recognition — reading the actual bits of the wireless packet and comparing them against learned ternary signatures. This is qualitatively different from rate detection or RSSI-based classification.

**Implication:** If you encrypted the payload, classification accuracy would drop to ~47% of current performance (from timing alone). The system is "reading" the message.

---

## Node 5: Peripheral Hardware Outperforms Rate-Only Baseline

- GIE-based TriX classification: **100%** (24,357 classifications over test window)
- Rate-only baseline: **84%** (standard in ESP-NOW classification literature)
- ISR firing rate: **711 Hz**

**Why it matters:** The peripheral-only architecture doesn't just match CPU-based classification — it outperforms the simpler CPU-based approach. The improvement comes from temporal geometry (face divergence), which requires processing at peripheral speed (711 Hz ISR) to capture inter-arrival timing structure at microsecond resolution.

**Tension:** The 711 Hz ISR fires on the HP core (GDMA interrupt), not in peripheral hardware directly. The ISR does the ternary dot product and TriX Cube update. So "peripherals-only" is slightly misleading for TriX — it's "peripheral-triggered, ISR-computed." The GIE dot product is truly peripheral; the TriX geometry is ISR.

---

## Node 6: The Peripherals-as-FPGA Analogy

The accessible description of GIE that emerged: configure the peripheral topology (descriptor chain → PARLIO routing → GPIO matrix → PCNT gating) and then let data flow through it. The topology IS the weight matrix. This is not execution of instructions; it is configuration of signal routing.

**Why it matters:** The right mental model is not "CPU running inference code" or even "CPU running at peripheral speed." It is "FPGA-like reconfigurable signal routing that computes by virtue of its wiring." This analogy is structurally accurate: FPGAs configure combinatorial logic paths; the GIE configures peripheral signal paths. Neither executes a program.

**Distinction from prior synthesis:** The prior cycle noted "computation as infrastructure." The FPGA analogy sharpens this: the GIE is a soft-FPGA implemented in peripheral configuration registers rather than LUTs. The comparison makes clear both what it CAN do (high-speed fixed-function computation) and what it CANNOT do (runtime reconfiguration without peripheral teardown/setup).

---

## Node 7: The System is Still Modular Proofs

As of 11/11 PASS, the following are separately proven but not integrated:
1. **GIE + CfC**: Peripheral dot products feed ISR → CfC blend (Phases 1–3)
2. **GIE + VDB feedback**: LP core CfC with memory modulation (Phase 3/CMD5)
3. **TriX classifier**: ISR-based ternary geometry on ESP-NOW input (Phase 4/Tests 9–11)

The TriX output does not feed the LP CfC. The LP CfC's hidden state does not modulate the TriX attention weights. The VDB does not store TriX classification events.

**Why it matters:** 11/11 is proof that each subsystem works in isolation with the others present. It is not proof that the layers interact as a system. The reflex arc analogy requires the output to loop back and influence the sensory layer — that loop doesn't exist yet.

**This is Node 7 of the prior cycle (The Open Loop), re-instantiated at a higher level.**

---

## Node 8: Hardware MAC Address as Architectural Fragility

The PEER_MAC in espnow_sender.c is a compile-time constant that must be manually transcribed from Board A's boot log. Two bytes were wrong (`0xC4, 0xD4` vs `0xC8, 0x24`). The system failed silently with 1060 send failures before diagnosis.

**Why it matters:** Multi-chip architectures have a class of failures that are not software bugs — they are hardware identity mismatches. The MAC address is not a configuration parameter; it is a physical identity. When Board A is replaced, the constant must be updated and both chips re-flashed. There is no runtime negotiation.

**Implication for scaling:** A 2-chip system with 1 hardcoded address is manageable. An N-chip mesh with N² hardcoded addresses is fragile. The architecture needs a discovery or registration protocol before it can scale beyond 2 boards.

---

## Node 9: Diagnostic Scaffolding has a Cost

40 lines of diagnostic printfs, counter declarations, and `[SF] step N:` traces were removed after hardware verification. The removal was clean — no behavior change, build passes, tests pass. But those 40 lines existed because we needed them during development and were reluctant to remove them until the behavior was fully confirmed.

**Why it matters:** Diagnostic scaffolding accumulates debt. It's not wrong to have it during development, but leaving it in production firmware adds flash/RAM overhead, adds UART transmission time (which is not free on USB-JTAG), and obscures the actual system behavior in log noise. The discipline of cleaning up after each milestone verification is load-bearing.

**Pattern observed:** Every milestone that was "working but uncertain" had scaffolding. Milestones that were "clean verified" did not. The scaffold-to-clean transition is the milestone closure event, not the test PASS.
