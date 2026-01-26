# Product Requirements Document: Reflex Substrate Discovery

> **Version:** 1.2
> **Date:** 2026-01-26
> **Status:** Draft (Reviewed)
> **Author:** Claude (via LMM synthesis)

---

## 1. Executive Summary

The Reflex is an embodied self-discovery system running on the ESP32-C6 microcontroller. Its purpose is to discover and learn to use its own hardware capabilities through direct interaction, without relying on hardcoded documentation.

**The Problem:** The current implementation attempts to discover correlations using ADC readings that are electrically identical to GPIO outputs (same physical pins). This is self-measurement, not discovery. All "learning" has been invalid.

**The Solution:** Redesign the Reflex to discover its body at the CPU substrate level - memory addresses, registers, timing characteristics - through systematic probing. Peripherals like GPIO become discoverable features OF the memory map, not hardcoded assumptions.

**Core Principle:** The Reflex's body IS the memory space. Discovery is probing. Learning is classification.

---

## 2. Goals and Non-Goals

### 2.1 Goals

1. **G1:** Enable the Reflex to discover the ESP32-C6 memory map without hardcoded knowledge
2. **G2:** Classify memory regions as RAM, ROM, REGISTER, or FAULT through empirical probing
3. **G3:** Persist discoveries across resets using NVS
4. **G4:** Provide cycle-accurate timing for all probe operations
5. **G5:** Recover gracefully from memory access faults (no crashes)
6. **G6:** Establish foundation for higher-level discovery (correlations, goals)

### 2.2 Non-Goals (Deferred)

1. **NG1:** Instruction timing discovery (ADD vs MUL performance)
2. **NG2:** Cache effect discovery (first vs second access timing)
3. **NG3:** Interrupt/preemption discovery
4. **NG4:** DMA and concurrent access patterns
5. **NG5:** Power mode and clock scaling discovery
6. **NG6:** WiFi/BLE sensing
7. **NG7:** Layer architecture redesign (evaluate after substrate works)

---

## 3. Background

### 3.1 Current State

The existing Reflex implementation (`reflex_layers.h`, `layers_main.c`) has:
- 8 output GPIOs (pins 0-7)
- 8 input GPIOs (pins 10, 11, 14, 15, 18, 19, 20, 21)
- 4 ADC channels (0-3)
- 1 temperature sensor
- 3 exploration layers with different time constants
- Crystal persistence to NVS

**Critical Flaw:** ADC channels 0-3 on ESP32-C6 ARE GPIO pins 0-3. The system reads its own output state, not external correlations.

### 3.2 Target Platform

| Property | Value |
|----------|-------|
| MCU | ESP32-C6 |
| Architecture | RISC-V RV32IMAC |
| SRAM | 320KB |
| Flash | 4MB (external, memory-mapped) |
| MMU | None (has PMP) |
| Cores | 1 |
| Clock | 160 MHz typical |
| Framework | ESP-IDF + FreeRTOS |

### 3.3 Relevant Memory Map (ESP32-C6)

| Region | Start | End | Type |
|--------|-------|-----|------|
| Internal ROM | 0x40000000 | 0x4005FFFF | ROM |
| Internal SRAM (HP) | 0x40800000 | 0x4084FFFF | RAM |
| LP SRAM | 0x50000000 | 0x50003FFF | RAM |
| External Flash | 0x42000000 | 0x423FFFFF | ROM (mapped) |
| Peripheral Bus | 0x60000000 | 0x600FFFFF | Registers |

**Note:** The Reflex should DISCOVER this map, not have it hardcoded. See Appendix A for details.

---

## 4. Requirements

### 4.1 Functional Requirements

#### FR1: Fault Recovery
| ID | Requirement | Priority |
|----|-------------|----------|
| FR1.1 | System SHALL install exception handler before any probing | P0 |
| FR1.2 | Memory access faults SHALL be caught and logged, not cause reset | P0 |
| FR1.3 | Fault handler SHALL record faulting address and cause | P0 |
| FR1.4 | System SHALL recover to known-safe state after fault | P0 |
| FR1.5 | Fault recovery SHALL use setjmp/longjmp or equivalent | P1 |

