# The Reflex: Full Repository Audit

**Date:** April 6, 2026
**Auditor:** Claude (Opus 4.6)
**Scope:** Complete first-hand review of all load-bearing code, documentation, build system, and repository structure.
**Method:** Every file read directly. No subagents. No sampling.

---

## Executive Summary

The Reflex is a three-layer ternary neural computing system running on an ESP32-C6 microcontroller. The core claims — 100% classification accuracy at 430.8 Hz on peripheral hardware, pattern-specific LP hidden states after 90 seconds, VDB episodic memory causally necessary — are backed by 13 progressive silicon-verified tests and clean, well-commented code.

The architecture is sound. The ISR timing is correct. The hand-written RISC-V assembly is correct. The test methodology is rigorous. The documentation is publication-grade.

Six issues were identified. None threaten correctness. All concern maintainability, reviewer accessibility, and hygiene.

---

## Files Audited

### Load-Bearing Code (Production Path)

| File | Lines | Role |
|------|-------|------|
| `embedded/main/geometry_cfc_freerun.c` | 5,069 | GIE engine, ISR, TriX, LP interface, test harness, app_main |
| `embedded/main/ulp/main.S` | 1,931 | Hand-written RISC-V: CfC (CMD 1), VDB search (CMD 2), VDB insert (CMD 3), CfC+VDB pipeline (CMD 4), CfC+VDB+Feedback (CMD 5) |
| `embedded/main/reflex_vdb.c` | 215 | HP-side VDB API: pack trits, dispatch commands, read results |
| `embedded/include/reflex.h` | 182 | Core primitive: reflex_channel_t, fences, cycle counter |
| `embedded/include/reflex_vdb.h` | 143 | VDB API declarations |
| `embedded/include/reflex_espnow.h` | 227 | ESP-NOW receiver: ring buffer, callbacks, drain API (header-only) |
| `embedded/main/CMakeLists.txt` | 48 | Build config: sources, ULP embedding, component deps |

### Reference / Verification Code

| File | Lines | Role |
|------|-------|------|
| `embedded/main/ulp/main.c` | 276 | LP core C reference (CMD 1 only; not compiled in production) |
| `sim/test14c.c` | 557 | AVX2 simulation of TEST 14C transition experiment (3 conditions x 1000 trials) |

### Documentation

| File | Purpose |
|------|---------|
| `README.md` | Project overview, three-layer architecture, key results, repo structure |
| `ROADMAP.md` | Strategic roadmap: Phase 5, three pillars, publication strategy |
| `docs/CURRENT_STATUS.md` | Up-to-date milestone history, what's next, blocking prerequisites |
| `docs/KINETIC_ATTENTION.md` | Phase 5 design: agreement-weighted gate bias, TEST 14 spec |
| `docs/PRIOR_SIGNAL_SEPARATION.md` | Theoretical note: structural hallucination resistance |
| `docs/THE_PRIOR_AS_VOICE.md` | Perspective paper: technical, engineering, ontological |
| `docs/LCACHE_REFLEX_OPCODES.md` | L-Cache opcode spec: 12 AVX2 opcodes, 1:1 firmware correspondence |
| `docs/MEMORY_MODULATED_ATTENTION.md` | Paper-quality writeup of TEST 12 |
| `docs/SESSION_MAR22_2026.md` | March 22 session record: 13/13 PASS |
| `docs/SESSION_MAR23_2026.md` | March 23 session record: LMM assessment, temporal context reframe |
| `docs/MILESTONE_PROGRESSION.md` | All 37 milestones, full narrative |
| `docs/HARDWARE_TOPOLOGY.md` | Nucleo <-> C6 wiring spec (APU-expanded mode) |

### Build & Config

| File | Purpose |
|------|---------|
| `.gitignore` | Exclusion rules (with tracked-file inconsistencies) |
| `.claude/settings.local.json` | Claude Code permission allowlist |

---

## Architecture Verification

### GIE Signal Path (Correct)

The ISR (`gdma_eof_isr` + `isr_loop_boundary`) implements the free-running loop correctly:

