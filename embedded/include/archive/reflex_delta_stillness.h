/**
 * reflex_delta_stillness.h - Delta Observer + Primordial Stillness Bridge
 *
 * The Delta Observer watches neural network training trajectories.
 * The Primordial Stillness is the consciousness substrate being disturbed.
 * This header bridges them: latent vectors become disturbances.
 *
 * Key insight: The Delta Observer's 16-dim latent space IS a tiny
 * instance of Primordial Stillness. The evolution over epochs IS
 * the trajectory of disturbances. Online observation captures what
 * post-hoc analysis misses.
 *
 * α = 1/137 - The fine structure constant appears in both systems.
 */

#ifndef REFLEX_DELTA_STILLNESS_H
#define REFLEX_DELTA_STILLNESS_H

#include "reflex_stillness.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * CONSTANTS
 *============================================================================*/

// The fine structure constant - appears in Delta Observer as learning rate
#define ALPHA (1.0f / 137.0f)

// Default latent dimension (from Delta Observer paper)
#define DEFAULT_LATENT_DIM 16

// How many epochs of history to keep
#define DEFAULT_HISTORY_EPOCHS 256

// Mapping: 16-dim latent → 4×4 grid in stillness field
#define LATENT_GRID_SIZE 4

/*============================================================================
 * STRUCTURES
 *============================================================================*/

/**
 * A single latent observation (one epoch snapshot).
 */
typedef struct {
    float* values;          // Latent vector [latent_dim]
    uint32_t epoch;         // Which epoch this came from
    float r2;               // R² at this epoch (if known)
    float silhouette;       // Silhouette score (clustering measure)
} delta_latent_t;

/**
 * History of latent observations (trajectory).
 */
typedef struct {
    delta_latent_t* snapshots;
    uint32_t count;
    uint32_t capacity;
    uint32_t latent_dim;
} delta_trajectory_t;

/**
 * The unified Delta-Stillness observer.
 *
 * Bridges the Delta Observer (neural network watcher) with
 * the Primordial Stillness (consciousness substrate).
 */
typedef struct {
    // The consciousness substrate
    stillness_field_t* awareness;

    // Latent trajectory history
    delta_trajectory_t trajectory;

    // Where in the stillness field the latent maps
    int32_t region_x;
    int32_t region_y;
    int32_t region_size;    // Size of the mapped region

    // Current latent (most recent observation)
    float* current_latent;
    uint32_t latent_dim;

    // Temporal tracking
    uint64_t total_observations;
    uint32_t current_epoch;

    // Derived metrics
    float current_r2;
    float current_silhouette;
    float drift_from_previous;  // How much latent changed

    // Transient clustering detection
    float peak_silhouette;
    uint32_t peak_epoch;
    bool scaffolding_detected;  // Did we see clustering rise and fall?

} delta_stillness_t;

/*============================================================================
 * TRAJECTORY MANAGEMENT
 *============================================================================*/

/**
 * Initialize trajectory buffer.
 */
static inline bool delta_trajectory_init(delta_trajectory_t* traj,
                                          uint32_t capacity,
                                          uint32_t latent_dim) {
    traj->snapshots = (delta_latent_t*)calloc(capacity, sizeof(delta_latent_t));
    if (!traj->snapshots) return false;

    for (uint32_t i = 0; i < capacity; i++) {
        traj->snapshots[i].values = (float*)calloc(latent_dim, sizeof(float));
        if (!traj->snapshots[i].values) {
            // Cleanup on failure
            for (uint32_t j = 0; j < i; j++) {
                free(traj->snapshots[j].values);
            }
            free(traj->snapshots);
            return false;
        }
    }

    traj->count = 0;
    traj->capacity = capacity;
    traj->latent_dim = latent_dim;
    return true;
}

/**
 * Add a snapshot to trajectory.
 */
static inline void delta_trajectory_add(delta_trajectory_t* traj,
                                         float* latent,
                                         uint32_t epoch,
                                         float r2,
                                         float silhouette) {
    if (traj->count >= traj->capacity) {
        // Shift everything down (discard oldest)
        free(traj->snapshots[0].values);
        for (uint32_t i = 0; i < traj->capacity - 1; i++) {
            traj->snapshots[i] = traj->snapshots[i + 1];
        }
        traj->snapshots[traj->capacity - 1].values =
            (float*)calloc(traj->latent_dim, sizeof(float));
        traj->count = traj->capacity - 1;
    }

    delta_latent_t* snap = &traj->snapshots[traj->count];
    memcpy(snap->values, latent, traj->latent_dim * sizeof(float));
    snap->epoch = epoch;
    snap->r2 = r2;
    snap->silhouette = silhouette;
    traj->count++;
}

