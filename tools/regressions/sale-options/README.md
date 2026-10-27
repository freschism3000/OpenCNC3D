# Selling-factory option regression (48E878)

This compiles the actual Update_Buildables body against isolated house/sidebar
counters. It exercises production categories, genuine additions, duplicate
suppression, captures, tech refusal, discovery and selling-factory exclusion.
It starts no game and writes only into a fresh temporary output directory.

```sh
python3 tools/regressions/sale-options/test-sale-options.py \
  --source brain/vanilla/tiberiandawn/building.cpp \
  --out /private/tmp/sale-options-regression
```

Repeat for brain/xl/tiberiandawn/building.cpp. The explicit --baseline option
requires an unguarded source and calibrates the old false-addition behavior.
Real hidden overlapping-sale playback is a separate installed-engine acceptance
check; counter tests alone do not prove the final runtime is current.

Red Alert's function has its own variant, test-sale-options-ra.py (vessels, the
kennel's dogs, Session.Type), run the same way against
brain/vanilla/redalert/building.cpp. Red Alert's house scan already skipped a
building being sold before the guard was ported, so the hidden overlapping-sale
playback on Soviet 2 (sell a power plant, then a new barracks while the plant is
still going) offers the rifleman exactly as long with and without it; the guard
covers the function's other callers.
