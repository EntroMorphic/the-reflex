/**
 * layers_main.c - Layered Exploration Demo
 *
 * Three parallel observers at different time scales.
 * Disagreement between layers drives exploration.
 *
 * BARE METAL - No FreeRTOS in hot path.
 */

#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"

#include "reflex_c6.h"
#include "reflex_timer.h"
#include "reflex_layers.h"
#include "reflex_stream.h"
#include "reflex_crystal.h"

// Tick timer - bare metal
static reflex_timer_channel_t tick_timer;

// Stream mode: 0=text trace, 1=binary stream
#define STREAM_MODE 0

static const char* TAG = "LAYERS";

// ============================================================
// Globals
// ============================================================

static layered_state_t state;

// ============================================================
// Initialization
// ============================================================

void layers_init(void) {
    ESP_LOGI(TAG, "Initializing layered exploration...");

    memset(&state, 0, sizeof(state));

    // Initialize layers with different time constants
    layer_init(&state.layers[0], 0.99f, 16, "SLOW");   // Slow: sees trends
    layer_init(&state.layers[1], 0.90f, 4,  "MED");    // Medium: sees patterns
    layer_init(&state.layers[2], 0.50f, 1,  "FAST");   // Fast: sees spikes

    // Initialize memory
    lmem_init(&state.memory);

    // Initialize crystals (loads from NVS)
    crystal_init();

    // Initialize temperature
    temp_init();

    // Initialize ADC
    for (int i = 0; i < NUM_ADC_IN; i++) {
        adc_channel_init(&state.adc_handles[i], ADC_CHANNELS[i], ADC_ATTEN_DB_12);
    }

    // Configure outputs
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        gpio_set_output(OUTPUT_PINS[i]);
        gpio_write(OUTPUT_PINS[i], 0);
        state.output_states[i] = 0;
    }

    // Configure LED
    gpio_set_output(PIN_LED);

    // Configure inputs
    for (int i = 0; i < NUM_GPIO_IN; i++) {
        gpio_set_input(INPUT_PINS[i]);
    }

    // Configure button
    gpio_set_input(PIN_BUTTON);

    ESP_LOGI(TAG, "Layers initialized:");
    ESP_LOGI(TAG, "  L0 (SLOW):  tau=0.99, window=16");
    ESP_LOGI(TAG, "  L1 (MED):   tau=0.90, window=4");
    ESP_LOGI(TAG, "  L2 (FAST):  tau=0.50, window=1");
}

// ============================================================
// Read All Inputs
// ============================================================

void read_inputs(layered_state_t* s) {
    // GPIO inputs
    for (int i = 0; i < NUM_GPIO_IN; i++) {
        s->gpio_in[i] = gpio_read(INPUT_PINS[i]) ? 1000 : 0;
    }

    // ADC inputs
    for (int i = 0; i < NUM_ADC_IN; i++) {
        s->adc_in[i] = (int16_t)adc_read(&s->adc_handles[i]);
    }

    // Temperature
    s->temp_in = temp_read();
}

void pack_inputs(layered_state_t* s, int16_t* out) {
    for (int i = 0; i < NUM_GPIO_IN; i++) {
        out[i] = s->gpio_in[i];
    }
    for (int i = 0; i < NUM_ADC_IN; i++) {
        out[NUM_GPIO_IN + i] = s->adc_in[i];
    }
    out[NUM_GPIO_IN + NUM_ADC_IN] = s->temp_in;
}

// ============================================================
// Main Tick
// ============================================================

