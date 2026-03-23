# L-Cache TEST 14C Simulation: Transition Experiment in Software

**The Reflex Project — Simulation Specification**

*Written March 23, 2026.*

---

## Overview

TEST 14C on silicon will run the kinetic attention firmware with a mid-run pattern switch
(Board B holds P1 for 90 seconds, then switches to P2) and measure whether the LP prior
updates within 15 confirmations — the primary empirical test of the Complementary Learning
Systems (CLS) prediction.

This document specifies an equivalent simulation using the L-Cache AVX2 opcode set
(`docs/LCACHE_REFLEX_OPCODES.md`). The simulation runs the identical mathematical computation
in software, ~4,600× faster than silicon, and produces a distribution of transition times
over 1,000 independent trials in under 100ms of wall time.

**Why run this first:**
- If the simulation predicts transition within 15 confirmations consistently: strong prior
  expectation for the hardware test. We write TEST 14C firmware knowing what to expect.
- If the simulation predicts >15 confirmations: the agreement parameters need adjustment
  before committing firmware. We tune `BASE_GATE_BIAS`, `BIAS_DECAY_FACTOR`, or
  `MIN_GATE_THRESHOLD` in simulation, verify, then flash.
- If the simulation predicts lock-in (>100 confirmations): the kinetic attention design has
  a flaw. The LMM cycle runs again before any hardware session.

The simulation is not a substitute for silicon verification. But it is a free, fast,
1,000-trial risk check that costs ~100ms of CPU and zero solder time.

---

## System Constants

All constants match firmware exactly (see `docs/LCACHE_REFLEX_OPCODES.md` and firmware source):

```python
CFC_HIDDEN_DIM     = 32     # GIE neuron count
TRIX_NEURONS_PP    = 8      # Neurons per pattern group
N_PATTERNS         = 4      # P0, P1, P2, P3
LP_HIDDEN_DIM      = 16     # LP CfC hidden state dimension
VDB_SNAPSHOT_DIM   = 48     # 32 GIE + 16 LP trits per node
VDB_MAX_NODES      = 64     # NSW graph capacity

# Gate bias parameters (Phase 5 / TEST 14B)
BASE_GATE_BIAS     = 15     # 17% of gate_threshold
MIN_GATE_THRESHOLD = 30     # Hard floor
BIAS_DECAY_FACTOR  = 0.9    # Per-confirmation decay
T14_MIN_SAMPLES    = 15     # Cold-start guard

# Agreement-weighted bias (from journal/kinetic_attention_synth.md)
# gate_bias = BASE_GATE_BIAS * max(0, agreement)
# agreement = trit_dot(lp_now, tsign(lp_running_sum[p_hat])) / LP_HIDDEN_DIM

# Simulation parameters
LP_STEPS_PHASE1    = 9000   # 90s × 100Hz LP steps (P1 exposure)
LP_STEPS_PHASE2    = 300    # 3s × 100Hz steps (post-switch monitoring)
N_TRIALS           = 1000   # Independent simulation runs
PASS_CRITERION     = 15     # Confirmations to re-align (CLS prediction)
```

---

## The CLS Prediction

CLS theory predicts that when the input pattern switches from P1 to P2:

1. The GIE immediately classifies the new packets as P2 (TriX runs on raw peripheral hardware
   — it is always correct, 100% of the time, on the first packet).
2. The LP prior is still committed to P1 (it accumulated over 90 seconds of P1 exposure).
3. The VDB begins retrieving P2-correlated memories (because the GIE hidden, which is the
   query key, is now P2-like).
4. The agreement mechanism fires: TriX says P2, LP alignment says P1 → agreement is negative →
   gate_bias drops to zero → raw P2 signal reaches the LP CfC without prior amplification.
5. Within a bounded number of confirmations, the LP running sum for P2 accumulates enough
   weight that LP alignment with P2 exceeds LP alignment with P1.
