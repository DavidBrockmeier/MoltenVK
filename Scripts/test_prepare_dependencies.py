#!/usr/bin/env python3
"""Exercise bootstrap safety entirely in temporary directories; never use network."""

import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import prepare_dependencies as bootstrap


class PrepareDependenciesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "ExternalRevisions").mkdir()
        for name, _, _, _ in bootstrap.DEPENDENCIES:
            self.pin(name).write_text("a" * 40)
        archive = self.root / "Templates/spirv-tools/build.zip"
        archive.parent.mkdir(parents=True)
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("build/generated.inc", "generated")

    def pin(self, name):
        return self.root / "ExternalRevisions" / (name + "_repo_revision")

    def stage(self, headers_at_root=False):
        for name, _, relative, inputs in bootstrap.DEPENDENCIES:
            if name == "SPIRV-Headers" and headers_at_root:
                relative = name
            path = self.root / "External" / relative
            for item in inputs:
                target = path / item
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("fixture")

    def run_prepare(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            bootstrap.prepare(self.root, **kwargs)

    def repository(self, name="cereal"):
        path = self.root / "External" / name
        bootstrap.git(path, "init", "--quiet")
        bootstrap.git(path, "add", ".")
        bootstrap.git(path, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                      "commit", "--quiet", "-m", "Fixture")
        revision = bootstrap.git(path, "rev-parse", "HEAD")
        self.pin(name).write_text(revision)
        return path

    def test_check_and_offline_missing_do_not_mutate(self):
        for mode in ({"check": True}, {"offline": True}):
            with self.assertRaises(bootstrap.PreparationError):
                self.run_prepare(**mode)
            self.assertFalse((self.root / "External").exists())

    def test_offline_stages_headers_without_network_and_preserves_original(self):
        self.stage(headers_at_root=True)
        with patch.object(subprocess, "run", side_effect=AssertionError("No subprocess needed")):
            self.run_prepare(offline=True)
            self.run_prepare(offline=True)
            self.run_prepare(check=True)
        self.assertTrue((self.root / "External/SPIRV-Headers").is_dir())
        self.assertTrue((self.root / "External/SPIRV-Tools/external/spirv-headers").is_symlink())

    def test_existing_generated_headers_are_not_overwritten(self):
        self.stage()
        output = self.root / "External/SPIRV-Tools/build/generated.inc"
        output.parent.mkdir()
        output.write_text("user-built")
        self.run_prepare(offline=True)
        self.assertEqual(output.read_text(), "user-built")

    def test_dirty_git_checkout_is_refused_before_any_staging(self):
        self.stage()
        repo = self.repository()
        (repo / "include/cereal/cereal.hpp").write_text("user edit")
        with self.assertRaisesRegex(bootstrap.PreparationError, "dirty dependency"):
            self.run_prepare(offline=True)
        self.assertEqual((repo / "include/cereal/cereal.hpp").read_text(), "user edit")
        self.assertFalse((self.root / "External/SPIRV-Tools/build").exists())

    def test_revision_mismatch_is_refused_without_checkout(self):
        self.stage()
        repo = self.repository()
        original = bootstrap.git(repo, "rev-parse", "HEAD")
        self.pin("cereal").write_text("b" * 40)
        with self.assertRaisesRegex(bootstrap.PreparationError, "revision mismatch"):
            self.run_prepare(offline=True)
        self.assertEqual(bootstrap.git(repo, "rev-parse", "HEAD"), original)

    def test_archive_sources_require_explicit_offline(self):
        self.stage()
        with self.assertRaisesRegex(bootstrap.PreparationError, "Use --offline"):
            self.run_prepare()

    def test_all_seven_missing_repositories_use_pins(self):
        fetched = []

        def fake_git(path, *args):
            if args[0] == "init":
                (path / ".git").mkdir()
            elif args[0] == "fetch":
                fetched.append(args)
            elif args[0] == "checkout":
                # Supply representative content for each mock repository.
                for _, _, _, inputs in bootstrap.DEPENDENCIES:
                    for item in inputs:
                        target = path / item
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_text("fixture")
            elif args[0] == "rev-parse":
                return "a" * 40
            return ""

        with patch.object(bootstrap, "git", side_effect=fake_git):
            self.run_prepare()
            self.run_prepare()
        self.assertEqual(len(fetched), 7)
        self.assertTrue(all(args[-1] == "a" * 40 for args in fetched))
        self.run_prepare(check=True)


if __name__ == "__main__":
    unittest.main()
