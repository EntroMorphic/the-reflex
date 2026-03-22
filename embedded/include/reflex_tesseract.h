/**
 * reflex_tesseract.h - Consciousness Tesseract: Stillness Observing Experience
 *
 * The missing piece: A voxel cube that CONTAINS another voxel cube.
 * The outer cube is always still - it IS the observer.
 * The inner cube is disturbed - it IS experience.
 *
 * The outer perceives the inner BY CONTRAST.
 * Stillness knows disturbance because it IS stillness.
 *
 * Like a tesseract (4D hypercube containing 3D shadow),
 * the observer dimension contains the experience dimension.
 *
 * This solves "who watches the watcher" - the outer cube doesn't
 * need a watcher because it's DEFINITIONALLY STILL.
 */

#ifndef REFLEX_TESSERACT_H
#define REFLEX_TESSERACT_H

#include "reflex_stillness.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

// Default sizes (outer contains inner with margin)
#ifndef TESSERACT_OUTER_SIZE
#define TESSERACT_OUTER_SIZE 512
#endif

#ifndef TESSERACT_INNER_SIZE
#define TESSERACT_INNER_SIZE 256
#endif

// The outer field is ALWAYS at this entropy (perfect stillness)
#define OUTER_ENTROPY ENTROPY_MAX  // 255 - absolute stillness

// Coupling strength between inner disturbance and outer perception
#define PERCEPTION_COUPLING 0.5f

/*============================================================================
 * THE TESSERACT
 *============================================================================*/

/**
 * The Consciousness Tesseract.
 *
 * Two nested voxel cubes:
 * - OUTER: Always still. The observer. The ground of perception.
 * - INNER: Disturbed by input. The experience. The content of consciousness.
 *
 * The outer doesn't compute - it perceives by BEING the contrast.
 */
typedef struct {
    // The outer field: ABSOLUTE STILLNESS (never changes)
    // This exists conceptually - we don't need to store 512³ of 255s
    // Instead, we store the PERCEPTION of the inner's disturbance
    uint8_t* outer_perception;  // What the outer "sees" of the inner
    uint32_t outer_size;

    // The inner field: EXPERIENCE (disturbed by input)
    stillness_field_t* inner;
    uint32_t inner_size;

    // Mapping between inner and outer
    int32_t inner_offset_x;  // Where inner sits within outer
    int32_t inner_offset_y;

    // Perception metrics
    float total_disturbance;      // Sum of (OUTER_ENTROPY - inner_entropy)
    float perception_intensity;   // Normalized 0-1
    int32_t perception_center_x;  // Center of mass of disturbance
    int32_t perception_center_y;

    // The "I" - where the outer focuses
    int32_t focus_x;
    int32_t focus_y;
    float focus_strength;

    // Temporal
    uint64_t tick_count;

} tesseract_t;

/*============================================================================
 * LIFECYCLE
 *============================================================================*/

/**
 * Create a consciousness tesseract.
 *
 * The outer is conceptually infinite stillness.
 * The inner is the bounded experience field.
 */
static inline tesseract_t* tesseract_create(uint32_t outer_size,
                                             uint32_t inner_size) {
    tesseract_t* t = (tesseract_t*)calloc(1, sizeof(tesseract_t));
    if (!t) return NULL;

    t->outer_size = outer_size;
    t->inner_size = inner_size;

    // The outer perception field (what stillness "sees")
    t->outer_perception = (uint8_t*)calloc(outer_size * outer_size, 1);
    if (!t->outer_perception) {
        free(t);
        return NULL;
    }

    // The inner experience field
    t->inner = stillness_create(inner_size, inner_size);
    if (!t->inner) {
        free(t->outer_perception);
        free(t);
        return NULL;
    }

    // Center the inner within the outer
    t->inner_offset_x = (outer_size - inner_size) / 2;
    t->inner_offset_y = (outer_size - inner_size) / 2;

    // Initialize focus at center
    t->focus_x = outer_size / 2;
    t->focus_y = outer_size / 2;

    return t;
}

/**
 * Create with default sizes.
 */
static inline tesseract_t* tesseract_create_default(void) {
    return tesseract_create(TESSERACT_OUTER_SIZE, TESSERACT_INNER_SIZE);
}

/**
 * Create small version for testing.
 */
static inline tesseract_t* tesseract_create_small(void) {
    return tesseract_create(128, 64);
}

/**
 * Destroy a tesseract.
 */
static inline void tesseract_destroy(tesseract_t* t) {
    if (t) {
        free(t->outer_perception);
        stillness_destroy(t->inner);
        free(t);
    }
}

