# Reflect: Team Requirements

## Core Insight

**The Domain Expert is the bottleneck.**

Everything else can be bought:
- RT expertise → hire or train
- Embedded skills → hire
- Integration time → allocate

Domain expertise cannot be bought. It comes from YEARS of working with the specific robot, application, and environment. The Domain Expert has tacit knowledge that they don't even know they have.

The Forge consumes this knowledge. Without it, the Reflexor learns nonsense.

---

## Resolved Tensions

### Node 2 (Tacit Knowledge Extraction)
**Resolution:** Structured interview process.

Questions that unlock tacit knowledge:
1. "Walk me through the last failure you saw."
2. "What do you notice that new operators miss?"
3. "If you had to train your replacement in one day, what would you emphasize?"
4. "What does the robot sound/feel/look like when it's about to have problems?"
5. "What rules do you follow that aren't written anywhere?"

The Domain Expert may not know they know. Our job is excavation.

### Node 8 (Gap-Filling Trade-offs)
**Resolution:** Decision matrix.

| Factor | Train | Co-develop | Hire |
|--------|-------|------------|------|
| Speed | Slow | Medium | Fast |
| Cost | Low | Medium | High |
| Long-term capability | High | Medium | High |
| Dependency | None | Temporary | None |

**Default recommendation:** Co-develop for first deployment, train for independence.

### Node 10 (Knowledge Transfer)
**Resolution:** Explicit goal from day one.

Every engagement includes:
- Documentation of what we learned
- Training sessions for customer team
- Handoff checklist before production
- Diminishing involvement over time

Success = customer doesn't need us for maintenance.

---

## The Team Evolution

### Phase 1: Pilot (Minimum Viable)
```
Customer Side:
├── Domain Expert (25%)
├── Robotics Engineer (50%)
└── Project Lead (25%)

EntroMorphic Side:
├── Solutions Engineer (50%)
└── Customer Success (25%)
```

### Phase 2: Production (Sustainable)
```
Customer Side:
├── Domain Expert (25%)
├── Reflex Lead [new] (100%)
├── Robotics Engineers (50% each)
├── Safety Engineer (25%)
└── Project Manager (50%)

EntroMorphic Side:
├── Solutions Engineer (25%)
└── Support (as needed)
```

### Phase 3: Self-Sufficient (Target State)
```
Customer Side:
├── Reflex Lead (owns it)
├── Domain Expert (advisory)
├── Robotics Team (integrated)
└── Operations (monitoring)

EntroMorphic Side:
└── Support contract only
```

---

## The Reflex Lead Role

A new role that emerges: **Reflex Lead**.

Responsibilities:
- RT configuration and maintenance
- Reflexor health monitoring
- Reforging coordination
- Integration troubleshooting
- Bridge between Reflex and ROS2 teams

This person becomes the internal champion. Invest in them.

---

## What I Now Understand

Team building is part of the product. We're not just selling software; we're building customer capability.

The Domain Expert constraint means we MUST invest time in knowledge extraction. This isn't overhead; it's essential to good instincts.

The path to self-sufficiency should be explicit. Customers shouldn't be dependent on us forever. That's not partnership; that's capture.

The Reflex Lead role is a leading indicator. When customers have a dedicated Reflex Lead, they're committed and will succeed.
