# Enhanced scripting

**Status: built and running.** Runtime in `game/enhanced_mod.h`, editor in the SCRIPT
panel's ENHANCED view, one new brain export. Gated by `tools/gate-enhanced.sh`.

This document is authoritative over any summary of it elsewhere.

---

## 0. Why there are two tiers

A Tiberian Dawn trigger is **one event paired with one action**. There is no AND, no OR,
no NOT, no counter and no variable (`trigger.h:41-108`). Every compound condition in the
shipped campaign is faked -- with the Cancel-trigger latch, or with a chain of timers that
only works if nothing runs late. The designer's actual intent is nowhere in the file; only
its approximation is.

The recon's own conclusion (`node-language-design.md` §5.3) was that a visual language
must therefore refuse to offer boolean composition, because an `AND` node would have to
compile to something that is not an AND. A project decision was to keep the honesty and lift the
constraint instead: **make it work in Enhanced mode only, and tag the maps that use it.**

So there are two tiers, and the boundary is not a matter of taste:

| | Native | Enhanced |
|---|---|---|
| Conditions | one event | up to eight clauses, ALL or ANY, each negatable |
| State | `Data` on five events, `AttachCount` | named counters |
| Effects | the eighteen actions | the same eighteen actions |
| Runs on | anything that reads `[Triggers]` | CNC3D only |
| Tagged | no | `[Basic] Enhanced=1` |

---

## 1. The trick: carriers

Enhanced does **not** reimplement the eighteen actions. It could not do so honestly --
`ACTION_CREATE_TEAM` brackets itself in `ScenarioInit++` to bypass `MaxAllowed`,
`ACTION_REINFORCEMENTS` runs `Do_Reinforcements`' edge resolution, `ACTION_ALLOWWIN` is a
win *blocker* that works through a `Blockage` counter and the trigger destructor. Copying
eighteen action bodies out of the engine means copying eighteen sets of quirks, and being
wrong about one of them.

Instead an Enhanced rule compiles to two halves:

- a **carrier** -- an ordinary `[Triggers]` line whose event is `None`, holding the
  action, house, team and persistence. Nothing inside the engine springs an `EVENT_NONE`
  trigger, so it is inert on its own.
- a **rule** in `[EnhancedScript]` -- the rich condition, naming its carrier.

`cnc_eyes` evaluates the condition each tick and, when it is satisfied, calls
`CNC3D_Spring_Trigger(name)`. The engine then runs **its own** action body.

### Why the house route

There is no shared action dispatcher in TD. `Spring(event, object)`, `Spring(event, cell)`
and `Spring(event, house, data)` each carry their own `switch (Action)`, and several
actions differ between them (`triggers.md` §3). The export uses the house route, which is
the one not tied to a particular object or cell.

Two actions are therefore out of reach on a carrier, and the editor should refuse them:
- `ACTION_WINLOSE` (`Cap=Win/Des=Lose`) exists only on the object route.
- `ACTION_ALLOWWIN`'s house-route arm is empty.

### How the match works

`Spring`'s house-route gate is `if (event != Event || house != House) return false`, so
passing the trigger's own `Event` and `House` always matches -- including `EVENT_NONE`,
which then skips every data check below the gate (`Credits`, `Build`, `NBuildings`,
`NUnits`, `Time`) and falls straight into the action switch. That is deliberate: the
carrier's `Data` field stays free and cannot be misread as a threshold.

### Persistence is the engine's

`Spring` ends with `if (success && IsPersistant == VOLATILE) Remove()`. So a one-shot
Enhanced rule gets one-shot semantics from the same code that gives them to a native one,
including the cell and object detachment `Remove` performs.

### What a 1995 engine sees

A trigger with event `None`. It loads, occupies one of the 80 trigger slots, and never
fires. **So an Enhanced map opened in the original game is a mission with holes, not a
crash** -- which is exactly why the tag matters, and why `cnc_eyes` says so loudly at
load when it finds Enhanced rules and no way to run them.

---

## 2. The file

```ini
[Basic]
Enhanced=1                        ; written only when the map really has Enhanced rules

[Triggers]
x000=None,Create Team,0,BadGuy,auto8,0     ; a carrier: event None

[EnhancedScript]
ambush=fire:x000|when:ALL|if:TIME 300|if:TYPE BadGuy HAND 1|if:!COUNTER done 1|do:ADD done 1|repeat:0

[EnhancedZones]
1234=ambush                       ; cell -> rule, as [CellTriggers] does
```

Pipe-separated `key:value` pairs, keys repeatable, order irrelevant. A reader that does
not know a key ignores it, so the format can grow without a version number.

| key | meaning |
|---|---|
| `fire` | the carrier trigger to spring. Optional: a rule may only move counters. |
| `when` | `ALL` or `ANY`. Default `ALL`. |
| `if` | one clause, repeatable, up to 8 |
| `do` | one effect, repeatable, up to 4 |
| `repeat` | `0` fire once ever, `1` fire again each time it becomes true |

**Line limit 240 characters.** `Read_Line` truncates at 255 into a fixed buffer. The
engine never queries this section so a truncated line there is harmless to it, but a
truncated line is a silently different rule to *us*. The writer refuses to write one and
says to split the rule and join the halves with a counter.

`[EnhancedZones]` is deliberately **not** `[CellTriggers]`. That section allows one
trigger per cell and forbids cell 0 (`display.cpp:1359-1391`), and both of those limits
are the engine's rather than ours.

---

## 3. The clause vocabulary

Every clause may be negated with a leading `!`.

