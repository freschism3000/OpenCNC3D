# Red Alert Enhanced menus: open choices

**A** is proposed in each row; the picture named shows the choice. Everything not listed
follows the requirement and the TD Enhanced layout seat for seat.

| # | Choice | A (proposed) | B (and C) | See |
|---|---|---|---|---|
| 1 | Colour by place | Red at the title, blue in a mission, as the 1996 dialogs are | B red everywhere; C blue everywhere | 01, 30, 31 |
| 2 | Field brightness | The 1996 field darkened to 62% (grid 50%) so plates and text stand off it | B the 1996 brightness | 50 (top: 1996) |
| 3 | Title lettering | Lifted off the title and drawn above the panel, as TD's wordmark is: the HIRES cut at half scale on even scales, the LORES cut on odd ones, so it always lands on whole pixels (1440p: 377 x 139) | B the title left whole: the panel then covers the lettering | 53 |
| 4 | Backdrop density | The HIRES title where it fits at least twice, the LORES title below that (720p) | B HIRES always (720p shows a 640 x 400 title, mostly behind the panel) | 01, 02 |
| 5 | Font | GRAD6FNT, the LORES face drawn 2x: TD Enhanced's metrics, so every label fits as in TD | B the Win95 HIRES GRAD6FNT at 1x: mixed case, smaller; every fit would be rechecked | 54 |
| 6 | Label colour | Warm white on every plate; the selected plate is the bright one | B 1996: unselected labels in the ramp's own red or blue, the selected one white | 01 |
| 7 | Pointer | The shared Enhanced pointer (CUR00), already Red Alert's Enhanced battlefield pointer | B Red Alert's own MOUSE frame 0 at 2x | 52 |
| 8 | Allied emblem | The 1996 emblem as the game shows it: the gold plaque, its eagle in chevrons. No 1996 picture puts a globe in it (radar logo, briefing paper, score screen, logo movie) | B the plaque in front of a steel globe, from the domed disc the logo movie raises it out of | 60 |
| 9 | Soviet emblem | The logo movie's star, hammered silver rim, red field, gold rays, the hammer and sickle in the radar logo's bronze gold | B the hammer and sickle in the movie's dark steel | 60 |
| 10 | Side choice | An Enhanced box: both emblems, Allies / Soviet, the difficulty, Start (the 1996 choice is a plain message box, which is what Enhanced restyles) | B keep the 1996 difficulty slider and "Choose your side." box, as TD kept its 1995 TV | 55 |
| 11 | Special Ops | Two tabs, Counterstrike and Aftermath, filled from the engine's expansion list when those missions are found; while none is installed (the freeware discs carry none) the main menu button is greyed | B a campaign mission select instead (all 42 missions, both routes): new, not 1996 | 01, 18, 19 |
| 12 | User Maps | Red Alert scenario files (.INI, .MPR) in the user maps folder; the empty screen says where they go; the button stays live | B greyed while the folder is empty | 20 |
| 13 | Database | Five categories (Ships added), Allied / Soviet; numbers from RULES.INI. The 1996 data has no unit descriptions: their text needs a source (the manual) before it ships; a type without an Enhanced model shows its 1996 cameo | B the TD wording "Enhanced model not installed" | 21 |
| 14 | Network lobby rows | "England / 1": the start moves to Player Setup, since country names cut "England / 1 / Random" short in the 176 px column | B as TD: cut with ".." | 16 |
| 15 | Legal footer | "OpenCNC3D is an unofficial fan project, not affiliated with or endorsed by Electronic Arts." / "Command & Conquer: Red Alert is a trademark of Electronic Arts Inc. Created by freschism" | B other wording | 01 |

Notes for the build that follows approval:

- Each emblem becomes one low-poly model with small nearest-sampled textures like the TD
  website logos, baked into a header as `menu/bake_faction_logos.py` does for GDI and Nod;
  the corner emblems, the 28 px row badges and the 136 x 120 details plates all come from
  it. The concept is a design reference only.
- RA's multiplayer colours are the 1996 remap tones (PALETTE.CPS rows 0 to 7), paler than
  TD's swatches; the country list is eight long and opens over the seat type, as drawn.
- Load and Save need the save index split by module first (both games have an SCG01EA).
- The TD intro dialog draws its 1280 x 720 still unscaled over the well and the Play
  button; these mockups draw the still at the well's 320 x 156, and the fix is shared code.
- Rendering stays what TD's menu is: a CPU-composited RGBA panel on one GL 1.1 quad, the
  emblems as depth-tested meshes. No shader.
