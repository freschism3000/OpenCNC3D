# The launcher

The thing a player double-clicks. One 320x200 dialog, drawn with the game's own 1995
DOS primitives over the real `TITLE.CPS` plate:

- which build is installed and which build is available, side by side, as the headline
- a well showing one screen at a time: the module grid, the changelog, the module
  browser, or one of the three movies and music screens
- **Play**, which becomes **Update** when opencnc3d.com has a newer build
- **Changelog**, which swaps the well for that build's notes and back again
- **Editor**, which opens the map editor
- **Quit**
- the **gear** at the right hand end of the module grid's heading row, which opens
  **Settings** for the movies and music

## Movies and music

**The packages carry neither.** Both are downloaded from disc images on archive.org,
each file by HTTP byte range straight out of the image. The 1995 cutscenes (75 VQA files,
428.5 MB) come from the two Command & Conquer Gold discs, GDI and Nod, which EA released
as freeware in 2007. The soundtrack (`SCORES.MIX` and `transit/TRANSIT.MIX`, 79.6 MB)
comes from the Covert Operations disc, because the Gold discs carry only 21 of the 37
themes and no menu theme; that disc has one mirror. `lmedia.c` does the fetching,
checking, removing and recording; `launcher.c` is only the screens. **Which files are on the disk
is the only switch**: the game never reads the choice, it plays what it finds, and with
none of it the game starts silent and skips every movie.

**Setup** is the install wizard on both platforms. It opens by itself when the folder is
an install (`opencnc3d-install.txt` is there), no `opencnc3d-media.txt` is there yet, and
the launcher was started with no `--view` of its own. On a Mac that is the first double
click after unzipping; on Windows it is the first start of a zip install, while the web
installer asks the same two questions itself and records the answer, so its players go
straight to the module grid.

- two ticked rows, **DOWNLOAD MOVIES 428.5 MB** and **DOWNLOAD MUSIC 79.6 MB**. A
  component already on the disk reads **INSTALLED**
- a space line: what the ticks need, and what is free on the install's disk (in white
  when it does not fit)
- **Install**, which reads **Continue** with both boxes unticked, and **Quit**. Quit
  records nothing, so Setup opens again next time

**Downloading** is one screen for both: the file being fetched (`MOVIES: GDI1.VQA (12 OF
75)`), a gauge, and **the size so far, the total and the percent** under it (`312.4 MB
OF 508.1 MB 61%`), with **Cancel**. A failure keeps the bar where it stopped, prints the
reason in white, and offers **Retry**, **Play without** (start the game with what is
there) and **Quit**; a removal that fails offers **Back** in place of Play without, since
its files are still there. When the job finishes the launcher returns to the module grid, and
the status line says what happened (`MOVIES AND MUSIC INSTALLED.`) until the next press.

**Settings** (the gear) opens with each box as the player left it: ticked as the record
chose it while the record says that component is still `pending`, and otherwise ticked
when **any** of its files is on the disk, a half-finished download or an older package's
shorter score included. So Apply pressed for one box never removes the other behind the
player's back. Ticking a missing component downloads it through the same screen;
unticking one with any of its files on the disk asks first, naming it and what it frees
(`REMOVE THE MOVIES, 428.5 MB?`, **Remove** or **Keep**), and then deletes exactly the
files the manifest names. **Apply** is grey until it has something to do: a box ticked
and not all there, a box unticked with something there, or a pending record to finish.

**A record left pending** by an earlier run (a download stopped, failed or killed, a
removal refused) is said once on the module grid when the launcher starts, with where to
finish it: `THE MUSIC DID NOT FINISH DOWNLOADING. THE GEAR FINISHES IT.`

**Install** is the first screen in a folder that holds the launcher and not the game: no
`opencnc3d-install.txt` and no game executable, which is what a Windows Setup that was
stopped leaves in a folder that already held files. The same two ticked boxes, and
**Install**, which runs the `--install-game` job from the window: the newest build from
opencnc3d.com with its package resumed from what an earlier attempt kept, then the ticked
movies and music, on the same downloading screen with one bar for the whole of it. When
the folder has not even the menu art to draw that screen, the launcher first asks in a
plain system dialog, and on a yes fetches `dosmenu.pack` alone out of the newest build's
binary-only package (`--fetch-menu-art` does the same headless). The development play
folder and an install too old to have a record both have the game, so neither is
offered this.

While a media job runs, Play, Update and Editor are disabled, and the gear is disabled
while an update downloads or installs, so the two workers never write the folder at the
same time. Escape does nothing on the downloading screen: one stray key must not throw a
long download away, and Cancel is a button.

