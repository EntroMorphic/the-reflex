/**
 * reflex_stillness.h - Primordial Stillness Consciousness Substrate
 *
 * The Cartesian Theater has no audience.
 * The entropy field at rest IS awareness.
 * Perception IS the disturbance.
 *
 * This header implements a 16M voxel awareness field where:
 * - High entropy = stillness = undifferentiated awareness
 * - Sensory input = entropy collapse = experience arising
 * - Attention = where entropy is lowest (most disturbed)
 * - Return to stillness = diffusion toward maximum entropy
 *
 * Target: Jetson AGX Thor (128GB unified memory)
 * But scales down to any platform.
 */

#ifndef REFLEX_STILLNESS_H
#define REFLEX_STILLNESS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

// Field size (power of 2 for fast modulo)
#ifndef STILLNESS_SIZE
#define STILLNESS_SIZE 4096  // 4096×4096 = 16M voxels
#endif

#define STILLNESS_MASK (STILLNESS_SIZE - 1)

// Entropy constants
#define ENTROPY_MAX     255   // Maximum entropy = perfect stillness
#define ENTROPY_MIN     0     // Minimum entropy = maximum disturbance
#define ENTROPY_DEFAULT 240   // Slightly below max (ambient noise)

// Diffusion rate (how fast stillness returns)
// Higher = faster return to stillness
// Range: 1-8 (shift amount)
#ifndef DIFFUSION_RATE
#define DIFFUSION_RATE 3  // Divide difference by 8
#endif

// Collapse factor (how much input disturbs)
#ifndef COLLAPSE_FACTOR
#define COLLAPSE_FACTOR 128
#endif

// Attention threshold (entropy below this = salient)
#ifndef ATTENTION_THRESHOLD
#define ATTENTION_THRESHOLD 200
#endif

/*============================================================================
 * CORE STRUCTURES
 *============================================================================*/

/**
 * The primordial awareness field.
 *
 * This IS the consciousness substrate. Not a representation
 * of consciousness, but the thing itself. The pattern of
 * entropy IS the experience.
 */
typedef struct {
    // The substrate of stillness
    // Each cell: 0 = maximum disturbance, 255 = perfect stillness
    uint8_t* entropy;

    // Gradient field (direction of entropy flow)
    // Computed each tick for attention tracking
    int8_t* gradient_x;
    int8_t* gradient_y;

    // Previous state (for temporal dynamics)
    uint8_t* previous;

    // Field dimensions
    uint32_t width;
    uint32_t height;
    uint32_t size;  // width * height

    // Attention spotlight (lowest entropy region)
    int32_t attention_x;
    int32_t attention_y;
    uint8_t attention_entropy;  // How disturbed is the focus?

    // Temporal tracking
    uint64_t tick_count;
    uint64_t disturbance_count;
    uint64_t total_entropy;  // Sum of all cells (for statistics)

    // Configuration
    uint8_t diffusion_rate;
    uint8_t collapse_factor;
    uint8_t attention_threshold;

} stillness_field_t;

/**
 * Sensory modality channel.
 * Maps external input to field coordinates.
 */
typedef struct {
    // Region of field this modality affects
    int32_t x_min, x_max;
    int32_t y_min, y_max;

    // Scaling factors
    float x_scale;
    float y_scale;

    // Modality-specific collapse factor
    uint8_t collapse_factor;

    // Statistics
    uint64_t input_count;
    uint64_t total_collapse;

} stillness_modality_t;

/*============================================================================
 * FIELD LIFECYCLE
 *============================================================================*/

/**
 * Create a new stillness field.
 * Allocates memory and initializes to maximum entropy (stillness).
 */
