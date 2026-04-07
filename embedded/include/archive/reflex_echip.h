/**
 * reflex_echip.h - Self-Reconfiguring Soft Processor
 *
 * A chip that watches itself think and rewires accordingly.
 *
 * Components:
 * - Frozen Shapes: ~4,000 static logic elements (the nouns)
 * - Mutable Routes: ~15,000 dynamic connections (the verbs)
 * - Entropy Field: Tracks usage, enables growth (the grammar)
 *
 * Behaviors:
 * - Hebbian Learning: Routes that fire together wire together
 * - Entropy Pruning: Unused routes dissolve back to void
 * - Crystallization: High-entropy voids become new shapes
 * - Self-Composition: The chip literally grows new circuits
 *
 * "The frozen shapes are the nouns. The routing is the verbs.
 *  The entropy field is the grammar that lets them compose into meaning."
 */

#ifndef REFLEX_ECHIP_H
#define REFLEX_ECHIP_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "reflex.h"
#include "reflex_void.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Constants
// ============================================================

#define ECHIP_MAX_SHAPES        4096
#define ECHIP_MAX_ROUTES        16384
#define ECHIP_MAX_PORTS         8
#define ECHIP_FIELD_SIZE        32      // 32x32 entropy field

// Fixed-point scale for weights (1024 = 1.0)
#define WEIGHT_SCALE            1024
#define WEIGHT_MAX              (WEIGHT_SCALE * 4)
#define WEIGHT_MIN              (-WEIGHT_SCALE * 4)

// Thresholds
#define ACTIVITY_THRESHOLD      10      // Min activity to stay alive
#define CRYSTALLIZE_THRESHOLD   50000   // Entropy level to spawn shape
#define STRENGTHEN_THRESHOLD    100     // Activity to start strengthening

// ============================================================
// Shape Types (The Nouns)
// ============================================================

typedef enum {
    SHAPE_VOID = 0,      // Empty slot (can be filled by crystallization)

    // Logic
    SHAPE_NAND,          // Universal gate: out = !(a & b)
    SHAPE_NOR,           // Universal gate: out = !(a | b)
    SHAPE_XOR,           // Parity: out = a ^ b
    SHAPE_BUFFER,        // Relay: out = in (with delay)
    SHAPE_NOT,           // Inverter: out = !in

    // Memory
    SHAPE_LATCH,         // D-latch: stores value
    SHAPE_TOGGLE,        // T flip-flop: toggles on input

    // Arithmetic
    SHAPE_ADD,           // out = a + b
    SHAPE_SUB,           // out = a - b
    SHAPE_MUL,           // out = a * b (lower bits)
    SHAPE_CMP,           // out = (a > b) ? 1 : (a < b) ? -1 : 0

    // Routing
    SHAPE_MUX,           // out = sel ? b : a
    SHAPE_DEMUX,         // routes input to one of outputs based on sel
    SHAPE_FANOUT,        // copies input to all outputs

    // Interface
    SHAPE_INPUT,         // External input port
    SHAPE_OUTPUT,        // External output port

    // Special
    SHAPE_NEURON,        // Integrate-and-fire neuron
    SHAPE_OSCILLATOR,    // Periodic signal source

    SHAPE_TYPE_COUNT
} shape_type_t;

// Port counts for each shape type
static const uint8_t shape_inputs[SHAPE_TYPE_COUNT] = {
    0,  // VOID
    2,  // NAND
    2,  // NOR
    2,  // XOR
    1,  // BUFFER
    1,  // NOT
    2,  // LATCH (data, enable)
    1,  // TOGGLE
    2,  // ADD
    2,  // SUB
    2,  // MUL
    2,  // CMP
    3,  // MUX (a, b, sel)
    2,  // DEMUX (in, sel)
    1,  // FANOUT
    0,  // INPUT (external)
    1,  // OUTPUT
    8,  // NEURON (multiple synapses)
    0,  // OSCILLATOR
};

