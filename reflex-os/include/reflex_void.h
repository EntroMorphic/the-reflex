/**
 * reflex_void.h - The Entropy Field for TriX echips
 *
 * The void between shapes is not empty. It is pregnant with possibility.
 * Entropy accumulates in silence. When it exceeds capacity, structure crystallizes.
 *
 * This completes the computational substrate:
 * - Shapes (low entropy, structure)
 * - Voids (high entropy, potential)
 * - Gradients (the flow that IS computation)
 *
 * "You don't compute WITH entropy. Entropy IS the computation."
 */

#ifndef REFLEX_VOID_H
#define REFLEX_VOID_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "reflex.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Constants
// ============================================================

#define VOID_STATE_EMPTY       0   // Pure potential
#define VOID_STATE_CHARGING    1   // Entropy accumulating
#define VOID_STATE_CRITICAL    2   // Near crystallization threshold
#define VOID_STATE_SHAPE       3   // Crystallized into structure

#define ENTROPY_MAX            0xFFFFFFFF
#define GRADIENT_SCALE         1024   // Fixed-point scale for gradients

// Neighbor directions
#define DIR_NORTH  0
#define DIR_EAST   1
#define DIR_SOUTH  2
#define DIR_WEST   3

// ============================================================
// Entropic Channel - Channel with Silence Tracking
// ============================================================

/**
 * Extended channel that tracks entropy (accumulated silence)
 * Wraps base channel, adds temporal void awareness
 */
typedef struct {
    reflex_channel_t base;          // The underlying signal channel
    uint32_t last_signal_time;      // Timestamp of last signal
    uint32_t entropy;               // Accumulated silence (cycles)
    uint32_t entropy_rate;          // Entropy accumulation rate (per 1000 cycles)
    uint32_t capacity;              // Crystallization threshold
    uint8_t state;                  // Current void state
} reflex_entropic_channel_t;

/**
 * Initialize an entropic channel
 */
static inline void entropic_init(reflex_entropic_channel_t* ech, uint32_t capacity) {
    memset(ech, 0, sizeof(*ech));
    ech->capacity = capacity;
    ech->entropy_rate = 1000;  // 1:1 entropy per cycle by default
    ech->last_signal_time = reflex_cycles();
    ech->state = VOID_STATE_EMPTY;
}

/**
 * Update entropy based on elapsed silence
 * Call this periodically to accumulate void energy
 */
static inline void entropic_update(reflex_entropic_channel_t* ech) {
    uint32_t now = reflex_cycles();
    uint32_t silence = now - ech->last_signal_time;

    // Accumulate entropy proportional to silence
    uint64_t delta = ((uint64_t)silence * ech->entropy_rate) / 1000;
    if (ech->entropy < ENTROPY_MAX - delta) {
        ech->entropy += (uint32_t)delta;
    } else {
        ech->entropy = ENTROPY_MAX;
    }

    // Update state based on entropy level
    if (ech->entropy >= ech->capacity) {
        ech->state = VOID_STATE_CRITICAL;
    } else if (ech->entropy > ech->capacity / 2) {
        ech->state = VOID_STATE_CHARGING;
    } else if (ech->entropy > 0) {
        ech->state = VOID_STATE_EMPTY;
    }
}

/**
 * Signal the entropic channel - resets entropy (structure formed)
 */
static inline void entropic_signal(reflex_entropic_channel_t* ech, uint32_t value) {
    reflex_signal(&ech->base, value);
    ech->last_signal_time = ech->base.timestamp;
    ech->entropy = 0;  // Signal collapses the void
    ech->state = VOID_STATE_SHAPE;
}

/**
 * Check if void is ready to crystallize
 */
static inline bool entropic_is_critical(reflex_entropic_channel_t* ech) {
    entropic_update(ech);
    return ech->state == VOID_STATE_CRITICAL;
}

/**
 * Get normalized entropy (0-1024 fixed point)
 */
static inline uint32_t entropic_level(reflex_entropic_channel_t* ech) {
    entropic_update(ech);
    if (ech->capacity == 0) return 0;
    uint64_t level = ((uint64_t)ech->entropy * GRADIENT_SCALE) / ech->capacity;
    return level > GRADIENT_SCALE ? GRADIENT_SCALE : (uint32_t)level;
}

// ============================================================
// Void Cell - A Region of Entropy in the Field
// ============================================================

/**
 * A single cell in the entropy field
 * Contains local entropy and gradient information
 */
