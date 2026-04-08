# Synthesis: Step-Changes for The Reflex

## Decision: Ship, Then Build

The project has three findings on silicon. None of them are published. Every hour spent adding features to an unpublished system is a hour not spent getting the findings on record.

## Immediate Actions (This Week)

### 1. Multi-Seed Validation (30 minutes)

Run Tests 12-14 with three different weight seeds. Change `init_lp_core_weights(0xCAFE1234)` to each seed, reflash, run. Normal sender mode.

Seeds: `0xCAFE1234` (existing), `0xDEAD5678` (new), `0xBEEF9ABC` (new).

Report per-seed:
- TEST 12: LP Hamming matrix (sign + MTFP), VDB causal necessity
- TEST 13: Ablation Hamming
- TEST 14: Mean Hamming 14A vs 14C, gate bias activation

Add a table to Stratum 1 paper: "Robustness across weight seeds."

### 2. Submit Stratum 3 (Same Day)

The prior-signal separation note is complete. Five-component framework, silicon verification, comparison table, red-teamed, scoped to context-neglect. Target: arXiv preprint (immediate visibility) + venue TBD.

### 3. Submit Stratum 1 (After Multi-Seed Data)

The kinetic attention paper with multi-seed table, MTFP results, UART noted as condition. Target: embedded systems venue (IEEE TVLSI, DAC, tinyML).

## Near-Term Actions (Next 2 Weeks)

### 4. Multi-Run TEST 14C (2 Hours)

Three seeds, transition sender. Report per-seed:
- Crossover step
- Regression under ablation (P1 reassertion)
- Bias decay trace

Add results to Stratum 2 paper. Submit.

### 5. UART Falsification (One Hardware Session)

Wire GPIO 16/17 UART, battery power, full suite, 3 seeds. Update all papers with the result. Publish as an addendum if papers are already submitted.

## Future Tracks (After Papers Ship)

| Track | What | When | New Paper? |
|-------|------|------|-----------|
| Dynamic Scaffolding (Pillar 1) | VDB sliding window: prune redundant nodes, retain distinctive | After papers | Engineering note |
| Insert Rate Tuning | Change from 1/8 to 1/16 or 1/32 for longer runs | Immediate (one-line) | No |
| GIE MTFP | Encode GIE dot magnitudes for HP-side measurement | Test empirically | Probably not — likely redundant with LP dot MTFP |
| Hebbian GIE (Pillar 3) | LP core writes weight updates to GDMA chain | After all fixed-weight papers published | Yes — separate paper: "From Fixed to Plastic" |
| SAMA (Pillar 2) | Cross-agent GIE state sharing via ESP-NOW | After Pillar 1 stable | Yes — multi-agent paper |
| Nucleo APU | SPI/QSPI acceleration, MTFP inference on coprocessor | When single-chip hits compute ceiling | Yes — heterogeneous compute paper |
| Novel Pattern Detection | "I don't know" as first-class output | When pattern count scales beyond 4 | Feature, not paper |
| Long-Run Dynamics | 30-minute runs to characterize post-VDB-full behavior | After scaffolding or insert rate tuning | Measurement only |
| Graph Visualization | BFS structure, cross-pattern edges, cluster analysis | Whenever curious | Diagnostic only |

## Success Criteria

- [ ] Multi-seed data shows gate bias improvement in ≥2 of 3 seeds
- [ ] Multi-seed data shows VDB causal necessity in ≥2 of 3 seeds
- [ ] Stratum 3 on arXiv by end of week
- [ ] Stratum 1 submitted with multi-seed table by end of week
- [ ] Stratum 2 submitted with multi-run 14C by end of next week
- [ ] UART falsification complete within 2 weeks

## The Principle

**Don't build features for an unpublished system.** Features are engineering. Findings are research. The findings exist. They need to be on paper, reviewed, and on record. Then the engineering begins.

The constraint that created the architecture also applies to the project itself: the limitation (papers not yet submitted) is the generative force (it focuses effort on validation and publication rather than premature feature development).

Ship. Then build.