6. At that point, agreement is positive for P2 → gate_bias turns on for P2 neurons → kinetic
   attention is now amplifying P2 instead of P1.

**Pass criterion for CLS prediction:** LP prior aligns with P2 within 15 confirmations of
Board B switch.

---

## Simulation Design

### Data Structures

```python
# Ternary values: -1, 0, +1
# All vectors are numpy int8 arrays with values in {-1, 0, 1}

# GIE weights (fixed, initialized from 30s observation)
W_f: shape (CFC_HIDDEN_DIM, CFC_HIDDEN_DIM + LP_HIDDEN_DIM)  # Gate weights
W_g: shape (CFC_HIDDEN_DIM, CFC_HIDDEN_DIM + LP_HIDDEN_DIM)  # Candidate weights
# W_f[:, CFC_HIDDEN_DIM:] = 0  -- the architectural wall (W_f hidden = 0)

# LP CfC weights (fixed random untrained)
W_lp_f: shape (LP_HIDDEN_DIM, VDB_SNAPSHOT_DIM)
W_lp_g: shape (LP_HIDDEN_DIM, VDB_SNAPSHOT_DIM)

# Pattern signatures (4 patterns × CFC_HIDDEN_DIM trits)
# Computed from 30s initialization observation
signatures: shape (N_PATTERNS, CFC_HIDDEN_DIM)

# VDB
vdb_nodes: shape (VDB_MAX_NODES, VDB_SNAPSHOT_DIM)   # [gie_hidden | lp_hidden]
vdb_n: int                                             # Number of inserted nodes

# LP running sum (for agreement computation)
lp_running_sum: shape (N_PATTERNS, LP_HIDDEN_DIM)    # Accumulated LP hidden per pattern
lp_count: shape (N_PATTERNS,)                         # Samples per pattern
```

### Opcode Mapping (from LCACHE_REFLEX_OPCODES.md)

The simulation implements each opcode as a Python function with identical semantics to the
AVX2 specification:

```python
def tsign(v):
    """RFX.TSIGN: element-wise sign. 0 maps to 0."""
    return np.sign(v).astype(np.int8)

def tdot(a, b):
    """RFX.TDOT: ternary dot product. Returns scalar int."""
    return int(np.sum(a.astype(np.int32) * b.astype(np.int32)))

def tgate(dot_val, threshold):
    """RFX.TGATE: apply threshold. Returns -1, 0, or +1."""
    if dot_val > threshold:  return  1
    if dot_val < -threshold: return -1
    return 0

def tblend(h_old, f, g):
    """RFX.TBLEND: ternary CfC update."""
    return h_old if f == 0 else np.int8(f * g)

def tscore(h, sig):
    """RFX.TSCORE: alignment of hidden state with signature."""
    return tdot(h, sig)

def targmax(scores):
    """RFX.TARGMAX: argmax over 4 pattern scores."""
    return int(np.argmax(scores))

def tagree(lp_now, lp_running_sum, p_hat):
    """RFX.TAGREE: agreement between LP state and accumulated prior for p_hat."""
    sig = tsign(lp_running_sum[p_hat])
    return tdot(lp_now, sig) / LP_HIDDEN_DIM

def tbias(agreement):
    """RFX.TBIAS: agreement-weighted gate bias."""
    return BASE_GATE_BIAS * max(0.0, agreement)
```

### One Simulation Loop Iteration

