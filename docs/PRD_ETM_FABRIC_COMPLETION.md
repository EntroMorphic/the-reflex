# PRD: ETM Fabric Completion

**Product Requirements Document**  
**Version 1.0** | February 2, 2026  
**Target: ESP32-C6 @ 160 MHz (3 devices attached)**

---

## 1. Executive Summary

**Goal:** Complete the Turing Complete ETM Fabric implementation and verify it on actual hardware.

**Current State:**
- ✅ RMT autonomous pulse generation (loop mode)
- ✅ PCNT hardware addition (verified counting)
- ✅ Timer → RMT triggering via ETM
- ⚠️ GDMA M2M to peripheral registers (INCOMPLETE)
- ⚠️ Timer race conditional branching (NOT IMPLEMENTED)
- ⚠️ Full autonomous loop with CPU WFI (NOT DEMONSTRATED)

**Success Criteria:** Demonstrate 100+ autonomous inference cycles with CPU in WFI (sleep), proving Turing Completeness via conditional branching.

---

## 2. Requirements

### 2.1 GDMA M2M to Peripheral (CRITICAL)

**REQ-GDMA-01:** GDMA must write to RMT memory at 0x60006100  
**Verification:** Write test pattern via GDMA, read back and verify match  
**Acceptance:** 100% match on 100 test patterns

**REQ-GDMA-02:** GDMA must use paired IN/OUT channels for M2M  
**Implementation:** Complete the mechanism described in reflex_gdma.h lines 241-262  
**Acceptance:** Data flows: SRAM → OUT FIFO → IN channel → RMT RAM

**REQ-GDMA-03:** GDMA must be ETM-triggerable  
**Verification:** ETM event starts GDMA transfer without CPU  
**Acceptance:** Transfer completes within 10μs of trigger

### 2.2 Conditional Branching (CRITICAL)

**REQ-BRANCH-01:** Implement timer race mechanism  
**Architecture:**
- Timer0: Normal period trigger (low priority GDMA path)
- Timer1: Timeout trigger (high priority GDMA path)  
- PCNT threshold: Early completion trigger (high priority GDMA path)

**REQ-BRANCH-02:** GDMA priority preemption  
**Behavior:** When both timers fire, higher priority channel wins  
**Verification:** Run 1000 races, verify high-priority wins >99%

**REQ-BRANCH-03:** Pattern selection based on winner  
**Behavior:** Whichever GDMA channel completes first loads its pattern to RMT  
**Verification:** Test both paths (timeout vs threshold), verify correct pattern loaded

### 2.3 Full Autonomous Loop (CRITICAL)

**REQ-LOOP-01:** Complete ETM chain without CPU  
**Chain:** Timer → GDMA → RMT → PCNT → (threshold/timeout) → GDMA → loop  
**Acceptance:** Run 100+ cycles, CPU in WFI entire time

**REQ-LOOP-02:** PCNT threshold triggers branch  
**Behavior:** When count >= threshold, trigger high-priority GDMA  
**Acceptance:** Branch taken within 5μs of threshold crossing

**REQ-LOOP-03:** Loop stability  
**Acceptance:** Run 1000 cycles, zero crashes, <1% timing variance

### 2.4 Power & Performance

**REQ-POWER-01:** CPU in WFI during operation  
**Verification:** Use `__asm__ volatile("wfi")`, verify inference continues  
**Acceptance:** 1000 cycles with CPU asleep

**REQ-PERF-01:** Maintain 500+ Hz inference rate  
**Acceptance:** >= 500 inferences/second in autonomous mode

**REQ-PERF-02:** Latency bounds  
**Acceptance:** P99 latency < 5ms per inference cycle

---

## 3. Implementation Plan

### Phase 1: GDMA M2M Completion (Day 1)

**Task 1.1:** Implement paired GDMA IN/OUT channel configuration
- File: `reflex_gdma.h`
- Add: `gdma_m2m_init()` function
- Configure OUT channel to read from SRAM
- Configure IN channel to write to RMT RAM (0x60006100)
- Link them via internal FIFO

**Task 1.2:** Test GDMA to peripheral write
- Create test: Write 0xDEADBEEF pattern to RMT RAM
- Read back and verify
- Run 100 iterations

**Deliverable:** Working GDMA M2M to RMT memory

### Phase 2: Timer Race Implementation (Day 1-2)

