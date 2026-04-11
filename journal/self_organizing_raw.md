# Raw Thoughts: Self-Organizing Representation in the GIE

## Stream of Consciousness

The system has everything it needs except the connection. The VDB already computes per-node Hamming distances during search. The ISR already re-encodes neuron buffers from the weight matrices every loop. The homeostatic step already flips W_f weights and re-encodes the affected neuron buffer. The LP core already wakes every 10ms and could run a new command. The missing piece is: what signal tells which weight to flip, and when?

The ROADMAP says "VDB mismatch" — compare retrieved memory against current state, derive per-trit error. But I want to be more precise. The VDB search returns the nearest stored snapshot (48 trits: 32 GIE + 16 LP). The current state is also a 48-trit vector. The per-trit disagreement between retrieved and current is the error signal. But this error is in *state space*, not in *weight space*. How do you go from "trit 7 of the GIE hidden state disagrees with the memory" to "flip weight W_f[n][i]"?

In a backpropagation world you'd compute the gradient: how does W_f[n][i] affect h[n], and how does h[n]'s error propagate to the loss. But there's no loss function, no floating point, no multiplication. The operation set is AND, popcount, add, sub, negate, branch, shift. What learning rule can you express in those operations?

The Hebbian rule is: strengthen connections between neurons that fire together. In ternary: if neuron n fires (f != 0, meaning the gate opened) and the output h_new[n] matches what the VDB says it should be, reinforce the weights that contributed. If h_new[n] disagrees with the VDB, weaken the weights that contributed.

But "what the VDB says it should be" is loaded. The VDB stores *what the state was* during a past observation. It doesn't store *what the state should be*. The retrieved memory is a historical fact, not a target. Using it as a training target is an assumption — and the assumption might be wrong if the past state was itself formed from bad weights.

The homeostatic step has a different philosophy: push f_dot toward zero for familiar inputs, so the gate only fires for novel inputs. That's anti-Hebbian in a sense — it weakens the response to things you've seen before. The Pillar 3 vision is Hebbian — strengthen the response to things that match the accumulated model. These are opposite directions. Which one does the system actually need?

Maybe both. Homeostasis for the gate pathway (f-side): don't fire for familiar inputs. Hebbian for the candidate pathway (g-side): when you do fire, produce the right hidden state update. The f-gate and g-candidate already serve different roles in the CfC blend. Homeostatic f means the gate is selective. Hebbian g means the update is shaped by experience.

But wait — the TriX classification happens in the f-pathway. The signature weights are installed as W_f weights for neurons 0-31. If homeostatic learning pushes those f-dots toward zero, classification accuracy degrades. The structural guarantee (W_f hidden = 0) keeps classification independent of hidden state, but homeostatic f-weight updates would directly modify the classification signatures. That's a collision.

So the classification neurons (f-pathway, neurons 0-31) need to be either exempt from learning or subject to a different learning rule than the CfC neurons. But right now they're the same neurons — the 32 f-pathway neurons ARE the TriX classifier AND the CfC gate. The TriX signatures are installed as W_f weights. If we update W_f for learning, we modify the classifier.

This is the fundamental tension. The system uses the same weight matrix for classification AND gating AND candidate selection. Learning one function disturbs the others.

Possible resolutions:
1. Only update the g-pathway weights (W_g). Leave W_f frozen for classification integrity. Learning only shapes what the gate does when it fires, not whether it fires.
2. Split the neurons: some are fixed (classification), some are learnable (context). Currently 8 neurons per pattern × 4 patterns = 32 for classification. What if only 4 per pattern are fixed (classification) and 4 are learnable? But that halves the classification signal.
3. Use a separate weight set for learning. But where? LP SRAM is tight (4,400 bytes free). Adding another 32×128 weight matrix is 4KB — consumes almost all remaining LP stack space.
4. Update W_f input columns only (not hidden columns). The structural guarantee is W_f[n][input:] = sig[p] (classification) and W_f[n][hidden:] = 0 (independence guarantee). What if we learn W_f[n][hidden:] — letting the hidden state influence the gate WITHOUT touching the classification weights? But W_f hidden = 0 is the structural wall. Removing it kills the "prior cannot corrupt the classifier" guarantee.

