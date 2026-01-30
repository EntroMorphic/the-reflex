# Raw Thoughts: Team Requirements

## Stream of Consciousness

Most robotics teams don't have real-time experience. This is a gap we need to address.

The irreplaceable role: DOMAIN EXPERT. This person knows:
- What "normal" looks like for their robot
- What anomalies they've seen before
- What the reflex should DO when it detects something
- The physics and dynamics of their system

You can hire RT engineers. You can train ROS2 developers. You CANNOT hire domain expertise - it comes from years of working with the specific robot and application.

The Forge needs tacit knowledge. The Domain Expert has to be able to articulate things they've never articulated before. "The motor sounds different before it fails." "The arm vibrates more when it's about to drop something."

Minimum viable team for pilot:
- Robotics Engineer (integration)
- Domain Expert (instinct definition)
- Project Lead (coordination)

That's 3 people, part-time. Doable for most teams.

Production team adds:
- Reflex Lead (dedicated RT specialist)
- Safety Engineer (if certified)
- More robotics engineers for testing

Skill gaps we see:
1. Real-time Linux - very few people understand isolcpus, PREEMPT_RT, SCHED_FIFO
2. Low-level C - modern developers are too high-level
3. Hardware interfaces - memory-mapped I/O, DMA, etc.

Options to fill gaps:
- Train existing staff (slow but builds internal capability)
- Co-develop with EntroMorphic (fast but creates dependency)
- Hire specialists (expensive and competitive market)

## Questions Arising

- How do we help customers extract tacit knowledge from their Domain Expert?
- Should we offer training programs?
- What's the minimum RT knowledge needed?

## First Instincts

The Domain Expert is the constraint. Everything else can be solved with money and time. Without domain expertise, the Forge produces garbage.

## What Scares Me

Customers without strong domain expertise who think they can "figure it out." They can't. The Reflexor will learn nonsense.