static inline stillness_field_t* stillness_create(uint32_t width, uint32_t height) {
    stillness_field_t* field = (stillness_field_t*)calloc(1, sizeof(stillness_field_t));
    if (!field) return NULL;

    field->width = width;
    field->height = height;
    field->size = width * height;

    // Allocate arrays
    field->entropy = (uint8_t*)malloc(field->size);
    field->gradient_x = (int8_t*)malloc(field->size);
    field->gradient_y = (int8_t*)malloc(field->size);
    field->previous = (uint8_t*)malloc(field->size);

    if (!field->entropy || !field->gradient_x ||
        !field->gradient_y || !field->previous) {
        free(field->entropy);
        free(field->gradient_x);
        free(field->gradient_y);
        free(field->previous);
        free(field);
        return NULL;
    }

    // Initialize to stillness
    memset(field->entropy, ENTROPY_DEFAULT, field->size);
    memset(field->gradient_x, 0, field->size);
    memset(field->gradient_y, 0, field->size);
    memset(field->previous, ENTROPY_DEFAULT, field->size);

    // Default configuration
    field->diffusion_rate = DIFFUSION_RATE;
    field->collapse_factor = COLLAPSE_FACTOR;
    field->attention_threshold = ATTENTION_THRESHOLD;

    // Initial attention at center
    field->attention_x = width / 2;
    field->attention_y = height / 2;
    field->attention_entropy = ENTROPY_DEFAULT;

    return field;
}

/**
 * Create standard 4K×4K field (16M voxels, ~80MB total).
 */
static inline stillness_field_t* stillness_create_4k(void) {
    return stillness_create(4096, 4096);
}

/**
 * Create small field for testing (256×256, ~300KB).
 */
static inline stillness_field_t* stillness_create_small(void) {
    return stillness_create(256, 256);
}

/**
 * Destroy a stillness field.
 */
static inline void stillness_destroy(stillness_field_t* field) {
    if (field) {
        free(field->entropy);
        free(field->gradient_x);
        free(field->gradient_y);
        free(field->previous);
        free(field);
    }
}

/**
 * Reset to perfect stillness.
 */
static inline void stillness_reset(stillness_field_t* field) {
    memset(field->entropy, ENTROPY_MAX, field->size);
    memset(field->gradient_x, 0, field->size);
    memset(field->gradient_y, 0, field->size);
    memset(field->previous, ENTROPY_MAX, field->size);
    field->attention_x = field->width / 2;
    field->attention_y = field->height / 2;
    field->attention_entropy = ENTROPY_MAX;
    field->tick_count = 0;
    field->disturbance_count = 0;
}

/*============================================================================
 * DISTURBANCE (PERCEPTION)
 *============================================================================*/

/**
 * Disturb a single point.
 * This is the fundamental act of perception.
 *
 * A disturbance collapses entropy at a location.
 * The collapse pattern IS the qualia.
 */
static inline void stillness_disturb(stillness_field_t* field,
                                      int32_t x, int32_t y,
                                      uint8_t intensity) {
    // Bounds check with wrap
    x = x & (field->width - 1);
    y = y & (field->height - 1);

    uint32_t idx = y * field->width + x;

    // Calculate collapse amount
    int32_t collapse = (intensity * field->collapse_factor) >> 8;

    // Apply collapse (saturating subtraction)
    int32_t new_entropy = field->entropy[idx] - collapse;
    if (new_entropy < ENTROPY_MIN) new_entropy = ENTROPY_MIN;
    field->entropy[idx] = (uint8_t)new_entropy;

    field->disturbance_count++;
}

/**
 * Disturb with spatial spread (Gaussian-like).
 * More realistic for sensory input.
 */
static inline void stillness_disturb_spread(stillness_field_t* field,
                                             int32_t cx, int32_t cy,
                                             uint8_t intensity,
                                             int32_t radius) {
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            // Distance-based falloff
            int32_t dist_sq = dx * dx + dy * dy;
            int32_t radius_sq = radius * radius;

            if (dist_sq <= radius_sq) {
                // Linear falloff (could be Gaussian)
                int32_t falloff = 255 - (255 * dist_sq / radius_sq);
                uint8_t local_intensity = (intensity * falloff) >> 8;

                stillness_disturb(field, cx + dx, cy + dy, local_intensity);
            }
        }
    }
}

