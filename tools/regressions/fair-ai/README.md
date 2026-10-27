# Fair AI simulation matrix

From the repository root on macOS:

```sh
python3 tools/regressions/fair-ai/run.py --install "$HOME/CNC3D/playable" --out /tmp/fair-ai-results
```

This compiles dependency-free probes and runs the actual installed TD/RA brains
without a game window. It writes per-case logs, canonical world snapshots and
`results.json`. Pass `--td` and `--ra` to test other built libraries. The default
12,000-tick matrix covers all levels, mixed seats, rule parity, exact saves and
future continuity, deterministic repeats, local-player independence, hidden enemy
money and RA shroud regrowth. Installed content is read only and saves use scratch
folders. See `docs/design-fair-skirmish-ai.md` for the policy and related UI/network gates.