#### FR2: Memory Probing
| ID | Requirement | Priority |
|----|-------------|----------|
| FR2.1 | System SHALL probe any 32-bit aligned address | P0 |
| FR2.2 | Probe SHALL write test pattern, read back, classify | P0 |
| FR2.3 | Classification types: RAM, ROM, REGISTER, FAULT, SELF | P0 |
| FR2.4 | Probe SHALL restore original value after classification | P1 |
| FR2.5 | System SHALL NOT probe its own code/data/stack regions | P0 |
| FR2.6 | System SHALL support configurable probe stride (4B to 1MB) | P1 |

#### FR3: Timing Measurement
| ID | Requirement | Priority |
|----|-------------|----------|
| FR3.1 | System SHALL measure CPU cycles for each probe operation | P0 |
| FR3.2 | Cycle counter SHALL use ESP32-C6 performance counter CSRs (0x7E0-0x7E2) | P0 |
| FR3.3 | Timing resolution SHALL be single-cycle (6.25ns at 160MHz) | P1 |
| FR3.4 | System SHALL record read and write cycle counts separately | P2 |
| FR3.5 | Performance counter SHALL be initialized before first use | P0 |

**Note:** ESP32-C6 does NOT use standard RISC-V mcycle CSR. It uses proprietary registers:
- `mpcer` (0x7E0): Machine Performance Counter Event Register
- `mpcmr` (0x7E1): Machine Performance Counter Mode Register
- `mpccr` (0x7E2): Machine Performance Counter Count Register

#### FR4: Map Persistence
| ID | Requirement | Priority |
|----|-------------|----------|
| FR4.1 | Discovered memory map SHALL persist to NVS | P0 |
| FR4.2 | Map SHALL load from NVS on boot if available | P0 |
| FR4.3 | Map SHALL support at least 64 distinct regions | P1 |
| FR4.4 | Adjacent same-type regions SHALL coalesce | P2 |
| FR4.5 | Map SHALL include discovery timestamp | P2 |
| FR4.6 | Discovery trajectory SHALL be captured (not just final state) | P1 |

**Rationale for FR4.6:** Research on online observation (Delta Observer) demonstrates that transient structure during learning contains information unavailable in final states. The discovery *process* — how regions are classified over time — may reveal patterns (timing anomalies, classification changes) that the final map obscures. Capture intermediate snapshots.

#### FR5: Self-Protection
| ID | Requirement | Priority |
|----|-------------|----------|
| FR5.1 | System SHALL identify own memory bounds at init | P0 |
| FR5.2 | Code section bounds SHALL be marked as SELF | P0 |
| FR5.3 | Data section bounds SHALL be marked as SELF | P0 |
| FR5.4 | Stack region SHALL be marked as SELF | P0 |
| FR5.5 | FreeRTOS heap SHALL be marked as SELF | P1 |

### 4.2 Non-Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR1 | Coarse discovery (1MB stride, 0x0-0x70000000) SHALL complete in <10 seconds | P1 |
| NFR2 | Fine discovery (4KB stride within region) SHALL complete in <1 second per MB | P2 |
| NFR3 | NVS storage for map SHALL use <8KB | P1 |
| NFR4 | Probe engine code SHALL fit in <4KB flash | P2 |
| NFR5 | System SHALL work with ESP-IDF v5.x | P0 |

---

## 5. System Architecture

