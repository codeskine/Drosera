#!/usr/bin/env python3
"""Checks a release tag against the project version and the changelog, and prints the release notes.

usage: release_check.py <tag> [--repo-root DIR]

vX.Y.Z must match project() in CMakeLists.txt and have a non-empty section in CHANGELOG.md. vX.Y.Z-rcN is a trial of
the release pipeline: only the X.Y.Z part must match, because CMake versions hold no suffix, and its notes come from
the Unreleased section.
"""
import argparse
import os
import re
import sys


def fail(message):
    print(f"release_check: {message}", file=sys.stderr)
    sys.exit(1)


def changelog_section(changelog, title):
    match = re.search(rf"^## \[{re.escape(title)}\][^\n]*\n(.*?)(?=^## \[|\Z)", changelog, re.MULTILINE | re.DOTALL)
    return match.group(1).strip() if match else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tag")
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args()

    tag = re.fullmatch(r"v(\d+\.\d+\.\d+)(-rc\d+)?", args.tag)
    if not tag:
        fail(f"'{args.tag}' is not a release tag: expected vX.Y.Z or vX.Y.Z-rcN")
    version, is_candidate = tag.group(1), tag.group(2) is not None

    with open(os.path.join(args.repo_root, "CMakeLists.txt")) as f:
        project = re.search(r"project\(Drosera VERSION (\d+\.\d+\.\d+)", f.read())
    if not project:
        fail("no project(Drosera VERSION ...) in CMakeLists.txt")
    if project.group(1) != version:
        fail(f"tag {args.tag} is version {version}, but CMakeLists.txt says {project.group(1)}")

    with open(os.path.join(args.repo_root, "CHANGELOG.md")) as f:
        changelog = f.read()
    title = "Unreleased" if is_candidate else version
    notes = changelog_section(changelog, title)
    if not any(line.strip() and not line.startswith("#") for line in (notes or "").splitlines()):
        fail(f"CHANGELOG.md has no text in a section for [{title}]")
    print(notes)


if __name__ == "__main__":
    main()
