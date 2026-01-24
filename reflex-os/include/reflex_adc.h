/**
 * reflex_adc.h - ADC as Channels for ESP32-C6
 *
 * ADC channels convert continuous analog signals to discrete values.
 * The external world is the producer - we just read the signal.
 *
 * ESP32-C6 has SAR ADC with 7 channels on ADC1.
 * Uses ESP-IDF oneshot ADC driver for setup, then fast reads.
 */

#ifndef REFLEX_ADC_H
#define REFLEX_ADC_H

#include <stdint.h>
#include <stdbool.h>
#include "reflex.h"

// ESP-IDF ADC includes
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 ADC Channel Mapping
// ============================================================

// ADC1 channels available on ESP32-C6
// GPIO to ADC channel mapping:
//   GPIO0 -> ADC1_CH0
//   GPIO1 -> ADC1_CH1
//   GPIO2 -> ADC1_CH2
//   GPIO3 -> ADC1_CH3
//   GPIO4 -> ADC1_CH4
//   GPIO5 -> ADC1_CH5
//   GPIO6 -> ADC1_CH6

#define ADC_CHANNEL_0   ADC_CHANNEL_0  // GPIO0
#define ADC_CHANNEL_1   ADC_CHANNEL_1  // GPIO1
#define ADC_CHANNEL_2   ADC_CHANNEL_2  // GPIO2
#define ADC_CHANNEL_3   ADC_CHANNEL_3  // GPIO3
#define ADC_CHANNEL_4   ADC_CHANNEL_4  // GPIO4
#define ADC_CHANNEL_5   ADC_CHANNEL_5  // GPIO5
#define ADC_CHANNEL_6   ADC_CHANNEL_6  // GPIO6

// ============================================================
// ADC Unit Handle (shared across channels)
// ============================================================

static adc_oneshot_unit_handle_t adc1_handle = NULL;

/**
 * Initialize ADC1 unit (call once before using any ADC channels)
 */
static inline void adc_unit_init(void) {
    if (adc1_handle != NULL) return;  // Already initialized

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
}

// ============================================================
// ADC Channel Structure
// ============================================================

typedef struct {
    adc_channel_t channel;          // Hardware channel ID
    adc_atten_t attenuation;        // Input attenuation
    reflex_channel_t reflex;        // Reflex channel for signaling
    int last_raw;                   // Last raw reading
    int last_mv;                    // Last millivolt reading (if calibrated)
} reflex_adc_channel_t;

// ============================================================
// ADC Channel API
// ============================================================

/**
 * Initialize an ADC channel
 *
 * @param adc       ADC channel structure
 * @param channel   Hardware ADC channel (ADC_CHANNEL_0 to ADC_CHANNEL_6)
 * @param atten     Attenuation level:
 *                  - ADC_ATTEN_DB_0:   0-750mV
 *                  - ADC_ATTEN_DB_2_5: 0-1050mV
 *                  - ADC_ATTEN_DB_6:   0-1300mV
 *                  - ADC_ATTEN_DB_12:  0-2500mV (full range)
 */
static inline void adc_channel_init(reflex_adc_channel_t* adc,
                                     adc_channel_t channel,
                                     adc_atten_t atten) {
    // Ensure ADC unit is initialized
    adc_unit_init();

    adc->channel = channel;
    adc->attenuation = atten;
    adc->last_raw = 0;
    adc->last_mv = 0;
    adc->reflex.sequence = 0;
    adc->reflex.value = 0;
    adc->reflex.timestamp = 0;
    adc->reflex.flags = 0;

    // Configure the channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,  // 12-bit resolution (0-4095)
        .atten = atten,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel, &config));
}

/**
 * Read ADC channel (blocking)
 * Returns raw 12-bit value (0-4095)
 *
 * This is the hot path - direct hardware read.
 */
static inline int adc_read(reflex_adc_channel_t* adc) {
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, adc->channel, &raw));
    adc->last_raw = raw;
    return raw;
}

/**
 * Read ADC and signal the reflex channel
 * Use this when you want observers to be notified of new readings.
 */
static inline int adc_read_signal(reflex_adc_channel_t* adc) {
    int raw = adc_read(adc);
    reflex_signal(&adc->reflex, (uint32_t)raw);
    return raw;
}

/**
 * Convert raw ADC value to millivolts (approximate)
 * Based on attenuation setting.
 */
static inline int adc_raw_to_mv(reflex_adc_channel_t* adc, int raw) {
    // Approximate conversion based on attenuation
    // These are nominal values; for accuracy, use ESP-IDF calibration
    int max_mv;
    switch (adc->attenuation) {
        case ADC_ATTEN_DB_0:   max_mv = 750;  break;
        case ADC_ATTEN_DB_2_5: max_mv = 1050; break;
        case ADC_ATTEN_DB_6:   max_mv = 1300; break;
        case ADC_ATTEN_DB_12:  max_mv = 2500; break;
        default:               max_mv = 2500; break;
    }
    return (raw * max_mv) / 4095;
}

/**
 * Read ADC and convert to millivolts
 */
static inline int adc_read_mv(reflex_adc_channel_t* adc) {
    int raw = adc_read(adc);
    adc->last_mv = adc_raw_to_mv(adc, raw);
    return adc->last_mv;
}

// ============================================================
// Convenience: Quick single-shot read without channel struct
// ============================================================

/**
 * Quick read from an ADC channel (initializes if needed)
 * Good for simple cases; for hot paths, use adc_channel_init + adc_read
 */
static inline int adc_quick_read(adc_channel_t channel) {
    adc_unit_init();

    // Configure channel for single read
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  // Full range
    };
    adc_oneshot_config_channel(adc1_handle, channel, &config);

    int raw;
    adc_oneshot_read(adc1_handle, channel, &raw);
    return raw;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_ADC_H