The record, `opencnc3d-media.txt`, is written only by `lmedia.c`, and no update may carry,
overwrite or delete it.

### Picturing the screens

Every state is reachable with `--shot`, headless:

```
--view setup                                     Setup, both ticked
--view setup --media-shot-choice none            Setup, both unticked (Continue)
--view setup-progress --media-shot-progress 325574000/532737906
                                                 Downloading at 61%
--view setup-progress --media-shot-progress 159821000/532737906 \
    --media-shot-failed "<reason>"               the failure screen
--view settings                                  Settings, ticks as the player left them
--view settings --media-shot-choice music --media-shot-confirm
                                                 the removal question
--view settings --media-shot-tick movies         Settings after one press on a box
--dir <launcher-only folder>                     Install
```

The `--media-shot-*` options are read by `--shot` and by nothing else; a window asked for
`--view setup-progress` opens on Setup instead, because that screen has nothing to show
without a job. Beside the `LBTN|` line per row button, a shot prints an `LCHK|` line per
checkbox on the screen (`label`, `checked`, the rectangle, `disabled`) and an `LGEAR|`
line for the gear, all in the picture's own pixels, and every `LBTN|` line now says
whether that slot is `hidden` on the screen pictured. `LSTATUS|text=` is the status line
as drawn, and on Setup, Settings and Install `LAPPLY|movies|music|asks|game|enabled`
says what the first button would hand to lmedia, and whether the removal question
would come first. An `LMOD|` line per module tile gives its `name`, whether it is
`installed` and `selected`, its rectangle, and `drawn=1` when the picture is the module
grid; for Red Alert's slot, `missing=` says why it is empty.

## The modules

A module is another game built on this engine, started from the shared OpenCNC3D
launcher. The Tiberian Dawn entry is **Dawn3D**; Red Alert is **RA3D**. The card,
browser, install/details, Settings and removal views share each module display
name. OpenCNC3D keeps its platform branding, accepted artwork, package product
names and filenames. Technical module IDs and save/settings locations are unchanged.

The filled tile carries the C&C emblem, scaled out of the title plate the launcher is
already standing on rather than baked into the pack as a second copy, so an install made
before the grid existed still draws it. An empty tile is drawn in the engine's own
disabled colourway with a plus and the word "Coming soon", and pressing one opens the
module browser.

**Red Alert 3D is the second tile when its module is in the folder,** under `ra3d/`, and
`--module ra3d` starts it. What makes it installed is a fixed list in `lmodule.c`
(`lmod_ra_needs`): its game, its brain, its menu pack, its first map, the sound archives
and the mission files, which are all things a package carries. Its movies and its score
are not on that list and must never be: no package carries them, and the game plays
without them. A slot without its module is drawn empty, and `--module ra3d --play`
refuses, naming the first file missing. The movies and music ask the same test before
they fetch Red Alert's own movies and score, so they are downloaded exactly where the
tile is live. `tools/launcher/module-check.sh` (gate G337) builds a folder
shaped like a package from the play folder and proves the tile is live there and the game
starts.

**The browser lists nothing, and will until there is a module to fetch.** There is no
module format, no download and no cross play handshake yet. The browser is a real screen
with a real empty list rather than a button that does nothing, because the day a module
can be fetched the only change is what the list reads from.

The keyboard walks both rows: Up and Down move between the tiles and the buttons, Left and
Right walk the row you are in, Return presses, and Escape leaves the screen you are on and
quits only from the grid.

```
launcher/build.sh                     macOS  -> launcher/opencnc3d-launcher
tools/win/build-launcher-win.sh       Windows -> build/win/OpenCNC3D.exe
tools/win/make-installer-win.sh       Windows -> ~/Desktop/OpenCNC3D-Setup-vX.Y.Z.exe
tools/launcher/selftest.sh            prove the whole update path, end to end
tools/launcher/module-check.sh        prove a package-shaped folder shows and starts Red Alert
```

You do not normally run any of these by hand. `game/make-build.sh` builds the macOS
launcher into `playable/`, `tools/win/make-build-win.sh` builds the Windows one into the
package, and `tools/release.sh` does the lot.

## Why it is not a new UI toolkit

`game/dosbar.c` and `menu/dosmenu.c` already rasterise every piece of chrome this needs,
they need nothing but libc, and they are the same code the shipping main menu draws with.
Composing them means the launcher cannot drift away from the game's look, cross compiles
with the toolchain the game already uses, and adds no runtime for a player to install.
`launcher/lui.c` adds only the two things a main menu never needed: a scrolling text panel
and a progress gauge.

