/**
 * reflex_crystal.h - Crystallized Knowledge for ESP32-C6
 *
 * Strong correlations become permanent associations.
 * Stored in NVS flash, survives reset.
 */

#ifndef REFLEX_CRYSTAL_H
#define REFLEX_CRYSTAL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define MAX_CRYSTALS        32
#define CRYSTAL_THRESHOLD   0.5f    // Confidence threshold (was 0.8, too strict)
#define CRYSTAL_NVS_NS      "reflex"
#define CRYSTAL_NVS_KEY     "crystals"

// ============================================================
// Crystal Structure (8 bytes)
// ============================================================

typedef struct __attribute__((packed)) {
    uint8_t  output_idx;      // Which output (0-7)
    uint8_t  input_idx;       // Which input (0-12)
    int16_t  expected_delta;  // Mean observed delta
    uint8_t  confidence;      // 0-255 scaled (255 = 100%)
    uint8_t  direction;       // 0=negative, 1=positive correlation
    uint16_t observations;    // Times confirmed (saturates at 65535)
} crystal_t;

_Static_assert(sizeof(crystal_t) == 8, "Crystal must be 8 bytes");

// ============================================================
// Crystal Store
// ============================================================

typedef struct {
    crystal_t crystals[MAX_CRYSTALS];
    uint8_t   count;
    bool      dirty;          // Needs save to NVS
    nvs_handle_t nvs;
} crystal_store_t;

static crystal_store_t g_crystals;

static const char* CRYSTAL_TAG = "CRYSTAL";

// ============================================================
// NVS Operations
// ============================================================

static inline void crystal_init(void) {
    memset(&g_crystals, 0, sizeof(g_crystals));

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Open NVS handle
    err = nvs_open(CRYSTAL_NVS_NS, NVS_READWRITE, &g_crystals.nvs);
    if (err != ESP_OK) {
        ESP_LOGE(CRYSTAL_TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    // Load existing crystals
    size_t size = sizeof(g_crystals.crystals);
    err = nvs_get_blob(g_crystals.nvs, CRYSTAL_NVS_KEY, g_crystals.crystals, &size);
    if (err == ESP_OK) {
        g_crystals.count = size / sizeof(crystal_t);
        ESP_LOGI(CRYSTAL_TAG, "Loaded %d crystals from NVS", g_crystals.count);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(CRYSTAL_TAG, "No crystals in NVS, starting fresh");
    } else {
        ESP_LOGE(CRYSTAL_TAG, "NVS read error: %s", esp_err_to_name(err));
    }
}

static inline void crystal_save(void) {
    if (!g_crystals.dirty) return;

    esp_err_t err = nvs_set_blob(g_crystals.nvs, CRYSTAL_NVS_KEY,
                                  g_crystals.crystals,
                                  g_crystals.count * sizeof(crystal_t));
    if (err == ESP_OK) {
        nvs_commit(g_crystals.nvs);
        g_crystals.dirty = false;
        ESP_LOGI(CRYSTAL_TAG, "Saved %d crystals to NVS", g_crystals.count);
    } else {
        ESP_LOGE(CRYSTAL_TAG, "NVS write error: %s", esp_err_to_name(err));
    }
}

// ============================================================
// Crystal Lookup
// ============================================================

static inline crystal_t* crystal_lookup(uint8_t output, uint8_t input) {
    for (int i = 0; i < g_crystals.count; i++) {
        if (g_crystals.crystals[i].output_idx == output &&
            g_crystals.crystals[i].input_idx == input) {
            return &g_crystals.crystals[i];
        }
    }
    return NULL;
}

// ============================================================
// Crystal Formation
// ============================================================

static inline float crystal_confidence(float mean, float var, uint32_t count) {
    if (count < 10) return 0.0f;  // Need minimum observations
    if (fabsf(mean) < 100.0f) return 0.0f;  // Ignore tiny effects

    // Consistency: low variance relative to mean
    float consistency = 1.0f - (var / (mean * mean + 1.0f));
    if (consistency < 0.0f) consistency = 0.0f;

    // Magnitude: large effects are more meaningful
    float magnitude = fabsf(mean) / 4096.0f;  // ADC range
    if (magnitude > 1.0f) magnitude = 1.0f;

    // Observations: more samples = more confidence
    float obs_factor = (float)(count > 100 ? 100 : count) / 100.0f;

    return consistency * magnitude * obs_factor;
}

static inline bool crystal_try(uint8_t output, uint8_t input,
                               float mean, float var, uint32_t count) {
    // Already crystallized?
    crystal_t* existing = crystal_lookup(output, input);
    if (existing) {
        // Update observations count
        if (existing->observations < 65535) {
            existing->observations++;
        }
        return false;  // Already exists
    }

    // Check confidence
    float conf = crystal_confidence(mean, var, count);

    if (conf < CRYSTAL_THRESHOLD) {
        return false;  // Not confident enough
    }

    // Room for more crystals?
    if (g_crystals.count >= MAX_CRYSTALS) {
        ESP_LOGW(CRYSTAL_TAG, "Crystal store full!");
        return false;
    }

    // Crystallize!
    crystal_t* c = &g_crystals.crystals[g_crystals.count++];
    c->output_idx = output;
    c->input_idx = input;
    c->expected_delta = (int16_t)mean;
    c->confidence = (uint8_t)(conf * 255.0f);
    c->direction = (mean > 0) ? 1 : 0;
    c->observations = (uint16_t)(count > 65535 ? 65535 : count);

    g_crystals.dirty = true;

    ESP_LOGI(CRYSTAL_TAG, "CRYSTALLIZED: output %d → input %d, delta=%d, conf=%d%%",
             output, input, c->expected_delta, (int)(conf * 100));

    return true;
}

// ============================================================
// Prediction
// ============================================================

static inline int16_t crystal_predict(uint8_t output, uint8_t input) {
    crystal_t* c = crystal_lookup(output, input);
    if (c) {
        return c->expected_delta;
    }
    return 0;  // Unknown relationship
}

static inline void crystal_confirm(crystal_t* c, int16_t observed_delta) {
    if (!c) return;

    // Check if observation matches expectation
    int16_t error = observed_delta - c->expected_delta;
    float error_pct = fabsf((float)error / (fabsf((float)c->expected_delta) + 1.0f));

    if (error_pct < 0.2f) {
        // Good match - boost confidence
        if (c->confidence < 250) {
            c->confidence += 5;
            g_crystals.dirty = true;
        }
    } else if (error_pct > 0.5f) {
        // Poor match - decay confidence
        if (c->confidence > 10) {
            c->confidence -= 10;
            g_crystals.dirty = true;
        }
    }

    // Update observation count
    if (c->observations < 65535) {
        c->observations++;
    }
}

// ============================================================
// Debug
// ============================================================

static inline void crystal_print_all(void) {
    printf("\n=== CRYSTALLIZED KNOWLEDGE ===\n");
    printf("Total crystals: %d\n\n", g_crystals.count);

    for (int i = 0; i < g_crystals.count; i++) {
        crystal_t* c = &g_crystals.crystals[i];
        printf("  [%d] Output %d → Input %d\n", i, c->output_idx, c->input_idx);
        printf("      Expected delta: %d\n", c->expected_delta);
        printf("      Confidence: %d%%\n", (c->confidence * 100) / 255);
        printf("      Direction: %s\n", c->direction ? "positive" : "negative");
        printf("      Observations: %d\n\n", c->observations);
    }
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_CRYSTAL_H