static const uint8_t shape_outputs[SHAPE_TYPE_COUNT] = {
    0,  // VOID
    1,  // NAND
    1,  // NOR
    1,  // XOR
    1,  // BUFFER
    1,  // NOT
    1,  // LATCH
    1,  // TOGGLE
    1,  // ADD
    1,  // SUB
    1,  // MUL
    1,  // CMP
    1,  // MUX
    4,  // DEMUX (4 outputs)
    4,  // FANOUT (4 outputs)
    1,  // INPUT
    0,  // OUTPUT (external)
    1,  // NEURON
    1,  // OSCILLATOR
};

// ============================================================
// Frozen Shape (Static Computational Element)
// ============================================================

typedef struct {
    uint16_t id;                     // Unique identifier
    uint16_t x, y;                   // Position in field
    shape_type_t type;               // What computation it performs

    // Port values
    int16_t inputs[ECHIP_MAX_PORTS]; // Input port values
    int16_t outputs[ECHIP_MAX_PORTS];// Output port values

    // Internal state
    int32_t state;                   // For latches, neurons, etc.
    int32_t threshold;               // For neurons: firing threshold

    // Metadata
    uint16_t age;                    // Ticks since creation
    uint8_t flags;                   // Application-defined
    uint8_t frozen;                  // If true, cannot be dissolved
} frozen_shape_t;

// ============================================================
// Route State
// ============================================================

typedef enum {
    ROUTE_EMPTY = 0,     // Slot available
    ROUTE_DORMANT,       // Exists but inactive
    ROUTE_ACTIVE,        // Currently carrying signals
    ROUTE_STRENGTHENING, // Being reinforced by Hebbian rule
    ROUTE_WEAKENING,     // Decaying from disuse
} route_state_t;

// ============================================================
// Mutable Route (Dynamic Connection)
// ============================================================

typedef struct {
    // Connection
    uint16_t src_shape;              // Source shape ID
    uint8_t src_port;                // Source output port
    uint16_t dst_shape;              // Destination shape ID
    uint8_t dst_port;                // Destination input port

    // Dynamics
    int16_t weight;                  // Synaptic strength (fixed-point)
    uint16_t activity;               // Recent activity counter
    uint8_t delay;                   // Propagation delay (ticks)
    route_state_t state;             // Current state

    // Field integration
    uint8_t field_x, field_y;        // Position in entropy field
} mutable_route_t;

// ============================================================
// The Self-Reconfiguring Processor
// ============================================================

typedef struct {
    // === Frozen Shapes ===
    frozen_shape_t* shapes;
    uint16_t num_shapes;
    uint16_t max_shapes;
    uint16_t next_shape_id;

    // === Mutable Routes ===
    mutable_route_t* routes;
    uint16_t num_routes;
    uint16_t max_routes;

    // === Entropy Field ===
    reflex_entropy_field_t field;

    // === Learning Parameters ===
    uint16_t hebbian_rate;           // Strengthening rate (0-1024)
    uint16_t decay_rate;             // Weakening rate (0-1024)
    uint16_t activity_decay;         // Activity counter decay (0-1024)
    uint32_t crystallize_threshold;  // Entropy to spawn shape
    uint16_t dissolve_activity;      // Activity below which routes dissolve

    // === Timing ===
    uint64_t tick;
    uint32_t ticks_per_entropy_update; // How often to update field

    // === Statistics ===
    uint32_t shapes_created;
    uint32_t shapes_dissolved;
    uint32_t routes_created;
    uint32_t routes_dissolved;
    uint32_t signals_propagated;

    // === External Interface ===
    int16_t* ext_inputs;             // External input values
    int16_t* ext_outputs;            // External output values
    uint8_t num_ext_inputs;
    uint8_t num_ext_outputs;

} echip_t;

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize an echip with given capacity
 */
