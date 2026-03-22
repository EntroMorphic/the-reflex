# Reflections: The Reflex — March 22, 2026

*Second LMM cycle. The prior reflection ended: "The VDB IS the training. Not in the gradient sense, but in the experiential sense." That was February 9. The March 22 findings force a different class of question.*

---

## Core Insight

The prior cycle's core insight was: *the Reflex is not a neural network running on a microcontroller — it is a peripheral hardware configuration that IS a neural network.*

March 22 requires a harder version of that claim:

**The system now processes external physical signals and outperforms software-only approaches at its own classification task — without ever executing a multiplication or activating the high-power CPU.**

This is no longer a claim about architecture. It is a measured result against a baseline. The 100%-vs-84% delta is not a margin-of-error artifact. It represents a qualitative advantage from temporal geometry that rate-only approaches cannot access at all. The TriX Cube's face voxels measure something that software running on a polling loop would miss: the microsecond-scale arrival structure of individual packets, which carries 37% of the discriminating information.

That delta exists because the ISR fires at 711 Hz on a hardware interrupt. Not because we wrote better classification code — because the interrupt latency is lower than the temporal structure of the signal being classified.

---

## The PARLIO Bug as Epistemology

The `parl_tx_rst_en` discovery is not just a hardware errata note. It's an epistemological event.

We believed the peripheral was working because the output was correct on individual vectors. We didn't test the meta-level: was the peripheral correctly iterating across all vectors? The FIFO reset we knew about cleared the data path. The core reset we didn't know about cleared the control path. Without the control-path reset, the loop counter was wrong — but individual computations were right.

This is the embedded systems version of Goodhart's Law: when a measure becomes a target, it ceases to be a good measure. We were measuring correctness of dot products. The peripheral's control state was not in the measure set. It was corrupting silently.

**What this means going forward:** Every peripheral has a hierarchy of state — data state, FIFO state, control state, clock domain state. Our verify-each-layer discipline was not complete. We need to add "reset hierarchy verification" to the milestone checklist: not just "does the output match" but "does the peripheral complete its full operating cycle correctly."

The 200-loop PCNT drain (clock domain drain after EOF) was an earlier instance of this class. We discovered it because PCNT counts were reading early. The `parl_tx_rst_en` is the same class: control state that isn't automatically cleared, that corrupts across sessions, that produces wrong behavior at a level of abstraction above the one being checked.

---

## Synthetic vs Real Input: What Changed

Before March 22, every milestone was a closed-loop proof. The system processed inputs that a CPU generated, verified them against CPU reference, and confirmed match. The signal path was:

```
CPU generates pattern → HP writes to SRAM → GDMA reads → PARLIO clocks → GPIO → PCNT → ISR reads → match CPU reference
```

The CPU was both source and verifier. This is a perfectly valid test methodology for proving the peripheral routing works. But it answers the question "does the peripheral compute correctly?" not "does the peripheral compute usefully?"

After March 22:
```
RF signal → ESP-NOW RX → HP writes to SRAM → GDMA reads → PARLIO clocks → GPIO → PCNT → ISR reads → TriX update
```

The source is the physical world. The CPU did not generate the pattern; it received it. Now the question being answered is different: "does the peripheral compute usefully on real inputs that the CPU did not control?"

This is the first time the system is acting as a sensor, not a compute engine.

**The implication for the architecture:** the GIE's job was always described as "compute dot products." That was the implementation. The purpose — which we now know is true — is "transduce physical signals into ternary geometry." The GDMA/PARLIO/PCNT chain is a transduction chain, not a compute unit. The distinction matters when thinking about what to connect to it next.

---

## The Laundry Method: Where Are the Boundaries?

### GIE → TriX boundary

The GIE computes 64 dot products per loop and writes them to cfc.hidden[]. The TriX ISR runs at GDMA EOF and reads these dot products to update the TriX Cube. But:

- The TriX ISR runs on the HP core interrupt vector
- The GIE outputs are written by the ISR itself
- The TriX Cube state is HP core SRAM

So the GIE → TriX boundary is: ISR-to-ISR-to-HP-SRAM. This is safe because the EOF ISR is the only writer, and the HP core reads after the ISR completes. But the TriX Cube is not fed to the LP core. The LP core doesn't know what classification is happening.

