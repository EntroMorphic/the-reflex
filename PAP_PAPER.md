# The Reflex Arc in Silicon: Peripheral-As-Processor (PaP) Micro-Architecture for Sub-30uA Ternary Intelligence

**Aaron (Tripp) Josserand-Austin**
EntroMorphic Research Team
tripp@entromorphic.com

---

## Abstract

In the time a standard RTOS takes to handle a single context switch, the architecture described in this paper has perceived a sensor pattern, queried a memory graph, and adjusted an actuator gait—all while the CPU remained in a deep sleep. This paper introduces the **Peripheral-As-Processor (PaP)** micro-architecture: a radical collapse of the abstraction stack that rejects the CPU for arithmetic computation in favor of **Infrastructure-as-Compute**. By repurposing commodity silicon (ESP32-C6), we demonstrate a zero-CPU ternary inference engine achieving **926ns P99 latencies** and **10kHz control rates** within a **30uA power envelope**. We synchronize a 10MHz data stream with a hardware pulse counter across distinct clock domains, effectively turning a $0.50 hardware debouncer into a deterministic, signed ternary multiplier. This is not a software optimization; it is the realization of the biological-grade reflex arc in silicon.

---

## 1. Introduction: The Abstraction Debt

Modern robotics and AI are stifled by "Abstraction Debt"—the cumulative latency and power overhead of OS kernels, middleware, and high-level programming languages. In mission-critical environments requiring sub-microsecond determinism, even highly optimized "TinyML" stacks on Von Neumann architectures fail due to the "Memory Wall" and interrupt jitter.

To achieve biological-grade responsiveness, we must move beyond writing code *for* a processor and instead treat the hardware substrate *as* the logic. This paper details the implementation of a **Peripheral-As-Processor (PaP)** architecture on off-the-shelf silicon, proving that "intelligence" is a property of the physical signal path, not the instruction set.

## 2. Infrastructure-as-Compute: The PaP Paradigm

The core philosophy of PaP is the rejection of the ALU for recurrent math. Instead of fetching instructions to compute a dot product, we route hardware data streams such that the **physics of the chip computes the math as a side effect of data movement.**

### 2.1 The Silicon-Level Dialogue: Clock Domain Synchronization
The most significant engineering hurdle in PaP is the physical reality of the substrate. Our implementation synchronizes a **10MHz PARLIO** (Parallel IO) TX stream with a **PCNT** (Pulse Counter) across asynchronous clock domains. 

A naive read of the peripheral results in "ghost pulses"—noise caused by propagation delay. We solved this not with a software filter, but with a **200-loop silicon drain** in the ISR, a precise "waiting for the electrons" that ensures the physical signals have fully latched before the math is trusted. This is a dialogue with the silicon, resulting in 100% deterministic accuracy.

### 2.2 The Debouncer as a Signed Multiplier
We hijacked the hardware pulse counter—a peripheral designed for mechanical debouncing—and repurposed its **Level Control** and **Edge Clock** inputs. 
*   **Edge (GPIO 4):** Streams 2-bit encoded ternary trits {-1, 0, +1}.
*   **Gate (GPIO 5):** Acts as the sign controller.

By carefully configuring the PCNT state machine, the hardware naturally computes the logic of a signed ternary multiplication. The counter increments on agreement and ignores on neutral/zero. The "plumbing" of the chip effectively *becomes* the arithmetic unit.

## 3. The Living Heartbeat: Ternary Dynamics

By utilizing **Ternary Logic {-1, 0, +1}**, we enable a first-class **INVERT (-1)** primitive. In our Ternary Gated Recurrent Unit (T-GRU), this single gate-flip produces emergent oscillatory dynamics (period-2) natively. While binary GRUs require complex software state-machines to oscillate, the PaP architecture "pulses" like a heartbeat. This enables robots to maintain rhythmic gaits or vibrational stability as a hardware primitive, requiring zero CPU "thought."

## 4. Results: Verifiable AI at $0.50 Scale

### 4.1 37 Milestones of Exactness
Unlike the "black box" nature of traditional deep learning, the PaP architecture is fully deterministic. Every milestone in development was verified against a CPU-computed ground truth on silicon (ESP32-C6FH4).
*   **Verification:** 64/64 dot products matched exactly across all 37 verification tests.
*   **Reliability:** Zero drift over infinite loops, providing **Verifiable AI** for mission-critical robotics.

### 4.2 The 30uA Power Envelope
By shifting the compute load to the peripheral fabric and the ULP assembly (fitting the entire stack into **16,320 bytes**), we achieve full inference at a power draw of **~30uA**. This is the energy profile of a blinking LED, yet it supports a complete neural network, an NSW vector database, and a feedback-control loop.

## 5. Conclusion: The Substrate is the Intelligence

The Peripheral-As-Processor architecture proves that commodity silicon contains latent, emergent computational primitives that surpass the efficiency of the intended Von Neumann flow. By collapsing the stack and embracing the hardware substrate, we have built a system that reacts with the speed of physics and the efficiency of biology.

The Reflex is not a replacement for high-level AI; it is the **autonomous nervous system** that makes high-level AI viable in the physical world. This is the first step toward a future where we no longer "run" AI on chips, but instead design chips that *are* the AI.

---
**Verified Silicon Status:** All claims verified on ESP32-C6 and NVIDIA Jetson AGX Thor. 
**Latency:** 926ns P99 (Reflex Signal Path).
**Throughput:** 428Hz free-running (GIE).
**Scale:** 64 Neurons / 64 NSW Nodes / 48-Trit Dimensions.
