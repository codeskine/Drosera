# Releasing Drosera

Pushing a tag builds and tests Drosera, builds the installer `Drosera-X.Y.Z-macos.pkg`, and creates a **draft** GitHub
Release with it. A person reviews the draft and publishes it.

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

5. Wait for the Release workflow. Download `Drosera-X.Y.Z-macos.pkg` and `SHA256SUMS.txt` from the draft with a
   browser, and check them in the folder you downloaded them to:

   ```bash
   shasum -a 256 -c SHA256SUMS.txt
   ```

6. Install the package and play Drosera in a host. Before the first unsigned release and before the first signed one,
   go through the [checklist](#checklist-on-a-clean-mac) instead.
7. Publish the draft. GitHub attaches the tagged source, which with JUCE pinned by CMake is the corresponding source
   AGPLv3 requires.

## What the workflow does

`release.yml` checks the tag, then runs `ci.yml` with the version. On a macOS runner, `ci.yml`:

1. Builds universal (arm64 and x86_64) and runs every test, `pluginval` on the VST3 and the AU, `auval`, and the
   reference renders.
2. Runs `tools/package/test_package.sh`, which proves the package checks still catch broken packages.
3. Builds `Drosera-X.Y.Z-macos.pkg` with `tools/package/build_pkg.sh`, signed if the signing secrets exist, and checks
   it with `tools/package/check_pkg.sh`. That check expands the package without installing it and loads the packaged
   VST3.
4. Uploads the package as the `drosera-macos` artefact.

`release.yml` then attaches the package and `SHA256SUMS.txt` to the draft. The draft's notes are the changelog section.
While releases are unsigned, they end with a paragraph on opening the installer with Open Anyway.

Every push to any branch runs the same build, tests and package check, unsigned and without uploading anything.

## Trying the pipeline

A tag `vX.Y.Z-rcN` runs the same pipeline without touching the changelog. Its notes come from `Unreleased`, the draft is
marked as a pre-release, and its package is `Drosera-X.Y.Z-rcN-macos.pkg`. Delete it afterwards:

```bash
gh release delete vX.Y.Z-rcN --cleanup-tag
```

## Signing and notarization

Releases are unsigned for now. macOS blocks the installer once; the user clicks Open Anyway in System Settings, as the
README and the release notes explain, and nothing the installer puts on the Mac is blocked afterwards. This was checked
on macOS 27.2 on 2026-11-03.

A signed and notarized installer opens with no warning. That needs a paid Apple Developer Program membership.
Software notarized while the membership is active keeps working after it lapses, so it only needs paying in the years
with a release.

### Setting it up

1. Join the Apple Developer Program. Only the account holder can create Developer ID certificates.
2. In the developer account, under Certificates, IDs & Profiles, create a **Developer ID Application** certificate
   and a **Developer ID Installer** certificate, and install both in the login keychain of a Mac.
3. In Keychain Access, under My Certificates, select both certificates. Each must show its private key beneath it.
   Choose File → Export Items and save them as one `.p12` file with a password.
4. Encode it for GitHub:

   ```bash
   base64 -i Drosera.p12 | pbcopy
   ```

5. At [account.apple.com](https://account.apple.com), under Sign-In and Security, create an app-specific password for
   notarization.
6. In the repository's settings, under Secrets and variables → Actions, add these five secrets:

   | Secret | Value |
   |---|---|
   | `APPLE_CERTIFICATE` | The base64 text of the `.p12` |
   | `APPLE_CERTIFICATE_PASSWORD` | The `.p12` file's password |
   | `APPLE_ID` | The Apple ID's email address |
   | `APPLE_TEAM_ID` | The Team ID, shown under Membership details in the developer account |
   | `APPLE_APP_PASSWORD` | The app-specific password |

7. Push a tag `vX.Y.Z-rc1`, follow the run to the end, and go through the [checklist](#checklist-on-a-clean-mac).
8. Once a signed release is published, remove "If macOS blocks the installer" from the README.

### What a signed build does

Only a build with a version signs, that is a tag push. It imports the `.p12` into a temporary keychain, reads the two
Developer ID identities from it, and passes them to `build_pkg.sh`, which:

1. Signs each bundle with the hardened runtime and a secure timestamp. The standalone app also gets the entitlement for
   audio input, in `tools/package/resources/Drosera.entitlements`.
2. Builds the package and signs it with the Developer ID Installer identity.
3. Sends it to Apple's notary service and waits for the verdict.
4. Staples the ticket to the package, so Gatekeeper accepts it offline.

`check_pkg.sh` then requires the Developer ID Installer signature, Gatekeeper's acceptance of the package as notarized,
the stapled ticket, and the hardened runtime on each bundle.

As soon as any of the five secrets exists, CI expects a signed package. If a secret is missing or a step fails, the
build fails. It never falls back to an unsigned package.

### When it fails

**This path has never run**, because the secrets do not exist yet. Expect to debug it the first time.

- **`signing needs all of …`:** a secret is missing, or the keychain did not yield both identities. A `.p12` exported
  with only one certificate, or without the private keys, does this.
- **Notarization rejected:** the job prints Apple's log for the submission. It names each file and the reason, such as
  a binary without the hardened runtime or without a timestamp.
- **`spctl does not accept the package as notarized`:** if Gatekeeper assessments are disabled on the runner, `spctl`
  cannot report a notarized source even for a good package. `pkgutil --check-signature` reports notarization on its own
  ("Notarization: trusted by the Apple notary service"), and `check_pkg.sh` can switch to it.

To sign on a Mac with the certificates installed, set the five variables `build_pkg.sh` reads, listed in the
[build guide](BUILDING.md#package).

## Checklist on a clean Mac

1. On a Mac, or a user account, that has never had Drosera, download the package from the draft with a browser.
2. Open it. Unsigned, macOS must block it once and open it with Open Anyway. Signed, it must open with no warning.
3. Check that Customize lists VST3, Audio Unit and Standalone, all selected, and read the three screens.
4. Install. Find Drosera as an Audio Unit in Logic and as a VST3 in another host, and play it.
5. Open the standalone app. macOS must ask for microphone access. In Options → Audio/MIDI Settings, untick Mute audio
   input and check that an input excites the strings.
6. Uninstall by following the installer's last screen, and check that the hosts no longer list Drosera.

## What is not covered

- The x86_64 part of the universal binary is built, but no CI job runs it.
- CI proves the plugins load and pass `pluginval` and `auval`. Only a person can play them in a DAW and listen.
- The signed path has never run.

## Checks before the first release

- **Name:** searched on 2026-11-01. The web shows no music product called Drosera. Two software companies outside music
  use the name: Drosera Network (blockchain security) and a London company in network management. In the registers,
  [TMview](https://www.tmdn.org/tmview/) lists 62 marks containing "drosera" and [USPTO](https://tmsearch.uspto.gov/) one,
  abandoned. Most live ones are TotalEnergies lubricants in class 4. None is in class 9 or 15, apart from "hidrosera",
  filed in Turkey on 2026-09-20 in classes 9, 42 and 44. The closest others are DROSERA SARL in France (classes 31 and 42)
  and a Polish pharmacy (35, 41 and 44). This is a search, not legal advice.