static inline bool echip_init(echip_t* chip,
                               uint16_t max_shapes,
                               uint16_t max_routes,
                               uint8_t field_size) {
    memset(chip, 0, sizeof(*chip));

    chip->max_shapes = max_shapes;
    chip->max_routes = max_routes;

    // Allocate shapes
    chip->shapes = (frozen_shape_t*)calloc(max_shapes, sizeof(frozen_shape_t));
    if (!chip->shapes) return false;

    // Allocate routes
    chip->routes = (mutable_route_t*)calloc(max_routes, sizeof(mutable_route_t));
    if (!chip->routes) {
        free(chip->shapes);
        return false;
    }

    // Initialize entropy field
    if (!entropy_field_init(&chip->field, field_size, field_size,
                            CRYSTALLIZE_THRESHOLD)) {
        free(chip->shapes);
        free(chip->routes);
        return false;
    }

    // Default learning parameters
    chip->hebbian_rate = 64;         // 6.25% per tick
    chip->decay_rate = 8;            // 0.8% per tick
    chip->activity_decay = 32;       // 3.1% per tick
    chip->crystallize_threshold = CRYSTALLIZE_THRESHOLD;
    chip->dissolve_activity = ACTIVITY_THRESHOLD;
    chip->ticks_per_entropy_update = 10;

    chip->next_shape_id = 1;  // 0 reserved for "no shape"

    return true;
}

/**
 * Free echip resources
 */
static inline void echip_free(echip_t* chip) {
    if (chip->shapes) free(chip->shapes);
    if (chip->routes) free(chip->routes);
    entropy_field_free(&chip->field);
    memset(chip, 0, sizeof(*chip));
}

// ============================================================
// Shape Management
// ============================================================

/**
 * Create a new frozen shape
 * Returns shape ID, or 0 on failure
 */
static inline uint16_t echip_create_shape(echip_t* chip,
                                           shape_type_t type,
                                           uint16_t x, uint16_t y) {
    if (chip->num_shapes >= chip->max_shapes) return 0;

    // Find empty slot
    for (uint16_t i = 0; i < chip->max_shapes; i++) {
        if (chip->shapes[i].type == SHAPE_VOID) {
            frozen_shape_t* s = &chip->shapes[i];
            memset(s, 0, sizeof(*s));
            s->id = chip->next_shape_id++;
            s->type = type;
            s->x = x;
            s->y = y;
            s->threshold = WEIGHT_SCALE;  // Default neuron threshold

            chip->num_shapes++;
            chip->shapes_created++;

            // Mark position in entropy field as shape
            reflex_void_cell_t* cell = field_cell(&chip->field,
                x % chip->field.width, y % chip->field.height);
            if (cell) {
                cell->state = VOID_STATE_SHAPE;
                cell->entropy = 0;
            }

            return s->id;
        }
    }
    return 0;
}

/**
 * Find shape by ID
 */
static inline frozen_shape_t* echip_find_shape(echip_t* chip, uint16_t id) {
    for (uint16_t i = 0; i < chip->max_shapes; i++) {
        if (chip->shapes[i].id == id) {
            return &chip->shapes[i];
        }
    }
    return NULL;
}

/**
 * Dissolve a shape back to void
 */
static inline bool echip_dissolve_shape(echip_t* chip, uint16_t id) {
    frozen_shape_t* s = echip_find_shape(chip, id);
    if (!s || s->frozen) return false;

    // Return entropy to field
    uint16_t fx = s->x % chip->field.width;
    uint16_t fy = s->y % chip->field.height;
    entropy_dissolve(&chip->field, fx, fy, chip->crystallize_threshold / 2);

    // Clear shape
    s->type = SHAPE_VOID;
    s->id = 0;
    chip->num_shapes--;
    chip->shapes_dissolved++;

    return true;
}

// ============================================================
// Route Management
// ============================================================

/**
 * Create a new route between shapes
 * Returns route index, or -1 on failure
 */