### 5.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     REFLEX SUBSTRATE LAYER                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐                                               │
│  │    FAULT     │  - Exception handler installation             │
│  │   CATCHER    │  - setjmp/longjmp recovery                    │
│  │              │  - Fault logging (addr, cause)                │
│  └──────┬───────┘                                               │
│         │                                                        │
│  ┌──────▼───────┐                                               │
│  │    PROBE     │  - Write/read/classify cycle                  │
│  │   ENGINE     │  - Self-region avoidance                      │
│  │              │  - Configurable stride                        │
│  └──────┬───────┘                                               │
│         │                                                        │
│  ┌──────▼───────┐                                               │
│  │   TIMING     │  - Performance counter CSR access (0x7E0-2)   │
│  │   ORACLE     │  - Per-operation cycle counts                 │
│  └──────┬───────┘                                               │
│         │                                                        │
│  ┌──────▼───────┐                                               │
│  │     MAP      │  - Region storage                             │
│  │  PERSISTER   │  - NVS read/write                             │
│  │              │  - Coalescing                                  │
│  └──────┬───────┘                                               │
│         │                                                        │
│  ┌──────▼───────┐                                               │
│  │   MEMORY     │  - Discovered topology                        │
│  │     MAP      │  - Query interface                            │
│  └─────────────┘                                               │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  FUTURE: CAPABILITY LAYER                        │
│         (Layers, Crystals, Goals - built on discovered map)      │
└─────────────────────────────────────────────────────────────────┘
```

### 5.2 Data Structures

#### Memory Classification Enum
```c
typedef enum {
    MEM_UNKNOWN    = 0,  // Not yet probed
    MEM_RAM        = 1,  // Read-write, value persists
    MEM_ROM        = 2,  // Read-only, value doesn't change on write
    MEM_REGISTER   = 3,  // Volatile, reads return varying values
    MEM_FAULT      = 4,  // Access causes exception
    MEM_SELF       = 5,  // Reflex's own memory, not probed
    MEM_READABLE   = 6,  // Readable but write status unknown (read-only probe)
} mem_type_t;
```

**Classification Logic:**
- `MEM_RAM`: Write test pattern, read back matches → writeable memory
- `MEM_ROM`: Write test pattern, read back unchanged → read-only or write-ignored
- `MEM_REGISTER`: Read returns different value than what was written AND different from original → volatile
- `MEM_FAULT`: Any access causes exception
- `MEM_SELF`: Address falls within Reflex's own memory bounds
- `MEM_READABLE`: Read succeeded but no write attempted (read-only probe mode)

#### Probe Result
```c
typedef struct {
    uint32_t addr;         // Probed address
    mem_type_t type;       // Classification result
    uint32_t read_cycles;  // Cycles for read operation
    uint32_t write_cycles; // Cycles for write operation
    uint32_t fault_cause;  // RISC-V mcause if faulted
} probe_result_t;
```

#### Memory Region
```c
typedef struct {
    uint32_t start;           // Region start address
    uint32_t end;             // Region end address (exclusive)
    mem_type_t type;          // Region type
    uint32_t avg_read_cycles; // Average read timing
} mem_region_t;
```

#### Memory Map
```c
#define MAX_REGIONS 64

typedef struct {
    mem_region_t regions[MAX_REGIONS];
    uint8_t num_regions;
    uint32_t discovery_tick;  // When discovered
    uint32_t total_probes;    // How many probes performed
    uint32_t version;         // Map format version
} memory_map_t;
```

#### Discovery Trajectory (FR4.6)
```c
#define MAX_SNAPSHOTS 16

typedef struct {
    uint32_t probe_count;     // Probes completed at snapshot
    uint32_t tick;            // When snapshot taken
    uint8_t num_regions;      // Regions discovered so far
    uint8_t num_ram;          // Count by type
    uint8_t num_rom;
    uint8_t num_register;
    uint8_t num_fault;
} trajectory_snapshot_t;

typedef struct {
    trajectory_snapshot_t snapshots[MAX_SNAPSHOTS];
    uint8_t num_snapshots;
} discovery_trajectory_t;
```

**Rationale:** Capturing the trajectory enables analysis of discovery dynamics — where the map changed, how confidence evolved, whether classifications were stable or oscillated. The transient states during discovery ARE the learning process, not just intermediate noise (see Delta Observer research).

### 5.3 Key Algorithms

#### Probe Classification
```
PROBE(addr):
    IF addr IN self_regions THEN RETURN SELF

    SET recovery_point
    IF recovered_from_fault THEN RETURN FAULT

    original = READ(addr)
    WRITE(addr, TEST_PATTERN)
    FENCE
    readback = READ(addr)
    WRITE(addr, original)  // Restore
    FENCE

    IF readback == TEST_PATTERN THEN RETURN RAM
    IF readback == original THEN RETURN ROM
    RETURN REGISTER