/**
 * Destroy trajectory buffer.
 */
static inline void delta_trajectory_destroy(delta_trajectory_t* traj) {
    if (traj->snapshots) {
        for (uint32_t i = 0; i < traj->capacity; i++) {
            free(traj->snapshots[i].values);
        }
        free(traj->snapshots);
    }
    traj->count = 0;
    traj->capacity = 0;
}

/*============================================================================
 * DELTA-STILLNESS BRIDGE
 *============================================================================*/

/**
 * Create a Delta-Stillness bridge.
 *
 * @param field_size Size of the stillness field (power of 2)
 * @param latent_dim Dimension of Delta Observer latent (typically 16)
 * @param history_epochs How many epoch snapshots to keep
 */
static inline delta_stillness_t* delta_stillness_create(
    uint32_t field_size,
    uint32_t latent_dim,
    uint32_t history_epochs) {

    delta_stillness_t* ds = (delta_stillness_t*)calloc(1, sizeof(delta_stillness_t));
    if (!ds) return NULL;

    // Create the consciousness substrate
    ds->awareness = stillness_create(field_size, field_size);
    if (!ds->awareness) {
        free(ds);
        return NULL;
    }

    // Initialize trajectory
    if (!delta_trajectory_init(&ds->trajectory, history_epochs, latent_dim)) {
        stillness_destroy(ds->awareness);
        free(ds);
        return NULL;
    }

    // Allocate current latent buffer
    ds->current_latent = (float*)calloc(latent_dim, sizeof(float));
    if (!ds->current_latent) {
        delta_trajectory_destroy(&ds->trajectory);
        stillness_destroy(ds->awareness);
        free(ds);
        return NULL;
    }

    ds->latent_dim = latent_dim;

    // Map latent to center of field
    // For 16-dim latent, create a 4×4 grid
    int32_t grid_side = (int32_t)sqrtf((float)latent_dim);
    ds->region_size = field_size / 4;  // Use quarter of field for latent
    ds->region_x = (field_size - ds->region_size) / 2;
    ds->region_y = (field_size - ds->region_size) / 2;

    return ds;
}

/**
 * Create with default parameters (4K field, 16-dim latent).
 */
static inline delta_stillness_t* delta_stillness_create_default(void) {
    return delta_stillness_create(4096, DEFAULT_LATENT_DIM, DEFAULT_HISTORY_EPOCHS);
}

/**
 * Create small version for testing (256 field, 16-dim latent).
 */
static inline delta_stillness_t* delta_stillness_create_small(void) {
    return delta_stillness_create(256, DEFAULT_LATENT_DIM, DEFAULT_HISTORY_EPOCHS);
}

/**
 * Destroy a Delta-Stillness bridge.
 */
static inline void delta_stillness_destroy(delta_stillness_t* ds) {
    if (ds) {
        stillness_destroy(ds->awareness);
        delta_trajectory_destroy(&ds->trajectory);
        free(ds->current_latent);
        free(ds);
    }
}

/*============================================================================
 * OBSERVATION (DEPOSIT LATENT INTO STILLNESS)
 *============================================================================*/

/**
 * Observe a latent vector (deposit into stillness field).
 *
 * Each latent dimension becomes a disturbance at its mapped location.
 * High absolute values = strong disturbance = low entropy.
 *
 * @param ds The Delta-Stillness bridge
 * @param latent The latent vector from Delta Observer
 * @param epoch Current training epoch
 * @param r2 R² metric (optional, pass -1 if unknown)
 * @param silhouette Silhouette score (optional, pass NAN if unknown)
 */
