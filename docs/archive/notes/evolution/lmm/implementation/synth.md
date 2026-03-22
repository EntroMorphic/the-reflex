# Synthesis: Implementation

## The Four Phases

```
DISCOVER        INTEGRATE       FORGE           OPERATE
2-3 weeks       3-4 weeks       3-4 weeks       Ongoing
────────────────────────────────────────────────────────
Validate fit    Build foundation Create instinct Maintain & improve
```

---

## Phase 1: Discover (2-3 weeks)

| Week | Activities | Outputs |
|------|------------|---------|
| 1 | Readiness assessment, use case definition | Readiness score |
| 2 | Architecture design, baseline measurement | Integration plan |
| 3 | Resource planning, timeline agreement | Project plan |

**Exit Criteria:**
- [ ] Readiness ≥ 15/25 (or remediation plan)
- [ ] Use case documented
- [ ] Baseline metrics collected
- [ ] Architecture approved
- [ ] Resources allocated

---

## Phase 2: Integrate (3-4 weeks)

| Week | Activities | Outputs |
|------|------------|---------|
| 1 | RT kernel, core isolation | Configured hardware |
| 2 | Sensor channel integration | Sensor → Reflex working |
| 3 | Actuator channel integration | Reflex → Actuator working |
| 4 | ROS2 bridge, telemetry | Full integration |

**Exit Criteria:**
- [ ] RT jitter < 10 μs
- [ ] Sensor path < 1 μs
- [ ] Actuator path < 1 μs
- [ ] ROS2 bridge operational
- [ ] End-to-end < 2 μs demonstrated

**Gate:** If integration fails, DO NOT proceed. Fix foundation first.

---

## Phase 3: Forge (3-4 weeks)

### Week 1: Immersion
| Activity | Watch For |
|----------|-----------|
| Deploy unfrozen Reflexor | Weight velocity |
| Feed live sensor data | Convergence trend |
| Reflexor learns "normal" | Stability indicators |

### Week 2: Observation
| Activity | Watch For |
|----------|-----------|
| Delta Observer monitors | Scaffolding formation |
| Collect validation data | Known anomaly events |
| Assess convergence | R² improvement |

### Week 3: Crystallization
| Activity | Watch For |
|----------|-----------|
| Detect scaffolding dissolution | Silhouette → 0 |
| Validate on held-out data | R² > 0.9 |
| Freeze weights | Stable output |

### Week 4: Validation
| Activity | Watch For |
|----------|-----------|
| Test against known anomalies | Accuracy > 95% |
| Measure false positives | FP rate < 5% |
| Tune thresholds | Optimal sensitivity |
| Domain Expert approval | Sign-off |

**Exit Criteria:**
- [ ] Accuracy > 95% on validation set
- [ ] False positive rate < 5%
- [ ] Latency < 1 μs P99 maintained
- [ ] Domain Expert approves
- [ ] Safety review complete (if applicable)

---

## Phase 4: Operate (Ongoing)

| Frequency | Activity | Owner |
|-----------|----------|-------|
| Daily | Telemetry review | Operations |
| Weekly | Health check | Reflex Lead |
| Monthly | Performance review | Team |
| Quarterly | Reforging assessment | Domain Expert + EM |

### Reforging Triggers
- Accuracy below threshold for 7+ days
- FP rate above threshold for 7+ days
- Environment change detected
- New anomaly types discovered

### Reforging Process
1. Alert triggered
2. Root cause analysis
3. Threshold tune vs full reforge decision
4. Execute (hours vs weeks)
5. Validate improvement
6. Document and update baseline

---

## Timeline Summary

| Deployment | Discover | Integrate | Forge | Total |
|------------|----------|-----------|-------|-------|
| **Pilot** | 2 wks | 3 wks | 3 wks | 8-10 wks |
| **Production** | 3 wks | 4 wks | 4 wks | 14-18 wks |

**No shortcuts.** Faster timelines = failed deployments = injuries.

---

## Recovery Procedures

```
Phase failed?
      │
      ▼
Identify root cause
      │
      ▼
Loop back (never forward)
      │
      ▼
Fix foundation
      │
      ▼
Re-attempt
```

**Never force forward.** The phases are sequential for safety.
