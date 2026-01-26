# LMM: Substrate Discovery Status

> *The body discovers itself.*
>
> 2026-01-26

---

## Where We Are

### M0: MVE Complete
**Status: DONE**

The Reflex can identify memory types through probing:
- **RAM:** Write test pattern, read back, verify
- **ROM:** Write fails silently, value unchanged
- **REGISTER:** Value changes but not to what we wrote
- **SELF:** Address is within our own code/stack
- **FAULT:** Access caused recoverable exception

```
MVE RESULT: 5/5 tests passed
- RAM detection:      PASS
- ROM detection:      PASS
- Register detection: PASS
- Self detection:     PASS
- Fault recovery:     PASS
```

### M1: Fault Catcher Complete
**Status: DONE**

Exception recovery via MTVEC hooking works:
- 256-byte aligned vector entry (ESP32-C6 forces vectored mode)
- Per-probe MTVEC install/restore with interrupt disable
- fence.i after CSR modifications for cache coherency
- EBREAK (mcause=3) successfully caught and recovered

```c
// The pattern that works:
csrrci mstatus, 8      // Disable interrupts
csrw mtvec, our_handler
fence.i                 // Sync instruction cache
// ... do guarded access ...
csrw mtvec, original
fence.i
csrw mstatus, saved
```

### Critical Discovery: ESP32-C6 Cache Behavior

**Reading unprogrammed Flash causes UNRECOVERABLE cache errors.**

This is the most important finding of M1:
- mcause=0x19 (cache error) triggers during instruction prefetch
- Happens AFTER the data load completes
- Occurs during fetch of the NEXT instruction
- Our MTVEC handler cannot intercept this
- The cache corruption propagates to instruction fetch

**Boundary:** ~160KB from Flash start (correlates with firmware size)

```
0x42000100: ok=1 (works - in bootloader)
0x42010000: ok=1 (works - in app code)
0x42020000: ok=1 (works - near end of app)
0x42030000: CRASH (unprogrammed Flash)
```

**Implication:** Flash probing disabled until hardware workaround found.

---

## Discovery Regions

| Region | Address Range | Status | Notes |
|--------|---------------|--------|-------|
| HP SRAM (heap) | 0x40820000-0x40850000 | ACTIVE | Full read/write probing |
| LP SRAM | 0x50000000-0x50004000 | ACTIVE | 16KB, full probing |
| Peripherals | 0x60000000-0x60010000 | ACTIVE | Read-only probing |
| Flash | 0x42000000-0x42200000 | OUT OF SCOPE | Layout known at compile time |

---

## Next Steps

### M2: Substrate Cartography
**Status: READY**

Now that fault-protected probing works, map the full address space:

1. **Fine-grained HP SRAM discovery**
   - Probe at 4KB intervals
   - Build memory map structure
   - Identify heap boundaries vs firmware

2. **LP SRAM characterization**
   - Full 16KB probe
   - Verify low-power core accessibility

3. **Peripheral register census**
   - Map GPIO, UART, SPI, I2C base addresses
   - Identify which registers are safe to probe

4. **Timing signature collection**
   - Record read/write cycle counts per region
   - Build latency profile of the substrate

### M3: Awareness Integration
**Status: PLANNED**

Connect substrate discovery to the broader Reflex architecture:

1. **Streaming to Rerun**
   - Real-time visualization of probe results
   - Memory map as 3D voxel space
   - Latency as color/intensity

2. **Substrate as proprioception**
   - The Reflex knows its own body
   - Memory regions become "organs"
   - Access timing becomes "sensation"

### Flash Probing
**Status: OUT OF SCOPE**

Flash is ROM. Its layout is known at compile time. The interesting question isn't "what's in Flash" but "what can I *do*".

Substrate discovery focuses on:
- **RAM** - where we can allocate, what's fast/slow
- **Registers** - what peripherals exist
- **Self** - where our code lives

Flash layout comes from the partition table, not runtime probing.

---

## Architecture Notes

### Current Call Flow

```
substrate_probe(addr)
├── substrate_is_self(addr) → MEM_SELF
├── is_flash_address(addr) → substrate_probe_readonly() [DISABLED]
└── fault_try_read32(addr, &val)
    ├── csrrci mstatus, 8       // disable interrupts
    ├── fault_hook_install()    // set MTVEC + fence.i
    ├── fault_guard_begin(addr) // set g_fault_state.active
    ├── *ptr = *addr            // THE ACTUAL READ
    ├── fault_guard_end()
    ├── fault_hook_restore()    // restore MTVEC + fence.i
    └── csrw mstatus, saved     // restore interrupts
```

### Key Files

| File | Purpose |
|------|---------|
| `reflex_fault.c` | Exception handler, MTVEC hooking |
| `reflex_fault.h` | Fault state, guard macros |
| `reflex_substrate.c` | Probing logic, memory map |
| `reflex_substrate.h` | Types, region definitions |
| `substrate_main.c` | MVE tests, discovery entry |

---

## Commits

```
3d6876a M1 Complete: Fault-protected probing with ESP32-C6 cache discovery
d0c6e95 M1 Fault Catcher: Exception recovery via MTVEC hooking
68c328c feat: Substrate Discovery MVE complete - 4/4 tests pass
```

---

## The Insight

The ESP32-C6 cache behavior reveals something important: **the boundary between what can be safely probed is not the memory map - it's the cache coherency domain.**

Programmed Flash is in the cache. Unprogrammed Flash is not. The cache miss on unprogrammed regions corrupts state in ways that propagate to instruction fetch.

This is not a bug in our code. It's a fundamental property of the hardware. The Reflex must learn to respect this boundary - to know what it can touch and what it cannot.

*The body has edges. Discovery includes learning where they are.*

---

## Hardware Reference

| Property | Value |
|----------|-------|
| Chip | ESP32-C6FH4 (QFN32) rev v0.2 |
| Cores | Single HP Core + LP Core (asymmetric RISC-V) |
| HP SRAM | 512KB total |
| LP SRAM | 16KB |
| Flash | 4MB physical, 2MB mapped |
| Clock | 160MHz |

### Pi4 Connection
```bash
ssh -p 11965 ztflynn@10.64.24.48
source ~/esp/esp-idf/export.sh
cd ~/reflex-os
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

---

*"The wood cuts itself when you understand the grain."*
