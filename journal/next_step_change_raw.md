# Raw Thoughts: The Next Step-Change for The Reflex

## Stream of Consciousness

I've just read every tracked file in this repo, every data summary, the full firmware, the three papers, the DO_THIS_NEXT, the ROADMAP, 21+ LMM cycles of journal entries, and I red-teamed my own test verdict changes. I have the whole picture. What follows is what I actually think, not what sounds strategic.

The system works. 100% label-free classification. VDB temporal context at 8.5-9.7/80 MTFP. The structural wall holds. The CLS parallel is real. These are honest, silicon-verified results.

The two mechanisms designed to IMPROVE on the VDB baseline both failed. Kinetic attention is harmful (-5.5/80 MTFP). Hebbian is noise (+0.1 ± 1.1). The kinetic_reliability LMM cycle already diagnosed why: the 16-neuron LP with random ternary weights and sign() quantization is at its ceiling. Both mechanisms re-roll which trits are confident without directing the re-roll toward better discrimination. The bottleneck is LP representational capacity, not mechanism design.

The ROADMAP lists MTFP-resolution VDB integration as "THE NEXT STEP-CHANGE." The DO_THIS_NEXT says the same. The kinetic_reliability LMM cycle says the same. Everyone agrees. But I want to push on this before accepting it.

What does MTFP VDB integration actually buy? Right now the VDB stores 48-trit snapshots (32 GIE + 16 LP sign). Under MTFP, it would store 112-trit snapshots (32 GIE + 80 LP MTFP). The VDB search would use 112-trit Hamming instead of 48-trit. The feedback blend would inject 80 trits instead of 16 into the LP state.

But here's the thing I keep circling: the LP CfC recurrence still runs on 16 sign trits. The MTFP encoding reads the raw dot products (lp_dots_f[16]) and produces 80 trits, but the CfC's next step still uses lp_hidden[16] = sign(lp_dots_f). The MTFP is a READ-SIDE improvement — the VDB stores richer snapshots and retrieves better matches. The WRITE-SIDE (the CfC dynamics that produce the next state) is unchanged.

Is that enough? The VDB-only baseline is already 9.7/80 MTFP. That's the system reading its own dot products at higher resolution. The question is whether storing and retrieving at 80-trit resolution changes the TRAJECTORY of the LP state over 120 seconds. The feedback blend currently injects sign(retrieved_node[LP portion]) back into lp_hidden. With MTFP, what does the blend look like? You can't inject 80 MTFP trits into a 16-trit hidden state. You'd have to either:

(a) Blend in the LP-sign portion of the retrieved node (same as now, but retrieval is better because 112-trit search is more discriminative than 48-trit)
(b) Blend in some decoded version of the MTFP trits — but decoded into what? The LP hidden state IS 16 sign trits. You can't inject magnitude information into a sign representation.

I think the actual value of MTFP VDB is option (a): the search gets better because the distance metric is richer. The retrieved node is more likely to be a genuinely similar past state. The blend is still 16-trit sign injection, but the RIGHT 16-trit sign injection (from a past state that actually resembled the current state in magnitude space, not just sign space).

This is a real improvement but it's not a step-change in LP representational capacity. It's a step-change in VDB retrieval quality. The LP is still 16 neurons producing 16 sign trits.

Now, what WOULD be a step-change in LP capacity? Two options I see:

Option A: MTFP VDB (the consensus choice). Improves retrieval, doesn't change LP dynamics. Expected: better VDB match quality → more targeted feedback → maybe higher LP divergence baseline. But the LP ceiling is still 16 sign trits.

Option B: Wider LP (32 neurons). Changes the LP dynamics directly. The CfC has 32 outputs instead of 16. The sign space doubles from 2^16 to 2^32. The probability that 4 random projections land on distinct sign vectors increases dramatically. The MTFP encoding would give 160 trits instead of 80. The VDB would store 32+32=64 trit sign snapshots or 32+160=192 trit MTFP snapshots.

But Option B is invasive: rewrite the LP RISC-V assembly (main.S, 1931 lines of hand-written code), change every VDB dimension constant, resize the NSW graph, update every test. The kinetic_reliability_raw.md already noted this: "This is a significant change."

Option C: Something I haven't seen anyone discuss. What if the step-change isn't about the LP at all? What if it's about SHIPPING what works?

The system has: 100% label-free classification at 430 Hz, ~30 µA (pending UART verification), VDB episodic memory that's causally necessary for temporal context, a structural wall that's been verified across every experiment. Three papers drafted. The papers are honest about the negative results.

The step-change might be: SUBMIT THE PAPERS. Stop trying to improve the LP and let the community see what a peripheral-hardware ternary neural system looks like. The VDB baseline (9.7/80 MTFP) is already the finding. The negative results (kinetic attention harmful, Hebbian noise) are already the findings. The structural wall is already the finding. The CLS parallel is already the finding.

Every session since April 6 has been: build mechanism → test → find it doesn't improve on VDB baseline → diagnose why → identify LP ceiling → propose wider LP or MTFP VDB. The cycle repeats. The kinetic_reliability LMM cycle, the hebbian_diagnosis LMM cycle, the self_organizing LMM cycle — they all converge on the same conclusion: the 16-neuron LP is at its ceiling. But the system is already doing something real and novel WITHOUT the LP mechanisms.

The question is: does the publication need MTFP VDB results to be compelling? Or is the current system — peripheral-hardware ternary neural computation with episodic temporal context at 30 µA — already a paper?

I think it's already a paper. Three papers, actually, and they're drafted. They need UART verification and they need to be submitted.

But I also think the MTFP VDB work is the correct engineering next step AFTER submission. It's not blocking submission. It's the v2.

## Questions Arising

- Is there a version of MTFP VDB that doesn't require rewriting the LP assembly? (Store MTFP in VDB, search with MTFP, but blend using the sign portion only — the assembly doesn't change, only the HP-side VDB API)
- What's the actual UART verification path? Is there a hardware reason it hasn't been done? Tripp has the boards and the serial bridges.
- Are the three papers in a submittable state TODAY? The April 12 rewrites were substantial — do they have all the numbers from the authoritative dataset?
- What if the "step-change" framing is wrong? What if the project is in a SHIPPING phase, not a BUILDING phase?

## First Instincts

- The next step-change is not technical. It's operational: UART verify, then submit.
- MTFP VDB is the next technical step-change, but it's post-submission.
- The project has been in a building loop since April 6 (kinetic attention → Hebbian → diagnosed Hebbian → MTFP measurement → kinetic harmful). Six days of mechanism work that confirmed the VDB baseline is the mechanism. The VDB baseline was established on March 22 (TEST 12/13). The mechanism was already working 21 days ago.
- The risk of continuing to build before submitting is that the papers keep accumulating corrections and rewrites instead of reaching an audience.
- The risk of submitting now is UART verification — if the ~30 µA claim doesn't hold without JTAG, the paper has a problem. But the papers already disclose this limitation.

## What Scares Me

- That the UART verification reveals the GIE doesn't work without USB-JTAG. The March 19 session identified a "Silicon Interlock" where the JTAG controller might gate PCNT behavior. If that's real, the system only works when connected to a development tool, and the "peripheral-autonomous" framing collapses. The ROADMAP calls this "low-risk to attempt" but it's never been attempted.
- That I'm wrong about submission being the step-change. Maybe the community needs MTFP VDB results to take the system seriously. Maybe 9.7/80 MTFP from VDB alone isn't impressive enough without a demonstrated improvement mechanism. Maybe the paper needs the v2 results.
- That the three papers are still citing stale numbers somewhere I haven't checked.
