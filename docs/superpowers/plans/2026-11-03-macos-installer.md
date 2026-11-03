# macOS Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the release zip with `Drosera-X.Y.Z-macos.pkg`, which installs the VST3, the AU and the Standalone app. The package is built and checked on every CI run, and signed and notarized when the Apple secrets exist.

**Architecture:** Two bash scripts in `tools/package/`. `build_pkg.sh` stages the bundles, runs `pkgbuild` once per format and `productbuild` with a `distribution.xml`, and signs when its variables are set. `check_pkg.sh` expands a package without installing it and checks it. A third script, `test_package.sh`, proves `check_pkg.sh` passes a good package and rejects broken ones. CI calls the scripts in place of the zip steps.

**Tech Stack:** bash, `pkgbuild`, `productbuild`, `pkgutil`, `codesign`, `notarytool`, `stapler`, CMake/JUCE 8, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-03-macos-installer-design.md`

## Global Constraints

- Output name: `Drosera-<version>-macos.pkg`. `<version>` is the CI version input, or the version in `project()` in `CMakeLists.txt` when there is none. It may carry an rc suffix such as `0.1.1-rc1`.
- Identifiers and paths, exactly: `com.codeskine.drosera.vst3` → `/Library/Audio/Plug-Ins/VST3/Drosera.vst3`; `com.codeskine.drosera.au` → `/Library/Audio/Plug-Ins/Components/Drosera.component`; `com.codeskine.drosera.app` → `/Applications/Drosera.app`.
- Every bundle has `BundleIsRelocatable` false. Decided here, beyond the spec: it also has `BundleIsVersionChecked` false, so installing a package always writes its own version, even over a newer one.
- `distribution.xml`: `minSpecVersion="2"`, `hostArchitectures="arm64,x86_64"`, `enable_localSystem="true"` only, `<os-version min="11.0"/>`, three visible choices, all selected by default, and no `<license>` element.
- The screens are in English, with the same text in both modes. Nothing mentions Gatekeeper or Open Anyway.
- Signing variables: `DEVELOPER_ID_APPLICATION`, `DEVELOPER_ID_INSTALLER`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`. None set → unsigned. All set → signed. Any other combination → exit 1 before building anything.
- Never `codesign --deep`. Only the Standalone app takes entitlements: `com.apple.security.device.audio-input`.
- Microphone text, verbatim: `Drosera uses audio input to feed external sound into its feedback loop.`
- Nothing touches `src/dsp/`. `tools/hardware-comparison/check_reference.sh build/render_cli` must still pass with no new checksums.
- The scripts start with `set -euo pipefail`, take paths that may contain spaces, and clean up their temporary folders with a `trap`.
- Commit messages follow the repo's style (an imperative sentence, no prefix) and end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **A missing bundle.** If the AU or the Standalone app did not build, `build_pkg.sh` must fail and name the missing bundle, not ship a package with two formats. Test in Task 1.
2. **Paths and versions outside the happy path.** An artefacts folder or output path containing a space, and an rc version such as `0.1.1-rc1`, must both build and pass `check_pkg.sh`. Test in Task 1.
3. **Licence text with HTML characters.** The AGPL contains `<https://www.gnu.org/licenses/>`. The Read Me must show it escaped as `&lt;https://www.gnu.org/licenses/&gt;`, not swallow it as a tag. Test in Task 1.
4. **A broken seal on a bundle.** `check_pkg.sh` must reject a package whose bundle fails `codesign --verify --strict`. This happened before: JUCE writes `moduleinfo.json` after signing. Test in Task 1.
5. **Half-configured signing.** In CI, a partial set of signing variables must fail the build, never produce an unsigned package. Test in Task 3.

---

### Task 1: Unsigned package, its checks, and their test

**Files:**
- Create: `tools/package/build_pkg.sh`, `tools/package/check_pkg.sh`, `tools/package/test_package.sh` (all executable)
- Create: `tools/package/resources/distribution.xml`, `components.plist`, `welcome.html`, `conclusion.html`, `readme-header.html`, `scripts/au/postinstall` (executable)
- Modify: `CLAUDE.md` (Commands and Code map)