static inline int echip_create_route(echip_t* chip,
                                      uint16_t src_shape, uint8_t src_port,
                                      uint16_t dst_shape, uint8_t dst_port,
                                      int16_t initial_weight) {
    if (chip->num_routes >= chip->max_routes) return -1;

    // Verify shapes exist
    frozen_shape_t* src = echip_find_shape(chip, src_shape);
    frozen_shape_t* dst = echip_find_shape(chip, dst_shape);
    if (!src || !dst) return -1;

    // Verify ports are valid
    if (src_port >= shape_outputs[src->type]) return -1;
    if (dst_port >= shape_inputs[dst->type]) return -1;

    // Find empty slot
    for (uint16_t i = 0; i < chip->max_routes; i++) {
        if (chip->routes[i].state == ROUTE_EMPTY) {
            mutable_route_t* r = &chip->routes[i];
            r->src_shape = src_shape;
            r->src_port = src_port;
            r->dst_shape = dst_shape;
            r->dst_port = dst_port;
            r->weight = initial_weight;
            r->activity = ACTIVITY_THRESHOLD * 2;  // Start healthy
            r->delay = 1;
            r->state = ROUTE_DORMANT;

            // Position in field (midpoint between shapes)
            r->field_x = ((src->x + dst->x) / 2) % chip->field.width;
            r->field_y = ((src->y + dst->y) / 2) % chip->field.height;

            chip->num_routes++;
            chip->routes_created++;

            return i;
        }
    }
    return -1;
}

/**
 * Dissolve a route back to void
 */
static inline void echip_dissolve_route(echip_t* chip, uint16_t route_idx) {
    if (route_idx >= chip->max_routes) return;
    mutable_route_t* r = &chip->routes[route_idx];
    if (r->state == ROUTE_EMPTY) return;

    // Return entropy to field
    entropy_deposit(&chip->field, r->field_x, r->field_y,
                    chip->crystallize_threshold / 10);

    r->state = ROUTE_EMPTY;
    chip->num_routes--;
    chip->routes_dissolved++;
}

// ============================================================
// Shape Evaluation (The Computation)
// ============================================================

/**
 * Evaluate a single shape's logic
 */
static inline void shape_evaluate(frozen_shape_t* s) {
    switch (s->type) {
        case SHAPE_NAND:
            s->outputs[0] = !(s->inputs[0] && s->inputs[1]) ? WEIGHT_SCALE : 0;
            break;

        case SHAPE_NOR:
            s->outputs[0] = !(s->inputs[0] || s->inputs[1]) ? WEIGHT_SCALE : 0;
            break;

        case SHAPE_XOR:
            s->outputs[0] = (s->inputs[0] != 0) != (s->inputs[1] != 0) ? WEIGHT_SCALE : 0;
            break;

        case SHAPE_BUFFER:
            s->outputs[0] = s->inputs[0];
            break;

        case SHAPE_NOT:
            s->outputs[0] = s->inputs[0] ? 0 : WEIGHT_SCALE;
            break;

        case SHAPE_LATCH:
            if (s->inputs[1]) {  // Enable
                s->state = s->inputs[0];
            }
            s->outputs[0] = s->state;
            break;

        case SHAPE_TOGGLE:
            if (s->inputs[0] > WEIGHT_SCALE/2 && s->state == 0) {
                s->outputs[0] = s->outputs[0] ? 0 : WEIGHT_SCALE;
            }
            s->state = s->inputs[0] > WEIGHT_SCALE/2 ? 1 : 0;
            break;

        case SHAPE_ADD:
            s->outputs[0] = s->inputs[0] + s->inputs[1];
            break;

        case SHAPE_SUB:
            s->outputs[0] = s->inputs[0] - s->inputs[1];
            break;

        case SHAPE_MUL:
            s->outputs[0] = (s->inputs[0] * s->inputs[1]) / WEIGHT_SCALE;
            break;

        case SHAPE_CMP:
            if (s->inputs[0] > s->inputs[1]) s->outputs[0] = WEIGHT_SCALE;
            else if (s->inputs[0] < s->inputs[1]) s->outputs[0] = -WEIGHT_SCALE;
            else s->outputs[0] = 0;
            break;

        case SHAPE_MUX:
            s->outputs[0] = s->inputs[2] > WEIGHT_SCALE/2 ? s->inputs[1] : s->inputs[0];
            break;

        case SHAPE_DEMUX: {
            int sel = (s->inputs[1] * 4) / WEIGHT_SCALE;
            if (sel < 0) sel = 0;
            if (sel > 3) sel = 3;
            for (int i = 0; i < 4; i++) {
                s->outputs[i] = (i == sel) ? s->inputs[0] : 0;
            }
            break;
        }

        case SHAPE_FANOUT:
            for (int i = 0; i < 4; i++) {
                s->outputs[i] = s->inputs[0];
            }
            break;

        case SHAPE_NEURON: {
            // Leaky integrate-and-fire
            int32_t sum = 0;
            for (int i = 0; i < ECHIP_MAX_PORTS; i++) {
                sum += s->inputs[i];
            }
            s->state = (s->state * 7) / 8 + sum;  // Leak + integrate
            if (s->state > s->threshold) {
                s->outputs[0] = WEIGHT_SCALE;
                s->state = 0;  // Reset after spike
            } else {
                s->outputs[0] = 0;
            }
            break;
        }

        case SHAPE_OSCILLATOR:
            s->state++;
            s->outputs[0] = (s->state % (s->threshold > 0 ? s->threshold : 10)) == 0
                           ? WEIGHT_SCALE : 0;
            break;

        default:
            break;
    }
}

