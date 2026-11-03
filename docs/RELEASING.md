# Releasing Drosera

Pushing a tag builds, tests and packages Drosera, then creates a **draft** GitHub Release. A person reviews the draft
and publishes it.

## Steps

1. Set the version in `project()` in `CMakeLists.txt`.
2. In `CHANGELOG.md`, move the `Unreleased` entries into `## [X.Y.Z] - YYYY-MM-DD`, and leave an empty `Unreleased`.
3. Check the two agree. The workflow also refuses a final tag on a commit that is not on `main`:

   ```bash
   python3 tools/release_check.py vX.Y.Z
   ```

4. Merge to `main`, then tag and push:

   ```bash
   git tag vX.Y.Z && git push origin main vX.Y.Z
   ```

5. Wait for the Release workflow. Download the `.pkg` from the draft, check it against `SHA256SUMS.txt`, install it,
   and try it in a host. Before the first unsigned release and the first signed one, go through the checklist below.
6. Publish the draft. GitHub attaches the tagged source, which with JUCE pinned by CMake is the corresponding source
   AGPLv3 requires.

## Trying the pipeline

A tag `vX.Y.Z-rcN` runs the same pipeline without touching the changelog. Its notes come from `Unreleased`, and the draft
is marked as a pre-release. Delete it afterwards:

```bash
gh release delete vX.Y.Z-rcN --cleanup-tag
```

## Notarization

The release is `Drosera-X.Y.Z-macos.pkg`, built by `tools/package/build_pkg.sh` and checked by
`tools/package/check_pkg.sh`. Without the secrets below it is unsigned: macOS blocks it once, the release notes say how
to click Open Anyway, and nothing it installs is blocked afterwards.

To sign and notarize, the owner needs a paid Apple Developer Program membership. Packages notarized while it is active
keep working after it lapses.

1. In the developer account, under Certificates, create a **Developer ID Application** and a **Developer ID
   Installer** certificate, and install both in the login keychain.
2. In Keychain Access, select both identities (certificate and private key), export them together as one `.p12` with a
   password, and encode it: `base64 -i Drosera.p12 | pbcopy`.
3. Create an app-specific password for the Apple ID at account.apple.com.
4. Add these repository secrets: `APPLE_CERTIFICATE` (the base64 `.p12`), `APPLE_CERTIFICATE_PASSWORD`, `APPLE_ID`,
   `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`.

A versioned build then signs each bundle with the hardened runtime, signs the package, notarizes and staples it, and
`check_pkg.sh` requires all of that. If notarization fails, the log of Apple's verdict is in the job's output. Once a
signed release is out, remove the Open Anyway subsection from the README.

**This path has never run**, because those secrets do not exist yet. Expect to debug it the first time.

### Checklist before the first unsigned release and the first signed one

1. Install the package on a Mac that has never had Drosera, from a browser download.
2. Find Drosera as an Audio Unit in Logic and as a VST3 in another host, and play it.
3. Open the standalone app and confirm it asks for microphone access.
4. Uninstall by following the installer's last screen, and check the hosts no longer list Drosera.

## What is not covered

- The x86_64 part of the universal binary is built, but no CI job runs it.
- CI proves the plugin loads and passes `pluginval`. Only a person can try it in a DAW and listen to it.

## Checks before the first release

- **Name:** searched on 2026-11-01. The web shows no music product called Drosera. Two software companies outside music
  use the name: Drosera Network (blockchain security) and a London company in network management. In the registers,
  [TMview](https://www.tmdn.org/tmview/) lists 62 marks containing "drosera" and [USPTO](https://tmsearch.uspto.gov/) one,
  abandoned. Most live ones are TotalEnergies lubricants in class 4. None is in class 9 or 15, apart from "hidrosera",
  filed in Turkey on 2026-09-20 in classes 9, 42 and 44. The closest others are DROSERA SARL in France (classes 31 and 42)
  and a Polish pharmacy (35, 41 and 44). This is a search, not legal advice.