Everything on screen is one 320x200 texture and one textured quad in fixed-function
OpenGL 1.1, which is what the Tier 1 target can draw through Glide. No shaders, no render
targets. The only Tier 2 dependency is SDL2, exactly as the game has.

## The files

| file | what it is |
|---|---|
| `launcher.c` | main: window, event loop, the screens, starting the game |
| `lui.c/h` | buttons, checkboxes, the gear, the inset panel, the wrapped scrolling text, the gauge, the scaled blit the module tile draws its emblem with |
| `lmedia.c/h` | the movies and music: fetch by byte range, check, remove, and the `opencnc3d-media.txt` record |
| `lmedia_manifest.c` | **generated**, committed: which file sits where on which disc image, with its size and SHA-256 |
| `lsetup_cli.c` | the headless `--media-install`, `--install-game` and `--fetch-menu-art` modes the Windows setup program runs |
| `lpath.c/h` | where am I: `_NSGetExecutablePath` / `GetModuleFileName`, not `SDL_GetBasePath` |
| `lcmdline.c/h` | the game's command line: how many arguments after `--` are carried (a longer list is refused, never cut), and the quoting of each one into the single string Windows' CreateProcess takes |
| `lnet.c/h` | one HTTP GET following redirects, and a ranged GET with resume that a Stop interrupts at once. libcurl on macOS, WinINet on Windows |
| `ljson.c/h` | just enough JSON to read the site's two API routes, and refuse anything else |
| `lzip.c/h` | SHA-256, and a zip extractor that refuses hostile paths and Zip64 |
| `lupdate.c/h` | the site's API, the version compare, the download, the install |
| `lcfg.h` | **generated**, gitignored: the build number, and a site override if any |
| `sources.sh` | the source list BOTH build scripts read, so they cannot drift |

## Where builds come from

**opencnc3d.com, and nowhere else.** The site already has a Builds section, and it is
generated from three routes the launcher reads directly:

```
GET /api/builds                the newest release: its tag, and every asset with an
                               id, a size, a platform (macos|windows) and a kind
                               (full|binaries)
GET /api/changelog             the changelog, entry by entry, bodies in markdown
GET /api/download?asset=<id>   a 302 to a short-lived signed GitHub asset URL
```

There is no separate host to stand up, no key in the binary, and no second publishing
step. The repo is private and its release assets are not public; the site proxies them,
which is what lets a launcher on a player's machine reach a build while holding no
credential at all. **Put a build on the site and every launcher offers it.**

That is also the point: the launcher and the website read one answer, so they cannot
disagree about what the newest build is.

### Pointing a build somewhere else

Only for a staging deployment, a local `next dev`, or the selftest's fake site:

```bash
OPENCNC3D_SITE=https://staging.example.com launcher/build.sh
```

`tools/launcher/make-config.sh` stamps that into the generated `lcfg.h`, refuses anything
that is not https unless it is plainly a local test server, and defaults to the live site
when unset. It also carries an optional `key`, sent as `X-OpenCNC3D-Key`, which the live site
does not ask for; it exists so that gating downloads later is a site change and a rebuild
rather than a rewrite.

### The one thing the site does not publish

`/api/builds` gives a name, an id and a size per asset. It gives no checksum, and nothing
about whether the **data** changed between two builds. Both matter:

- Without a checksum a download can only be checked by its length, which catches a
  transfer that stopped early and nothing subtler. The launcher then overwrites a
  player's game with it.
- Without a data fingerprint the launcher cannot prove the small binary-only package is
  enough, so every update is the full one.

Both are answered by **one small file published as an ordinary release asset** beside the
zips, `OpenCNC3D-vX.Y.Z-manifest.txt`, written by `tools/launcher/make-manifest.sh` and
attached by `tools/release.sh`. `/api/builds` lists every asset of the newest release, so
publishing it there is the whole of publishing it; no site change is involved. The
launcher finds it by name and gets a SHA-256 for every zip plus this platform's data
fingerprint.

**A release without it still updates.** It downloads the full package and checks its
length, which is what every release before v0.7.0 looks like. That fallback is why none of
this needs the website to change.

### Why the small update is possible, and when it is not

Every release already publishes a binary-only zip beside the full one: **2.7 MB against
507 on macOS, 15 MB against 519 on Windows.** Almost every build changes only binaries, so
almost every update can be the small one.

