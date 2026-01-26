/**
 * reflex_substrate.c - Substrate Discovery Implementation
 *
 * The Reflex discovers its own body through memory probing.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#include "reflex_substrate.h"
#include "reflex_fault.h"
#include "reflex.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_cpu.h"
#include "nvs_flash.h"
#include "nvs.h"

// Flag indicating if fault recovery is available
static bool s_fault_recovery_enabled = false;

static const char* TAG = "SUBSTRATE";

// ============================================================
// Self-Region Tracking
// ============================================================

#define MAX_SELF_REGIONS 8

typedef struct {
    uint32_t start;
    uint32_t end;
} self_region_t;

static self_region_t s_self_regions[MAX_SELF_REGIONS];
static int s_num_self_regions = 0;

// ============================================================
// Test Patterns
// ============================================================

#define TEST_PATTERN_1 0xA5A5A5A5
#define TEST_PATTERN_2 0x5A5A5A5A

// ============================================================
// Timing
// ============================================================

uint32_t substrate_cycles(void) {
    return (uint32_t)esp_cpu_get_cycle_count();
}

uint32_t substrate_cycles_to_ns(uint32_t cycles) {
    // 160MHz = 6.25ns per cycle = 25/4 ns per cycle
    return (cycles * 25) / 4;
}

// ============================================================
// Initialization
// ============================================================

// Linker symbols for self-detection
extern uint32_t _text_start;
extern uint32_t _text_end;
extern uint32_t _rodata_start;
extern uint32_t _rodata_end;
extern uint32_t _data_start;
extern uint32_t _data_end;
extern uint32_t _bss_start;
extern uint32_t _bss_end;

bool substrate_has_fault_recovery(void) {
    return s_fault_recovery_enabled;
}

void substrate_init(void) {
    ESP_LOGW(TAG, "Initializing substrate discovery...");

    s_num_self_regions = 0;

    // Initialize fault handling system
    fault_init();

    // Check if fault recovery was successfully enabled
    s_fault_recovery_enabled = fault_recovery_enabled();

    // Mark our own memory regions
    // These will be skipped during probing

    // Note: These linker symbols may not be available on all builds
    // If they fail, we'll use hardcoded safe ranges

    ESP_LOGW(TAG, "Substrate initialized. Self regions: %d", s_num_self_regions);
    ESP_LOGW(TAG, "Fault recovery: %s",
             s_fault_recovery_enabled ? "ENABLED" : "DISABLED (defensive mode)");
}

void substrate_mark_self(uint32_t start, uint32_t end) {
    if (s_num_self_regions >= MAX_SELF_REGIONS) {
        ESP_LOGW(TAG, "Max self regions reached");
        return;
    }

    s_self_regions[s_num_self_regions].start = start;
    s_self_regions[s_num_self_regions].end = end;
    s_num_self_regions++;

    ESP_LOGI(TAG, "Marked self region: 0x%08lx - 0x%08lx",
             (unsigned long)start, (unsigned long)end);
}

bool substrate_is_self(uint32_t addr) {
    for (int i = 0; i < s_num_self_regions; i++) {
        if (addr >= s_self_regions[i].start && addr < s_self_regions[i].end) {
            return true;
        }
    }
    return false;
}

// ============================================================
// Probing (Fault-Protected Version)
// ============================================================

probe_result_t substrate_probe_readonly(uint32_t addr) {
    probe_result_t result = {0};
    result.addr = addr;

    // Check self regions
    if (substrate_is_self(addr)) {
        result.type = MEM_SELF;
        return result;
    }

    // Use fault-protected read if available
    if (s_fault_recovery_enabled) {
        uint32_t val;
        uint32_t t0 = substrate_cycles();
        bool ok = fault_try_read32(addr, &val);
        uint32_t t1 = substrate_cycles();

        if (!ok) {
            result.type = MEM_FAULT;
            result.read_cycles = t1 - t0;
            return result;
        }

        result.read_cycles = t1 - t0;
        result.original = val;
        result.type = MEM_UNKNOWN;  // Read-only can't distinguish RAM/ROM
        return result;
    }

    // Fallback: raw read (may crash on unmapped memory!)
    volatile uint32_t* ptr = (volatile uint32_t*)addr;

    uint32_t t0 = substrate_cycles();
    uint32_t val = *ptr;
    uint32_t t1 = substrate_cycles();

    result.read_cycles = t1 - t0;
    result.original = val;
    result.type = MEM_UNKNOWN;

    return result;
}

// Flash address range on ESP32-C6 (memory-mapped, read-only)
// Writing to these addresses causes a cache error
#define FLASH_START 0x42000000
#define FLASH_END   0x44000000

static bool is_flash_address(uint32_t addr) {
    return (addr >= FLASH_START && addr < FLASH_END);
}

probe_result_t substrate_probe(uint32_t addr) {
    probe_result_t result = {0};
    result.addr = addr;

    // Check self regions
    if (substrate_is_self(addr)) {
        result.type = MEM_SELF;
        return result;
    }

    // Flash is memory-mapped read-only - writing causes cache error
    // Use read-only probing and classify as ROM
    if (is_flash_address(addr)) {
        result = substrate_probe_readonly(addr);
        result.type = MEM_ROM;  // Flash is ROM by definition
        return result;
    }

    // Use fault-protected probing if available
    if (s_fault_recovery_enabled) {
        uint32_t original, readback;

        // Phase 1: Read original value
        uint32_t t0 = substrate_cycles();
        bool read_ok = fault_try_read32(addr, &original);
        uint32_t t1 = substrate_cycles();
        result.read_cycles = t1 - t0;

        if (!read_ok) {
            result.type = MEM_FAULT;
            return result;
        }
        result.original = original;

        // Phase 2: Write test pattern
        uint32_t t2 = substrate_cycles();
        bool write_ok = fault_try_write32(addr, TEST_PATTERN_1);
        uint32_t t3 = substrate_cycles();
        result.write_cycles = t3 - t2;

        if (!write_ok) {
            // Write faulted - this is ROM or unmapped
            result.type = MEM_ROM;
            return result;
        }

        // Phase 3: Read back
        if (!fault_try_read32(addr, &readback)) {
            result.type = MEM_FAULT;
            return result;
        }
        result.readback = readback;

        // Phase 4: Restore original
        fault_try_write32(addr, original);

        // Classify based on behavior
        if (readback == TEST_PATTERN_1) {
            result.type = MEM_RAM;
        } else if (readback == original) {
            result.type = MEM_ROM;
        } else {
            result.type = MEM_REGISTER;
        }

        return result;
    }

    // Fallback: raw probing (may crash!)
    volatile uint32_t* ptr = (volatile uint32_t*)addr;

    // Phase 1: Read original value
    uint32_t t0 = substrate_cycles();
    uint32_t original = *ptr;
    uint32_t t1 = substrate_cycles();
    result.read_cycles = t1 - t0;
    result.original = original;

    // Phase 2: Write test pattern
    uint32_t t2 = substrate_cycles();
    *ptr = TEST_PATTERN_1;
    uint32_t t3 = substrate_cycles();
    result.write_cycles = t3 - t2;

    REFLEX_FENCE();

    // Phase 3: Read back
    uint32_t readback = *ptr;
    result.readback = readback;

    // Phase 4: Restore original
    *ptr = original;
    REFLEX_FENCE();

    // Classify based on behavior
    if (readback == TEST_PATTERN_1) {
        result.type = MEM_RAM;
    } else if (readback == original) {
        result.type = MEM_ROM;
    } else {
        result.type = MEM_REGISTER;
    }

    return result;
}

// ============================================================
// Map Management
// ============================================================

void substrate_map_init(memory_map_t* map) {
    memset(map, 0, sizeof(memory_map_t));
    map->version = 1;
}

void substrate_map_add(memory_map_t* map, probe_result_t* result) {
    map->total_probes++;

    if (result->type == MEM_FAULT) {
        map->total_faults++;
    }

    // Find existing region or create new one
    for (int i = 0; i < map->num_regions; i++) {
        mem_region_t* r = &map->regions[i];

        // Check if this probe extends an existing region
        if (r->type == result->type) {
            // Adjacent or overlapping?
            if (result->addr == r->end) {
                // Extends end
                r->end = result->addr + 4;
                r->probe_count++;
                // Update average timing
                r->avg_read_cycles =
                    ((r->avg_read_cycles * (r->probe_count - 1)) + result->read_cycles)
                    / r->probe_count;
                return;
            }
            if (result->addr + 4 == r->start) {
                // Extends start
                r->start = result->addr;
                r->probe_count++;
                r->avg_read_cycles =
                    ((r->avg_read_cycles * (r->probe_count - 1)) + result->read_cycles)
                    / r->probe_count;
                return;
            }
        }
    }

    // Create new region
    if (map->num_regions < MAX_REGIONS) {
        mem_region_t* r = &map->regions[map->num_regions];
        r->start = result->addr;
        r->end = result->addr + 4;
        r->type = result->type;
        r->probe_count = 1;
        r->avg_read_cycles = result->read_cycles;
        map->num_regions++;
    } else {
        ESP_LOGW(TAG, "Max regions reached, dropping probe result");
    }
}

mem_region_t* substrate_map_find(memory_map_t* map, uint32_t addr) {
    for (int i = 0; i < map->num_regions; i++) {
        if (addr >= map->regions[i].start && addr < map->regions[i].end) {
            return &map->regions[i];
        }
    }
    return NULL;
}

void substrate_map_print(memory_map_t* map) {
    ESP_LOGI(TAG, "=== MEMORY MAP ===");
    ESP_LOGI(TAG, "Total probes: %lu, Faults: %lu, Regions: %d",
             (unsigned long)map->total_probes,
             (unsigned long)map->total_faults,
             map->num_regions);

    for (int i = 0; i < map->num_regions; i++) {
        mem_region_t* r = &map->regions[i];
        uint32_t size = r->end - r->start;
        ESP_LOGI(TAG, "  [%d] 0x%08lx - 0x%08lx (%lu KB) %s  avg=%lu cycles",
                 i,
                 (unsigned long)r->start,
                 (unsigned long)r->end,
                 (unsigned long)(size / 1024),
                 mem_type_str(r->type),
                 (unsigned long)r->avg_read_cycles);
    }
}

// ============================================================
// NVS Persistence
// ============================================================

#define NVS_NAMESPACE "substrate"
#define NVS_KEY_MAP   "memmap"
#define NVS_KEY_TRAJ  "traj"

bool substrate_map_save(memory_map_t* map) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    map->discovery_tick = substrate_cycles();

    err = nvs_set_blob(handle, NVS_KEY_MAP, map, sizeof(memory_map_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save map: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Map saved to NVS (%d regions)", map->num_regions);
    return err == ESP_OK;
}

bool substrate_map_load(memory_map_t* map) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved map found");
        return false;
    }

    size_t size = sizeof(memory_map_t);
    err = nvs_get_blob(handle, NVS_KEY_MAP, map, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load map: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Loaded map from NVS (%d regions)", map->num_regions);
    return true;
}

// ============================================================
// Trajectory
// ============================================================

void substrate_trajectory_snapshot(memory_map_t* map, discovery_trajectory_t* traj) {
    if (traj->num_snapshots >= MAX_SNAPSHOTS) {
        // Shift old snapshots to make room
        memmove(&traj->snapshots[0], &traj->snapshots[1],
                sizeof(trajectory_snapshot_t) * (MAX_SNAPSHOTS - 1));
        traj->num_snapshots = MAX_SNAPSHOTS - 1;
    }

    trajectory_snapshot_t* snap = &traj->snapshots[traj->num_snapshots];
    snap->probe_count = map->total_probes;
    snap->tick = substrate_cycles();
    snap->num_regions = map->num_regions;

    // Count by type
    snap->num_ram = 0;
    snap->num_rom = 0;
    snap->num_register = 0;
    snap->num_fault = 0;

    for (int i = 0; i < map->num_regions; i++) {
        switch (map->regions[i].type) {
            case MEM_RAM:      snap->num_ram++; break;
            case MEM_ROM:      snap->num_rom++; break;
            case MEM_REGISTER: snap->num_register++; break;
            case MEM_FAULT:    snap->num_fault++; break;
            default: break;
        }
    }

    traj->num_snapshots++;
}

bool substrate_trajectory_save(discovery_trajectory_t* traj) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_set_blob(handle, NVS_KEY_TRAJ, traj, sizeof(discovery_trajectory_t));
    nvs_commit(handle);
    nvs_close(handle);

    return err == ESP_OK;
}

bool substrate_trajectory_load(discovery_trajectory_t* traj) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t size = sizeof(discovery_trajectory_t);
    err = nvs_get_blob(handle, NVS_KEY_TRAJ, traj, &size);
    nvs_close(handle);

    return err == ESP_OK;
}

// ============================================================
// Discovery (Coarse - 1MB stride)
// ============================================================

// Known safe regions to start with (from ESP32-C6 TRM)
// These won't fault, so we can probe them without exception handling
typedef struct {
    uint32_t start;
    uint32_t end;
    const char* name;
    bool read_only;  // If true, use read-only probing
} known_region_t;

// Note: HP SRAM starts at 0x40800000 but firmware is loaded there.
// We start probing from 0x40820000 (128KB in) to avoid firmware regions.
// A smarter approach would use heap_caps APIs to find actual free regions.
//
// Substrate discovery focuses on what The Reflex can DO, not what it can READ.
// Flash is ROM - its layout is known at compile time from the partition table.
// RAM and registers are discovered at runtime because they represent capability.
//
// NOTE: Flash probing is OUT OF SCOPE. Reading unprogrammed Flash causes
// unrecoverable cache errors on ESP32-C6 (mcause=0x19 during instruction prefetch).
static const known_region_t KNOWN_SAFE_REGIONS[] = {
    {0x40820000, 0x40850000, "HP SRAM (heap)", false},  // Skip first 128KB where firmware lives
    {0x50000000, 0x50004000, "LP SRAM", false},         // 16KB low-power SRAM
    {0x60000000, 0x60010000, "Peripherals", true},      // 64KB peripheral registers (read-only)
};
#define NUM_KNOWN_REGIONS (sizeof(KNOWN_SAFE_REGIONS) / sizeof(KNOWN_SAFE_REGIONS[0]))

void substrate_discover_coarse(memory_map_t* map) {
    ESP_LOGI(TAG, "Starting coarse discovery (known safe regions)...");

    discovery_trajectory_t traj = {0};

    // First, probe known safe regions
    for (int r = 0; r < NUM_KNOWN_REGIONS; r++) {
        const known_region_t* kr = &KNOWN_SAFE_REGIONS[r];
        ESP_LOGI(TAG, "Probing %s (0x%08lx - 0x%08lx)%s...",
                 kr->name, (unsigned long)kr->start, (unsigned long)kr->end,
                 kr->read_only ? " [read-only]" : "");

        // Probe start, middle, end of each region
        uint32_t addrs[] = {
            kr->start,
            kr->start + (kr->end - kr->start) / 2,
            kr->end - 4
        };

        for (int i = 0; i < 3; i++) {
            probe_result_t result;
            if (kr->read_only) {
                result = substrate_probe_readonly(addrs[i]);
                // Assign type based on region knowledge
                if (kr->start >= 0x42000000 && kr->start < 0x44000000) {
                    result.type = MEM_ROM;
                } else if (kr->start >= 0x60000000) {
                    result.type = MEM_REGISTER;
                }
            } else {
                result = substrate_probe(addrs[i]);
            }
            substrate_map_add(map, &result);

            ESP_LOGI(TAG, "  0x%08lx: %s (%lu cycles)",
                     (unsigned long)result.addr,
                     mem_type_str(result.type),
                     (unsigned long)result.read_cycles);
        }

        // Take trajectory snapshot after each region
        substrate_trajectory_snapshot(map, &traj);
    }

    // Save results
    substrate_map_save(map);
    substrate_trajectory_save(&traj);

    ESP_LOGI(TAG, "Coarse discovery complete.");
    substrate_map_print(map);
}

void substrate_discover_fine(memory_map_t* map) {
    ESP_LOGI(TAG, "Starting fine discovery (4KB stride in non-FAULT regions)...");

    discovery_trajectory_t traj = {0};
    substrate_trajectory_load(&traj);  // Load existing trajectory

    // For each known safe region, probe at 4KB intervals
    for (int r = 0; r < NUM_KNOWN_REGIONS; r++) {
        const known_region_t* kr = &KNOWN_SAFE_REGIONS[r];

        ESP_LOGI(TAG, "Fine probing %s%s...", kr->name,
                 kr->read_only ? " [read-only]" : "");

        for (uint32_t addr = kr->start; addr < kr->end; addr += 0x1000) {
            probe_result_t result;
            if (kr->read_only) {
                result = substrate_probe_readonly(addr);
                // Assign type based on region knowledge
                if (kr->start >= 0x42000000 && kr->start < 0x44000000) {
                    result.type = MEM_ROM;
                } else if (kr->start >= 0x60000000) {
                    result.type = MEM_REGISTER;
                }
            } else {
                result = substrate_probe(addr);
            }
            substrate_map_add(map, &result);

            // Progress every 64KB
            if ((addr & 0xFFFF) == 0) {
                ESP_LOGI(TAG, "  0x%08lx: %s",
                         (unsigned long)addr, mem_type_str(result.type));
            }
        }

        substrate_trajectory_snapshot(map, &traj);
    }

    substrate_map_save(map);
    substrate_trajectory_save(&traj);

    ESP_LOGI(TAG, "Fine discovery complete.");
    substrate_map_print(map);
}

void substrate_discover_registers(memory_map_t* map) {
    ESP_LOGI(TAG, "Starting register discovery (peripheral bus, 4B stride)...");

    // The peripheral bus is at 0x60000000 - 0x600FFFFF
    // This is where GPIO, UART, SPI, etc. registers live

    // WARNING: Writing to some registers can have side effects!
    // For now, use read-only probing for the peripheral region

    uint32_t start = 0x60000000;
    uint32_t end = 0x60010000;  // First 64KB only for safety

    for (uint32_t addr = start; addr < end; addr += 4) {
        probe_result_t result = substrate_probe_readonly(addr);

        // Mark as REGISTER since it's in peripheral space
        if (result.type == MEM_UNKNOWN) {
            result.type = MEM_REGISTER;
        }

        substrate_map_add(map, &result);

        // Progress every 4KB
        if ((addr & 0xFFF) == 0) {
            ESP_LOGI(TAG, "  0x%08lx probed", (unsigned long)addr);
        }
    }

    substrate_map_save(map);
    ESP_LOGI(TAG, "Register discovery complete.");
}
