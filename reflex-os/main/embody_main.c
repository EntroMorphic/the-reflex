/**
 * embody_main.c - Embodied Self-Discovery Demo
 *
 * The ESP32-C6 discovers its own hardware through exploration.
 * Navigation, not optimization. Meaning discovered, not assigned.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "reflex_c6.h"
#include "reflex_embody.h"

static const char* TAG = "EMBODY";

// ============================================================
// Initialization
// ============================================================

void embody_init(embodied_state_t* state) {
    ESP_LOGI(TAG, "Initializing embodied self-discovery...");

    // Clear state
    memset(state, 0, sizeof(embodied_state_t));

    // Initialize models
    forward_init(&state->forward);
    backward_init(&state->backward);
    memory_init(&state->memory);
    entropy_init(&state->entropy);

    // Initialize temperature sensor
    embody_temp_init();

    // Initialize ADC channels
    for (int i = 0; i < NUM_ADC_INPUTS; i++) {
        adc_channel_init(&state->adc_channels[i],
                         EXPLORE_ADC_CHANNELS[i],
                         ADC_ATTEN_DB_12);
    }

    // Configure output pins
    for (int i = 0; i < NUM_EXPLORE_OUTPUTS; i++) {
        gpio_set_output(EXPLORE_OUTPUT_PINS[i]);
        gpio_write(EXPLORE_OUTPUT_PINS[i], 0);  // Start LOW
    }

    // Configure LED (special output we know about)
    gpio_set_output(PIN_LED_BUILTIN);
    gpio_write(PIN_LED_BUILTIN, 0);

    // Configure input pins
    for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
        gpio_set_input(EXPLORE_INPUT_PINS[i]);
    }

    // Configure boot button as input (external agent detector)
    gpio_set_input(PIN_BOOT_BUTTON);
    state->last_button_state = gpio_read(PIN_BOOT_BUTTON);

    // Read initial baseline
    embody_read_all(state,
                    state->baseline_gpio,
                    state->baseline_adc,
                    &state->baseline_temp);

    ESP_LOGI(TAG, "Embodied system initialized.");
    ESP_LOGI(TAG, "  Outputs: %d pins", NUM_EXPLORE_OUTPUTS);
    ESP_LOGI(TAG, "  GPIO inputs: %d pins", NUM_EXPLORE_INPUTS);
    ESP_LOGI(TAG, "  ADC inputs: %d channels", NUM_ADC_INPUTS);
    ESP_LOGI(TAG, "  Initial entropy: %"PRIu32, state->entropy.total_entropy);
    ESP_LOGI(TAG, "  Baseline temp: %.2f C", state->baseline_temp / 100.0f);
}

// ============================================================
// Read All Inputs
// ============================================================

void embody_read_all(embodied_state_t* state,
                     int16_t* gpio_out,
                     int16_t* adc_out,
                     int16_t* temp_out) {
    // Read GPIO inputs
    for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
        gpio_out[i] = gpio_read(EXPLORE_INPUT_PINS[i]) ? 1000 : 0;
    }

    // Read ADC inputs
    for (int i = 0; i < NUM_ADC_INPUTS; i++) {
        adc_out[i] = (int16_t)adc_read(&state->adc_channels[i]);
    }

    // Read temperature
    *temp_out = embody_temp_read();
}

// ============================================================
// Main Exploration Tick
// ============================================================

// Trace mode: show decision process
#define TRACE_EVERY_N  10   // Print trace every N ticks

bool embody_tick(embodied_state_t* state) {
    state->tick++;
    bool discovered = false;
    bool trace = (state->tick % TRACE_EVERY_N == 0);

    // 1. Check for external agent (button press) - BEFORE our action
    bool button_now = gpio_read(PIN_BOOT_BUTTON);
    if (button_now != state->last_button_state) {
        state->button_changes++;
        state->last_button_state = button_now;

        // External agent detected!
        ESP_LOGW(TAG, "EXTERNAL AGENT DETECTED! Button changed to %d (count: %"PRIu32")",
                 button_now, state->button_changes);

        // Record as special relationship
        if (state->num_relationships < MAX_RELATIONSHIPS) {
            relationship_t* rel = &state->relationships[state->num_relationships++];
            rel->type = REL_EXTERNAL_AGENT;
            rel->output_pin = 255;  // No output caused this
            rel->input_channel = PIN_BOOT_BUTTON;
            rel->effect_high = button_now ? 1000 : 0;
            rel->confidence = 1;
            rel->discovered_tick = state->tick;
            rel->active = true;
            discovered = true;
        }
    }

    // 2. Choose action based on entropy gradient + forward model
    // TRACE: Show entropy values to prove self-directed choice
    if (trace) {
        printf("[%"PRIu32"] ENTROPY: ", state->tick);
        for (int o = 0; o < NUM_EXPLORE_OUTPUTS; o++) {
            printf("G%d=%d ", EXPLORE_OUTPUT_PINS[o], state->entropy.output_entropy[o]);
        }
        printf("\n");
    }

    uint8_t output_idx = entropy_choose_output(&state->entropy);
    uint8_t output_pin = EXPLORE_OUTPUT_PINS[output_idx];
    uint8_t new_state = !gpio_read(output_pin);  // Toggle

    if (trace) {
        printf("[%"PRIu32"] CHOSE: GPIO %d → %s (highest entropy)\n",
               state->tick, output_pin, new_state ? "HIGH" : "LOW");
    }

    // 3. Read baseline (before action)
    int16_t gpio_before[NUM_EXPLORE_INPUTS];
    int16_t adc_before[NUM_ADC_INPUTS];
    int16_t temp_before;
    embody_read_all(state, gpio_before, adc_before, &temp_before);

    // 4. Execute action
    gpio_write(output_pin, new_state);
    state->actions_taken++;
    state->current_output_idx = output_idx;
    state->current_output_state = new_state;

    // 5. Wait for physical effects to propagate
    delay_us(SETTLE_TIME_US);

    // 6. Read after action
    int16_t gpio_after[NUM_EXPLORE_INPUTS];
    int16_t adc_after[NUM_ADC_INPUTS];
    int16_t temp_after;
    embody_read_all(state, gpio_after, adc_after, &temp_after);

    // 7. Compute deltas
    int16_t gpio_delta[NUM_EXPLORE_INPUTS];
    int16_t adc_delta[NUM_ADC_INPUTS];
    int16_t temp_delta;

    for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
        gpio_delta[i] = gpio_after[i] - gpio_before[i];
    }
    for (int i = 0; i < NUM_ADC_INPUTS; i++) {
        adc_delta[i] = adc_after[i] - adc_before[i];
    }
    temp_delta = temp_after - temp_before;

    // TRACE: Show observation vs prediction
    if (trace) {
        // What did we predict?
        int16_t pred_temp = state->forward.temp_pred[output_idx][new_state];
        printf("[%"PRIu32"] PREDICT: temp_delta=%d  OBSERVED: temp_delta=%d  ERROR=%d\n",
               state->tick, pred_temp, temp_delta, temp_delta - pred_temp);

        // Show any significant ADC changes
        for (int i = 0; i < NUM_ADC_INPUTS; i++) {
            if (abs(adc_delta[i]) > 10) {
                printf("[%"PRIu32"] ADC[%d]: before=%d after=%d delta=%d\n",
                       state->tick, i, adc_before[i], adc_after[i], adc_delta[i]);
            }
        }
    }

    // 8. Store in memory
    memory_entry_t entry = {
        .output_pin = output_pin,
        .output_state = new_state,
        .temp_before = temp_before,
        .temp_after = temp_after,
        .timestamp = reflex_cycles()
    };
    memcpy(entry.gpio_before, gpio_before, sizeof(gpio_before));
    memcpy(entry.gpio_after, gpio_after, sizeof(gpio_after));
    memcpy(entry.adc_before, adc_before, sizeof(adc_before));
    memcpy(entry.adc_after, adc_after, sizeof(adc_after));
    memory_push(&state->memory, &entry);

    // 9. Update forward model
    forward_update(&state->forward, output_idx, new_state,
                   gpio_delta, adc_delta, temp_delta);

    // TRACE: Show updated prediction after learning
    if (trace) {
        int16_t new_pred = state->forward.temp_pred[output_idx][new_state];
        uint8_t conf = state->forward.confidence[output_idx][new_state];
        printf("[%"PRIu32"] LEARNED: GPIO %d %s → temp_pred=%d (conf=%d)\n",
               state->tick, output_pin, new_state ? "HIGH" : "LOW", new_pred, conf);
    }

    // 10. Update backward model (credit assignment)
    backward_credit(&state->backward, &state->memory,
                    gpio_delta, adc_delta, temp_delta);

    // TRACE: Show credit assignment
    if (trace && abs(temp_delta) > 5) {
        printf("[%"PRIu32"] CREDIT: temp_delta=%d assigned to recent actions\n",
               state->tick, temp_delta);
    }

    // 11. Update entropy (we explored this output)
    uint16_t old_entropy = state->entropy.output_entropy[output_idx];
    entropy_explored(&state->entropy, output_idx);

    if (trace) {
        printf("[%"PRIu32"] ENTROPY UPDATE: GPIO %d: %d → %d\n",
               state->tick, output_pin, old_entropy, state->entropy.output_entropy[output_idx]);
    }

    // 12. Check for crystallization
    if (embody_check_crystallize(state)) {
        discovered = true;
    }

    // 13. Update baseline
    memcpy(state->baseline_gpio, gpio_after, sizeof(gpio_after));
    memcpy(state->baseline_adc, adc_after, sizeof(adc_after));
    state->baseline_temp = temp_after;

    return discovered;
}

// ============================================================
// Crystallization Check
// ============================================================

bool embody_check_crystallize(embodied_state_t* state) {
    bool discovered = false;

    for (int o = 0; o < NUM_EXPLORE_OUTPUTS; o++) {
        // Need enough confidence
        uint8_t conf = state->forward.confidence[o][0] +
                       state->forward.confidence[o][1];
        if (conf < CONFIDENCE_THRESH) continue;

        // Check GPIO relationships
        for (int i = 0; i < NUM_EXPLORE_INPUTS; i++) {
            int16_t effect_high = state->forward.gpio_pred[o][1][i];
            int16_t effect_low = state->forward.gpio_pred[o][0][i];

            if (abs(effect_high) > CORRELATION_THRESH ||
                abs(effect_low) > CORRELATION_THRESH) {

                // Check if already discovered
                bool already = false;
                for (int r = 0; r < state->num_relationships; r++) {
                    relationship_t* rel = &state->relationships[r];
                    if (rel->type == REL_GPIO_TO_GPIO &&
                        rel->output_pin == EXPLORE_OUTPUT_PINS[o] &&
                        rel->input_channel == i) {
                        already = true;
                        rel->last_verified = state->tick;
                        break;
                    }
                }

                if (!already && state->num_relationships < MAX_RELATIONSHIPS) {
                    relationship_t* rel = &state->relationships[state->num_relationships++];
                    rel->type = REL_GPIO_TO_GPIO;
                    rel->output_pin = EXPLORE_OUTPUT_PINS[o];
                    rel->input_channel = i;
                    rel->effect_high = effect_high;
                    rel->effect_low = effect_low;
                    rel->confidence = conf;
                    rel->discovered_tick = state->tick;
                    rel->last_verified = state->tick;
                    rel->active = true;

                    ESP_LOGW(TAG, "DISCOVERED: GPIO %d → GPIO input %d (effect: %d/%d)",
                             rel->output_pin, EXPLORE_INPUT_PINS[i],
                             effect_high, effect_low);
                    discovered = true;
                }
            }
        }

        // Check ADC relationships
        for (int i = 0; i < NUM_ADC_INPUTS; i++) {
            int16_t effect_high = state->forward.adc_pred[o][1][i];
            int16_t effect_low = state->forward.adc_pred[o][0][i];

            if (abs(effect_high) > CORRELATION_THRESH ||
                abs(effect_low) > CORRELATION_THRESH) {

                bool already = false;
                for (int r = 0; r < state->num_relationships; r++) {
                    relationship_t* rel = &state->relationships[r];
                    if (rel->type == REL_GPIO_TO_ADC &&
                        rel->output_pin == EXPLORE_OUTPUT_PINS[o] &&
                        rel->input_channel == i) {
                        already = true;
                        rel->last_verified = state->tick;
                        break;
                    }
                }

                if (!already && state->num_relationships < MAX_RELATIONSHIPS) {
                    relationship_t* rel = &state->relationships[state->num_relationships++];
                    rel->type = REL_GPIO_TO_ADC;
                    rel->output_pin = EXPLORE_OUTPUT_PINS[o];
                    rel->input_channel = i;
                    rel->effect_high = effect_high;
                    rel->effect_low = effect_low;
                    rel->confidence = conf;
                    rel->discovered_tick = state->tick;
                    rel->last_verified = state->tick;
                    rel->active = true;

                    ESP_LOGW(TAG, "DISCOVERED: GPIO %d → ADC %d (effect: %d/%d)",
                             rel->output_pin, EXPLORE_ADC_CHANNELS[i],
                             effect_high, effect_low);
                    discovered = true;
                }
            }
        }

        // Check temperature relationship
        int16_t temp_effect_high = state->forward.temp_pred[o][1];
        int16_t temp_effect_low = state->forward.temp_pred[o][0];

        if (abs(temp_effect_high) > 10 || abs(temp_effect_low) > 10) {  // 0.1°C threshold
            bool already = false;
            for (int r = 0; r < state->num_relationships; r++) {
                relationship_t* rel = &state->relationships[r];
                if (rel->type == REL_GPIO_TO_TEMP &&
                    rel->output_pin == EXPLORE_OUTPUT_PINS[o]) {
                    already = true;
                    rel->last_verified = state->tick;
                    break;
                }
            }

            if (!already && state->num_relationships < MAX_RELATIONSHIPS) {
                relationship_t* rel = &state->relationships[state->num_relationships++];
                rel->type = REL_GPIO_TO_TEMP;
                rel->output_pin = EXPLORE_OUTPUT_PINS[o];
                rel->input_channel = 0;
                rel->effect_high = temp_effect_high;
                rel->effect_low = temp_effect_low;
                rel->confidence = conf;
                rel->discovered_tick = state->tick;
                rel->last_verified = state->tick;
                rel->active = true;

                ESP_LOGW(TAG, "DISCOVERED: GPIO %d → TEMPERATURE (effect: %.2f/%.2f °C)",
                         rel->output_pin,
                         temp_effect_high / 100.0f, temp_effect_low / 100.0f);
                discovered = true;
            }
        }
    }

    return discovered;
}

// ============================================================
// Print Functions
// ============================================================

void embody_print_relationships(embodied_state_t* state) {
    printf("\n=== DISCOVERED RELATIONSHIPS ===\n");
    printf("Total: %d\n\n", state->num_relationships);

    for (int i = 0; i < state->num_relationships; i++) {
        relationship_t* rel = &state->relationships[i];
        if (!rel->active) continue;

        switch (rel->type) {
            case REL_GPIO_TO_GPIO:
                printf("[%d] GPIO %d → GPIO input %d: HIGH=%d, LOW=%d (conf=%d)\n",
                       i, rel->output_pin, EXPLORE_INPUT_PINS[rel->input_channel],
                       rel->effect_high, rel->effect_low, rel->confidence);
                break;
            case REL_GPIO_TO_ADC:
                printf("[%d] GPIO %d → ADC %d: HIGH=%d, LOW=%d (conf=%d)\n",
                       i, rel->output_pin, EXPLORE_ADC_CHANNELS[rel->input_channel],
                       rel->effect_high, rel->effect_low, rel->confidence);
                break;
            case REL_GPIO_TO_TEMP:
                printf("[%d] GPIO %d → TEMP: HIGH=%.2f°C, LOW=%.2f°C (conf=%d)\n",
                       i, rel->output_pin,
                       rel->effect_high / 100.0f, rel->effect_low / 100.0f,
                       rel->confidence);
                break;
            case REL_EXTERNAL_AGENT:
                printf("[%d] EXTERNAL AGENT detected on GPIO %d\n",
                       i, rel->input_channel);
                break;
        }
    }
    printf("\n");
}

void embody_print_state(embodied_state_t* state) {
    printf("\n=== EMBODIED STATE ===\n");
    printf("Tick: %"PRIu32"\n", state->tick);
    printf("Actions taken: %"PRIu32"\n", state->actions_taken);
    printf("Relationships discovered: %d\n", state->num_relationships);
    printf("Button changes (external agent): %"PRIu32"\n", state->button_changes);
    printf("Total entropy: %"PRIu32"\n", state->entropy.total_entropy);
    printf("Temperature: %.2f°C\n", state->baseline_temp / 100.0f);

    printf("\nForward model confidence:\n");
    for (int o = 0; o < NUM_EXPLORE_OUTPUTS; o++) {
        printf("  GPIO %d: LOW=%d HIGH=%d\n",
               EXPLORE_OUTPUT_PINS[o],
               state->forward.confidence[o][0],
               state->forward.confidence[o][1]);
    }
    printf("\n");
}

// ============================================================
// Main Entry Point
// ============================================================

void app_main(void) {
    printf("\n");
    printf("============================================================\n");
    printf("       EMBODIED SELF-DISCOVERY - ESP32-C6 @ 160MHz\n");
    printf("============================================================\n");
    printf("\nThe chip discovers its own body through exploration.\n");
    printf("Navigation, not optimization. Meaning discovered, not assigned.\n\n");
    fflush(stdout);

    // Initialize
    reflex_c6_init();
    reflex_led_init();

    static embodied_state_t state;
    embody_init(&state);

    printf("\nStarting exploration loop at %d Hz...\n", 1000 / EXPLORE_TICK_MS);
    printf("Press BOOT button (GPIO 9) to be detected as external agent.\n\n");
    fflush(stdout);

    // Main exploration loop
    uint32_t last_print = 0;

    while (1) {
        // Run one exploration tick
        bool discovered = embody_tick(&state);

        // Blink LED to show we're alive
        if (state.tick % 10 == 0) {
            gpio_toggle(PIN_LED_BUILTIN);
        }

        // Print status periodically
        if (state.tick - last_print >= 100) {  // Every 10 seconds
            embody_print_state(&state);
            embody_print_relationships(&state);
            last_print = state.tick;
        }

        // If we discovered something, print immediately
        if (discovered) {
            embody_print_relationships(&state);
        }

        // Wait for next tick
        vTaskDelay(pdMS_TO_TICKS(EXPLORE_TICK_MS));
    }
}