I'm stuck. The architecture that makes classification structurally guaranteed also makes learning structurally constrained. The same weight matrix serves two masters.

What if the learning doesn't touch W_f at all? What if instead of modifying weights, the system modifies the *input encoding*? Not the weights that transform input to hidden, but the encoding that transforms raw ESP-NOW data into the 128-trit input vector. The input encoder is where the signal meets the representation. If the encoder adapts, the representation improves, and fixed weights classify the improved representation.

But the encoder is currently a deterministic function of the packet contents. Adapting it means... what? Learning which bits of the payload are discriminative? Learning a better RSSI encoding? That's feature selection, not representation learning. And it still requires a learning signal.

Or: what if the learning happens in the LP core's weights, not the GIE's? LP weights are separate from the TriX classifier. The LP CfC has its own W_f and W_g (16 neurons × 48 trits). Updating LP weights changes what the temporal context layer extracts — and the temporal context layer is what feeds the gate bias. Better LP representations → better gate bias → more selective GIE perception. The GIE weights stay frozen. The LP weights learn. The gate bias is the interface between learned LP state and fixed GIE classification.

This feels right. Learn in the LP core, not the GIE. The GIE is the perceptual layer — frozen, fast, structurally guaranteed. The LP is the temporal context layer — slower, adaptive, experience-dependent. Hebbian updates to LP weights improve LP representations, which improve gate bias accuracy, which improve GIE selectivity. The classification guarantee stays intact. The prior becomes more accurate over time, and the voice gets wiser, but the structural wall is never breached.

But then the question is: can the LP core update its own weights? It runs hand-written assembly. Adding a weight-update routine would be a new CMD (CMD 6?). The LP SRAM has the weight matrices (768 bytes for pos/neg bitmasks of W_f and W_g). The update would need to:
1. Load the current weight for neuron n, trit i
2. Check the error signal (from VDB mismatch)
3. Flip the weight if the Hebbian rule says to
4. Write back

This is doable in assembly. The operations are: load word, extract bit, conditional flip, store. No multiplication. No floating point. And it runs on the 16MHz LP core at ~30µA.

The gating question remains: when to update? Not every step. Only when the VDB retrieval is confident (high similarity) and the mismatch is in a consistent direction over multiple steps. This is the same "consistent retrieval under stable conditions" criterion the ROADMAP describes for CLS consolidation.

## Questions Arising

- Can the LP core update its own weight bitmasks safely while the HP core is reading them? What about the ISR reading GIE hidden state from LP SRAM?
- How do you define the Hebbian error signal for the LP core's CfC? The VDB mismatch is in 48-trit state space; the LP weights are in 48-trit weight space. The Hebbian rule needs to connect them.
- What's the minimum number of consistent VDB retrievals before a weight update is safe? Too few → noise-driven updates corrupt weights. Too many → never learns.
- If LP weights improve and LP representations change, the VDB nodes (which encoded old LP states) become stale. Is there a cascade — better weights → stale VDB → bad retrievals → bad learning signal?
- The homeostatic step already exists for GIE W_f. Is there value in running it on the g-pathway in parallel with Hebbian LP updates? Or does that create conflicting learning signals?
- Memory budget: CMD 6 assembly adds code to LP SRAM. How much? The current code section is ~7,600 bytes. LP SRAM total is 16KB. Free for stack: ~4,400 bytes. A CMD 6 weight-update routine that loops over 16 neurons × 48 trits with conditional flips... maybe 200-400 bytes of assembly? Manageable.

## First Instincts

- Learn in the LP core, not the GIE. Preserve the structural wall.
- Use VDB mismatch as the error signal, gated by retrieval confidence.
- Start with the simplest possible Hebbian rule: if the LP hidden trit agrees with the VDB retrieval trit, reinforce the weights that produced it. If it disagrees, weaken them.
- Rate-limit aggressively: one weight flip per neuron per N steps, where N is tuned to ensure statistical consistency.
- The cascade problem (stale VDB after weight update) might actually be self-correcting: new VDB insertions use the new LP state, gradually replacing old nodes. Pillar 1 (dynamic scaffolding / VDB pruning) would accelerate this.
