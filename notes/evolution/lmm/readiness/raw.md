# Raw Thoughts: Readiness Assessment

## Stream of Consciousness

Not every customer is ready for The Reflex. Selling to unready customers creates failures, support burden, and bad reputation. Need a filter.

Five pillars emerged from thinking about what makes deployments succeed:

HARDWARE - can their robot run RT?
- Need decent processor (ARM Cortex-A53+ or x86_64)
- Need Linux that can be RT-configured
- Need low-latency sensor access (not through three API layers)
- Need direct actuator control (not through cloud)
- Need isolatable CPU cores

SOFTWARE - is their architecture compatible?
- Can we inject a fast path alongside their slow path?
- Can we tap sensor data before ROS2 processing?
- Can we command actuators directly?
- Will ROS2 and Reflex coexist peacefully?

OPERATIONAL - do they understand their own system?
- Can they define "normal" operation?
- Do they know what anomalies look like?
- Have they thought about what the reflex should DO?
- Do they have historical data for training?

TEAM - do they have the skills?
- Real-time Linux experience (rare!)
- Embedded C/C++ capability
- Domain expertise (this is irreplaceable)
- Time allocated for integration

ORGANIZATIONAL - will they let it succeed?
- Executive sponsor who believes
- Budget for real implementation (not just "pilot and see")
- Realistic timeline (3-6 months, not 3 weeks)
- Culture that accepts adaptive/learning systems

The scoring matters. Below 15/25, don't proceed. You'll fail. Address gaps first.

## Questions Arising

- How do we assess readiness without offending prospects?
- What's the minimum viable readiness for a pilot?
- How do we help customers BECOME ready?

## First Instincts

The readiness assessment is a sales tool AND a quality gate. It shows we're serious about success, not just closing deals.

## What Scares Me

Customers who score low but have budget. Tempting to take the money, but it leads to failure.