| clause | arguments | true when |
|---|---|---|
| `TIME n` | tenths of a minute | `n * 90` ticks have passed since the mission started |
| `CREDITS house n` | | that house has `n` credits or more |
| `UNITS house n` | | that house owns `n` units or more |
| `BUILDINGS house n` | | that house owns `n` buildings or more |
| `TYPE house code n` | | that house owns `n` or more of that type |
| `ZONE house n` | | `n` or more of that house's things stand in this rule's painted zone |
| `COUNTER name n` | | that counter has reached `n` |
| `FIRED name` | | that native trigger is gone, i.e. it fired and was volatile |

`TIME` uses the same unit a native `Time` trigger does, so the two tiers mean the same
thing when they say `300`.

`FIRED` is the bridge between the tiers: an Enhanced condition can depend on a native rule
having gone off. It reads a trigger's **absence**, so it only works for volatile and
semi-persistent triggers. A fully persistent one never leaves the game and the condition
can never become true -- the editor says so when you pick one, and the mission check
flags it.

### Effects

| effect | meaning |
|---|---|
| `SET name n` | that counter becomes `n` |
| `ADD name n` | that counter increases by `n` |

Counters are created on first use and start at zero. They reset with the mission.

---

## 4. Evaluation

**Edge-triggered, always.** A rule fires on the tick its condition *becomes* true, not on
every tick it stays true. `repeat:1` means it may fire again after going false and true
again; without it the rule is spent. Without this, `CREDITS GoodGuy 1000` would create a
team fifteen times a second.

**Rules are evaluated in file order, once per tick, and a rule can see what an earlier
rule just did.** `SET`/`ADD` land immediately, and a `FIRED` clause sees a carrier that an
earlier rule sprang on the same tick. That makes one tick a small sequential program
rather than a simultaneous snapshot, which is what you want when composing two rules
through a counter -- but it does mean reordering the entries in `[EnhancedScript]` can
change behaviour, so the editor never reorders them on its own.

**A rule with no clauses is never true.** Logic says ALL over an empty set is vacuously
true; that would make a half-authored rule fire on the first tick, which is the worst
possible moment to learn about it.

The evaluator runs on the same once-per-engine-frame heartbeat the renderer uses for its
own integration, because the dump is called more than once for some ticks and an edge must
not be seen twice.

It reads **only** state the renderer already has: the per-house line the brain prints each
frame (credits, units, buildings) and the object list it draws from. It cannot desync the
simulation; the worst it can do is fail to fire a rule.

---

## 5. What Enhanced still cannot express

State these in the UI, not just here.

1. **Nesting.** `ALL` and `ANY` are flat. `(A AND B) OR C` is out of reach in one rule.
   Compose it: one rule sets a counter, another reads it. That boundary is a deliberate
   stop, not an oversight -- an expression tree in this file format would be a parser
   nobody can read in a text editor.
2. **`Cap=Win/Des=Lose` and `Allow Win`** on a carrier. See §1.
3. **Anything outside the eight clauses.** The list is what the host can answer from the
   dump. Adding a clause means adding an observation, which is a real change and not a
   config entry.
4. **Aircraft in teams** are still delivery-only; that is the engine's limit and Enhanced
   does not touch team recruitment.
5. **The five hardcoded scenario patches** and `Fixup_Scenario`'s type-class mutations
   are engine overrides that no file can express (`node-language-design.md` §5.1-2).

---

## 6. The editor

SCRIPT's third view, beside RULES and TEAMS.

- The canvas draws the conditions stacked with one continuous spine down their left --
  cyan for ALL, gold for ANY -- feeding a terminal node that names the effect and says it
  is the engine's own action, reached through the carrier.
- Conditions read as sentences: "BadGuy owns 1 HAND or more", not `TYPE BadGuy HAND 1`.
- **ADD RULE creates the carrier too, and DELETE takes it away.** A rule whose carrier
  someone must remember to add by hand is a rule that silently does nothing; a carrier
  left behind is a trigger with event None that nothing can ever fire.
- The carrier picker only offers triggers whose event is `None`. A carrier with a real
  event would fire twice, once by the engine and once by this tier.
- Zones paint green and are their own table.

The status line reads `MAP RUNS ANYWHERE` or `MAP NEEDS CNC3D`.

---

## 7. Testing

`--scripttest` (per mission, and the 95-mission gate):
- an authored three-clause rule round-trips through a save byte for byte
- the tag is written when there are rules and **removed** when the last one goes
- the evaluator over a made-up world: must not fire early, must fire when all three
  clauses hold, must not fire twice

`tools/gate-enhanced.sh` builds a real map, boots the real brain, and demands:
- the map is recognised as Enhanced at load
- an `ALL` rule fires at tick 90, which is what `TIME 1` means
- the engine runs the carrier's action (the mission ends in a loss)
- an `ANY` rule fires on its one true clause
- a rule with a false clause stays quiet
- `--trace` reports both tiers and the world readout
- the editor's own TRACE button survives both reboots with its results intact

---

## 8. Trace

`--trace N` plays N ticks headless and reports the tick each rule fired on. It is also the
TRACE button in the editor, which reboots out to play and back.

Native rules are traced by **absence**: a volatile trigger removes itself when it springs,
so the first tick its name stops resolving is the tick it fired. That is exact for the 928
volatile and 113 semi-persistent triggers in the campaign; for semi-persistent ones the
tick is the *last* of their attachments, and the report says so. The 68 fully persistent
ones have no signal at all and are reported as **not traceable** rather than as "never
fired", which would be the single most misleading thing the tool could print.

Enhanced rules need none of that inference: the evaluator records the tick itself.

The report ends with what the conditions were actually looking at -- every house's
credits, units and buildings, and every counter -- so "never fired" comes with a reason.

Editing any rule throws the last trace away. A verdict describing a rule you have since
changed is worse than no verdict, because it looks like evidence.
