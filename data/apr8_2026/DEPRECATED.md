# DEPRECATED — do not cite this dataset

All `results_*.log` files in this directory were captured **before** two compounding bugs were fixed. They are invalid for any TEST 14C claim.

**Superseded by:** `data/apr9_2026/SUMMARY.md` + `results_final_seed_{a,b,c}.log`.

---

## Why these logs are invalid

1. **`trix_enabled` was not set in Tests 12-13** (fixed April 8 in commit `f97ac1c`). The ISR never ran TriX classification. `trix_pred = -1` matched no pattern accumulator, so the agreement-weighted gate bias stayed at zero for the entire Full condition. Every "Full" number in every apr8 log is actually measuring the no-bias condition.

2. **Enrollment starvation in transition mode** (fixed April 9 in commit `63877f7`). The sender's TRANSITION_MODE started sending P1 immediately on boot, so Board A's Test 11 Phase 0a (30s observation window) only ever saw pattern P1. `sig[0]`, `sig[2]`, and `sig[3]` were computed from zero samples. TriX's argmax over `sig[] · hidden` could never select P2. **Every condition in every apr8 transition-mode log is affected, not just Full** — No-bias and Ablation also had broken TriX classification because the classifier itself had no P2 signature.

The April 8 `DO_THIS_NEXT.md` (pre-rewrite) incorrectly concluded that the VDB-stabilization data from No-bias and Ablation was valid. It was not. Bug #2 was discovered on April 9 after running Seed A against the just-fixed sender and observing enrollment output.

## What is still valid

- **TEST 13 (CMD 4 distillation):** Measures LP hidden-state Hamming between CMD 4 (CfC only) and CMD 5 (CfC + VDB blend). Does not depend on TriX classification. The "VDB causal necessity" finding is independent of both bugs.
- **LP characterization output** (Path A/B regime detection, dot magnitude probes): Independent of TriX classification.
- **Alignment traces** (`align_P1` / `align_P2`): Computed from LP MTFP means accumulated during TEST 14C, not from `sig[]`. The dynamic LP state is partially independent of the broken TriX classifier. The QUALITATIVE pattern (ablation regresses, VDB stabilizes) may hold even though the QUANTITATIVE numbers are unreliable.
- **Tests 1-8** (GIE, LP core, VDB, pipeline, feedback): Independent of TriX classification and enrollment.

## What is invalid

- **All TriX accuracy numbers** (TriX@15, per-pattern accuracy, ISR vs CPU agreement)
- **Classification-dependent metrics** (`pred` latency, crossover step, bias release timing)
- **Multi-seed TEST 14C crossover numbers** (0, 22, 2 for Seeds A, B, C)
- **Anything citing "100% accuracy"** from these runs

For authoritative numbers: `data/apr11_2026/SUMMARY.md`.

## Files preserved for historical record

The logs are kept in place because the research history matters and because the "what did the data look like before the fix" question may come up in review. They are **not** to be cited in papers.

---

*Marked deprecated April 9, 2026. See `data/apr9_2026/SUMMARY.md` for the authoritative dataset.*
