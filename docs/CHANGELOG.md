# Changelog

## C&C 3D v0.6.5 "Second Look" (2026-09-01)

A pass back over the player board: every open report was read against the code again, and
this is the half that was cheap, unblocked and needed nobody's permission.

### What changed

- A music volume you set is remembered from the first note. The main menu used to put the score back to full every time it opened, so a quiet setting only took hold once you paused a mission.
- Tiberium shows on the radar, in a bright green that reads at a glance rather than the near-invisible shade the 1995 palette used for it.
- A helicopter sitting on the ground turns its blades at half speed, the way the cartridge idles them.
- The power meter on the 640x480 sidebar marks how much power the base is drawing, not just how much it makes.
- Team colours reach the infantry: a seat's foot soldiers wear its colour, not only its vehicles and buildings.
- A tank firing east or west threw its cannon flash out of the wrong side of the hull instead of off the end of its gun.
- Debris from a destroyed building rolls down a slope instead of spinning like a top. Two different rotations had been sharing the same three numbers.
- Moving the pointer off the map and onto the sidebar no longer drags the map east. Scrolling answers the edges of the screen and nothing else.
- Clicking the radar, or pressing H for your base, now puts what you jumped to in the middle of the map you can see rather than the middle of the window. It used to land half a sidebar's width to the right, and further out the wider your screen.
- A helicopter landed on sloping ground, or on the step a building's platform leaves beside it, keeps the end that points uphill instead of having it buried in the ground.

- The skirmish lobby draws its Unit Count slider and its Bonus Crates box. Both were fully built and simply never drawn, so they took your clicks and showed nothing. Unit Count still defaults to none, and says why when you move it: extra units can land on the Construction Yard's pad and block your first deploy.

- The Advanced page in the visuals options scrolls, so it can hold more settings than fit on it. Rows have their old breathing space back.

- With more than one factory or barracks, clicking a second time on one makes it the primary, the one your units come out of, and it says PRIMARY under itself while it is selected.

- Hold the right button and push: the view follows your hand, faster the further out you hold it, with a small still spot in the middle of the screen. It is on out of the box, and the pointer becomes four arrows while it travels. A right click is still a right click: the push only starts once the pointer has moved further than a click ever does, so cancelling, ordering, the radar and the build column all behave exactly as before.
- Mouse settings moved to their own page, Options then Gameplay: swapped buttons, and the new push scroll.
- **Classic mode no longer changes your mouse.** Classic is about the picture, and how you drive the game is yours. Until now, choosing Classic silently turned Swapped Mouse Buttons off and wrote that over your saved settings, so the setting could not survive being looked at. That is fixed in both directions and for both routes: the in-game Classic button leaves your mouse alone, and starting the game with --classic reads your saved settings too. It used to read none of them, and then wrote the shipped defaults back over your tuned file on the way out.
- The tuning panel keeps its own file. A dial you move with the panel open and never SAVE no longer reaches disc because you changed something else in the pause menu afterwards. Press SAVE and it does, and it survives whatever you change next.
- A preset named on the command line stays the file the game writes.
- A mouse setting changed from the in-game pause menu is remembered. Only the main menu's Visuals screen and the tuning panel ever wrote your settings to disc, so anything set while paused was lost when you quit.
- The radar painted cartridge colours under DOS ground. Each cell's minimap colour is averaged off the terrain art at load, and there was only one average while the ground could be drawn from either atlas.
- The mouse pointer went behind the codex page and drew into the world underneath it. It gets the plain arrow now, which is what 1995 shows over a modal dialog.
- The codex page shrank on a big display: every size on it is an absolute number of page pixels, so a logically bigger page put the same fixed content in a corner of it.

- The crashed aircraft the cartridge lays on certain maps is in the game at last, and now on the ground rather than only in the packs. It is a model the console draws in place of a small rock patch, on 33 of the shipped maps and 86 cells in all, and it had never been in a single pack or on a single map. It ships in every pack, appears in the model viewer as Wrecked Airframe, and the renderer stands it on the terrain where the map lays that patch. Getting there needed a second fix nobody could see: the game and the engine disagreed about the shape of one structure they pass between them, so the list of which cells carry the patch came back as nothing at all, silently and without an error. Both sides now agree, and the game says so in its log every time it reads a map and refuses to read that list if they ever stop agreeing.

- Bonus crates are the cartridge's own 3D crates, a grey steel cube and an olive wooden one, instead of a flat 1995 sprite. They are in every mission pack and are drawn on the ground. They are smaller than the sprite was, which is the size the console draws them at. A pack that has not been re-baked still draws the old sprites rather than nothing.
- Crates now appear on maps that have no tiberium on them. Three shipped missions place a crate and no tiberium, and on those the crate was drawn nowhere at all.

- Desert maps grow desert plants. The cartridge quietly swaps every tree for a cactus or a scrub bush when the theater is desert, and we had been planting green conifers on red sand: 939 cells across 44 of the shipped desert missions. All four of the console's desert plants are now in the packs and every desert map draws them. Temperate maps are untouched, which matters because one tree type appears in both.

