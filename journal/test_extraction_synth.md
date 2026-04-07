# Synthesis: Test Extraction Refactor

## Architecture

Single file (`geometry_cfc_freerun.c`), one function per test, clean orchestrator in `app_main`.

```
geometry_cfc_freerun.c (after refactor):
  ┌─ File-scope state (promoted from nested scopes) ─┐
  │  sig[4][128]         — TriX signatures             │
  │  drain_buf[32]       — ESP-NOW drain buffer         │
  │  t12_mean1/2[16]     — TEST 12 → TEST 13 handoff   │
  │  t12_p1p3/p1p2       — TEST 12 → TEST 13 handoff   │
  │  #define NOVELTY_THRESHOLD 60                       │
  │  #define T11_DRAIN_MS 10                            │
  └─────────────────────────────────────────────────────┘

  ┌─ Functions ─────────────────────────────────────────┐
  │  static int run_test_1(void)  — free-running loop   │
  │  static int run_test_2(void)  — hidden evolves      │
  │  static int run_test_3(void)  — dot accuracy        │
  │  static int run_test_4(void)  — LP core processor   │
  │  static int run_test_5(void)  — VDB NSW graph       │
  │  static int run_test_6(void)  — CfC→VDB pipeline    │
  │  static int run_test_7(void)  — reflex channel      │
  │  static int run_test_8(void)  — VDB→CfC feedback    │
  │  static int run_test_9(void)  — ESP-NOW receive     │
  │  static int run_test_10(void) — ESP-NOW live input   │
  │  static int run_test_11(void) — pattern classif.    │
  │    (internally: 30s warmup, sig computation,         │
  │     sig install as W_f, TriX Cube, classification)   │
  │  static int run_test_12(void) — memory-mod attn     │
  │  static int run_test_13(void) — CMD 4 ablation      │
  │  static int run_test_14(void) — kinetic attention    │
  │  static void run_lp_char(void) — LP dynamics diag   │
  │  static void run_lp_dot_diag(void) — LP dot probe   │
  └─────────────────────────────────────────────────────┘

  ┌─ app_main ──────────────────────────────────────────┐
  │  init peripherals (unchanged)                        │
  │  prime_pipeline()                                    │
  │  int test_count = 0, pass_count = 0;                 │
  │  test_count++; pass_count += run_test_1();           │
  │  test_count++; pass_count += run_test_2();           │
  │  ... through run_test_14() ...                       │
  │  run_lp_char();    // not counted                    │
  │  run_lp_dot_diag(); // not counted                   │
  │  print summary                                       │
  └─────────────────────────────────────────────────────┘
```

## Key Decisions

1. **One file, not fourteen.** The test progression is a narrative. Splitting destroys it. Named functions provide navigability without fragmentation.

2. **Promote 6 items to file scope.** `drain_buf`, `NOVELTY_THRESHOLD`, `T11_DRAIN_MS`, `t12_mean1`, `t12_mean2`, `t12_p1p3_result`, `t12_p1p2_result`. These are genuinely shared across test boundaries. Keeping them in nested scopes was an accident of the monolithic structure, not a design choice.

3. **Binary return: 1=pass, 0=fail.** Every test function returns int. `app_main` does `test_count++; pass_count += result;` for each. The skip-counts-as-fail semantic is preserved because skipped tests return 0.

4. **Tests 11-14 are independent functions that depend on ordering.** Test 11 computes and installs TriX signatures. Tests 12-14 assume signatures are installed. This is a test-ordering dependency enforced by `app_main`, not a runtime coupling. Each test starts and stops its own GIE session.

5. **The TriX Cube stays in run_test_11().** It's 800 lines and specific to Test 11's classification methodology. Not shared.

6. **LP CHAR and LP Dot Diagnostic return void.** They are diagnostics, not pass/fail tests. They are not counted in the test tally.

## Implementation Spec

### Step 1: Promote shared state to file scope

