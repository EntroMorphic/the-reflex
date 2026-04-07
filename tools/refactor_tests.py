#!/usr/bin/env python3
"""
Mechanical refactor: extract test functions from geometry_cfc_freerun.c

Reads the original file and produces a refactored version where:
- Each TEST N block becomes a static int run_test_N(void) function
- LP CHAR becomes static void run_lp_char(void)
- LP Dot Diagnostic becomes static void run_lp_dot_diag(void)
- app_main becomes a clean orchestrator
- Shared state promoted to file scope

Strategy: tests 1-10 are at indent level 1 (4 spaces) inside app_main.
We keep their body verbatim (same indentation) inside the new function.
Tests 12-14 are at indent level 2 (8 spaces) inside test 11.
We dedent them by 4 spaces so they're at level 1 inside the new function.
"""

import re
import sys

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'embedded/main/geometry_cfc_freerun.c'
    dst = sys.argv[2] if len(sys.argv) > 2 else src

    with open(src, 'r') as f:
        lines = f.readlines()

    # ── Find key landmarks ──
    app_main_start = next(i for i, l in enumerate(lines) if 'void app_main(void)' in l)
    test_count_line = next(i for i in range(app_main_start, len(lines))
                          if 'int test_count = 0, pass_count = 0;' in lines[i])

    # Summary block (the one after TEST 14, before LP diagnostics)
    summary_start = next(i for i in range(3000, len(lines))
                        if '    printf("========' in lines[i] and 'RESULTS' in lines[i+1])

    # LP CHAR: back up from "LP CHARACTERIZATION" to the /* ══ line
    lp_char_text = next(i for i, l in enumerate(lines) if 'LP CHARACTERIZATION' in l)
    lp_char_start = lp_char_text
    while lp_char_start > 0 and '══════' not in lines[lp_char_start]:
        lp_char_start -= 1

    # LP DOT MAGNITUDE
    lp_dot_text = next(i for i, l in enumerate(lines) if 'LP DOT MAGNITUDE DIAGNOSTIC' in l)
    lp_dot_start = lp_dot_text
    while lp_dot_start > 0 and '══════' not in lines[lp_dot_start]:
        lp_dot_start -= 1

    final_while = next(i for i in range(len(lines)-1, -1, -1)
                      if 'while (1) vTaskDelay' in lines[i])

    # ── Find test printf lines ──
    test_printfs = {}
    for i, l in enumerate(lines):
        m = re.search(r'printf\("-- TEST (\d+):', l)
        if m:
            test_printfs[int(m.group(1))] = i

    # Back up from each printf to find the comment block start (/* ══)
    test_starts = {}
    for tnum, printf_line in test_printfs.items():
        j = printf_line - 1
        # Walk backward past the closing ══ */ line
        # Find the OPENING /* ══ line (contains both /* and ══)
        while j > 0:
            line_s = lines[j].strip()
            if line_s.startswith('/*') and '══' in line_s:
                break
            if line_s.startswith('/*') and f'TEST {tnum}' in lines[j+1]:
                break
            # Stop if we hit a blank line or closing brace (went too far)
            if line_s in ('', '}') and j < printf_line - 8:
                j = printf_line  # fallback
                break
            j -= 1
        test_starts[tnum] = j

    print(f"app_main: {app_main_start+1}, test_count: {test_count_line+1}")
    print(f"summary: {summary_start+1}, lp_char: {lp_char_start+1}, lp_dot: {lp_dot_start+1}")
    for t in sorted(test_starts):
        print(f"  TEST {t}: starts line {test_starts[t]+1}, printf line {test_printfs[t]+1}")

    # ── Determine test end lines ──
    # Tests 1-10: each ends before the next test starts
    # Test 11: ends before LP CHAR (encompasses tests 12-14 in original)
    # But we split test 11 at the point where test 12's shared vars start

    # Find "Shared results: TEST 12" line
    t12_shared = next(i for i, l in enumerate(lines) if 'Shared results: TEST 12' in l)
    # Test 11's own code ends at the } just above the shared results comment
    t11_code_end = t12_shared - 1
    while lines[t11_code_end].strip() == '':
        t11_code_end -= 1
    t11_code_end += 1  # exclusive

    test_ends = {}
    main_level_tests = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    for idx, tnum in enumerate(main_level_tests):
        if idx + 1 < len(main_level_tests):
            test_ends[tnum] = test_starts[main_level_tests[idx + 1]]
        else:
            # Test 10 ends before Test 11
            test_ends[tnum] = test_starts[11]

    # Test 11's function body: from test 11 start to t11_code_end
    test_ends[11] = t11_code_end

    # Tests 12-14: within the old test 11 block
    test_ends[12] = test_starts[13]
    test_ends[13] = test_starts[14]
    # Test 14 ends at the closing braces of the test 11 outer block
    # Find "    }" followed by blank line before LP CHAR
    t14_end = lp_char_start - 1
    while lines[t14_end].strip() == '':
        t14_end -= 1
    t14_end += 1  # exclusive
    test_ends[14] = t14_end

    for t in sorted(test_ends):
        print(f"  TEST {t}: lines {test_starts[t]+1}-{test_ends[t]}")

    # ── Helper: dedent by N spaces ──
    def dedent(line, n=4):
        if line[:n] == ' ' * n:
            return line[n:]
        return line

    # ── Helper: transform test body ──
    def transform_test_body(start, end, indent_remove=0, ok_var='ok'):
        """Extract lines [start:end], remove test_count/pass_count lines,
        remove promoted defines/declarations, apply dedent if needed."""
        result = []
        for i in range(start, end):
            line = lines[i]
            stripped = line.strip()

            # Remove promoted shared state declarations
            if any(pat in stripped for pat in [
                '#define NOVELTY_THRESHOLD',
                '#define T11_DRAIN_MS',
                'static espnow_rx_entry_t drain_buf[32]',
                'int t12_p1p3_result',
                'int t12_p1p2_result',
                'int t12_n1 = 0',
                'static int8_t t12_mean1',
                'static int8_t t12_mean2',
                'Shared results: TEST 12',
                'P1 vs P2 is the primary ablation pair',
            ]):
                continue

            # Remove test_count++; and pass_count += lines
            if stripped == 'test_count++;':
                continue
            if 'test_count++' in stripped and 'pass_count' not in stripped:
                # test_count++; on its own line
                if stripped.endswith('test_count++;'):
                    continue
            if stripped.startswith('if (ok') and 'pass_count' in stripped:
                continue
            if stripped.startswith('if (ok1') and 'pass_count' in stripped:
                continue

            # (skip-path return 0 handled in post-processing)

            if indent_remove > 0:
                line = dedent(line, indent_remove)

            result.append(line)

        # Strip trailing blank lines only (keep closing braces — they're real scope)
        while result and result[-1].strip() == '':
            result.pop()

        return result

    # ── Build output ──
    out = []

    # File header
    out.append('/*\n')
    out.append(' * geometry_cfc_freerun.c — Test Harness for the GIE Engine\n')
    out.append(' *\n')
    out.append(' * This file contains the test suite (TEST 1-14) and app_main().\n')
    out.append(' * Each test is a static function returning 1 (pass) or 0 (fail).\n')
    out.append(' * The GIE engine core is in gie_engine.c with its interface in\n')
    out.append(' * gie_engine.h.\n')
    out.append(' *\n')
    out.append(' * Separated from engine code: April 6, 2026 (audit remediation).\n')
    out.append(' * Tests extracted to functions: April 7, 2026 (LMM-guided refactor).\n')
    out.append(' *\n')
    out.append(' * Board: ESP32-C6FH4 (QFN32) rev v0.2\n')
    out.append(' * ESP-IDF: v5.4\n')
    out.append(' */\n')
    out.append('\n')

    # Includes (original lines 14-24, 0-indexed 13-23)
    for i in range(13, 25):
        out.append(lines[i])

    # LP binary extern (original lines 27-28)
    out.append('\n')
    out.append('/* ── LP Core binary (embedded by build system) ── */\n')
    for i in range(27, 29):
        out.append(lines[i])

    # Engine constants
    out.append('\n')
    out.append('/* ── Engine-internal constants needed by test harness ── */\n')
    out.append('#define NUM_DUMMIES     5\n')
    out.append('#define SEP_SIZE        64\n')
    out.append('#define CAPTURES_PER_LOOP  (NUM_DUMMIES + NUM_NEURONS)\n')
    out.append('#define NUM_TEMPLATES   4\n')
    out.append('\n')
    out.append('/* ── Shared constants (used across Tests 11-14) ── */\n')
    out.append('#define NOVELTY_THRESHOLD  60\n')
    out.append('#define T11_DRAIN_MS       10\n')
    out.append('\n')
    out.append('/* ── File-scope shared state ── */\n')
    out.append('static int8_t sig[NUM_TEMPLATES][CFC_INPUT_DIM];\n')
    out.append('static espnow_rx_entry_t drain_buf[32];\n')
    out.append('\n')
    out.append('/* TEST 12 → TEST 13 handoff */\n')
    out.append('static int8_t  t12_mean1[LP_HIDDEN_DIM];\n')
    out.append('static int8_t  t12_mean2[LP_HIDDEN_DIM];\n')
    out.append('static int     t12_p1p3_result = -1;\n')
    out.append('static int     t12_p1p2_result = -1;\n')
    out.append('static int     t12_n1 = 0, t12_n2 = 0;\n')
    out.append('\n')

    # Forward declarations
    out.append('/* ── Forward declarations ── */\n')
    for n in range(1, 15):
        out.append(f'static int run_test_{n}(void);\n')
    out.append('static void run_lp_char(void);\n')
    out.append('static void run_lp_dot_diag(void);\n')
    out.append('\n')

    # TOC
    out.append('/*\n')
    out.append(' * ══════════════════════════════════════════════════════════════════\n')
    out.append(' *  TEST SUITE — Table of Contents\n')
    out.append(' *\n')
    toc = [
        ('1', 'Free-running loop count (GIE basic)'),
        ('2', 'Hidden state evolves autonomously'),
        ('3', 'Per-neuron dot accuracy vs CPU reference'),
        ('4', 'LP Core geometric processor'),
        ('5', 'Ternary VDB — NSW graph (M=7), 64 nodes'),
        ('6', 'CfC → VDB pipeline (CMD 4)'),
        ('7', 'Reflex channel coordination'),
        ('8', 'VDB → CfC feedback loop (CMD 5)'),
        ('9', 'ESP-NOW receive from Board B'),
        ('10', 'ESP-NOW → GIE live input'),
        ('11', 'Pattern classification (stream CfC + TriX)'),
        ('12', 'Memory-modulated adaptive attention'),
        ('13', 'CMD 4 ablation (VDB causal necessity)'),
        ('14', 'Kinetic attention (agreement-weighted gate bias)'),
    ]
    for num, desc in toc:
        out.append(f' *  run_test_{num}(){" " * (8 - len(num))}— {desc}\n')
    out.append(' *  run_lp_char()      — LP dynamics characterization (diagnostic)\n')
    out.append(' *  run_lp_dot_diag()  — LP dot magnitude probe (diagnostic)\n')
    out.append(' *\n')
    out.append(' *  Each run_test_N() returns 1 (pass) or 0 (fail/skip).\n')
    out.append(' *  Tests 12-14 require Test 11 to have run first (installs TriX signatures).\n')
    out.append(' * ══════════════════════════════════════════════════════════════════\n')
    out.append(' */\n')
    out.append('\n')

    # ── app_main ──
    out.append('/* ══════════════════════════════════════════════════════════════════\n')
    out.append(' *  MAIN — ORCHESTRATOR\n')
    out.append(' * ══════════════════════════════════════════════════════════════════ */\n')
    out.append('\n')
    out.append('void app_main(void) {\n')

    # Init block: original lines from app_main+1 through test_count_line-1
    for i in range(app_main_start + 1, test_count_line):
        out.append(lines[i])

    out.append('    /* ── Run all tests ── */\n')
    out.append('    int test_count = 0, pass_count = 0;\n')
    out.append('\n')
    for n in range(1, 15):
        out.append(f'    test_count++; pass_count += run_test_{n}();\n')
    out.append('\n')
    out.append('    /* ── Summary ── */\n')

    # Copy original summary lines (they're already at correct indent)
    for i in range(summary_start, lp_char_start):
        out.append(lines[i])

    out.append('\n')
    out.append('    /* ── Diagnostics (not counted in pass/fail) ── */\n')
    out.append('    run_lp_char();\n')
    out.append('    run_lp_dot_diag();\n')
    out.append('\n')
    out.append('    while (1) vTaskDelay(pdMS_TO_TICKS(10000));\n')
    out.append('}\n')

    # ── Emit test functions ──

    # Tests 1-11: bodies at indent level 1, keep as-is
    for tnum in range(1, 12):
        ok_var = 'ok'
        body = transform_test_body(test_starts[tnum], test_ends[tnum],
                                   indent_remove=0, ok_var=ok_var)
        out.append('\n')
        out.append(f'static int run_test_{tnum}(void) {{\n')
        for line in body:
            out.append(line)
        out.append('\n')
        out.append(f'    return {ok_var};\n')
        out.append('}\n')

    # Tests 12-14: bodies at indent level 2, dedent by 4
    for tnum, ok_var in [(12, 'ok12'), (13, 'ok13'), (14, 'ok14')]:
        body = transform_test_body(test_starts[tnum], test_ends[tnum],
                                   indent_remove=4, ok_var=ok_var)
        out.append('\n')
        out.append(f'static int run_test_{tnum}(void) {{\n')
        for line in body:
            out.append(line)
        out.append('\n')
        out.append(f'    return {ok_var};\n')
        out.append('}\n')

    # ── LP CHAR ──
    out.append('\n')
    out.append('static void run_lp_char(void) {\n')
    for i in range(lp_char_start, summary_start):
        out.append(lines[i])
    # Strip trailing blank lines
    while out and out[-1].strip() == '':
        out.pop()
    out.append('\n')
    out.append('}\n')

    # ── LP DOT DIAGNOSTIC ──
    out.append('\n')
    out.append('static void run_lp_dot_diag(void) {\n')
    for i in range(lp_dot_start, final_while):
        out.append(lines[i])
    while out and out[-1].strip() in ('}', ''):
        out.pop()
    out.append('\n')
    out.append('}\n')

    # ── Post-processing: insert return 0; after SKIPPED fflush in test functions ──
    # Pattern: printf("  SKIPPED..."); fflush(stdout); → need return 0; after fflush
    processed = []
    i = 0
    while i < len(out):
        processed.append(out[i])
        # Check if this line is fflush after a SKIPPED printf
        if 'fflush(stdout);' in out[i]:
            # Look back for SKIPPED
            for back in range(len(processed)-2, max(len(processed)-5, -1), -1):
                if 'SKIPPED' in processed[back]:
                    # Insert return 0 at same indent as fflush
                    indent = len(out[i]) - len(out[i].lstrip())
                    processed.append(' ' * indent + 'return 0;\n')
                    break
        i += 1
    out = processed

    with open(dst, 'w') as f:
        f.writelines(out)

    print(f"\nWrote {len(out)} lines to {dst}")

if __name__ == '__main__':
    main()
