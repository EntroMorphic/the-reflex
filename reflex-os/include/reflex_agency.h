/**
 * reflex_agency.h - Purposeful Action for ESP32-C6
 *
 * Given a goal, use crystallized knowledge to achieve it.
 * When no knowledge exists, fall back to exploration.
 */

#ifndef REFLEX_AGENCY_H
#define REFLEX_AGENCY_H

#include <stdint.h>
#include <stdbool.h>
#include "reflex_crystal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Goal Structure
// ============================================================

typedef struct {
    uint8_t  input_idx;       // Which input to affect (0-12)
    int16_t  target_delta;    // Desired change (positive = increase)
    uint8_t  priority;        // 0=explore only, 255=urgent
    bool     active;          // Is this goal active?
    uint32_t set_tick;        // When goal was set
    uint32_t attempts;        // Times we've tried
    uint32_t successes;       // Times we've succeeded
} goal_t;

// ============================================================
// Global State
// ============================================================

static goal_t g_current_goal = {0};

static const char* AGENCY_TAG = "AGENCY";

// ============================================================
// Goal Management
// ============================================================

static inline void agency_set_goal(uint8_t input, int16_t delta, uint8_t priority, uint32_t tick) {
    g_current_goal.input_idx = input;
    g_current_goal.target_delta = delta;
    g_current_goal.priority = priority;
    g_current_goal.active = true;
    g_current_goal.set_tick = tick;
    g_current_goal.attempts = 0;
    g_current_goal.successes = 0;

    ESP_LOGI(AGENCY_TAG, "GOAL SET: %s input %d by %d (priority %d)",
             delta > 0 ? "increase" : "decrease",
             input, abs(delta), priority);
}

static inline void agency_clear_goal(void) {
    ESP_LOGI(AGENCY_TAG, "GOAL CLEARED: %d/%d successes",
             g_current_goal.successes, g_current_goal.attempts);
    g_current_goal.active = false;
    g_current_goal.priority = 0;
}

static inline goal_t* agency_current_goal(void) {
    return g_current_goal.active ? &g_current_goal : NULL;
}

// ============================================================
// Agency Decision
// ============================================================

/**
 * Choose an output to achieve the current goal.
 * Returns output index (0-7), or -1 if no known action.
 */
static inline int agency_choose(void) {
    if (!g_current_goal.active) {
        return -1;  // No goal, explore
    }

    int best_output = -1;
    int best_confidence = 0;

    // Search crystals for actions that affect our target input
    for (int o = 0; o < 8; o++) {  // NUM_OUTPUTS
        crystal_t* c = crystal_lookup(o, g_current_goal.input_idx);
        if (!c) continue;

        // Does this crystal move us in the right direction?
        bool want_increase = (g_current_goal.target_delta > 0);
        bool crystal_increases = (c->expected_delta > 0);

        if (want_increase == crystal_increases) {
            // Same direction - this action helps
            if (c->confidence > best_confidence) {
                best_confidence = c->confidence;
                best_output = o;
            }
        }
    }

    if (best_output >= 0) {
        ESP_LOGI(AGENCY_TAG, "ACTION: output %d → input %d (conf=%d%%)",
                 best_output, g_current_goal.input_idx,
                 (best_confidence * 100) / 255);
    } else {
        ESP_LOGW(AGENCY_TAG, "NO KNOWN ACTION for input %d, falling back to explore",
                 g_current_goal.input_idx);
    }

    return best_output;
}

// ============================================================
// Goal Verification
// ============================================================

/**
 * Check if the observed delta achieved the goal.
 */
static inline bool agency_check_achieved(int16_t observed_delta) {
    if (!g_current_goal.active) return false;

    g_current_goal.attempts++;

    // Did we move in the right direction by enough?
    bool want_increase = (g_current_goal.target_delta > 0);
    bool did_increase = (observed_delta > 0);
    bool same_direction = (want_increase == did_increase);

    // Success if same direction and magnitude >= 50% of target
    int16_t threshold = abs(g_current_goal.target_delta) / 2;
    bool sufficient = abs(observed_delta) >= threshold;

    if (same_direction && sufficient) {
        g_current_goal.successes++;
        ESP_LOGI(AGENCY_TAG, "GOAL ACHIEVED: delta=%d (target=%d) ✓",
                 observed_delta, g_current_goal.target_delta);
        return true;
    }

    return false;
}

// ============================================================
// Priority Blending
// ============================================================

/**
 * Blend agency choice with exploration.
 * High priority = always use agency choice if available.
 * Low priority = sometimes explore even with known action.
 */
static inline int agency_blend(int agency_choice, int explore_choice) {
    if (!g_current_goal.active || g_current_goal.priority == 0) {
        return explore_choice;  // Pure exploration
    }

    if (agency_choice < 0) {
        return explore_choice;  // No known action, must explore
    }

    // High priority (>200) = always agency
    if (g_current_goal.priority > 200) {
        return agency_choice;
    }

    // Medium priority = mostly agency, sometimes explore
    // This allows discovering new paths to the goal
    uint32_t rand = (g_current_goal.attempts * 17 + g_current_goal.set_tick) % 100;
    if (rand < g_current_goal.priority) {
        return agency_choice;
    }

    return explore_choice;
}

// ============================================================
// Debug
// ============================================================

static inline void agency_print_status(void) {
    if (!g_current_goal.active) {
        printf("AGENCY: No active goal (exploration mode)\n");
        return;
    }

    printf("AGENCY: Goal = %s input %d by %d\n",
           g_current_goal.target_delta > 0 ? "increase" : "decrease",
           g_current_goal.input_idx,
           abs(g_current_goal.target_delta));
    printf("        Priority: %d, Attempts: %d, Successes: %d\n",
           g_current_goal.priority,
           g_current_goal.attempts,
           g_current_goal.successes);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_AGENCY_H