Move to file scope (near existing `sig[]` declaration at line 37):
```c
static int8_t sig[NUM_TEMPLATES][CFC_INPUT_DIM];        // already here
static espnow_rx_entry_t drain_buf[32];                  // from Test 11
#define NOVELTY_THRESHOLD 60
#define T11_DRAIN_MS      10
/* TEST 12 → TEST 13 handoff */
static int8_t  t12_mean1[LP_HIDDEN_DIM];
static int8_t  t12_mean2[LP_HIDDEN_DIM];
static int     t12_p1p3_result = -1;
static int     t12_p1p2_result = -1;
static int     t12_n1 = 0, t12_n2 = 0;
```

### Step 2: Extract each test

Mechanical transformation for each test block:
1. Cut the test's `{ }` block from `app_main`
2. Wrap it in `static int run_test_N(void) {`
3. Replace `test_count++; if (ok) pass_count++;` with `return ok;`
4. For ISR-ready checks: `if (!gie_isr_ready()) { printf("SKIPPED\n"); return 0; }`
5. Any `static` locals inside the block stay as function-scope statics (identical lifetime)

### Step 3: Rewrite app_main

```c
void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));
    /* ... banner ... */
    /* ... peripheral init (unchanged) ... */
    prime_pipeline();

    int test_count = 0, pass_count = 0;

    test_count++; pass_count += run_test_1();
    test_count++; pass_count += run_test_2();
    test_count++; pass_count += run_test_3();
    test_count++; pass_count += run_test_4();
    test_count++; pass_count += run_test_5();
    test_count++; pass_count += run_test_6();
    test_count++; pass_count += run_test_7();
    test_count++; pass_count += run_test_8();
    test_count++; pass_count += run_test_9();
    test_count++; pass_count += run_test_10();
    test_count++; pass_count += run_test_11();
    test_count++; pass_count += run_test_12();
    test_count++; pass_count += run_test_13();
    test_count++; pass_count += run_test_14();

    run_lp_char();
    run_lp_dot_diag();

    /* Summary */
    printf("============================================================\n");
    printf("  RESULTS: %d / %d PASSED\n", pass_count, test_count);
    printf("============================================================\n");
    if (pass_count == test_count) {
        printf("\n  *** FULL SYSTEM VERIFIED ***\n");
        /* ... existing detail lines ... */
    }
    fflush(stdout);
    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}
```

### Step 4: Add TOC comment at top of file

```c
/*
 * Test Suite Table of Contents:
 *
 *   run_test_1()   — Free-running loop count (GIE basic)
 *   run_test_2()   — Hidden state evolves autonomously
 *   run_test_3()   — Per-neuron dot accuracy vs CPU reference
 *   run_test_4()   — LP Core geometric processor
 *   run_test_5()   — Ternary VDB — NSW graph (M=7), 64 nodes
 *   run_test_6()   — CfC → VDB pipeline (CMD 4)
 *   run_test_7()   — Reflex channel coordination
 *   run_test_8()   — VDB → CfC feedback loop (CMD 5)
 *   run_test_9()   — ESP-NOW receive from Board B
 *   run_test_10()  — ESP-NOW → GIE live input
 *   run_test_11()  — Pattern classification (stream CfC + TriX)
 *   run_test_12()  — Memory-modulated adaptive attention
 *   run_test_13()  — CMD 4 ablation (VDB causal necessity)
 *   run_test_14()  — Kinetic attention (agreement-weighted gate bias)
 *   run_lp_char()  — LP dynamics characterization (diagnostic)
 *   run_lp_dot_diag() — LP dot magnitude probe (diagnostic)
 */
```

## Success Criteria

- [ ] Every test function compiles to identical machine code as the inline version (modulo function prologue/epilogue)
- [ ] `app_main` is under 100 lines
- [ ] A reviewer can find any test by name in under 5 seconds (TOC + function name search)
- [ ] No behavior change: 14/14 PASS on silicon after refactor
- [ ] File stays as one compilation unit — no new .c files needed
- [ ] Backup exists for instant revert if build fails

## Explicit Tension Handling

- **Compile risk:** Mitigated by backup copy + git branch. Transformation is mechanical (wrap in function, change return). No logic changes.
- **Test 11-14 coupling:** Resolved by Node 6 insight — they're ordering-dependent, not runtime-coupled. File-scope state for the 6 promoted items.
- **Skip semantic:** Preserved by binary return. Skip returns 0, `app_main` always increments test_count.