**Interfaces:**
- Produces: `tools/package/build_pkg.sh <artefacts dir> <version> <output.pkg>`. `<artefacts dir>` holds `VST3/Drosera.vst3`, `AU/Drosera.component` and `Standalone/Drosera.app`, like `build/Drosera_artefacts/Release`. Exit 0 and the package at `<output.pkg>`, or exit 1 with a message on stderr.
- Produces: `tools/package/check_pkg.sh <package.pkg>`. It reads `PLUGIN_HOST_TEST`, default `build/plugin_host_test_artefacts/Release/plugin_host_test`, and `STATE_FIXTURE`, default `tests/fixtures/state-baseline.bin`. Exit 0, or exit 1 with one line per failed check on stderr.
- Produces: `tools/package/test_package.sh <artefacts dir>`, which prints `PASS <name>` / `FAIL <name>` for each case and exits 1 on any failure.
- Produces, for Task 3: a function `sign_and_notarize` in `build_pkg.sh`, called between staging and `productbuild` when signing is on, and a `signed_checks` block in `check_pkg.sh`. Task 1 leaves both as the unsigned branch only.

- [ ] **Step 1: Write `test_package.sh`, the failing test**

It builds packages into a temporary folder from `$1` and runs `check_pkg.sh` on each. Cases and assertions:
- `good`: `build_pkg.sh "$1" 0.0.0-test <tmp>/Drosera.pkg`, then `check_pkg.sh` exits 0.
- `spaces_and_rc`: copy `$1` to `<tmp>/arte facts/` and output to `<tmp>/out dir/Drosera-0.1.1-rc1-macos.pkg`; both scripts exit 0.
- `readme_escapes_html`: the expanded package's `Resources/readme.html` contains `&lt;https://www.gnu.org/licenses/&gt;` and not `<https://www.gnu.org/licenses/>`.
- `missing_au`: copy `$1` without `AU/`; `build_pkg.sh` exits non-zero and its stderr contains `Drosera.component`.
- `relocatable_rejected`: build the same three component packages with plain `pkgbuild --root` (no `--component-plist`, so the bundles default to relocatable) and the repo's `distribution.xml`. `check_pkg.sh` exits non-zero and stderr contains `relocatable`.
- `broken_seal_rejected`: copy `$1`, append a byte to `VST3/Drosera.vst3/Contents/Resources/moduleinfo.json` (create it if absent), build. `check_pkg.sh` exits non-zero and stderr contains `codesign`.
- `two_components_rejected`: a product built from only the VST3 and AU component packages; `check_pkg.sh` exits non-zero.

- [ ] **Step 2: Run it and see it fail**

Run: `cmake --build build --target Drosera_VST3 Drosera_AU Drosera_Standalone plugin_host_test && tools/package/test_package.sh build/Drosera_artefacts/Release`
Expected: FAIL on every case, because `build_pkg.sh` does not exist.

- [ ] **Step 3: Write the resources**

- `distribution.xml`: as the Global Constraints say. Choices `vst3`, `au`, `app`, titled `VST3`, `Audio Unit`, `Standalone`. `<welcome file="welcome.html"/>`, `<readme file="readme.html"/>`, `<conclusion file="conclusion.html"/>`, title `Drosera`. The placeholder `@VERSION@` goes in the three `pkg-ref` versions.
- `components.plist`: the array `pkgbuild --analyze` produces for one bundle, with `BundleIsRelocatable` false, `BundleIsVersionChecked` false, `BundleOverwriteAction` `upgrade`. `RootRelativeBundlePath` stays `@BUNDLE@`, and the script fills it in per format.
- `welcome.html`: what Drosera is (one line from the README), the three formats with their paths, and that Customize leaves formats out.
- `readme-header.html`: `Copyright 2026 codeskine.`, the AGPLv3 notice, and the source address `https://github.com/codeskine/Drosera/releases`, as `docs/INSTALL.txt` words it today.
- `conclusion.html`: rescan plugins in the host, and the Standalone app is in Applications. To uninstall, delete the three paths. Users of 0.1.x: delete `~/Library/Audio/Plug-Ins/VST3/Drosera.vst3`, or the host finds two copies.
- `scripts/au/postinstall`: `#!/bin/sh`, `killall -9 AudioComponentRegistrar 2>/dev/null`, `exit 0`.

