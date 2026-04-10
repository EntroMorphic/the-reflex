# DEPRECATED — do not cite this dataset

All `results_*.log` files in this directory were captured **before** two compounding bugs were fixed. They are invalid for any TEST 14C claim.

**Superseded by:** `data/apr9_2026/SUMMARY.md` + `results_final_seed_{a,b,c}.log`.

---

## Why these logs are invalid

1. **`trix_enabled` was not set in Tests 12-13** (fixed April 8 in commit `f97ac1c`). The ISR never ran TriX classification. `trix_pred = -1` matched no pattern accumulator, so the agreement-weighted gate bias stayed at zero for the entire Full condition. Every "Full" number in every apr8 log is actually measuring the no-bias condition.

2. **Enrollment starvation in transition mode** (fixed April 9 in commit `63877f7`). The sender's TRANSITION_MODE started sending P1 immediately on boot, so Board A's Test 11 Phase 0a (30s observation window) only ever saw pattern P1. `sig[0]`, `sig[2]`, and `sig[3]` were computed from zero samples. TriX's argmax over `sig[] · hidden` could never select P2. **Every condition in every apr8 transition-mode log is affected, not just Full** — No-bias and Ablation also had broken TriX classification because the classifier itself had no P2 signature.

The April 8 `DO_THIS_NEXT.md` (pre-rewrite) incorrectly concluded that the VDB-stabilization data from No-bias and Ablation was valid. It was not. Bug #2 was discovered on April 9 after running Seed A against the just-fixed sender and observing enrollment output.

## What may still be partially salvageable

- **Alignment traces** (`align_P1` / `align_P2`) are computed from LP MTFP means accumulated *during* TEST 14C, not from `sig[]`. They reflect the dynamic LP state, which is partially independent of the broken TriX classifier. A careful reader could compare alignment-trace patterns across conditions in apr8 logs against apr9 logs to check whether the ablation-regression signal was visible before the fix. But nothing in these logs can be cited as a quantitative TriX accuracy claim.
- **LP characterization output** (Path A/B regime detection) is independent of TriX classification and should still be valid.

Everything else — TriX@15 accuracy, `pred` latency, crossover step, bias release timing — must come from `data/apr9_2026/`.

## Files preserved for historical record

The logs are kept in place because the research history matters and because the "what did the data look like before the fix" question may come up in review. They are **not** to be cited in papers.

---

*Marked deprecated April 9, 2026. See `data/apr9_2026/SUMMARY.md` for the authoritative dataset.*
