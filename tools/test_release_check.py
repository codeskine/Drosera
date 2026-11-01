#!/usr/bin/env python3
"""Tests for release_check.py. Run: python3 tools/test_release_check.py"""
import os
import subprocess
import sys
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "release_check.py")
UNRELEASED = "# Changelog\n\n## [Unreleased]\n\n### Added\n\n- Something new.\n"
RELEASED = UNRELEASED + "\n## [0.1.0] - 2026-12-01\n\n### Added\n\n- First release.\n\n## [0.0.9] - 2026-01-01\n\n- Old.\n"


def run(tag, changelog, cmake_version="0.1.0"):
    with tempfile.TemporaryDirectory() as root:
        with open(os.path.join(root, "CMakeLists.txt"), "w") as f:
            f.write(f"cmake_minimum_required(VERSION 3.22)\nproject(Drosera VERSION {cmake_version} LANGUAGES C CXX)\n")
        with open(os.path.join(root, "CHANGELOG.md"), "w") as f:
            f.write(changelog)
        return subprocess.run([sys.executable, SCRIPT, tag, "--repo-root", root], capture_output=True, text=True)


class ReleaseCheck(unittest.TestCase):
    def test_final_ok(self):
        r = run("v0.1.0", RELEASED)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("First release.", r.stdout)
        self.assertNotIn("Something new.", r.stdout)
        self.assertNotIn("Old.", r.stdout)

    def test_version_mismatch(self):
        r = run("v0.2.0", UNRELEASED + "\n## [0.2.0] - 2026-12-01\n\n- Text.\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("0.2.0", r.stderr)
        self.assertIn("0.1.0", r.stderr)

    def test_missing_section(self):
        self.assertEqual(run("v0.1.0", UNRELEASED).returncode, 1)

    def test_empty_section(self):
        self.assertEqual(run("v0.1.0", UNRELEASED + "\n## [0.1.0] - 2026-12-01\n\n## [0.0.9] - 2026-01-01\n\n- Old.\n").returncode, 1)

    def test_rc_uses_unreleased(self):
        r = run("v0.1.0-rc1", UNRELEASED)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Something new.", r.stdout)

    def test_rc_mismatch(self):
        self.assertEqual(run("v0.2.0-rc1", UNRELEASED).returncode, 1)

    def test_malformed(self):
        for tag in ("v0.1", "0.1.0", "v0.1.0-beta"):
            self.assertEqual(run(tag, RELEASED).returncode, 1, tag)


if __name__ == "__main__":
    unittest.main()
