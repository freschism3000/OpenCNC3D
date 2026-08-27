# The launcher

The thing a player double-clicks. One 320x200 dialog, drawn with the game's own 1995
DOS primitives over the real `TITLE.CPS` plate:

- which build is installed
- which build is available
- that build's changelog, scrolling
- **Play**, which becomes **Update** when cnc3dgame.com has a newer build
- **Editor**, drawn disabled in the engine's own `BOXSTYLE_GREEN_DIS_RAISED`, because
  there is a map editor being built and not yet one to launch
- **Quit**

```
launcher/build.sh                     macOS  -> launcher/cnc3d-launcher
tools/win/build-launcher-win.sh       Windows -> build/win/C&C3D.exe
tools/win/make-installer-win.sh       Windows -> ~/Desktop/CNC3D-Setup-vX.Y.Z.exe
tools/launcher/selftest.sh            prove the whole update path, end to end
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
| `launcher.c` | main: window, event loop, the screen, starting the game |
| `lui.c/h` | buttons, the inset panel, the wrapped scrolling text, the gauge |
| `lpath.c/h` | where am I: `_NSGetExecutablePath` / `GetModuleFileName`, not `SDL_GetBasePath` |
| `lnet.c/h` | one HTTP GET, following redirects. libcurl on macOS, WinINet on Windows |
| `ljson.c/h` | just enough JSON to read the site's two API routes, and refuse anything else |
| `lzip.c/h` | SHA-256, and a zip extractor that refuses hostile paths and Zip64 |
| `lupdate.c/h` | the site's API, the version compare, the download, the install |
| `lcfg.h` | **generated**, gitignored: the build number, and a site override if any |
| `sources.sh` | the source list BOTH build scripts read, so they cannot drift |

## Where builds come from

**cnc3dgame.com, and nowhere else.** The site already has a Builds section, and it is
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
CNC3D_SITE=https://staging.example.com launcher/build.sh
```

`tools/launcher/make-config.sh` stamps that into the generated `lcfg.h`, refuses anything
that is not https unless it is plainly a local test server, and defaults to the live site
when unset. It also carries an optional `key`, sent as `X-CNC3D-Key`, which the live site
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
zips, `CNC3D-vX.Y.Z-manifest.txt`, written by `tools/launcher/make-manifest.sh` and
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
`cnc3d-install.txt` by the packagers using `tools/launcher/data-id.sh`, and
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
- the running launcher is stepped aside **only if the zip actually carries a replacement**

`tools/launcher/selftest.sh` exercises all of it against a fake site that speaks the real
three routes, including the 302, in both the with-manifest and without-manifest cases.
Three of those behaviours are there because that test caught them going wrong.

## The Windows installer

`tools/win/make-installer-win.sh` wraps the same staged folder the zip is made from, so
the two carry identical contents. It needs `brew install makensis` and cross builds from
the Mac.

It installs **per user**, into `%LOCALAPPDATA%\Programs\CNC3D`, and that is the
load-bearing decision: the launcher updates the game in place, and in Program Files that
write needs elevation, so a Program Files install would mean a UAC prompt on every update
or a launcher that asks for administrator rights every time it starts. Per user needs no
administrator rights at all.

The zip does not go away. It stays for anyone who wants the folder, and it stays because
the binary-only update is a zip unpacked over exactly that folder.

## Running it by hand

```
launcher/cnc3d-launcher                       the window
launcher/cnc3d-launcher --dir <folder>        point it at an install
launcher/cnc3d-launcher --check               ask the host, print, exit
launcher/cnc3d-launcher --update              check and install. No window.
launcher/cnc3d-launcher --shot out.png        render one frame to a PNG
launcher/cnc3d-launcher -- --w 1600 --h 960   pass the rest to the game
```

`--check` exits 0 for up to date, 2 for "an update is available", 1 for failed, so a
script can tell the three apart without reading the text.

The `--` passthrough is how the macOS bundle keeps handing the game `--w 1600 --h 960`.
That is not a number the launcher should know: the sidebar magnifies by whole numbers
only, so the window height has to be a multiple of 480.

## What is not done

Registered as an open question rather than left to be rediscovered:

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
