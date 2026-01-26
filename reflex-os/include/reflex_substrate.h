/**
 * reflex_substrate.h - Substrate Discovery for ESP32-C6
 *
 * The Reflex discovers its own body through memory probing.
 * No hardcoded knowledge - empirical discovery from scratch.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#ifndef REFLEX_SUBSTRATE_H
#define REFLEX_SUBSTRATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Memory Classification
// ============================================================

typedef enum {
    MEM_UNKNOWN    = 0,  // Not yet probed
    MEM_RAM        = 1,  // Read-write, value persists
    MEM_ROM        = 2,  // Read-only (or write-ignored)
    MEM_REGISTER   = 3,  // Volatile (different on re-read)
    MEM_FAULT      = 4,  // Access causes exception
    MEM_SELF       = 5,  // Reflex's own memory (not probed)
    MEM_RESERVED   = 6,  // Known reserved, skip probing
} mem_type_t;

static inline const char* mem_type_str(mem_type_t t) {
    switch(t) {
        case MEM_UNKNOWN:  return "UNKNOWN";
        case MEM_RAM:      return "RAM";
        case MEM_ROM:      return "ROM";
        case MEM_REGISTER: return "REGISTER";
        case MEM_FAULT:    return "FAULT";
        case MEM_SELF:     return "SELF";
        case MEM_RESERVED: return "RESERVED";
        default:           return "???";
    }
}

// ============================================================
// Probe Result
// ============================================================

typedef struct {
    uint32_t addr;           // Probed address
    mem_type_t type;         // Classification result
    uint32_t read_cycles;    // Cycles for read operation
    uint32_t write_cycles;   // Cycles for write operation (0 if not written)
    uint32_t original;       // Original value at address
    uint32_t readback;       // Value after write attempt
} probe_result_t;

// ============================================================
// Memory Region
// ============================================================

typedef struct {
    uint32_t start;           // Region start address
    uint32_t end;             // Region end address (exclusive)
    mem_type_t type;          // Region type
    uint32_t probe_count;     // Number of probes in this region
    uint32_t avg_read_cycles; // Average read timing
} mem_region_t;

// ============================================================
// Memory Map
// ============================================================

#define MAX_REGIONS 64

typedef struct {
    mem_region_t regions[MAX_REGIONS];
    uint8_t num_regions;
    uint32_t discovery_tick;  // When discovered
    uint32_t total_probes;    // How many probes performed
    uint32_t total_faults;    // How many faults caught
    uint32_t version;         // Map format version
} memory_map_t;

// ============================================================
// Discovery Trajectory (captures the process, not just result)
// ============================================================

#define MAX_SNAPSHOTS 32

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

// ============================================================
// API: Initialization
// ============================================================

/**
 * Initialize substrate discovery system
 * Must be called before any probing.
 */
void substrate_init(void);

/**
 * Mark region as self (will not be probed)
 * Call this for Reflex's own code/data/stack regions.
 */
void substrate_mark_self(uint32_t start, uint32_t end);

// ============================================================
// API: Probing
// ============================================================

/**
 * Probe a single address
 * Returns classification and timing information.
 */
probe_result_t substrate_probe(uint32_t addr);

/**
 * Probe a single address (read-only, safe for volatile registers)
 */
probe_result_t substrate_probe_readonly(uint32_t addr);

/**
 * Check if address is in self region (skip probing)
 */
bool substrate_is_self(uint32_t addr);

// ============================================================
// API: Discovery
// ============================================================

/**
 * Run coarse discovery (1MB stride)
 * Quick scan to identify major regions.
 */
void substrate_discover_coarse(memory_map_t* map);

/**
 * Run fine discovery (4KB stride within non-FAULT regions)
 */
void substrate_discover_fine(memory_map_t* map);

/**
 * Run register discovery (4B stride within REGISTER regions)
 */
void substrate_discover_registers(memory_map_t* map);

// ============================================================
// API: Map Management
// ============================================================

/**
 * Initialize empty map
 */
void substrate_map_init(memory_map_t* map);

/**
 * Add probe result to map
 */
void substrate_map_add(memory_map_t* map, probe_result_t* result);

/**
 * Find region containing address
 */
mem_region_t* substrate_map_find(memory_map_t* map, uint32_t addr);

/**
 * Save map to NVS
 */
bool substrate_map_save(memory_map_t* map);

/**
 * Load map from NVS
 */
bool substrate_map_load(memory_map_t* map);

/**
 * Print map to console
 */
void substrate_map_print(memory_map_t* map);

// ============================================================
// API: Trajectory
// ============================================================

/**
 * Take a snapshot of current discovery state
 */
void substrate_trajectory_snapshot(memory_map_t* map, discovery_trajectory_t* traj);

/**
 * Save trajectory to NVS
 */
bool substrate_trajectory_save(discovery_trajectory_t* traj);

/**
 * Load trajectory from NVS
 */
bool substrate_trajectory_load(discovery_trajectory_t* traj);

// ============================================================
// API: Timing
// ============================================================

/**
 * Get current cycle count (ESP32-C6 specific)
 */
uint32_t substrate_cycles(void);

/**
 * Convert cycles to nanoseconds (at 160MHz)
 */
uint32_t substrate_cycles_to_ns(uint32_t cycles);

#ifdef __cplusplus
}
#endif

#endif // REFLEX_SUBSTRATE_H