- [ ] **Step 4: Implement `build_pkg.sh`**

1. Check the three bundles exist and name any missing one.
2. Stage each with `ditto` into its own root.
3. Generate `readme.html`: `readme-header.html`, then for each licence file in the spec's order, an `<h2>` with its name and its text in a `<pre>` escaped with `sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'`.
4. Run `pkgbuild --root <stage> --component-plist <filled plist> --identifier <id> --version <version> --install-location <dir>` for each format. For the AU only, add `--scripts resources/scripts/au`.
5. Run `productbuild --distribution <filled xml> --resources <dir with the three html files> --package-path <components dir> <output>`.

Leave the signing branch as a comment naming `sign_and_notarize` for Task 3.

- [ ] **Step 5: Implement `check_pkg.sh`**

1. Expand with `pkgutil --expand-full` into a temporary folder.
2. Require exactly three component folders. Each `PackageInfo` must have the identifier and `install-location` of its row, and none may have a `<relocate>` element; else fail with `relocatable`.
3. Require `Distribution` to contain `hostArchitectures="arm64,x86_64"` and `min="11.0"`.
4. Run `codesign --verify --strict` on each bundle under `Payload`. A failure prints a line containing `codesign`.
5. Run `"$PLUGIN_HOST_TEST" <expanded VST3> "$STATE_FIXTURE"`.

- [ ] **Step 6: Run the test and see it pass**

Run: `tools/package/test_package.sh build/Drosera_artefacts/Release`
Expected: `PASS` for all seven cases, exit 0.

- [ ] **Step 7: Add the commands to `CLAUDE.md`**

Under Commands, two blocks:
- `tools/package/build_pkg.sh build/Drosera_artefacts/Release 0.0.0 build/Drosera.pkg && tools/package/check_pkg.sh build/Drosera.pkg`
- `tools/package/test_package.sh build/Drosera_artefacts/Release`

In the code map, a row for `tools/package/`: the installer package, its checks, and the installer's screens.

- [ ] **Step 8: Commit**

```bash
git add tools/package CLAUDE.md
git commit -m "Build an installer package for the VST3, AU and Standalone, and check it without installing"
```

### Task 2: Microphone permission for the Standalone app

**Files:**
- Modify: `CMakeLists.txt` (the `juce_add_plugin(Drosera …)` call)
- Create: `tools/package/resources/Drosera.entitlements`
- Modify: `tools/package/test_package.sh`

**Interfaces:**
- Produces: `tools/package/resources/Drosera.entitlements`, a plist whose only key is `com.apple.security.device.audio-input`, true. Task 3 signs the Standalone app with it.

- [ ] **Step 1: Add the failing case `standalone_asks_for_microphone` to `test_package.sh`**

In the `good` package's expanded Standalone app, `/usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" Info.plist` prints exactly `Drosera uses audio input to feed external sound into its feedback loop.`
Also: `plutil -lint` passes on `Drosera.entitlements`, and its only key is `com.apple.security.device.audio-input`.

- [ ] **Step 2: Run it and see it fail**

Run: `tools/package/test_package.sh build/Drosera_artefacts/Release`
Expected: `FAIL standalone_asks_for_microphone`.

- [ ] **Step 3: Add `MICROPHONE_PERMISSION_ENABLED TRUE` and `MICROPHONE_PERMISSION_TEXT` with the constraint's text to `juce_add_plugin`, and write `Drosera.entitlements`**

- [ ] **Step 4: Rebuild and run everything that could notice**

