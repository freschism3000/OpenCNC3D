# Changelog

## OpenCNC3D v0.9.0 Alpha "Red Alert Arrives" (2026-10-26)

### New features

- **Red Alert arrives as RA3D.** Play Red Alert in 3D as a separate downloadable game, alongside Tiberian Dawn under its new Dawn3D launcher name.
- **New Skirmish AI for both games.** Choose Easy, Normal, Hard or Expert for each computer player in Classic and Enhanced lobbies. Normal is the default.
- **AI that builds and fights.** Computers deploy their MCV, build an economy and technology, scout, defend, counter observed forces, coordinate attacks and retreat damaged units. Higher levels plan more often and coordinate stronger attacks.
- **Fair AI difficulty.** All levels use normal income, production, combat and visibility rules, including shroud and cloaking. No free money, free units, faster harvesting, hidden enemy information or difficulty-based stat bonuses.
- **Persistent AI settings.** Mix difficulties in one match, synchronize host choices in multiplayer, and preserve AI decisions and random state in saves. Campaign AI and scripts retain their existing behavior.
- **Red Alert campaigns.** Allied and Soviet mission routes include difficulty selection, briefings, saves, campaign scores, map screens and ending movies, using Red Alert's own engine and rules.
- **Red Alert Skirmish.** Play all 24 multiplayer maps with previews, up to seven computer opponents, eight countries and their colors, technology levels up to 10 and selectable starting credits.
- **Red Alert multiplayer.** Host and join through the game list and relay routes. Matches synchronize the host's seed, reject mismatched engines or map data, and end on RA's TOP SCORES screen.
- **Red Alert Enhanced HUD.** A faction-neutral steel interface adds live radar, fog, production controls, the Database and information panels, including installations containing only RA3D.
- **Game modules in the launcher.** Add Dawn3D or RA3D, manage each game's optional content, and remove or reinstall modules while keeping saves and settings.
- **Independent water reflection rotation.** Turn the water highlight separately from sunlight with the new WATER control in the developer F5 menu.
- **Animated graphics-mode transitions.** Map tiles tear and turn when switching modes, accompanied by rumble and camera shake.
- **Optional language packs.** Dawn3D supports German; RA3D supports German and French. Movies and music can be chosen separately for each game.

### Improvements

- **Remastered Red Alert artwork.** Approved buildings, vehicles, aircraft and ships join the shared OpenCNC3D models, with Red Alert's own Enhanced menu artwork, animated emblems and action cursors.
- **Animated RA buildings.** Placement, damage and destruction stages follow actual game state, including factory doors, the Tesla Coil, Iron Curtain and Ore Silo. Naval debris splashes and sinks.
- **Animated RA vehicles.** Ore Trucks scoop, carry and unload ore; V2 launchers raise and fire arcing rockets; Transports deploy ramps. Aircraft have landing gear and propellers, ships turn their weapons, Cruisers use both turrets and Submarines dive.
- **RA scenery and effects.** Approved ore, gems, mines, barrels, crates, fences, ice floes and other props gain 3D artwork, alongside RA's atomic blast, crate rewards, flags and parachutes.
- **Shared vehicle redesigns.** The approved Mammoth, Harvester and turbine Orca include their materials and wrecks. Vehicles, aircraft and wrecks use smoother normals and smoother hull and turret turning.
- **Moving lights and working machinery.** Lamps follow moving parts, cast soft light and respect intervening geometry. The Harvester's working parts and sprung chassis follow movement and refinery unloading.
- **Winter and Desert scenery.** Winter villages, frosted broadleaf trees and snowy conifers appear on Dawn3D Winter and RA3D Snow maps. Desert scenery uses source-placed bushes and fitted rocks.
- **Updated visual defaults.** Both games use the approved saved visual preset, consistent daylight and materials, smoother terrain transitions, paired stone normal and roughness data, Desert sandstone treatment and corrected Snow brightness.
- **Coastal depth and water movement.** Shoreline-aligned waves, pulsed swash, river flow and lake direction improve water presentation. Native bank contact and shader depth replace generated bank geometry.
- **Softer Desert decals.** Craters and scorch marks blend more naturally into the ground, with more consistent construction and ground marks.
- **Organized settings.** Title and pause settings use Game Controls, Gameplay and Audio groups. Dropdowns, navigation, music selection and backdrop fitting have been improved.
- **Separate voice volume.** A Voices slider joins the audio controls. Soldier and vehicle sounds, tracks, footsteps and temperate ambience follow the correct game events.
- **Game speed and camera.** The fastest Game Speed is twice Normal. Missions start at the wider shared camera distance in both games.
- **RA match options.** Configure ore and shroud regrowth, crates, capture the flag, short game and AI takeover. RA's New AI default applies to newly added computers without changing configured seats.
- **Clearer multiplayer settings.** Hosts choose AI levels, joiners see synchronized choices, and changing a setting clears READY. AI policy and state participate in compatibility and synchronization checks.
- **Optional media controls.** Downloads show progress, each game uses its own media, and leaving Movies or Music unchecked preserves content already installed.

### Performance

- Reuse terrain rays, prepared textures, animated poses and model lookups across rendering passes; retain RA ground in GPU buffers and skip irrelevant or off-screen work.
- Update nearby terrain and foliage after construction instead of rebuilding the whole map.
- Prepare required resources during loading and prioritize textures for visible buildings.
- Reduce the frame-rate drop during building placement animations while preserving the intended artwork.