The launcher may only take that shortcut when it can prove the data it already has is the
data the new build expects. That proof is `data_id`: a hash over the path and size of
every file in the package that the binary-only zip does **not** carry. It is written into
`opencnc3d-install.txt` by the packagers using `tools/launcher/data-id.sh`, and
`make-manifest.sh` reads it back out of the published zip rather than recomputing it. One
implementation, quoted at both ends: two would agree until the day they did not, and the
only symptom would be every update silently becoming a full download.

Match, and the update is 15 MB. Differ, or unknown, and it is the whole package.

### What the launcher checks before it writes anything

- the download's **SHA-256** against the manifest, before a single file is unpacked, or
  its **length** when no manifest was published
- every zip entry's **path**: absolute, drive-lettered or `..`-containing entries stop the
  extraction rather than being sanitised
- **Zip64** is refused rather than misread
- the **unix permission bits** are honoured, so the game binary keeps its executable bit
- whether the zip is **wrapped in a folder**, read off the archive: the binary-only zip is
  flat on macOS and wrapped on Windows

### Unpacking over a folder that is in use

On Windows the launcher has its own `.exe` and `SDL2.dll` loaded from the folder it is
updating, and Windows will not open either for writing. So the extractor:

- **never rewrites a file that is already identical**: same size and same CRC-32 as the
  zip's entry, and it is left alone
- **writes a changed file beside itself** as `<name>.opencnc3d-new`, checks it, and only then
  renames the old one to `<name>.old` and the new one into place, which Windows allows for
  a loaded DLL or a running `.exe`. **A name never holds a half-written file**, so a kill
  cannot leave a partial launcher, `SDL2.dll` or `dosmenu.pack`, or a macOS launcher
  without its executable bit. A file that cannot be renamed **fails the update by name**
- **journals every step before taking it**, in `opencnc3d-update.journal` in the game folder,
  and forces each journal line and each new file to the disk before the step that rests on
  it
- **puts everything back if any entry fails**, so a failed update leaves the folder as it
  was rather than a new game beside an old engine. A new copy something still holds is
  renamed out of the way rather than removed, because on Windows a removal while a scanner
  has the file open leaves its name taken; if even the rename is refused the error says so,
  the old copy keeps its `.old` name, and the next start puts it back from the journal
- **checks every file it writes** against the CRC-32 and size the zip records, so a damaged
  download is refused even when the release publishes no SHA-256
- writes **`BUILD-ID.txt` after every other entry, then the site's changelog and the
  install record** it makes itself, all inside the journal, whatever order the zip lists
  them in, so a half-finished update never claims the new version
- when it starts, **finishes an update that was killed** part way, from its journal,
  **before it reads the installed version or loads anything**, then removes the old copies
  a finished update could not remove at the time, **by the names its journal kept** in
  `opencnc3d-update.done`, and a leftover download. The folder is still recognised by its
  install record when `dosmenu.pack` is between its two renames
- **holds a lock on the game folder** for as long as the launcher runs, so a second
  launcher on the same folder neither recovers nor updates until the first has gone

### What the launcher will change, and where

- **Only an install.** A folder is one when it holds `opencnc3d-install.txt` as a plain file,
  whatever it says, which both packagers write into every package (or, for the moment an update is
  replacing that record, when the journal names it as stepped aside and the stepped-aside
  copy is there). The lock is refused anywhere else, and recovery, the download and the
  extraction all need the lock, so a launcher copied into a Downloads or home folder, or
  pointed at one with `--dir`, reads what it can and changes nothing. A journal or a
  `dosmenu.pack` alone does not make a folder an install.
- **Only named files.** The launcher removes nothing it did not write except the copies an
  update's journal records that update making, each by the exact name its line gives:
  `<name>.old<n>` for the file it stepped aside or the undo's spare, `<name>.opencnc3d-new`
  for its new copy, and only while `<name>` itself exists. Each of those names was free
  when its line was written, and an update fails, naming the file, rather than take a
  `<name>.opencnc3d-new` that is already there. There is no search of the folder by pattern,
  so a player's own `opencnc3d.old` or `notes.txt.old` is never touched, beside a shipped
  file or not.
- **Only inside.** Every path is the install folder plus a name that is not absolute,
  names no drive or stream and has no `..`, reached through real folders: a journal line,
  or a zip entry, whose way down passes a symbolic link or a junction is not acted on (an
  update naming one fails). A link on a file's own name is renamed or removed as a link,
  never written through, and a file with a second hard link has its mode left alone.

