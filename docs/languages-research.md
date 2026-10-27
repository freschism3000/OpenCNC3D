# Languages: what exists, where it lives, what the engine needs

Research for a language choice in Settings, covering Tiberian Dawn (TD, 1995) and
Red Alert (RA, 1996). Nothing was downloaded whole: every disc
finding below comes from HTTP range reads of the archive.org images (ISO9660 directories,
MIX indexes and a few 64 KB windows, about 3 MB in total), using the project's own
`tools/media/medialib.py`. Claims are marked **verified** (read from the bytes),
**sourced** (a URL says so) or **unconfirmed**.

## 1. Short answer

| Language | Game | Text | EVA | Unit voices | Movies | Range-servable archive.org source | EA freeware? |
|---|---|---|---|---|---|---|---|
| English | TD | yes | yes | yes | yes | `gdi-95` (in use) | yes (2007) |
| English | RA | yes | yes | yes | yes | `cnc-red-alert` (in use) | yes (2008) |
| German | TD | yes (verified) | yes, 44/44 lines (verified) | yes (verified) | dubbed: same frames, new audio (verified); 11 of 75 differ in length (cut or edited) | `command-conquer-der-tiberiumkonflikt-incl-addon-der-ausnahmezustand`, DOS `GDI.iso` / `NOD.iso` | no |
| German | RA | yes (verified) | yes, 103/107 entries differ (verified) | yes (verified) | own set, different audio and some different video (verified); 51 vs 52 Allied entries | `command-conquer-alarmstufe-rot-incl-addon_202202`, Virgin and EA discs | no |
| French | TD | yes (official, per the 1.06c patch notes) | yes (sourced, indirect) | unconfirmed | French set exists (sourced) | **none found on archive.org** | no |
| French | RA | yes (verified) | yes, 103/107 (verified) | yes (verified) | own set, different audio (verified) | `Westwood_CommandAndConquer_Alerte_Rouge_French_Version_MSDOS_Win95_1996`, **Soviet disc only** | no |
| Japanese | TD | DOS 1996 release is Japanese (redump); Win95 1997 JP release is English (redump) | Japanese on Saturn / PlayStation (sourced) | same | a Japanese video set exists (sourced, origin unconfirmed) | DOS discs only as `.rar` raw dumps: not range-servable | no |
| Japanese | RA | **unconfirmed** that any Japanese RA exists | | | | none found | |
| Korean | TD, RA | English game, Korean box and manual (item metadata, redump names) | English | English | English | n/a | no |
| Spanish, Italian, Polish, Br. Portuguese, British English | TD | fan translations only (text; Spanish also EVA rebuilt from Tiberian Sun) | | | Spanish subtitles (fan) | not on archive.org | n/a |
| Spanish | RA | one archive.org repack with a "translation backup": looks like a fan translation, **unconfirmed** | | | | `Videogame_____hyhygggV7` (7z) | no |

Realistically supportable from archive.org with the pipeline we already have: **German TD,
German RA, and French RA (with the Allied disc missing)**. Details and the caveats are in
section 6.

## 2. Tiberian Dawn: official language releases

### PC

- **English**: DOS (1995) and Windows 95 "Gold" (C&C95, 1997). The Gold discs are what EA
  made freeware on 31 Aug 2007. The freeware list names only the English GDI, Nod and
  Covert Operations images: https://cncnz.com/features/freeware-classic-command-conquer-games/
