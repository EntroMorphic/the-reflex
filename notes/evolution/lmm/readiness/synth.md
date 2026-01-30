# Synthesis: Readiness Assessment

## Purpose

**The assessment protects everyone.**

- Protects customer from failed deployment
- Protects EntroMorphic from reputation damage
- Creates trust through honest qualification
- Extracts knowledge for better Forge outcomes

---

## The Five Pillars

### 1. Hardware (5 points)
| Requirement | 1 pt | 3 pt | 5 pt |
|-------------|------|------|------|
| Processor | Any ARM/x86 | Cortex-A53+ | Jetson/M-series |
| OS | Linux 4.x | Linux 5.x | PREEMPT_RT |
| Sensor latency | > 100 μs | < 100 μs | < 10 μs |
| Actuator interface | Network API | Serial/CAN | Direct/PWM |
| Core isolation | None | 1 core | 2-3 cores |

### 2. Software (5 points)
| Requirement | 1 pt | 3 pt | 5 pt |
|-------------|------|------|------|
| Control loop access | Closed | Partial | Full |
| Sensor data access | ROS2 only | Pre-callback | Raw stream |
| Actuator commands | Planner only | Shared | Direct |
| ROS2 coexistence | Conflict risk | Manageable | Clean separation |
| RT capability | Blockers | Some concerns | RT-safe |

### 3. Operational (5 points)
| Requirement | 1 pt | 3 pt | 5 pt |
|-------------|------|------|------|
| "Normal" defined | Vague | Verbal | Documented |
| Anomalies known | Guesses | Some history | Full dataset |
| Response defined | Ad hoc | Partial | Response matrix |
| Safety boundaries | Unclear | Informal | Certified |
| Success metrics | None | Ideas | Baseline data |

### 4. Team (5 points)
| Requirement | 1 pt | 3 pt | 5 pt |
|-------------|------|------|------|
| RT experience | None | Some exposure | Dedicated expert |
| Embedded skills | High-level only | Some C/C++ | Low-level expert |
| Domain expertise | New to system | Experienced | Deep expert |
| Integration capacity | Overloaded | Some slack | Dedicated time |
| Operations support | None | Part-time | Dedicated ops |

### 5. Organizational (5 points)
| Requirement | 1 pt | 3 pt | 5 pt |
|-------------|------|------|------|
| Executive support | Unaware | Aware | Active sponsor |
| Budget | Unfunded | Pilot only | Production funded |
| Timeline | < 2 months | 3-4 months | 6+ months |
| Risk tolerance | Risk averse | Moderate | Embraces adaptive |
| Safety culture | Blame-focused | Cautious | Defense-in-depth |

---

## Scoring Interpretation

| Score | Level | Action |
|-------|-------|--------|
| **20-25** | Ready | Proceed to pilot |
| **15-19** | Conditional | Address gaps, then pilot |
| **10-14** | Foundation needed | Remediation plan, defer |
| **< 10** | Not ready | Significant development needed |

---

## The Domain Expert Interview

**Questions to extract tacit knowledge:**

1. "Walk me through the last three failures you saw."
2. "What do you notice that new operators miss?"
3. "If you had to train your replacement in one day, what's most important?"
4. "What does the robot sound/feel/look like before problems?"
5. "What rules do you follow that aren't written anywhere?"
6. "What would you do if you were the robot?"

**The goal:** Surface knowledge they don't know they have.

---

## Gap Remediation Paths

| Gap | Self-Fix | Co-Fix | EntroMorphic Fix |
|-----|----------|--------|------------------|
| Hardware | Upgrade | Recommend | N/A |
| Software | Refactor | Consult | N/A |
| Operational | Document | Workshop | Interview |
| Team | Train | Augment | Embed |
| Organizational | Internal | Advisory | N/A |

**Typical approach:** Co-fix for first deployment, transfer capability for independence.

---

## Assessment as Sales Tool

The assessment demonstrates:
1. We care about success (not just sales)
2. We're rigorous (not cowboys)
3. We have expertise (we know what matters)
4. We're partners (we help fix gaps)

**The assessment IS the value proposition** for serious buyers.