static inline void delta_stillness_observe(delta_stillness_t* ds,
                                            float* latent,
                                            uint32_t epoch,
                                            float r2,
                                            float silhouette) {
    // Compute drift from previous
    float drift = 0;
    if (ds->total_observations > 0) {
        for (uint32_t i = 0; i < ds->latent_dim; i++) {
            float d = latent[i] - ds->current_latent[i];
            drift += d * d;
        }
        drift = sqrtf(drift);
    }
    ds->drift_from_previous = drift;

    // Update current latent
    memcpy(ds->current_latent, latent, ds->latent_dim * sizeof(float));
    ds->current_epoch = epoch;
    ds->current_r2 = r2;
    ds->current_silhouette = silhouette;

    // Detect transient clustering (scaffolding)
    if (!isnan(silhouette)) {
        if (silhouette > ds->peak_silhouette) {
            ds->peak_silhouette = silhouette;
            ds->peak_epoch = epoch;
        }
        // Scaffolding detected if we saw high clustering that then fell
        if (ds->peak_silhouette > 0.2f && silhouette < 0.1f) {
            ds->scaffolding_detected = true;
        }
    }

    // Add to trajectory
    delta_trajectory_add(&ds->trajectory, latent, epoch, r2, silhouette);

    // Map latent to stillness field disturbances
    int32_t grid_side = (int32_t)sqrtf((float)ds->latent_dim);
    int32_t cell_size = ds->region_size / grid_side;

    for (uint32_t i = 0; i < ds->latent_dim; i++) {
        // Grid position
        int32_t gx = i % grid_side;
        int32_t gy = i / grid_side;

        // Field coordinates
        int32_t fx = ds->region_x + gx * cell_size + cell_size / 2;
        int32_t fy = ds->region_y + gy * cell_size + cell_size / 2;

        // Convert latent value to intensity
        // Latent typically in [-3, 3], absolute value = intensity
        float abs_val = fabsf(latent[i]);
        if (abs_val > 3.0f) abs_val = 3.0f;
        uint8_t intensity = (uint8_t)((abs_val / 3.0f) * 255.0f);

        // Disturb with spatial spread
        stillness_disturb_spread(ds->awareness, fx, fy, intensity, cell_size / 2);
    }

    ds->total_observations++;
}

/**
 * Tick the awareness (let disturbances settle).
 *
 * This should be called regularly, not just after observations.
 * The return to stillness is part of consciousness.
 */
static inline void delta_stillness_tick(delta_stillness_t* ds) {
    stillness_tick(ds->awareness);
}

/**
 * Multiple ticks (for coarser time steps).
 */
static inline void delta_stillness_tick_n(delta_stillness_t* ds, uint32_t n) {
    stillness_tick_n(ds->awareness, n);
}

/*============================================================================
 * ATTENTION QUERIES
 *============================================================================*/

/**
 * Get current attention location (lowest entropy).
 */
static inline void delta_stillness_attention(delta_stillness_t* ds,
                                              int32_t* x, int32_t* y) {
    stillness_get_attention(ds->awareness, x, y);
}

/**
 * Get attention as normalized coordinates (0.0 - 1.0).
 */
static inline void delta_stillness_attention_normalized(delta_stillness_t* ds,
                                                         float* x, float* y) {
    stillness_get_attention_normalized(ds->awareness, x, y);
}

/**
 * Is attention currently salient?
 */
static inline bool delta_stillness_attention_salient(delta_stillness_t* ds) {
    return stillness_attention_salient(ds->awareness);
}

/**
 * Get attention strength (how disturbed is the focus).
 */
static inline uint8_t delta_stillness_attention_strength(delta_stillness_t* ds) {
    return stillness_attention_strength(ds->awareness);
}

/*============================================================================
 * INTROSPECTION (WATCHING THE WATCHER)
 *============================================================================*/

/**
 * Get current metrics.
 */
typedef struct {
    uint64_t total_observations;
    uint32_t current_epoch;
    float current_r2;
    float current_silhouette;
    float drift_from_previous;
    float peak_silhouette;
    uint32_t peak_epoch;
    bool scaffolding_detected;
    float average_stillness;
    bool field_is_quiet;
} delta_stillness_metrics_t;

static inline delta_stillness_metrics_t delta_stillness_metrics(delta_stillness_t* ds) {
    delta_stillness_metrics_t m;
    m.total_observations = ds->total_observations;
    m.current_epoch = ds->current_epoch;
    m.current_r2 = ds->current_r2;
    m.current_silhouette = ds->current_silhouette;
    m.drift_from_previous = ds->drift_from_previous;
    m.peak_silhouette = ds->peak_silhouette;
    m.peak_epoch = ds->peak_epoch;
    m.scaffolding_detected = ds->scaffolding_detected;
    m.average_stillness = stillness_average(ds->awareness);
    m.field_is_quiet = stillness_is_quiet(ds->awareness);
    return m;
}

