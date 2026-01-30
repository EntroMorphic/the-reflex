# Raw Thoughts: Support & Maintenance

## Stream of Consciousness

Support is where companies die or thrive. Product gets you the sale. Support gets you the renewal.

Three tiers make sense:
- Community: Free, best-effort, GitHub/Discord
- Standard: Commercial customers, 4-hour critical response, business hours
- Premium: Enterprise, 1-hour critical response, 24/7

The escalation path matters:
1. Customer operations team tries first
2. Customer Reflex Lead if ops can't solve
3. EntroMorphic Support if customer is stuck
4. EntroMorphic Engineering if it's a bug

Goal: Most issues resolved at Level 1-2. We should be Level 3-4 only.

Maintenance activities:
- Daily: Telemetry review (automated alerts)
- Weekly: Reflexor health check (vitality scores)
- Monthly: Performance review (KPI tracking)
- Quarterly: Reforging assessment (drift detection)

Common issues we'll see:
1. Latency spikes - usually RT config problems
2. False positives - threshold tuning needed
3. Detection misses - possible drift, may need reforging
4. Integration failures - channel setup, permissions

Reforging is the big maintenance activity. When the Reflexor drifts:
- Accuracy drops over time
- Environment has changed
- New anomaly types appear
- Need to re-run the Forge process

Reforging is NOT a failure. It's maintenance. Instincts need updating as reality changes.

Documentation is support. If docs are good, tickets go down. Invest in:
- Quick start guides
- Troubleshooting guides
- Architecture docs
- API references

## Questions Arising

- How do we detect when reforging is needed automatically?
- What's our SLA for critical issues?
- How do we handle customers in different time zones?

## First Instincts

Support is a product, not a cost center. Excellent support is a differentiator.

## What Scares Me

Growing faster than support can scale. Every unhappy customer tells 10 people.
