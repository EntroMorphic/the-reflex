# Claims vs. Capability — what the code does, what the papers say

**September 3, 2026.** Compiled from the audit, the red-team pass, and the
September 2–3 silicon campaign. Every entry traces to a measurement or a read of
the source, cited inline.

The point of this document is to separate four different things that get lumped
together as "problems with the paper":

- **Claims that hold**, including one the papers *understate*.
- **Capability deltas** — the code does something narrower than the words imply.
- **Evidence deltas** — the code may well do it; the measurements don't show it.
- **Unverified** — never measured either way.

---

## A. Claims that hold

**No floating point in the mechanism path.** Verified by inspection: zero
`float`/`double` in `gie_engine.c`, `reflex_vdb.c`, the headers, or `ulp/main.S`.
Floats appear only in test files printing percentages. The invariant comment at
the top of `gie_engine.c` is accurate.

**The structural wall, `W_f[hidden] = 0`.** Verified on every write path. The
signature-install and online re-sign paths write only the input portion;
`lp_hebbian_step()` touches LP weights exclusively; the one routine that could
write `W_f` at an arbitrary index (`cfc_homeostatic_step`) is never called, and
would not select a zero weight if it were.

**This is the one claim the papers understate.** They present the wall as holding
"across all experiments" — an empirical statement. It is stronger than that: it
holds *by construction*, verifiable by reading the writes. That is a
qualitatively better kind of guarantee and the papers should say so.

**Label-free classification works.** 31–32/32 windowed across 15 runs, unchanged
across every configuration tested — including with the sequence counter masked,
which collapses everything downstream. The classifier is the most robust part of
the system.

**ISR dot decoding is exact.** In every calibration record the ISR's per-group
scores equal the CPU's brute-force dots element for element, negatives included.

---

## B. Capability deltas — the code does less than the words imply

### B1. "The CPU never computes a dot product" — **was true only of the sum** *(now closed)*

The peripheral fabric computed the *sum*; the CPU computed every *product* —
2048 `tmul()` calls per loop, ~880k ternary multiplies/second, ~285 µs of a
2326 µs loop. The claim was literally true and materially misleading.

**Closed September 2–3.** Both operands are now streamed on 4-bit PARLIO and PCNT
performs the multiply in hardware. Verified `Dot errors: 0 / 64` in the
free-running engine (`docs/FABRIC_MULTIPLY.md`). The claim is now true of the
products as well — behind `FABRIC_MUL`, default off pending a full-suite run.

### B2. "Navigable Small World graph" / "approximate nearest-neighbor retrieval"

The navigability property that "NSW" denotes is **deliberately traded away at
construction**. Neighbour selection is plain top-M by score; the diversity
heuristic that makes such graphs navigable was rejected because at 48 trits it
pruned too hard (documented in the Phase 2 comment, and an honest response to a
real problem). The search then compensates with `EF_SEARCH=32`, `CAND_MAX=64` on
a 64-node graph, visiting **60/64 nodes in every run in the corpus**.

What runs is a densely connected neighbour list, searched almost exhaustively.
It still pays 8 B/node of edge metadata, a 608-byte search frame, and up to 63
dot products per insert on a 16 MHz core. Recall@1 is 19/20 = 95%; a plain
linear scan would be exact, in less memory. *(R10, R12)*

### B3. "Accumulates a temporal model ... over 120 seconds of live operation"

There is **no eviction anywhere**. Every insert site gates on
`vdb_count() < 64`. TEST 12 reaches `vdb=64`; TEST 15 saturates ~15 s into
Phase B, so **Phase C — which produces the reported divergence — measures
entirely against a frozen store**. Retention is first-come-first-served: the
surviving 64 episodes are simply the earliest 64.

Accurate wording: accumulates for roughly the first 60–75 s, static thereafter.
Stratum 2 does note the VDB "fills in ~3 minutes", but frames it as a *future
scaling* concern; it fills inside the measurement window. ROADMAP Pillar 1 exists
to fix this and is unbuilt. *(R11)*

### B4. Component 5 — the evidence-deference policy