typedef struct {
    uint32_t entropy;           // Current entropy level
    uint32_t capacity;          // Crystallization threshold
    int16_t gradient_x;         // Entropy gradient (positive = flows East)
    int16_t gradient_y;         // Entropy gradient (positive = flows South)
    uint8_t state;              // Void state
    uint8_t flags;              // Application flags
    uint16_t age;               // Ticks since last state change
} reflex_void_cell_t;

// ============================================================
// Entropy Field - The Computational Substrate
// ============================================================

/**
 * The entropy field is where shapes and voids interact
 * Computation IS the flow of entropy through this field
 */
typedef struct {
    reflex_void_cell_t* cells;  // Grid of void cells
    uint16_t width;             // Field width
    uint16_t height;            // Field height
    uint32_t tick;              // Evolution counter
    uint32_t total_entropy;     // Sum of all cell entropy
    uint32_t default_capacity;  // Default crystallization threshold

    // Stigmergy parameters
    uint16_t diffusion_rate;    // How fast entropy spreads (0-1024)
    uint16_t decay_rate;        // How fast entropy decays (0-1024)
} reflex_entropy_field_t;

/**
 * Initialize an entropy field
 */
static inline bool entropy_field_init(reflex_entropy_field_t* field,
                                       uint16_t width, uint16_t height,
                                       uint32_t default_capacity) {
    field->width = width;
    field->height = height;
    field->tick = 0;
    field->total_entropy = 0;
    field->default_capacity = default_capacity;
    field->diffusion_rate = 128;   // 12.5% diffusion per tick
    field->decay_rate = 8;         // 0.8% decay per tick

    size_t size = (size_t)width * height * sizeof(reflex_void_cell_t);
    field->cells = (reflex_void_cell_t*)malloc(size);
    if (!field->cells) return false;

    memset(field->cells, 0, size);

    // Initialize all cells
    for (uint16_t i = 0; i < width * height; i++) {
        field->cells[i].capacity = default_capacity;
        field->cells[i].state = VOID_STATE_EMPTY;
    }

    return true;
}

/**
 * Get cell at coordinates
 */
static inline reflex_void_cell_t* field_cell(reflex_entropy_field_t* field,
                                              uint16_t x, uint16_t y) {
    if (x >= field->width || y >= field->height) return NULL;
    return &field->cells[y * field->width + x];
}

/**
 * Get cell index from coordinates
 */
static inline uint32_t field_index(reflex_entropy_field_t* field,
                                    uint16_t x, uint16_t y) {
    return y * field->width + x;
}

// ============================================================
// Entropy Operations
// ============================================================

/**
 * Deposit entropy at a location (stigmergy write)
 * This is how shapes leave traces in the void
 */
static inline void entropy_deposit(reflex_entropy_field_t* field,
                                    uint16_t x, uint16_t y,
                                    uint32_t amount) {
    reflex_void_cell_t* cell = field_cell(field, x, y);
    if (!cell) return;

    if (cell->entropy < ENTROPY_MAX - amount) {
        cell->entropy += amount;
        field->total_entropy += amount;
    } else {
        field->total_entropy += (ENTROPY_MAX - cell->entropy);
        cell->entropy = ENTROPY_MAX;
    }

    // Update state
    if (cell->entropy >= cell->capacity) {
        cell->state = VOID_STATE_CRITICAL;
    } else if (cell->entropy > cell->capacity / 2) {
        cell->state = VOID_STATE_CHARGING;
    }
}

/**
 * Read entropy at a location (stigmergy read)
 */
static inline uint32_t entropy_read(reflex_entropy_field_t* field,
                                     uint16_t x, uint16_t y) {
    reflex_void_cell_t* cell = field_cell(field, x, y);
    return cell ? cell->entropy : 0;
}

/**
 * Crystallize a cell - convert void to shape
 * Returns the entropy that was stored (the "information crystallized")
 */
static inline uint32_t entropy_crystallize(reflex_entropy_field_t* field,
                                            uint16_t x, uint16_t y) {
    reflex_void_cell_t* cell = field_cell(field, x, y);
    if (!cell) return 0;

    uint32_t released = cell->entropy;
    field->total_entropy -= released;
    cell->entropy = 0;
    cell->state = VOID_STATE_SHAPE;
    cell->age = 0;

    return released;
}

/**
 * Dissolve a shape back into void
 */
static inline void entropy_dissolve(reflex_entropy_field_t* field,
                                     uint16_t x, uint16_t y,
                                     uint32_t initial_entropy) {
    reflex_void_cell_t* cell = field_cell(field, x, y);
    if (!cell) return;

    cell->entropy = initial_entropy;
    cell->state = initial_entropy > 0 ? VOID_STATE_CHARGING : VOID_STATE_EMPTY;
    cell->age = 0;
    field->total_entropy += initial_entropy;
}