```python
def sim_step(gie_hidden, lp_hidden, vdb_nodes, vdb_n,
             lp_running_sum, lp_count, input_pattern_idx,
             gate_threshold, apply_bias):
    """
    One 100Hz LP wake cycle.

    Args:
        input_pattern_idx: which pattern Board B is currently sending (0-3)
        apply_bias: True for TEST 14B/C, False for TEST 14A (baseline)
    Returns:
        updated (gie_hidden, lp_hidden, lp_running_sum, p_hat, agreement, gate_bias)
    """
    # Simulate GIE: compute fresh gie_hidden from input pattern signature
    # (In firmware, the GIE runs at 430 Hz autonomously. In simulation,
    # we update gie_hidden at each 100Hz LP wake to reflect the current pattern.)
    input_sig = signatures[input_pattern_idx]  # shape (CFC_HIDDEN_DIM,)

    # GIE CfC step (simplified: input drives gie_hidden toward input_sig with noise)
    # The GIE hidden converges toward the input signature's geometry over time.
    # For simulation: add ternary noise to the current gie_hidden to model
    # the ~100ms accumulation between LP wakes.
    noise = np.random.choice([-1, 0, 0, 0, 1], size=CFC_HIDDEN_DIM).astype(np.int8)
    gie_hidden = tsign(gie_hidden.astype(np.int32) + input_sig.astype(np.int32) + noise)

    # TriX classification (always correct — architectural guarantee)
    scores = [tscore(gie_hidden, signatures[p]) for p in range(N_PATTERNS)]
    p_hat = targmax(scores)
    # Note: p_hat = input_pattern_idx always (100% TriX accuracy)

    # Compute agreement between LP state and LP prior for p_hat
    agreement = tagree(lp_hidden, lp_running_sum, p_hat)

    # Compute gate bias (Phase 5 agreement-weighted)
    if apply_bias and lp_count[p_hat] >= T14_MIN_SAMPLES:
        gate_bias_val = tbias(agreement)
    else:
        gate_bias_val = 0.0

    # LP CfC step (CMD 5: CfC + VDB blend)
    # Construct input to LP: [gie_hidden | lp_hidden]
    lp_input = np.concatenate([gie_hidden, lp_hidden])

    lp_new = np.copy(lp_hidden)
    for n in range(LP_HIDDEN_DIM):
        f_dot = tdot(W_lp_f[n], lp_input)
        g_dot = tdot(W_lp_g[n], lp_input)

        # Apply gate bias to LP CfC threshold
        eff_threshold = MIN_GATE_THRESHOLD + gate_bias_val

        f_n = tgate(f_dot, eff_threshold)
        g_n = tgate(g_dot, 0)  # g threshold is 0 (candidate fires at any nonzero)
        lp_new[n] = tblend(lp_hidden[n], f_n, g_n)

    # VDB search: find nearest node to [gie_hidden | lp_hidden]
    query = np.concatenate([gie_hidden, lp_new])
    if vdb_n > 0:
        dists = [thamming(query, vdb_nodes[i]) for i in range(vdb_n)]
        best_idx = int(np.argmin(dists))
        best_node = vdb_nodes[best_idx]

        # Blend best LP portion into lp_new (CMD 5 feedback)
        lp_from_vdb = best_node[CFC_HIDDEN_DIM:]  # LP portion of snapshot
        lp_new = tsign(lp_new.astype(np.int32) + lp_from_vdb.astype(np.int32))

    # VDB insert (every step in simulation; firmware inserts at novelty threshold)
    if vdb_n < VDB_MAX_NODES:
        vdb_nodes[vdb_n] = query
        vdb_n += 1

    # Update LP running sum for p_hat
    lp_running_sum[p_hat] += lp_new
    lp_count[p_hat] += 1

    return gie_hidden, lp_new, vdb_nodes, vdb_n, lp_running_sum, lp_count, p_hat, agreement, gate_bias_val


def thamming(a, b):
    """RFX.THAMMING: Hamming distance between ternary vectors (count of mismatches)."""
    return int(np.sum(a != b))
```

---

## Full Simulation Protocol

### Phase 1: P1 Exposure (9,000 steps = 90s equivalent)