1. **Clock-domain drain:** 200 volatile loops (~5us) before PCNT read. Necessary because PCNT samples GPIO through the IO matrix, and PARLIO's last byte needs time to propagate. Verified by the PCNT_DRAIN_FIX errata.

2. **Base detection:** Auto-detects the dummy/neuron boundary by scanning for consecutive zero-delta captures. Handles GDMA circular chain offset transparently.

3. **Dot decoding:** Cumulative differencing (`dots[n] = agree[base+n+1] - agree[base+n]`) correctly extracts per-neuron dot products from the running PCNT counters.

4. **TriX classification:** Validates group uniformity (all 8 neurons in a group should have identical dots when driven by identical signature weights), then publishes via reflex_channel_t. The argmax is permutation-invariant, so GDMA chain offset doesn't affect classification.

5. **CfC blend:** Ternary gate with threshold. `f = tsign(dot)` when threshold=0; `f = (|dot| > threshold) ? tsign(dot) : 0` when threshold > 0. Three blend modes: UPDATE, HOLD, INVERT.

6. **Re-encode:** Only the hidden portion (indices 128..159 of the 160-trit neuron buffer) is re-encoded after each loop. Input portion stays static until `gie_input_pending` is set.

7. **PARLIO re-arm:** Stop (clear tx_start) -> clear PCNT x3 -> clear phantom EOFs -> reprogram tx_bytelen -> set tx_start -> force PCR clock enable. The triple PCNT clear and the phantom EOF clear are both necessary due to hardware errata.

8. **DMA race prevention:** Input re-encode happens only when `gie_input_pending=1`, and only during the loop boundary when PARLIO has stopped (tx_bytelen exhausted). Main loop waits 2 full loops after clear: loop N+1 flushes PARLIO FIFO, loop N+2 guarantees all descriptors reflect new data.

### LP Core Assembly (Correct)

The RISC-V assembly in `main.S` is clean:

- **Register discipline:** Callee-saved registers (s0-s11, ra) properly saved/restored across three different frame sizes. No register spills to global memory.
- **Stack frames:** CfC = 96 bytes, VDB search = 608 bytes, VDB insert = 224 bytes. Peak is 608B (VDB search), verified against 4,356B free LP SRAM.
- **CfC->VDB bridge:** Copies 6 words (packed x_pos/x_neg) from CfC stack to VDB query BSS, restores CfC regs, deallocates CfC frame, then falls through to VDB entry which allocates its own frame. Clean separation.
- **POPCOUNT macro:** Byte-wise LUT, fully inlined. 4 loads + 3 adds per 32-bit word.
- **INTERSECT macro:** Fully unrolled for CfC (hot path). Looped version (INTERSECT_LOOP) for VDB.
- **VDB graph search:** Two-list algorithm (candidate + result), dual entry points (node 0 and N/2), 64-bit visited bitset (VIS_LO + VIS_HI), early termination, insertion sort on result list.
- **VDB insert:** Brute-force candidates -> top-M selection -> forward edges -> reverse edges with weakest-eviction. No diversity heuristic (correct for small N, low-dimensional ternary).
- **Feedback blend:** Unpacks LP-hidden portion (trits 32-47) from best VDB match, applies 4-case blend rule (agreement/gap-fill/conflict/silent), counts modifications.
- **No MUL/DIV:** Confirmed. The M extension is declared in the ISA but never exercised.

### VDB HP-Side API (Correct)

`reflex_vdb.c` correctly:
- Packs trits into LP SRAM at the right node offset
- Dispatches commands via `ulp_lp_command`
- Polls for completion using counter comparison (not flag comparison, avoiding ABA)
- Reads results from LP SRAM volatile pointers
- Uses the `vdb_ulp_addr()` opacity trick to prevent GCC array-bounds warnings on ULP symbol access

### Simulation (Correct)

`sim/test14c.c` implements three conditions (14A no bias, 14C full bias, 14C-iso Phase 2 bias only) with:
- Three independent claims tested separately
- Firmware-exact constants
- 1000 trials with per-trial random LP weights
- Pass criterion structurally derived (lp_delta > 0 at step 30)
- Wall-time reporting per condition
- Interpretive notes for each outcome combination

