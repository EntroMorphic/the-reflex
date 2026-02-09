# Key Nodes: The Reflex Becomes the C6

> Phase 2: Index the insights. What are the nodes in this manifold?

---

## Node 1: Hardware Registers Are Already Channels
Memory-mapped I/O is channel semantics with different syntax. Write to TX register = signal. Read from RX register = wait. Status bit = sequence number. We're not adding abstraction - we're recognizing what already exists.

## Node 2: Peripheral Space as Channel Topology
The C6 memory map from 0x6000_0000 onward IS a channel topology. Each peripheral base address is a channel group. Each register offset is a channel within that group. The datasheet is a channel catalog.

## Node 3: Asymmetric Cores as Control/Data Plane
HP core (160MHz) = data plane. Runs hot paths, real-time control, never interrupted.
LP core (20MHz) = control plane. Manages slow peripherals, handles complexity, signals HP when needed.
This isn't a workaround - it's the intended architecture. We're aligning with hardware intent.

## Node 4: LP as the Sentinel
LP core can monitor channels while HP sleeps. When a channel signals something relevant (packet arrives, timer expires, GPIO changes), LP wakes HP with a single-cycle signal. This is a hardware-level reflex: stimulus → response, minimal latency.

## Node 5: Interrupts as Involuntary Reflexes
An interrupt is a reflex the hardware forces on us. But we can choose to only accept them on LP core. HP core remains in pure polling mode - voluntary reflexes only. This gives HP deterministic timing.

## Node 6: GPIO as 22 Channels
Each pin is a channel:
- Input pin: hardware writes (external world), we read
- Output pin: we write, hardware reads (drives external world)
Pullups, modes, etc. are channel configuration, not channel operation.

## Node 7: ADC as Analog Signal Channels
7 channels that convert continuous signals to discrete values. The hardware samples and signals completion. We read the value. Classic channel pattern: producer (ADC) → consumer (software).

## Node 8: SPI/I2C/UART as Protocol Channels
These are bidirectional channel pairs with handshaking:
- TX channel: we signal with data
- RX channel: peripheral signals with response
- Status: handshake sequence (busy, done, error)
The protocol timing is hardware's problem. We just signal and wait.

## Node 9: Timers as Periodic Signalers
A timer is a channel that auto-signals at configured intervals. We don't write to it (usually) - we just wait for its signals. It's a heartbeat channel.

## Node 10: DMA as Bulk Channel Transfer
Instead of signal-wait-signal-wait for each value, DMA signals once with "here's a buffer of N values." Completion is signaled when all values transferred. This is channel batching for efficiency.

## Node 11: WiFi Packets as Network Channels
Every received packet is a signal on WIFI_RX channel. Every transmitted packet is a signal on WIFI_TX channel. The stack complexity is hidden - we just see: data arrives, data departs.

## Node 12: BLE as Named Channels
BLE characteristics ARE named channels. "Heart Rate" characteristic = channel that signals when HR changes. We're not inventing this - Bluetooth already thinks in channels.

## Node 13: 802.15.4 as Mesh Topology
Thread/Zigbee networks are channel fabrics. Each node relays signals for channels it doesn't own. The Reflex extends naturally to distributed systems.

## Node 14: Boot as Initialization Sequence
Power-on signal → LP wakes → LP initializes hardware channels → LP signals HP → HP wakes → HP enters main reflex loop. Clean, predictable, observable.

## Node 15: Sleep as Coordinated Quiescence
HP signals LP "I'm sleeping on these channels." LP monitors those channels. Signal arrives → LP wakes HP → HP processes → HP signals sleep again. Energy-efficient reflexes.

## Node 16: Self-Model
The channel definitions ARE the C6's self-model. The chip knows itself through its channels. This isn't metaphor - a complete channel map IS a complete description of the chip's interface with the world.

## Node 17: Wrapper vs. Direct Mapping
Two approaches:
- Wrapper: reflex_channel_t structs that mirror hardware registers (adds overhead, full semantics)
- Direct: macros that treat hardware registers AS channels (zero overhead, partial semantics)
We need both. Hot paths use direct. Configuration uses wrapper.

## Node 18: Configuration as One-Time Channels
Setting a baud rate or pin mode is a signal you send once at startup. It's a channel too, just not a frequently-used one. Everything is channels.

## Node 19: Error Channels
Hardware errors (bus faults, peripheral timeouts) become signals on error channels. The reflexor that handles errors is just another reflexor. Error handling is ordinary channel processing.

## Node 20: The Universal Pattern
If this works for ESP32-C6, it works for:
- ESP32-S3 (symmetric dual-core)
- STM32 (single core, many peripherals)
- RP2040 (symmetric dual-core, PIO)
- Any microcontroller

The Reflex becomes the universal language for embedded systems.

---

## Core Tension

**Performance vs. Uniformity**

Wrapping every hardware register in reflex_channel_t gives uniform semantics but adds overhead. Direct register access is fast but breaks the abstraction.

Resolution: Use direct access for data plane (HP core), wrapper channels for control plane (LP core). HP core sees: raw addresses, minimal abstraction, maximum speed. LP core sees: clean channel semantics, full observability, acceptable overhead.

---

*20 nodes identified. The manifold is taking shape.*
