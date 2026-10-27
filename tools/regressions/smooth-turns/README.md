# Smooth vehicle turns

Run `clang++ -std=c++11 tools/gate-heading-smooth.cpp -o /tmp/gate-heading-smooth && /tmp/gate-heading-smooth` for exhaustive shortest-arc/subframe math.

Run `python3 tools/regressions/smooth-turns/test-smooth-turns.py --runtime /path/to/playable --out /tmp/fresh-turn-proof` for real Harvester hull turns and independent tank turret turns. Optionally pass `--baseline /path/to/previous/cnc_eyes` and a shared `--cfg /path/to/snapshot.cfg` to demand byte-identical smoothing-off frames. The baseline executable must have its normal SDL libraries beside it.

All native runs use `--script`, BackgroundOnly mode, private settings and saves. Five alpha samples within each fixed engine tick must leave raw orientations unchanged, give fractional rendered hull/turret headings, and produce distinct drawn subframes. Smoothing off must equal engine orientation. The fixtures derive from stock SCG90EA, with no installed mission edits.