---

## Issues Found

### Issue 1: `geometry_cfc_freerun.c` is a 5069-Line Monolith

**Severity:** High (blocks paper submission per ROADMAP)
**Location:** `embedded/main/geometry_cfc_freerun.c`

The file contains: register defines, GPIO mapping, constants, CfC typedefs, static BSS allocations (ISR state, neuron buffers), ternary operations, encoding functions, CfC init/premultiply/encode, ISR loop boundary handler, GDMA EOF ISR, GPIO init, PARLIO init, PCNT init, timer init, GDMA channel detection, GDMA ISR registration, LP core weight packing, LP core start, LP SRAM feed, TriX signature management, TriX classification channel consumer, prime_pipeline, start/stop_freerun, helper print functions, and 13 test functions + app_main.

The ROADMAP states: "Separate core layer (stable GIE, VDB, LP, CMD dispatch) from test layer (condition flags, parameters, logging) before Phase 5 code lands. Reviewers must be able to find the difference between TEST 14A and 14B in under 10 lines."

**Remediation:** Split into `gie_engine.c` + `gie_engine.h` (core) and `reflex_tests.c` (test harness + app_main). The engine exposes the GIE state, init/start/stop, gate threshold, TriX enable, LP interface, and channel access. The test file contains only test logic and condition parameters.

### Issue 2: Root Directory Cluttered with Tracked Log/Bin Files

**Severity:** Medium (obscures project structure)
**Location:** Repository root

25+ files at root level: `reflex_*.log`, `reflex_*.bin`, `reflex_*.txt`, `esp32_monitor_output.txt`, `reflex_etm_with_gdma.txt`. These were committed before `.gitignore` rules were added. The `.gitignore` prevents new ones from being staged but doesn't affect already-tracked files.

Additional non-essential root files: `WHITEPAPER.md`, `PAP_PAPER.md`, `TECHNICAL_REALITY.md`, `AUDIT_THE-REFLEX.md` (66KB, untracked), `silicon_grail_deploy.sh`.

**Remediation:** Create `archive/logs/` and `archive/captures/`. Move tracked log/bin/txt files there via `git mv`. Move early-stage documents (`WHITEPAPER.md`, `PAP_PAPER.md`, `TECHNICAL_REALITY.md`) to `docs/archive/`. Move previous audit to `docs/archive/`.

### Issue 3: ESP-NOW Static Globals in Header

**Severity:** Low (works but fragile)
**Location:** `embedded/include/reflex_espnow.h`, lines 63-67 and 81

Ring buffer state (`_espnow_ring[64]`, `_espnow_ring_head/tail/drops`) and receiver state (`_espnow_state`) are declared `static volatile` in the header. All functions (receive callback, init, drain, flush, get_latest) are `static inline`. This creates silent duplicate storage if the header is ever included by more than one translation unit.

**Remediation:** Create `reflex_espnow.c` with the state and non-inline functions. Header retains only typedefs, constants, and function declarations.

### Issue 4: `ulp/main.c` Is a Partial, Divergent Reference

**Severity:** Low (misleading to readers)
**Location:** `embedded/main/ulp/main.c` (276 lines)

Implements only CMD 1 (CfC step). The assembly (`main.S`, 1931 lines) implements CMD 1-5 including VDB search, insert, and feedback blend. No test verifies the C reference matches the assembly. A reader encountering `main.c` first would miss 80% of the LP core's functionality and could draw incorrect conclusions about the architecture.

**Remediation:** Move to `embedded/main/ulp/archive/main_c_reference.c` with a header comment explaining it is a historical C reference for CMD 1 only, superseded by `main.S`.

### Issue 5: Untracked Binaries and Missing .gitignore Rules

**Severity:** Low (hygiene)
**Locations:**
- `sim/test14c` — compiled binary, untracked
- `docs/SUBSTACK_DRAFT_V1.md` — untracked draft
- `AUDIT_THE-REFLEX.md` — untracked previous audit (66KB)

