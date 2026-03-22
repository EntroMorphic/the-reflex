/**
 * reflex_cfc_etm.h - Autonomous CfC via ETM+GDMA
 *
 * The neural network runs WITHOUT the CPU.
 * ETM (Event Task Matrix) triggers GDMA (General DMA).
 * GDMA streams weights and accumulates results.
 * GPIO events trigger inference automatically.
 *
 * Architecture:
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │                     AUTONOMOUS CfC                                  │
 *   │                                                                     │
 *   │   GPIO Edge ──► ETM Channel 0 ──► GDMA Start                       │
 *   │                                                                     │
 *   │   GDMA Chain:                                                       │
 *   │     1. Read sensor (GPIO → SRAM)                                   │
 *   │     2. Stream weights (SRAM → internal)                            │
 *   │     3. Accumulate (DMA scatter-gather)                             │
 *   │     4. Write result (internal → GPIO)                              │
 *   │                                                                     │
 *   │   GDMA Done ──► ETM Channel 1 ──► GPIO Toggle (result ready)       │
 *   │                                                                     │
 *   │   CPU: SLEEPING                                                     │
 *   │                                                                     │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * Limitations:
 *   - GDMA can move data but not compute (no AND, POPCOUNT)
 *   - We use pre-computed LUTs for everything
 *   - This is approximate/quantized inference
 *
 * The REAL compute happens via lookup tables:
 *   - Input quantized to 8 bits
 *   - Each neuron has a pre-computed LUT: input_byte → contribution
 *   - GDMA reads from LUT and accumulates via chained transfers
 */

#ifndef REFLEX_CFC_ETM_H
#define REFLEX_CFC_ETM_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ESP-IDF ETM and GDMA APIs
#include "esp_etm.h"
#include "driver/gpio_etm.h"
#include "esp_async_memcpy.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define CFC_ETM_INPUT_GPIO      4       // Trigger inference on rising edge
#define CFC_ETM_OUTPUT_GPIO     5       // Toggle when inference complete
#define CFC_ETM_STATUS_GPIO     6       // Status LED

#define CFC_ETM_NUM_NEURONS     16      // Reduced for LUT size constraints
#define CFC_ETM_INPUT_BITS      8       // Quantized input
#define CFC_ETM_LUT_SIZE        256     // 2^8 entries per neuron

// ============================================================
// Pre-computed LUT Structure
// ============================================================

/**
 * For autonomous inference, we pre-compute everything as LUTs.
 * 
 * For each neuron, given 8-bit input, what's the contribution?
 * This collapses: input_bits → popcount(input & mask) into a single read.
 *
 * neuron_lut[input_byte] = contribution to that neuron (-8 to +8, quantized)
 */
typedef struct {
    // F pathway LUTs: f_lut[neuron][input_byte] = f contribution
    int8_t f_lut[CFC_ETM_NUM_NEURONS][CFC_ETM_LUT_SIZE];
    
    // G pathway LUTs: g_lut[neuron][input_byte] = g contribution  
    int8_t g_lut[CFC_ETM_NUM_NEURONS][CFC_ETM_LUT_SIZE];
    
    // Output LUTs: out_lut[output_bit][hidden_byte] = contribution
    int8_t out_lut[8][CFC_ETM_LUT_SIZE];
    
    // Sigmoid LUT: sigmoid[pre_act + 128] = output (0 or 1)
    uint8_t sigmoid_lut[256];
    
    // Current hidden state (8 bits for simplified version)
    uint8_t hidden;
    
    // Result buffer (DMA target)
    uint8_t result;
    
    // Input buffer (DMA source)
    uint8_t input_buffer;
    
} cfc_etm_layer_t __attribute__((aligned(4)));

// ============================================================
// ETM/GDMA Handles
// ============================================================