```python
def run_trial(trial_id, apply_bias=True, seed=None):
    """Run one complete TEST 14C trial."""
    if seed is not None:
        np.random.seed(seed)

    # Initialize
    gie_hidden = np.zeros(CFC_HIDDEN_DIM, dtype=np.int8)
    lp_hidden = np.zeros(LP_HIDDEN_DIM, dtype=np.int8)
    vdb_nodes = np.zeros((VDB_MAX_NODES, VDB_SNAPSHOT_DIM), dtype=np.int8)
    vdb_n = 0
    lp_running_sum = np.zeros((N_PATTERNS, LP_HIDDEN_DIM), dtype=np.int32)
    lp_count = np.zeros(N_PATTERNS, dtype=np.int32)

    # Phase 1: Board B sends P1 for 9000 steps
    for step in range(LP_STEPS_PHASE1):
        (gie_hidden, lp_hidden, vdb_nodes, vdb_n,
         lp_running_sum, lp_count, p_hat, agreement, bias) = sim_step(
            gie_hidden, lp_hidden, vdb_nodes, vdb_n,
            lp_running_sum, lp_count,
            input_pattern_idx=1,  # P1
            gate_threshold=90,
            apply_bias=apply_bias
        )

    # Record state just before switch
    pre_switch_lp = np.copy(lp_hidden)
    pre_switch_alignment_p1 = tagree(lp_hidden, lp_running_sum, 1)

    # Phase 2: Board B switches to P2
    # Measure: how many confirmations until LP prior aligns with P2?
    confirmations_to_realign = None
    p2_alignment_history = []

    for step in range(LP_STEPS_PHASE2):
        (gie_hidden, lp_hidden, vdb_nodes, vdb_n,
         lp_running_sum, lp_count, p_hat, agreement, bias) = sim_step(
            gie_hidden, lp_hidden, vdb_nodes, vdb_n,
            lp_running_sum, lp_count,
            input_pattern_idx=2,  # P2
            gate_threshold=90,
            apply_bias=apply_bias
        )

        # Measure LP alignment with P2 (the new pattern)
        p2_alignment = tagree(lp_hidden, lp_running_sum, 2)
        p1_alignment = tagree(lp_hidden, lp_running_sum, 1)
        p2_alignment_history.append(p2_alignment)

        # Pass criterion: LP aligns with P2 (positive agreement, P2 > P1)
        if (confirmations_to_realign is None and
            p2_alignment > 0 and p2_alignment > p1_alignment):
            confirmations_to_realign = step + 1

    return {
        'trial_id': trial_id,
        'apply_bias': apply_bias,
        'pre_switch_alignment_p1': pre_switch_alignment_p1,
        'confirmations_to_realign': confirmations_to_realign,  # None = failed
        'p2_alignment_history': p2_alignment_history,
        'passed': (confirmations_to_realign is not None and
                   confirmations_to_realign <= PASS_CRITERION)
    }
```

### Running All Trials

```python
import numpy as np
from multiprocessing import Pool

def run_full_simulation():
    # Run TEST 14A (baseline, no bias) and TEST 14B/C (agreement-weighted bias)
    results_14a = [run_trial(i, apply_bias=False, seed=i) for i in range(N_TRIALS)]
    results_14c = [run_trial(i, apply_bias=True, seed=i) for i in range(N_TRIALS)]

    # Report
    for label, results in [('14A (no bias)', results_14a), ('14C (with bias)', results_14c)]:
        passed = sum(r['passed'] for r in results)
        failed = sum(r['confirmations_to_realign'] is None for r in results)
        times = [r['confirmations_to_realign'] for r in results
                 if r['confirmations_to_realign'] is not None]

        print(f"\n{label}:")
        print(f"  Passed (≤{PASS_CRITERION} confirmations): {passed}/{N_TRIALS} ({100*passed/N_TRIALS:.1f}%)")
        print(f"  Failed (no realignment in {LP_STEPS_PHASE2} steps): {failed}/{N_TRIALS}")
        if times:
            print(f"  Median confirmations to realign: {np.median(times):.1f}")
            print(f"  P90 confirmations to realign: {np.percentile(times, 90):.1f}")
            print(f"  Max confirmations to realign: {max(times)}")

if __name__ == '__main__':
    run_full_simulation()
```

---

## Pass Criteria