```

#### Hierarchical Discovery
```
DISCOVER():
    // Phase 1: Coarse (1MB stride)
    FOR addr FROM 0x00000000 TO 0x70000000 STEP 0x100000:
        result = PROBE(addr)
        MAP_ADD(result)
    SAVE_MAP()

    // Phase 2: Fine (4KB stride within non-FAULT regions)
    FOR region IN map WHERE type != FAULT:
        FOR addr FROM region.start TO region.end STEP 0x1000:
            result = PROBE(addr)
            MAP_ADD(result)
    SAVE_MAP()

    // Phase 3: Register detail (4B stride within REGISTER regions)
    FOR region IN map WHERE type == REGISTER:
        FOR addr FROM region.start TO region.end STEP 4:
            result = PROBE(addr)
            MAP_ADD(result)
    SAVE_MAP()
```

---

## 6. API Specification

### 6.1 Fault Catcher API

```c
// Initialize exception handling (call before any probing)
void reflex_fault_init(void);

// Check if last probe caused a fault
bool reflex_probe_faulted(void);

// Get fault details
uint32_t reflex_fault_address(void);   // mtval
uint32_t reflex_fault_cause(void);     // mcause
```

**Implementation Note (ESP-IDF specific):**
```c
#include "esp_system.h"
#include <setjmp.h>

static jmp_buf s_probe_jmp;
static volatile bool s_probe_active = false;
static volatile uint32_t s_fault_addr = 0;
static volatile uint32_t s_fault_cause = 0;

// Custom panic handler - registered with ESP-IDF
static void IRAM_ATTR probe_panic_handler(void *frame) {
    if (s_probe_active) {
        // Extract fault info from exception frame
        // Frame structure is architecture-specific
        s_fault_cause = /* read mcause */;
        s_fault_addr = /* read mtval */;
        s_probe_active = false;
        longjmp(s_probe_jmp, 1);
    }
    // If not probing, continue to default panic behavior
}

void reflex_fault_init(void) {
    // Register our custom panic handler
    // NOTE: Verify this API exists in your ESP-IDF version
    // May need to use lower-level hooks
}
```

**Warning:** The panic handler approach needs careful testing. ESP-IDF's panic handling is complex and may not support longjmp in all cases. Alternative approach: use PMP to make regions temporarily accessible and rely on exceptions only for truly unmapped addresses.

### 6.2 Probe Engine API

```c
// Initialize probe engine
void reflex_probe_init(void);

// Mark region as self (will not be probed)
void reflex_probe_mark_self(uint32_t start, uint32_t end);

// Probe modes
typedef enum {
    PROBE_MODE_FULL,      // Read, write test pattern, read, restore
    PROBE_MODE_READ_ONLY, // Read only (safe for volatile registers)
    PROBE_MODE_TIMING,    // Multiple reads to measure timing variance
} probe_mode_t;

// Probe single address
probe_result_t reflex_probe_addr(uint32_t addr);

// Probe single address with mode
probe_result_t reflex_probe_addr_mode(uint32_t addr, probe_mode_t mode);

// Probe region with callback
typedef void (*probe_callback_t)(probe_result_t* result);
void reflex_probe_region(uint32_t start, uint32_t end,
                         uint32_t stride, probe_callback_t cb);

// Probe region with mode
void reflex_probe_region_mode(uint32_t start, uint32_t end,
                              uint32_t stride, probe_mode_t mode,
                              probe_callback_t cb);
```

**Probe Mode Notes:**
- `PROBE_MODE_FULL`: Standard write/read classification. May have side effects on peripheral registers.
- `PROBE_MODE_READ_ONLY`: Only reads the address. Cannot distinguish RAM from ROM, but safe for volatile registers. Classifies as RAM_OR_ROM vs FAULT.
- `PROBE_MODE_TIMING`: Multiple reads to detect volatile registers (registers that return different values on consecutive reads).

### 6.3 Timing Oracle API

```c
// Initialize performance counter (must call before reflex_cycles)
void reflex_timing_init(void);

// Read cycle counter (returns current cycle count)
static inline uint32_t reflex_cycles(void);

// Measure operation timing
#define TIMED_OP(op, cycles_out)
```

**Implementation Note (ESP32-C6 specific):**
```c
// ESP32-C6 uses custom CSRs, NOT standard mcycle
#define CSR_MPCER  0x7E0  // Event register
#define CSR_MPCMR  0x7E1  // Mode register
#define CSR_MPCCR  0x7E2  // Count register

