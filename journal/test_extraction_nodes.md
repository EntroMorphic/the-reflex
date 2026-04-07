# Nodes of Interest: Test Extraction Refactor

## Node 1: The Three Tiers of Coupling

Tests fall into three coupling tiers:
- **Tier A (independent):** Tests 1-8. No shared state beyond `cfc`, `loop_count`, and other engine globals. Each reinitializes with `cfc_init()`.
- **Tier B (ESP-NOW dependent):** Tests 9-10. Need `espnow_state_t` and a running ESP-NOW receiver. Independent of each other.
- **Tier C (deeply coupled):** Tests 11-14. Share a continuous GIE session, TriX signatures, drain buffer, NOVELTY_THRESHOLD, and cross-test data flow (TEST 12 means → TEST 13 attribution).

Why it matters: Tier A and B are trivial extractions. Tier C is where all the complexity lives.

## Node 2: The Shared State Inventory (Tier C)

Exact shared state between Tests 11-14:
- `sig[4][128]` — file-scope, computed in Test 11 Phase 0b, read by Tests 12-14 and LP Dot Diagnostic
- `sig_sum[4][128]` — static inside Test 11 scope, used only during signature computation
- `drain_buf[32]` — static inside Test 11 scope, used by Tests 11-14
- `cube[7]` — TriX Cube, static inside Test 11, used only by Test 11's classification phase
- `NOVELTY_THRESHOLD` (60) — #define inside Test 11, used by Tests 12-14
- `T11_DRAIN_MS` (10) — #define inside Test 11, used by Tests 12-14
- `T11_WINDOW_MS` (1000) — #define inside Test 11, used only by Test 11
- `MAX_TEST_SAMPLES` (32) — #define inside Test 11, used only by Test 11
- `t12_mean1[16]`, `t12_mean2[16]`, `t12_p1p3_result`, `t12_p1p2_result` — TEST 12 results consumed by TEST 13

Tension: promoting everything to file scope is simple but leaks implementation details. Keeping it scoped requires parameter passing.

## Node 3: Return Value Design

Three options for test_count/pass_count:
- **(a) Return int:** 1=pass, 0=fail, -1=skip. `app_main` tallies. Clean but loses the skip-without-count semantic (Test 1 increments test_count even when skipped).
- **(b) Pointer parameters:** `run_test_1(int *tc, int *pc)`. Faithful to current behavior but verbose.
- **(c) Struct return:** `typedef struct { int ran; int passed; } test_result_t;`. Clean, extensible, slightly over-engineered for the use case.

Tension with Node 5: the simplest option (a) changes behavior for the "skipped" case.

## Node 4: The Narrative Value of One File

Tests 1-14 tell a progression story. A reviewer reading top-to-bottom sees the system grow: peripheral loop → LP core → VDB → pipeline → feedback → live input → classification → memory modulation → kinetic attention. Splitting into 14 files destroys this.

Keeping everything in one file with named functions preserves the narrative while adding navigability. A TOC at the top of the file would let a reviewer jump to any test in seconds.

## Node 5: The Skip/Fail Semantic

Current behavior when ISR allocation fails: `test_count++` but NOT `pass_count++`. This means "we tried, it failed" — the test is counted in the denominator. If the ISR never allocated, the final "14/14 PASS" would instead read "14/15" (one skip counted as fail).

Any extraction must preserve this: a skipped test still increments the total count.

## Node 6: The Warm-Start Problem (Tests 11-14)

Test 11 runs a 30-second warmup + signature observation. The GIE stays running. Tests 12-14 use the signatures and the still-running GIE.

If I extract Tests 12-14 as separate functions, the GIE must remain running between calls. This is fine — the GIE state is all in globals/peripherals, not local variables. But the TriX signatures must be available. Since `sig[]` is already file-scope, this works.

The real issue: the `stop_freerun()` at the end of each test. Currently, Test 11 calls `stop_freerun()` before Test 12 starts its own GIE session. Tests 12 and 13 each start/stop their own sessions. Test 14 starts its own session for each of its 3 conditions.

So Tests 12-14 are NOT continuing Test 11's session — they each start fresh. The only shared state is the computed signatures. This is much cleaner than I initially thought.

## Node 7: Compile Risk

I cannot build this firmware. Every change is a hypothesis about correctness. The mechanical transformation — wrapping existing code in a function without changing any logic — has low but non-zero risk. The risks are:
- Variable scope: a local becomes inaccessible after extraction
- Static lifetime: a `static` variable inside a block vs. inside a function behaves differently (answer: identically, both persist for program lifetime)
- Macro scope: `#define` inside a block is visible to the rest of the file. Moving it to file scope is a no-op.

## Node 8: What Reviewers Actually Need

A paper reviewer looking at this firmware needs to:
1. Find the test that matches a paper claim (e.g., "TEST 14 measures kinetic attention")
2. Read the test's pass criteria
3. Verify the test measures what the paper says it measures
4. See that the test is self-contained (not dependent on hidden state from a prior test)

Named functions with descriptive signatures directly serve needs 1 and 4. A TOC serves need 1. Clear pass criteria (already present) serve needs 2 and 3.