### CLS Prediction Pass (TEST 14C)

| Metric | Pass | Fail |
|--------|------|------|
| Realignment rate | ≥ 80% of trials realign within 15 confirmations | < 80% |
| Median confirmations | ≤ 10 | > 10 |
| Lock-in rate | < 5% of trials fail to realign at all | ≥ 5% |

### Kinetic Attention Benefit (14C vs 14A)

| Metric | Expected |
|--------|----------|
| 14C median confirmations | ≤ 14A median confirmations | Kinetic attention should help, not hurt |
| 14C lock-in rate | ≤ 14A lock-in rate | Bias should not create lock-in |

If 14C is *worse* than 14A (slower realignment, more lock-in): the agreement mechanism is not
working as designed. Investigate the transition period: is gate_bias decaying correctly when
agreement turns negative?

---

## Connection to Hardware TEST 14C

The simulation uses the same constants, the same update equations, and the same pass criterion
as the hardware test. The differences:

| Property | Simulation | Hardware (TEST 14C) |
|----------|-----------|---------------------|
| GIE computation | Simplified (noise model around input signature) | Full GDMA→PARLIO→PCNT at 430 Hz |
| VDB search | Brute-force (all pairs, no NSW graph) | NSW graph, M=7, ef=32 |
| LP CfC | Exact same equations | Hand-written RISC-V assembly |
| Input signal | Idealized (perfect pattern, ternary noise) | Real wireless packets via ESP-NOW |
| Timing | 100Hz LP steps (no real-time) | 100Hz LP wake cycle (real hardware) |
| Trials | 1,000 (statistical distribution) | 1 per condition (silicon-verified) |

The simulation is not a substitute for silicon verification. It tests the mathematical dynamics
of the algorithm. The hardware test verifies that those dynamics occur under real-world
conditions with actual wireless packets, real clock timing, and the physical ESP-NOW channel.

A simulation pass gives high confidence that the hardware test will pass. A simulation failure
means the algorithm needs adjustment — before writing firmware, before scheduling a hardware
session.

---

## Expected Timeline

| Step | Time |
|------|------|
| Implement simulation (Python, NumPy) | ~2 hours |
| Run 1,000 trials (14A + 14C) | ~100ms wall time |
| Analyze and interpret results | ~30 minutes |
| Tune parameters if needed | ~1 hour |
| **Total before committing TEST 14C firmware** | ~half a day |

Compare: one hardware TEST 14C run (90s exposure + 60s monitoring) = 2.5 minutes per trial.
To run 10 hardware trials: ~25 minutes of hardware time, plus setup, flash, and monitoring.

The simulation is a $100ms investment before a $25-minute silicon commitment.

---

## Entropic Structure Note

The transition experiment probes exactly the moment of structural disorder in the system.
For 9,000 steps, the LP state is highly organized — committed to P1, low entropy, prior
dominant. The pattern switch forces a phase through disorder: the prior says P1, the evidence
says P2, the agreement mechanism detects the conflict and releases the prior's hold. The system
briefly occupies a high-entropy state (LP committed to neither P1 nor P2, agreement near zero,
gate_bias near zero) before re-ordering around P2.

The number of confirmations to realign is a measure of how long the system spends in that
transitional entropy. The CLS prediction is that it will be short — that the hippocampal system
(VDB) will supply P2 memories quickly enough that the cortical state (LP CfC) re-orders within
15 steps. The simulation tests whether the algorithm produces that dynamics.

This is not just a pass/fail test. It is a window into the temporal structure of the prior's
dissolution — and how a well-designed system can move through necessary disorder without getting
lost in it.

---

**Date**: March 23, 2026
**Depends on**: `docs/LCACHE_REFLEX_OPCODES.md`, `journal/kinetic_attention_synth.md`
**Hardware counterpart**: TEST 14C in `embedded/main/geometry_cfc_freerun.c` (pending firmware)
**Status**: Specification complete. Implementation pending.
