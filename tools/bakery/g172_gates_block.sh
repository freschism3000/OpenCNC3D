# =====================================================================================
# G172 THE DESERT THEATER DRAWS DESERT FLORA, AND THE TEMPERATE ONE DOES NOT.
#
# The cartridge ships one set of tree TYPES and swaps the MODEL under them at draw time
# when the theater is desert: the draw-command enqueue at RAM 0x8004A420 adds 97 to any
# model id in [103,121] when the theater word at RAM 0x80097150 is zero, and theater 0 is
# DESERT by the loader's own filename switch at RAM 0x801E5190. Ids 103..121 are exactly
# T01..T18, so the outputs are model slots 210..228, and those nineteen slots hold exactly
# four meshes: two cacti and two scrub trees. A pack is baked for one scenario and so for
# one theater, which is why the baker resolves that swap when it writes the type table.
#
# WITHOUT IT, 939 cells across the 46 shipped desert missions plant a leafy temperate
# conifer on red sand, and T08 alone is 558 of them. T08 is also the reason the binding
# has to follow the theater rather than simply be corrected: it is the one tree registered
# in all three theater fields and it is placed on 79 TEMPERATE cells across 19 missions.
#
# THIS GATE READS THE PACKS THE RUN FOLDER ACTUALLY SHIPS, so it is as red on stale DATA
# as on broken code. That is deliberate: a fixed baker and a pack baked before it look
# identical from the source, and only the pack can tell them apart.
G172T="$GATEDIR/../tools/bakery/g172_desert_flora.py"
if [ ! -f "$G172T" ]; then
  bad "G172 desert flora: $G172T is not there, so nothing was checked. A gate that cannot run is not a gate that passes"
else
  G172OUT=$(python3 "$G172T" --dir="$RUNDIR" 2>&1); G172RC=$?
  printf '%s\n' "$G172OUT" >> "$OUT"
  G172TOT=$(printf '%s\n' "$G172OUT" | grep '^G172|total|')
  G172P=$(printf '%s\n' "$G172TOT" | sed -n 's/.*pass=\([0-9]*\).*/\1/p')
  G172F=$(printf '%s\n' "$G172TOT" | sed -n 's/.*fail=\([0-9]*\).*/\1/p')
  G172U=$(printf '%s\n' "$G172TOT" | sed -n 's/.*unread=\([0-9]*\).*/\1/p')
  G172BAD=$(printf '%s\n' "$G172OUT" | grep '^G172|FAIL|' | cut -d'|' -f3 | head -4 | tr '\n' ' ')
  G172WHY=$(printf '%s\n' "$G172OUT" | grep -m1 '^G172|FAIL|' | cut -d'|' -f4)
  G172UN=$(printf '%s\n' "$G172OUT" | grep '^G172|unread|' | cut -d'|' -f3 | tr '\n' ' ')
  if [ -z "$G172TOT" ]; then
    bad "G172 desert flora: the checker printed no total line (exit $G172RC), so every number below would be read off nothing. First lines: $(printf '%s\n' "$G172OUT" | head -3 | tr '\n' ' ')"
  elif [ "$G172RC" != "0" ] && [ "${G172F:-0}" = "0" ] && [ "${G172U:-0}" = "0" ]; then
    bad "G172 desert flora: not one pack was opened and checked (pass=$G172P fail=$G172F unread=$G172U). Zero checked is not a pass, and the usual cause is a run folder with no mission packs in it"
  elif [ "$G172RC" != "0" ]; then
    bad "G172 desert flora: $G172F of the run folder's mission packs bind a tree name to the wrong theater's mesh or are missing one of the four cartridge meshes, and $G172U could not be opened at all ($G172UN). A pack baked before the desert-flora bind reports exactly this, so re-bake and re-install before reading it as a code fault. First four: $G172BAD. First reason: $G172WHY"
  elif [ "${G172P:-0}" = "0" ]; then
    bad "G172 desert flora: the checker exited clean having passed $G172P packs. A clean exit over nothing is not a pass"
  else
    ok "G172 desert flora: all $G172P mission packs in the run folder bind T04, T08, T09 and T18 to the mesh their own theater calls for, carry all four cartridge meshes (dl_00FE360 12 tris 10 opaque + 2 shadow, dl_00FE660 8 tris 6 cutout + 2 shadow, dl_00FE918 8 tris 6 opaque + 2 shadow, dl_00FEBD8 8 tris 6 cutout + 2 shadow), and not one name draws the other theater's mesh"
  fi
fi

