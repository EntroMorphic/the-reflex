# LMM: Substrate Discovery

> *The body discovers itself.*
>
> 2026-01-26 | M3 Streaming Complete

---

## Current State

**M0 MVE:** DONE — 5/5 tests pass
**M1 Fault Catcher:** DONE — EBREAK recovery works
**M2 Cartography:** DONE — 16,461 probes, 0 faults
**M3 Awareness:** DONE — Rerun streaming protocol implemented

---

## Scope

Substrate discovery answers: **What can The Reflex DO?**

| In Scope | Why |
|----------|-----|
| HP SRAM | Allocatable memory, runtime capability |
| LP SRAM | Low-power operations, sleep state |
| Peripherals | Hardware interfaces, I/O capability |

| Out of Scope | Why |
|--------------|-----|
| Flash | ROM, layout known at compile time |

Flash is static. RAM and registers are dynamic. We discover what moves.

---

## Available Regions

### HP SRAM (High-Performance)
```
Address:  0x40820000 - 0x40850000
Size:     192KB (heap region, avoiding firmware)
Mode:     Read/Write probing
Purpose:  Find allocatable memory, measure latency
```

### LP SRAM (Low-Power)
```
Address:  0x50000000 - 0x50004000
Size:     16KB
Mode:     Read/Write probing
Purpose:  LP core memory, sleep-persistent state
```

### Peripherals
```
Address:  0x60000000 - 0x60010000
Size:     64KB (conservative limit)
Mode:     Read-only probing
Purpose:  Discover GPIO, UART, SPI, I2C, timers
```

**Total probeable space: 272KB**

---

## M2 Results: Substrate Map

```
Total probes: 16,461
Faults: 0
Regions: 64
```

| Region | Size | Latency | Type |
|--------|------|---------|------|
| HP SRAM | 192KB (48 × 4KB) | 74 cycles | RAM |
| LP SRAM | 16KB (4 × 4KB) | 75-76 cycles | RAM |
| Peripherals | 64KB | 82-98 cycles | REGISTER |

**Findings:**
- LP SRAM accessible from HP core with 1-2 cycle penalty
- Peripheral registers 10-20 cycles slower than SRAM
- Zero faults across entire substrate
- Map persists in NVS across reboots

---

## M2: Substrate Cartography

### Phase 1: HP SRAM Mapping
**Goal:** Build complete map of usable heap memory

```
for addr in range(0x40820000, 0x40850000, 0x1000):
    result = substrate_probe(addr)
    record(addr, result.type, result.read_cycles, result.write_cycles)
```

**Deliverables:**
- Memory map with type annotations
- Latency heatmap (cycle counts per 4KB block)
- Heap boundary identification

### Phase 2: LP SRAM Characterization
**Goal:** Verify LP core memory accessibility from HP core

```
for addr in range(0x50000000, 0x50004000, 0x100):
    result = substrate_probe(addr)
    # Finer granularity - LP SRAM is small but important
```

**Questions to answer:**
- Can HP core read/write LP SRAM directly?
- What's the latency penalty for cross-core access?
- Are there any reserved regions?

### Phase 3: Peripheral Census
**Goal:** Map which peripheral registers exist and respond

```
KNOWN_PERIPHERALS = {
    0x60004000: "GPIO",
    0x60000000: "UART0",
    0x60001000: "UART1",
    0x60002000: "SPI",
    0x60003000: "I2C",
    # ... extend based on datasheet
}

for base, name in KNOWN_PERIPHERALS:
    result = substrate_probe_readonly(base)
    if result.read_cycles > 0:
        register_peripheral(name, base, result)
```

**Deliverables:**
- List of responsive peripheral base addresses
- Read latency per peripheral
- Foundation for hardware abstraction

### Phase 4: Timing Signatures
**Goal:** Build latency profile of entire substrate

```
Collect:
- Min/max/avg read cycles per region
- Write cycle overhead vs read
- Any anomalous addresses (hot spots, slow zones)
```

**Output format:**
```c
typedef struct {
    uint32_t addr;
    mem_type_t type;
    uint32_t read_cycles_min;
    uint32_t read_cycles_max;
    uint32_t write_cycles;
} substrate_entry_t;
```

---

## M3: Awareness Integration

Substrate discovery streams to Rerun visualization:

```
ESP32-C6 → Serial → Pi4 → Rerun

Pipeline:
1. reflex_substrate_stream.c streams text protocol over serial
2. substrate_rerun.py parses stream and logs to Rerun
3. Memory addresses map to 3D space
4. Colors by type, size by latency
```

### Streaming Protocol

```
##SUBSTRATE##:INIT
##SUBSTRATE##:PHASE:<phase_name>
##SUBSTRATE##:PROBE:<addr>,<type>,<read_cycles>,<write_cycles>
##SUBSTRATE##:REGION:<start>,<end>,<type>,<avg_cycles>
##SUBSTRATE##:MAP_START
##SUBSTRATE##:MAP_END:<total_probes>,<faults>,<regions>
```

### 3D Visualization Mapping