- A repairing building gets the cartridge's own 3D wrench, and it turns. It was a flat 1995 sprite, and the source said plainly that the console had no repair wrench anywhere in it. It has one: the same open-ended spanner the repair cursor is made of. It also no longer blinks while it works. The 1995 flash showed it for fifteen ticks and hid it for fifteen, so a full revolution was never once visible.

- The repair CURSOR turns too, which it never did. The console spends its hundred-frame count as a yaw, and ours was reading the clock every frame and then drawing the wrench at a fixed angle. One spin law now serves both the pointer under your hand and the wrench over the roof, so they cannot drift apart.

### The picture

- The terrain art is a choice of three: the cartridge's own bank, the 1995 PC game's tiles, or the Remastered Collection's. Enhanced Visuals opens on the DOS art and Classic puts the cartridge back.
- The Remastered textures are read from YOUR OWN installation at runtime. None of that art ships here and nothing is written to disk. On a machine without the game the option stays greyed with a tooltip saying what would unlock it.
- Remastered infantry the same way, recoloured to this game's own eight house liveries rather than the Remaster's, so a seat's men match its vehicles and buildings.
- Every infantry type is drawn at ONE scale for all of its actions. The scale used to be derived per action, so a walking man drew about a fifth larger than a standing one and grew as he set off.
- WEATHER: soft cloud shadows drift across the ground, the buildings and the units, cast by the same sun as everything else and moving at a speed measured in cells per second. Off by default, because nothing in the cartridge has weather. Eight dials on the F5 panel.
- The cloud mask is generated rather than shipped: tiling noise, warped so the shapes have a wind in them.
- Answering a long-standing question: the cartridge's terrain is NOT lower resolution than the PC game's. Both draw 24 x 24 texels a cell. It is colour depth and smoothing, and 91% of the first GDI mission is drawn from a 16-colour bank.

### The codex

- The DATABASE tab is back, behind the fourth caption of the pre-release four-tab strip, in the slot the 640 HUD has been leaving empty. It opens a full-screen reference for all fifty things a player can build.
- Opening it stops the war: the world pauses, the sidebar drawer folds away, the battlefield dims and the music fades down a quarter. Closing it puts all four back.
- Two axes: structures, infantry, vehicles and aircraft across the top, your own house's roster down the side. The real cartridge model turns in the window, drawn through the renderer's own path so it cannot drift from the battlefield. The infantry have no model and never did, so they turn through their eight baked facings instead.
- It wears the pause dialog's own clothes: the same green, the same borders, plates and gauges, taken from those screens rather than reimplemented beside them.
- Pushing the pointer to the top edge over the DATABASE plate scrolls the map north, as it does over the others.

### The editor

- Waypoint rows are called waypoints, and each carries the number the map file, the triggers and the team orders already use.
- An armed cliff piece previews the cells it will really stamp, instead of the height brush, and says so when part of it falls off the map.
- Where you click inside a cell decides which of the five spots an infantryman stands on. A cell takes five men rather than a stack of them on one point, and placed vehicles and infantry stand in the middle of their cell instead of on its corner.
- An open dialog holds the keyboard as well as the mouse: the camera stays put, the tool and mode keys do nothing, the map cursor stops tracking the ground behind it, and Escape closes the dialog rather than the application.
- The editor can place craters and the six scorch marks, so a map made from scratch can look fought over. Clicking a crater again deepens it.
- The palette shows the barbwire fence, the wood fence, the gun boat and the hovercraft as pictures instead of name plates.
- Maps built in other C&C editors open. The picker listed them and then failed to load them, because a map remembered its name but not the folder it was found in, so the editor went looking in the folder it had been launched with. Only the two maps that happened to live in both places ever opened.
- The big New Map row says what it builds. It was labelled 128 x 128 and made a 120 x 120 playable map, because it was naming its grid on a list where every other row names its rectangle.
- New Map takes a size you type. Beside the five presets there is a CUSTOM row that accepts any rectangle from 16 x 16 up to 126 x 126, and anything over 62 a side is saved as a big map.
- A brand new map comes up as flat ground. It used to arrive wearing the raised platforms of whichever mission the editor had borrowed its tiles from, and no elevation tool could level them.
- A SMOOTH TERRAIN BRUSH, beside the rung tool rather than instead of it. Its own tab on the elevation panel, with centre size, falloff and strength sliders and RAISE, LOWER and ERASE. It writes ground heights directly and dresses no cliff art, so it makes rolling hills and soft slopes where the rung tool makes terraces. It refuses to paint water, because the sea is built from the ground's own corners and painting there lifts the sea into a hill. ERASE returns ground to the level that map calls flat, which is a different number on every map and never the constant the first build used.
- The rung tool is untouched, and that was the condition of the whole thing: its page is identical to the pixel at every window size, and a gate holds it there.
- Painting smooth ground no longer locks the rung tool out. The first smooth stroke marks the map as legitimately off the ladder, and that mark lives in the map rather than the session, so it survives saving and reopening.
- AUTO HEIGHTMAP: a button that reads the cliff art already on a map and fits ground to it, then shows a small report of the cells where it declined to guess. It no longer demands the map be converted first, which mattered because converting repainted level blocks as plain ground and erased the very art the fit reads. On a 62 x 62 imported map: 116 cliff placements read, 3868 corners moved, every one of 2769 off-ladder corners resolved, and 286 cells marked as uncertain.
- On a map that opens off the elevation ladder, UNDO, REDO, REVERT, SAVE and PLAY were all unclickable. The whole panel's hit test returned dead there.
- The editor no longer dies when you open a map after running the fit. The report from the previous map was still being drawn while the new map came up underneath it, through a function that checked one of its two pointers.

