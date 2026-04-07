# Reflections: Test Extraction Refactor

## Core Insight

The coupling between Tests 11-14 is thinner than it appeared. They share computed signatures (`sig[]`, already file-scope) and a few macros, but each test starts its own GIE session. The "deeply coupled" perception was wrong — what's coupled is the *setup* (signature observation), not the *tests*. This means a clean decomposition is: one `trix_build_signatures()` function for the shared warmup, then each test fully independent.

## Resolved Tensions

### Node 2 vs Node 4: Shared state promotion vs. one-file narrative

**Resolution:** Promote the 3 items that are actually shared across test boundaries to file scope:
- `NOVELTY_THRESHOLD` → file-scope `#define`
- `T11_DRAIN_MS` → file-scope `#define`
- `drain_buf[32]` → file-scope `static`

Everything else stays in its respective function scope:
- `cube[7]` stays in `run_test_11()`
- `sig_sum[4][128]` stays in `trix_build_signatures()`
- `T11_WINDOW_MS`, `MAX_TEST_SAMPLES` stay in `run_test_11()`

The file remains one compilation unit. Narrative preserved.

### Node 3 vs Node 5: Return value design vs. skip semantic

**Resolution:** Use option (a) with a refinement. Each function returns:
- `1` = passed (test_count++, pass_count++)
- `0` = failed (test_count++)
- `-1` = skipped without counting (neither increments — only for "ISR not ready")

Wait — actually, re-reading the current code: when the ISR isn't ready, the code does `test_count++` (counts it as attempted) but not `pass_count++`. So a skip IS a fail in the current accounting. This means return `0` for skip/fail, `1` for pass. Binary. Simple.

Actually, let me re-examine. Test 1 code:
```c
if (!gie_isr_ready()) {
    printf("  SKIPPED\n");
    test_count++;     // counted
    // pass_count NOT incremented
} else {
    // ... run test ...
    test_count++;
    if (ok) pass_count++;
}
```

So: skip increments test_count, doesn't increment pass_count. This is equivalent to "fail". Return `0`. For the ISR-ready check, the function itself can check `gie_isr_ready()` and return `0` if not ready. `app_main` just does `pass_count += run_test_1(); test_count++;` for every test.

Simpler: every test function returns 1 or 0. `app_main` always increments `test_count`, and adds the return to `pass_count`.

### Node 6 resolution: The warm-start problem is not a problem

Tests 12-14 each call `start_freerun()` / `stop_freerun()` independently. The only prerequisite is that `sig[]` has been populated. Since `sig[]` is file-scope and populated by `trix_build_signatures()` (called during Test 11), Tests 12-14 can be called after Test 11 returns, and they'll find valid signatures.

But wait — Test 11 also installs signatures as W_f weights (Phase 0c) and sets `gate_threshold`. Tests 12-14 rely on this. These are engine globals (`cfc.W_f`, `gate_threshold`), not local variables. They persist after Test 11 returns. So the dependency is on engine state set during Test 11, not on scoped variables. This is the same pattern as "Tests 1-3 initialize the engine" — subsequent tests inherit engine state.

**Decision:** Document that Tests 12-14 require Test 11 to run first (it installs TriX signatures). The function signatures don't need to encode this — it's a test-ordering dependency, and `app_main` enforces the order.

## Hidden Assumptions Challenged

1. **"Tests 11-14 are deeply coupled"** — Wrong. They share setup artifacts (signatures, W_f weights, macros) but each manages its own GIE session. The coupling is initialization dependency, not runtime dependency.

2. **"Extracting tests into functions changes behavior"** — Only if scope or lifetime changes. For `static` locals, lifetime is program-wide regardless of scope. For `#define` macros, scope is file-wide regardless of where defined. For engine globals, scope is already global. The transformation is purely mechanical.

3. **"Test 12 results flow into Test 13"** — True. `t12_mean1[]`, `t12_mean2[]`, `t12_p1p3_result` are computed in Test 12 and used in Test 13 for attribution comparison. Solution: promote these to file-scope static. Three arrays and two ints. Minimal leakage.

## What I Now Understand

The decomposition is:

```
File scope:
  sig[4][128]         (already there)
  drain_buf[32]       (promote from Test 11 scope)
  NOVELTY_THRESHOLD   (promote from Test 11 scope)
  T11_DRAIN_MS        (promote from Test 11 scope)
  t12_mean1[16]       (promote from Test 12 scope, for Test 13)
  t12_mean2[16]       (promote from Test 12 scope, for Test 13)
  t12_p1p3_result     (promote from Test 12 scope, for Test 13)
  t12_p1p2_result     (promote from Test 12 scope, for Test 13)

Functions:
  trix_build_signatures()  — 30s warmup, compute sig[], install as W_f, set gate_threshold
  run_test_1() through run_test_10()  — fully independent
  run_test_11(sig, drain_buf) — TriX Cube classification (calls trix_build_signatures internally)
  run_test_12() — memory-modulated attention
  run_test_13() — CMD 4 ablation (reads t12_ results)
  run_test_14() — kinetic attention (3 conditions)
  run_lp_char() — LP dynamics characterization (diagnostic, no pass/fail)
  run_lp_dot_diag() — LP dot magnitude probe (diagnostic)

app_main():
  init peripherals
  test_count = 0, pass_count = 0
  for each test: pass_count += run_test_N(); test_count++;
  run_lp_char()      — not counted
  run_lp_dot_diag()  — not counted
  print summary
```

Every test function has the same shape: print banner, do work, print result, return 1 or 0. The body is the existing code, unmodified. The transformation is: remove the outermost `{ }` block, add `static int run_test_N(void) {`, move `test_count++; if (ok) pass_count++;` to `return ok;`, done.

## Remaining Risk

The only real risk is a scoping error where a variable declared in `app_main` before the test block is referenced inside the test. I need to verify that each test block is self-contained — no references to variables declared between tests. The init block (lines 43-98) sets up peripherals and loads the LP binary — these are all engine globals, not local variables. `test_count` and `pass_count` are the only `app_main` locals threaded through, and those are handled by the return-value pattern.