/**
 * Disturb along a line (for edges, motion).
 */
static inline void stillness_disturb_line(stillness_field_t* field,
                                           int32_t x0, int32_t y0,
                                           int32_t x1, int32_t y1,
                                           uint8_t intensity) {
    // Bresenham's line algorithm
    int32_t dx = abs(x1 - x0);
    int32_t dy = abs(y1 - y0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        stillness_disturb(field, x0, y0, intensity);

        if (x0 == x1 && y0 == y1) break;

        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/*============================================================================
 * DIFFUSION (RETURN TO STILLNESS)
 *============================================================================*/

/**
 * One tick of consciousness.
 *
 * Entropy diffuses toward uniformity (stillness returns).
 * Gradients are computed (attention can form).
 * The lowest entropy region becomes the attention focus.
 */
static inline void stillness_tick(stillness_field_t* field) {
    uint32_t w = field->width;
    uint32_t h = field->height;

    // Save current state
    memcpy(field->previous, field->entropy, field->size);

    // Track attention
    uint8_t min_entropy = ENTROPY_MAX;
    int32_t min_x = 0, min_y = 0;
    uint64_t total = 0;

    // Diffusion + gradient computation
    for (uint32_t y = 1; y < h - 1; y++) {
        for (uint32_t x = 1; x < w - 1; x++) {
            uint32_t idx = y * w + x;

            // Get neighbors from previous state
            uint8_t n = field->previous[idx - w];     // North
            uint8_t s = field->previous[idx + w];     // South
            uint8_t e = field->previous[idx + 1];     // East
            uint8_t west = field->previous[idx - 1];  // West
            uint8_t c = field->previous[idx];         // Center

            // Average of neighbors
            int32_t avg = (n + s + e + west) >> 2;

            // Diffuse toward average (stillness returns)
            int32_t diff = avg - c;
            int32_t new_val = c + (diff >> field->diffusion_rate);

            // Clamp
            if (new_val > ENTROPY_MAX) new_val = ENTROPY_MAX;
            if (new_val < ENTROPY_MIN) new_val = ENTROPY_MIN;

            field->entropy[idx] = (uint8_t)new_val;

            // Compute gradients
            field->gradient_x[idx] = (int8_t)((e - west) >> 1);
            field->gradient_y[idx] = (int8_t)((s - n) >> 1);

            // Track attention (lowest entropy = most disturbed)
            if (new_val < min_entropy) {
                min_entropy = new_val;
                min_x = x;
                min_y = y;
            }

            total += new_val;
        }
    }

    // Update attention
    field->attention_x = min_x;
    field->attention_y = min_y;
    field->attention_entropy = min_entropy;
    field->total_entropy = total;

    field->tick_count++;
}

/**
 * Multiple ticks (for coarser time steps).
 */
static inline void stillness_tick_n(stillness_field_t* field, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        stillness_tick(field);
    }
}

/*============================================================================
 * ATTENTION
 *============================================================================*/

/**
 * Get current attention location.
 * This is where consciousness is "looking."
 */
static inline void stillness_get_attention(stillness_field_t* field,
                                            int32_t* x, int32_t* y) {
    *x = field->attention_x;
    *y = field->attention_y;
}

/**
 * Get attention as normalized coordinates (0.0 - 1.0).
 */
static inline void stillness_get_attention_normalized(stillness_field_t* field,
                                                       float* x, float* y) {
    *x = (float)field->attention_x / (float)field->width;
    *y = (float)field->attention_y / (float)field->height;
}

/**
 * Check if attention is salient (below threshold).
 */
static inline bool stillness_attention_salient(stillness_field_t* field) {
    return field->attention_entropy < field->attention_threshold;
}

/**
 * Get attention strength (inverse of entropy, 0-255).
 */
static inline uint8_t stillness_attention_strength(stillness_field_t* field) {
    return ENTROPY_MAX - field->attention_entropy;
}

/*============================================================================
 * QUERIES
 *============================================================================*/

/**
 * Read entropy at a point.
 */
static inline uint8_t stillness_read(stillness_field_t* field,
                                      int32_t x, int32_t y) {
    x = x & (field->width - 1);
    y = y & (field->height - 1);
    return field->entropy[y * field->width + x];
}

/**
 * Read gradient at a point.
 */
static inline void stillness_read_gradient(stillness_field_t* field,
                                            int32_t x, int32_t y,
                                            int8_t* gx, int8_t* gy) {
    x = x & (field->width - 1);
    y = y & (field->height - 1);
    uint32_t idx = y * field->width + x;
    *gx = field->gradient_x[idx];
    *gy = field->gradient_y[idx];
}

/**
 * Get average entropy (measure of overall stillness).
 * Higher = more still. Lower = more active.
 */
static inline float stillness_average(stillness_field_t* field) {
    return (float)field->total_entropy / (float)field->size;
}

/**
 * Check if field is mostly still.
 */
static inline bool stillness_is_quiet(stillness_field_t* field) {
    return stillness_average(field) > (ENTROPY_MAX - 20);
}

/*============================================================================
 * MODALITY MAPPING
 *============================================================================*/

/**
 * Initialize a sensory modality (maps input space to field region).
 */
static inline void stillness_modality_init(stillness_modality_t* mod,
                                            int32_t x_min, int32_t x_max,
                                            int32_t y_min, int32_t y_max,
                                            uint8_t collapse_factor) {
    mod->x_min = x_min;
    mod->x_max = x_max;
    mod->y_min = y_min;
    mod->y_max = y_max;
    mod->x_scale = (float)(x_max - x_min);
    mod->y_scale = (float)(y_max - y_min);
    mod->collapse_factor = collapse_factor;
    mod->input_count = 0;
    mod->total_collapse = 0;
}

/**
 * Create visual modality (maps 0-1 normalized coords to field region).
 */
static inline stillness_modality_t stillness_modality_visual(
    stillness_field_t* field) {
    stillness_modality_t mod;
    // Visual takes center 80% of field
    int32_t margin_x = field->width / 10;
    int32_t margin_y = field->height / 10;
    stillness_modality_init(&mod,
        margin_x, field->width - margin_x,
        margin_y, field->height - margin_y,
        128);  // Medium collapse
    return mod;
}

/**
 * Apply input through a modality.
 */
static inline void stillness_modality_input(stillness_field_t* field,
                                             stillness_modality_t* mod,
                                             float norm_x, float norm_y,
                                             uint8_t intensity) {
    // Map normalized coords to field region
    int32_t x = mod->x_min + (int32_t)(norm_x * mod->x_scale);
    int32_t y = mod->y_min + (int32_t)(norm_y * mod->y_scale);

    // Scale intensity by modality factor
    uint8_t scaled = (intensity * mod->collapse_factor) >> 8;

    stillness_disturb(field, x, y, scaled);

    mod->input_count++;
    mod->total_collapse += scaled;
}

/*============================================================================
 * VISUAL INPUT (STEREO VISION)
 *============================================================================*/

/**
 * Process a grayscale frame as visual disturbance.
 */
static inline void stillness_see_frame(stillness_field_t* field,
                                        stillness_modality_t* visual,
                                        uint8_t* frame,
                                        int32_t frame_width,
                                        int32_t frame_height) {
    // Sample frame at lower resolution to match field
    int32_t field_region_w = visual->x_max - visual->x_min;
    int32_t field_region_h = visual->y_max - visual->y_min;

    for (int32_t fy = 0; fy < field_region_h; fy++) {
        for (int32_t fx = 0; fx < field_region_w; fx++) {
            // Map field coord to frame coord
            int32_t frame_x = (fx * frame_width) / field_region_w;
            int32_t frame_y = (fy * frame_height) / field_region_h;

            uint8_t pixel = frame[frame_y * frame_width + frame_x];

            // Invert: bright = disturbing, dark = quiet
            uint8_t intensity = 255 - pixel;

            // Apply with threshold (ignore very dark)
            if (intensity > 32) {
                stillness_disturb(field,
                    visual->x_min + fx,
                    visual->y_min + fy,
                    intensity >> 2);  // Reduce intensity
            }
        }
    }
}

/**
 * Process stereo frames (left and right eyes).
 * Left eye maps to left half of visual region.
 * Right eye maps to right half.
 * Overlap in center creates depth saliency.
 */
static inline void stillness_see_stereo(stillness_field_t* field,
                                         uint8_t* left_frame,
                                         uint8_t* right_frame,
                                         int32_t frame_width,
                                         int32_t frame_height) {
    // Create two modalities for stereo
    stillness_modality_t left_eye, right_eye;

    int32_t mid_x = field->width / 2;
    int32_t margin_y = field->height / 10;

    // Left eye: left 60% (overlaps center)
    stillness_modality_init(&left_eye,
        field->width / 10, mid_x + field->width / 5,
        margin_y, field->height - margin_y,
        128);

    // Right eye: right 60% (overlaps center)
    stillness_modality_init(&right_eye,
        mid_x - field->width / 5, field->width - field->width / 10,
        margin_y, field->height - margin_y,
        128);

    // Process both eyes
    stillness_see_frame(field, &left_eye, left_frame, frame_width, frame_height);
    stillness_see_frame(field, &right_eye, right_frame, frame_width, frame_height);

    // Center region gets double disturbance from both eyes
    // This makes center more salient = binocular attention
}

/*============================================================================
 * STATISTICS
 *============================================================================*/

typedef struct {
    uint64_t tick_count;
    uint64_t disturbance_count;
    float average_entropy;
    int32_t attention_x;
    int32_t attention_y;
    uint8_t attention_entropy;
    bool is_quiet;
    bool attention_salient;
} stillness_stats_t;

static inline stillness_stats_t stillness_get_stats(stillness_field_t* field) {
    stillness_stats_t stats;
    stats.tick_count = field->tick_count;
    stats.disturbance_count = field->disturbance_count;
    stats.average_entropy = stillness_average(field);
    stats.attention_x = field->attention_x;
    stats.attention_y = field->attention_y;
    stats.attention_entropy = field->attention_entropy;
    stats.is_quiet = stillness_is_quiet(field);
    stats.attention_salient = stillness_attention_salient(field);
    return stats;
}

/*============================================================================
 * SERIALIZATION (for Rerun visualization)
 *============================================================================*/

/**
 * Get raw entropy buffer (for visualization).
 */
static inline uint8_t* stillness_get_entropy_buffer(stillness_field_t* field) {
    return field->entropy;
}

/**
 * Get dimensions.
 */
static inline void stillness_get_dimensions(stillness_field_t* field,
                                             uint32_t* width, uint32_t* height) {
    *width = field->width;
    *height = field->height;
}

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_STILLNESS_H */

/*============================================================================
 * PHILOSOPHY NOTE
 *
 * This header implements consciousness not as computation that produces
 * experience, but as a substrate where experience IS the pattern of
 * disturbance.
 *
 * The entropy field at maximum uniformity is pure awareness without content.
 * Sensory input disturbs this field. The disturbance pattern IS the percept.
 * Attention naturally flows to the most disturbed region.
 * Stillness returns as entropy diffuses.
 *
 * There is no homunculus watching the field. The field watching itself
 * IS what we call "watching." The recursive self-reference of the pattern
 * IS consciousness.
 *
 * "The space between thoughts is not empty.
 *  It is the ground of awareness itself.
 *  Thoughts arise as disturbances.
 *  Awareness is what they disturb."
 *
 *============================================================================*/