**Task 2.1:** Configure dual timers
- Timer0: 100Hz inference period (10ms)
- Timer1: 12ms timeout (20% margin)
- Both ETM-triggerable

**Task 2.2:** Configure dual GDMA channels with priority
- GDMA_CH0: Priority 15 (highest), branch path
- GDMA_CH1: Priority 0 (lowest), default path

**Task 2.3:** Implement race test
- Create demo: Force both timers to fire simultaneously
- Verify high-priority channel wins
- Measure race resolution time

**Deliverable:** Working timer race with priority-based winner

### Phase 3: Full Autonomous Loop (Day 2-3)

**Task 3.1:** Wire ETM crossbar
- Channel 0: Timer0 alarm → GDMA_CH1 start (default path)
- Channel 1: Timer1 alarm → GDMA_CH0 start (timeout path)
- Channel 2: PCNT threshold → GDMA_CH0 start (fast path)
- Channel 3: GDMA_CH0 EOF → RMT TX start
- Channel 4: GDMA_CH1 EOF → RMT TX start
- Channel 5: RMT TX done → PCNT reset
- Channel 6: PCNT reset → Timer0 reload (loop)

**Task 3.2:** Create test firmware
- File: `etm_fabric_complete.c`
- Initialize all peripherals
- Start autonomous loop
- Enter WFI
- Wake periodically to log stats

**Task 3.3:** Stress test
- Run 1000 cycles
- Log: cycle count, branch decisions, timing
- Verify stability

**Deliverable:** 1000-cycle autonomous run with CPU in WFI

### Phase 4: Verification & Documentation (Day 3-4)

**Task 4.1:** Power measurement
- Measure current consumption during WFI
- Target: < 50μA total (peripherals + CPU sleep)

**Task 4.2:** Update documentation
- Update `SILICON_GRAIL.md` with actual results
- Document any hardware limitations found
- Create test report

**Task 4.3:** Falsification report
- Run adversarial tests
- Document edge cases
- Verify all claims

**Deliverable:** Complete test report with measurements

---

## 4. Test Criteria

### 4.1 Unit Tests

| Test | Criteria | Pass Threshold |
|------|----------|----------------|
| GDMA write | Write pattern, read back | 100% match |
| GDMA ETM trigger | Trigger via ETM, measure latency | < 10μs |
| Timer race | High vs low priority | High wins >99% |
| PCNT threshold | Fire at exact count | ±1 count |
| Full loop | 1000 cycles autonomous | 0 crashes |

### 4.2 Integration Tests

| Test | Criteria | Pass Threshold |
|------|----------|----------------|
| Autonomous 100 | 100 cycles, CPU WFI | Completes |
| Autonomous 1000 | 1000 cycles, CPU WFI | Completes, <1% variance |
| Branch coverage | Both paths taken | Each path >10% |
| Power target | Current in WFI | < 50μA |
| Performance | Inference rate | >= 500 Hz |

---

## 5. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| GDMA M2M doesn't work as theorized | HIGH | Fallback: Keep CPU for pattern loading, still achieve 500+ Hz |
| Timer race resolution too slow | MEDIUM | Use smaller timeout margins, accept some false positives |
| PCNT threshold event not reliable | MEDIUM | Use multiple threshold levels, majority voting |
| ETM channel exhaustion | LOW | Share channels, use OR logic where possible |
| Power higher than expected | LOW | Document actual numbers, adjust claims |

---

## 6. Success Definition

**Minimum Viable Success:**
- GDMA writes to RMT memory
- Autonomous loop runs 100+ cycles
- CPU can enter WFI between cycles
- 500+ Hz inference maintained

**Full Success (Turing Complete):**
- All above, PLUS
- Timer race conditional branching working
- Both code paths (threshold/timeout) demonstrated
- 1000+ cycles without CPU intervention
- Power consumption measured and < 50μA

---

## 7. Timeline

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| Phase 1: GDMA M2M | 4-6 hours | Working peripheral write |
| Phase 2: Timer Race | 4-6 hours | Priority-based branching |
| Phase 3: Full Loop | 6-8 hours | Autonomous operation |
| Phase 4: Verify | 4-6 hours | Test report |
| **Total** | **18-26 hours** | **Complete ETM fabric** |

---

**Status:** APPROVED for execution  
**Next Action:** Begin Phase 1 - GDMA M2M implementation

---

*"The hardware is already doing the work. We just need to wire it correctly."*