### Platforms and builds

- **Windows and universal macOS packages.** Both platforms offer OpenCNC3D Installer, OpenCNC3D Complete with Dawn3D, RA3D Complete, OpenCNC3D Binaries and RA3D Binaries.
- **Empty launcher installer.** Install the launcher, then choose a game in the Module Browser with separate Movies and Music checkboxes.
- **Complete installers.** Start with the selected game already installed and choose optional Movies and Music during setup. RA3D Complete uses its own wizard graphics.
- **Separate RA3D downloads.** RA3D has its own Complete, Binaries and module packages, keeping Red Alert content out of ordinary Dawn3D packages.
- **Verified installation and updates.** Modules and optional content are verified before use. Updates choose platform-compatible assets and keep RA-only installations separate from the Dawn3D updater.
- **Bundled changelogs.** Each game module carries release notes, and newer local builds retain their bundled notes when the approved public release is older.

### Bugs fixed

- Fresh RA3D installations now start in Enhanced mode with the approved menu and terrain blending; an existing saved Classic choice is preserved.
- Fixed hard terrain joins, square water patches, low-bank coverage, black ford gaps, bridge water levels, river crossings, detached stones and Snow road contacts.
- Corrected Allied infantry colors and faction color mapping.
- Fixed independent RA HUD loading, rendered-pose picking, Classic tree anchors, damaged building slabs, placement jumps and Windows snapshots.
- Corrected RA Load Mission and User Maps routing, and campaign ending-movie and match debrief routes.
- Improved module and optional-media recovery while preserving saves and settings.
- Fixed the Windows Complete installer's large solid-compression data-stream failure while preserving extracted files and installation choices.
- Private development builds keep Launch available for installed games and prevent public updates from replacing the local runtime.

---

---

## OpenCNC3D v0.8.0 "From The Ground Up" (2026-10-10)

### New features