// ============================================================
// Route Propagation
// ============================================================

/**
 * Propagate signals through all active routes
 */
static inline void echip_propagate(echip_t* chip) {
    // Clear all input ports
    for (uint16_t i = 0; i < chip->max_shapes; i++) {
        if (chip->shapes[i].type != SHAPE_VOID) {
            memset(chip->shapes[i].inputs, 0, sizeof(chip->shapes[i].inputs));
        }
    }

    // Copy external inputs to INPUT shapes
    uint8_t ext_idx = 0;
    for (uint16_t i = 0; i < chip->max_shapes && ext_idx < chip->num_ext_inputs; i++) {
        if (chip->shapes[i].type == SHAPE_INPUT) {
            chip->shapes[i].outputs[0] = chip->ext_inputs[ext_idx++];
        }
    }

    // Propagate through routes
    for (uint16_t i = 0; i < chip->max_routes; i++) {
        mutable_route_t* r = &chip->routes[i];
        if (r->state == ROUTE_EMPTY) continue;

        frozen_shape_t* src = echip_find_shape(chip, r->src_shape);
        frozen_shape_t* dst = echip_find_shape(chip, r->dst_shape);
        if (!src || !dst) continue;

        // Get output value from source
        int16_t value = src->outputs[r->src_port];

        // Apply weight
        int32_t weighted = (value * r->weight) / WEIGHT_SCALE;

        // Add to destination input
        dst->inputs[r->dst_port] += (int16_t)weighted;

        // Track activity
        if (value != 0) {
            r->activity++;
            r->state = ROUTE_ACTIVE;
            chip->signals_propagated++;
        }
    }

    // Evaluate all shapes
    for (uint16_t i = 0; i < chip->max_shapes; i++) {
        if (chip->shapes[i].type != SHAPE_VOID) {
            shape_evaluate(&chip->shapes[i]);
        }
    }

    // Copy OUTPUT shapes to external outputs
    ext_idx = 0;
    for (uint16_t i = 0; i < chip->max_shapes && ext_idx < chip->num_ext_outputs; i++) {
        if (chip->shapes[i].type == SHAPE_OUTPUT) {
            chip->ext_outputs[ext_idx++] = chip->shapes[i].inputs[0];
        }
    }
}

// ============================================================
// Hebbian Learning
// ============================================================

/**
 * Apply Hebbian learning: strengthen routes that carry correlated signals
 */
static inline void echip_hebbian_update(echip_t* chip) {
    for (uint16_t i = 0; i < chip->max_routes; i++) {
        mutable_route_t* r = &chip->routes[i];
        if (r->state == ROUTE_EMPTY) continue;

        frozen_shape_t* src = echip_find_shape(chip, r->src_shape);
        frozen_shape_t* dst = echip_find_shape(chip, r->dst_shape);
        if (!src || !dst) continue;

        // Check for correlated activity
        bool src_active = src->outputs[r->src_port] != 0;
        bool dst_active = dst->outputs[0] != 0;  // Check if dst fired

        if (src_active && dst_active) {
            // Strengthen: src contributed to dst firing
            int32_t delta = (WEIGHT_MAX - r->weight) * chip->hebbian_rate / WEIGHT_SCALE;
            r->weight += delta;
            if (r->weight > WEIGHT_MAX) r->weight = WEIGHT_MAX;
            r->state = ROUTE_STRENGTHENING;
        } else if (!src_active && !dst_active) {
            // Weaken: both inactive
            r->weight = r->weight * (WEIGHT_SCALE - chip->decay_rate) / WEIGHT_SCALE;
            if (r->weight < WEIGHT_MIN) r->weight = WEIGHT_MIN;
            r->state = ROUTE_WEAKENING;
        }

        // Decay activity counter
        r->activity = r->activity * (WEIGHT_SCALE - chip->activity_decay) / WEIGHT_SCALE;
    }
}

