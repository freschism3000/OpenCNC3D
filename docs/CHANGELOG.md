# Changelog

## C&C 3D v0.6.3 "Blast Radius" (2026-08-24)

### New features

- Destroyed buildings, vehicles and aircraft come apart into their own pieces, which fly out, bounce, roll downhill, settle and sink into the ground.
- A destroyed tank's turret comes off whole, and the Hand of Nod's ball leaves the building in one piece.
- The ground shakes when a building blows up, scaled by its footprint.
- Buildings wear the cartridge's own battle damage below half health.
- The Ion Cannon fires a beam down onto its target, with three shock rings that spread out and dissolve.
- A nuclear strike grows the cartridge's own mushroom cloud, the cap swelling on a stretching stem, and drifts away when it is spent.
- Unlock Superweapons on the cheat menu: ion cannon, nuclear strike and air strike, granted and always ready.
- Build Anywhere on the cheat menu: put a building down anywhere on the map, not just beside your base.
- Instant Win and Instant Lose buttons on the cheat menu.
- Score screens end on the faction's own emblem, in 3D, turning slowly, in a corner cleared for it.
- The Nod campaign has its map screens between missions: the globe, Africa, and the territory choice.
- Nod's map and score screens play Nod's own music.
- Music shuffles by default in the campaign and in skirmish, and GDI's first mission still opens on Act On Instinct.
- Map previews in the skirmish lobby.
- Crates can be switched on in the skirmish lobby, and show on the map.
- A starting-units setting in the skirmish lobby.
- The skirmish lobby seats every player on the map, and each one picks their own side, team and colour.
- Eight player colours, one square each: taking a colour someone already holds swaps the two of them rather than refusing.
- Control groups on the number keys: CTRL assigns, a bare digit recalls, SHIFT adds the group to your selection, ALT recalls and centres the camera.
- Click a unit cameo again to queue another, up to nine deep, with the count drawn on the cameo.
- Right-clicking a cameo takes one off its queue before it holds or cancels what is already building.
- Rally points: left-click the ground with a production building selected and everything it builds heads there.
- A selected building with a rally point flies a flag on it, in your colour.
- Sidebar tooltips: hover a cameo for its name and price, or the Repair, Sell, Map and Options buttons for their names.
- Abort Mission now asks first: Abort, Restart or Cancel.
- User Maps on the main menu lists the single player maps in missions/user_maps, ready to play.
- Your own multiplayer maps are in the skirmish lobby, on a USER MAPS tab beside OFFICIAL.
- A launcher on both platforms, in the 1995 dialog style: the build you have, the changelog in a scrolling panel, Play, and a greyed-out Editor.
- The launcher watches the Builds section of cnc3dgame.com. When a newer build is up, Play becomes Update.
- Update downloads it with a progress bar and installs it, without leaving the launcher.
- The changelog in the panel is the one on the website, so it is never out of date.
- An update takes the small binaries package instead of the whole 500 MB one whenever your game data already matches.
- Windows has a proper install wizard, with a Start Menu entry, an optional desktop shortcut and an uninstaller.

### Improvements

- A damaged Communications Center or Advanced Communications Center stops turning its dish, and a damaged Weapons Factory stops working its door.
- New desert cameo art in the Enhanced HUD for every buildable except the aircraft, ships and superweapons.
- Shatter controls in the F5 settings panel: blast force, tumble, roll, linger, and camera shake.
- Building shatter and camera shake ship on tuned settings: a harder outward blast, less tumble, a shorter linger and a heavier jolt.
- The changelog also ships beside the game, so the launcher has notes to show before it has spoken to anything.
- Every download is checked against its published SHA-256 before a single file is replaced.
- A download that stops part way is refused rather than unpacked.

### Platforms and builds

- The Windows package installs per user, so updates need no administrator prompt.
- C&C3D.exe is the launcher now and cnc3d.exe beside it is the game.

### Bugs fixed

- A superweapon aimed at ground you had not explored spent its charge and nothing happened.
- Tiberium blurred when bilinear filtering was on.
- Reset to Defaults on the cheat menu could not turn Unlock Superweapons back off.
- Winning a skirmish and starting a new match resumed the finished one.
- The launcher left a copy of the mouse pointer behind everywhere it had been.
- The GDI score screen painted its own gold medal into the corner the turning emblem occupies, so the two overlapped. GDI now uses the same background plate as Nod, which never drew one -- it keeps its own music and its own emblem, but it no longer shows the four casualty bars that plate carried.

---

---

## C&C 3D v0.6.2 "Now You See It" (2026-08-24)

### Platforms and builds

- The Windows executable carries its build number, so Explorer's Properties tab shows which build you have.
- The Windows log is always written now: it falls back to your user folder when the game folder is read-only, and says which one it used.
- A crash on Windows names the stage it died in.
- The Windows packager refuses to ship a brain it did not build, instead of quietly reusing an old one.
- The download explains the "Windows protected your PC" warning and how to clear it before extracting.

