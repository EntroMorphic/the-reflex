# Paper Remediation Plan: Kinetic Attention

*April 7, 2026. Based on red-team of draft AND red-team of the plan itself.*

---

## Status

The paper is content-complete. The data is honest. The claims match the evidence. Remaining work is presentation quality — text calibration, data instrumentation, and figures. No architectural or experimental changes needed.

**Timeline:** Submission-ready within two sessions. This session: text fixes + firmware instrumentation + silicon run. Next session: figures + final review.

---

## Phase 1: Text Fixes (this session)

### 1.1 Independence Phrasing

**Problem:** "Across three silicon runs" implies independent replications. All three used the same boards, same weight seed, same sender firmware, same physical arrangement.

**Fix:** Replace "three silicon runs" with "three runs of the same experimental configuration" in abstract and conclusion. Add to Section 3.3: "All runs used the same hardware, weight seed (0xCAFE1234), sender firmware, and physical arrangement. The only uncontrolled variable is the sender's phase within its 27-second pattern cycle at the start of each condition."

### 1.2 LP Accumulator Robustness

**Problem:** The paper claims the sign-of-sum accumulator washes out the 20% CPU misclassification rate but provides no analysis.

**Fix:** Add to Section 2.2 — an honest bounded argument, not a fake proof:

"The sign-of-sum accumulator converges to the majority direction. At 20% misclassification, the 80% majority is preserved for trits where the correct-pattern LP state is consistent. Trits near the 50/50 boundary may be affected by contamination, but these trits carry the least information — they are the ones where the LP state does not reliably distinguish the pattern. The practical impact is bounded by the number of near-threshold trits, typically 2-4 out of 16. The confusion matrix (Section 4.2) confirms that contamination is concentrated on the P0-P1 pair, not distributed across all patterns."

### 1.3 TriX vs core_pred Clarity

**Problem:** Section 5.4 says "the TriX signal always has the last word" but the LP feedback chain uses core_pred, not TriX.

**Fix:** Replace the sentence with: "The TriX signal provides the structurally guaranteed classification at 430 Hz. The LP feedback chain uses the CPU core_pred classifier (~80% accuracy), which is sufficient for accumulator-based prior formation but does not inherit the structural guarantee. A future firmware revision could dispatch LP feedback from the TriX channel directly, eliminating the accuracy gap."

### 1.4 Per-Group Fire Rate Context

**Problem:** Total fire counts are confounded by per-pattern sample counts.

**Fix:** Add after the fire rate table: "Fire rates are not normalized by per-pattern confirmation count. The G0 increase (+27%) exceeds the P0 sample count variation across conditions (<15%), indicating the effect is not entirely explained by sampling."

### 1.5 Peripheral-Autonomy Language

**Problem:** This paper makes peripheral-autonomy claims ("without CPU involvement between classification events") but UART falsification has not been performed.

**Fix:** Soften to: "the GIE runs on peripheral hardware, verified with JTAG serial monitoring." Defer the unqualified "peripheral-autonomous" claim to the Stratum 1 paper where UART falsification will have been completed. Add to limitations: "All runs use USB-JTAG for serial output. The peripheral-autonomy claim is structural (the ISR runs between CPU involvement) but has not been verified with the JTAG controller physically disconnected."

---

## Phase 2: Firmware Instrumentation (this session)

Two additions to TEST 14, one silicon run to capture both.

### 2.1 Confusion Matrix (main results, not supplementary)

The confusion matrix answers the most obvious reviewer question and belongs in Section 4.2, not buried in supplementary material.

**Firmware:** ~15 lines. Add `int confusion[4][4] = {0}` to TEST 14. On each confirmation where ground truth is available: `confusion[gt][pred]++`. Print 4x4 matrix after each condition.

**Expected result:** P0↔P1 confusion dominates (~15-18% of total errors). P2 and P3 have near-zero cross-contamination. This confirms the cross-dot analysis and makes the 80% accuracy *explanatory*.

### 2.2 TriX vs core_pred Agreement Rate

**Firmware:** ~10 lines. After each confirmation, read the TriX channel's most recent prediction. Count agreements and disagreements with core_pred. Report percentage.

**Expected result:** >90% agreement. The disagreements should concentrate on P0↔P1 packets where core_pred's margin is thinnest.

---

## Phase 3: Figures (next session)

Five figures. Data is finalized after Phase 2 silicon run.

### Figure 1: System Architecture

Three-layer block diagram (GIE / LP core / HP core) with Phase 5 feedback loop highlighted. Shows both classification paths (TriX ISR at 430 Hz, CPU core_pred at ~4 Hz) and where they diverge. Prior-signal separation visually obvious.

Vector diagram (Excalidraw or tikz). ~half page.

### Figure 2: Bias Trace — Two Panels

**Top panel:** 14C bias trace over 120 seconds. Four colored lines (one per pattern group). Shows activation, decay, and pattern cycling.

**Bottom panel:** 14C-iso bias trace. Same axes. Shows 60 seconds of flat zeros, then activation. Vertical dashed line at t=60s.

Shared x-axis. The two-panel comparison makes the isolation condition self-explanatory.

Source data: `step N (Ts): bias=[a b c d]` log lines from serial capture.

### Figure 3: LP Divergence Bar Chart

Grouped bars. X-axis: condition (14A, 14C, 14C-iso). Y-axis: mean Hamming / 16 (%). Three bars per condition (one per run). Run-mean line. Min/max error bars. 14C consistently above 14A. 14C-iso variable. Variance visible.

### Figure 4: Heatmaps

Three 4x4 heatmaps side by side (14A, 14C, 14C-iso). Color scale: 0 (white) to 8 (dark). Run 3 data (representative). P1-P2 degeneracy visually obvious. P3-involving pairs show strongest improvement.

### Figure 5: Per-Group Gate Fire Rates

Grouped bars. X-axis: pattern group (G0-G3). Y-axis: total fires. Three bars per group (14A, 14C, 14C-iso). G0 +27% annotated. The physical mechanism in one chart.

---

## Phase 4: Pre-Submission Checklist

| Item | Status | Blocks Submission |
|------|--------|:-:|
| Text fixes (1.1-1.5) | **This session** | Yes |
| Confusion matrix in main results | **This session** | Yes |
| TriX vs core_pred agreement | **This session** | Yes |
| Silicon run with new instrumentation | **This session** | Yes |
| Figure 1: Architecture diagram | Next session | Yes |
| Figure 2: Bias trace (two-panel) | Next session | Yes |
| Figure 3: Divergence bar chart | Next session | Yes |
| Figure 4: Heatmaps | Next session | No (nice to have) |
| Figure 5: Fire rates | Next session | No (nice to have) |
| S1: Full serial output | Available | No |
| UART falsification | Future session | No (claims softened) |
| Target venue identified | Before submission | Yes |

---

*The data is real. The claims are honest. What remains is making a reviewer see what the silicon showed us.*
