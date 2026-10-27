# Fair Skirmish AI — four per-player difficulties

Implemented 19 October 2026 for Tiberian Dawn, the TD large-map fork and Red Alert.
This supersedes the old lobby investigation's statement that skirmish difficulty
is unavailable. Campaign difficulty and campaign scripts retain their existing paths.

## Player contract

Every computer seat has its own Easy, Normal, Hard or Expert setting. Normal is
the default. Human, open and blocked seats display a dash. The host controls AI
settings; network joiners see the synchronized values without editing them.
Red Alert's Rules page **New AI default** chooses the level for newly added computers;
changing it does not overwrite existing per-player choices.

All four levels use the same game rules as human players: starting funds,
resource yield, prices, refunds, prerequisites, production queues, build speed,
damage, armor, weapon reload, movement, sight and power rules. Real faction and
country differences remain. No level receives money injections, free replacement
units, faster harvesting, a full-value sale refund, parallel copies of the same
production queue, or knowledge of hidden enemy assets or funds.

## Controller and differences

`brain/fair_ai.h` is the shared policy table. Its stable serialized values are
Easy=0, Normal=1, Hard=2 and Expert=3. Timers use simulation ticks, not wall time;
15 ticks are one game second at normal simulation speed.

| Decision parameter | Easy | Normal | Hard | Expert |
| --- | ---: | ---: | ---: | ---: |
| Economy planning interval (ticks) | 45 | 30 | 15 | 10 |
| Tactical interval (ticks) | 30 | 15 | 10 | 6 |
| Scout reassignment interval (ticks) | 450 | 300 | 180 | 120 |
| Attack planning interval (ticks) | 2700 | 1800 | 1200 | 900 |
| Minimum attack force | 4 | 6 | 8 | 10 |
| Home reserve (%) | 35 | 25 | 20 | 20 |
| Normal cash reserve | 600 | 500 | 500 | 500 |
| Scouts | 1 | 1 | 2 | 2 |
| Observed-counter weighting (%) | 25 | 60 | 85 | 100 |
| Retreat health threshold (%) | 25 | 30 | 35 | 40 |
| Maximum orders per tactical pass | 3 | 6 | 10 | 14 |
| Refinery expansion limit | 2 | 3 | 4 | 4 |
| Attack groups | 1 | 1 | 2 | 3 |

Both controllers deploy their MCV, establish power and income, build legal
production and technology, maintain harvesters, expand their economy, scout,
counter observed forces, reserve defenders, send attack waves, retreat damaged
units and use permitted support powers. Higher modes plan more often, respond
more strongly to observed counters and coordinate more groups. These are policy
budgets; terrain, starting funds, available technology and opposition still
determine the actual force and timing. A larger force threshold can delay an
individual attack. The labels do not promise a monotonic victory time on every map.

Income-chain recovery takes priority over discretionary production. The AI can
cancel its own paid queue or sell an optional owned building at the ordinary
refund to fund a lost harvester/refinery. It cannot recover by creating money.
All production requests are checked against current prerequisites under each
game's human production rules, including after a factory or prerequisite is lost.
RA uses completed buildings; TD retains its shared active-building scan semantics. Multiple real factories retain
the same acceleration a human receives, with one paid queue per category.

The controllers have separate private per-house random streams so planning does
not consume the combat/resource random stream. Selection is attached to the
original lobby seat before the engine assigns/remaps houses, including gaps in
the roster and randomized colors or countries.

## Knowledge boundary

Computers receive their own normal sight updates rather than the legacy path that
skipped exploration for nonhuman houses. Target selection, counters, superweapons
and persistent attack/navigation handles are filtered through per-house observation.
Hidden enemy money, cargo, kill history and base-zone metadata are not target
valuation inputs. Scout destinations may use the map's public potential starts;
they never use the actual hidden enemy start or a hidden moving enemy position.

This follows each game's existing shroud semantics, including permanently explored
terrain where that game exposes it. It does not introduce a different modern fog
system. RA's GPS, spy-plane reveal, Gap Generator jamming, shroud regrowth, cloaking
and spy disguise use the same relevant rules; nearby owned dogs can detect spies.
An enemy object handle that becomes hidden is cleared; cell orders can retain
an already observed location without tracking hidden movement.

## Save, network and diagnostics

The legacy `HouseClass` layouts are unchanged. TD stores an optional versioned
footer with skill, timers, scouts, objectives, random streams and policy identity.
The TD footer also restores engine random words needed for exact future continuity.
Older saves without the footer remain readable; damaged/truncated tagged data is
rejected. RA uses the existing reserved DLL save area with explicit fixed-width
state and policy/version validation. Old multiplayer saves normalize legacy
difficulty handicaps while retaining faction/country differences. Campaign saves
retain campaign behavior.

The lobby wire version is 14, with all eight per-seat levels carried in SETUP.
Changes invalidate READY. The engine's policy hash participates in the network
compatibility identity, so older/different AI policies cannot silently join.
Private AI state also participates in the ongoing lockstep world hash; divergence
in plans or random state is detected before it becomes an unexplained battle difference.

Engine exports are `OpenCNC3D_Set_AI_Difficulties`, `OpenCNC3D_AI_Policy`,
`OpenCNC3D_AI_State_Hash` and `OpenCNC3D_AI_Diagnostics`. The renderer's read-only
`aidump` script command prints `FAIRAI` and `FAIRRULE` receipts. `--difficulty`
accepts all four names in both games. `--ai-levels` accepts up to eight comma-separated
names or values 0–3 indexed by original roster seat (human entries are ignored).

## Interface construction and evidence

The column reuses the accepted TD/RA native menu material and typography. Enhanced
local Players stays 600 logical units wide; Difficulty is a 120×30 cell at x500
with a 32-unit row stride. Enhanced network Players is 508 wide, Map is 188 wide
at x566 and Chat is 404 wide at x774; their height is 368 and gaps are 20. Difficulty
is at x408. The Classic 320×200 controller uses a narrow E/N/H/X column and full
names in the seat popup. Human rows display a dash.

The reproducible Remaster Lab review and exact source/image hashes are preserved
in `tools/art/remaster/viewer/ai-difficulty-v01/manifest.json`. The native controller
gate exercises pointer, keyboard, popup, all four values, host permissions and
720p/1440p drawing for both games. Browser review uses actual native captures.

## Regression procedure

`tools/regressions/fair-ai/run.py --install <Dock-playable> --out <scratch>` builds
the dependency-free probes and runs real headless simulations. The matrix covers
all four levels in both games, four mixed computer levels with shuffled colors,
same-country stat parity, other seeds, independent local-player contexts, RA shroud
regrowth, exact save restoration and subsequent trajectory equality. A hidden-enemy
cash perturbation changes the world but must leave the AI state unchanged. The
baseline and independent repeat must match both world and private AI state.

The matrix counts deployed structures, excluding limbo/pending production. Separate
two-brain harnesses compare independent engine instances every tick while orders,
production, combat and mixed levels run. A deliberate one-sided mutation is the
negative control. Native lobby/network tests cover all eight seats and four wire
values, invalid values, readiness and read-only clients. Installed shell traces
exercise actual lobby clicks, a blocked middle seat and the engine handoff.

Final build identities, installed hashes, preservation checks and test receipts
belong in the dated verification record. Workspace builds and browser screenshots
alone are not Dock delivery.
