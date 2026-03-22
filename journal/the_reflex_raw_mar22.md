# Raw Thoughts: The Reflex — March 22, 2026

*Second LMM cycle. Prior synthesis ended February 9 with "The tree has been chopped" — VDB feedback closed. March 22 is a different thing entirely. Starting fresh.*

---

## Stream of Consciousness

The prior cycle was all internal. Synthetic test patterns, CPU-generated inputs, the system talking to itself. Every milestone through M5/CMD5 was the system proving it could do something — but we were the ones feeding it. Board B changed that. Today was the first time something outside the chip sent real data and the reflex responded.

That feels like a bigger deal than 11/11. The test count is a score. The source of the input is a category change.

But let me back up. The PARLIO TX state machine bug. We had a silent failure mode where the TX core's internal state was corrupted between free-running sessions. Not the FIFO — the core itself. `parl_tx_rst_en` is PCR bit 19. `FIFO_RST` is bit 30. These are different things and both exist. We only knew about FIFO_RST. The hardware had a second state machine we didn't know existed, and it was silently accumulating corruption across test invocations. The symptom was loop counts that didn't grow — TEST 2 was only seeing 37 EOFs per second instead of the expected ~800. The corruption was invisible to the software.

What scares me about this: how long did it go undetected? The tests passed because we were checking agreement between CPU reference and PCNT counts on individual vectors. We weren't checking that the peripheral was actually looping correctly. The verification missed a whole class of failure.

And then Board B: the PEER_MAC was wrong. `0xC4, 0xD4` instead of `0xC8, 0x24`. Two bytes. The board was sending 1060 packets to a nonexistent destination. The symptom was `ok=0 fail=1060` — clean, unambiguous, total failure. But we had to know what the symptom meant. The MAC address is a hardware fact that has to be discovered at boot time and transcribed by hand. There's no automation here. The failure mode is: someone changed Board A (hardware failure, replacement), the MAC is now different, and Board B silently fails to connect. Zero debugging tools to diagnose this remotely.

The classification results: 100% vs 84%. I keep trying to figure out what the right frame for this is. The 84% baseline is rate-only — just counting packet arrival rate and matching to the closest known rate. The 100% is TriX: ternary dot products against 4 pattern signatures, then measuring temporal displacement of the TriX Cube's face voxels from the core. What's happening is that two patterns might arrive at the same rate but have different timing structures, and the TriX Cube catches that. Payload content (47%), timing (37%), RSSI (9%), PatID (5%).

The payload content is the most discriminating signal. That surprises me. I would have guessed timing or RSSI. But 47% of the classification weight comes from whether the bits in the packet agree or disagree with the learned signature. The peripheral hardware is reading the actual content of the wireless signal, not just detecting its presence.

That changes what this system IS. It's not a timing detector. It's not a rate counter. It's a content-addressable pattern recognizer running at 711 Hz with no CPU involvement after setup. The classification happens in the ISR, which runs on hardware interrupt, which fires when the GDMA EOF hits, which fires when the PARLIO finishes clocking the descriptor. The CPU learns the result by reading a shared variable when it wakes up.

The peripherals-only compute explanation I had to write forced me to find the right analogy. I landed on: GDMA is a conveyor belt, PARLIO is a flasher, GPIO is a wire, PCNT is a tally machine. The "computation" is the arrangement of the belt's items in relation to the tally machine's gating wires. Change the arrangement and you change what gets counted. The weights ARE the arrangement. This is why I keep saying it's closer to an FPGA than to software — you configure the hardware topology and then let data flow through it.

But FPGAs reconfigure between tasks. The GIE doesn't. It's fixed at setup. The descriptor chain is the network. Once you understand that, "peripherals-only compute" stops being surprising and starts being obvious: of course the peripheral can compute, it's computing by virtue of being wired correctly.

The thing that gnaws at me now: what's next? We've proven the system can classify external signals at 100% accuracy in a controlled 4-pattern environment. We've proven the LP core can run a CfC that adapts based on memory. We've proven the VDB feedback loop is stable. But these things don't talk to each other yet. The TriX classifier is a separate system from the GIE/CfC/VDB stack. Board B feeds the GIE through ESP-NOW, but the GIE output doesn't feed the TriX, and the TriX output doesn't feed the VDB.

The whole thing is still modular proofs, not an integrated loop. When does the reflex become a reflex arc where the output actually influences the input channel? When does the system's memory modulate what it pays attention to next?

## Questions Arising

- The PARLIO TX state machine has hidden state. What other peripheral state are we not resetting?
- The MAC address problem is a reliability problem at scale. What's the right abstraction?
- 100% on 4 patterns is not 100% in the real world. How does the system degrade as pattern count grows?
- The TriX Cube face divergence measures temporal displacement. What does "temporal displacement" actually mean in terms of what Board B sent?
- XOR mask: payload 47%. Does this mean we can distinguish patterns that have the same rate but different content? Yes. But can we distinguish patterns with the same content but different ordering?
- When does the LP core's CfC receive input from the TriX classification? Right now it doesn't.
- The system is 11/11. What would test 12 look like?

## First Instincts

- The PARLIO bug is a symptom of a deeper question: peripheral hardware has its own state machines that aren't in the datasheet
- The system now touches the world. Everything before was internal validation. This is the first external signal.
- The XOR mask decomposition is the first quantitative measurement of what the system is actually using to discriminate
- The modular proof phase is over. The question is now integration.
- "Peripherals-only compute" is not a curiosity — it's an architecture primitive. The question is what you build on top of it.
- The next step-change is when the output of classification modulates the input of attention