- Setup downloads the 1995 movies and music: the Windows installer and the launcher's first start offer both as ticked boxes, with a progress bar showing the size downloaded and the percent
- A gear button in the launcher adds or removes the movies and music at any time
- Three graphics modes under Visuals, from the main menu and the in-game Options: Classic (the original 1995 picture), N64 (the cartridge's own picture) and Enhanced (new art, models, grass and sound)
- The new Classic mode is the 1995 MS-DOS game's own flat 320x200 top-down picture, with its sprites, terrain, sidebar, building placement and build-up animations, effects, Nod colours and order flashes
- SPACE switches to the next graphics mode during play: a line of energy sweeps across the screen revealing the new mode, a whoosh follows it from left to right, and the mode's name appears across the top in the original C&C lettering
- Under Enhanced every structure is a new hand-painted remastered 3D model in its player's colour, with turning fans, dishes and radar, aiming turrets, a Tiberium Silo that fills up, flashing lights, steam and exhaust, and a Barracks flag that waves in the wind
- Under Enhanced all 21 vehicles and aircraft are new remastered 3D models in their player's colour: turrets turn, tracks and wheels roll as they drive, front wheels steer, rotors spin up into blur discs, ramps and doors open, gun barrels kick back when they fire, and the harvester docks at the refinery and works its arms
- Under Enhanced every infantry type is a smoothly shaded remastered 3D model in its own kit and colours: all seven soldier types, the ten townsfolk, Dr. Moebius, Dr. Chan and Agent Delphi
- The remastered soldiers stand, run leaning in at the low ready, aim and fire from the shoulder with a recoil that settles, lie down, crawl, fire prone, get up and die, shift their weight and look round while standing, play the idles, salutes and gestures the 1995 engine picks, and blend smoothly from one animation into the next
- Under Enhanced the trees of temperate and desert maps are new hand-painted models: spruces, firs, broadleaf trees and saplings in temperate, saguaro cacti and acacias in the desert, each lit, casting a shadow and swaying gently in the wind
- Under Enhanced Tiberium is drawn from new models laid out cell by cell the way the 1995 game lays it: dark earth mounds crowned with glowing yellow-green crystals that grow from a bare mound to a sprout to a full cluster, with soft streaks of light rising through them
- Under Enhanced the ground is the 1995 terrain repainted at 144 texels a cell in Temperate, Desert and Winter and on snow maps made in the editor, each tile blending into its neighbours across the join, the cliff pieces maps repeat side by side painted three ways, and the coast fading into the sea; it is the new Enhanced Textures choice under Advanced > Terrain
- Under Enhanced a new sidebar: a larger radar with Repair, Sell and Power buttons beside it, and Structures, Infantry, Vehicles and Aircraft tabs that list everything your side can build, with locked items dimmed and what they need shown on hover
- The Power button on the Enhanced sidebar switches one of your buildings off and on: a switched-off building stops making or using power and stops firing, producing and repairing, and carries an amber bolt until it is switched back on
- Under Enhanced a new structure builds itself part by part: its slab rises out of the ground in dust and broken earth, then each part comes up through it and unfolds, landing in a puff of dust with a jolt and a rumble; the Construction Yard unfolds out of the MCV and raises its robot-arm crane from the turntable, and a sold structure plays it backwards
- Placing a building under Enhanced shows a see-through ghost of it centred on the pointer, green where it fits and red where it does not, over a fading grid of glowing marks that shows where the base can grow
- Under Enhanced, outside online matches, a wall piece placed in line with one of your own walls of the same kind, up to five cells away, fills the gap for free; the cursor previews the whole line, which rises piece by piece in a puff of dust
- Remastered structures take visible damage: hits chip them on the side they come from, at half health pieces break away and fall, broken parts smoke, spark, burn or pour out water and Tiberium, a crane arm sags and a door jams, and the camera jolts as a building breaks
- A destroyed remastered structure blows apart into a couple of hundred pieces in its own explosion of flash, towering fireball, shock ring, wreckage trailing streaks of embers, smoke and fire, and the wreck sinks and fades over a scorch mark
- Under Enhanced the sandbag, chain-link and concrete walls are remastered models that wear down cell by cell as they are damaged, chip while they are shot and collapse in their own dust
- Under Enhanced every civilian building, the farm fields and haystacks, the oil pump, and the barbed wire and wooden fences are hand-painted remasters that stand on their plots as the 1995 art draws them and wear down, break and blow apart; the oil pump nods, and struggles once damaged
- Under Enhanced every explosion, hit, fire, flame and smoke is redrawn in new remastered art, the chain guns and cannons fire with a muzzle flash, and the landing-zone smoke is green signal smoke over a burning flare
- Under Enhanced the Hand of Nod is a sculpted statue: its forearm climbs twisting out of a pit in the rock and catches the globe as it drops, the camouflage net unfurls and the side door, emblem and main gate swing open
- Under Enhanced the Hand of Nod's gate opens for every soldier it trains, who steps out through it, and the Weapons Factory's door opens for every vehicle it builds, damaged or not
- A destroyed remastered vehicle breaks into charred, glowing wreckage; an aircraft shot down falls and breaks where it lands, and a wreck that ends on water sinks
- Under Enhanced fallen soldiers stay where they fell, darkened, up to 200 on a map, ending their fall as a ragdoll that vehicles and soldiers shove, with blood pooled beneath them
- Under Enhanced explosions tear soldiers apart and tracked vehicles crush them, blood sprays from the wounds onto the ground, vehicles and buildings, and tracks laid through it run red for a while
- Under Enhanced rifles and pistols flash at the muzzle and throw brass casings that bounce and fade
- Under Enhanced every cursor is a glowing remastered 3D model, a quarter smaller than the cartridge's and animating exactly as the cartridge's do
- Under Enhanced temperate and desert maps have ambient sound, with rivers and the sea where they are in view, crossfading as the camera zooms in and out and looping through a smooth crossfade with no gap; a new Ambience slider in Sound Controls sets its volume and the rain's
- Under Enhanced tanks, artillery, rocket launchers, APCs, harvesters and Humvees are heard as they drive, louder the closer the camera is
- Under Enhanced soldiers' footsteps are heard as they move: recorded steps that vary in pitch and volume from step to step, louder when the camera is close
- Under Enhanced a building you place lands with a heavy impact and rumbles as it rises, and damaged and collapsing buildings and walls and destroyed vehicles carry impact sounds of their own
- Under Enhanced the sidebar's buttons make a sound on hover and on click
- Under Enhanced every unit and structure being built or queued gets a small card beside the power meter, with its build clock and percentage and one card per type with a count, sliding out from under the sidebar and back
- Under Enhanced clumps of tall grass grow in wide patches on the open grass of temperate maps and sway in the wind, never on roads, bridges, water, cliffs, Tiberium or under buildings
- Vehicles flatten the grass clumps they drive over, which then fade away, and a structure going up lays the clumps around it flat
- Under Enhanced hand-painted stones in each theater's own colours lie at the foot of cliffs and rock faces: a vehicle driving into one shoves it and it rolls or tumbles to rest, and a structure going up throws the rocks near it outward
- Under Enhanced soft patches of soil, gravel and fallen leaves break up the open ground of temperate maps, and patches of soil and gravel matched to the sand break up the desert, so open fields no longer repeat one tile
- Under Enhanced the far part of the view, toward the top of the screen, fades into a pale haze on temperate and desert maps
- Under Enhanced mist drifts low across temperate and winter maps and sand dust blows across desert maps, lit by fires, explosions, muzzle flashes and Tiberium, and fading softly at the edge of the shroud
- Under Enhanced a glowing fog hangs over every Tiberium field, and the field's glow lights the whole field instead of a few spots
- Contact shadows under Enhanced, so units, trees and buildings sit on the ground instead of floating over it
- Under Enhanced a remastered tree that catches fire burns with its own flames, glow, smoke and embers, and is left as a charred snag that smokes until it goes out
- Under Enhanced the Blossom Trees are new models: a swollen golden bulb on a crooked trunk with dead branches, crowded with glowing pods and painted to suit each theater, rising out of glowing Tiberium cracks in the ground and squeezing out a spray of green spores on an eight-second loop
- Under Enhanced the desert rocks, both bonus crates and the crashed Orca, lying on its own scorched patch of ground, are hand-painted remasters
- Under Enhanced vehicles leave tracks on the ground behind them, laid along the path they actually drove, fading on a timer and thinning away a set distance back
- The thirteen Covert Operations missions, the nine multiplayer maps and the Special Ops bonus missions and test scenarios, which were flat, now have hills, plateaus, cliffs and ramps generated from their own tiles in the N64 and Enhanced pictures, with every multiplayer start on level ground
- Each graphics mode keeps its own Advanced settings, with a tab per mode to change any of them without switching to it
- The Advanced page opens with the settings every mode shares: fullscreen, windowed or borderless and resolution, plus new Vertical sync and FPS counter switches
- A new OpenCNC3D logo on the title screen behind the main menu
- The main menu states that the game is an unofficial fan project, not affiliated with or endorsed by Electronic Arts
- The launcher opens on a grid of four module slots, the first of them this game: a module is another game on this engine, started from the launcher
- Pressing an empty slot in the launcher opens the module browser, which lists nothing yet and says so
- The changelog moves behind a button of its own in the launcher, which swaps the launcher's panel and swaps back
- Tree clumps, which the cartridge never drew, are drawn as single trees on the cells they occupy, so nothing invisible refuses a building any more

### Improvements

- Without the music installed the jukebox lists nothing to play, and without the movies Intro & Sneak Peek is greyed out
- Enhanced's default picture is retuned: softer, finer ambient occlusion, a brighter, warmer and richer colour grade with more contrast, softer terrain shadows, thinner mist, a deeper sea and a wider, gentler glow on bright highlights, which roll off smoothly instead of clipping flat to white
- Under Enhanced the camera zooms in a quarter closer than the cartridge allows
- ESC is the pause key; SPACE no longer pauses
- The cartridge's picture, called Classic in earlier builds, is now N64 under Visuals, and its saved settings carry over
- Under Enhanced the pointer stays the same size as the camera zooms in and out, on the map and over the sidebar
- Under Enhanced building aprons are redrawn at higher resolution as packed grey-brown earth and gravel, keeping their ragged outline and blending softly into sand and grass alike
- Enhanced water is calmer, with a gentler ripple, a more natural colour and only a faint wake behind boats
- Under Enhanced the Database tab turns the 22 military buildings as repaired cartridge models, complete on every side, with rounder parts and sharper textures
- The map editor's AUTO HEIGHTMAP runs the new generator: it closes plateaus, settles and carves water, spreads ramps, puts buildings and starts on level ground and rolls the open ground, all in one undo
- Sound is mixed at 44.1 kHz instead of 22 kHz, so the new Enhanced sounds keep their full top end; the 1995 sounds, music and movie audio play unchanged
- The target flash is seen by the player who gave the order and their allies, not by the defender
- A building going up shakes only its owner's camera
- The launcher's headline is the two build numbers side by side, and the available one carries the version whenever it is known instead of a word
- The launcher's keyboard walks both rows: up and down move between the module slots and the buttons, and escape leaves the screen you are on

### Performance

- 3D Tiberium fields cost far less to draw
- Harvesters working a 3D Tiberium field no longer make the game stutter
- Sun shadows and water reflections cost less to draw under Enhanced, and buildings, vehicles and walls cost less in the N64 picture
- Switching graphics mode during a mission no longer stalls

### Platforms and builds

- The downloads no longer carry the movies and music; Setup fetches them from archived copies of the original discs
- Windows has a small web installer that downloads the game itself during setup
- Builds older than v0.8.0 cannot update themselves to it: download v0.8.0 and install it once; saved games come along
- The macOS package runs natively on Apple Silicon and Intel Macs in one download; Rosetta 2 is no longer needed
- The wire version is 12: a v0.8.0 game and an older one refuse each other by name in the lobby instead of parting on the first frame

### Bugs fixed

- Playing several missions in one sitting no longer uses more memory with each one
- Special Ops and campaign missions refused to start with a missing-file error after a User Map had been played
- Pressing CTRL+1..0 to assign a control group in an online match no longer ends the match for everyone with THE MACHINES STOPPED AGREEING
- Queuing several units in an online match no longer loses all but the first
- A building could vanish for a few seconds, shadow and all, when a vehicle elsewhere on the map was destroyed
- The User Maps list opened empty even when maps were installed, and could open correctly on one run and empty on the next
- The User Maps list showed each map's file code instead of its own name
- On User Maps with raised ground the pointer sat about a cell away from what it was over, so units on high ground could only be selected by aiming well above them
- Choosing the cartridge's picture under Visuals did not survive a restart: the game came back with the DOS terrain, the new sidebar and texture filtering from Enhanced
- The haystacks and the wheat, fallow, corn, celery and potato fields that 17 missions lay out as farmland were not drawn; they now stand on the map in N64 and Enhanced
- Tiberium no longer twitches across the whole field when a cell is harvested or spreads
- A flamethrower facing north no longer sprays straight up and into the ground
- The Obelisk of Light's beam is drawn when it fires, in every graphics mode
- The chat from the previous match no longer shows over a new one
- Seats left open when a match started are open again in the next room

---

## OpenCNC3D v0.7.3 "Room For More" (2026-09-21)

### New features

- The launcher opens on a grid of four module slots, the first of them OpenCNC3D: a module is another game on this engine, started from the launcher and playing across to the others
- Pressing an empty slot opens the module browser, which lists nothing yet and says so
- The changelog moves behind a button of its own, which swaps the launcher's panel and swaps back
- Vehicles leave tracks on the ground behind them under Enhanced Visuals, laid along the path they actually drove, fading on a timer and thinning away a set distance back

### Improvements

- The launcher's headline is the two build numbers side by side, and the available one now carries the version whenever it is known instead of a word
- The launcher's keyboard walks both rows: up and down move between the module slots and the buttons, and escape leaves the screen you are on

### Platforms and builds

- CI builds the macOS launcher on every push, which it never did before, so a launcher change is compiled on both platforms before a release is cut

---

---

## OpenCNC3D v0.7.2 "The Machines Agree" (2026-09-20)

### New features

- Tree clumps, which the cartridge never drew, are drawn as single trees on the cells they occupy, so nothing invisible refuses a building any more

### Improvements

- The target flash is seen by the player who gave the order and their allies, not by the defender
- A building going up shakes only its owner's camera
- On Windows the Remastered Collection is also looked for under a Steam installed on another drive

### Platforms and builds

- The wire version is 11: a v0.7.1 game and a v0.7.2 game refuse each other by name in the lobby instead of parting on the first frame
- When a match parts, the log lists every seat's world fingerprint for that frame (`NETDESYNC-SEATS`), so one log says which machine drifted
- The two loopback match tests assign a control group on the joiner
- Two new loopback match tests: selling and repairing in a match, and the build queue in a match
- A release dry run puts the developer build (with F5) back into the play folder when it finishes

### Bugs fixed

- Pressing CTRL+1..0 to assign a control group in an online match no longer ends the match for everyone with THE MACHINES STOPPED AGREEING
- Queuing several units in an online match no longer loses all but the first
- The chat from the previous match no longer shows over a new one
- Seats left open when a match started are open again in the next room
- Tiberium no longer twitches across the whole field when a cell is harvested or spreads
- A flamethrower facing north no longer sprays straight up and into the ground

---

---

## OpenCNC3D v0.7.1 "On The Level" (2026-09-18)

### New features

- Save Mission, Load Mission and Delete Mission use the 1995 slot dialog: sixteen named slots, newest first, a typed description, and a Delete that asks first
- Load Mission on the main menu loads any slot, booting the mission it was saved in; a load from the pause dialog can do the same
- An empty Load Mission dialog says there are no saved games
- A save made during the campaign remembers where the campaign had got to
- Restate in the pause dialog shows the mission objective, with a Video button that replays the mission briefing movie
- The credits readout ticks as it counts, up and down, with a Gameplay switch to silence it

### Improvements

- The Special Ops list is filed by faction and then by source: GDI and Nod each have a Covert Operations section and a Special Ops section, with the test scenarios last
- The Test Map is the last row of the Special Ops list instead of a main menu button of its own
- HOME and END jump to the first and last mission of the Special Ops list
- Under Enhanced the tank shell flies laid along its direction of travel; Classic keeps the console's upright shell, and the grenade stays upright in both
- Under Enhanced the Rocket Launcher turns to face where its rockets go; under Classic it keeps the console's fixed pod
- The repair wrench floats above a building's own roof rather than at half its footprint, so on the Refinery, Weapons Factory, Hand of Nod and Temple it no longer sits inside the structure
- Structure idle animations (the Barracks flag, the Communications Center dishes, the SAM site, the Airstrip's scanner, the nuclear dome) follow the cartridge's own curve evaluator between keyframes
- A helipad no longer takes a rally point, since its aircraft never followed one
- A mission that cannot start says why on screen instead of returning to the menu in silence

### Platforms and builds

- Nineteen more tests in the build's gate suite that could pass on a broken build now fail on one
- The suite's two-run reproducibility test no longer fails on a freshly built test folder
- A room opened from the command line without a name takes the game's own default room name
- Nine new tests in the build's gate suite cover the pad, wreck colours, edge tiberium, Remastered attack poses, the wrench, the tank shell, the Rocket Launcher, the texture books and the animation curves
- Five more cover the factory exit and the airstrip rally point, the screen edges over the unit card, the Resolution list, the credits tick and a refused mission start
- Both Windows executables are linked large-address-aware and declare the UTF-8 code page
- The Windows READ-ME names the BOOTFAIL and FATAL lines to look for in the log

### Bugs fixed

- A building placed beside a taller neighbour no longer sinks into the ground; buildings whose footprints touch stand on one platform, and a neighbour's apron row no longer lifts or cuts a building
- A transport helicopter docked on a helipad directly below a taller building no longer loses its nose in the slope
- A destroyed vehicle or aircraft comes apart in its seat's team colour instead of snapping back to its faction's default
- Tiberium crystals no longer show in the map's always-shrouded edge ring or in any unexplored cell (Enhanced)
- Remastered infantry are drawn whole when they fire: a standing flamethrower keeps his legs, and a prone flamethrower, minigunner, bazooka man or commando is no longer cut off at the ground
- In the Enhanced picture the repair wrench is lit as a wrench instead of taking the shading and outline of the roof behind it
- Tiberium silos show their fill level; the fill dome is drawn on the silo instead of hidden inside it
- The Tiberium Refinery's storage strip stands on the building and fills there; the second strip beside it is gone
- The Advanced Communications Center's dish pans in a level circle in the direction the cartridge authored, instead of tumbling
- A soldier leaving a Barracks or Hand of Nod with a rally point set walks out through the door before turning for the rally, instead of through the side of the hut
- A vehicle delivered by the cargo plane drives to the Airstrip's rally point; a harvester still goes to the tiberium field
- The bottom-left corner and the screen edges over the unit card scroll the map with units selected
- The Resolution list carries every size the display offers, the desktop first as Desktop, and scrolls; sizes larger than the desktop are greyed under Windowed and pickable under True fullscreen, and a windowed size is clamped to the desktop
- A saved resolution is read back exactly instead of rounded to a multiple of 8
- A mission whose theater archive is missing or damaged no longer closes the game
- A damaged data archive whose header cannot be read is refused instead of searched

---

---

## OpenCNC3D v0.6.12 "Step Aside" (2026-09-14)

### New features

- The game list is ten rows tall, sorts by any column header, scrolls with a bar and can hide locked or greyed games
- A PING column shows each internet game's round trip; a direct game is measured only once it is selected
- The game list's sort, HIDE boxes and the player's name are remembered between launches

### Platforms and builds

- The Windows full package lists CHANGELOG.txt, opencnc3d-install.txt, BUILD-ID.txt and SDL2.dll last
- The Windows binary-only zip no longer wraps its files in a folder
- A release stops when the Windows SDL2.dll is not the pinned one
- The Windows launcher declares UTF-8 as its code page
- Online play needs every player on v0.6.12: an older build is refused by version when it tries to join
- Fifteen tests in the build's gate suite that could pass on a broken build now fail on one

### Bugs fixed

- Updating on Windows replaces a file the launcher has loaded, such as SDL2.dll, by renaming instead of writing over it
- An update that fails or is interrupted part way is undone from a journal in the game folder
- An update stops and names the file when a file cannot be moved aside
- Every file an update writes is checked against the zip's own CRC-32 and size
- A launcher locks the game folder while it recovers or updates it
- The launcher reads whether an update zip is wrapped in a folder instead of assuming it
- A changed file is written beside itself and renamed into place once checked
- The launcher finishes an interrupted update before it reads the installed version
- The Windows installer sets aside an interrupted update's journal before it installs
- On macOS the app starts the launcher, not the game, while an update is unfinished
- The launcher changes files only in a folder holding opencnc3d-install.txt, and never removes a file there by pattern
- A player leaving an online match no longer desyncs it when one machine hears the goodbye a turn later than another
- A machine that missed a departed player's last orders gets them from the host instead of playing on without them
- A building that would cover a cell off the edge of the map is refused with CANNOT DEPLOY HERE instead of vanishing into its factory

---

---

## OpenCNC3D v0.6.11 "Out Of The Box" (2026-09-13)

### Improvements

- A new player starts at game speed 4 instead of 3, in campaign, skirmish and online alike

### Bugs fixed

- Reset to defaults on Visuals > Advanced puts every setting back, not the twenty six rows it knew about

---

---

## OpenCNC3D v0.6.10 "Bring Your Own Tank" (2026-09-09)

### New features

- An artist's FBX model can replace a cartridge vehicle, tracks and all
- Water Shader: the Enhanced sea knows its coastline, flows, reflects and takes a boat's wake
- Solid tiberium: crystals stand on the field under Enhanced instead of a flat decal
- Rain under Enhanced: streaks in the air, wet ground, hulls and men, drops on the water; tuning panel only for now, off by default
- The outermost ring of cells on every map stays shrouded, whatever the shroud is set to
- A 3D Trees row on Visuals > Advanced, on under Enhanced, beside the Water Shader
- An MCV that cannot deploy where it stands says CANNOT DEPLOY HERE under OPTIONS
- Sixteen of the eighteen tree names draw their own species under Enhanced, from an imported game-ready pack, with a shader, baked canopy occlusion, wind and burning
- Desert maps get desert trees: the four tree names used only on desert maps are mapped to acacias
- Real grass grows on temperate and winter ground under Enhanced, in the trees' own wind; tuning panel only for now, off by default
- Grass takes its colour from the ground it grows out of, thins with distance, and is cleared from under buildings and hulls
- A Grass section in the F5 panel, thirty four dials, saved and reloaded with the rest of the preset
- Grass switches itself off with the cartridge terrain art, which it is not tuned against, and comes back with the 1995 art
- Grass bends away from the mouse pointer as it moves over the field
- The ground takes a small jolt when a building is put down
- A Debug row at the bottom of the F5 panel draws updated meshes from a debug pack
- Type a host's address on the INTERNET tab and join a game across the internet
- The host is shown the address to read out, and the port to forward
- `netcheck relay <host>` says whether a CnCNet tunnel is reachable from here
- Tick INTERNET GAME to host through a relay: others join with a room code, nobody forwards a port
- The waiting room shows the host the room code to read out

### Improvements

- Trees sway on the rig their artist authored, instead of leaning together as one
- Trees are lit from their real surface direction rather than a guessed one
- The F5 panel's rows stop drawing over its own title and buttons when the list is scrolled

### Performance

- Trees are only drawn where the camera can see them, in the lit pass and in the water reflection

### Fixes

- Ambient occlusion does something: its depth test compared against a threshold no scene could cross, so the feature changed nothing at any setting
- Grass covers the men standing in it instead of being drawn behind them
- A vehicle driving over grass leaves wheel tracks instead of clearing a strip its own width
- Windows and Mac players can join each other's games again
- A game built against a different engine is refused by name, not by accident
- Two builds that could not read their own engine no longer match each other
- A joiner without the host's map is told so, instead of playing a different one
- A joiner whose copy of the map differs is refused before the match starts
- Changing the map no longer leaves the room refusing against the old one
- Changing the map or the rules unlights every READY, so nobody is committed unseen
- A refused joiner leaves its seat instead of a phantom the host waits on
- A READY the network loses is said again, instead of leaving a room that can never start
- One order given to a large group no longer stops the match dead
- A player who leaves goes quiet on the same turn in every world, so the armies do not part
- A room stops advertising itself the moment START is pressed, not the moment the match begins
- A game that has started comes off the internet list at once instead of after forty five seconds
- A build too old for the room is refused by name instead of joining and hanging
- One peer can no longer make the host repeat a kilobyte to everyone else on demand
- Only the seat itself can surrender it or leave it
- A match that breaks ends on the debrief with the reason on it, not on the desktop
- A host that quits ends the match at once instead of after thirty seconds of silence
- The browser can finally grey a game you would be refused from
- Health bars only while a unit is selected, which is the engine's own default
- The 3D pointer tracks the mouse instead of snapping to whatever it crosses
- Refused and move markers sit on the ground instead of floating over hills
- Backspace works in the YOUR NAME prompt
- Switching to the INTERNET tab no longer leaves JOIN pointing at a hidden LAN game
- A field stops hiding what you just typed once it passes 32 characters
- One refused send no longer condemns every later join with a local-network message
- Shadows no longer strobe when you zoom in: one frame in four was drawn with no depth test
- Cut-out objects drawn after a tree keep their alpha test, instead of turning solid
- Desert maps grow cacti again instead of random temperate trees
- The menu music keeps playing when the multiplayer screen is opened
- Leaving a mission gives back the sidebar and decal art it loaded, instead of two megabytes a mission
- A click near an enemy standing in unexplored shroud is no longer re-aimed onto it

---

---

## OpenCNC3D v0.6.9 "Ask First" (2026-09-07)

### New features

- A match ends on its own screen, one commander at a time, with a Continue button
- The debrief shows the emblem and plays the theme of the side you played
- Surrender and stay to watch the rest of the match
- AI Takeover: a player who leaves is played by the computer, or their base is destroyed
- Players can talk to each other during a match, and EVA talks only to your side
- Players have names, and a private room asks for a passcode
- A joiner picks its own side, colour, team and start position
- The room gains AI Takeover and Short Game, and the browser lists maps by name
- Windows Desktop shortcuts for the newest build from main and for the source tree
- The outermost ring of every map is always shrouded, whatever the shroud setting
- The water reflects the world, bends the sea floor beneath it and catches the sun (Enhanced)
- Boats push the water and leave a wake in it, on a wave simulation that bounces off the shore (Enhanced)
- The water is a greyer, more natural blue (Enhanced)
- Water Shader, a switch on the Visuals page under Enhanced: the sea knows where its coast is: the beach fades under the water, a pale band of water marks every shore and riverbank, the water line creeps up the sand, the sea pans on its current and rivers run downstream, shallows and deep water (Enhanced)

### Improvements

- The room measures its slowest link and stamps orders far enough ahead for it
- Surrendering asks Yes or No instead of offering a Restart a match cannot do
- A surrendered commander loses the cameos, Repair and Sell, and keeps the radar
- A surrendered commander reads as SPECTATOR in the player list
- The map is revealed to a commander who is out of the match
- A joiner's rows read EMPTY until the host's room arrives
- A failed network send is written to the log
- The model gallery plays its animations at the speed the game plays them
- The unit card's frame, cameo and group tabs are cut to the sidebar's chamfered corners
- Each control group tab on the unit card is its own framed button
- The unit card's health bar is a row of raised blocks

### Platforms and builds

- The macOS build asks for the Local Network permission where the player can see it
- Peers can talk through a CnCNet relay, which nothing uses yet

### Bugs fixed

- Both players were told they had won when a match ended
- A joiner could not change faction, and the side buttons showed the host's side
- Opening the pause dialog in a match ended the match for everyone a minute later
- Double-clicking the MCV selected every unit of its type instead of deploying it
- A match where every player had resigned never ended
- One player leaving ended the match for everybody
- A match froze on its fourth tick when the room was not settled at START
- A Mac started from the Finder never reached a LAN host
- The launcher quit on the first mouse movement, and carried no SDL of its own
- The engine patch no longer matched the engine, so no build would start
- A failed join did not give its socket back, and the sixth attempt was refused
- The question when leaving a match ran outside its box
- Choosing CLASSIC destroyed the New HUD setting instead of overriding it
- A filled build slot showed a square frame with a dark triangle in each corner

---

---

## OpenCNC3D v0.6.8 "Another Angle" (2026-09-06)

### New features

- Perspective row on the Advanced page: Classic, or Isometric at 45 degrees
- Picking, edge push, drag band, infantry facings and paint order all follow the camera
- Four isometric dials under F5: yaw, tilt, field of view and distance
- The ground is lit from its own normal, with a soft penumbra and a single sun
- Six dials for the new ground lighting, each with an off that draws the old picture

### Improvements

- Infantry are a quarter smaller under Enhanced, all three art sets
- The 3D cursors keep their Classic heading under any yaw, and draw at 0.7 size
- Cliffs and slopes no longer show cell-shaped shadow blocks
- A joiner that gets no answer for ten seconds is told so, with the address it tried
- The room is not drawn until the host's copy of it arrives
- The host's log names what it refused and what it did not understand
- A joiner accepts the host's answer from any of the host's addresses, and says which
- The Mac writes the same log Windows does when started from Finder

### Bugs fixed

- Resolution, UI scaling and Perspective drew their labels under their drop lists
- The lobby refused two installs whose first map differed, rather than the hosted one

---

---

## OpenCNC3D v0.6.7 "One World" (2026-09-05)

### New features

- HOST opens the room and goes straight to the multiplayer game screen
- A hosted game is as big as its map: one seat per start, re-sized when the map changes
- Every seat is a menu: BOT, EMPTY or BLOCK, and a BOT seat is how a computer joins
- Chat in the lobby, each player's lines in their own colour
- The room reports its own events in the chat pane: who joined, left, readied or is waited on
- Start positions are numbered on the map preview and picked from each player's drop down
- A player who picks no start is dealt one by the engine, the same on every machine
- The map list is a window with OFFICIAL and USER MAPS tabs, opened by CHANGE MAP
- True fullscreen, Windowed and Windowed borderless on the Advanced page
- Resolution picks from the sizes the display offers
- UI scaling, with -2x as the new default
- Reset to defaults on the Advanced page

### Improvements

- The lobby was redrawn: rule boxes, sliders in one row, and a six line chat pane
- The AI Players slider is gone; a skirmish seats one computer per start
- The lobby's wording is shorter, with no explanatory sentences under the controls
- F5, the tuning panel, is compiled out of published builds
- Cheats will not open in a network game, and any switched on beforehand are cleared
- The colour grade is brighter and punchier, and cloud shadows are stronger and softer

### Bugs fixed

- A LAN match was two separate games: the lobby never switched the engine into network mode
- A joiner played the host's faction whatever seat it was in
- The tech level did not travel, so the two sides had different build lists
- A player whose connection died froze everyone else silently and for ever
- Clicking MULTIPLAYER left the menu on screen while the browser ran behind it
- Both installers shipped one person's personal map folder
- Double-clicking a map in the list threw the host back to the Host/Join screen

---

---

## OpenCNC3D v0.6.6 "In Step" (2026-09-05)

### New features

- MULTIPLAYER is live on the main menu: host a game or join one on your own network
- Up to eight people in one match, with only the host's machine needing to be reachable
- Games announce themselves on the network and are listed with name, map and player count
- The lobby is the Skirmish screen: the host sets the rules, each player picks side, team and colour
- Nobody starts until everyone has pressed READY
- Games can be given a name and a four digit passcode
- The host can remove a player who never readies, and that player is told so
- The host prints the address to join it at

### Improvements

- A player whose map differs from the host's is refused by name before the match starts
- A desync report names the order wire, the agreed map, and which player disagreed
- Tests no longer open windows or play music

### Bugs fixed

- Switching the ground to the 1995 tiles did nothing, because no pack carried the tiles
- The camera readout covered the OPTIONS and DATABASE tabs
- Remastered soldiers hopped into the air when they attacked
- The chemical warrior sprayed a rifle flash instead of a spray
- Selling a building put the players in a network match out of step
- Repairing a building, and a commando planting explosives, did the same
- Two players were dealt different armies depending on what each had played before
- Players disagreed about what was discovered once a computer's harvester delivered
- Build Anywhere did nothing in the Enhanced engine
- One player leaving a match ended it for everyone else

---

---

## OpenCNC3D v0.6.5 "Second Look" (2026-09-01)

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
- Classic mode no longer turns Swapped Mouse Buttons off, by either route, and no longer writes the shipped defaults over your saved settings
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

### The unit card

- A card in the bottom-left corner of the Enhanced HUD shows what you have selected: the unit's cameo, its name, health as a segmented bar with the numbers beside it, and its weapon's damage.
- The rest of a multiple selection appears as a row of smaller cameos under it. Click one and that unit becomes your whole selection.
- Ten tabs under the card are the control groups, keyed 1 to 9 then 0 as on the keyboard, each showing how many units the group holds. Click a tab to select that group; the tab lights amber while the selection is that group.
- Clicking the big cameo centres the view on the unit.
- The card is cut from the sidebar's own chrome and lettered in its own font, so it is the same panel wearing another shape. Enhanced mode with the new HUD only; Classic is untouched.

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

## OpenCNC3D v0.6.4 "Line Of Sight" (2026-08-31)

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

## OpenCNC3D v0.6.3 "Blast Radius" (2026-08-24)

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
- The launcher watches the Builds section of opencnc3d.com. When a newer build is up, Play becomes Update.
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
- OpenCNC3D.exe is the launcher now and opencnc3d-game.exe beside it is the game.

### Bugs fixed

- A superweapon aimed at ground you had not explored spent its charge and nothing happened.
- Tiberium blurred when bilinear filtering was on.
- Reset to Defaults on the cheat menu could not turn Unlock Superweapons back off.
- Winning a skirmish and starting a new match resumed the finished one.
- The launcher left a copy of the mouse pointer behind everywhere it had been.
- The GDI score screen painted its own gold medal into the corner the turning emblem occupies, so the two overlapped. GDI now uses the same background plate as Nod, which never drew one -- it keeps its own music and its own emblem, but it no longer shows the four casualty bars that plate carried.

---

---

## OpenCNC3D v0.6.2 "Now You See It" (2026-08-24)

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

## OpenCNC3D v0.6.1 "Skirmish" (2026-08-23)

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

## OpenCNC3D v0.6.0 "Clear Skies" (2026-08-22)

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
- Windows now writes opencnc3d-log.txt, which the READ-ME has always asked players to send

### Bugs fixed

- The Airstrike fired but nothing ever appeared
- An Orca built on a helipad was invisible
- A build could ship a renderer and an engine that disagreed, with every test still green
- Pressing Cancel in the Special Ops screen quit the whole game
- The minimap was visible before you had built a Communications Center
- The power meter read full whenever output met demand, whatever the numbers were
- The second East territory on the GDI map led to the same mission as the first

---

