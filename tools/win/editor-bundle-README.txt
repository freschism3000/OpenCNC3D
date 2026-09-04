CNC3D MISSION EDITOR -- test build
==================================
build v0.6.2-346-g03d2a79, 27 Aug 2026, Windows (32-bit)

This is a standalone copy of the map editor. Nothing needs installing and it
touches nothing outside its own folder. Delete the folder to uninstall.


READ THIS FIRST: NOBODY HAS EVER RUN THIS BUILD
-----------------------------------------------
It was cross compiled on a Mac. The machine that built it has no Windows and no
Wine, so this .exe has not been started even once, by anyone. You are the first.

What HAS been checked is everything that can be checked without running it:

  * It compiles and links clean for i686-w64-mingw32, and the fix that this
    build exists for -- water tiles in the TERRAIN drawer showing as empty
    boxes -- is confirmed present in the binary that shipped, by symbol.

  * Every DLL it asks the loader for was read out of the import tables of all
    three binaries. Three files travel together in this folder: cnc_eyes.exe,
    SDL2.dll and TiberianDawn.dll. Everything else they import is a Windows
    system DLL. The GCC runtime, the C++ runtime and winpthread are linked in
    statically, so there is no libstdc++-6.dll, libgcc_s_*.dll or
    libwinpthread-1.dll for you to be missing.

That is a static audit and it is not a test. It says the file should load; it
says nothing about whether a window opens or anything draws. If it does not
start at all, that is useful information and not your fault. Send Editor.log
and say what you saw, including the exact text of any error box.


1. WHAT YOU NEED
----------------
  * Windows 10 or 11. The build is 32-bit on purpose and runs on 64-bit
    Windows with nothing extra installed.
  * A GPU with OpenGL 2.1, which means anything from the last decade. If you
    are on a virtual machine or a remote desktop, OpenGL is often the thing
    that is missing.
  * On Windows 8.1 or 7 you would also need Microsoft's Universal C Runtime
    update, KB2999226, because the binaries use the api-ms-win-crt runtime
    that Windows 10 and 11 carry as standard. Untested there, like everything
    else here.


2. GETTING PAST WINDOWS' WARNING
--------------------------------
You will see a blue "Windows protected your PC" box, or files will simply
refuse to run. Nothing is wrong with the download. That is Microsoft Defender
SmartScreen, and it appears because this build is not code signed. An unsigned
program keeps getting it until enough people have downloaded that exact file
for Microsoft to have formed an opinion of it, which for a test build never
happens.

The quickest way past it, and the one that clears every file at once, is to
UNBLOCK THE ZIP BEFORE EXTRACTING IT:

    Right-click the .zip  ->  Properties  ->  tick "Unblock"  ->  OK

then extract. If you have already extracted it, the same tick on Editor.bat
and on cnc_eyes.exe does the job one file at a time. If the blue box still
appears, click "More info" and then "Run anyway".

Extract the folder somewhere you can write to, such as your Desktop or
Documents. The editor saves your maps inside its own folder, so a read-only
location will stop it.


3. RUNNING IT
-------------
Double-click  Editor.bat.

It opens the editor on SCG01EA, the temperate map. Everything the editor
prints goes to Editor.log in this folder; that is the file to send back if
something breaks. If the editor exits badly the window stays open, shows the
end of the log and waits for a key, so nothing flashes past.