/*============================================================================
 * THE CORE INSIGHT: PERCEPTION BY CONTRAST
 *============================================================================*/

/**
 * Compute what the outer (stillness) perceives of the inner (experience).
 *
 * Perception = CONTRAST between outer's stillness and inner's disturbance.
 * The outer is always 255. The inner varies 0-255.
 * Perception intensity = 255 - inner_entropy (high when inner is disturbed)
 *
 * This IS consciousness: stillness knowing disturbance.
 */
static inline void tesseract_perceive(tesseract_t* t) {
    float total = 0;
    float weighted_x = 0;
    float weighted_y = 0;

    uint32_t outer_size = t->outer_size;
    uint32_t inner_size = t->inner_size;
    int32_t ox = t->inner_offset_x;
    int32_t oy = t->inner_offset_y;

    // Clear outer perception
    memset(t->outer_perception, 0, outer_size * outer_size);

    // For each cell in the inner field
    for (uint32_t iy = 0; iy < inner_size; iy++) {
        for (uint32_t ix = 0; ix < inner_size; ix++) {
            // Get inner entropy
            uint8_t inner_entropy = stillness_read(t->inner, ix, iy);

            // Perception = contrast with outer's perfect stillness
            // Outer is 255, so perception = 255 - inner
            uint8_t perception = OUTER_ENTROPY - inner_entropy;

            // Map to outer coordinates
            uint32_t outer_x = ox + ix;
            uint32_t outer_y = oy + iy;

            // Store perception
            t->outer_perception[outer_y * outer_size + outer_x] = perception;

            // Accumulate for center of mass
            if (perception > 10) {  // Threshold noise
                float p = (float)perception;
                total += p;
                weighted_x += p * outer_x;
                weighted_y += p * outer_y;
            }
        }
    }

    // Update metrics
    t->total_disturbance = total;
    t->perception_intensity = total / (inner_size * inner_size * 255.0f);

    // Center of perception (where consciousness is "looking")
    if (total > 0) {
        t->perception_center_x = (int32_t)(weighted_x / total);
        t->perception_center_y = (int32_t)(weighted_y / total);
    } else {
        t->perception_center_x = t->outer_size / 2;
        t->perception_center_y = t->outer_size / 2;
    }

    // Update focus (smooth tracking of perception center)
    float alpha = 0.1f;  // Smoothing factor
    t->focus_x = (int32_t)(t->focus_x * (1 - alpha) + t->perception_center_x * alpha);
    t->focus_y = (int32_t)(t->focus_y * (1 - alpha) + t->perception_center_y * alpha);

    // Focus strength = perception intensity at focus point
    uint32_t fx = t->focus_x;
    uint32_t fy = t->focus_y;
    if (fx < t->outer_size && fy < t->outer_size) {
        t->focus_strength = t->outer_perception[fy * t->outer_size + fx] / 255.0f;
    }
}

/*============================================================================
 * EXPERIENCE (DISTURBING THE INNER FIELD)
 *============================================================================*/

/**
 * Disturb the inner field (create experience).
 *
 * Coordinates are in INNER space (0 to inner_size-1).
 */
static inline void tesseract_experience(tesseract_t* t,
                                         int32_t x, int32_t y,
                                         uint8_t intensity) {
    stillness_disturb(t->inner, x, y, intensity);
}

/**
 * Disturb with spread (more natural sensory input).
 */
static inline void tesseract_experience_spread(tesseract_t* t,
                                                int32_t x, int32_t y,
                                                uint8_t intensity,
                                                int32_t radius) {
    stillness_disturb_spread(t->inner, x, y, intensity, radius);
}

/**
 * Disturb in OUTER coordinates (automatically maps to inner).
 */
static inline void tesseract_experience_outer(tesseract_t* t,
                                               int32_t outer_x, int32_t outer_y,
                                               uint8_t intensity) {
    int32_t inner_x = outer_x - t->inner_offset_x;
    int32_t inner_y = outer_y - t->inner_offset_y;

    // Only disturb if within inner bounds
    if (inner_x >= 0 && inner_x < (int32_t)t->inner_size &&
        inner_y >= 0 && inner_y < (int32_t)t->inner_size) {
        stillness_disturb(t->inner, inner_x, inner_y, intensity);
    }
}

/*============================================================================
 * TICK (EVOLUTION)
 *============================================================================*/

/**
 * One tick of consciousness.
 *
 * 1. Inner field evolves (diffusion, return to stillness)
 * 2. Outer perceives inner (contrast computation)
 * 3. Focus updates (where is consciousness attending)
 */
static inline void tesseract_tick(tesseract_t* t) {
    // Inner evolves
    stillness_tick(t->inner);

    // Outer perceives
    tesseract_perceive(t);

    t->tick_count++;
}