**Remediation:** Add `sim/test14c` to `.gitignore`. Stage and commit the draft and audit (or archive them).

### Issue 6: Triple PCNT Clear Undocumented

**Severity:** Low (cargo-cult risk)
**Location:** `embedded/main/geometry_cfc_freerun.c`, lines 662-667

```c
pcnt_unit_clear_count(pcnt_agree);
pcnt_unit_clear_count(pcnt_disagree);
pcnt_unit_clear_count(pcnt_agree);
pcnt_unit_clear_count(pcnt_disagree);
pcnt_unit_clear_count(pcnt_agree);
pcnt_unit_clear_count(pcnt_disagree);
```

Six calls (three per unit) with no comment explaining why a single clear is insufficient. The surrounding code documents everything else meticulously. This pattern suggests a clock-domain or register-latching issue that was solved empirically but not explained.

**Remediation:** Add a comment block documenting the reason (likely: PCNT clear is asynchronous to the PCNT sampling clock, and multiple clears ensure the clear propagates through the pipeline before PARLIO restarts).

---

## Structural Observations

### Geological Layers

70+ `.c` files in `embedded/main/` trace the full development arc from M1 (Sub-CPU ALU, `alu_fabric.c`) through M13 (VDB causal necessity). Only two files are in the active build (`geometry_cfc_freerun.c`, `reflex_vdb.c`). The rest is archaeological record. The CMakeLists.txt correctly compiles only the active files.

Similarly, 54 header files in `embedded/include/`, of which only 4 are actively used: `reflex.h`, `reflex_vdb.h`, `reflex_espnow.h`, and (indirectly via ULP) `ulp_main.h`.

### Commit History

193 commits, single branch (`main`), single author. Linear history, descriptive messages following a consistent convention (`feat:`, `docs:`, `refactor:`, `test:`, `fix:`). No merge conflicts, no force pushes. Clean provenance.

### Documentation Discipline

22 documents in `docs/`, plus session records and journal entries. The README accurately describes the system. The CURRENT_STATUS tracks exactly what is verified. The ROADMAP has a clear dependency graph and honest blocking prerequisites. Three strata of contribution are well-articulated with distinct target venues.

### Build System

ESP-IDF v5.4, CMake-based. ULP binary embedded via `ulp_embed_binary()`. The build switches between GIE firmware and ESP-NOW sender by commenting/uncommenting a single `set(app_sources ...)` line. Not elegant, but functional. The ROADMAP's `-DREFLEX_TARGET=sender` approach (mentioned in CMakeLists.txt header comments) is not yet implemented.

---

## Remediation Plan

### Phase 1: Trivial Fixes (Issues 5, 6)

1. Add `sim/test14c` to `.gitignore`
2. Document the triple PCNT clear in `geometry_cfc_freerun.c`

### Phase 2: Archive & Clean (Issues 2, 4)

3. Create `archive/logs/` and `archive/captures/` directories
4. `git mv` all tracked root-level log/bin/txt files to `archive/`
5. `git mv` early-stage docs to `docs/archive/`
6. Move `AUDIT_THE-REFLEX.md` to `docs/archive/`
7. Archive `ulp/main.c` to `embedded/main/ulp/archive/`

### Phase 3: ESP-NOW Refactor (Issue 3)

8. Create `embedded/main/reflex_espnow.c` with state and functions
9. Reduce `reflex_espnow.h` to declarations only
10. Update `CMakeLists.txt` to compile `reflex_espnow.c`

### Phase 4: The Split (Issue 1)

11. Create `embedded/include/gie_engine.h` — public interface
12. Create `embedded/main/gie_engine.c` — core GIE engine, ISR, init, LP interface
13. Reduce `geometry_cfc_freerun.c` to test harness + app_main
14. Update `CMakeLists.txt`
15. Verify build compiles clean

---

*This audit reflects the repository state at commit `8161a30` (March 26, 2026) plus untracked files observed on April 6, 2026.*