void reflex_timing_init(void) {
    // Configure counter to count CPU cycles
    __asm__ volatile("csrw 0x7E0, 1");  // Event = CPU cycles
    // Enable the counter
    __asm__ volatile("csrw 0x7E1, 1");  // Mode = enabled
}

static inline uint32_t reflex_cycles(void) {
    uint32_t cycles;
    __asm__ volatile("csrr %0, 0x7E2" : "=r"(cycles));
    return cycles;
}
```

### 6.4 Map Persister API

```c
// Initialize map (loads from NVS if available)
void reflex_map_init(memory_map_t* map);

// Add probe result to map
void reflex_map_add(memory_map_t* map, probe_result_t* result);

// Save map to NVS
void reflex_map_save(memory_map_t* map);

// Load map from NVS
bool reflex_map_load(memory_map_t* map);

// Find region containing address
mem_region_t* reflex_map_find(memory_map_t* map, uint32_t addr);

// Print map for debugging
void reflex_map_print(memory_map_t* map);

// Trajectory capture (FR4.6)
void reflex_map_snapshot(memory_map_t* map, discovery_trajectory_t* traj);

// Save trajectory to NVS
void reflex_trajectory_save(discovery_trajectory_t* traj);

// Load trajectory from NVS
bool reflex_trajectory_load(discovery_trajectory_t* traj);
```

---

## 7. Test Plan

### 7.1 Unit Tests

| ID | Test | Expected Result |
|----|------|-----------------|
| UT1 | Probe known RAM (0x40800000) | Returns MEM_RAM |
| UT2 | Probe known ROM (0x42000000) | Returns MEM_ROM |
| UT3 | Probe unmapped (0xFFFFFFFF) | Returns MEM_FAULT, no crash |
| UT4 | Probe peripheral (0x60004000) | Returns MEM_REGISTER |
| UT5 | Probe self code region | Returns MEM_SELF |
| UT6 | Cycle counter reads | Non-zero, increasing |
| UT7 | Map save/load cycle | Data survives |

### 7.2 Integration Tests

| ID | Test | Expected Result |
|----|------|-----------------|
| IT1 | Coarse discovery completes | Map has >5 regions |
| IT2 | Map persists across reset | Regions identical |
| IT3 | Fine discovery refines RAM | Sub-regions identified |
| IT4 | Register discovery finds GPIO | 0x60004xxx in map |

### 7.3 Validation Tests

| ID | Test | Expected Result |
|----|------|-----------------|
| VT1 | Compare discovered map to datasheet | >90% match |
| VT2 | No self-corruption after 1000 probes | System stable |
| VT3 | Fault recovery under stress | No resets |

### 7.4 Minimum Viable Experiment

```c
void mve_substrate(void) {
    reflex_substrate_init();

    printf("=== Minimum Viable Experiment ===\n");

    // Test 1: RAM detection
    probe_result_t r1 = reflex_probe_addr(0x40800000);
    printf("RAM  (0x40800000): type=%d, expect=1 [%s]\n",
           r1.type, r1.type == MEM_RAM ? "PASS" : "FAIL");

    // Test 2: ROM detection
    probe_result_t r2 = reflex_probe_addr(0x42000000);
    printf("ROM  (0x42000000): type=%d, expect=2 [%s]\n",
           r2.type, r2.type == MEM_ROM ? "PASS" : "FAIL");

    // Test 3: Fault detection
    probe_result_t r3 = reflex_probe_addr(0xFFFFFFFF);
    printf("FAULT(0xFFFFFFFF): type=%d, expect=4 [%s]\n",
           r3.type, r3.type == MEM_FAULT ? "PASS" : "FAIL");

    // Test 4: Register detection
    probe_result_t r4 = reflex_probe_addr(0x60004000);
    printf("REG  (0x60004000): type=%d, expect=3 [%s]\n",
           r4.type, r4.type == MEM_REGISTER ? "PASS" : "FAIL");

    printf("=================================\n");
}
```

---

## 8. Implementation Plan

### 8.1 Milestones

| Milestone | Description | Dependencies |
|-----------|-------------|--------------|
| M1 | Fault Catcher working | None |
| M2 | Single-address probing | M1 |
| M3 | MVE passes all 4 tests | M2 |
| M4 | Map persistence | M3 |
| M5 | Coarse discovery | M4 |
| M6 | Validation against datasheet | M5 |
| M7 | Fine discovery | M6 |
| M8 | Integration with existing layers | M7 |

### 8.2 File Structure

```
the-reflex/
└── reflex-os/
    ├── include/
    │   ├── reflex_fault.h      # Exception handling
    │   ├── reflex_probe.h      # Probe engine
    │   ├── reflex_timing.h     # Cycle counter
    │   ├── reflex_map.h        # Memory map
    │   └── reflex_substrate.h  # Top-level substrate API
    ├── src/
    │   ├── reflex_fault.c
    │   ├── reflex_probe.c
    │   ├── reflex_map.c
    │   └── reflex_substrate.c
    └── main/
        └── substrate_main.c    # MVE and discovery entry point
