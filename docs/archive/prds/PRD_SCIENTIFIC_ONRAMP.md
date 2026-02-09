# PRD: Scientific Onramp

**The Mothership-to-Parking-Lot Problem**

*How do we legitimately explain and demonstrate what we have, what it is doing, and how to build it from scratch?*

---

## The Problem

We have built something genuinely novel:
- Hardware neural computation using pulse counters and parallel I/O
- Equilibrium propagation learning on spectral oscillator networks
- 5679 Hz inference on a $5 microcontroller
- Learning without backpropagation - the backward pass IS the forward dynamics, perturbed

But the gap between "impressive demo" and "reproducible science" is vast:
- 40+ source files with implicit dependencies
- Documentation assumes context from months of development
- Benchmarks that outsiders can't verify
- Concepts scattered across code, docs, and LMM reflections
- No clear connection to existing literature

**The risk:** This work gets dismissed as "cool hack" rather than recognized as legitimate scientific contribution.

---

## Audience Analysis

### Primary Audiences

| Audience | Background | Needs | Entry Point |
|----------|------------|-------|-------------|
| **Embedded Systems Engineers** | ESP32, STM32, Arduino | Working code, clear hardware setup | Minimal repo + flash instructions |
| **ML Researchers** | PyTorch, backprop, theory | Mathematical formulation, comparison to literature | Paper + derivations |
| **Neuroscience/Computational Neuro** | Oscillator models, Kuramoto, biological plausibility | Connection to neural dynamics | Paper + biological framing |
| **Robotics Researchers** | ROS, control theory | Latency, real-time guarantees | Benchmark reproduction |
| **Hardware Hackers/Makers** | Breadboards, tinkering | Can I build this tonight? | Video + BOM + code |

### Secondary Audiences

| Audience | What They Want |
|----------|----------------|
| Neuromorphic computing community | How does this compare to Loihi, TrueNorth? |
| Edge AI practitioners | Power consumption, throughput, memory |
| Academic reviewers | Novelty, rigor, reproducibility |
| Investors/Industry | Applications, defensibility, scalability |

---

## Core Scientific Claims

These must be:
1. **Clearly stated** - No ambiguity about what we're claiming
2. **Falsifiable** - Specific conditions under which the claim would be false
3. **Reproducible** - Others can verify with our instructions

### Claim 1: Pulse Counting as Neural Computation

**Statement:** The ESP32-C6's PCNT (Pulse Counter) and PARLIO (Parallel I/O) peripherals can perform neural network inference without floating-point operations, achieving 1249-5679 Hz on a single core.

**Falsification conditions:**
- If inference results differ from reference implementation by >1% on test suite
- If throughput claims cannot be reproduced within 10% on specified hardware
- If the computation is actually happening in software, not hardware peripherals

**Evidence required:**
- Side-by-side comparison with NumPy reference
- Oscilloscope traces showing pulse trains
- Register-level documentation of what peripherals do what

### Claim 2: Equilibrium Propagation on Oscillator Networks

**Statement:** A network of coupled spectral oscillators can learn input-output mappings via equilibrium propagation, where the weight update rule requires only local phase correlations measured during free and nudged phases.