### Bugs fixed

- The Hand of Nod dropped its globe on the ground and snatched it back, over and over.
- Enemy Stealth Tanks were drawn in plain sight while cloaked, shadow and radar blip included.
- Nothing on fire cast any light, and neither did artillery, vehicle hits, the Ion Cannon or the nuclear strike.
- The building placement outline ignored the height of the ground, so on a slope it sat off the cells it was naming.
- Starting a music track decoded nearly three seconds of audio inside a single frame.
- Ground decals split their cells on the opposite diagonal to the ground beneath them.
- A short or corrupt pack ended the game instead of naming the file.

---

---

## C&C 3D v0.6.1 "Skirmish" (2026-08-23)

### New features

- Skirmish against the computer. Skirmish on the main menu is live: pick a side, up to five opponents, a map, and play.
- A proper skirmish lobby: a player roster in each player's colour, the map list, a preview of the ground with the start positions marked, side buttons, and sliders for opponents, tech level and credits.
- Nine skirmish maps from the 1995 discs, converted to cartridge terrain art.
- The computer opponent builds its own base, produces units, defends and attacks.
- Command line entry too: `--skirmish [--side gdi|nod] [--ai N] [--credits N] [--starts a,b] [--notiberium] [--crates] [--nosuper]`.
- A cheat menu on the star key: infinite money, instant build, unlock tech tree, fog of war and invincibility, with Reset to Defaults. Nothing is on until you turn it on.

### Improvements

- A skirmish opens the view on your own base instead of the middle of the map.
- The cursor now shows when an MCV cannot deploy where it stands, instead of silently accepting the click.
- A preview picture for every skirmish map, drawn from the same terrain art the game draws.
- The main menu's Multiplayer entry is now two: Skirmish, which works, and Multiplayer, drawn disabled because there is no networking yet.
- New cameo art in the Enhanced HUD, 49 of the 54 buildables replaced.
- Eighteen new verification gates: skirmish start, the computer building, the opening click on every shipped map, side art, reproducibility, every cheat switch, the lobby labels and click-through, and each of the fixes below.

### Platforms and builds

- The Windows log overwrote half its own lines, losing every diagnostic sent to stdout.

### Bugs fixed

- A skirmish could never end: the engine crashed the moment a human player was defeated.
- Every unit in a skirmish drew in Nod colours, including your own army.
- The radar drew every blip light blue in a skirmish.
- Infantry uniforms and construction scaffolding used the wrong side's art in a skirmish.
- Saving or loading during a skirmish silently turned it into a single player game and switched the computer opponent off. It is refused for now.
- Scrolling east at the right edge of the screen never worked.
- Aircraft were always drawn facing south instead of the way they were flying.
- The mouse cursor grew enormous while placing a building or clicking around the radar.
- Infantry stepping off a transport were drawn far from where they came ashore.
- A moving unit left its selection bracket behind.
- Vehicles standing still kept playing their movement animation.
- Building aprons, scorch marks and craters were brighter than the ground they lie on.
- Terrain showed through the fog of war.
- The SAM site's launcher swept the wrong way and its rockets left sideways.
- The Ion Cannon had no targeting cursor.
- A superweapon left armed stayed armed into the next mission.

---

---

## C&C 3D v0.6.0 "Clear Skies" (2026-08-22)

### New features

- Aircraft appear in the game for the first time: Orcas, Apaches, Chinooks and A-10s
- The Airstrike now arrives: three A-10s fly in and attack the cell you picked
- Orcas built on a helipad are visible, and can be selected and ordered
- Orcas and Apaches show their ammo as pips, from the cartridge's own count of five
- Aircraft fly at the cartridge's own altitude, with their shadows on the ground below

### Improvements

- Health bars and pip rows on an aircraft sit at its altitude instead of on the ground
- Aircraft move smoothly between simulation steps, like every other unit
- Band select picks up aircraft
- Clicking an aircraft parked on a helipad selects the aircraft, not the pad
- A selected Orca or Apache shows its ammo strip
- The Game Speed slider works; it did nothing before
- Health bars and storage pips sit clear of the building they belong to

### Platforms and builds

- The macOS build now copies the engine into the package and refuses to build without it
- Windows now writes cnc3d-log.txt, which the READ-ME has always asked players to send

### Bugs fixed

- The Airstrike fired but nothing ever appeared
- An Orca built on a helipad was invisible
- A build could ship a renderer and an engine that disagreed, with every test still green
- Pressing Cancel in the Special Ops screen quit the whole game
- The minimap was visible before you had built a Communications Center
- The power meter read full whenever output met demand, whatever the numbers were
- The second East territory on the GDI map led to the same mission as the first

---

