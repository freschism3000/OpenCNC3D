#!/usr/bin/env python3
"""Exercise staging boundary guarantees using disposable, synthetic assets."""

import copy
from pathlib import Path
import tempfile
import unittest

import stage


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.output = self.root / "GameData"
        (self.source / "missions").mkdir(parents=True)
        (self.source / "mission.pack").write_bytes(b"mission-art")
        (self.source / "missions/test.INI").write_bytes(b"scenario")
        self.profile = {"id": "fixture", "scenario": "test", "max_bytes": 1000,
                        "launch_args": ["--scen", "test"], "assets": [
                            {"path": "mission.pack", "role": "art"},
                            {"path": "missions/test.INI", "role": "scenario"},
                            {"path": "optional.pack", "role": "optional", "optional": True}]}

    def tearDown(self):
        self.temp.cleanup()

    def test_roundtrip_manifest_hashes_and_idempotence_preserve_unrelated_file(self):
        self.output.mkdir()
        unrelated = self.output / "savegame.sav"
        unrelated.write_bytes(b"player-save")
        first = stage.stage(self.source, self.output, self.profile)
        self.assertEqual(first["files"], 2)
        self.assertEqual(first["missing_optional"], ["optional.pack"])
        self.assertEqual(stage.verify(self.output)["verified"], 2)
        second = stage.stage(self.source, self.output, self.profile)
        self.assertEqual(second["copied"], 0)
        self.assertEqual(unrelated.read_bytes(), b"player-save")

    def test_missing_required_source_fails_before_output_creation(self):
        (self.source / "missions/test.INI").unlink()
        with self.assertRaises(stage.StageError):
            stage.stage(self.source, self.output, self.profile)
        self.assertFalse(self.output.exists())

    def test_size_bound_fails_before_output_creation(self):
        self.profile["max_bytes"] = 1
        with self.assertRaises(stage.StageError):
            stage.stage(self.source, self.output, self.profile)
        self.assertFalse(self.output.exists())

    def test_dry_run_copies_nothing(self):
        result = stage.stage(self.source, self.output, self.profile, dry_run=True)
        self.assertEqual(result["copied"], 2)
        self.assertFalse(self.output.exists())

    def test_unowned_conflict_preflights_before_any_copy(self):
        (self.output / "missions").mkdir(parents=True)
        (self.output / "missions/test.INI").write_bytes(b"unrelated")
        with self.assertRaises(stage.StageError):
            stage.stage(self.source, self.output, self.profile)
        self.assertFalse((self.output / "mission.pack").exists())
        self.assertFalse((self.output / stage.MANIFEST).exists())

    def test_refresh_only_updates_intact_previously_owned_asset(self):
        stage.stage(self.source, self.output, self.profile)
        (self.source / "mission.pack").write_bytes(b"new-authoritative-art")
        updated = stage.stage(self.source, self.output, self.profile)
        self.assertEqual(updated["copied"], 1)
        (self.output / "mission.pack").write_bytes(b"locally-modified-art")
        with self.assertRaises(stage.StageError):
            stage.stage(self.source, self.output, self.profile)
        with self.assertRaises(stage.StageError):
            stage.verify(self.output)

    def test_cannot_stage_into_or_over_source_install(self):
        for output in [self.source, self.source / "nested", self.root]:
            with self.assertRaises(stage.StageError):
                stage.stage(self.source, output, self.profile)
        self.assertEqual((self.source / "mission.pack").read_bytes(), b"mission-art")

    def test_output_symlink_cannot_escape_staging_directory(self):
        external = self.root / "external"
        external.mkdir()
        self.output.mkdir()
        (self.output / "missions").symlink_to(external, target_is_directory=True)
        with self.assertRaises(stage.StageError):
            stage.stage(self.source, self.output, self.profile)
        self.assertEqual(list(external.iterdir()), [])
        self.assertFalse((self.output / "mission.pack").exists())

    def test_profile_traversal_and_duplicates_are_rejected(self):
        for name in ["../escape", "/absolute", "folder/../escape", "back\\slash"]:
            profile = copy.deepcopy(self.profile)
            profile["assets"][0]["path"] = name
            with self.assertRaises(stage.StageError):
                stage.stage(self.source, self.output, profile)
        self.profile["assets"].append(self.profile["assets"][0])
        with self.assertRaises(stage.StageError):
            stage.stage(self.source, self.output, self.profile)


if __name__ == "__main__":
    unittest.main()