Five maps are included, one per terrain theater, plus a worked example:

    SCG01EA   Temperate  (green)
    SCB01EA   Desert
    SCW01EA   Winter     (Tiberian Dawn's snow)
    SCS01EA   Snow       (Red Alert's, fully snowy ground)
    SCA01EA   Sand       (Arrakis, from Dune 2000)

    USER90    "Worked example: rules, teams and Enhanced". It exists to show
              what the SCRIPT tab can do.

Editor.bat deliberately has no map picker. SWITCHING MAPS HAPPENS INSIDE THE
EDITOR: the MAP menu has OPEN MAP and NEW MAP, and that is where the other
four theaters and the worked example are.


4. THE EDITOR
-------------
Five tabs across the top: OBJECTS - TERRAIN - ELEVATN - STARTS - SCRIPT.
Keys 1-5 switch tabs. Key 0 frames the whole map.

  OBJECTS   Five palettes: BUILDING, UNIT, INFANTRY, TERRAIN, WALL. Pick an
            owner, click a palette entry to arm it, click the map to place.
            Footprint and legality are checked before a stamp lands.
            Tools: V place, M move, I pick, X erase.

  TERRAIN   The theater's own tile drawers: GROUND, WATER, SHORE, RIVER,
            ROAD, ROCK, BRIDGE. Click to arm, then click or DRAG to paint.
            One drag is one undo step.

  ELEVATN   The tier ladder (LOW / GROUND / +1 / +2 / +3), a brush size, and
            RAMP. Cliff edges and corners are chosen for you as you paint.
            If automatic art gets something wrong, the CLIFF PIECES drawer at
            the bottom lets you paint any single cliff tile by hand.

  STARTS    A MULTIPLAYER map gets eight player starts. A SINGLEPLAYER map
            gets HOME, REINFORCE and eight mission cells instead.

  SCRIPT    Visual scripting in three views -- RULES, TEAMS and ENHANCED --
            with pickers instead of typed codes, plus CHECK MISSION.

  PLAY      Runs the mission for real, inside the same viewport, with the
            editor chrome unchanged around it. ESC comes back to the editor.
            Only Exit Game or closing the window quits.

  SAVE      Writes the map. SAVE AS... takes a name of up to 40 characters
            and files it in the next free USERnn slot.

NEW MAP offers five preset sizes in any of the five theaters, plus a size you
type yourself:

    SMALL   32 x 32     MEDIUM  44 x 44     LARGE  54 x 54
    HUGE    62 x 62     120 x 120 (BIG MAP)     CUSTOM  W x H

Every one of those numbers is the PLAYABLE rectangle. The first four are
rectangles inside the classic 64 x 64 world, the size the original game used.
120 x 120 sits inside the 128 x 128 big-map format and is the one that supports
up to 8 player starts. CUSTOM takes any rectangle from 16 x 16 up to 126 x 126,
which is the widest the big-map format allows, and anything over 62 a side is
saved as a big map too.

Your maps are written to  missions\user_maps\  in this folder. There is no
separate bake or export step: a map you make borrows the theater's tile bank
and is playable the moment it is saved.


5. WHAT IS NOT IN THIS BUILD
----------------------------
Deliberately left out to keep the download small; none of it is broken:

  * The game itself -- the campaign, the menus, the skirmish lobby. PLAY
    inside the editor is the only way to run a mission here.
  * The other 100+ campaign maps and their terrain packs.
  * Movies and music.


6. KNOWN ROUGH EDGES
--------------------
  * Nobody has run this build on Windows. Anything in section 4 could turn
    out to behave differently here than it does on the Mac build it was
    compiled from. Treat the whole of it as a claim to be checked.
  * SAND (Dune 2000) has no dedicated west-facing cliff variants yet, so a
    west cliff face there reuses another angle and can look wrong.
  * A map that has never been played has no radar minimap colours yet.
  * On a map with no elevation, RAMP has nothing to attach to and does
    nothing. Raise ground first.
  * There is no sound. The editor is silent on purpose, and this build ships
    without the game's audio data, so PLAY is silent too. Not a bug.


7. REPORTING A PROBLEM
----------------------
Send Editor.log from this folder, say which map and which tab you were in,
and a screenshot if it is something visible. The log has the build id at the
top of every run.

If the editor never got far enough to write a log, say that instead, and copy
out whatever Windows put on screen. On a missing-DLL failure Windows names the
file it could not find, and that name is the whole answer.