---

## C&C 3D v0.6.4 "Line Of Sight" (2026-08-31)

### New features

- Attack-move: press A, then click. Your units walk to that place and attack whatever they meet on the way, carrying on once the fighting stops.
- Hold the right mouse button and drag to pan the camera, the way the middle button already did.
- Shift-click to queue an order instead of replacing the current one. On by default in Enhanced.
- An option to swap the left and right mouse buttons, off by default.
- Team colours in skirmish: every seat picks a colour and its army wears it on the units and buildings themselves, not just on the selection box and the radar.
- The Map button cycles: the radar, then a list of the players with their names in their own colour and their kills, then back.
- The map editor ships with the build, and the Editor button on the launcher is live.
- The editor can place tiberium, so a map made from scratch can have an economy.
- The editor offers every house rather than four, and a new singleplayer map can choose which one you play.
- Units and buildings in the editor wear their owner's colour.
- Delete in the editor removes one thing at a time and finally puts the ground back to default, one undo step each.
- A PNG or PCX can be imported as a heightmap, in one undo step.
- A new map can be any size rather than one of five presets.
- The camera zooms out a little further than the cartridge allowed.
- Orders from the radar: with something selected, a left press on the minimap sends it there and the camera stays put; the right button still jumps the view.
- The editor autosaves, and its fourth mode is called Waypoints rather than Starts, which is what it holds.

### Improvements

- Buildings hold still when they are idle. The refinery, the helipad and the repair bay only move while something is actually being serviced.
- Damaged buildings smoke, from the cartridge's own emitter recipes. The Obelisk has no damaged model, so this is the only damage it has ever shown.
- The Power Plant's coolant boils.
- A building repairing itself wears the original's blinking wrench, so its health climbing and its credits falling are not the only sign.
- Harvesters work their fangs while they harvest.
- A dying vehicle shakes the camera less than a dying building.
- Health bars are a dial with three positions: off, selected only, or selected and damaged.
- Build queue numbers and hover tooltips are half the size they were.
- Four command shortcuts: X scatters, H jumps to the Construction Yard, double-tap a unit to select that type on screen, and a key to rebuild the last thing.
- Game speed, scroll rate and the three volumes survive a restart.
- A cell that refuses a building now says what is standing on it.
- The editor's palette is legible, its waypoint panel shows all 28 rather than 8, and its trigger labels no longer say the opposite of what the engine does.

### Bugs fixed

- Turning Fog of War off now reaches the mouse cursor. The pointer offered a move over an enemy the click would in fact attack.
- The camera no longer scrolls on its own while the window is in the background, which a second display made the ordinary case.
- An enemy near the top edge of the map can be shot back at. The order took the target's own cell and then threw it away on a separate test of the bare ground behind it.
- Terrain tiles draw at the right offset: the atlas packing is a compile-time constant on our side and a property of the pack on the other, and nothing checked that the two agreed.
- The selection cursor lights on enemy units, which the click already allowed.
- Infantry placed in the editor take the five sub-cell positions rather than all standing in the middle.
- Moving the pointer to the edge of the screen scrolls the map again. Throwing it past the window edge, which is what a hand actually does, did nothing.
- Scrolling right works from the window's own edge, not only from a strip beside the sidebar.
- Enemy units can be selected.
- Screen-edge scroll arrows point the way the map is going.
- Prone and firing infantry stand at the right height; a muzzle flash was deciding where their feet were.
- Flamethrower fire comes out of the gun rather than the ground.
- A parked helicopter sits on its pad instead of inside it.
- Advanced Guard Towers face the right way.
- Pinholes of open sea no longer show through bridges and riverbanks.
- Trees in the editor stand where you put them.
- You can reopen your own map in the editor, and ESC asks before throwing the session away.
- Clicking Skirmish on an install with no map packs says so instead of doing nothing.
- The tooltip and the mouse pointer no longer change size between missions.
- A rally flag no longer appears on buildings that cannot hold one, and stops vanishing when any building leaves the game.
- At most two copies of one sound effect start in a single tick.
- The GDI campaign's twelfth territory no longer offers a second choice the globe never marked.
- The power meter reads consumption, not just output.

### Platforms and builds

- The licence notice now names all 33 modified files of the borrowed engine rather than nine, and no longer claims the changes leave the simulation untouched.
- Nothing automated makes a noise: every test and harness mode is silent.

---

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