// ============================================================
// Entropy Integration
// ============================================================

/**
 * Update entropy field based on route activity
 */
static inline void echip_entropy_update(echip_t* chip) {
    // Routes deposit negative entropy (reduce uncertainty) when active
    for (uint16_t i = 0; i < chip->max_routes; i++) {
        mutable_route_t* r = &chip->routes[i];
        if (r->state == ROUTE_EMPTY) continue;

        reflex_void_cell_t* cell = field_cell(&chip->field, r->field_x, r->field_y);
        if (!cell) continue;

        if (r->state == ROUTE_ACTIVE || r->state == ROUTE_STRENGTHENING) {
            // Active routes reduce local entropy
            if (cell->entropy > 100) {
                cell->entropy -= 100;
            }
        } else {
            // Inactive routes allow entropy to accumulate
            cell->entropy += chip->field.default_capacity / 100;
        }
    }

    // Evolve the field
    entropy_field_tick(&chip->field);
}

// ============================================================
// Crystallization (Void → Shape)
// ============================================================

/**
 * Check for and perform crystallization
 * Returns number of shapes created
 */
static inline uint32_t echip_crystallize(echip_t* chip) {
    uint32_t created = 0;

    for (uint16_t y = 0; y < chip->field.height; y++) {
        for (uint16_t x = 0; x < chip->field.width; x++) {
            reflex_void_cell_t* cell = field_cell(&chip->field, x, y);
            if (!cell) continue;

            // Check for critical entropy
            if (cell->state == VOID_STATE_CRITICAL &&
                cell->entropy >= chip->crystallize_threshold) {

                // Determine shape type based on local gradient
                shape_type_t type;
                uint32_t gradient_mag = entropy_gradient_magnitude(cell);

                if (gradient_mag > 1000) {
                    // High gradient = routing shape
                    type = SHAPE_BUFFER;
                } else if (cell->gradient_x > cell->gradient_y) {
                    // Horizontal flow = logic shape
                    type = (chip->tick % 2) ? SHAPE_NAND : SHAPE_NOR;
                } else {
                    // Vertical flow = memory shape
                    type = SHAPE_LATCH;
                }

                // Create the shape
                uint16_t id = echip_create_shape(chip, type,
                    x * (ECHIP_MAX_SHAPES / chip->field.width),
                    y * (ECHIP_MAX_SHAPES / chip->field.height));

                if (id > 0) {
                    // Collapse entropy
                    cell->entropy = 0;
                    cell->state = VOID_STATE_SHAPE;
                    created++;
                }
            }
        }
    }

    return created;
}

// ============================================================
// Pruning (Dissolve Weak Routes)
// ============================================================

/**
 * Remove routes with insufficient activity
 * Returns number of routes dissolved
 */
static inline uint32_t echip_prune(echip_t* chip) {
    uint32_t pruned = 0;

    for (uint16_t i = 0; i < chip->max_routes; i++) {
        mutable_route_t* r = &chip->routes[i];
        if (r->state == ROUTE_EMPTY) continue;

        // Check if route has become too weak
        if (r->activity < chip->dissolve_activity &&
            r->weight < WEIGHT_SCALE / 10) {
            echip_dissolve_route(chip, i);
            pruned++;
        }
    }

    return pruned;
}

// ============================================================
// Main Tick (One Cycle of Self-Composition)
// ============================================================