```

### 8.3 Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| ESP-IDF panic handler conflicts | Medium | High | Test early, use `esp_panic_handler_register()` |
| setjmp/longjmp incompatible with FreeRTOS | Low | High | Use task-local recovery points |
| Probing corrupts DMA/peripheral state | Medium | Medium | Add quiescence checks |
| PMP blocks expected memory regions | Low | Medium | Document and adapt |
| Flash wear from frequent NVS writes | Low | Low | Batch writes, rate limit |
| Using wrong cycle counter CSR | High | Medium | **RESOLVED:** Use 0x7E0-0x7E2, not mcycle |
| Peripheral side effects on probe | Medium | Medium | Consider read-only probe mode |
| Watchdog timeout during discovery | Medium | Low | Feed watchdog, or disable during probe |

---

## 9. Success Criteria

### 9.1 Must Have (P0)

- [ ] Exception handler catches faults without system reset
- [ ] Probe correctly classifies RAM, ROM, REGISTER, FAULT
- [ ] Self-memory regions are never probed
- [ ] Map persists to NVS and loads on reboot
- [ ] MVE passes all 4 test cases

### 9.2 Should Have (P1)

- [ ] Coarse discovery completes in <10 seconds
- [ ] Discovered map matches ESP32-C6 datasheet >90%
- [ ] Timing measurements have <10% variance
- [ ] Map uses <8KB NVS storage

### 9.3 Nice to Have (P2)

- [ ] Fine discovery identifies individual GPIO registers
- [ ] Adjacent same-type regions coalesce automatically
- [ ] Per-region timing statistics available
- [ ] Discovery progress reported via serial

---

## 10. Open Questions

1. **Q1:** How does ESP-IDF's panic handler interact with custom exception handling?
   - ESP-IDF provides `esp_panic_handler_register()` to register custom handler
   - Handler receives exception frame pointer with register state
   - Need to verify longjmp works from within panic context
   - Alternative: hook at lower level via `rv_utils_set_mtvec()`

2. **Q2:** Can setjmp/longjmp cross FreeRTOS task boundaries safely?
   - Likely yes if used within single task
   - Need to verify stack unwinding is clean
   - Consider using `esp_task_wdt_reset()` if probing takes long

3. **Q3:** What happens when probing active peripheral registers?
   - Some registers have side effects on read (e.g., clear-on-read status)
   - Some registers have side effects on write (e.g., trigger actions)
   - May need "safe probe" mode that only reads, doesn't write
   - Consider adding MEM_VOLATILE subtype for such registers

4. **Q4:** How to handle memory-mapped flash that requires cache flush?
   - External flash at 0x42000000 is accessed through MMU cache
   - Probe may see stale data without proper cache handling
   - Use `fence` instruction and potentially `esp_rom_spiflash_*` APIs

5. **Q5:** Should discovery be interruptible/resumable?
   - Long discovery could be interrupted by reset or watchdog
   - Map should persist incrementally during discovery
   - Consider checkpoint every N probes

6. **Q6:** How do we distinguish RAM from write-through cache?
   - Some addresses may appear as RAM but are actually cached flash
   - May need secondary classification based on timing characteristics

---

## 11. References

1. [ESP32-C6 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
2. [ESP32-C6 Datasheet v1.3](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
3. [ESP-IDF Memory Types (ESP32-C6)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/memory-types.html)
4. [ESP32 Family Memory Map 101](https://developer.espressif.com/blog/esp32-memory-map-101/)
5. [Counting CPU Cycles on ESP32-C3/C6](https://ctrlsrc.io/posts/2023/counting-cpu-cycles-on-esp32c3-esp32c6/)
6. [ESP-IDF Panic Handler](https://github.com/espressif/esp-idf/blob/master/components/esp_system/panic.c)
7. RISC-V Privileged Architecture Specification (CSRs)
8. Lincoln Manifold Method documentation (`/the-reflex/notes/LMM.md`)
9. LMM Synthesis: `/tmp/reflex_substrate_synth.md`
10. [Delta Observer](https://github.com/EntroMorphic/delta-observer) - Empirical validation that online observation captures transient structure invisible to post-hoc analysis (4% R² improvement)

---

## Appendix A: ESP32-C6 Memory Map Reference

For validation purposes only - the Reflex should discover this, not use it as input.

| Region | Start | End | Size | Type | Notes |
|--------|-------|-----|------|------|-------|
| Internal ROM | 0x40000000 | 0x4005FFFF | 384KB | ROM | Bootloader, ROM functions |
| Internal SRAM (HP) | 0x40800000 | 0x4084FFFF | 320KB | RAM | High-power SRAM |
| LP SRAM | 0x50000000 | 0x50003FFF | 16KB | RAM | Low-power SRAM (was "RTC memory") |
| External Flash | 0x42000000 | 0x423FFFFF | 4MB | ROM | MMU-mapped, cached |
| Peripheral Bus | 0x60000000 | 0x600FFFFF | 1MB | REG | Memory-mapped I/O |

**Important Notes:**
- HP SRAM is the primary working memory for the HP (High Power) CPU
- LP SRAM retains data in low-power modes and can be accessed by LP CPU
- External Flash access goes through MMU cache - timing may vary
- Peripheral registers may have side effects on read/write

---

## Appendix B: RISC-V Exception Causes

| mcause | Exception | Relevant to Probing |
|--------|-----------|---------------------|
| 0 | Instruction address misaligned | No |
| 1 | Instruction access fault | No |
| 2 | Illegal instruction | No |
| 4 | Load address misaligned | Yes - if probe addr not 4-byte aligned |
| 5 | Load access fault | Yes - unmapped or protected read |
| 6 | Store address misaligned | Yes - if probe addr not 4-byte aligned |
| 7 | Store access fault | Yes - unmapped, protected, or ROM write |

---

## Appendix C: ESP32-C6 Performance Counter CSRs

ESP32-C6 does **NOT** implement the standard RISC-V `mcycle` CSR. Instead, it provides custom performance counters:

| CSR Address | Name | Purpose |
|-------------|------|---------|
| 0x7E0 | mpcer | Machine Performance Counter Event Register |
| 0x7E1 | mpcmr | Machine Performance Counter Mode Register |
| 0x7E2 | mpccr | Machine Performance Counter Count Register |

### Initialization Sequence
```asm
csrw 0x7E0, 1    ; Configure to count CPU cycles (event = 1)
csrw 0x7E1, 1    ; Enable the counter (mode = 1)
```

### Reading Cycle Count
```asm
csrr t0, 0x7E2   ; Read current cycle count into t0
```

### Event Types (mpcer values)
| Value | Event |
|-------|-------|
| 1 | CPU cycles |
| (others) | See ESP32-C6 TRM for full list |

**Reference:** [Counting CPU Cycles on ESP32-C3/C6](https://ctrlsrc.io/posts/2023/counting-cpu-cycles-on-esp32c3-esp32c6/)

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-26 | Claude | Initial draft from LMM synthesis |
| 1.1 | 2026-01-26 | Claude | Corrected cycle counter (ESP32-C6 uses 0x7E0-0x7E2, not mcycle); Updated exception handler API to `esp_panic_handler_register()`; Added LP SRAM terminology; Added Appendix C for performance counters; Expanded risk assessment; Added Q6 to open questions |
| 1.2 | 2026-01-26 | Claude | Added FR4.6 (trajectory capture) based on Delta Observer research; Added discovery_trajectory_t data structure; Added trajectory API; Added reference to Delta Observer |

