# Experimental Data Corpus

Raw silicon capture logs for The Reflex. These are the evidence base for the
three papers in `papers/`.

**These files are version-controlled deliberately.** Until September 2, 2026 a
global `*.log` rule in `.gitignore` left every one of them untracked while the
papers cited them by name — the corpus existed on one machine only. The ignore
rule is now narrowed to build logs (audit finding D1,
[`docs/AUDIT_SEP2026.md`](../docs/AUDIT_SEP2026.md)).

Board: ESP32-C6FH4 (QFN32) rev v0.2. ESP-IDF v5.4. Every log includes its
firmware build timestamp in the boot banner (`compile time ...`).

---

## Datasets

| Directory | Date | Status | Use |
|---|---|---|---|
| `apr11_2026/` | Apr 11–12, 2026 | **Authoritative** | All paper numbers. See `apr11_2026/SUMMARY.md`. |
| `apr9_2026/` | Apr 9, 2026 | Supporting | Multi-seed TEST 14C (3 seeds × 3 conditions), pre-label-free. See `apr9_2026/SUMMARY.md`. |
| `apr8_2026/` | Apr 8, 2026 | **Deprecated — do not cite** | Two compounding bugs. See `apr8_2026/DEPRECATED.md`. |
| `full_suite_remediation{,_v2,_v3}.log` | Apr 8, 2026 | Provenance unresolved | See below. |

## Root-level logs — status unresolved

`full_suite_remediation.log`, `_v2`, `_v3` were captured April 8, 2026
(build timestamps `Apr 8 2026 13:55:45` and later that day). They sit at the
`data/` root rather than in a dated directory.

They are **not** automatically covered by `apr8_2026/DEPRECATED.md`, which
scopes itself to `results_*.log` in that directory, and whose second bug
(sender enrollment starvation) affects `TRANSITION_MODE` runs specifically —
these are full-suite runs, not transition-mode seeds. The first bug
(`trix_enabled` not set in Tests 12–13, fixed April 8 in `f97ac1c`) may or may
not predate these captures depending on the exact build.

**They are therefore not cited by any paper, and should not be** until someone
who ran them confirms which side of `f97ac1c` each one falls on. They are kept
for the research record. Resolving this is a small task for whoever has the
bench notes.

## Conventions

- One directory per capture session, named `mon<D>_<YYYY>`.
- A `SUMMARY.md` in each directory is the authoritative digest for that session;
  papers cite the summary, and the summary cites the logs.
- A dataset that is superseded gets a `DEPRECATED.md` in its directory stating
  what is invalid, what survives, and what replaces it. Files are kept, never
  deleted — "what did the data look like before the fix" is a review question.