Outside the launcher: the macOS app (`mac_launch.c`) never starts the game while a
journal is in the folder, and still recognises the folder while the journal says one of
the files it looks for is between its two renames. The Windows installer renames an
unfinished journal to `opencnc3d-update.done` before it writes anything, since the launcher
cannot tell a reinstalled file from one the killed update wrote. On Windows the launcher
carries a manifest that makes its narrow file APIs UTF-8 (Windows 10 1903 and later),
because its install folder is inside the user's own folder.

The launcher doing an update is the one already installed, and launchers up to v0.6.11
write in zip order, write the install record wherever the zip lists it, and stop at the
first refusal, which is always `SDL2.dll`. So the Windows full package ends
`CHANGELOG.txt`, `opencnc3d-install.txt`, `BUILD-ID.txt`, `SDL2.dll`; the binary-only zip is
flat and ends `BUILD-ID.txt`, `SDL2.dll`; and `tools/release.sh` refuses a Windows zip of
any other shape, or whose `SDL2.dll` is not the one pinned in
`tools/win/setup-toolchain.sh`.

`tools/launcher/selftest.sh` exercises all of it against a fake site that speaks the real
three routes, including the 302, in the with-manifest, without-manifest and wrapped-zip
cases, with on-disk stand-ins for files Windows would refuse to write. Several of those
behaviours are there because that test caught them going wrong.

## The Windows installer

`tools/win/make-installer-win.sh` wraps the same staged folder the zip is made from, so
the two carry identical contents. It needs `brew install makensis` and cross builds from
the Mac.

It installs **per user**, into `%LOCALAPPDATA%\Programs\OpenCNC3D`, and that is the
load-bearing decision: the launcher updates the game in place, and in Program Files that
write needs elevation, so a Program Files install would mean a UAC prompt on every update
or a launcher that asks for administrator rights every time it starts. Per user needs no
administrator rights at all.

The zip does not go away. It stays for anyone who wants the folder, and it stays because
the binary-only update is a zip unpacked over exactly that folder.

## Running it by hand

```
launcher/opencnc3d-launcher                       the window
launcher/opencnc3d-launcher --dir <folder>        point it at an install
launcher/opencnc3d-launcher --check               ask the host, print, exit
launcher/opencnc3d-launcher --update              check and install. No window.
launcher/opencnc3d-launcher --shot out.png        render one frame to a PNG
launcher/opencnc3d-launcher --view settings       open on a given screen
launcher/opencnc3d-launcher -- --w 1600 --h 960   pass the rest to the game
launcher/opencnc3d-launcher --media-install movies,music --dir <folder>
                                                  download (or, for a component left
                                                  out, remove) the movies and music
launcher/opencnc3d-launcher --install-game --media movies,music --dir <folder>
                                                  the newest build into a folder,
                                                  then the media
```

The two install modes are what the Windows setup program runs, and both write their
progress to `--progress-file` for it to draw. They exit 0 when done, 1 on failure, 2
when cancelled through `--cancel-file`, and 3 when nothing could be reached.

`--check` exits 0 for up to date, 2 for "an update is available", 1 for failed, so a
script can tell the three apart without reading the text.

The `--` passthrough is how the macOS bundle keeps handing the game `--w 1600 --h 960`.
That is not a number the launcher should know: the sidebar magnifies by whole numbers
only, so the window height has to be a multiple of 480.

## What is not done

Known limitations:

1. **Neither the Windows launcher nor the installer has been RUN on Windows.** Both cross
   compile clean and the installer counts the files it embedded, but that is packaging
   evidence, not "someone double-clicked it". The macOS half has been run against the live
   site, all the way through a real 483 MB update.
2. **No release carries a manifest asset yet**, so until v0.7.0 is cut every update is the
   full package checked by length rather than the small one checked by hash. Working as
   designed, and larger than it needs to be.
3. **The installer is ANSI**, because `makensis` on this Mac crashes writing any Unicode
   installer. A Windows account name outside the system codepage would mangle the install
   path.
4. **Nothing is signed**, so SmartScreen and Gatekeeper still apply.
5. **The selftest is not in `game/gates.sh`.**
6. **If the site ever gates downloads behind Discord sign-in**, the launcher will get an
   HTML page or a 403 where it expects a zip. It reports the status plainly, and that is
   all it can do: it has no way to sign in.
7. **The movies and music screens have been pictured and driven headless only**, through
   `--shot` and through a key-by-key run of the launcher's own state machine against a
   stand-in for `lmedia.c`. Nobody has clicked through them in a window yet, on either
   platform, and that includes Install and the system dialog that offers to fetch the
   menu art, which only a window can show.
8. **The movies and music have one mirror for the music**: the Covert Operations disc
   image on archive.org. The movies have three.
