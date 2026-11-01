# Releasing Drosera

Pushing a tag builds, tests and packages Drosera, then creates a **draft** GitHub Release. A person reviews the draft
and publishes it.

## Steps

1. Set the version in `project()` in `CMakeLists.txt`.
2. In `CHANGELOG.md`, move the `Unreleased` entries into `## [X.Y.Z] - YYYY-MM-DD`, and leave an empty `Unreleased`.
3. Check the two agree:

   ```bash
   python3 tools/release_check.py vX.Y.Z
   ```

4. Merge to `main`, then tag and push:

   ```bash
   git tag vX.Y.Z && git push origin main vX.Y.Z
   ```

5. Wait for the Release workflow. Download the zip from the draft, check it against `SHA256SUMS.txt`, install it by
   following `INSTALL.txt`, and try it in a host.
6. Publish the draft. GitHub attaches the tagged source, which with JUCE pinned by CMake is the corresponding source
   AGPLv3 requires.

## Trying the pipeline

A tag `vX.Y.Z-rcN` runs the same pipeline without touching the changelog. Its notes come from `Unreleased`, and the draft
is marked as a pre-release. Delete it afterwards:

```bash
gh release delete vX.Y.Z-rcN --cleanup-tag
```

## Notarization

Releases are signed ad hoc, and `INSTALL.txt` gives the unblocking command. To notarize, add these repository secrets:
`APPLE_CERTIFICATE` (a base64 `.p12`), `APPLE_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`.
The workflow then signs with Developer ID, notarizes and staples, with no other change. Then remove the unblocking
steps from `INSTALL.txt` and the README.

**This path has never run**, because those secrets do not exist yet. Expect to debug it the first time.

## What is not covered

- The x86_64 part of the universal binary is built, but no CI job runs it.
- CI proves the plugin loads and passes `pluginval`. Only a person can try it in a DAW and listen to it.

## Checks before the first release

The name "Drosera" and the copyright holder: see the README and below.