void layers_tick(void) {
    state.tick++;
    bool trace = (state.tick % 10 == 0);

    // 1. Read current inputs (before)
    int16_t before[NUM_INPUTS];
    read_inputs(&state);
    pack_inputs(&state, before);

    // 2. Each layer analyzes the current state
    for (int l = 0; l < NUM_LAYERS; l++) {
        layer_analyze(&state.layers[l], &state);
    }

    // 3. Aggregate scores
    aggregate_scores(&state);

    // 4. Track consensus
    track_consensus(&state);

    // 5. Choose output
    state.chosen_output = choose_output(&state);
    state.chosen_state = !state.output_states[state.chosen_output];

    // 6. Check for stuck behavior
    if (check_stuck(&state, state.chosen_output)) {
        ESP_LOGW(TAG, "STUCK DETECTED: same output 10x in a row!");
    }

    // TRACE: Layer scores
    if (trace) {
        printf("\n[%"PRIu32"] LAYER SCORES:\n", state.tick);
        for (int l = 0; l < NUM_LAYERS; l++) {
            printf("  %s: ", state.layers[l].name);
            for (int o = 0; o < NUM_OUTPUTS; o++) {
                printf("G%d=%.0f ", OUTPUT_PINS[o], state.layers[l].scores[o]);
            }
            printf("\n");
        }

        printf("[%"PRIu32"] FINAL: ", state.tick);
        for (int o = 0; o < NUM_OUTPUTS; o++) {
            printf("G%d=%.0f ", OUTPUT_PINS[o], state.final_scores[o]);
        }
        printf("\n");

        printf("[%"PRIu32"] CHOSE: GPIO %d → %s\n",
               state.tick, OUTPUT_PINS[state.chosen_output],
               state.chosen_state ? "HIGH" : "LOW");
    }

    // 7. Execute action
    uint8_t pin = OUTPUT_PINS[state.chosen_output];
    gpio_write(pin, state.chosen_state);
    state.output_states[state.chosen_output] = state.chosen_state;
    state.output_counts[state.chosen_output]++;

    // 8. Wait for physical effect
    delay_us(1000);

    // 9. Read inputs (after)
    int16_t after[NUM_INPUTS];
    read_inputs(&state);
    pack_inputs(&state, after);

    // 10. Compute deltas
    int16_t deltas[NUM_INPUTS];
    for (int i = 0; i < NUM_INPUTS; i++) {
        deltas[i] = after[i] - before[i];
    }

    // TRACE: Significant deltas
    if (trace) {
        bool any_delta = false;
        for (int i = 0; i < NUM_INPUTS; i++) {
            if (abs(deltas[i]) > 10) {
                if (!any_delta) {
                    printf("[%"PRIu32"] DELTAS: ", state.tick);
                    any_delta = true;
                }
                if (i < NUM_GPIO_IN) {
                    printf("GPIO%d=%d ", INPUT_PINS[i], deltas[i]);
                } else if (i < NUM_GPIO_IN + NUM_ADC_IN) {
                    printf("ADC%d=%d ", i - NUM_GPIO_IN, deltas[i]);
                } else {
                    printf("TEMP=%d ", deltas[i]);
                }
            }
        }
        if (any_delta) printf("\n");
    }

#if STREAM_MODE
    // Stream binary packet every tick
    {
        stream_packet_t pkt;
        int16_t adc_deltas[4] = {
            deltas[NUM_GPIO_IN + 0],
            deltas[NUM_GPIO_IN + 1],
            deltas[NUM_GPIO_IN + 2],
            deltas[NUM_GPIO_IN + 3]
        };
        float agree_pct = (float)state.agreements / (state.tick * 8 + 1);
        float disagree_pct = (float)state.disagreements / (state.tick * 8 + 1);

        stream_pack(&pkt, state.tick,
                    state.layers[0].scores,
                    state.layers[1].scores,
                    state.layers[2].scores,
                    state.chosen_output,
                    state.chosen_state,
                    agree_pct,
                    disagree_pct,
                    adc_deltas,
                    state.output_counts);
        stream_send(&pkt);
    }
#endif

    // 11. Update all layers
    for (int l = 0; l < NUM_LAYERS; l++) {
        layer_update(&state.layers[l], state.chosen_output, deltas);
    }

    // 12. Try to crystallize strong correlations (use slow layer stats)
    layer_t* slow = &state.layers[0];
    for (int i = 0; i < NUM_INPUTS; i++) {
        crystal_try(state.chosen_output, i,
                    slow->ema[state.chosen_output][i],
                    slow->var[state.chosen_output][i],
                    state.output_counts[state.chosen_output]);
    }

    // 13. Save crystals periodically (every 100 ticks)
    if (state.tick % 100 == 0) {
        crystal_save();
    }

    // 14. Push to memory
    lmem_push(&state.memory, state.chosen_output, state.chosen_state,
             deltas, state.tick);

    // 15. Blink LED
    if (state.tick % 5 == 0) {
        gpio_toggle(PIN_LED);
    }
}

// ============================================================
// Print Stats
// ============================================================

void print_stats(void) {
    printf("\n=== LAYERED EXPLORATION STATS ===\n");
    printf("Tick: %"PRIu32"\n", state.tick);
    printf("Agreements: %"PRIu32"\n", state.agreements);
    printf("Disagreements: %"PRIu32"\n", state.disagreements);
    printf("Temperature: %.2f C\n", state.temp_in / 100.0f);

    printf("\nOutput counts:\n");
    for (int o = 0; o < NUM_OUTPUTS; o++) {
        printf("  GPIO %d: %"PRIu32"\n", OUTPUT_PINS[o], state.output_counts[o]);
    }

    printf("\nLayer entropy (exploration remaining):\n");
    for (int l = 0; l < NUM_LAYERS; l++) {
        printf("  %s: ", state.layers[l].name);
        for (int o = 0; o < NUM_OUTPUTS; o++) {
            printf("%.0f ", state.layers[l].entropy[o]);
        }
        printf("\n");
    }

    // Print crystallized knowledge
    crystal_print_all();
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n");
    printf("============================================================\n");
    printf("       LAYERED EXPLORATION - ESP32-C6 @ 160MHz\n");
    printf("============================================================\n");
    printf("\nThree observers. Different time scales. Disagreement is signal.\n\n");
    fflush(stdout);

    // Initialize
    reflex_c6_init();
    reflex_led_init();
    layers_init();

    printf("\nStarting exploration at 10 Hz (bare metal timer)...\n");
    printf("Press BOOT button to see stats.\n\n");
    fflush(stdout);

    // Initialize bare metal tick timer: 100ms = 100000us
    timer_channel_init(&tick_timer, 0, 0, TICK_MS * 1000);

    uint32_t last_stats = 0;
    bool last_button = gpio_read(PIN_BUTTON);

    while (1) {
        // Wait for tick period (bare metal busy-wait)
        timer_wait(&tick_timer);

        // Run one tick
        layers_tick();

        // Print stats every 100 ticks or on button press
        bool button = gpio_read(PIN_BUTTON);
        if (state.tick - last_stats >= 100 || (button != last_button && !button)) {
            print_stats();
            last_stats = state.tick;
        }
        last_button = button;
    }
}