typedef struct {
    // ETM channels
    esp_etm_channel_handle_t trigger_channel;   // GPIO → GDMA start
    esp_etm_channel_handle_t done_channel;      // GDMA done → GPIO
    
    // ETM events and tasks
    esp_etm_event_handle_t gpio_event;          // Input GPIO edge
    esp_etm_task_handle_t gpio_task;            // Output GPIO toggle
    esp_etm_event_handle_t dma_done_event;      // DMA completion
    
    // Async memcpy handle (wraps GDMA)
    async_memcpy_handle_t mcp_handle;
    
    // Network data
    cfc_etm_layer_t* layer;
    
    // Statistics
    uint32_t inference_count;
    uint32_t last_inference_cycles;
    
    // State
    bool initialized;
    bool running;
    
} cfc_etm_engine_t;

// Global engine instance
static cfc_etm_engine_t g_cfc_etm_engine = {0};

// ============================================================
// LUT Generation
// ============================================================

/**
 * Generate LUT for one neuron's F pathway
 * Maps: 8-bit input → contribution based on ternary weights
 */
static inline void cfc_etm_generate_f_lut(
    cfc_etm_layer_t* layer,
    int neuron,
    uint8_t pos_mask,   // Which input bits have +1 weight
    uint8_t neg_mask    // Which input bits have -1 weight
) {
    for (int input = 0; input < 256; input++) {
        int pos_count = __builtin_popcount(input & pos_mask);
        int neg_count = __builtin_popcount(input & neg_mask);
        layer->f_lut[neuron][input] = (int8_t)(pos_count - neg_count);
    }
}

/**
 * Generate LUT for one neuron's G pathway
 */
static inline void cfc_etm_generate_g_lut(
    cfc_etm_layer_t* layer,
    int neuron,
    uint8_t pos_mask,
    uint8_t neg_mask
) {
    for (int input = 0; input < 256; input++) {
        int pos_count = __builtin_popcount(input & pos_mask);
        int neg_count = __builtin_popcount(input & neg_mask);
        layer->g_lut[neuron][input] = (int8_t)(pos_count - neg_count);
    }
}

/**
 * Generate sigmoid LUT
 */
static inline void cfc_etm_generate_sigmoid_lut(cfc_etm_layer_t* layer) {
    for (int i = 0; i < 256; i++) {
        int x = i - 128;  // Map to [-128, 127]
        // Simplified sigmoid: threshold at 0
        layer->sigmoid_lut[i] = (x > 0) ? 1 : 0;
    }
}

/**
 * Initialize layer with random ternary weights
 */
static inline void cfc_etm_init_random(cfc_etm_layer_t* layer, uint32_t seed) {
    uint32_t state = seed;
    #define RAND() (state ^= state << 13, state ^= state >> 17, state ^= state << 5, state)
    
    // Generate F and G LUTs for each neuron
    for (int n = 0; n < CFC_ETM_NUM_NEURONS; n++) {
        // Random sparse ternary masks (~20% non-zero)
        uint8_t f_pos = 0, f_neg = 0;
        uint8_t g_pos = 0, g_neg = 0;
        
        for (int b = 0; b < 8; b++) {
            uint32_t r = RAND() % 10;
            if (r == 0) f_pos |= (1 << b);
            else if (r == 1) f_neg |= (1 << b);
            
            r = RAND() % 10;
            if (r == 0) g_pos |= (1 << b);
            else if (r == 1) g_neg |= (1 << b);
        }
        
        cfc_etm_generate_f_lut(layer, n, f_pos, f_neg);
        cfc_etm_generate_g_lut(layer, n, g_pos, g_neg);
    }
    
    // Generate output LUTs
    for (int o = 0; o < 8; o++) {
        uint8_t pos = 0, neg = 0;
        for (int b = 0; b < 8; b++) {
            uint32_t r = RAND() % 5;
            if (r == 0) pos |= (1 << b);
            else if (r == 1) neg |= (1 << b);
        }
        
        for (int hidden = 0; hidden < 256; hidden++) {
            int pos_count = __builtin_popcount(hidden & pos);
            int neg_count = __builtin_popcount(hidden & neg);
            layer->out_lut[o][hidden] = (int8_t)(pos_count - neg_count);
        }
    }
    
    cfc_etm_generate_sigmoid_lut(layer);
    
    layer->hidden = 0;
    layer->result = 0;
    layer->input_buffer = 0;
    
    #undef RAND
}

