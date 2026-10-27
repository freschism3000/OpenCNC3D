# Infantry sprite comparisons

20October2026: accepted the current result and authorized all remaining TD
infantry, including civilians and mission characters. Read the current all-infantry
section in the pixel sprite notes before further work. Exact E1v15 stays frozen;
new types use their own source actions, semantic analysis, character parts and
individual frame audits. Current work: art/sprites/infantry-snes-v01/.

Runtime20October2026: F5 -> Experimental -> New Sprites now opts TD E1into the
exactv15pixel art. Current-main and Dock deployment are verified; other infantry,
RA and Classic retain their art. Build with `build_runtime_v15.py`; full contract,
material mask and replay commands are in `art/sprites/minigunner-snes-v01/runtime-v01/README.md`.

Current complete browser proposal: **v15**. All31source actions /133camera
contexts /532native cells pass exact personal evidence gates. The380new slots
await design-review artistic approval; accepted72v14poses and80v09standing/fire cells
are preserved. See `motion-analysis-v03/` and `motion-study-v15/`. The entries
below retain their original delivery stage as history; v15supersedes live-v09
statements after installation. No game/Dock integration is part of this work.

New art sources: `motion_body_v15.py`, `motion_standing_v15.py`,
`motion_falls_v15.py`. Analysis and comparison/assembly tools keep DOS reads
separate from new art authoring. `review_remaining_v15.py` records individually
written observations; it does not invent verdicts. `sequence_observations_v15.py`
contains specific observations from all new ordered motions.
`package_release_v15.py` validates and packages the completed review; it cannot
fill missing frame records. `audit_gate.py` also binds AFTER stage maps, source
fps and opposite BOXING contexts. Ten synthetic rejection tests pass under
normal and optimized Python. Actual133browser endpoint/rate checks passed.

The design review approved all v14 run and Gesture1 views on19 October2026.
All72native poses and16action/facing sequences are accepted:48run+24Gesture1,
including12preserved v13 poses and60newly approved poses. Exact hashes/scope are
in motion-study-v14/approval-2026-10-19.json; the complete accepted snapshot and
original producing sources are frozen in approved/motion-study-v14-2026-10-19/.
Both v14 author and comparison writer refuse regeneration. Use a new version for
further actions/revisions. The review and frozen GIFs retain historical
pre-approval labels; explicit acceptance supersedes them. The current gallery
records approval. Live v09/full-sheet release gate stay intact; no game/Dock update.

The design review approved the shown v13 east run and east/south Gesture1 studies on19 October2026.
The exact12frames/GIFs are frozen in approved/motion-study-v13-2026-10-19/.
`author_motion_v13.py` refuses to overwrite this approved version. Author further
work under a new version; other actions/views still require analysis and review.

Current authoring order — design-review correction19 October2026: read
the pixel sprite notes and the whole-action analysis in
`art/sprites/minigunner-snes-v01/motion-analysis-v01/` BEFORE drawing.
Understand intent, support/weight, limb identity, weapon handling and entry/exit;
then plan and draw a new motion using the approved design. Audit frames afterward.
Do not continue `redraw.py`'s v10–v12 cell-reconstruction/bulk-fitting approach.
Those candidates are rejected, even when their counts/hashes are valid.

`author_motion_v13.py` is an independent local motion study: east run6poses,
east/south Gesture1 each3poses. Its authoring functions read the semantic brief
and approved design only; DOS art is read afterward by a comparison compositor.
Specific personal frame/sequence reviews are in motion-study-v13/personal-review.json.
This small study is not a complete new sheet or an installed Lab release.
The existing delivered v09 remains preserved while other actions are analyzed.

Current delivered review: v09 (standing, running and firing only). Preserve its
exact public atlas/catalog while rebuilding remaining Minigunner actions.

Mandatory personal art review: read the pixel sprite notes first. Inspect
EVERY cell against the approved character and the same DOS frame, write a specific
per-frame verdict, then review each animation/facing at source timing. Include
padding and aliases. A count/hash/build/alpha test is not an art audit. Never
programmatically fill passing verdicts. Rejected and unreviewed candidates are not
publishable and must remain outside `public/`.

`redraw.py` currently writes an INTERNAL v11 candidate under
`art/sprites/minigunner-snes-v01/animation-v11/candidate/data/`, not the public Lab.
Its death method is rejected and must be replaced before generating another batch.
`audit_boards.py` creates labelled DOS / approved character / candidate evidence;
it does not write verdicts. `animation-v11/frame-audit.json` records actual failed
inspections and accurately counts the unreviewed cells. The source/rejected v10
atlas and user examples are preserved in `animation-v10/rejection-2026-10-19/`.

`audit_gate.py` rejects new installs without every frame's personal passing verdict,
exact candidate/DOS/reference/evidence hashes, complete action/facing/stage/alias
contexts, all recipe checks and per-sequence continuity/timing reviews. Altered
pixels or evidence invalidate the review. Only the exact delivered v09 baseline
has a narrow preservation exception. This software validates review records;
it cannot certify visual quality or confer artistic approval.
Run `python3 -O -m unittest discover -s tools/sprite_lab -p test_audit_gate.py`
for synthetic gate tests. Synthetic test records are never actual art reviews.

Historical v04 gait revision:
The design review rejected v03's overlong side legs and rear rightward lean. `gait.py` anchors
the run to the measured waist, fits4px thigh/4px shin reach and keeps front/rear
legs in their hip lanes. `redraw.py` exports v04; `review_legs.py` proves that only
the48 running frames changed and preserves old/new loops plus anatomical checks.
Actual loaded browser evidence is in animation-v04/browser-proof/. Read the current
rules in the pixel sprite notes. Run `build.py`, then `redraw.py`, then
`review_legs.py`; the raw v02 export is historical, not the current displayed art.
Original timing remains run7.5fps / standing fire15fps. Other replacement actions
remain pending. This is browser-only work, without game or Dock runtime changes.

Browser-only Sprites section for the existing C&C Remaster Lab, initially E1 Minigunner. Original on the left, redesigned pixel sprites on the right. No game binaries, launchers, settings, saves or art approvals are changed.

`python3 tools/sprite_lab/build.py` reads all 532 original E1.SHP frames through the project's proven SHP decoder and the authentic LOCAL.MIX palette. All 31 applicable actions are parsed from MiniGunnerDos in idata.cpp, including directional and non-directional actions. Shadows (palette index 4) and transparent index 0 are excluded from this isolated art preview.

Historical v02 generated sheets contained standing/ground poses from v01 plus48 running and48 standing-fire frames. They were rejected for inconsistent anatomy and poor gait. Their source sheets and prompts remain in art/sprites/minigunner-snes-v01/animation-v02/. Current art uses fixed canonical upper bodies and native pixel limbs; it does not play those rejected generated cycles.

Both canvases share cycle progress, direction, origin and playback speed. Every authored new frame maps to the same original stage index, including eight-stage standing fire. Direction is disabled for original non-directional actions. Unsupported new actions show a pending message and an empty after canvas rather than an unrelated pose.

`python3 tools/sprite_lab/install_lab.py --lab-root "$HOME/CNC3D"` backs up the current Lab index, adds an idempotent C&C-only Sprites navigation route, and installs this standalone static page. Existing Infantry 3D, C&C Interface, parked RA Interface and all other routes remain intact. Open http://127.0.0.1:8796/?c=Sprites. No server restart is needed.
