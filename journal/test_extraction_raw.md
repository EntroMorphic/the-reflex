# Raw Thoughts: Test Extraction Refactor

## Stream of Consciousness

The file is 4123 lines. One function: `app_main()`. 14 tests, an LP characterization diagnostic, and an LP dot magnitude probe, all inline. It works — 14/14 PASS. The question is whether extracting tests into functions makes the code better or just different.

The real audience is paper reviewers. The ROADMAP said: "Reviewers must be able to find the difference between TEST 14A and 14B in under 10 lines." The monolith split (gie_engine.c extracted) already addressed the core of that concern — the engine logic is separate from the tests. What remains is test *navigation* within the harness file.

My gut says the right decomposition is NOT 14 separate files. That would scatter related code across the repo and make it harder to follow the test progression. The tests tell a story: 1-3 verify the GIE, 4 adds LP, 5 adds VDB, 6 adds pipeline, 7 adds channels, 8 adds feedback, 9-10 add live input, 11 adds classification, 12-13 test memory modulation, 14 tests kinetic attention. That narrative matters.

What scares me: Tests 11-14 are deeply coupled. They share a running GIE session, TriX signatures computed during a 30-second observation phase, a drain buffer, and TEST 12 results flow into TEST 13's attribution comparison. Extracting them naively would either (a) break the coupling, (b) require a massive shared context struct, or (c) result in functions that are just wrappers around the same inline code with extra parameters.

The static arrays are a hazard. `sig[4][128]` is file-scope. `drain_buf[32]`, `cube[7]` (the TriX Cube), `sig_sum[4][128]` are declared `static` inside the Test 11 scope. Tests 12-14 reference `sig[]` (file scope) and `drain_buf[]` (Test 11 scope). The `#define` macros (`NOVELTY_THRESHOLD`, `T11_DRAIN_MS`, etc.) are scoped inside Test 11's block but used by Tests 12-14 because they're nested.

Another concern: `test_count` and `pass_count` are local variables in `app_main`, threaded through every test. Each test does `test_count++; if (ok) pass_count++;`. Any extraction needs to handle these — either by pointer, return value, or global.

The LP CHAR and LP Dot Diagnostic at the end are clean and independent. They don't touch test_count/pass_count (LP CHAR explicitly says "diagnostic — not counted in pass/fail"). The dot diagnostic uses `sig[]` and `lp_W_f/lp_W_g` but doesn't modify them.

What would the naive approach look like? 16 functions: `run_test_1()` through `run_test_14()`, `run_lp_char()`, `run_lp_dot_diag()`. Each returns 1/0 for pass/fail (or -1 for "not counted"). `app_main` calls them in sequence and tallies. Tests 1-10 are clean extractions. Tests 11-14 get wrapped in `run_live_classification_suite()` that handles the shared warmup and returns the count of passed tests out of 4.

What's probably wrong with that: the Test 11 scope is ~800 lines and contains the TriX Cube classifier, the signature observation phase, the signature computation, the warmup, and the actual classification test. Tests 12-14 are children of that scope. Pulling 12-14 out of 11's scope means either (a) passing all shared state as parameters, or (b) promoting the shared state to file scope.

Option (b) — promote shared state to file scope — is simpler and more honest. The state IS shared. Pretending it's local by passing 8 parameters is ceremony. Move `drain_buf`, `sig_sum`, `NOVELTY_THRESHOLD`, `T11_DRAIN_MS` to file scope. Then each test function can access them directly.

## Questions Arising

1. Should Tests 11-14 remain one function or become four functions with a shared init?
2. How to handle `test_count`/`pass_count` — return values, pointer parameters, or globals?
3. Should the TriX Cube (specific to Test 11) stay in the Test 11 function or become its own module?
4. Does this refactor actually help paper reviewers, or does it just make the code "cleaner" in a way that doesn't matter?
5. What's the risk of introducing a bug in a file I can't compile?
6. Should LP CHAR and LP Dot Diagnostic become separate files or just separate functions?

## First Instincts

- Promote shared state to file scope
- Each test becomes a function returning int (1=pass, 0=fail, -1=skip/diagnostic)
- Tests 11-14 get a shared `trix_warmup()` function, then each is independent
- `app_main` becomes ~80 lines: init, call tests, tally, summary
- Keep everything in one file — the narrative progression matters
- LP CHAR and LP Dot Diagnostic become functions at the bottom of the same file