Run: `cmake --build build && tools/package/test_package.sh build/Drosera_artefacts/Release && build/render_test && tools/hardware-comparison/check_reference.sh build/render_cli`
Expected: all PASS. The reference check shows no changed checksum.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tools/package
git commit -m "Ask for microphone access in the Standalone app, and add its entitlement for signing"
```

### Task 3: Signed mode

**Files:**
- Modify: `tools/package/build_pkg.sh`, `tools/package/check_pkg.sh`, `tools/package/test_package.sh`

**Interfaces:**
- Consumes: `Drosera.entitlements` from Task 2, and the signing variables in the Global Constraints.
- Produces: in signed mode, `build_pkg.sh` writes a signed, notarized and stapled package to the same `<output.pkg>`.

- [ ] **Step 1: Add the failing cases**

- `partial_signing_vars_rejected`: run `build_pkg.sh` once with only `DEVELOPER_ID_APPLICATION` set, and once with all of them but `APPLE_APP_PASSWORD`. Each must exit non-zero within a second, print `signing` on stderr, and leave no output file.
- `unsigned_package_has_no_signature`: on the `good` package, `pkgutil --check-signature` reports `no signature`, and `check_pkg.sh` still exits 0. The signed checks apply only to a signed package.

- [ ] **Step 2: Run them and see `partial_signing_vars_rejected` fail**

Run: `tools/package/test_package.sh build/Drosera_artefacts/Release`
Expected: `FAIL partial_signing_vars_rejected`.

- [ ] **Step 3: Implement the check of the variables and `sign_and_notarize`**

Check the variables first, before any build work. `sign_and_notarize` runs on the staged bundles before `pkgbuild`:
- `codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID_APPLICATION"` on the VST3 and the AU;
- the same plus `--entitlements resources/Drosera.entitlements` on the app.

After `productbuild`, which now takes `--sign "$DEVELOPER_ID_INSTALLER"`:
- run `xcrun notarytool submit <pkg> --apple-id … --team-id … --password … --wait --output-format json`;
- read `status` and `id` from the JSON. Unless the status is `Accepted`, print `xcrun notarytool log <id> …` and exit 1;
- then run `xcrun stapler staple <pkg>`.

- [ ] **Step 4: Implement `signed_checks` in `check_pkg.sh`**

When `pkgutil --check-signature` does not say `no signature`:
- it must name `Developer ID Installer`;
- `spctl --assess --type install -vv` must print `source=Notarized Developer ID`;
- `xcrun stapler validate` must exit 0;
- every bundle must pass `codesign --verify --strict` and show `runtime` in `codesign -dv` flags.

- [ ] **Step 5: Run the test and see it pass**

Run: `tools/package/test_package.sh build/Drosera_artefacts/Release`
Expected: all PASS. The signed path itself runs only with real certificates, and Task 6 says so.

- [ ] **Step 6: Commit**

```bash
git add tools/package
git commit -m "Sign, notarize and staple the package when the Developer ID variables are set"
```

### Task 4: CI and release

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `build_pkg.sh`, `check_pkg.sh`, `test_package.sh`.
- Produces: the `drosera-macos` artefact, holding `Drosera-<version>-macos.pkg`.

- [ ] **Step 1: Edit `ci.yml`**

1. Add `Drosera_Standalone` to the Build step's targets.
2. After "Check reference renders", add a step "Test the package scripts" that runs `tools/package/test_package.sh build/Drosera_artefacts/Release`.
3. Replace "Sign with Developer ID and notarize" and "Package" with one step, "Build and check the installer", which runs on every push:
   - the version is `inputs.version`, or else `sed -nE 's/^project\(Drosera VERSION ([0-9.]+).*/\1/p' CMakeLists.txt`;
   - when `inputs.version != ''` and `CAN_NOTARIZE == 'true'`, it imports the `.p12` into a temporary keychain, as the old step does, and exports `DEVELOPER_ID_APPLICATION` and `DEVELOPER_ID_INSTALLER`, read with `security find-identity -v -p basic "$keychain"`. Only this step sees the secrets;
   - then it runs `build_pkg.sh` and `check_pkg.sh` on `Drosera-$version-macos.pkg`.