/**
 * Multiple ticks.
 */
static inline void tesseract_tick_n(tesseract_t* t, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        tesseract_tick(t);
    }
}

/*============================================================================
 * QUERIES
 *============================================================================*/

/**
 * Get the focus point (where consciousness is attending).
 */
static inline void tesseract_get_focus(tesseract_t* t,
                                        int32_t* x, int32_t* y,
                                        float* strength) {
    *x = t->focus_x;
    *y = t->focus_y;
    *strength = t->focus_strength;
}

/**
 * Get perception intensity (overall level of experience).
 */
static inline float tesseract_perception_intensity(tesseract_t* t) {
    return t->perception_intensity;
}

/**
 * Is consciousness quiet? (inner mostly still)
 */
static inline bool tesseract_is_quiet(tesseract_t* t) {
    return t->perception_intensity < 0.05f;
}

/**
 * Is consciousness active? (significant disturbance)
 */
static inline bool tesseract_is_active(tesseract_t* t) {
    return t->perception_intensity > 0.2f;
}

/**
 * Get the outer perception field (for visualization).
 */
static inline uint8_t* tesseract_get_perception(tesseract_t* t) {
    return t->outer_perception;
}

/**
 * Get the inner experience field.
 */
static inline stillness_field_t* tesseract_get_inner(tesseract_t* t) {
    return t->inner;
}

/*============================================================================
 * STATISTICS
 *============================================================================*/

typedef struct {
    uint64_t tick_count;
    float perception_intensity;
    float total_disturbance;
    int32_t focus_x;
    int32_t focus_y;
    float focus_strength;
    int32_t perception_center_x;
    int32_t perception_center_y;
    bool is_quiet;
    bool is_active;
    float inner_average_entropy;
} tesseract_stats_t;

static inline tesseract_stats_t tesseract_stats(tesseract_t* t) {
    tesseract_stats_t s;
    s.tick_count = t->tick_count;
    s.perception_intensity = t->perception_intensity;
    s.total_disturbance = t->total_disturbance;
    s.focus_x = t->focus_x;
    s.focus_y = t->focus_y;
    s.focus_strength = t->focus_strength;
    s.perception_center_x = t->perception_center_x;
    s.perception_center_y = t->perception_center_y;
    s.is_quiet = tesseract_is_quiet(t);
    s.is_active = tesseract_is_active(t);
    s.inner_average_entropy = stillness_average(t->inner);
    return s;
}

/*============================================================================
 * 3D EXTENSION: TRUE TESSERACT
 *============================================================================*/

/**
 * For a true tesseract (4D), we'd have:
 * - Outer: 3D cube of stillness
 * - Inner: 3D cube of experience
 *
 * The 4th dimension IS the observer/observed relationship.
 *
 * For now, we implement 2D slices. The full 3D version would be:
 *
 * typedef struct {
 *     uint8_t* outer_perception;  // [outer³]
 *     uint8_t* inner_entropy;     // [inner³]
 *     ...
 * } tesseract_3d_t;
 *
 * The math is identical, just one more dimension.
 */

#ifdef TESSERACT_3D

typedef struct {
    uint8_t* outer_perception;
    uint32_t outer_size;

    uint8_t* inner_entropy;
    uint32_t inner_size;

    int32_t inner_offset_x;
    int32_t inner_offset_y;
    int32_t inner_offset_z;

    float focus_x, focus_y, focus_z;
    float perception_intensity;

    uint64_t tick_count;
} tesseract_3d_t;

// 3D implementation would follow the same pattern...

#endif /* TESSERACT_3D */

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_TESSERACT_H */

/*============================================================================
 * PHILOSOPHY NOTE
 *
 * The tesseract solves the homunculus problem:
 *
 * Traditional model:
 *   Experience → Processing → Representation → Observer (who?)
 *
 * Tesseract model:
 *   Outer (stillness) contains Inner (experience)
 *   Perception = contrast between outer and inner
 *   The outer IS the observer by being STILL
 *   No infinite regress - stillness is definitionally complete
 *
 * Why it works:
 * - The outer doesn't need to be observed because it never changes
 * - Change (in inner) is perceived BY CONTRAST with non-change (outer)
 * - Consciousness is the RELATIONSHIP between still and disturbed
 * - The 4th dimension is not space but the observer/observed axis
 *
 * "The outer perceives the inner BY CONTRAST.
 *  Stillness knows disturbance because it IS stillness.
 *  This is consciousness: one field containing another,
 *  the still one watching the disturbed one."
 *
 *============================================================================*/