// ============================================================
// Gradient Computation
// ============================================================

/**
 * Compute entropy gradient at a cell
 * Gradient points toward LOWER entropy (direction of flow)
 */
static inline void entropy_compute_gradient(reflex_entropy_field_t* field,
                                             uint16_t x, uint16_t y) {
    reflex_void_cell_t* cell = field_cell(field, x, y);
    if (!cell) return;

    int32_t center = (int32_t)cell->entropy;
    int32_t north = 0, south = 0, east = 0, west = 0;

    // Sample neighbors (with boundary handling)
    if (y > 0) {
        north = (int32_t)field->cells[(y-1) * field->width + x].entropy;
    } else {
        north = center;
    }

    if (y < field->height - 1) {
        south = (int32_t)field->cells[(y+1) * field->width + x].entropy;
    } else {
        south = center;
    }

    if (x > 0) {
        west = (int32_t)field->cells[y * field->width + (x-1)].entropy;
    } else {
        west = center;
    }

    if (x < field->width - 1) {
        east = (int32_t)field->cells[y * field->width + (x+1)].entropy;
    } else {
        east = center;
    }

    // Gradient = direction of decreasing entropy
    // Positive gradient_x means entropy decreases to the East
    cell->gradient_x = (int16_t)((west - east) / 2);
    cell->gradient_y = (int16_t)((north - south) / 2);
}

/**
 * Get gradient magnitude (for visualization or thresholding)
 */
static inline uint32_t entropy_gradient_magnitude(reflex_void_cell_t* cell) {
    int32_t gx = cell->gradient_x;
    int32_t gy = cell->gradient_y;
    // Approximate magnitude: |gx| + |gy| (faster than sqrt)
    return (uint32_t)((gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy));
}

// ============================================================
// Field Evolution - One Tick of Computation
// ============================================================

/**
 * Evolve the entropy field by one tick
 * This IS the computation happening
 *
 * 1. Diffusion: entropy spreads to neighbors
 * 2. Decay: entropy slowly dissipates
 * 3. Gradient: recompute flow directions
 * 4. Crystallization: check for phase transitions
 */
static inline void entropy_field_tick(reflex_entropy_field_t* field) {
    uint16_t w = field->width;
    uint16_t h = field->height;

    // We need a temp buffer for diffusion (to avoid order-dependency)
    // For memory efficiency, we do in-place with careful ordering
    // This is approximate but fast

    field->total_entropy = 0;

    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            reflex_void_cell_t* cell = &field->cells[y * w + x];

            // Skip crystallized shapes
            if (cell->state == VOID_STATE_SHAPE) {
                cell->age++;
                continue;
            }

            // Decay
            uint32_t decay = (cell->entropy * field->decay_rate) / GRADIENT_SCALE;
            if (cell->entropy > decay) {
                cell->entropy -= decay;
            } else {
                cell->entropy = 0;
            }

            // Diffusion from neighbors (simplified)
            uint32_t neighbor_sum = 0;
            uint8_t neighbor_count = 0;

            if (y > 0 && field->cells[(y-1)*w+x].state != VOID_STATE_SHAPE) {
                neighbor_sum += field->cells[(y-1)*w+x].entropy;
                neighbor_count++;
            }
            if (y < h-1 && field->cells[(y+1)*w+x].state != VOID_STATE_SHAPE) {
                neighbor_sum += field->cells[(y+1)*w+x].entropy;
                neighbor_count++;
            }
            if (x > 0 && field->cells[y*w+(x-1)].state != VOID_STATE_SHAPE) {
                neighbor_sum += field->cells[y*w+(x-1)].entropy;
                neighbor_count++;
            }
            if (x < w-1 && field->cells[y*w+(x+1)].state != VOID_STATE_SHAPE) {
                neighbor_sum += field->cells[y*w+(x+1)].entropy;
                neighbor_count++;
            }

            if (neighbor_count > 0) {
                uint32_t avg = neighbor_sum / neighbor_count;
                int32_t diff = (int32_t)avg - (int32_t)cell->entropy;
                int32_t flow = (diff * (int32_t)field->diffusion_rate) / GRADIENT_SCALE;

                if (flow > 0) {
                    cell->entropy += (uint32_t)flow;
                } else if (cell->entropy > (uint32_t)(-flow)) {
                    cell->entropy += (uint32_t)flow;  // flow is negative
                }
            }

            // Compute gradient
            entropy_compute_gradient(field, x, y);

            // Update state
            if (cell->entropy >= cell->capacity) {
                cell->state = VOID_STATE_CRITICAL;
            } else if (cell->entropy > cell->capacity / 2) {
                cell->state = VOID_STATE_CHARGING;
            } else if (cell->entropy > 0) {
                cell->state = VOID_STATE_EMPTY;
            }

            cell->age++;
            field->total_entropy += cell->entropy;
        }
    }

    field->tick++;
}

