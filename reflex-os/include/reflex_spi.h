/**
 * reflex_spi.h - SPI as Channels for ESP32-C6
 *
 * SPI is a bidirectional channel pair:
 * - TX channel: we signal data to send
 * - RX channel: peripheral signals data received
 * - Handshake: signal go, wait for completion
 *
 * Uses ESP-IDF SPI master driver for setup, exposes channel semantics.
 */

#ifndef REFLEX_SPI_H
#define REFLEX_SPI_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex.h"

// ESP-IDF SPI includes
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// ESP32-C6 SPI Pins (SPI2 - the general-purpose SPI)
// ============================================================

// Default pins (can be overridden)
#define SPI_DEFAULT_MOSI    7
#define SPI_DEFAULT_MISO    2
#define SPI_DEFAULT_SCLK    6
#define SPI_DEFAULT_CS      10

// ============================================================
// SPI Channel Structure
// ============================================================

typedef struct {
    spi_device_handle_t handle;     // ESP-IDF device handle
    spi_host_device_t host;         // SPI host (SPI2_HOST on C6)

    // Channel semantics
    reflex_channel_t tx_channel;    // Outbound data channel
    reflex_channel_t rx_channel;    // Inbound data channel
    reflex_channel_t status;        // Transaction status

    // Buffers for DMA
    uint8_t* tx_buf;
    uint8_t* rx_buf;
    size_t buf_size;

    // Stats
    uint32_t transactions;
    uint32_t errors;
} reflex_spi_channel_t;

// ============================================================
// SPI Channel API
// ============================================================

/**
 * Initialize SPI channel with default pins
 *
 * @param spi       SPI channel structure
 * @param cs_pin    Chip select GPIO pin
 * @param speed_hz  Clock speed in Hz (max ~80MHz on C6)
 * @param mode      SPI mode (0-3)
 */
static inline esp_err_t spi_channel_init(reflex_spi_channel_t* spi,
                                          int cs_pin,
                                          int speed_hz,
                                          int mode) {
    memset(spi, 0, sizeof(*spi));
    spi->host = SPI2_HOST;
    spi->buf_size = 64;  // Default buffer size

    // Allocate DMA buffers (must be DMA-capable memory)
    spi->tx_buf = heap_caps_malloc(spi->buf_size, MALLOC_CAP_DMA);
    spi->rx_buf = heap_caps_malloc(spi->buf_size, MALLOC_CAP_DMA);
    if (!spi->tx_buf || !spi->rx_buf) {
        return ESP_ERR_NO_MEM;
    }

    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_DEFAULT_MOSI,
        .miso_io_num = SPI_DEFAULT_MISO,
        .sclk_io_num = SPI_DEFAULT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = spi->buf_size,
    };

    esp_err_t ret = spi_bus_initialize(spi->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;  // ESP_ERR_INVALID_STATE means already initialized
    }

    // Add device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = speed_hz,
        .mode = mode,
        .spics_io_num = cs_pin,
        .queue_size = 1,
        .flags = 0,
    };

    ret = spi_bus_add_device(spi->host, &dev_cfg, &spi->handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // Initialize channels
    spi->tx_channel.sequence = 0;
    spi->rx_channel.sequence = 0;
    spi->status.sequence = 0;
    spi->status.value = 0;  // 0 = idle

    return ESP_OK;
}

/**
 * Initialize SPI with custom pins
 */
static inline esp_err_t spi_channel_init_pins(reflex_spi_channel_t* spi,
                                               int mosi, int miso, int sclk, int cs,
                                               int speed_hz, int mode) {
    memset(spi, 0, sizeof(*spi));
    spi->host = SPI2_HOST;
    spi->buf_size = 64;

    spi->tx_buf = heap_caps_malloc(spi->buf_size, MALLOC_CAP_DMA);
    spi->rx_buf = heap_caps_malloc(spi->buf_size, MALLOC_CAP_DMA);
    if (!spi->tx_buf || !spi->rx_buf) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = spi->buf_size,
    };

    esp_err_t ret = spi_bus_initialize(spi->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = speed_hz,
        .mode = mode,
        .spics_io_num = cs,
        .queue_size = 1,
    };

    ret = spi_bus_add_device(spi->host, &dev_cfg, &spi->handle);
    if (ret != ESP_OK) {
        return ret;
    }

    spi->tx_channel.sequence = 0;
    spi->rx_channel.sequence = 0;
    spi->status.sequence = 0;

    return ESP_OK;
}

