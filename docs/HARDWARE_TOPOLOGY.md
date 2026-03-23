# Hardware Topology: ESP32-C6 + STM32 Nucleo M4 Expansion

**The Reflex Project — Hardware Reference**

*Written March 22, 2026. Pre-wiring session document.*

---

## System Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          RF SIGNAL DOMAIN                               │
│                                                                         │
│              ┌──────────────┐                                           │
│              │   BOARD B    │  2.4GHz 802.11 beacon                     │
│              │  ESP32-C6    │  cycles P0→P1→P2→P3 (27s period)          │
│              │  (SENDER)    │  2s P3 window, ~4 Hz confirmed rate        │
│              └──────┬───────┘                                           │
│                     │  RF (over-the-air)                                │
│          ┌──────────┴──────────┐                                        │
│          ▼                     ▼                                        │
│   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐             │
│   │   BOARD A    │    │   BOARD C    │    │   BOARD D    │             │
│   │  ESP32-C6    │    │  ESP32-C6    │    │  ESP32-C6    │             │
│   │  (RX #1)     │    │  (RX #2)    │    │  (RX #3)     │             │
│   │  PRIMARY     │    │  SECONDARY  │    │  RESERVED    │             │
│   └──────┬───────┘    └──────┬───────┘    └──────────────┘             │
│          │                   │                                          │
└──────────┼───────────────────┼──────────────────────────────────────────┘
           │                   │
           │  COMPUTE DOMAIN   │
           │                   │
           ▼                   ▼
┌──────────────────┐  ┌──────────────────┐
│  Nucleo L4R5ZI-P │  │  Nucleo L4A6ZG   │
│  STM32L4R5ZI     │  │  STM32L4A6ZG     │
│  M4 @ 120MHz     │  │  M4 @ 80MHz      │
│  FPU + FMAC      │  │  FPU             │
│  2MB Flash       │  │  1MB Flash       │
│  640KB SRAM      │  │  320KB SRAM      │
│                  │  │                  │
│  PRIMARY APU     │  │  SECONDARY APU   │
│  - VDB offload   │  │  - MTFP21 infer  │
│  - MTFP21 APU    │  │  - Backup VDB    │
└────────┬─────────┘  └────────┬─────────┘
         │                     │
         └──────────┬──────────┘
                    │  SPI/UART (inter-Nucleo)
                    │  direct connection
```

---

## Board Inventory

| Board | Part | Role | Connected To |
|-------|------|------|--------------|
| Board B | ESP32-C6 DevKit | RF sender, pattern generator | (transmit only) |
| Board A | ESP32-C6 DevKit | Primary receiver, Reflex runtime | Nucleo L4R5ZI-P |
| Board C | ESP32-C6 DevKit | Secondary receiver, ablation pair | Nucleo L4A6ZG |
| Board D | ESP32-C6 DevKit | Third receiver, reserved | (unconnected initially) |
| Nucleo-1 | L4R5ZI-P (Nucleo-144) | Primary APU: VDB + MTFP21 | Board A |
| Nucleo-2 | L4A6ZG (Nucleo-144) | Secondary APU: MTFP21 | Board C |

---

## Interface Specifications

### Interface 1: SPI (VDB Query Path)

**Purpose:** Board A / Board C dispatch VDB queries and MTFP inference requests to the Nucleo APU. This replaces or supplements the LP core (~10-15ms) with a hardware-accelerated path (~6µs).

**Configuration:**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Mode | SPI Mode 0 (CPOL=0, CPHA=0) | Standard; verify both ends |
| Clock | 40 MHz | C6 FSPI max. Use 20MHz for first bring-up |
| DMA | Both ends | C6 GDMA; Nucleo DMA1/DMA2 on SPI channel |
| Frame | 8-bit | Byte-oriented transactions |
| CS | Active-low, software-controlled | One CS line per Nucleo |
| Master | ESP32-C6 | C6 initiates all transactions |
| Slave | STM32 Nucleo | Nucleo responds, signals ready via IRQ |

**Latency budget:**
- C6 asserts CS, clock starts: ~100ns
- Transfer 32 bytes (VDB query vector): 32 × 8 bits / 40MHz = 6.4µs
- Nucleo processes (VDB lookup or MTFP kernel): TBD by firmware
- Transfer 16 bytes (result vector): 3.2µs
- **Round-trip (wire only):** ~10µs
- **Round-trip (with Nucleo compute):** TBD — target < 500µs

**GPIO interrupt line (Nucleo → C6):**

Nucleo asserts a GPIO line to signal result-ready before C6 needs to poll. C6 configures this line as an edge-triggered interrupt (rising edge = result ready). This avoids busy-wait polling and is the same pattern used by the existing GIE peripheral chain (GDMA → PARLIO → PCNT → interrupt).

```
Nucleo GPIO_OUT ──────────────────► C6 GPIO_IN (interrupt)
(result ready)                      (triggers classification callback)
```

---

### Interface 2: QSPI (MTFP Inference Path)

**Purpose:** High-bandwidth transfer of MTFP21 weight tensors and activation vectors between C6 and Nucleo. The Nucleo FPU register file (32 S-registers, 32-trit working set in {-1.0, 0.0, +1.0}) acts as the MTFP21 APU. QSPI provides the input/output data bus.

**Configuration:**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Mode | Quad SPI (4 data lines) | IO0–IO3 bidirectional |
| Clock | 40 MHz | 4 × 40MHz = 160 Mbps effective |
| DMA | Both ends | Nucleo via OctoSPI/QSPI DMA |
| Direction | C6 → Nucleo (weights/inputs); Nucleo → C6 (outputs) | Half-duplex quad |
| Frame size | 32 or 64 bytes per burst | Aligned to MTFP21 tile size |

**Throughput:**
- 160 Mbps = 20 MB/s
- A 16-element MTFP21 vector (16 × 1 byte ternary) transfers in < 1µs
- A 256-element weight matrix (trit-packed) transfers in ~13µs

**Note:** The L4R5ZI has an OctoSPI peripheral (up to 8 data lines) that is backward-compatible with QSPI. The L4A6ZG has a standard QUADSPI peripheral. Both support DMA. Wire 4 data lines (IO0–IO3) for quad mode. The C6 FSPI peripheral supports quad mode natively.

---

## Pin Assignments

### Board A (C6 #1) → Nucleo L4R5ZI-P: SPI + QSPI

The ESP32-C6 GPIO matrix allows any SPI-capable pin to serve any SPI function. Suggested assignment to avoid conflict with the existing GIE peripheral chain (GDMA/PARLIO/PCNT uses GPIO 0–7, BOOT=GPIO9):

**SPI (VDB path):**

| Signal | C6 GPIO | Nucleo Pin | STM32 Port | Notes |
|--------|---------|------------|------------|-------|
| SCK | GPIO 18 | CN7 pin 7 | PA5 / SPI1_SCK | Arduino D13 equiv |
| MOSI | GPIO 19 | CN7 pin 14 | PA7 / SPI1_MOSI | Arduino D11 equiv |
| MISO | GPIO 20 | CN7 pin 12 | PA6 / SPI1_MISO | Arduino D12 equiv |
| CS0 | GPIO 21 | CN7 pin 16 | PD14 (GPIO_OUT) | SW-controlled CS |
| IRQ | GPIO 22 | CN7 pin 8 | PB8 (GPIO_OUT) | Nucleo→C6, result-ready |

**QSPI (MTFP path):**

| Signal | C6 GPIO | Nucleo Pin | STM32 Port | Notes |
|--------|---------|------------|------------|-------|
| CLK | GPIO 23 | CN10 pin 6 | PB1 / QSPI_CLK | Verify L4R5 alt-func |
| NCS | GPIO 24 | CN10 pin 8 | PB6 / QSPI_NCS | or PB11 per board rev |
| IO0 | GPIO 25 | CN10 pin 10 | PC9 / QSPI_BK1_IO0 | |
| IO1 | GPIO 26 | CN10 pin 12 | PC8 / QSPI_BK1_IO1 | |
| IO2 | GPIO 27 | CN10 pin 14 | PA7 / QSPI_BK1_IO2 | CHECK: conflicts SPI1_MOSI |
| IO3 | GPIO 28 | CN10 pin 16 | PA6 / QSPI_BK1_IO3 | CHECK: conflicts SPI1_MISO |

**IMPORTANT:** IO2 and IO3 on the L4R5ZI share port pins with SPI1_MOSI/MISO (PA7/PA6). SPI and QSPI cannot be active simultaneously on those lines. Resolution options:
1. Use SPI2 instead of SPI1 for the VDB path (PB13/PB14/PB15 — no conflict with QSPI)
2. Time-multiplex: SPI and QSPI are never active at the same time (likely true — VDB query and MTFP inference are sequential steps)
3. Use a different QSPI bank (BK2) if IO2/IO3 cannot be remapped

**Recommended resolution:** Move SPI to SPI2. Pinout below.

**SPI2 (preferred — no QSPI conflict):**

| Signal | C6 GPIO | Nucleo Pin | STM32 Port | Notes |
|--------|---------|------------|------------|-------|
| SCK | GPIO 18 | CN10 pin 22 | PB13 / SPI2_SCK | |
| MOSI | GPIO 19 | CN10 pin 26 | PB15 / SPI2_MOSI | |
| MISO | GPIO 20 | CN10 pin 28 | PB14 / SPI2_MISO | |
| CS0 | GPIO 21 | CN10 pin 24 | PD8 (GPIO_OUT) | SW CS |
| IRQ | GPIO 22 | CN7 pin 8 | PB8 (GPIO_OUT) | Nucleo→C6 |

**Verify all pin numbers against your specific Nucleo-144 board revision before soldering. The CN7/CN10 Morpho connector row assignments can differ between board revisions.**

---

### Board C (C6 #2) → Nucleo L4A6ZG: SPI

Same SPI wiring as Board A → Nucleo-1, using the same GPIO assignments on the C6 side. The L4A6ZG also has SPI2 on PB13/PB14/PB15.

| Signal | C6 GPIO | STM32 Port |
|--------|---------|------------|
| SCK | GPIO 18 | PB13 / SPI2_SCK |
| MOSI | GPIO 19 | PB15 / SPI2_MOSI |
| MISO | GPIO 20 | PB14 / SPI2_MISO |
| CS0 | GPIO 21 | PD8 (GPIO) |
| IRQ | GPIO 22 | PB8 (GPIO) |

The L4A6ZG has a standard QUADSPI peripheral (not OctoSPI). Add QSPI wiring once the primary interface is validated.

---

### Inter-Nucleo Link (Nucleo-1 ↔ Nucleo-2)

The two Nucleo boards can coordinate directly — e.g., Nucleo-1 offloads part of an MTFP21 inference to Nucleo-2 while continuing VDB work. Use UART for simplicity initially; migrate to SPI if throughput demands it.

**UART (initial):**

| Signal | Nucleo-1 Pin | Nucleo-2 Pin | Notes |
|--------|-------------|-------------|-------|
| TX1→RX2 | PD5 / UART2_TX | PD6 / UART2_RX | Cross-wire |
| RX1←TX2 | PD6 / UART2_RX | PD5 / UART2_TX | |
| GND | GND | GND | Common ground |

**UART rate:** 921600 baud to start; 4Mbaud if UART on these M4s is run from 80MHz APB clock with clock dividers.

---

## Voltage and Power

Both ESP32-C6 and STM32 Nucleo boards operate at **3.3V logic**. No level shifting required for signal lines.

| Board | Logic Level | Power Source |
|-------|------------|-------------|
| ESP32-C6 DevKit | 3.3V | USB-C (5V → onboard LDO) |
| Nucleo L4R5ZI-P | 3.3V | USB-C or external 3.3V |
| Nucleo L4A6ZG | 3.3V | USB-C or external 3.3V |

**Power rule:** Each board sources its own power from its own USB connection. Do **not** power a Nucleo from the C6's 3.3V rail — the LDO on the C6 DevKit is not rated for it. Connect only signal lines across boards.

**Common ground:** All boards must share a common ground reference. Add one ground wire from each C6 DevKit GND pin to the Nucleo GND pin on the Morpho connector. Without this, signal levels float and SPI will be unreliable.

---

## Signal Integrity

At 40MHz on short bench wires, signal integrity is the primary concern.

| Constraint | Value | Reason |
|------------|-------|--------|
| Max trace/wire length | 10 cm | Quarter-wave at 40MHz ≈ 1.875m; 10cm is safely sub-wavelength |
| Wire type | Solid-core or short jumper | Stranded jumpers add capacitance; use Dupont 10cm max |
| Ground returns | One GND wire per 3 signal wires | Reduces ground bounce |
| Decoupling | 100nF ceramic at each power pin | Already on Nucleo boards; verify C6 DevKit |
| Slew rate | STM32 GPIO high-speed mode | Set GPIO_Speed = GPIO_SPEED_FREQ_VERY_HIGH in STM32 HAL |
| Pull-up on IRQ line | 10kΩ to 3.3V | Prevents spurious interrupts when Nucleo is in reset |

**First bring-up:** Start at 10MHz, verify SPI transactions with logic analyzer or scope. Step to 20MHz, then 40MHz only after clean operation at 20MHz.

---

## Data Flow

### VDB Query Path (Phase 5 — TEST 14)

```
Board A ESP32-C6                    Nucleo L4R5ZI-P
────────────────                    ───────────────
1. Packet confirmed by TriX
2. compute gate_bias (HP core)
3. feed_lp_core() → LP SRAM
4. [NEW] SPI transaction:
   CS assert ─────────────────────►
   send gie_hidden[32] (32 bytes) ─►
   send lp_hidden[16] (16 bytes) ──►
   send p_hat (1 byte) ────────────►
                                    5. Receive query
                                    6. NSW graph search (VDB)
                                    7. Pack result: neighbor_lp[16]
                                    8. Assert IRQ ──────────────────►
   C6 IRQ handler fires ◄──────────
9. CS assert (read phase)
   receive neighbor_lp[16] ◄───────
   CS deassert ◄───────────────────
10. vdb_cfc_feedback_step() using
    received neighbor_lp as VDB
    retrieval result
```

### MTFP21 Inference Path (Phase 5+ — Future)

```
Board A ESP32-C6                    Nucleo L4R5ZI-P
────────────────                    ───────────────
1. Activation tensor ready
   (output of LP CfC step)
2. QSPI burst:
   send activation[32] (ternary) ──►
   send weight_tile (packed trits) ─►
                                    3. FPU registers loaded
                                    4. FMAC: exact trit dot products
                                    5. MTFP21 kernel executes
                                    6. Pack output (MTFP21 precision)
                                    7. Assert IRQ ──────────────────►
   C6 IRQ handler fires ◄──────────
8. QSPI burst (receive):
   receive output_tensor ◄──────────
9. Continue inference pipeline
```

### Gate Bias Write Path (Phase 5 — TEST 14, no Nucleo involvement)

Gate bias is computed entirely on the HP core and written to `gate_bias_shadow[4]` in HP SRAM. The ISR reads from HP SRAM directly. The Nucleo is **not** in this path for TEST 14 — it is a future optimization.

```
HP Core (ESP32-C6)           ISR (ESP32-C6)
──────────────────           ──────────────
classification callback:     CfC blend step:
  compute agreement            read gate_bias_shadow[4]
  gate_bias_staging[p_hat]     n/TRIX_NEURONS_PP → group
  memcpy → gate_bias_shadow    eff_thresh = thresh + gbias[group]
                               apply to f_dot comparison
```

---

## Bring-Up Sequence

### Session 1 (tomorrow)

1. **Physical wiring**: Connect one C6 to one Nucleo using SPI2 (PB13/14/15) + IRQ. Start with Board A ↔ Nucleo-1.
2. **SPI loopback test**: Nucleo echoes back whatever it receives. C6 sends 0xAA, 0x55 pattern and verifies reception. Confirms electrical connection and SPI configuration before any real logic.
3. **Clock speed validation**: Loopback at 1MHz, 10MHz, 20MHz, 40MHz. Capture with logic analyzer if available. Accept 20MHz if 40MHz shows errors.
4. **IRQ test**: After echo, Nucleo toggles IRQ line. C6 interrupt fires. Confirms interrupt routing.
5. **First real transaction**: C6 sends a fixed 48-byte query (gie_hidden + lp_hidden + p_hat). Nucleo returns 16-byte all-zeros result. C6 integration layer uses this as a "null VDB response" and verifies the firmware doesn't crash.

### Session 2 (after TEST 14 firmware)

6. **VDB query offload**: Implement VDB graph search on Nucleo. C6 measures round-trip latency against the LP core baseline.
7. **Second Nucleo**: Connect Board C ↔ Nucleo-2. Validate independent operation.
8. **Inter-Nucleo link**: Connect UART between the two Nucleos. Test at 921600 baud.

### Session 3 (MTFP21 path)

9. **QSPI bring-up**: Wire IO0–IO3. Validate quad mode at 10MHz before 40MHz.
10. **First MTFP21 kernel**: Run a single 16-element trit dot product on the Nucleo FPU. Compare result against reference computed on C6.

---

## Open Questions for Tomorrow

- **Board D (C6 #3) role**: Options: (a) second sender for multi-transmitter experiments, (b) third receiver for 3-way ensemble, (c) dedicated coordination/aggregation node. Suggest: leave unconnected in Session 1, decide after TEST 14 results.
- **Nucleo L4A6ZG vs L4R5ZI for MTFP21**: L4R5ZI has FMAC (hardware multiply-accumulate unit) which is better suited for exact trit dot products. L4A6ZG has standard FPU only. L4R5ZI should be the primary MTFP21 APU. L4A6ZG is the VDB offload backup.
- **SPI transaction framing**: Define the query/response packet format before writing Nucleo firmware. Suggested: 2-byte length header + payload + 1-byte CRC. Or: fixed-length transactions (48 bytes query, 16 bytes response) with no framing overhead.
- **LP SRAM alignment**: Verify `ulp_gate_bias` offset in `ulp/main.S` before first TEST 14 flash.

---

## Connector Quick Reference

### Nucleo-144 Morpho CN7 (left connector, odd rows on pin 1 side)

Pins most relevant to this topology. Count from the notch end.

| Row | Pin A (odd) | Pin B (even) |
|-----|-------------|--------------|
| 1 | PC10 | PC11 |
| 3 | PC12 | PD2 |
| 5 | PG2 | PG3 |
| 7 | PA5 (SPI1_SCK) | PA6 (SPI1_MISO) |
| 9 | PA7 (SPI1_MOSI) | PD14 |
| 11 | PB8 (IRQ out) | PB9 |
| 13 | AVDD | GND |

### Nucleo-144 Morpho CN10 (right connector)

| Row | Pin A | Pin B |
|-----|-------|-------|
| 1 | PC8 | PC9 |
| 3 | PB13 (SPI2_SCK) | PB14 (SPI2_MISO) |
| 5 | PB15 (SPI2_MOSI) | PD8 |
| 7 | PD9 | PD10 |
| 13 | GND | — |

**Cross-reference against the Nucleo-144 UM2179 user manual (Table 16–17) before wiring.** Row assignments above are approximate and may differ between board revisions (MB1312, MB1313, MB1136).

---

*Wiring session: March 23, 2026.*
*Depends on: `docs/KINETIC_ATTENTION.md`, `journal/kinetic_attention_synth.md`*
