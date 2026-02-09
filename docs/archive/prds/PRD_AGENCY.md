# PRD: Agency

> Given a goal, use crystallized knowledge to achieve it.

## Problem

The C6 explores, crystallizes knowledge, and predicts outcomes. But it has no *purpose*. It doesn't act toward goals — it just explores.

Agency closes the loop: knowledge enables purposeful action.

## Goal

When given a goal state (e.g., "increase ADC0 reading"), the system should:
1. Query crystallized knowledge for actions that affect that input
2. Choose the action most likely to achieve the goal
3. Execute and verify

## Goal Specification

```c
typedef struct {
    uint8_t  input_idx;       // Which input to affect (0-12)
    int16_t  target_delta;    // Desired change (positive = increase)
    uint8_t  priority;        // 0=explore, 255=urgent
} goal_t;
```

**Example goals:**
- `{input: 8, delta: +1000, priority: 200}` — Increase ADC0
- `{input: 8, delta: -1000, priority: 200}` — Decrease ADC0
- `{input: 0, delta: +1000, priority: 100}` — Toggle GPIO input 0 high

## Agency Logic

```c
uint8_t agency_choose(goal_t* goal) {
    // 1. Find crystals that affect this input
    int best_output = -1;
    int best_match = 0;

    for (int o = 0; o < NUM_OUTPUTS; o++) {
        crystal_t* c = crystal_lookup(o, goal->input_idx);
        if (!c) continue;

        // Does this crystal move us toward the goal?
        bool same_direction = (c->expected_delta > 0) == (goal->target_delta > 0);
        if (same_direction) {
            int match = c->confidence;
            if (match > best_match) {
                best_match = match;
                best_output = o;
            }
        }
    }

    return best_output;  // -1 if no known action
}
```

## Integration with Exploration

Agency and exploration coexist:

```c
void layers_tick(void) {
    uint8_t chosen;

    if (current_goal.priority > 0) {
        // Agency mode: try to achieve goal
        int agency_choice = agency_choose(&current_goal);
        if (agency_choice >= 0) {
            chosen = agency_choice;
        } else {
            // No known action — explore to learn
            chosen = choose_output(&state);
        }
    } else {
        // Pure exploration
        chosen = choose_output(&state);
    }

    state.chosen_output = chosen;
    // ... rest of tick
}
```

## Goal Sources

Goals can come from:
1. **External command** — Serial/USB message sets a goal
2. **Button press** — Hardware trigger
3. **Internal threshold** — "If ADC0 > X, goal: reduce it"
4. **Streaming command** — Pi4 sends goal via reverse channel

## API

```c
// Set a goal
void agency_set_goal(uint8_t input, int16_t delta, uint8_t priority);

// Clear goal (return to pure exploration)
void agency_clear_goal(void);

// Check if goal was achieved
bool agency_goal_achieved(goal_t* goal, int16_t observed_delta);

// Get current goal
goal_t* agency_current_goal(void);
```

## Falsification

1. **Can it achieve a goal?** Set goal "increase ADC0", verify it toggles GPIO0.
2. **Does it use knowledge?** With no crystals, it should explore. With crystals, it should act.
3. **Does it verify?** After action, does it check if goal was achieved?
4. **Does it give up?** If goal impossible (no known action), does it explore?

## Success Metrics

| Metric | Target |
|--------|--------|
| Goal achievement rate | >80% for known relationships |
| Time to goal | <5 ticks when crystal exists |
| Fallback to exploration | 100% when no crystal exists |
| False confidence | <10% (acting when shouldn't) |

## Example Session

```
[100] GOAL SET: increase ADC0
[100] AGENCY: crystal says GPIO0 → ADC0 (+3218)
[100] ACTION: toggle GPIO0 HIGH
[100] OBSERVE: ADC0=+3195 (goal: +1000) ✓ ACHIEVED

[200] GOAL SET: increase ADC2
[200] AGENCY: no crystal for ADC2
[200] FALLBACK: explore (chose GPIO5)
[201] CRYSTALLIZED: GPIO2 → ADC2 (+3200)
[202] AGENCY: crystal says GPIO2 → ADC2 (+3200)
[202] ACTION: toggle GPIO2 HIGH
[202] OBSERVE: ADC2=+3180 ✓ ACHIEVED
```

## Files

| File | Purpose |
|------|---------|
| `reflex_agency.h` | Goal structure, agency_choose logic |
| Update `layers_main.c` | Integration with tick loop |

## Non-Goals

- Multi-step planning (just single-action goals)
- Continuous control (just discrete toggle actions)
- Learning new goals (goals are externally specified)

---

*"Knowledge without purpose is idle. Purpose without knowledge is blind. Agency requires both."*
