# Nodes: The Problem

## Node 1: The Speed Mismatch
Physics: microseconds. Software: milliseconds. Gap: 1000x.
**Why it matters:** The problem is quantifiable.

## Node 2: The Latency Stack
Each layer adds delay. They compound. 3-10ms per hop minimum.
**Components:** Application, ROS2 node, DDS serialization, network, DDS deserialization, callback.

## Node 3: The Scenario Gaps
| Need | Have | Gap |
| <1ms | 3-10ms | 3-10x |
**Why it matters:** Concrete examples of where it fails.

## Node 4: The Dirty Secret
Serious teams bypass ROS2 for control. Two parallel systems. Unsustainable.
**Why it matters:** The industry already knows this is broken.

## Node 5: Why Not RTOS?
Fast but dumb. No learning. No ecosystem. Expensive.
**Tension:** RTOS vendors will push back.

## Node 6: The Architecture Problem
Not hardware. Not implementation. ARCHITECTURE. Designed for distribution, not reaction.
**Why it matters:** Can't optimize your way out. Need different primitive.

## Node 7: The Cost of Slow
- Safety incidents
- Limited capabilities
- Competitive disadvantage
- Two-system maintenance burden
**Why it matters:** Problem has dollar value.

## Node 8: Getting Worse
More capable robots = higher speeds = tighter timing requirements.
**Why it matters:** Problem compounds over time.

## Node 9: The Disbelief Barrier
"Our robots work fine." Until they don't.
**Tension:** Some won't believe until they experience failure.

## Node 10: The Industry Awareness
Is this problem widely recognized? Or is it our job to educate?
**Implication:** Market creation vs market capture.