// ============================================================
// SPI Transaction as Channel Signal
// ============================================================

/**
 * Transfer data: signal TX, wait, read RX
 *
 * This is the fundamental SPI reflex:
 * 1. Signal TX channel with data
 * 2. Hardware performs transfer
 * 3. RX channel signals with response
 *
 * @param spi       SPI channel
 * @param tx_data   Data to send (can be NULL for read-only)
 * @param rx_data   Buffer for received data (can be NULL for write-only)
 * @param len       Number of bytes
 * @return          ESP_OK on success
 */
static inline esp_err_t spi_transfer(reflex_spi_channel_t* spi,
                                      const uint8_t* tx_data,
                                      uint8_t* rx_data,
                                      size_t len) {
    if (len > spi->buf_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Signal status: busy
    reflex_signal(&spi->status, 1);

    // Prepare TX buffer
    if (tx_data) {
        memcpy(spi->tx_buf, tx_data, len);
        reflex_signal(&spi->tx_channel, len);
    }

    // Prepare transaction
    spi_transaction_t trans = {
        .length = len * 8,  // In bits
        .tx_buffer = tx_data ? spi->tx_buf : NULL,
        .rx_buffer = rx_data ? spi->rx_buf : NULL,
    };

    // Execute (blocking)
    esp_err_t ret = spi_device_polling_transmit(spi->handle, &trans);

    if (ret == ESP_OK) {
        spi->transactions++;

        // Copy RX data and signal
        if (rx_data) {
            memcpy(rx_data, spi->rx_buf, len);
            reflex_signal(&spi->rx_channel, len);
        }
    } else {
        spi->errors++;
    }

    // Signal status: idle
    reflex_signal(&spi->status, 0);

    return ret;
}

/**
 * Write-only transfer (TX channel signal)
 */
static inline esp_err_t spi_write(reflex_spi_channel_t* spi,
                                   const uint8_t* data, size_t len) {
    return spi_transfer(spi, data, NULL, len);
}

/**
 * Read-only transfer (RX channel signal)
 */
static inline esp_err_t spi_read(reflex_spi_channel_t* spi,
                                  uint8_t* data, size_t len) {
    return spi_transfer(spi, NULL, data, len);
}

/**
 * Single byte transfer (most common case)
 */
static inline uint8_t spi_transfer_byte(reflex_spi_channel_t* spi, uint8_t tx) {
    uint8_t rx;
    spi_transfer(spi, &tx, &rx, 1);
    return rx;
}

/**
 * Write single byte
 */
static inline void spi_write_byte(reflex_spi_channel_t* spi, uint8_t data) {
    spi_transfer(spi, &data, NULL, 1);
}

/**
 * Read single byte
 */
static inline uint8_t spi_read_byte(reflex_spi_channel_t* spi) {
    uint8_t rx;
    spi_transfer(spi, NULL, &rx, 1);
    return rx;
}

// ============================================================
// Register Access Pattern (common for sensors)
// ============================================================

/**
 * Write to a register (common SPI sensor pattern)
 * Sends: [reg_addr] [data...]
 */
static inline esp_err_t spi_write_reg(reflex_spi_channel_t* spi,
                                       uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {reg, value};
    return spi_write(spi, tx, 2);
}

/**
 * Read from a register
 * Sends: [reg_addr | 0x80] and reads response
 */
static inline uint8_t spi_read_reg(reflex_spi_channel_t* spi, uint8_t reg) {
    uint8_t tx[2] = {reg | 0x80, 0x00};  // Read flag
    uint8_t rx[2];
    spi_transfer(spi, tx, rx, 2);
    return rx[1];
}

// ============================================================
// Channel Status Helpers
// ============================================================

/**
 * Check if SPI is busy
 */
static inline bool spi_is_busy(reflex_spi_channel_t* spi) {
    return spi->status.value != 0;
}

/**
 * Get transaction count
 */
static inline uint32_t spi_get_transactions(reflex_spi_channel_t* spi) {
    return spi->transactions;
}

/**
 * Get error count
 */
static inline uint32_t spi_get_errors(reflex_spi_channel_t* spi) {
    return spi->errors;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_SPI_H
