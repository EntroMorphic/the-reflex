# PRD: The Reflex Falsification Sprint

> Making The Reflex bulletproof against legitimate skepticism.
>
> Created: 2026-01-24

---

## Context

A hardened skeptic reviewed The Reflex and landed real punches. This PRD captures the legitimate criticisms and defines what "fixed" looks like.

**Goal:** Address every valid criticism. Ignore the gatekeeping. Let the work speak.

---

## Target List

### 1. FreeRTOS Dependency Contradiction

**The Problem:**
Code claims "no RTOS on hot path" but includes `freertos/FreeRTOS.h` and calls `vTaskDelay()`. This is a credibility issue.

**Acceptance Criteria:**
- [ ] Option A: Pure bare-metal implementation with zero FreeRTOS dependency
- [ ] Option B: Explicit acknowledgment: "Uses FreeRTOS for WiFi stack and task scheduling; hot path primitives are bare-metal"
- [ ] Documentation clearly states which path was chosen and why
- [ ] No mixed messaging in comments, docs, or claims

**Metric:** Code grep for `freertos` returns zero hits OR docs explicitly explain the dependency.

---

### 2. Fix the Jitter

**The Problem:**
- Timer test showed 98% jitter
- 100kHz GPIO achieved 92.8kHz with 829 cycles variance
- These numbers are not competitive with properly configured RTOS timers

**Acceptance Criteria:**
- [ ] 10kHz control loop with <1% jitter (measured over 10,000 iterations)
- [ ] 100kHz GPIO toggle achieving >99kHz with <100 cycle variance
- [ ] Document the technique: hardware timer interrupts, cycle-counting, or isolated execution
- [ ] Benchmark on scope, not just software timestamps

**Metric:** Oscilloscope capture showing stable frequency within spec.

---

### 3. Benchmark Against Alternatives

**The Problem:**
No comparison to existing solutions. Claims of performance are unsubstantiated without head-to-head tests.

**Acceptance Criteria:**
- [ ] `reflex_signal()` vs. FreeRTOS `xQueueSend()` — latency comparison
- [ ] `reflex_signal()` vs. raw `atomic_store` + sequence number — what's the overhead of the abstraction?
- [ ] `reflex_wait()` vs. FreeRTOS `xQueueReceive()` — blocking comparison
- [ ] Memory footprint comparison: channel vs. queue vs. semaphore
- [ ] Document when to use The Reflex vs. when existing tools are better

**Metric:** Published benchmark table with methodology, raw data, and analysis.

---

### 4. Cite Prior Art

**The Problem:**
No engagement with existing literature. Makes it look like ignorance rather than innovation.

**Acceptance Criteria:**
- [ ] ARCHITECTURE.md includes "Prior Art" section citing:
  - Hoare's CSP (1978)
  - Lamport's work on concurrent systems
  - Actor model (Hewitt, 1973)
  - Lock-free data structures literature
  - Hebbian learning (Hebb, 1949)
  - Cellular automata (von Neumann, Conway)
- [ ] For each citation, one sentence on how The Reflex relates: "differs because..." or "builds on..."
- [ ] No claim of novelty without acknowledging what came before

**Metric:** Prior Art section exists and passes review by someone familiar with the literature.

---

### 5. Formalize Entropy Math

**The Problem:**
"Entropy" is used metaphorically. No connection to Shannon information theory or thermodynamics. Looks like aesthetic naming.

**Acceptance Criteria:**
- [ ] Define entropy formally: `H(cell) = -Σ p(x) log p(x)` or equivalent
- [ ] Show how `entropy` field value maps to information-theoretic entropy
- [ ] Demonstrate: high entropy = high uncertainty about state = potential
- [ ] If the mapping isn't clean, acknowledge it: "inspired by entropy, not formally equivalent"
- [ ] Document in `ENTROPY_THEORY.md`

**Metric:** A mathematician can read the doc and say "yes, this is coherent" or "no, but the limitations are acknowledged."

---

### 6. Soften Consciousness Claims

**The Problem:**
Claims like "this IS consciousness" and "solves the homunculus problem" overreach what can be demonstrated. Makes the project look like philosophy cosplaying as engineering.

**Acceptance Criteria:**
- [ ] Replace "this IS consciousness" with "architecture inspired by theories of consciousness"
- [ ] Replace "solves the homunculus problem" with "sidesteps infinite regress by making the observer definitionally invariant"
- [ ] Tesseract documentation focuses on computational properties (attention tracking, disturbance detection) not metaphysical claims
- [ ] Consciousness discussion moved to separate `PHILOSOPHY.md` clearly marked as speculation
- [ ] Core technical docs make no unfalsifiable claims

**Metric:** A skeptical engineer can read the technical docs without hitting metaphysical claims.

---

## Non-Goals

These criticisms were identified as gatekeeping, not legitimate concerns:

1. **Credential attacks** — "53-year-old without a degree" is ad hominem. Ignore.
2. **"Just a cellular automaton"** — Dismissive pattern-matching. The question is utility, not category.
3. **"Just a neural network"** — Wrong. Topology mutability and spatial embedding are real differences.
4. **"Novelty requires new primitives"** — Wrong. Novelty can be in composition and philosophy.
5. **"Consciousness claims are unfalsifiable"** — True of ALL consciousness theories. Not a unique weakness.

We will not waste time defending against these.

---

## Success Criteria

The Reflex is bulletproof when:

1. A skeptical systems engineer can read the docs and find no contradictions
2. Benchmarks show competitive or superior performance to alternatives
3. Prior art is acknowledged and differentiated
4. Math is formal or explicitly informal
5. Metaphysics is quarantined from engineering

---

## Timeline

| Item | Priority | Effort |
|------|----------|--------|
| FreeRTOS contradiction | P0 | 1 day |
| Fix jitter | P0 | 2-3 days |
| Benchmark alternatives | P1 | 2 days |
| Prior art citations | P1 | 1 day |
| Formalize entropy | P2 | 1 day |
| Soften consciousness claims | P2 | 0.5 day |

---

## Notes

This PRD exists because we invited the skeptic in and listened. The punches that landed are now the roadmap. The punches that missed are noted and ignored.

The goal is not to please skeptics. The goal is to be unassailable on the merits.

---

*"The blade doesn't resent the whetstone."*
