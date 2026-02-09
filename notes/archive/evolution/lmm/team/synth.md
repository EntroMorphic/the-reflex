# Synthesis: Team Requirements

## The Critical Constraint

**The Domain Expert is irreplaceable.**

Everything else can be solved with money and time:
- RT expertise → hire or train
- Embedded skills → hire
- Integration capacity → allocate

Domain expertise comes from years with the specific robot, application, and environment. It cannot be purchased.

---

## Role Definitions

### Domain Expert (Customer)
| Responsibility | Time | Criticality |
|----------------|------|-------------|
| Define "normal" patterns | 25% | **Critical** |
| Identify known anomalies | 15% | High |
| Validate Reflexor behavior | 10% | High |
| Approve crystallization | 5% | **Critical** |

**Cannot be outsourced. Must be internal.**

### Reflex Lead (Customer)
| Responsibility | Time | Criticality |
|----------------|------|-------------|
| RT configuration | 30% | High |
| Integration | 40% | High |
| Health monitoring | 20% | Medium |
| Troubleshooting | 10% | Medium |

**New role. Emerges from robotics engineer with RT training.**

### Robotics Engineer (Customer)
| Responsibility | Time | Criticality |
|----------------|------|-------------|
| ROS2 integration | 40% | High |
| Testing | 30% | Medium |
| Documentation | 20% | Medium |
| Support | 10% | Low |

### Project Manager (Customer)
| Responsibility | Time | Criticality |
|----------------|------|-------------|
| Coordination | 40% | Medium |
| Timeline | 30% | Medium |
| Stakeholder mgmt | 20% | Medium |
| Reporting | 10% | Low |

---

## Team Sizing

| Phase | Minimum | Recommended |
|-------|---------|-------------|
| **Pilot** | 3 part-time | 4 part-time |
| **Production** | 5 mixed | 7 mixed |
| **Scale** | 8+ | 10+ |

### Pilot Team (Minimum)
- Domain Expert (25%)
- Robotics Engineer (50%)
- Project Lead (25%)

### Production Team (Recommended)
- Domain Expert (25%)
- Reflex Lead (100%)
- Robotics Engineers ×2 (50% each)
- Safety Engineer (25%)
- Project Manager (50%)

---

## Skill Gap Analysis

| Skill | Gap Frequency | Remediation |
|-------|---------------|-------------|
| RT Linux | 80% of teams | Training (2-day course) |
| Low-level C | 60% of teams | Hire or train |
| Hardware interfaces | 70% of teams | Hire embedded specialist |
| Domain expertise | 0% (internal) | Interview extraction |

---

## Knowledge Extraction Interview

**Questions to surface tacit knowledge:**

1. "Walk me through the last three failures you saw."
2. "What do new operators always get wrong?"
3. "What does the robot sound/feel/look like before problems?"
4. "If you had to train your replacement in one day, what's essential?"
5. "What rules do you follow that aren't documented?"
6. "What patterns do you notice that you've never told anyone?"

**Goal:** They don't know what they know. Help them articulate.

---

## Team Evolution Path

```
PHASE 1: Pilot
├── EntroMorphic: Heavy involvement (50%+)
├── Customer: Learning mode
└── Goal: First successful Reflexor

PHASE 2: Production
├── EntroMorphic: Moderate involvement (25%)
├── Customer: Taking ownership
└── Goal: Production deployment

PHASE 3: Self-Sufficient
├── EntroMorphic: Support contract only
├── Customer: Full ownership
└── Goal: Independent operation

PHASE 4: Center of Excellence
├── EntroMorphic: Partnership
├── Customer: Teaching others
└── Goal: Internal capability center
```

**Success = customer doesn't need us for maintenance.**
