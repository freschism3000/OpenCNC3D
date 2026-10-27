#!/usr/bin/env python3
"""Stage an allowlisted, hashed first-mission asset set without touching the install."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tempfile

PROFILE = Path(__file__).with_name("first-mission.json")
MANIFEST = "asset-manifest.json"
SCHEMA = "opencnc3d.ios.staged-resources.v1"


class StageError(RuntimeError):
    pass


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def relative_name(value: str) -> str:
    name = PurePosixPath(value)
    if not value or name.is_absolute() or ".." in name.parts or "\\" in value:
        raise StageError(f"Unsafe resource path: {value!r}")
    if name.as_posix() != value or value == MANIFEST:
        raise StageError(f"Non-canonical resource path: {value!r}")
    return value


def destination_path(output: Path, name: str) -> Path:
    """Do not follow links inside the staging tree."""
    target = output / relative_name(name)
    for part in (target, *target.parents):
        if part.is_symlink():
            raise StageError(f"Refusing a symlink in output path: {part}")
        if part == output:
            break
    return target


def normalized_output(output: Path) -> Path:
    output = Path(os.path.abspath(output.expanduser()))
    if output.is_symlink():
        raise StageError(f"Refusing a symlink staging directory: {output}")
    # Normalize system aliases such as macOS /var -> /private/var before checking
    # source separation. No links within the staging tree are permitted.
    return output.resolve()


def read_manifest(output: Path) -> dict | None:
    path = output / MANIFEST
    if path.is_symlink():
        raise StageError(f"Refusing a symlink manifest: {path}")
    if not path.exists():
        return None
    try:
        result = json.loads(path.read_text())
        if result.get("schema") != SCHEMA:
            raise StageError(f"Unrecognized manifest at {path}; refusing to replace it")
        entries = result["files"]
        names = [relative_name(item["path"]) for item in entries]
        if len(names) != len(set(names)):
            raise StageError("Duplicate paths in existing manifest")
        return result
    except (ValueError, KeyError, TypeError) as exc:
        raise StageError(f"Invalid manifest at {path}: {exc}") from exc


def plan(source: Path, profile: dict) -> tuple[list[dict], list[dict]]:
    files, missing, seen = [], [], set()
    for item in profile["assets"]:
        name = relative_name(item["path"])
        if name in seen:
            raise StageError(f"Duplicate resource: {name}")
        seen.add(name)
        path = source / name
        if not path.is_file():
            if item.get("optional"):
                missing.append(item.copy())
                continue
            raise StageError(f"Required source asset is missing: {path}")
        files.append({"path": name, "role": item["role"],
                      "bytes": path.stat().st_size, "sha256": digest(path)})
    size = sum(item["bytes"] for item in files)
    if size > profile["max_bytes"]:
        raise StageError(f"Profile exceeds its {profile['max_bytes']:,}-byte bound: {size:,} bytes")
    return files, missing


def atomic_json(path: Path, value: dict) -> None:
    descriptor, temp_name = tempfile.mkstemp(prefix=".ios-manifest-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temp_name, path)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


def stage(source: Path, output: Path, profile: dict, *, dry_run: bool = False) -> dict:
    source = source.expanduser().resolve(strict=True)
    output = normalized_output(output)
    if output == source or source in output.parents or output in source.parents:
        raise StageError("Output must be separate from the read-only source installation")
    files, missing = plan(source, profile)
    previous = read_manifest(output)
    old = {item["path"]: item for item in previous["files"]} if previous else {}
    if previous and previous["profile"] != profile["id"]:
        raise StageError("Output belongs to another resource profile; choose a new directory")

    # Preflight every conflict before creating directories or copying a single byte.
    actions = []
    for item in files:
        dest = destination_path(output, item["path"])
        if dest.exists():
            if not dest.is_file():
                raise StageError(f"Output asset is not a regular file: {dest}")
            current = digest(dest)
            if current == item["sha256"]:
                continue
            if item["path"] not in old or current != old[item["path"]]["sha256"]:
                raise StageError(f"Refusing to replace an unrelated or locally modified file: {dest}")
        actions.append(item)

    result = {"schema": SCHEMA, "profile": profile["id"],
              "scenario": profile["scenario"], "source_install": str(source),
              "launch_args": profile["launch_args"], "files": files,
              "missing_optional": missing, "excluded": profile.get("excluded", []),
              "total_bytes": sum(item["bytes"] for item in files)}
    if not dry_run:
        output.mkdir(parents=True, exist_ok=True)
        for item in actions:
            dest = destination_path(output, item["path"])
            dest.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temp_name = tempfile.mkstemp(prefix=".ios-asset-", dir=dest.parent)
            os.close(descriptor)
            try:
                shutil.copyfile(source / item["path"], temp_name)
                os.chmod(temp_name, 0o644)
                if digest(Path(temp_name)) != item["sha256"]:
                    raise StageError(f"Source changed during copy: {item['path']}")
                os.replace(temp_name, dest)
            finally:
                if os.path.exists(temp_name):
                    os.unlink(temp_name)
        # Old profile-owned paths omitted by a future profile are intentionally retained;
        # this tool never sweeps, deletes or touches unrelated output contents.
        atomic_json(output / MANIFEST, result)
        verify(output)
    return {"output": str(output), "scenario": profile["scenario"],
            "files": len(files), "copied": len(actions), "total_bytes": result["total_bytes"],
            "total_mib": round(result["total_bytes"] / 1048576, 2),
            "missing_optional": [item["path"] for item in missing], "dry_run": dry_run}


def verify(output: Path) -> dict:
    output = normalized_output(output)
    manifest = read_manifest(output)
    if manifest is None:
        raise StageError(f"No staged resource manifest in {output}")
    for item in manifest["files"]:
        path = destination_path(output, item["path"])
        if not path.is_file() or path.stat().st_size != item["bytes"] or digest(path) != item["sha256"]:
            raise StageError(f"Staged asset verification failed: {path}")
    total = sum(item["bytes"] for item in manifest["files"])
    if total != manifest["total_bytes"]:
        raise StageError("Manifest byte total does not match its files")
    return {"verified": len(manifest["files"]), "total_bytes": total, "output": str(output)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-install", type=Path,
                        default=Path(os.environ.get("OPENCNC3D_SOURCE_ROOT", str(Path(__file__).resolve().parents[3]))) / "playable",
                        help="Read-only current-main installation (default: source checkout playable directory)")
    parser.add_argument("--output", required=True, type=Path, help="App bundle's GameData directory")
    parser.add_argument("--dry-run", action="store_true", help="Hash and preflight without writing")
    parser.add_argument("--verify", action="store_true", help="Verify an existing stage without source access")
    args = parser.parse_args()
    try:
        if args.verify:
            if args.dry_run:
                parser.error("--verify and --dry-run cannot be combined")
            result = verify(args.output)
        else:
            profile = json.loads(PROFILE.read_text())
            result = stage(args.source_install, args.output, profile, dry_run=args.dry_run)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (StageError, OSError, ValueError) as exc:
        print(f"iOS resources: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
