# RAID ETM Fabric Architecture

Autonomous computation using only ESP32-C6 peripheral hardware. No CPU involvement after initialization.

**File:** `reflex-os/main/raid_etm_fabric.c`
**Verified:** Feb 6, 2026 — 5/5 tests passing, 5004 autonomous state transitions in 500ms.

## Concept

"RAID ETM" stripes single ETM events across multiple channels to trigger N simultaneous hardware actions, analogous to RAID striping across disks. The computation loop runs entirely in hardware:

```
ETM event → fan-out to multiple ETM channels → simultaneous hardware tasks
```

## Hardware Chain

```
Timer alarm → ETM → GDMA start → data to PARLIO → 4-bit GPIO output → PCNT counts edges
                                                                            ↓
                                                              PCNT threshold/limit
                                                                            ↓
                                                              ETM fan-out → next cycle
```

## Resource Allocation

| Resource | Count | Purpose |
|----------|-------|---------|
| ETM channels | 38/50 | Full crossbar wiring |
| PCNT units | 4/4 | Branch detection on GPIO 4-7 |
| GDMA OUT channels | 3/3 | CH0 = PARLIO driver (Tests 1-3), then bare-metal (Test 4). CH1-2 = bare-metal |
| GP Timers | 2/2 | T0 = phase clock, T1 = watchdog |
| LEDC Timers | 3 | T0=5kHz, T1=3kHz, T2=2kHz — state registers |
| LEDC Channels | 3 | CH0-2 on GPIO 0-2 — overflow counters for feedback |
| PARLIO TX | 1 | 4-bit output on GPIO 4-7, loopback mode |

## Pattern Encoding

Each byte encodes which PCNT units receive pulses. The pattern IS the instruction:

| Pattern | Byte | Bits set | Target GPIOs | Target PCNTs |
|---------|------|----------|-------------|--------------|
| alpha | 0x33 | 0,1 | GPIO 4,5 | PCNT0, PCNT1 |
| beta | 0x66 | 1,2 | GPIO 5,6 | PCNT1, PCNT2 |
| gamma | 0xCC | 2,3 | GPIO 6,7 | PCNT2, PCNT3 |

Patterns alternate with 0x00 to create edges: `[0x33, 0x00, 0x33, 0x00, ...]` (64 bytes total, 32 active).

## ETM Wiring Topology (38 channels)

### Kickoff (2 channels)
```
CH0: Timer0 alarm → GDMA CH0 start (send first pattern)
CH1: Timer0 alarm → PCNT reset (clear counters)
```

### State Branch (20 channels, 8 states)
States S0-S5 run the computation. S6-S7 are halt states.

```
S0: PCNT threshold → GDMA bare0 start + Timer0 capture + PCNT reset
S1: PCNT limit    → GDMA bare1 start + Timer0 capture + PCNT reset
S2: PCNT threshold → GDMA bare0 start + Timer0 capture + PCNT reset
S3: PCNT limit    → GDMA bare1 start + Timer0 reload + PCNT reset
S4: PCNT threshold → GDMA bare0 start + Timer0 capture + PCNT reset
S5: PCNT limit    → GDMA bare1 start + Timer0 reload + PCNT reset
S6: PCNT threshold → Timer0 stop + Timer0 capture (halt)
S7: PCNT limit    → Timer0 stop + Timer1 stop (halt)
```

### GDMA EOF Cleanup (2 channels)
```
CH27: GDMA bare0 EOF → PCNT reset
CH28: GDMA bare1 EOF → PCNT reset
```

### Watchdog (1 channel)
```
CH30: Timer1 alarm → Timer0 stop
```

### LEDC Feedback — Gate/Inhibit (6 channels)
PCNT events gate LEDC oscillators. Different thresholds activate different timers:
```
CH31: PCNT threshold → LEDC Timer0 resume
CH32: PCNT limit    → LEDC Timer1 resume
CH33: PCNT threshold → LEDC Timer2 resume
CH34: PCNT limit    → LEDC Timer0 pause
CH35: PCNT threshold → LEDC Timer1 pause
CH36: PCNT limit    → LEDC Timer2 pause
```

### Winner-Take-All (2 channels)
First LEDC overflow selects the next program:
```
CH37: LEDC OVF CH0 → GDMA bare0 start (alpha wins)
CH38: LEDC OVF CH1 → GDMA bare1 start (beta wins)
```

### OVF Cleanup (3 channels)
```
CH40-42: LEDC OVF CH0/CH1/CH2 → PCNT reset
```

## Initialization Ordering (Critical)

The order matters due to driver/hardware interactions:

1. **ETM clock** — bare-metal PCR register write
2. **Patterns** — generate DMA buffers and descriptors
3. **GP Timers** — IDF driver, with alarm + watchdog callback
4. **PCNT** — IDF driver, 4 units on GPIO 4-7, with watch callbacks
5. **PARLIO** — IDF driver, 4-bit TX on GPIO 4-7 with loopback
6. **Detect PARLIO's GDMA channel** — scan PERI_SEL registers
7. **LEDC** — IDF driver, 3 timers + 3 channels, all paused
8. **LEDC ETM enable** — bare-metal EVT_TASK_EN0/EN1 registers
9. **GDMA bare-metal** — DEFERRED (configuring during init corrupts PARLIO)
10. **ETM wiring** — DEFERRED (wiring during init causes spurious triggers)

Steps 9-10 happen right before Test 4, after PARLIO driver tests complete.

## Test Sequence

### Test 1: Pattern Targeting
CPU-driven PARLIO transmit. Verifies each pattern hits the correct PCNT units.
- alpha → PCNT0+1 get 32 counts each, PCNT2+3 get 0
- beta → PCNT1+2 get 32, PCNT0+3 get 0
- gamma → PCNT2+3 get 32, PCNT0+1 get 0

### Test 2: ETM Fan-Out
CPU starts timer, transmits pattern. PCNT watch callback fires, proving the ISR path works.

### Test 3: LEDC Overflow Counter
Enables OVF_CNT on LEDC CH0 (10-overflow threshold), resumes timer for 10ms, verifies OVF_CNT_CH0 interrupt fires at INT_RAW bit 12.

### Test 4: Autonomous Computation
The core test. After releasing the PARLIO driver:
1. Reconfigure GDMA CH0 bare-metal with ETM_EN
2. Set PARLIO TX_START via bare-metal register write
3. Wire ETM kickoff: Timer0 alarm → GDMA CH0 start
4. Start timer0 (100us alarm) and timer1 (500ms watchdog)
5. CPU enters NOP spin loop
6. Hardware fabric computes autonomously until watchdog fires

**Result:** 5004 state transitions, PCNT[70,71,0,0], 500ms duration.

### Test 5: Watchdog Halt
Timer1 alarm ISR stops Timer0 via bare-metal register write. Verifies Timer0 stopped at 10ms (not 20ms of the delay).

## Key Design Decisions

1. **PARLIO 4-bit mode** — each nibble maps directly to GPIO 4-7. Pattern bytes encode which PCNT units receive pulses.

2. **Loopback mode** — PARLIO output loops back to the same GPIOs that PCNT reads. No external wiring needed.

3. **LEDC as state registers** — oscillators at different frequencies race. First to overflow wins (winner-take-all), selecting the next DMA program.

4. **Bare-metal GDMA** — IDF GDMA API doesn't expose ETM_EN bit. Must write registers directly.

5. **Bare-metal ETM** — register base 0x600B8000. IDF ETM API exists but bare-metal gives full control over the 50-channel crossbar.

6. **Deferred initialization** — GDMA and ETM setup happens after PARLIO driver tests, not during init. This prevents driver corruption.