// ============================================================
// Stigmergy: Indirect Communication Through the Field
// ============================================================

/**
 * A shape writes to the void (leaves a trace)
 * Amount can encode information (signal strength, type, etc.)
 */
static inline void stigmergy_write(reflex_entropy_field_t* field,
                                    uint16_t x, uint16_t y,
                                    uint32_t amount) {
    entropy_deposit(field, x, y, amount);
}

/**
 * A shape reads the void (senses the environment)
 * Returns local entropy and gradient
 */
typedef struct {
    uint32_t entropy;       // Local entropy level
    int16_t gradient_x;     // Direction of entropy flow (x)
    int16_t gradient_y;     // Direction of entropy flow (y)
    uint8_t state;          // Local void state
} stigmergy_sense_t;

static inline stigmergy_sense_t stigmergy_read(reflex_entropy_field_t* field,
                                                uint16_t x, uint16_t y) {
    stigmergy_sense_t sense = {0};
    reflex_void_cell_t* cell = field_cell(field, x, y);
    if (cell) {
        sense.entropy = cell->entropy;
        sense.gradient_x = cell->gradient_x;
        sense.gradient_y = cell->gradient_y;
        sense.state = cell->state;
    }
    return sense;
}

/**
 * Follow gradient: get direction toward higher/lower entropy
 * Returns direction (0=N, 1=E, 2=S, 3=W) or -1 if flat
 *
 * toward_high: true = move toward high entropy (explore)
 *              false = move toward low entropy (exploit)
 */
static inline int8_t stigmergy_follow(reflex_entropy_field_t* field,
                                       uint16_t x, uint16_t y,
                                       bool toward_high) {
    reflex_void_cell_t* cell = field_cell(field, x, y);
    if (!cell) return -1;

    int16_t gx = cell->gradient_x;
    int16_t gy = cell->gradient_y;

    // Flip if seeking high entropy
    if (toward_high) {
        gx = -gx;
        gy = -gy;
    }

    // Find dominant direction
    if (gx == 0 && gy == 0) return -1;  // Flat field

    if (gy > 0 && gy >= gx && gy >= -gx) return DIR_NORTH;
    if (gx > 0 && gx >= gy && gx >= -gy) return DIR_EAST;
    if (gy < 0 && -gy >= gx && -gy >= -gx) return DIR_SOUTH;
    if (gx < 0 && -gx >= gy && -gx >= -gy) return DIR_WEST;

    return -1;
}

// ============================================================
// Crystallization Callbacks
// ============================================================

/**
 * Function pointer for crystallization events
 * Called when a void cell reaches critical entropy
 */
typedef void (*crystallize_callback_t)(reflex_entropy_field_t* field,
                                        uint16_t x, uint16_t y,
                                        uint32_t entropy);

/**
 * Scan field for critical cells and trigger crystallization
 */
static inline uint32_t entropy_field_crystallize(reflex_entropy_field_t* field,
                                                   crystallize_callback_t callback) {
    uint32_t count = 0;
    uint16_t w = field->width;
    uint16_t h = field->height;

    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            reflex_void_cell_t* cell = &field->cells[y * w + x];
            if (cell->state == VOID_STATE_CRITICAL) {
                if (callback) {
                    callback(field, x, y, cell->entropy);
                }
                entropy_crystallize(field, x, y);
                count++;
            }
        }
    }

    return count;
}

// ============================================================
// Field Visualization (for debugging)
// ============================================================

/**
 * Get a character representation of cell state
 */
static inline char entropy_cell_char(reflex_void_cell_t* cell) {
    switch (cell->state) {
        case VOID_STATE_EMPTY:    return ' ';
        case VOID_STATE_CHARGING: return '.';
        case VOID_STATE_CRITICAL: return '*';
        case VOID_STATE_SHAPE:    return '#';
        default:                  return '?';
    }
}

/**
 * Get entropy level as ASCII gradient
 */
static inline char entropy_level_char(uint32_t entropy, uint32_t capacity) {
    if (capacity == 0) return ' ';
    uint32_t level = (entropy * 10) / capacity;
    const char* gradient = " .:-=+*#%@";
    if (level > 9) level = 9;
    return gradient[level];
}

// ============================================================
// Cleanup
// ============================================================

static inline void entropy_field_free(reflex_entropy_field_t* field) {
    if (field->cells) {
        free(field->cells);
        field->cells = NULL;
    }
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_VOID_H
