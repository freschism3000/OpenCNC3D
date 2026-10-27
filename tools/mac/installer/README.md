# Native macOS release products —25 October2026

The design review requested the same five product names on macOS and Windows. This helper
builds the macOS half from the vetted, current-source Mac base stage and the
same-version public module ZIPs/catalog:

```sh
tools/mac/make-release-packages-mac.sh --version 0.9.0 \
  --base-stage /path/to/vetted/no-RA/mac-stage \
  --module-dir /path/to/verified/module-assets --out /path/to/new/output
```

It emits `OpenCNC3DInstaller-macos-vVERSION.pkg`,
`OpenCNC3DComplete-macos-vVERSION.pkg`, `RA3DComplete-macos-vVERSION.pkg`,
`OpenCNC3DBinaries-macos-vVERSION.zip` and `RA3DBinaries-macos-vVERSION.zip`,
plus a source/input/artifact SHA256 receipt. Existing outputs are never silently
replaced. The two binary ZIPs are patches for an otherwise complete matching
module, with its own frontend/brain/SDL layout, the native launcher/bundle and
licence notices; they carry no game data packs, saves or CFG files.

All three native products offer only the current-user home domain and install
under `~/Applications/OpenCNC3D`. Bundle relocation is disabled, so native
Installer does not find and replace a different copy behind an existing Dock
shortcut. Existing settings/saves/custom maps are absent from payload ownership.
Preflight refuses links in every named payload path, unfinished journals and
processes using the destination. The exact source base stage stays unchanged.

The empty installer contains only launcher/bundle, SDL, bootstrap menu/cursor art
and notices. Add Game owns game selection and game-specific optional-media
choices afterward. It performs no game or media download during installation.

Each Complete product carries a SHA256-verified offline module cache. Native
Installer's Customize page offers independent Music and Movies checkboxes,
initially unchecked. Its evaluated choice matrix selects exactly one hidden
script component for none/music/movies/both. That component first invokes:

```text
opencnc3d-launcher --dir INSTALL --install-module td|ra3d \
  --module-file ZIP --module-sha SHA256
opencnc3d-launcher --dir INSTALL --media-install none|music|movies|movies,music \
  --media-game td|ra --media-add-only
```

The native controller verifies and installs the offline game under its existing
lock and rollback journal before any media download. The additive installer flag
leaves unchecked components and their previous preferences untouched; on a fresh installation the controller records honest
unchecked defaults. Settings retains its separate intentional-removal action.
A failed base install stops; optional network/media failure keeps the validated game installed and its scoped partial
state available for retry in game content settings. Scripts drop to the recipient
before executing any installed controller. They never open a game window.

The welcome/conclusion pages embed the unchanged 160×160 accepted v05 emblem:
`art/interface/launcher-enhanced-v05/CNC-native.png` for platform/Dawn and
`RA-native.png` for RA3D. These are the approved shared shiny ring and established
RA battle artwork. No generation, palette replacement or model work occurs.
The native system controls use their platform typography. Actual visual approval
of a new Installer window is not inferred from package/container checks.

`launcher/mac_launch.c` now recognizes a valid launcher/menu bootstrap without a
Dawn brain, so Empty and RA-only installations launch their own platform. The
old full-Dawn direct fallback and interrupted-update `.old` recovery remain.

Verification commands (no GUI or actual user installation):

```sh
python3 tools/mac/installer/test_wrapper.py
python3 tools/mac/installer/test_scripts.py
python3 tools/mac/installer/test_additive_media.py /path/to/new/native/launcher
python3 tools/mac/installer/test_packages.py
```

The package fixture builds three actual native products from inert universal
executables. It expands/re-hashes real payloads and components, asks macOS
Installer to evaluate all media combinations, checks both checkboxes remain
visible/enabled, and verifies the distinct binary ZIPs. Installer metadata
queries require normal access to the macOS Installer services; a restricted
sandbox can list the home domain but fail to resolve it. The script fixture
substitutes only destination resolution/privilege dropping and an inert CLI;
it checks offline-before-media order, all 8 scoped combinations, failed-base stop,
optional retry, existing media/preferences under every choice, a destructive-call
negative control, link/journal refusal and unchanged private CFG. The actual
additive-media CLI test runs the native headless controller against tiny pinned
existing files and all four choices in each game, preserving unchecked
wanted/state fields, the other game and current CFG. No HTTP is needed for those
already-complete files. The wrapper test
compiles its actual predicate without entering app main or calling osascript.
Temporary fixture/payload trees are retired after verification.

These are source/container/controller checks, not a claim of a performed native
user install, media download, first launch or Gatekeeper acceptance. Actual
release artifacts and current-main installed runtime proof remain separately
required. Products are unsigned/unnotarised unless an existing Developer ID
Installer identity is explicitly supplied through
`OPENCNC3D_MAC_INSTALLER_IDENTITY`; the receipt records that distinction.