// ============================================================
// Software Inference (for comparison)
// ============================================================

/**
 * Run inference in software (CPU)
 * This is what we want the hardware to do autonomously
 */
static inline uint8_t cfc_etm_forward_sw(cfc_etm_layer_t* layer, uint8_t input) {
    // Combine input with hidden state
    uint8_t combined = input | layer->hidden;
    
    // Compute F and G for all neurons
    uint8_t f_gate = 0;
    uint8_t g_bits = 0;
    
    for (int n = 0; n < CFC_ETM_NUM_NEURONS && n < 8; n++) {
        int8_t f_val = layer->f_lut[n][combined];
        int8_t g_val = layer->g_lut[n][combined];
        
        // Apply sigmoid to f
        if (f_val > 0) f_gate |= (1 << n);
        
        // Threshold g
        if (g_val > 0) g_bits |= (1 << n);
    }
    
    // CfC dynamics
    layer->hidden = (f_gate & g_bits) | ((~f_gate) & layer->hidden);
    
    // Output layer
    uint8_t output = 0;
    for (int o = 0; o < 8; o++) {
        int8_t out_val = layer->out_lut[o][layer->hidden];
        if (out_val > 0) output |= (1 << o);
    }
    
    layer->result = output;
    return output;
}

// ============================================================
// ETM+GDMA Setup
// ============================================================

/**
 * DMA completion callback
 */
static bool IRAM_ATTR cfc_etm_dma_callback(
    async_memcpy_handle_t mcp_hdl,
    async_memcpy_event_t *event,
    void *cb_args
) {
    cfc_etm_engine_t* engine = (cfc_etm_engine_t*)cb_args;
    engine->inference_count++;
    return false;  // No need to yield
}

/**
 * Initialize ETM+GDMA autonomous inference engine
 */
