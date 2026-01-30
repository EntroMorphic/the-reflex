# Synthesis: Support & Maintenance

## Support Philosophy

**Support is the relationship after the sale.**

| Sale | Support |
|------|---------|
| Transaction | Relationship |
| One-time | Ongoing |
| Closes deal | Drives renewal |
| Gets customer | Keeps customer |

**Excellent support is competitive advantage.**

---

## Support Tiers

| Aspect | Community | Standard | Premium |
|--------|-----------|----------|---------|
| **Included in** | Open Core | Commercial | Premium |
| **Channels** | GitHub, Discord | + Email, ticket | + Phone, Slack |
| **Response (critical)** | Best effort | 4 hours | 1 hour |
| **Response (normal)** | Best effort | 1 business day | 4 hours |
| **Availability** | Community | Business hours | 24/7 |
| **Named contact** | No | No | Yes |
| **Escalation access** | Community | Level 3 | Level 4 |

---

## Escalation Path

```
Level 1: Customer Operations
    │ (unresolved 30 min)
    ▼
Level 2: Customer Reflex Lead
    │ (unresolved 2 hours)
    ▼
Level 3: EntroMorphic Support
    │ (critical or complex)
    ▼
Level 4: EntroMorphic Engineering
```

**Goal:** Most issues resolved at L1-L2.

---

## Maintenance Cadence

| Frequency | Activity | Owner | Trigger |
|-----------|----------|-------|---------|
| Daily | Telemetry review | Ops | Anomaly alert |
| Weekly | Health check | Reflex Lead | Vitality < 0.7 |
| Monthly | Performance review | Team | KPI decline |
| Quarterly | Reforging assessment | DE + EM | Drift detected |

---

## Common Issues & Resolution

| Issue | Symptoms | Resolution |
|-------|----------|------------|
| Latency spikes | P99 > 1μs | Check RT config, core isolation |
| False positives | FP rate > 5% | Threshold tuning, possible reforge |
| Detection misses | Accuracy < 95% | Validate training data, check drift |
| Integration failures | Channels not working | Permissions, shared memory setup |
| Health decline | Vitality dropping | Investigate sensor changes |

---

## Reforging Process

**Reforging is maintenance, not failure.**

### Triggers
- Accuracy below threshold for 7+ days
- FP rate above threshold for 7+ days
- Environment change detected
- New anomaly types discovered

### Process
```
1. ALERT
   └── Trigger detected

2. ANALYSIS
   └── Root cause assessment
   
3. DECISION
   ├── Threshold tune (hours)
   └── Full reforge (weeks)
   
4. ACTION
   └── Execute chosen path
   
5. VALIDATION
   └── Confirm improvement
   
6. DOCUMENTATION
   └── Update baseline
```

---

## Self-Service Investment

**Layer 1 (self-service) handles 80% of issues.**

| Resource | Purpose | ROI |
|----------|---------|-----|
| Quick start guide | First 30 minutes | Reduces onboarding tickets |
| Troubleshooting guide | Common issues | Reduces support tickets |
| Architecture docs | Deep understanding | Reduces complex questions |
| API reference | Integration details | Reduces dev questions |
| FAQ | Repeated questions | Reduces repeat tickets |

**Every self-service resource that prevents one ticket saves hours.**

---

## Support Metrics

| Metric | Target | Purpose |
|--------|--------|---------|
| First response time | < SLA | Meet commitments |
| Resolution time | < 24 hours (non-critical) | Customer satisfaction |
| Ticket volume trend | Decreasing | Self-service effectiveness |
| CSAT | > 4.5/5 | Relationship health |
| Escalation rate | < 10% | L1-L2 effectiveness |

---

## Success vs Support

| Support | Success |
|---------|---------|
| Reactive | Proactive |
| Fix problems | Ensure value |
| Restore function | Drive outcomes |
| Ticket-driven | Relationship-driven |

**Both needed. Support keeps things working. Success ensures they're worth working.**