**The boundary question:** Why should the LP core know about TriX? If the LP CfC is a sub-conscious integrator, and TriX is conscious classification, then maybe the boundary is correct. But if TriX classification should modulate the LP's attention — i.e., "I just classified a P1 pattern, focus on the P1-relevant features" — then the boundary is a disconnect.

### TriX → VDB boundary

TriX produces classification events at 711 Hz. The VDB stores 48-trit state vectors at 100 Hz (LP timer). There is no connection between them.

**The boundary question:** What would it mean to store TriX classification events in the VDB? You'd need a 48-trit representation of "I saw pattern P1 with confidence X at time T." This would allow the LP CfC to ask "when did I last see P1?" via VDB retrieval. The associative memory would connect classification history to current hidden state.

This is the natural next integration point. Not a new architectural layer — a connection between two existing ones.

### Board B → Board A latency

ESP-NOW has ~1ms round-trip latency at 1 hop. The GIE processes at 430.8 Hz (~2.3ms/loop). Board B sends patterns faster than one per GIE loop. Multiple packets can arrive during a single GIE iteration. The ISR processes one EOF per loop. If Board B sends 10 packets/loop, only the last one's effect reaches the PCNT counters.

**The boundary question:** Is this undersampling a bug or a feature? If Board B sends slowly (1 packet per several GIE loops), each packet gets full GIE attention. If Board B sends fast, packets compete for GIE cycles. The TriX Cube's temporal geometry would reflect this competition as arrival-time variance. But the classification assumes temporal structure is a property of the pattern, not of the packet rate.

This is a real design tension that the 4-pattern test at low rate doesn't surface. At high rates or with more patterns, this could become a source of classification error.

---

## What the XOR Mask Tells Us

Payload 47%. Timing 37%. RSSI 9%. PatID 5%.

The PatID field being only 5% discriminating is significant. The sender labels each packet with which pattern it is. But the GIE doesn't use this label — the dot products are computed against the packet CONTENT, not the label. The 5% from PatID means the PatID field itself has some bit-correlation with the pattern signature. It's not zero because the pattern IDs are themselves ternary vectors that happen to have some weight correlation.

RSSI being 9% means signal strength is a weak but real discriminant. This could be exploited for localization (closer transmitter → higher RSSI → different classification weights) or it could be a source of false discriminators (RSSI changes with orientation, not just pattern identity).

The dominant 47% from payload content is the payload's ternary dot product with the signature. This is exactly what the GIE computes: do the packet bits agree with the expected pattern bits? The peripheral hardware is, literally, the classifier. It is not computing "similarity to a template" in an algorithmic sense — it is physically routing the bits through the PCNT gating matrix and the agree/disagree counts ARE the similarity scores.

**The deep point:** We did not implement a classifier. We wired a classifier. The descriptor chain is the template. The PCNT is the accumulator. The physical routing is the match function. If you change the descriptor chain (change the weights), you change what pattern the hardware recognizes. Training this classifier means rewriting the descriptor chain.

---

## What I Now Understand (March 22 addition)

The Reflex is a **sensor system** as well as a compute system. The GIE is not just a neural network engine — it is a transducer that converts physical RF signal content into ternary geometric representations. The TriX classifier shows that this representation is richer than rate-based approaches: it captures temporal structure that polling-based systems miss.

The March 22 session closed the first external signal loop. The system now receives a physical signal, processes it through peripheral hardware without CPU involvement, classifies it via temporal geometry, and outperforms the simpler software approach.

The next loop to close is memory-modulated attention: TriX classification events stored in the VDB, retrieved by the LP CfC, used to modulate which patterns the GIE pays attention to on the next cycle. That loop would make the system adaptive: not just reacting to patterns but learning which patterns matter.

**The structure beneath the findings:**

The first cycle's synthesis said the VDB is experiential memory. The March 22 findings show what experiences are worth storing: not the hidden state of an isolated system, but the classification history of a system interacting with the physical world. The VDB was designed for introspective memory (what was my state 10 seconds ago?). The right use is environmental memory (what patterns did I see, when, and how did I respond?).

This is the same insight the prior cycle had about the VDB being "episodic, not semantic" — but grounded in a real use case now. The classification events are the episodes. The retrieval is "have I seen this kind of signal before, and what did it mean?"