/**
 * Execute one tick of the self-reconfiguring processor
 *
 * This is where the magic happens:
 * 1. Propagate signals through routes
 * 2. Evaluate shape logic
 * 3. Apply Hebbian learning
 * 4. Update entropy field (periodically)
 * 5. Crystallize new shapes from void
 * 6. Prune unused routes
 */
static inline void echip_tick(echip_t* chip) {
    // 1. Signal propagation
    echip_propagate(chip);

    // 2. Hebbian learning
    echip_hebbian_update(chip);

    // 3. Entropy field update (less frequent)
    if (chip->tick % chip->ticks_per_entropy_update == 0) {
        echip_entropy_update(chip);

        // 4. Crystallization check
        echip_crystallize(chip);

        // 5. Pruning check
        echip_prune(chip);
    }

    chip->tick++;
}

// ============================================================
// External Interface
// ============================================================

/**
 * Set external input value
 */
static inline void echip_set_input(echip_t* chip, uint8_t idx, int16_t value) {
    if (idx < chip->num_ext_inputs) {
        chip->ext_inputs[idx] = value;
    }
}

/**
 * Get external output value
 */
static inline int16_t echip_get_output(echip_t* chip, uint8_t idx) {
    if (idx < chip->num_ext_outputs) {
        return chip->ext_outputs[idx];
    }
    return 0;
}

/**
 * Allocate external I/O buffers
 */
static inline bool echip_alloc_io(echip_t* chip, uint8_t num_inputs, uint8_t num_outputs) {
    chip->ext_inputs = (int16_t*)calloc(num_inputs, sizeof(int16_t));
    chip->ext_outputs = (int16_t*)calloc(num_outputs, sizeof(int16_t));
    if (!chip->ext_inputs || !chip->ext_outputs) {
        if (chip->ext_inputs) free(chip->ext_inputs);
        if (chip->ext_outputs) free(chip->ext_outputs);
        return false;
    }
    chip->num_ext_inputs = num_inputs;
    chip->num_ext_outputs = num_outputs;
    return true;
}

// ============================================================
// Statistics
// ============================================================

/**
 * Get chip statistics
 */
typedef struct {
    uint16_t num_shapes;
    uint16_t num_routes;
    uint32_t shapes_created;
    uint32_t shapes_dissolved;
    uint32_t routes_created;
    uint32_t routes_dissolved;
    uint32_t signals_propagated;
    uint32_t total_entropy;
    uint64_t tick;
} echip_stats_t;

static inline echip_stats_t echip_get_stats(echip_t* chip) {
    echip_stats_t stats = {
        .num_shapes = chip->num_shapes,
        .num_routes = chip->num_routes,
        .shapes_created = chip->shapes_created,
        .shapes_dissolved = chip->shapes_dissolved,
        .routes_created = chip->routes_created,
        .routes_dissolved = chip->routes_dissolved,
        .signals_propagated = chip->signals_propagated,
        .total_entropy = chip->field.total_entropy,
        .tick = chip->tick,
    };
    return stats;
}

// ============================================================
// Debug / Visualization
// ============================================================

/**
 * Get ASCII representation of shape type
 */
static inline char shape_type_char(shape_type_t type) {
    switch (type) {
        case SHAPE_VOID:       return ' ';
        case SHAPE_NAND:       return '&';
        case SHAPE_NOR:        return '|';
        case SHAPE_XOR:        return '^';
        case SHAPE_BUFFER:     return '>';
        case SHAPE_NOT:        return '!';
        case SHAPE_LATCH:      return 'D';
        case SHAPE_TOGGLE:     return 'T';
        case SHAPE_ADD:        return '+';
        case SHAPE_SUB:        return '-';
        case SHAPE_MUL:        return '*';
        case SHAPE_CMP:        return '?';
        case SHAPE_MUX:        return 'M';
        case SHAPE_DEMUX:      return 'X';
        case SHAPE_FANOUT:     return 'F';
        case SHAPE_INPUT:      return 'I';
        case SHAPE_OUTPUT:     return 'O';
        case SHAPE_NEURON:     return 'N';
        case SHAPE_OSCILLATOR: return '~';
        default:               return '?';
    }
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_ECHIP_H