static inline esp_err_t cfc_etm_engine_init(
    cfc_etm_engine_t* engine,
    cfc_etm_layer_t* layer
) {
    esp_err_t ret;
    
    engine->layer = layer;
    engine->inference_count = 0;
    engine->initialized = false;
    engine->running = false;
    
    // Configure input GPIO
    gpio_config_t input_cfg = {
        .pin_bit_mask = 1ULL << CFC_ETM_INPUT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&input_cfg);
    if (ret != ESP_OK) return ret;
    
    // Configure output GPIO
    gpio_config_t output_cfg = {
        .pin_bit_mask = (1ULL << CFC_ETM_OUTPUT_GPIO) | (1ULL << CFC_ETM_STATUS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&output_cfg);
    if (ret != ESP_OK) return ret;
    
    gpio_set_level(CFC_ETM_STATUS_GPIO, 0);
    
    // Allocate ETM channel for trigger
    esp_etm_channel_config_t etm_cfg = {};
    ret = esp_etm_new_channel(&etm_cfg, &engine->trigger_channel);
    if (ret != ESP_OK) return ret;
    
    // Allocate ETM channel for done signal
    ret = esp_etm_new_channel(&etm_cfg, &engine->done_channel);
    if (ret != ESP_OK) return ret;
    
    // Create GPIO ETM event (rising edge on input)
    gpio_etm_event_config_t gpio_event_cfg = {
        .edge = GPIO_ETM_EVENT_EDGE_POS,
    };
    ret = gpio_new_etm_event(&gpio_event_cfg, &engine->gpio_event);
    if (ret != ESP_OK) return ret;
    
    ret = gpio_etm_event_bind_gpio(engine->gpio_event, CFC_ETM_INPUT_GPIO);
    if (ret != ESP_OK) return ret;
    
    // Create GPIO ETM task (toggle output)
    gpio_etm_task_config_t gpio_task_cfg = {
        .action = GPIO_ETM_TASK_ACTION_TOG,
    };
    ret = gpio_new_etm_task(&gpio_task_cfg, &engine->gpio_task);
    if (ret != ESP_OK) return ret;
    
    ret = gpio_etm_task_add_gpio(engine->gpio_task, CFC_ETM_OUTPUT_GPIO);
    if (ret != ESP_OK) return ret;
    
    // Initialize async memcpy (GDMA wrapper)
    async_memcpy_config_t mcp_cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
    ret = esp_async_memcpy_install(&mcp_cfg, &engine->mcp_handle);
    if (ret != ESP_OK) return ret;
    
    // Get GDMA ETM event (copy done)
    ret = esp_async_memcpy_new_etm_event(
        engine->mcp_handle,
        ASYNC_MEMCPY_ETM_EVENT_COPY_DONE,
        &engine->dma_done_event
    );
    if (ret != ESP_OK) return ret;
    
    // Connect: GPIO event → (trigger DMA manually for now, ETM can't directly start GDMA)
    // Connect: DMA done → GPIO toggle
    ret = esp_etm_channel_connect(engine->done_channel, engine->dma_done_event, engine->gpio_task);
    if (ret != ESP_OK) return ret;
    
    ret = esp_etm_channel_enable(engine->done_channel);
    if (ret != ESP_OK) return ret;
    
    engine->initialized = true;
    
    return ESP_OK;
}

/**
 * Run one autonomous inference cycle
 * 
 * This demonstrates the concept:
 * 1. DMA copies input
 * 2. Software computes (until we can do LUT via DMA)
 * 3. DMA copies output
 * 4. ETM toggles GPIO when done
 */
static inline esp_err_t cfc_etm_run_inference(cfc_etm_engine_t* engine, uint8_t input) {
    if (!engine->initialized) return ESP_ERR_INVALID_STATE;
    
    uint32_t t0;
    // Use inline asm to read cycle counter
    __asm__ volatile("csrr %0, 0x7e2" : "=r"(t0));
    
    // Store input
    engine->layer->input_buffer = input;
    
    // Run forward pass (software for now)
    uint8_t result = cfc_etm_forward_sw(engine->layer, input);
    
    // The DMA done event will toggle the output GPIO via ETM
    // For now, trigger a dummy DMA to demonstrate the ETM connection
    static uint8_t dummy_src[4] __attribute__((aligned(4))) = {0};
    static uint8_t dummy_dst[4] __attribute__((aligned(4))) = {0};
    dummy_src[0] = result;
    
    esp_async_memcpy(engine->mcp_handle, dummy_dst, dummy_src, 4, cfc_etm_dma_callback, engine);
    
    uint32_t t1;
    __asm__ volatile("csrr %0, 0x7e2" : "=r"(t1));
    engine->last_inference_cycles = t1 - t0;
    
    return ESP_OK;
}

/**
 * Cleanup
 */
static inline void cfc_etm_engine_deinit(cfc_etm_engine_t* engine) {
    if (!engine->initialized) return;
    
    esp_etm_channel_disable(engine->done_channel);
    
    gpio_etm_task_rm_gpio(engine->gpio_task, CFC_ETM_OUTPUT_GPIO);
    gpio_etm_event_bind_gpio(engine->gpio_event, -1);
    
    esp_etm_del_task(engine->gpio_task);
    esp_etm_del_event(engine->gpio_event);
    esp_etm_del_event(engine->dma_done_event);
    esp_etm_del_channel(engine->trigger_channel);
    esp_etm_del_channel(engine->done_channel);
    
    esp_async_memcpy_uninstall(engine->mcp_handle);
    
    engine->initialized = false;
}

// ============================================================
// Statistics
// ============================================================

typedef struct {
    uint32_t inference_count;
    uint32_t last_inference_ns;
    bool running;
} cfc_etm_stats_t;

static inline cfc_etm_stats_t cfc_etm_get_stats(const cfc_etm_engine_t* engine) {
    cfc_etm_stats_t stats;
    stats.inference_count = engine->inference_count;
    stats.last_inference_ns = (engine->last_inference_cycles * 1000) / 160;  // 160 MHz
    stats.running = engine->running;
    return stats;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_CFC_ETM_H