The immediate-deference branch (`n_disagree >= 4` → bias = 0) is **never entered
on any clean seed**. What runs is a geometric decay — a timer, not deference.
The bias also never reaches neuron group 3, which does not fire the gate in any
condition of any run, so the mechanism was only ever exercised on 3 of 4 groups.

Four of the five components in Stratum 3 are verified. The fifth — the one the
"prior as voice" argument depends on — is **unverified, not falsified**.

### B5. Loop rate

430 Hz is the **CfC-blend-active** rate, cleanly measured. Classification runs in
a different configuration, now measured at **490 Hz** (97% of the 503 Hz PARLIO
ceiling implied by 9936 bytes at 2-bit/20 MHz). A previously reported 664 Hz was
**physically impossible** — 132% of that ceiling — and came from a diagnostic
dividing counters that spanned different windows.

---

## C. Evidence deltas — the measurements don't support the claim

### C1. "8.5–9.7/80 MTFP divergence from VDB feedback alone"

Three findings, each independently sufficient to require a rewrite:

- **"Alone" is unsupportable.** A field carrying *zero* pattern information (a
  per-packet random block replacing the sequence counter) reproduces **68%** of
  the MTFP effect. The metric is driven by input variation, largely regardless of
  whether that variation carries pattern content.
- **The null floor is 4.3/80, not the ~1 asserted in `README.md`** — measured by
  shuffling the metric's bins so all four means are drawn from one distribution.
- **The margin over that floor is undecided.** n=5 within-session: 6.16 ± 1.96 vs
  4.00 ± 1.29, p = 0.079. Required n for 80% power is 10 per group.

### C2. "The episodic memory is causally necessary"

Three independent sessions under corrected (ground-truth) binning, sender reset
between each:

| Session | CMD 5 | CMD 4 | Contribution |
|---|---|---|---|
| 1 | 1 | 2 | −1 |
| 2 | 0 | 4 | −4 |
| 3 | 1 | 0 | +1 |

**−1.33 ± 2.52, t(2) = −0.92. 1 of 3 sessions positive → NOT SUPPORTED** against
the pre-registered criterion. The between-session SD of 2.52 dwarfs the
published effect of +1.

*This does not show the VDB does nothing.* TEST 13's original observation about
CfC projection degeneracy may still describe something real. What is unsupported
is attributing a positive, reproducible contribution to the blend.

### C3. Every reported `±` measures the wrong variance

All published error bars are **within-session** SDs — reps inside one boot. The
between-session term is several times larger: the same configuration gave sign
3.57 ± 0.40 and 0.58 ± 0.43 across two sessions, a discrepancy ~7× the
within-session SD. The bars are not merely underpowered; they quantify rep noise
and call it measurement uncertainty. *(R9)*

### C4. Classification accuracy

"100% label-free" is a 32-window **majority vote** where the trivial packet-rate
baseline already scores 87%. Per admitted packet the same builds give
**90.5–96.4%**, with P3 recall as low as 52–63% and a heavily imbalanced sample
(P0 ≈ 300 vs P3 ≈ 46). *Corrected in the papers Sept 2.*

Note this was a **regression, not a discovery**: `SESSION_APR06_07_2026.md`
already recorded that the 100% was an ensemble result and per-packet was ~96%.
The finding didn't need discovering; it needed to survive the trip into the paper.

---

## D. Unverified — never measured either way

**~30 µA.** A datasheet figure, asserted in three abstracts, open since March 19.
All runs use USB-JTAG for power and serial. Requires physical rewiring to
GPIO 16/17, battery or dumb-USB power, and a current meter.

**Generality.** One board pair, four patterns, one sender, one RF environment.
More reps tighten intervals; they do not establish generality.

---

## What is *not* a legitimate delta

Worth stating, because the list above is long and the system is better than it
reads:

- The peripheral-fabric ternary dot product is real, exact, and now performs the
  multiply in hardware as well.
- The classifier genuinely works, label-free, and is unmoved by everything that
  breaks downstream of it.
- The LP core genuinely runs a 16-neuron ternary CfC plus vector search in
  hand-written assembly at 100 Hz.
- The structural wall is real and better than claimed.

The failures are concentrated in one place: **the claims about what the temporal
memory layer demonstrates.** The perception layer holds up throughout.