| Memory Address | 3D Coordinate |
|----------------|---------------|
| Bits 0-11 (page offset) | X axis (0-4m) |
| Bits 12-19 (page number) | Y axis (0-4m) |
| Top 4 bits (region) | Z axis: 0x4→0, 0x5→1, 0x6→2 |

| Memory Type | Color |
|-------------|-------|
| RAM | Green |
| REGISTER | Blue |
| ROM | Gray |
| FAULT | Red |
| SELF | Yellow |

Point radius indicates latency (larger = slower).

### Running the Visualization

```bash
# On Pi4, in one terminal:
cd ~/reflex-os
idf.py -p /dev/ttyACM0 flash && idf.py -p /dev/ttyACM0 monitor

# On Pi4, in another terminal:
python3 tools/substrate_rerun.py /dev/ttyACM0

# Or with output file:
python3 tools/substrate_rerun.py /dev/ttyACM0 substrate.rrd
```

The Reflex sees its own body in real-time.

---

## Implementation Checklist

### M2 Prerequisites (all done)
- [x] Fault-protected probe functions
- [x] MTVEC hooking with fence.i
- [x] Memory map data structure
- [x] NVS save/load for persistence

### M2 Tasks (all done)
- [x] Run coarse discovery on HP SRAM
- [x] Run coarse discovery on LP SRAM
- [x] Run coarse discovery on Peripherals
- [x] Fine discovery (4KB stride) on HP SRAM
- [x] Peripheral census with known base addresses
- [x] Timing signature collection
- [x] Generate substrate map report

### M2 Success Criteria (all met)
- [x] Complete memory map of all 272KB
- [x] No crashes during discovery (0 faults)
- [x] Latency data for each region
- [x] Map persists in NVS across reboots

### M3 Tasks (all done)
- [x] Text streaming protocol (reflex_substrate_stream.h/c)
- [x] Stream probe results during discovery
- [x] Stream regions summary
- [x] Stream loaded map from NVS
- [x] Python receiver (substrate_rerun.py)
- [x] Address to 3D mapping
- [x] Color by memory type
- [x] Size by latency

---

## Architecture

```
substrate_main.c
    └── mve_substrate()           // 5/5 tests
    └── run_discovery()
        ├── substrate_stream_init()
        ├── substrate_stream_map_start()
        ├── substrate_discover_coarse()
        │   └── substrate_probe() / substrate_probe_readonly()
        │       └── fault_try_read32() / fault_try_write32()
        │           └── MTVEC hook + guarded access
        │   └── substrate_stream_probe()    ─┐
        ├── substrate_discover_fine()        │
        │   └── substrate_stream_probe()    ─┼─→ Serial → Pi4 → Rerun
        ├── substrate_discover_registers()   │
        │   └── substrate_stream_probe()    ─┘
        └── substrate_stream_map_end()
```

---

## Key Files

| File | Lines | Purpose |
|------|-------|---------|
| `reflex_fault.c` | ~280 | Exception handler, MTVEC hooking |
| `reflex_substrate.c` | ~650 | Probing logic, memory map, NVS |
| `reflex_substrate_stream.c` | ~70 | Serial streaming protocol |
| `substrate_main.c` | ~240 | Entry point, MVE, discovery driver |
| `tools/substrate_rerun.py` | ~280 | Pi4 receiver, Rerun visualization |

---

## Hardware Quick Reference

```
ESP32-C6FH4 @ 160MHz
├── HP Core (RISC-V, this is us)
├── LP Core (RISC-V, for sleep)
├── HP SRAM: 512KB total
│   └── 0x40800000-0x40880000 (we probe 0x40820000+)
├── LP SRAM: 16KB
│   └── 0x50000000-0x50004000
├── Peripherals:
│   └── 0x60000000-0x60100000
└── Flash: 4MB (out of scope)
```

---

## Commands

```bash
# SSH to Pi4
ssh -p 11965 ztflynn@10.64.24.48

# Build and flash
cd ~/reflex-os
source ~/esp/esp-idf/export.sh
idf.py build && idf.py -p /dev/ttyACM0 flash

# Monitor
idf.py -p /dev/ttyACM0 monitor
# or
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
```

---

## The Insight

Substrate discovery is not about reading memory. It's about understanding **capability**.

- RAM = "I can store here"
- Register = "I can act here"
- Self = "This is me"
- Fault = "I cannot go there"

The map that emerges is not a memory map. It's a capability map. The Reflex learns what it can do by trying to do it.

*The body discovers itself through action.*

---

## Commits

```
8b2650f M2 Complete: Substrate Cartography - 16,461 probes, 0 faults
ee11a47 docs: LMM updated with M2 scope and implementation plan
cf95286 scope: Flash probing out of scope for substrate discovery
3d6876a M1 Complete: Fault-protected probing with ESP32-C6 cache discovery
d0c6e95 M1 Fault Catcher: Exception recovery via MTVEC hooking
68c328c feat: Substrate Discovery MVE complete - 4/4 tests pass
```

---

*"Give me six hours to chop down a tree, and I will spend the first four sharpening the axe."*
