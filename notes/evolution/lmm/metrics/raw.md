# Raw Thoughts: Metrics & ROI

## Stream of Consciousness

Metrics matter because they're how we prove value. Without metrics, it's just faith.

Primary KPIs - the things that MUST work:
- Response Latency (P99): < 1 μs - this is the whole point
- Control Rate: > 10 kHz - proves we can do high-bandwidth
- Detection Accuracy: > 95% - the Reflexor actually works
- False Positive Rate: < 5% - not crying wolf

These are non-negotiable. If we don't hit these, we've failed.

Secondary KPIs - the things that show broader value:
- Safety Incidents: 0 (ideally) - prevented events
- Autonomous Resolution: > 80% - handled without human
- System Availability: > 99.9% - uptime
- Reflexor Vitality: > 0.8 - self-organizing health

ROI is where it gets interesting.

COSTS:
- Software: $0 (open core) to $10K/month (commercial)
- Hardware: Usually existing robot + RT config
- Services: $20-50K implementation
- Internal: 1-2 FTE for 3-6 months

TOTAL COST: ~$100-200K for typical production deployment

VALUE:
- Prevented collisions: $20-100K each × how many?
- Reduced downtime: $1-10K/hour × hours saved
- Increased throughput: depends on unit economics
- Safety incidents avoided: $100K-10M+ each

The math usually works heavily in our favor. One prevented safety incident pays for years of Reflex.

Example: Industrial manipulator
- 5 collisions prevented/year × $20K = $100K
- 100 hours downtime avoided × $1K = $100K
- 5% throughput increase × $500K baseline = $25K
- Total value: $225K
- Total cost: $150K
- ROI: 50%
- Payback: 8 months

This is CONSERVATIVE. Most customers see faster payback.

## Questions Arising

- How do we measure "prevented" incidents? (Counterfactual is hard)
- How do we handle customers who don't track baseline metrics?
- What if ROI is negative for some use cases?

## First Instincts

Measure baseline BEFORE deployment. Without before/after, you can't prove value.

## What Scares Me

Customers who won't measure. "We just know it's better." That's not proof. That's hope.