- **German** ("Command & Conquer: Teil 1 - Der Tiberiumkonflikt", later "Der
  Tiberiumkonflikt"): DOS, Win95 SVGA (spring 1997), Covert Ops as "Der
  Ausnahmezustand". Censored: human infantry became "cyborgs", red blood became black oil,
  death screams were replaced by small explosions or metal-crunch sounds, and some videos
  were altered. The game was put forward for indexing in 1996, and the indexing was
  rejected for good on 13 Feb 1997. Source:
  https://de.wikipedia.org/wiki/Command_%26_Conquer:_Der_Tiberiumkonflikt
- **French**: an official French release existed. The unofficial 1.06c patch calls ENG, GER
  and FRE "the three basic languages" and "all three official language releases", and
  ships French icons and strings: http://nyerguds.arsaneus-design.com/cnc95upd/cc95p106/patch106c_r3_en.html
  The same author distributes a "French videos addon pack" (365.7 MB) beside German
  (351.1 MB) and Japanese (224.9 MB) ones: http://nyerguds.arsaneus-design.com/cnc95upd/cc95p106/
- **Japanese**: DOS, Victor Entertainment, 1 Mar 1996, serial JJV-1004/1005, v1.21. Redump
  lists its language as Japanese: http://redump.org/disc/100712/ and
  http://redump.org/disc/100713/ . A Windows release by Virgin Interactive Entertainment
  Japan (1997, VIWC-1013, v1.01a) is listed by redump as **English**:
  http://redump.org/disc/77114/ and http://redump.org/disc/77113/
- **Korean**: v1.07, the disc images are named "(En)" in the archive.org item: the game
  itself is English.
- **Spanish, Italian**: no official PC localisation found. The 1.06c notes say Spanish was
  "removed because it is unofficial". A 1995 "Addendum Win95 En De Fr It Es" exists on
  archive.org (`command-and-conquer-1995-extra-addendum-win-95-en-de-fr-it-es`): printed
  matter, not game data. The PAL PlayStation SKU SLES-00530 (manual and covers labelled
  Es, It) has English menus per https://psxdatacenter.com/games/P/C/SLES-00530.html

### Consoles and Mac

- **Saturn** (EU Dec 1996, JP 25 Apr 1997 by Sega) and **PlayStation** (EU Dec 1996, JP
  1997 or 1998; sources disagree). PAL PlayStation had separate English, French
  (SLES-00531) and German (SLES-00532) SKUs: https://psxdatacenter.com/games/P/C/SLES-00532.html
  Japanese console versions had Japanese unit and EVA speech: the 1.06c patch "added the
  official Japanese units and EVA sounds from the console versions" (link above).
- **Nintendo 64** (1999): the EU cart (NUS-NCCP-EUR) has English and French voice and text;
  a separate German cart (NUS-NCCD-NOE) has German voice and text:
  https://www.nintendo64ever.com/Nintendo-64-Game,160,Command-_-Conquer.html and
  https://www.nintendo64ever.com/Nintendo-64-Game,161,Command-_-Conquer.html
- **Mac** (Dec 1996): English; no localised Mac release found.

## 3. Red Alert: official language releases

- **English**: DOS/Win95, 1996. EA made the English version freeware in 2008 ("die
  englischsprachige Vollversion"): https://de.wikipedia.org/wiki/Command_%26_Conquer:_Alarmstufe_Rot
- **German** ("Command & Conquer: Teil 2 - Alarmstufe Rot"): censored. Soldiers became
  androids (called cyborgs), the Einstein and Hitler scene was cut, Stalin's scenes were
  shortened (same Wikipedia page). Expansions: Gegenangriff (Counterstrike) and
  Vergeltungsschlag (Aftermath). German PlayStation versions exist.
- **French** ("Alerte Rouge"): official; the GPL source itself carries French CD names
  ("ALERTE ROUGE CD1", "CD Missions Taiga", "CD Missions M.A.D.") and a French registry
  key (`redalert/conquer.cpp`).
- **Japanese**: **unconfirmed**. Not found on redump or archive.org. (The archive.org
  "Red Alert (Japan) TJCD9004" items are an unrelated PC Engine game.)
- **Korean**: English game (archive.org item says "Supported language: English, Voice
  language: English").
- **Spanish**: only a repack with a translation backup on archive.org; looks unofficial.
- The GPL source only knows ENGLISH, GERMAN and FRENCH for RA, plus a commented-out
  `SPAIN` "(never used)" (`redalert/defines.h:41-45`).

## 4. What is on the discs: verified byte comparisons

### TD German DOS vs our English `data/dosdata` (same 1995 DOS layout)

Read from `GDI.iso` in the item
`command-conquer-der-tiberiumkonflikt-incl-addon-der-ausnahmezustand`. Every archive has the
same name and the same entry IDs as ours; only contents differ. **The string table is still
called `CONQUER.ENG`**, inside `INSTALL/LOCAL.MIX`.

| Archive | Entries EN / DE | What differs |
|---|---|---|
| `LOCAL.MIX` | 12 / 10 | `CONQUER.ENG` (the German strings under the English name); fonts same; DE lacks `TITLE.CPS` and one unnamed entry |
| `GENERAL.MIX` | 118 / 118 | `MISSION.INI` (the briefings, 14,277 vs 14,679 bytes), `SCG08EB.INI`, six unnamed |
| `CONQUER.MIX` | 266 / 266 | 20 entries: every infantry and civilian sprite (E1-E6, RMBO, C1-C10, CHAN, MOEBIUS, DELPHI): the cyborg censorship |
| `SOUNDS.MIX` | 148 / 148 | 76 named: every unit acknowledgement (`ACKNO`, `AFFIRM1`, `MOVOUT1`, `REPORT1`, `YESSIR1` ...; commando and civilian lines) **and** the death sounds `NUYELL1-12`, `YELL1`, `RAMYELL1`, `SQUISH2`. Weapons and explosions identical |
| `SPEECH.MIX` (EVA) | 44 / 44 | all 44 |
| `MOVIES.MIX` | GDI 62, Nod per disc | all 75 of our movie names are present. 64 have the same size as English; for those the `VQFR` (picture) chunks are byte-identical and every `SND2` (audio) chunk differs: **German dub over the same pictures**. 11 differ in length (cut or edited): DESFLEES, GDI12, GDI5, GDI8A, GDI8B, NOD1, NOD4A, NOD8, NODFLEES, TBRINFO3, TIBERFX. LOGO and BANNER are identical |
| `SCORES.MIX` | 33 / 33 | same size (56,654,668): the music is language independent |

The German Win95 Gold ("Megabox") discs in the same item carry the same `MOVIES.MIX`
(418,683,798 bytes), `CONQUER.MIX`, `SOUNDS.MIX` and a `GENERAL.MIX`, but their
`LOCAL`/`SPEECH` archives sit inside `INSTALL/SETUP.Z` (InstallShield), so they cannot be
range-read. **The DOS discs are the usable German TD source**, and they match our DOS data
layout exactly.

### RA German and French vs our English `data/radata`

Same archive tree on every language (`MAIN.MIX` and `INSTALL/REDALERT.MIX`, encrypted
indexes, bodies plain), same entry IDs, and **the strings are still `CONQUER.ENG`** in
`LOCAL.MIX` for both German and French.

| Archive | German (Virgin Allied disc) | French (Soviet disc) |
|---|---|---|
| `LOCAL.MIX` (66) | 4 differ incl. `CONQUER.ENG` (9,578 to 10,906 bytes) | 4 differ incl. `CONQUER.ENG` (11,069) |
| `GENERAL.MIX` (116) | 5 differ incl. `MISSION.INI` (briefings) | 4 differ incl. `MISSION.INI` |
| `SPEECH.MIX` (107) | 103 differ | 103 differ |
| `SOUNDS.MIX` (116) | 118 entries, 48 differ; all nine `DEDMAN*` death screams replaced by one 8,240-byte sound, `SQUISHY2` replaced | 118 entries, 49 differ (French re-recordings, not uniform) |
| `HIRES.MIX` / `LORES.MIX` | 99 / 23 differ: every infantry sprite (E1-E7, SPY, THF, MEDI, EINSTEIN, C*, CHAN): the android censorship; `PIPS.SHP` | 82 / 7 differ; infantry identical; `PIPS.SHP` |
| `CONQUER.MIX` (229) | `DOG.SHP` only | identical |
| Movies | `MOVIES1.MIX` 51 entries vs 52; `ALLY1` 11.2 MB vs 13.5 MB (cut); `PROLOG` picture chunks equal, audio differs | `MOVIES2.MIX` 55 entries; audio differs in every sampled movie |

## 5. The engine's language mechanism

`command grep -a` over `brain/vanilla/tiberiandawn` and `brain/vanilla/redalert`.

### Runtime: the data IS the language

- `Language_Name()` always returns `<base>.ENG` (`tiberiandawn/conquer.cpp:1337-1346`,
  `redalert/conquer.cpp:1494-1503`). The strings load once in `Init_Game`
  (`tiberiandawn/init.cpp:288-291`, `redalert/init.cpp:2533-2541`) into `SystemStrings`.
- `CONQUER.INI [Language] Language=Eng` is read into `config->Language`
  (`tiberiandawn/profile.cpp:61`, `redalert/profile.cpp:63`) and **never used**.
- Every archive name is language-neutral (`LOCAL.MIX`, `CCLOCAL.MIX`, `SPEECH.MIX`,
  `MOVIES.MIX`, `GENERAL.MIX`...). The discs above confirm that the German and French
  releases use the same internal names, `CONQUER.ENG` included.
- So switching language is: **put a different set of the same-named archives first on the
  search path, before boot.** Nothing re-reads strings, briefings or cached MIX files after
  boot, so a change takes effect at the next start (or needs a deliberate reload).
- In OpenCNC3D the brain's string table is not even loaded (the DLL build never reaches
  `Init_Game`, so `Text_String` answers NULL). The eyes read `CONQUER.ENG` from
  `LOCAL.MIX` themselves (`audio/cncaudio.c:116-118`), read briefings from `MISSION.INI`
  (`game/brief_mod.h`), and **bake** English unit names into `game/dosnames.h` at build
  time (`game/bake_dosnames.py`). Those baked names, and every string the eyes, launcher
  and F5 panel print themselves, would need a per-language table.

### Compile time: `GERMAN`, `FRENCH`, `JAPANESE` (TD), `ENGLISH`/`GERMAN`/`FRENCH` (RA)

TD (`tiberiandawn/defines.h:188-190`, all commented out):

- language-specific shape names: `PIPS_G/PIPS_F.SHP` (`object.cpp:1153-1159`),
  `REPAIRG/SELLG/MAPG.SHP` and the `F` variants (`sidebar.cpp:274-286`);
- hard-coded English/German/French message strings (`init.cpp`, `conquer.cpp:1761`,
  `startup.cpp`);
- layout nudges for longer words (`sounddlg.cpp:84`, `score.cpp`, `mapsel.cpp`,
  `intro.cpp:166-176`, `menus.cpp`); "MISSION" heading dropped (`scenario.cpp:462`);
- German palette kludge `RETROGER.VQP` for the Covert disc (`conquer.cpp:2222-2237`);
- `JAPANESE`: registers `LANGUAGE.MIX` (`init.cpp:204-207`), shows briefings as
  pre-rendered `.CPS` pictures instead of text (`scenario.cpp:791-800`), keyboard and
  `-ENGLISH` switch.
- **No censorship code at all**: the German cyborgs, oil and screams are entirely data
  (section 4).

RA (CMake options `GERMAN`/`FRENCH`, else `ENGLISH`: `brain/vanilla/CMakeLists.txt:39-95`):

- `language.h`: hard-coded error and title strings per language;
- `techno.cpp:167-221` `NewName[]`: translates INI unit names (ants, Tesla Tank, Volkov,
  Chitzkoi, Longbow, Civilian Specialist, Alloy Facility) for German and French builds;
- `VOX_MADTANK_DEPLOYED` exists only in English (`defines.h:3368`, `audio.cpp:521`,
  `unit.cpp:2800`); Tanya's response set differs (`infantry.cpp:2736`, `2867`);
- "MORE/MEHR/SUITE" (`scenario.cpp:1417`), "Accomplie" (`scenario.cpp:100`), CD names,
  registry key, score layout (`score.cpp`), `FOREIGN_VERSION_NUMBER 7`.

**Consequence:** an English-built engine reading German or French data works (it is all
data), with small blemishes: a handful of hard-coded English strings, RA's untranslated
`NewName` units, and German words that may overflow layouts tuned for English. Since
OpenCNC3D draws its own UI, most of the compile-time layout code does not matter to us;
the `NewName` table and the MAD Tank line are the two RA items worth handling at runtime.

## 6. archive.org sources, in detail

Sizes are bytes from `https://archive.org/metadata/<id>`. "Range-servable" means a plain
2048-byte-sector `.iso`, the only form `medialib.py` can read by byte range. None of the
non-English items is an EA freeware release; all are retail discs uploaded by users.

### Tiberian Dawn

| Item id | File | Size | MD5 | Notes |
|---|---|---|---|---|
| `gdi-95` | `GDI95.ISO`, `NOD95.ISO` | 608,987,136 / 592,476,160 | `1cb7c629...`, `1dfb2cca...` | English, EA freeware, in use |
| `command-conquer-der-tiberiumkonflikt-incl-addon-der-ausnahmezustand` | `Image/Der Tiberiumkonflikt DOS/GDI.iso`, `NOD.iso` | 499,906,560 / 506,296,320 | `726b11a2...`, `44243238...` | **German DOS, range-servable, verified** |
| same item | `Image/Der Tiberiumkonflikt SVGA (Megabox)/GDI95.iso`, `NOD95.iso`, `COVERT95.iso` | 611,194,880 / 617,484,288 / 305,901,568 | `d2e3a0f5...`, `38b28a9b...`, `2098b457...` | German Win95; LOCAL/SPEECH inside `SETUP.Z` |
| same item | `Image/Der Ausnahmezustand/...` | `.img`/`.mdf` only | | German Covert Ops, raw sectors: not range-servable |
| `Command_And_Conquer_Teil_1_-_Der_Tiberiumkonflikt_1995_Virgin_DE` | `(Disc 1 of 2).iso`, `(Disc 2 of 2).iso` | 499,906,560 / 506,296,320 | `726b11a2...`, `44243238...` | **second mirror of the German DOS discs** (identical MD5) |
| `command-conquer-der-tiberiumkonflikt-dos-german` | one `.rar` | 894,465,412 | | German DOS, not range-servable |
| `command-conquer-der-tiberiumkonflikt-windows-german` | one `.rar` | 1,052,892,036 | | German Win95, not range-servable |
| `command-conquer-der-tiberiumkonflikt-saturn-german` | `.rar` | 942,574,033 | | German Saturn |
| `command-conquer-der-tiberiumkonflikt-psx-german` | `.rar` | 811,901,731 | | German PlayStation, access-restricted |
| `command-conquer-n-64-german` | `.rar` | 13,129,206 | | German N64 cartridge dump |
| `Cc_ger`, `ccdemo` | `.zip` | 31,315,712 / 12,288,852 | | German demos |
| `command__conquer_jp_19960301_dos` | `CD1_GDI_DIC.rar`, `CD2_NOD_DIC.rar` | 1,015,224,932 / 974,602,541 | | Japanese DOS, raw dumps in rar |
| `command-conquer-jp-1997-win-1.01a` | two `.rar` | 1,042,552,256 / 1,001,938,228 | | Japanese Win95 box, English game per redump |
| `pc-command-conquer-korea` | two cue-bin `.zip` | 442,638,244 / 428,230,834 | | English game |
| `cc95v106_videobase_eng.1` | `.exe` | 502,536,752 | | English videos pack of the 1.06c patch |
| French TD | | | | **nothing found** (searched titles, languages and descriptions) |

### Red Alert

| Item id | File | Size | MD5 | Notes |
|---|---|---|---|---|
| `cnc-red-alert` | `redalert_allied.iso`, `redalert_soviets.iso` | 653,725,696 / 677,222,400 | `13e213c7...`, `b6eddcf8...` | English, EA freeware, in use |
| `command-conquer-alarmstufe-rot-incl-addon_202202` | `Image/Alarmstufe Rot (Virgin Interactive)/Alliierten.iso`, `Sowjetunion.iso` | 508,180,480 / 557,889,536 | `c8c7de4b...`, `7bbd9e86...` | **German, range-servable, verified** |
| same item | `Image/Alarmstufe Rot (Electronic Arts)/Alliierten (EA).iso`, `Sowjetunion (EA).iso` | 519,387,136 / 569,096,192 | `2a525ba6...`, `baae4141...` | German EA re-release, range-servable (Allied verified) |
| same item | `Image/Gegenangriff/Gegenangriff.mdf`, `Image/Vergeltungsschlag/Vergeltungsschlag.mdf` | 564,315,360 / 611,585,856 | | German expansions, `.mdf` |
| `Command_And_Conquer_Teil_2_-_Alarmstufe_Rot_1996_Virgin_DE` | `(Disc 1 of 2).iso`, `(Disc 2 of 2).iso` | 508,180,480 / 557,889,536 | `c8c7de4b...`, `7bbd9e86...` | **second mirror of the German Virgin discs** |
| `cd-2_20210716` | `CD1.ISO`, `CD2.ISO` | 519,387,136 / 569,096,192 | `2a525ba6...`, `baae4141...` | **second mirror of the German EA discs** |
| `cc_red_alert_german`, `command-and-conquer-2-alarmstufe-rot`, `command-conquer-alarmstufe-rot-dos-windows` | `.img`, `.mdf`, `.rar` | 1.3 GB, 1.3 GB, 1.6 GB | | German, not range-servable |
| `Westwood_CommandAndConquer_Alerte_Rouge_French_Version_MSDOS_Win95_1996` | `Westwood-CommandAndConquer-AlerteRouge-FrenchVersion-MSDOSWin95.iso` | 586,039,296 | `b88bb4b8...` | **French, range-servable, but volume `CD2` = Soviet disc only** |
| `cd-3_20241005` | `CD3.iso` | 292,876,288 | `875050cd...` | Counterstrike, tagged French (not probed) |
| `Videogame_____hyhygggV7` | `Red_Alert_PC_ES_1996.7z`, `Respaldo_Traducción_ES_Red_Alert.7z` | 1,290,343,801 / 463,949,530 | | Spanish repack, probably fan translation |
| `pc-command-and-conquer-red-alert-korea` | two `.zip` | 515,183,839 / 548,503,714 | | English game |

## 7. What the repo already holds in other languages

- **The EU N64 cartridge** (`data/rom/cnc_eu.z64`) carries `CONQUER.ENG/.FRE/.GER`,
  `MISSION.ENG/.FRE/.GER`, `BRIEFING.*`, `INTRO.*`, `HINTDATA.*` (all in
  `docs/rom-manifest.csv`), the German and French verdict banners
  `MACCGERM/MFAILGER/MACCFRCH/MFAILFRC` (the N64 cartridge's own banner files), PRIMARY / PRIMAIRE /
  PRIMAR world labels, an English and a French trailing archive of briefing scripts and
  sounds (`docs/re-findings.md`), a language list reading `ENGLISH`, `FRANCAIS`, and 56
  `.JIM` files: every campaign briefing pre-rendered as Japanese text pictures
  (`docs/re-findings.md`, `docs/CHANGELOG.md`). These are N64 texts (shorter, console
  wording), not the 1995 PC tables, but they are German and French TD text the repo already
  has. No N64 German voice: that is on the separate German cart.
- `data/dosdata` (TD) and `data/radata` (RA) are English only.

## 8. Recommendation

1. **German TD**: take `LOCAL.MIX`, `GENERAL.MIX` (or just `MISSION.INI`), `SPEECH.MIX`,
   the voice entries of `SOUNDS.MIX` and `MOVIES.MIX` from the German DOS discs, by range,
   from item `command-conquer-der-tiberiumkonflikt-incl-addon-der-ausnahmezustand` with
   `Command_And_Conquer_Teil_1_-_Der_Tiberiumkonflikt_1995_Virgin_DE` as an identical
   second mirror. It is the same layout and the same file names as our data, so the
   engine and the eyes need only a search-path switch. **Decision needed:** keep the
   English (uncensored) sprites and death sounds with German text and voices (what the
   1.06c patch does), or ship the censored set faithfully. Note the voices and the
   death screams share `SOUNDS.MIX`, so "uncensored German" means building a per-entry
   overlay, not copying the archive. Also decide whether the 11 German movies that are a
   different length (cut) play in German or fall back to English.
2. **German RA**: same approach from the Virgin discs in
   `command-conquer-alarmstufe-rot-incl-addon_202202` (mirror:
   `Command_And_Conquer_Teil_2_-_Alarmstufe_Rot_1996_Virgin_DE`), or the EA re-release
   (mirror: `cd-2_20210716`). Same censorship decision (sprites in `HIRES/LORES.MIX`, death
   sounds in `SOUNDS.MIX`). Apply RA's `NewName` translations at runtime.
3. **French RA**: text, EVA, voices and the Soviet movies are available; the Allied movies
   (`MOVIES1.MIX`) are not, because only the Soviet disc is on archive.org. Strings and
   speech live in `REDALERT.MIX`, which is on both discs, so French is complete except for
   Allied cinematics, which would fall back to English.
4. **Not now**: French TD (no source found), Japanese (no range-servable source, needs a
   Japanese font or the mission briefing pictures, voices exist only on consoles), Korean (there is
   no Korean game), Spanish / Italian / Polish / Portuguese (fan work, not official).
5. **Legal standing, plainly**: EA's freeware releases were English only. Every German and
   French source above is a retail disc someone uploaded. That is weaker than the Gold and
   Red Alert freeware we use today, comparable to the Covert Operations image whose
   standing `tools/media/README.md` already calls "less clear". This is a decision for
   the project, not a technical one.
6. Whatever is chosen, the eyes' own strings (menus, launcher, F5 panel) and the baked
   `game/dosnames.h` need their own per-language tables; the 1995 data does not cover them.

## 9. Unconfirmed, and dead ends

- French TD: whether its movies were dubbed (a French video pack exists, so probably).
- Japanese TD DOS: which parts were Japanese beyond text; whether its movies were dubbed
  or subtitled; where the "Japanese videos addon pack" came from.
- Japanese RA: no evidence of a release found.
- Dubbing was verified from the bytes (identical picture chunks, different audio chunks),
  not by listening.
- Pages that could not be read: cnc.fandom.com (HTTP 402), moddb.com (403), segaretro.org
  (bot wall). The tcrf.net Command & Conquer (Windows) page returned no language content
  and was of no use here.