**Falsification conditions:**
- If learning does not converge (error doesn't decrease over epochs)
- If learned weights don't generalize (test error >> train error)
- If the update rule requires non-local information

**Evidence required:**
- Learning curves showing convergence
- Generalization tests (train/test split)
- Mathematical derivation connecting our implementation to Scellier & Bengio (2017)

### Claim 3: Self-Modification via Coherence

**Statement:** The spectral oscillator network modifies its own coupling strengths based on global coherence, creating a nested self-modification loop that operates alongside (and potentially aids) explicit learning.

**Falsification conditions:**
- If coherence doesn't actually affect coupling in measured runs
- If removing coherence feedback has no effect on dynamics
- If the two loops (coherence vs. learning) don't interact

**Evidence required:**
- Time series of coherence and coupling during operation
- Ablation study: with/without coherence feedback
- Analysis of interaction effects

### Claim 4: Hardware Substrate Independence

**Statement:** The core algorithms (pulse arithmetic, spectral oscillators, equilibrium propagation) can be implemented on any platform with pulse counting and parallel I/O capabilities, not just ESP32-C6.

**Falsification conditions:**
- If the approach fundamentally depends on ESP32-specific features
- If porting to another platform (e.g., RP2040, STM32) fails

**Evidence required:**
- Clear separation of algorithm from hardware
- At least one additional platform implementation (even partial)

---

## What's Actually Novel?

### Novel Contributions

| Contribution | Prior Art | Our Advance |
|--------------|-----------|-------------|
| Pulse counting as addition | Rotary encoders, rate coding | Systematic use for ternary matrix-vector products |
| PARLIO for parallel computation | Parallel I/O for data transfer | Using it as computational primitive |
| Equilibrium propagation on oscillators | Scellier & Bengio on Hopfield nets | Extension to Kuramoto-coupled spectral networks |
| Coherence-coupled self-modification | Hebbian learning, homeostasis | Nested loops: coherence modifies coupling, learning modifies coupling |
| 5679 Hz inference on $5 MCU | TinyML, neuromorphic | Different approach: peripheral-native, not CPU-native |

### What We're NOT Claiming

- Not claiming to beat GPUs on large models
- Not claiming biological realism (inspired by, not modeling)
- Not claiming Turing completeness (earlier Silicon Grail work is separate)
- Not claiming this is the best approach for all problems

---

## Layered Communication Strategy

### Layer 1: The Hook (5 minutes)

**Format:** README + GIF/Video

**Content:**
- One-paragraph explanation
- 30-second video of build-flash-run
- Single impressive number (5679 Hz, $5, no GPU)
- "Want to reproduce this? Keep reading."

**Goal:** Capture attention, establish credibility, invite deeper engagement.

### Layer 2: The Hands-On (30 minutes)

**Format:** Interactive Colab Notebook OR Minimal Standalone Repo

**Content:**
- Zero-setup reproduction (Colab) OR minimal setup (ESP32-C6 + USB)
- Step-by-step: flash firmware, see output, understand what happened
- Inline explanations of key concepts
- Exercises: "Change this parameter, observe that effect"

**Goal:** Visceral understanding. "I made it work. I see what's happening."

### Layer 3: The Deep Dive (2 hours)

**Format:** Tutorial Paper (Arxiv-style)

**Content:**
- Mathematical formulation
- Connection to literature (Kuramoto, equilibrium propagation, CfC)
- Derivations and proofs
- Complete experimental methodology
- Ablation studies
- Limitations and future work

**Goal:** Scientific credibility. "This is rigorous. I could review this."

### Layer 4: The Full Picture (Ongoing)

**Format:** Main Repository + Extended Documentation

**Content:**
- All implementations (40+ files)
- Historical evolution
- LMM reflections (philosophical context)
- Advanced topics (multi-chip, hardware learning)

**Goal:** Deep engagement. "I want to build on this."

---

## Deliverables

### D1: Minimal Reproduction Repository

**Name:** `pulse-arithmetic-lab` (or similar)

**Contents:**
```
pulse-arithmetic-lab/
├── README.md                 # The hook (Layer 1)
├── SETUP.md                  # Hardware requirements, 10-minute setup
├── CLAIMS.md                 # Falsifiable claims and how to test them
├── firmware/
│   ├── 01_pulse_addition/    # Simplest demo: PCNT counts pulses
│   ├── 02_parallel_dot/      # PARLIO + PCNT = dot product
│   ├── 03_spectral_oscillator/ # Single band, no learning
│   ├── 04_equilibrium_prop/  # Full learning demo
│   └── reference/            # NumPy reference implementations
├── notebooks/
│   └── pulse_arithmetic.ipynb  # Colab-compatible, simulates concepts
├── docs/
│   ├── THEORY.md             # Mathematical background
│   ├── HARDWARE.md           # Register-level details
│   └── LITERATURE.md         # Connection to prior work
└── tests/
    └── verify_claims.py      # Automated verification of claims
```

**Constraints:**
- No file depends on the main reflex repo
- Each firmware example is self-contained (<500 lines)
- Can be reproduced with ONE ESP32-C6 DevKit ($8) and a USB cable

### D2: Interactive Notebook

**Platform:** Google Colab (zero install)

**Content:**
1. Simulated pulse counting (pure Python, visualized)
2. Spectral oscillator dynamics (animated)
3. Equilibrium propagation walkthrough (step-by-step)
4. Connection to real hardware (optional, for those with device)

**Goal:** Someone with no hardware can understand the concepts in 30 minutes.

### D3: Tutorial Paper

**Format:** Arxiv preprint, 15-20 pages

**Structure:**
1. Abstract (the claim in 150 words)
2. Introduction (why this matters)
3. Background (Kuramoto, equilibrium propagation, pulse counting)
4. Method (our architecture, with diagrams)
5. Experiments (benchmarks, learning curves, ablations)
6. Discussion (limitations, future work)
7. Conclusion
8. Appendices (derivations, hardware details)

**Target venues (eventually):**
- NeurIPS (neuromorphic track)
- ICML (novel architectures)
- Nature Electronics (hardware angle)
- IEEE Embedded Systems Letters (practical engineering)

### D4: Video Walkthrough

**Format:** 10-15 minute screen recording with voiceover

**Content:**
1. What we're going to build (1 min)
2. Hardware setup (2 min)
3. Flash and run simplest demo (2 min)
4. Explain what just happened (3 min)
5. Run learning demo (2 min)
6. Explain equilibrium propagation visually (3 min)
7. Where to go next (1 min)

**Goal:** For people who learn by watching.

---

## Success Criteria

### Minimum Viable Onramp

- [ ] Someone with an ESP32-C6 can reproduce pulse_addition in <10 minutes
- [ ] Someone with no hardware can understand the concept via Colab in <30 minutes
- [ ] All claims have explicit falsification conditions
- [ ] At least 3 external reproductions (people we don't know)

### Full Success

- [ ] Paper accepted at peer-reviewed venue
- [ ] >100 GitHub stars on minimal repo
- [ ] Citations from at least 2 different research communities
- [ ] Someone builds something new on top of this work

---

## Risk Analysis

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Claims don't hold under scrutiny | Low | Critical | Pre-register falsification tests, run them publicly |
| Too complex to reproduce | Medium | High | Ruthless simplification of minimal repo |
| Dismissed as "just a hack" | Medium | Medium | Strong connection to theory, peer review |
| Scooped by larger lab | Low | Medium | Publish preprint early, open source everything |
| No one cares | Medium | High | Find the 10 people who would care, reach them directly |

---

## Timeline

### Phase 1: Foundation (1 week)

- [ ] Create minimal repo structure
- [ ] Port simplest demos (pulse_addition, parallel_dot)
- [ ] Write CLAIMS.md with falsification conditions
- [ ] Draft README hook

### Phase 2: Notebook + Validation (1 week)

- [ ] Build Colab notebook (simulated concepts)
- [ ] Create verify_claims.py test suite
- [ ] Run all tests, document results
- [ ] Get 1-2 friendly external reproductions

### Phase 3: Paper Draft (2 weeks)

- [ ] Write full tutorial paper
- [ ] Create all figures and diagrams
- [ ] Run ablation studies
- [ ] Internal review and revision

### Phase 4: Release (1 week)

- [ ] Record video walkthrough
- [ ] Post to Arxiv
- [ ] Announce on relevant communities (Reddit, HN, Twitter, Discord)
- [ ] Respond to feedback, fix issues

---

## Decisions (Feb 5, 2026)

1. **Naming:** "Pulse Arithmetic Engine" is fine for now. Descriptive > clever.

2. **Scope:** Full path from zero to hero. User with a C6 should be able to reproduce the complete tech demo, including learning.

3. **Hardware:** ESP32-C6 only. That's what we have, that's what we support.

4. **Biological framing:** Zero. Pure engineering and math. No neuroscience language, no biological plausibility claims.

5. **Commercial:** No conflicts. This research is funded by EntroMorphic, LLC. Open science is the goal.

---

## Next Steps

1. **Validate this PRD** - Does this capture the right scope and priorities?
2. **Create minimal repo** - Start with pulse_addition, the simplest possible demo
3. **Draft CLAIMS.md** - Make our claims explicit and falsifiable
4. **Build Colab notebook** - Zero-install entry point

---

*"Science is a way of trying not to fool yourself. The first principle is that you must not fool yourself—and you are the easiest person to fool."*
— Richard Feynman

We must not fool ourselves about what we've built. The onramp must be honest, reproducible, and falsifiable. If we've discovered something real, it will survive scrutiny. If we haven't, we need to know.

---

**Document Status:** Draft v1
**Author:** Claude + Human
**Date:** February 5, 2026
