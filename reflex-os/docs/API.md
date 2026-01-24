# Reflex OS API Reference

Complete API documentation for all Reflex OS headers.

---

## Table of Contents

1. [reflex.h - Core Primitive](#reflexh---core-primitive)
2. [reflex_gpio.h - GPIO Channels](#reflex_gpioh---gpio-channels)
3. [reflex_timer.h - Timer Channels](#reflex_timerh---timer-channels)
4. [reflex_adc.h - ADC Channels](#reflex_adch---adc-channels)
5. [reflex_spline.h - Spline Channels](#reflex_splineh---spline-channels)
6. [reflex_spi.h - SPI Channels](#reflex_spih---spi-channels)
7. [reflex_wifi.h - WiFi Channels](#reflex_wifih---wifi-channels)
8. [reflex_void.h - Entropy Field](#reflex_voidh---entropy-field)
9. [reflex_c6.h - Master Header](#reflex_c6h---master-header)

---

## reflex.h - Core Primitive

The foundation of Reflex OS. 50 lines. 118ns.

### Types

#### `reflex_channel_t`

```c
typedef struct {
    volatile uint32_t sequence;   // Monotonic counter (writer increments)
    volatile uint32_t timestamp;  // Cycle count when written
    volatile uint32_t value;      // Primary payload
    volatile uint32_t flags;      // Application-defined flags
    uint32_t _pad[4];             // Pad to 32 bytes
} __attribute__((aligned(32))) reflex_channel_t;
```

### Macros

#### `REFLEX_FENCE()`
Hardware memory fence. Ensures all prior reads/writes complete before subsequent ones.

#### `REFLEX_COMPILER_BARRIER()`
Compiler-only barrier. Prevents reordering without hardware fence.

### Functions

#### `reflex_cycles()`
```c
uint32_t reflex_cycles(void);
```
Read CPU cycle counter. 160MHz = 6.25ns per cycle.

**Returns:** Current cycle count (32-bit, wraps every ~27 seconds).

---

#### `reflex_signal()`
```c
void reflex_signal(reflex_channel_t* ch, uint32_t val);
```
Signal a new value on the channel.

**Parameters:**
- `ch` - Channel to signal
- `val` - Value to write

**Latency:** 118ns (19 cycles)

**Behavior:**
1. Write value
2. Write timestamp (current cycles)
3. Memory fence
4. Increment sequence
5. Memory fence

---

#### `reflex_signal_ts()`
```c
void reflex_signal_ts(reflex_channel_t* ch, uint32_t val, uint32_t ts);
```
Signal with explicit timestamp.

**Parameters:**
- `ch` - Channel to signal
- `val` - Value to write
- `ts` - Timestamp to use

---

#### `reflex_wait()`
```c
uint32_t reflex_wait(reflex_channel_t* ch, uint32_t last_seq);
```
Spin until sequence changes (blocking).

**Parameters:**
- `ch` - Channel to wait on
- `last_seq` - Last seen sequence number

**Returns:** New sequence number.

---

#### `reflex_wait_timeout()`
```c
uint32_t reflex_wait_timeout(reflex_channel_t* ch, uint32_t last_seq, uint32_t timeout_cycles);
```
Wait with timeout.

**Parameters:**
- `ch` - Channel to wait on
- `last_seq` - Last seen sequence number
- `timeout_cycles` - Maximum cycles to wait

**Returns:** New sequence number, or 0 if timeout.

---

#### `reflex_try_wait()`
```c
uint32_t reflex_try_wait(reflex_channel_t* ch, uint32_t last_seq);
```
Non-blocking check for new signal.

**Parameters:**
- `ch` - Channel to check
- `last_seq` - Last seen sequence number

**Returns:** New sequence number if changed, 0 if unchanged.

---

#### `reflex_read()`
```c
uint32_t reflex_read(reflex_channel_t* ch);
```
Read current value from channel.

**Returns:** Channel value.

---

#### `reflex_read_timestamp()`
```c
uint32_t reflex_read_timestamp(reflex_channel_t* ch);
```
Read timestamp of last signal.

**Returns:** Cycle count when last signaled.

---

#### `reflex_latency()`
```c
uint32_t reflex_latency(reflex_channel_t* ch);
```
Calculate cycles since last signal.

**Returns:** Current cycles - channel timestamp.

---

#### `reflex_cycles_to_ns()`
```c
uint32_t reflex_cycles_to_ns(uint32_t cycles);
```
Convert cycles to nanoseconds (at 160MHz).

---

#### `reflex_ns_to_cycles()`
```c
uint32_t reflex_ns_to_cycles(uint32_t ns);
```
Convert nanoseconds to cycles (at 160MHz).

---

## reflex_gpio.h - GPIO Channels

Direct register access for minimum latency.

### Constants

```c
#define GPIO_BASE           0x60091000
#define GPIO_OUT_REG        (GPIO_BASE + 0x0004)
#define GPIO_OUT_W1TS_REG   (GPIO_BASE + 0x0008)  // Write 1 to set
#define GPIO_OUT_W1TC_REG   (GPIO_BASE + 0x000C)  // Write 1 to clear
#define GPIO_IN_REG         (GPIO_BASE + 0x003C)
```

### Functions

#### `gpio_set_output()`
```c
void gpio_set_output(uint8_t pin);
```
Configure pin as output.

---

#### `gpio_set_input()`
```c
void gpio_set_input(uint8_t pin);
```
Configure pin as input.

---

#### `gpio_write()`
```c
void gpio_write(uint8_t pin, bool value);
```
Write to output pin.

**Latency:** 12ns (2 cycles)

---

#### `gpio_set()` / `gpio_clear()`
```c
void gpio_set(uint8_t pin);    // Set high
void gpio_clear(uint8_t pin);  // Set low
```

---

#### `gpio_toggle()`
```c
void gpio_toggle(uint8_t pin);
```
Toggle output pin.

---

#### `gpio_read()`
```c
bool gpio_read(uint8_t pin);
```
Read input pin state.

---

#### `delay_cycles()`
```c
void delay_cycles(uint32_t cycles);
```
Busy-wait for specified cycles.

---

## reflex_timer.h - Timer Channels

Cycle-based timing for deterministic loops.

### Types

#### `reflex_timer_channel_t`
```c
typedef struct {
    uint32_t period_cycles;  // Cycles per period
    uint32_t last_cycles;    // Last trigger time
    uint64_t count;          // Total ticks
} reflex_timer_channel_t;
```

### Functions

#### `timer_channel_init()`
```c
void timer_channel_init(reflex_timer_channel_t* timer,
                        int group, int idx, uint32_t period_us);
```
Initialize timer with period in microseconds.

---

#### `timer_wait()`
```c
void timer_wait(reflex_timer_channel_t* timer);
```
Wait for next timer tick (blocking).

---

#### `timer_check()`
```c
bool timer_check(reflex_timer_channel_t* timer);
```
Check if timer tick occurred (non-blocking).

---

#### `timer_get_count()`
```c
uint64_t timer_get_count(reflex_timer_channel_t* timer);
```
Get total tick count.

---

## reflex_adc.h - ADC Channels

Analog input as channels.

### Types

#### `reflex_adc_channel_t`
```c
typedef struct {
    adc_oneshot_unit_handle_t handle;
    adc_channel_t channel;
    reflex_channel_t base;
    int32_t last_raw;
    uint32_t sample_count;
} reflex_adc_channel_t;
```

### Functions

#### `adc_channel_init()`
```c
esp_err_t adc_channel_init(reflex_adc_channel_t* adc,
                            adc_channel_t channel,
                            adc_atten_t atten);
```
Initialize ADC channel.

**Attenuation options:**
- `ADC_ATTEN_DB_0` - 0-750mV
- `ADC_ATTEN_DB_2_5` - 0-1050mV
- `ADC_ATTEN_DB_6` - 0-1300mV
- `ADC_ATTEN_DB_12` - 0-2500mV (recommended)

---

#### `adc_read()`
```c
int adc_read(reflex_adc_channel_t* adc);
```
Read ADC value (blocking).

**Returns:** 12-bit raw value (0-4095).

**Latency:** 21us

---

#### `adc_read_mv()`
```c
int adc_read_mv(reflex_adc_channel_t* adc);
```
Read ADC value in millivolts.

---

#### `adc_raw_to_mv()`
```c
int adc_raw_to_mv(reflex_adc_channel_t* adc, int raw);
```
Convert raw value to millivolts.

---

## reflex_spline.h - Spline Channels

Catmull-Rom interpolation between control points.

### Types

#### `reflex_spline_channel_t`
```c
typedef struct {
    int32_t values[4];      // Control points (circular buffer)
    uint32_t times[4];      // Timestamps
    uint8_t head;           // Next write position
    uint8_t count;          // Valid points
    reflex_channel_t base;  // Underlying channel
} reflex_spline_channel_t;
```

### Functions

#### `spline_init()`
```c
void spline_init(reflex_spline_channel_t* sp);
```
Initialize empty spline.

---

#### `spline_init_at()`
```c
void spline_init_at(reflex_spline_channel_t* sp, int32_t value);
```
Initialize with starting value (fills history).

---

#### `spline_signal()`
```c
void spline_signal(reflex_spline_channel_t* sp, int32_t value);
```
Add new control point.

---

#### `spline_read()`
```c
int32_t spline_read(reflex_spline_channel_t* sp);
```
Read interpolated value at current time.

**Latency:** 137ns (22 cycles)

---

#### `spline_velocity()`
```c
int32_t spline_velocity(reflex_spline_channel_t* sp);
```
Get rate of change (per 1000 cycles).

---

#### `spline_predict()`
```c
int32_t spline_predict(reflex_spline_channel_t* sp, uint32_t future_cycles);
```
Extrapolate future value.

---

#### Float Convenience Functions

```c
void spline_signal_f(sp, float value, int32_t scale);
float spline_read_f(sp, int32_t scale);
```

---

## reflex_spi.h - SPI Channels

Bidirectional protocol channels.

### Types

#### `reflex_spi_channel_t`
```c
typedef struct {
    spi_device_handle_t handle;
    spi_host_device_t host;
    reflex_channel_t tx_channel;
    reflex_channel_t rx_channel;
    reflex_channel_t status;
    uint8_t* tx_buf;
    uint8_t* rx_buf;
    size_t buf_size;
    uint32_t transactions;
    uint32_t errors;
} reflex_spi_channel_t;
```

### Functions

#### `spi_channel_init()`
```c
esp_err_t spi_channel_init(reflex_spi_channel_t* spi,
                            int cs_pin, int speed_hz, int mode);
```
Initialize SPI with default pins.

---

#### `spi_transfer()`
```c
esp_err_t spi_transfer(reflex_spi_channel_t* spi,
                        const uint8_t* tx_data,
                        uint8_t* rx_data,
                        size_t len);
```
Full-duplex transfer.

**Latency:** ~29us per byte at 1MHz

---

#### `spi_write()` / `spi_read()`
```c
esp_err_t spi_write(spi, data, len);
esp_err_t spi_read(spi, buf, len);
```
Half-duplex operations.

---

#### `spi_transfer_byte()`
```c
uint8_t spi_transfer_byte(reflex_spi_channel_t* spi, uint8_t tx);
```
Single byte transfer.

---

#### `spi_write_reg()` / `spi_read_reg()`
```c
esp_err_t spi_write_reg(spi, reg, value);
uint8_t spi_read_reg(spi, reg);
```
Register access pattern (common for sensors).

---

## reflex_wifi.h - WiFi Channels

Network events as channel signals.

### Types

#### `wifi_channel_state_t`
```c
typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_ERROR
} wifi_channel_state_t;
```

#### `reflex_wifi_channel_t`
```c
typedef struct {
    reflex_channel_t status;
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    char ssid[33];
    int8_t rssi;
    uint32_t connect_count;
    uint32_t disconnect_count;
} reflex_wifi_channel_t;
```

### Functions

#### `wifi_channel_init()`
```c
esp_err_t wifi_channel_init(reflex_wifi_channel_t* wifi,
                             const char* ssid,
                             const char* password);
```
Initialize and connect to AP.

---

#### `wifi_wait_connected()`
```c
bool wifi_wait_connected(reflex_wifi_channel_t* wifi, uint32_t timeout_ms);
```
Wait for IP address (blocking).

---

#### `wifi_is_connected()`
```c
bool wifi_is_connected(reflex_wifi_channel_t* wifi);
```
Check connection status.

---

#### `wifi_get_ip_str()`
```c
void wifi_get_ip_str(reflex_wifi_channel_t* wifi, char* buf, size_t len);
```
Get IP as string.

---

#### `wifi_get_rssi()`
```c
int8_t wifi_get_rssi(reflex_wifi_channel_t* wifi);
```
Get signal strength (dBm).

---

### UDP Channels

#### `reflex_udp_channel_t`
```c
typedef struct {
    int sock;
    struct sockaddr_in dest_addr;
    reflex_channel_t tx_channel;
    reflex_channel_t rx_channel;
} reflex_udp_channel_t;
```

#### `udp_channel_init()`
```c
esp_err_t udp_channel_init(reflex_udp_channel_t* udp,
                            const char* dest_ip,
                            uint16_t dest_port,
                            uint16_t local_port);
```

#### `udp_send()` / `udp_recv()`
```c
int udp_send(udp, data, len);
int udp_recv(udp, buf, max_len);
```

---

## reflex_void.h - Entropy Field

The computational substrate for TriX echips.

### Constants

```c
#define VOID_STATE_EMPTY       0   // Pure potential
#define VOID_STATE_CHARGING    1   // Entropy accumulating
#define VOID_STATE_CRITICAL    2   // Near crystallization
#define VOID_STATE_SHAPE       3   // Crystallized structure

#define DIR_NORTH  0
#define DIR_EAST   1
#define DIR_SOUTH  2
#define DIR_WEST   3
```

### Types

#### `reflex_entropic_channel_t`
```c
typedef struct {
    reflex_channel_t base;
    uint32_t last_signal_time;
    uint32_t entropy;
    uint32_t entropy_rate;
    uint32_t capacity;
    uint8_t state;
} reflex_entropic_channel_t;
```

#### `reflex_void_cell_t`
```c
typedef struct {
    uint32_t entropy;
    uint32_t capacity;
    int16_t gradient_x;
    int16_t gradient_y;
    uint8_t state;
    uint8_t flags;
    uint16_t age;
} reflex_void_cell_t;
```

#### `reflex_entropy_field_t`
```c
typedef struct {
    reflex_void_cell_t* cells;
    uint16_t width;
    uint16_t height;
    uint32_t tick;
    uint32_t total_entropy;
    uint32_t default_capacity;
    uint16_t diffusion_rate;
    uint16_t decay_rate;
} reflex_entropy_field_t;
```

### Entropic Channel Functions

#### `entropic_init()`
```c
void entropic_init(reflex_entropic_channel_t* ech, uint32_t capacity);
```

#### `entropic_update()`
```c
void entropic_update(reflex_entropic_channel_t* ech);
```
Accumulate entropy from silence.

#### `entropic_signal()`
```c
void entropic_signal(reflex_entropic_channel_t* ech, uint32_t value);
```
Signal and collapse entropy.

#### `entropic_is_critical()`
```c
bool entropic_is_critical(reflex_entropic_channel_t* ech);
```
Check if ready to crystallize.

### Entropy Field Functions

#### `entropy_field_init()`
```c
bool entropy_field_init(reflex_entropy_field_t* field,
                         uint16_t width, uint16_t height,
                         uint32_t default_capacity);
```

#### `entropy_deposit()`
```c
void entropy_deposit(field, uint16_t x, uint16_t y, uint32_t amount);
```
Stigmergy write - deposit entropy.

#### `entropy_read()`
```c
uint32_t entropy_read(field, uint16_t x, uint16_t y);
```
Read entropy at location.

#### `entropy_field_tick()`
```c
void entropy_field_tick(reflex_entropy_field_t* field);
```
Evolve field by one tick (diffusion, decay, gradients).

#### `entropy_crystallize()`
```c
uint32_t entropy_crystallize(field, uint16_t x, uint16_t y);
```
Convert void to shape.

### Stigmergy Functions

#### `stigmergy_write()`
```c
void stigmergy_write(field, x, y, amount);
```
Deposit entropy (leave trace).

#### `stigmergy_read()`
```c
stigmergy_sense_t stigmergy_read(field, x, y);
```
Sense local entropy and gradient.

#### `stigmergy_follow()`
```c
int8_t stigmergy_follow(field, x, y, bool toward_high);
```
Get direction along gradient.

**Returns:** DIR_NORTH/EAST/SOUTH/WEST or -1 if flat.

---

## reflex_echip.h - Self-Reconfiguring Processor

The complete TriX echip implementation.

### Constants

```c
#define ECHIP_MAX_SHAPES        4096
#define ECHIP_MAX_ROUTES        16384
#define ECHIP_MAX_PORTS         8
#define WEIGHT_SCALE            1024    // Fixed-point 1.0
```

### Types

#### `shape_type_t`
```c
typedef enum {
    SHAPE_VOID,        // Empty slot
    SHAPE_NAND,        // Universal gate
    SHAPE_NOR,         // Universal gate
    SHAPE_XOR,         // Parity
    SHAPE_BUFFER,      // Relay
    SHAPE_NOT,         // Inverter
    SHAPE_LATCH,       // D-latch
    SHAPE_TOGGLE,      // T flip-flop
    SHAPE_ADD,         // Addition
    SHAPE_SUB,         // Subtraction
    SHAPE_MUL,         // Multiplication
    SHAPE_CMP,         // Comparison
    SHAPE_MUX,         // Multiplexer
    SHAPE_DEMUX,       // Demultiplexer
    SHAPE_FANOUT,      // Signal splitter
    SHAPE_INPUT,       // External input
    SHAPE_OUTPUT,      // External output
    SHAPE_NEURON,      // Integrate-and-fire
    SHAPE_OSCILLATOR,  // Periodic signal
} shape_type_t;
```

#### `frozen_shape_t`
```c
typedef struct {
    uint16_t id;                     // Unique ID
    uint16_t x, y;                   // Position
    shape_type_t type;               // Computation type
    int16_t inputs[8];               // Input port values
    int16_t outputs[8];              // Output port values
    int32_t state;                   // Internal state
    int32_t threshold;               // Neuron threshold
    uint16_t age;                    // Ticks since creation
    uint8_t frozen;                  // Cannot dissolve
} frozen_shape_t;
```

#### `mutable_route_t`
```c
typedef struct {
    uint16_t src_shape, dst_shape;   // Connected shapes
    uint8_t src_port, dst_port;      // Connected ports
    int16_t weight;                  // Synaptic strength
    uint16_t activity;               // Usage counter
    uint8_t delay;                   // Propagation delay
    route_state_t state;             // EMPTY/DORMANT/ACTIVE/etc.
    uint8_t field_x, field_y;        // Entropy field position
} mutable_route_t;
```

#### `echip_t`
```c
typedef struct {
    frozen_shape_t* shapes;          // Shape array
    mutable_route_t* routes;         // Route array
    reflex_entropy_field_t field;    // Entropy substrate
    // Learning parameters, stats, I/O...
} echip_t;
```

### Initialization

#### `echip_init()`
```c
bool echip_init(echip_t* chip, uint16_t max_shapes,
                uint16_t max_routes, uint8_t field_size);
```
Initialize processor with given capacity.

#### `echip_free()`
```c
void echip_free(echip_t* chip);
```
Release resources.

#### `echip_alloc_io()`
```c
bool echip_alloc_io(echip_t* chip, uint8_t num_inputs, uint8_t num_outputs);
```
Allocate external I/O buffers.

### Shape Operations

#### `echip_create_shape()`
```c
uint16_t echip_create_shape(echip_t* chip, shape_type_t type,
                             uint16_t x, uint16_t y);
```
Create shape at position. Returns ID or 0.

#### `echip_find_shape()`
```c
frozen_shape_t* echip_find_shape(echip_t* chip, uint16_t id);
```
Find shape by ID.

#### `echip_dissolve_shape()`
```c
bool echip_dissolve_shape(echip_t* chip, uint16_t id);
```
Return shape to void.

### Route Operations

#### `echip_create_route()`
```c
int echip_create_route(echip_t* chip,
                        uint16_t src_shape, uint8_t src_port,
                        uint16_t dst_shape, uint8_t dst_port,
                        int16_t initial_weight);
```
Create connection. Returns index or -1.

#### `echip_dissolve_route()`
```c
void echip_dissolve_route(echip_t* chip, uint16_t route_idx);
```
Return route to void.

### Execution

#### `echip_tick()`
```c
void echip_tick(echip_t* chip);
```
**One cycle of self-composition:**
1. Propagate signals
2. Evaluate shapes
3. Hebbian learning
4. Entropy update
5. Crystallization
6. Pruning

#### `echip_set_input()` / `echip_get_output()`
```c
void echip_set_input(echip_t* chip, uint8_t idx, int16_t value);
int16_t echip_get_output(echip_t* chip, uint8_t idx);
```
External I/O access.

### Statistics

#### `echip_get_stats()`
```c
echip_stats_t echip_get_stats(echip_t* chip);

typedef struct {
    uint16_t num_shapes, num_routes;
    uint32_t shapes_created, shapes_dissolved;
    uint32_t routes_created, routes_dissolved;
    uint32_t signals_propagated;
    uint32_t total_entropy;
    uint64_t tick;
} echip_stats_t;
```

---

## reflex_c6.h - Master Header

Include this one header to access everything.

```c
#include "reflex_c6.h"  // Includes all reflex headers
```

### Constants

```c
#define REFLEX_CHIP_NAME    "ESP32-C6"
#define REFLEX_CHIP_FREQ    160000000   // 160 MHz
#define REFLEX_CYCLE_NS     6           // ~6.25 ns per cycle
#define PIN_LED             8           // Onboard LED
#define PIN_BOOT            9           // Boot button
```

### Convenience Functions

```c
void reflex_c6_init(void);      // Initialize hardware
void reflex_led_init(void);     // Setup LED pin
void reflex_led_toggle(void);   // Toggle LED
void reflex_led_set(bool on);   // Set LED
```

---

*Every peripheral is a channel. The hardware already thinks in signals.*
