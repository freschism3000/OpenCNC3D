# Automatic storage retention

Scheduled maintenance runs
`python3 tools/clean-storage.py --apply` from the canonical main checkout.
A dry run omits `--apply`. Both use the same guarded helpers and one exclusive
cleanup lock. Reports, per-file hashes, outcomes, free disk space and failures
are saved under `output/storage-runs/`; `output/storage-cleanup-latest-run.json`
points to the latest results. A new low-space warning is raised below 50 GB.
No game build or foreground application is launched by storage cleanup.

## Retired film frames

`clean-showcase.py` removes PNG sequences and numbered PPM frame/exposure sequences
only inside versions explicitly declared retired in
`output/vehicle-showcase/storage-retention.json`. Protected frame roots take
precedence. Every version with candidates needs a delivery receipt and retained
final video whose SHA-256 still matches. Active process references, missing or
changed delivery proofs, changed frames, symlinks and invalid paths retain data.
Art, music, source, native scenes/settings, delivery records and useful contact
sheets remain. Version age never establishes retirement.

At delivery, verify final Desktop export hashes, materialize and verify every
input still needed by a newer version, update protected roots, then run
`clean-showcase.py --root OUTPUT --retire VERSION --apply`. A newer frame
dependency blocks retirement. Keep receipts even after frames are removed.

## Completed generated build and test outputs

`clean-generated.py` reads `output/storage-retention-builds.json`. It never
infers that an arbitrary folder is disposable from its name, age, Git ignore
status, file count or size. The registry explicitly names generated output roots.

The registered NativeCook family discovers completed successful cook receipts,
retains the newest successful cook and checks older successful outputs against it.
Failed, incomplete and unrecognized runs remain untouched. Each Unreal package's
metadata and payload sidecars must all match before any part is deduplicated.
The other supported registry kind, `verified-pairs`, requires exact older/newer
relative output roots plus completion/publication receipts with expected fields.
It accepts only compiled object/binary formats and compressed `.bin.gz` captures;
it cannot target general source artwork. Add new output pairs only after proving
completion and supersession. Do not register source, saves/settings, installation
folders, unique authoring snapshots or an entire checkout.

All registered outputs must be idle for at least 24 hours (including ctime, so
copying files with preserved old modification dates does not bypass this guard).
Neither old nor retained output may have active open files or process references.
SHA-256 must match for every replaced file. Any differing or unmatched file stays.
The completion proofs, registry and file identity are checked again before mutation.

Verified duplicates use independent APFS copy-on-write clones, atomically replacing
only duplicate storage. Every old archive path and its exact bytes remain available;
modifying one clone cannot change the retained file. Cloning failure leaves the
original intact. No hardlinks or symlinks are introduced. An unchanged-pair cache
avoids repeatedly reading and cloning the same large outputs; changing either file
invalidates the cache. Dry runs never clone or update that cache.

Publishers of new generated output families must register the narrowly scoped roots,
retained newer output and verified completion receipts in the registry. Unregistered
outputs stay. NativeCook runs under the registered root need no per-run registration.

## Temporary sessions

`clean-session-temp.py` requires a direct temp-root child with a completion receipt:

```json
{"owner":"OpenCNC3D","completed":true,"disposable":true,"kind":"session","unique_authoring_files":false}
```

Published release staging additionally uses `kind: "release-staging"` and
`published: true`. A folder without an explicit absence-of-unique-work attestation
stays. Every file/directory must have at least 72 idle hours, and activity/idle/authoring
checks are repeated just before removal. Git checkouts and recognized authoring
projects remain even when a receipt claims disposability.

Scheduled maintenance runs these scoped helpers only. It does not run the legacy broad
worktree, Git-object, release-folder or unmarked-temp deletion loops in
`clean-builds.sh`. Manual Git/worktree cleanup remains a separate task requiring
its own preservation and delivery checks. Existing game installations, Dock targets,
source assets, unique work and unrelated folders are outside automatic scope.

## Verification

Run `python3 -m unittest discover -s tools/tests -p test_storage_cleanup.py -v`.
Safety regressions cover PNG/PPM retention, protected inputs, delivery hash mismatch,
symlink/path escapes, failed/missing completion proofs, active references, changed
proofs and sidecars, independent clone isolation, clone failure, recent writes,
cache reuse, 72-hour temp retention, authoring preservation and overlapping runs.
