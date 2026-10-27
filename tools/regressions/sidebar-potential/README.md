# Sidebar potential regression (AC4249)

This runs the real native TD library and extracts the production `sb_poll`,
`eh_refresh`, and `eh_display` adapters from the requested checkout. It creates no
window and writes only to the supplied private output directory.

```sh
python3 tools/regressions/sidebar-potential/test-sidebar-potential.py \
  --repo "$PWD" \
  --brain brain/vanilla/build-native/tiberiandawn/TiberianDawn.dylib \
  --playable playable --output /private/tmp/sidebar-potential-regression
```

The scenarios use shipped GDI/Nod missions and actual multiplayer lobby data at
tech levels 1, 5, and 7. A private empty-map fixture exercises real factory capture
and Barracks removal/reconstruction through engine methods. Tests distinguish
mission impossibility from missing prerequisites; verify current build entries,
legality, and house masks are unchanged by the query; and compare drawn/hit item
identities. The optional-export fallback must contain only live engine entries.

The fixed 16-byte result record and capacity guards are exercised, including a
canary beyond the caller's buffer. Timing reports use 20,000 calls to the actual
native export with frozen representative campaign/high-tech match state. These
are CPU query timings, not Windows frame-rate measurements.

`fixtures/SCG97EA.BIN` is the existing flat-desert SCG94 regression map payload,
used unchanged with a private mission INI created by the runner. No model or art
assets are generated or changed.