4. "Upload the package" uploads `Drosera-${{ inputs.version }}-macos.pkg`.

- [ ] **Step 2: Edit `release.yml`**

1. The publish job uses the `.pkg` in place of the zip, both in `sha256sum` and in `gh release create`.
2. Add a job-level env `SIGNED: ${{ secrets.APPLE_ID != '' }}`. When it is not `true`, append to `notes.md` a paragraph: the package is not notarized yet; open it, then go to System Settings → Privacy & Security and click Open Anyway.

- [ ] **Step 3: Check the workflows**

Run: `actionlint .github/workflows/ci.yml .github/workflows/release.yml`. If it is missing, install it with `brew install actionlint`.
Expected: no errors.

- [ ] **Step 4: Commit, push the branch, and watch CI**

```bash
git add .github/workflows
git commit -m "Build and check the installer in CI, and release it in place of the zip"
git push -u origin macos-installer
```

Expected: the CI run for the branch is green, with "Test the package scripts" and "Build and check the installer" passing.

### Task 5: Documents

**Files:**
- Delete: `docs/INSTALL.txt`
- Modify: `README.md` (Install), `docs/RELEASING.md`, `docs/superpowers/specs/2026-11-01-repository-and-release-design.md` (a note at the top), `CHANGELOG.md` (Unreleased)

- [ ] **Step 1: Check that nothing still uses `INSTALL.txt`**

Run: `git grep -n "INSTALL.txt"`
Expected: matches only in `docs/RELEASING.md`, `README.md` and the 2026-11-01 spec. All three are changed below; the old spec keeps its own text under the new note.

- [ ] **Step 2: Write the changes the spec's Documents section lists**

- **README:** download the `.pkg` from Releases and open it. "If macOS blocks the installer": open it, then System Settings → Privacy & Security → Open Anyway; no Terminal. Then how to uninstall, and the note for 0.1.x users.
- **`RELEASING.md`:**
  - how to request both Developer ID certificates and export them as one `.p12`;
  - that the signed path has never run;
  - the manual checklist from the spec;
  - step 5 of the release steps now downloads and installs the `.pkg`;
  - remove the unblocking text.
- **CHANGELOG:** under Added, the installer and the AU and Standalone formats. Under Changed, the 0.1.x note.

- [ ] **Step 3: Commit**

```bash
git rm docs/INSTALL.txt
git add README.md docs/RELEASING.md docs/superpowers/specs/2026-11-01-repository-and-release-design.md CHANGELOG.md
git commit -m "Document installing with the package, and how to sign and notarize it"
```

### Task 6: Install on this Mac (manual, with the owner)

Run the spec's checklist on the unsigned package. Only the owner can click through Installer and a DAW.

- [ ] **Step 1: Build the package from the branch, quarantine it as Safari would, and reveal it in Finder**

```bash
tools/package/build_pkg.sh build/Drosera_artefacts/Release 0.0.0 ~/Downloads/Drosera-0.0.0-macos.pkg
xattr -w com.apple.quarantine "0083;$(printf '%x' $(date +%s));Safari;$(uuidgen)" ~/Downloads/Drosera-0.0.0-macos.pkg
open -R ~/Downloads/Drosera-0.0.0-macos.pkg
```

- [ ] **Step 2: The owner installs it**

They open it, use Open Anyway, and look at Customize and the three screens. Then they find Drosera as an AU in Logic and as a VST3 in another host, and open the Standalone app, which should ask for microphone access.

- [ ] **Step 3: Check the result**

None of the three installed bundles has `com.apple.quarantine` (`xattr -l`). `pkgutil --pkgs | grep com.codeskine.drosera` lists three receipts.

- [ ] **Step 4: Report**

Say what passed, what the owner saw, and that the signed path is untested until the secrets exist.