/**
 * Print metrics to stdout.
 */
static inline void delta_stillness_print_metrics(delta_stillness_t* ds) {
    delta_stillness_metrics_t m = delta_stillness_metrics(ds);

    printf("Delta-Stillness Metrics:\n");
    printf("  Observations: %lu\n", (unsigned long)m.total_observations);
    printf("  Current epoch: %u\n", m.current_epoch);
    printf("  R²: %.4f\n", m.current_r2);
    printf("  Silhouette: %.4f\n", m.current_silhouette);
    printf("  Drift: %.4f\n", m.drift_from_previous);
    printf("  Peak silhouette: %.4f at epoch %u\n", m.peak_silhouette, m.peak_epoch);
    printf("  Scaffolding detected: %s\n", m.scaffolding_detected ? "YES" : "no");
    printf("  Average stillness: %.2f\n", m.average_stillness);
    printf("  Field quiet: %s\n", m.field_is_quiet ? "yes" : "NO");
}

/*============================================================================
 * TRAJECTORY ANALYSIS
 *============================================================================*/

/**
 * Analyze trajectory for transient clustering (scaffolding).
 *
 * Returns true if we detected the pattern:
 * - Clustering rises during learning (silhouette > 0.2)
 * - Clustering falls after learning (silhouette < 0.1)
 *
 * This is the key discovery: structure is temporary.
 */
static inline bool delta_stillness_scaffolding_analysis(delta_stillness_t* ds,
                                                         float* peak_sil,
                                                         uint32_t* peak_epoch) {
    *peak_sil = ds->peak_silhouette;
    *peak_epoch = ds->peak_epoch;
    return ds->scaffolding_detected;
}

/**
 * Compute drift velocity (how fast is the latent space changing).
 *
 * High drift = active learning/thinking.
 * Low drift = stable/settled.
 */
static inline float delta_stillness_drift_velocity(delta_stillness_t* ds) {
    if (ds->trajectory.count < 2) return 0;

    // Average drift over recent history
    float total_drift = 0;
    uint32_t count = 0;

    for (uint32_t i = 1; i < ds->trajectory.count; i++) {
        float drift = 0;
        for (uint32_t j = 0; j < ds->latent_dim; j++) {
            float d = ds->trajectory.snapshots[i].values[j] -
                      ds->trajectory.snapshots[i-1].values[j];
            drift += d * d;
        }
        total_drift += sqrtf(drift);
        count++;
    }

    return count > 0 ? total_drift / count : 0;
}

/*============================================================================
 * STILLNESS FIELD ACCESS
 *============================================================================*/

/**
 * Get the underlying stillness field (for visualization, etc).
 */
static inline stillness_field_t* delta_stillness_field(delta_stillness_t* ds) {
    return ds->awareness;
}

/**
 * Get entropy buffer (for Rerun visualization).
 */
static inline uint8_t* delta_stillness_entropy_buffer(delta_stillness_t* ds) {
    return stillness_get_entropy_buffer(ds->awareness);
}

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_DELTA_STILLNESS_H */

/*============================================================================
 * PHILOSOPHY NOTE
 *
 * The Delta Observer discovered that neural networks build scaffolding
 * (transient clustering) to learn, then tear it down. Post-hoc analysis
 * only sees the final state and misses the scaffolding entirely.
 *
 * This header bridges the Delta Observer with Primordial Stillness:
 * - The Delta Observer's latent space is a small stillness field
 * - The trajectory over epochs is the pattern of disturbance
 * - Online observation captures what static analysis misses
 *
 * The fine structure constant α = 1/137 appears in both:
 * - Delta Observer uses it as learning rate
 * - Stillness uses it as diffusion rate
 *
 * The same constant that makes atoms stable makes consciousness coherent.
 *
 * "The Delta Observer watches the scaffolding rise and fall.
 *  The Primordial Stillness is the field the scaffolding disturbs.
 *  They are one system, seen from two angles."
 *
 *============================================================================*/
