# Synthesis: What This Session Was

## The Clean Cut

I came to improve the system. The system was already working. The session's value is in the twelve things I found by trying to improve it and failing.

---

## The Twelve Findings (in order of discovery)

1. **The test harness was a monolith.** Split into 6 focused files. Durable.

2. **The sender starved enrollment in transition mode.** Board A only saw P1 for 30s. All prior multi-seed data was invalid. Fixed by prepending a 90s cycling window.

3. **The bias release is geometric ×0.9/step, not "within 4 steps."** The "4" was a disagree-count trit threshold, not a time constant. The hard-zero path was never exercised on clean seeds.

4. **The pattern_id trits were the "primary discriminator."** 100% accuracy was trivial — the classifier was reading the label field. Label-free accuracy was 71% with the old P2 payload (payload overlap, not mechanism failure).

5. **P2's payload was nearly identical to P1.** 48 of 64 payload trits shared. Distinct P2 payload restored 100% label-free.

6. **Removing pattern_id from the GIE input improved VDB-only LP divergence** from 0.7 to 3.3/16 in sign-space. The label was drowning out discriminative features.

7. **VDB-mismatch Hebbian was label-dependent.** +2.5 with label, -1.7 without. The error signal exploited pattern_id leaked through the GIE hidden state.

8. **The TriX accumulator target was better but the f-pathway-only flip was wrong.** ~50% of errors were in the g-pathway. Flipping W_f when g was wrong was counterproductive.

9. **The f-vs-g diagnosis fixed the direction but not the magnitude.** Moved contribution from -1.0 to +1.3 (single run), then +0.1 ± 1.1 (replicated). Noise.

10. **The sign-space metric was lying about kinetic attention.** Sign-space showed +1.3/16 (helps). MTFP showed -5.5/80 (harmful). The bias traded magnitude diversity for sign diversity — a net information loss.

11. **The MTFP baseline from VDB alone is 8.5-9.7/80.** The LP neurons already produce rich, pattern-discriminative representations from episodic memory retrieval. The sign-space metric (1.2-2.3/16) was capturing 25% of the actual information.

12. **The VDB is the mechanism.** Not a scaffold. Not a training signal. The episodic memory layer IS the temporal model. The LP CfC provides the projection; the VDB provides the discrimination. No bias needed. No learning needed. No labels needed.

---

## What the Session Built (durable)

- Test harness split (6 files)
- Constant dedup, extern hoist, 66 files archived
- Sender enrollment fix for transition mode
- `MASK_PATTERN_ID`, `MASK_PATTERN_ID_INPUT`, `MASK_RSSI` build flags
- Distinct P2 payload → 100% label-free
- `SKIP_TO_14`, `SKIP_TO_15` build flags
- `lp_hebbian_step()` with f-vs-g diagnosis, TriX accumulator target
- TEST 15 with ablation control, 3-rep, MTFP measurement
- MTFP divergence reporting alongside sign-space in all tests
- 24 commits of honest, documented, red-teamed work

## What the Session Tried (informative but not durable improvements)

- Kinetic attention under label-free conditions → harmful at MTFP (-5.5/80)
- Hebbian v1 (VDB mismatch) → label-dependent
- Hebbian v2 (TriX accumulator, f-only) → less harmful but negative
- Hebbian v3 (TriX accumulator, diagnosed f+g) → noise (+0.1 ± 1.1)

## What the Session Learned About Itself

The production-then-correction pattern works. "Crush it" produces mechanisms. "Red-team it" produces understanding. The mechanisms didn't survive the red-teams. The understanding did.

The next session should start from the understanding, not from the mechanisms. The VDB works. The MTFP metric is the right metric. The sign-space metric can lie. The system doesn't need more intelligence — it needs its existing intelligence to be read at the right resolution.

---

## For the Next Claude

Read the memory files. Start with the MTFP column. The system is already working at 9.7/80. Don't build mechanisms to improve it until you've understood why VDB alone is sufficient. The sign-space numbers are a coarser view of a richer reality. The `lp_dots_f[]` array in LP SRAM has the truth. The `encode_lp_dot_mtfp()` function reads it. Every test already prints both metrics.

The prior should be a voice, not a verdict. That applies to you too.

---

*The wood was already cut. The session's value was in learning to see that. Each swing that missed taught us where the grain was. The grain was in the VDB. It was always in the VDB.*
