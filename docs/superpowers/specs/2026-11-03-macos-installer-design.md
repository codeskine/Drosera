# macOS installer design

This design replaces the release zip with an installer package. It reverses two non-goals of
`2026-11-01-repository-and-release-design.md`: installers, and AU and Standalone releases.

## Goal

Release Drosera as one file, `Drosera-X.Y.Z-macos.pkg`, which installs the VST3, the AU and the Standalone app. The
package is built and checked in CI. When Apple credentials are available it is signed with Developer ID and notarized,
and it opens with no Gatekeeper warning.

Success means:

- A user installs all three formats by opening one file. They never use Terminal, `xattr` included.
- With the credentials, the package opens with no warning. `spctl` reports it as `accepted` from a notarized
  Developer ID.
- Without them, macOS blocks the package once. The user clicks **Open Anyway** in System Settings, and nothing else
  is blocked afterwards: the hosts load the plugins and the Standalone app opens.
- Every CI run builds and checks the package, so packaging cannot break unnoticed between releases.
- Renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`, because nothing here touches the
  DSP.

## Non-goals

A `.dmg`, an uninstaller, localised installer screens, per-user installation, Windows, automatic updates, and a
documented way to sign on a developer's own Mac. The script can sign anywhere the variables are set, but CI is the
only signing path this design documents and tests.

## Findings that shape the design

**Signing needs a paid account.** Developer ID certificates and notarization require the Apple Developer Program.
Software notarized while the membership is active keeps working after it lapses, so the owner can pay for the years
with a release. Until then, the package is unsigned.

**An unsigned package costs the user one approval.** Since macOS 15, Control-click → Open no longer overrides
Gatekeeper; the route is System Settings → Privacy & Security → Open Anyway. A probe on macOS 27.2 on 2026-11-03
checked what happens next. It used an unsigned `.pkg` holding an app and a `.vst3` bundle, both signed ad hoc, given
the quarantine attribute Safari writes, and opened from Finder:

- Installer did not pass the quarantine attribute to the installed files. They carried only `com.apple.provenance`.
- The installed app launched through LaunchServices without Gatekeeper.
- The `.vst3` binary loaded into another process.

The probe did not use Drosera's own bundles or a real DAW. The manual checklist below covers that.

**Homebrew offers no way round it.** Homebrew 5 deprecated `--no-quarantine`, and from 2026-09-01 it disables casks
that fail Gatekeeper.

## Package layout

`pkgbuild` builds one component package per format. `productbuild` combines them with `distribution.xml` into the
product package.

| Choice | Identifier | Installs |
|---|---|---|
| VST3 | `com.codeskine.drosera.vst3` | `/Library/Audio/Plug-Ins/VST3/Drosera.vst3` |
| Audio Unit | `com.codeskine.drosera.au` | `/Library/Audio/Plug-Ins/Components/Drosera.component` |
| Standalone | `com.codeskine.drosera.app` | `/Applications/Drosera.app` |

- Each component's version is the version in `project()` in `CMakeLists.txt`.
- The three choices are shown under Customize and are all selected by default.
- Installation is for all users, on the system volume, as an administrator (`enable_localSystem` only).
- A component plist sets `BundleIsRelocatable` to false on every bundle. Otherwise Installer updates a copy found
  elsewhere, for example `~/Downloads/Drosera.app`, instead of writing to the paths above.
- `distribution.xml` requires macOS 11 or newer, matching `CMAKE_OSX_DEPLOYMENT_TARGET`. It declares
  `hostArchitectures="arm64,x86_64"`, so Installer on Apple silicon does not ask for Rosetta.
- The AU component has a `postinstall` script. Its only command is `killall -9 AudioComponentRegistrar`, so hosts see
  a new version without a restart. It exits 0 whether or not that process was running.

### Screens

The screens are in English, like the rest of the project. Their text is the same in both modes. Instructions about
Gatekeeper cannot go here, because a user reads them only after the package has opened.

1. **Welcome:** what Drosera is, the three formats and where each goes, and that Customize leaves formats out.
2. **Read Me:** the copyright notice and the AGPLv3 notice, with the address of the source. Then the full text of
   `LICENSE`, `LICENSE-AudreyII`, `THIRD_PARTY_LICENSES.md`, `third_party/DaisySP/LICENSE`,
   `assets/fonts/OFL-Inter.txt` and `assets/fonts/OFL-Cormorant.txt`. `build_pkg.sh` generates this page from those
   files, so it cannot drift from them. It replaces the licence files the zip carried, and adds the font licences,
   which the zip lacked.
3. **Conclusion:** rescan plugins in the host, and where to find the Standalone app. Then how to uninstall: delete the
   three paths in the table. And a note for users of 0.1.x, who copied the VST3 to
   `~/Library/Audio/Plug-Ins/VST3/` by hand: delete that copy, or the host finds two.

No screen has the Agree / Disagree licence dialog. The AGPL does not need to be accepted to use the program, and an
agreement to accept would misrepresent it.

The installer never touches a user's home folder, the old 0.1.x copy included.

## Signing and notarization

`build_pkg.sh` builds an unsigned package by default. It signs when `DEVELOPER_ID_APPLICATION`,
`DEVELOPER_ID_INSTALLER` and the notarization variables are all set. Each `DEVELOPER_ID_*` variable holds the full
name of a certificate, such as `Developer ID Application: Name (TEAMID)`. If only some of the variables are set, the
script stops with an error. Once signing has started, any failure stops the script. In CI it never falls back to an
unsigned package once the secrets exist.

**Unsigned mode.** The bundles keep the ad hoc signatures CMake gives them. `productbuild` runs without `--sign`.

**Signed mode.**

1. Sign each bundle on its own with
   `codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID_APPLICATION"`, never with `--deep`.
   Notarization requires the hardened runtime on every executable, plug-ins included.
2. Sign the Standalone app with `tools/package/resources/Drosera.entitlements` as well. Its only entitlement is
   `com.apple.security.device.audio-input`. The VST3 and the AU take no entitlements, because they run with the
   host's.
3. Run `productbuild --sign "$DEVELOPER_ID_INSTALLER"`. The component packages inside are not signed.
4. Run `xcrun notarytool submit --wait`. If Apple rejects the package, print `xcrun notarytool log` for the
   submission, then fail.
5. Run `xcrun stapler staple` on the package, so Gatekeeper accepts it offline.

**Microphone.** Under the hardened runtime, macOS gives a signed app audio input only if it declares why.
`CMakeLists.txt` adds `MICROPHONE_PERMISSION_ENABLED TRUE` and
`MICROPHONE_PERMISSION_TEXT "Drosera uses audio input to feed external sound into its feedback loop."` to
`juce_add_plugin`. This changes only the bundles' `Info.plist`. The post-build ad hoc signing stays as it is.

**Credentials in CI.** The repository secret names stay as `RELEASING.md` lists them: `APPLE_CERTIFICATE`,
`APPLE_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`. `APPLE_CERTIFICATE` becomes one
base64 `.p12` holding both identities, Developer ID Application and Developer ID Installer. CI imports it into a
temporary keychain, as the current notarization step does. It reads the two certificate names from that keychain and
passes them to the script.

## Files

| Path | Holds |
|---|---|
| `tools/package/build_pkg.sh` | `build_pkg.sh <artefacts dir> <version> <output.pkg>`: stages the three bundles, generates the Read Me, builds the component and product packages, and signs and notarizes in signed mode |
| `tools/package/check_pkg.sh` | `check_pkg.sh <package.pkg>`: checks a package without installing it (below) |
| `tools/package/resources/distribution.xml` | The product definition; the script fills in the version |
| `tools/package/resources/components.plist` | `BundleIsRelocatable` false for every bundle |
| `tools/package/resources/welcome.html`, `conclusion.html` | The fixed screens |
| `tools/package/resources/readme-header.html` | The top of the Read Me, before the licence texts |
| `tools/package/resources/Drosera.entitlements` | The Standalone app's entitlement |
| `tools/package/resources/scripts/au/postinstall` | The AU cache refresh |

## Checks

`check_pkg.sh` expands the package with `pkgutil --expand-full` into a temporary folder and fails unless:

- it holds exactly the three component packages, with the identifiers and install paths in the table;
- every bundle has `BundleIsRelocatable` false;
- `distribution.xml` requires macOS 11 and lists both architectures;
- every bundle passes `codesign --verify --strict`;
- the VST3 from the package loads and renders in `plugin_host_test` against `tests/fixtures/state-baseline.bin`. What
  a user installs is what must work, as the zip check had it.

If the package is signed, it also requires `pkgutil --check-signature` to name a Developer ID Installer certificate,
`spctl --assess --type install` to report `source=Notarized Developer ID`, and `xcrun stapler validate` to succeed.

## CI and release

**`ci.yml`**

- A new step builds the package unsigned and runs `check_pkg.sh` on every push. Without a version input, it uses the
  version in `project()`.
- For a build with a version, the step runs in signed mode when the secrets exist.
- It replaces the steps "Sign with Developer ID and notarize" and "Package".
- `auval` and `pluginval` on the AU, already in CI, stay as they are.
- "Upload the package" uploads `Drosera-X.Y.Z-macos.pkg` as the `drosera-macos` artefact.

**`release.yml`** publishes `Drosera-X.Y.Z-macos.pkg` and its checksum in `SHA256SUMS.txt`. Its publish job runs on
Linux, where `pkgutil` does not exist, so it decides as `ci.yml` does, from whether the `APPLE_ID` secret is set. When
it is not, it adds a short paragraph to the notes on opening the package with Open Anyway. `release_check.py` is
unchanged.

## Documents

- `docs/INSTALL.txt` is deleted. Its content moves to the installer screens.
- `README.md`, under Install: download the `.pkg` from Releases and open it. While releases are unsigned, a subsection
  gives the Open Anyway steps, with no Terminal command. Also how to uninstall, and the note for 0.1.x users.
- `docs/RELEASING.md`:
  - how to request both Developer ID certificates and export them as one `.p12`;
  - that the signed path has never run until the secrets exist;
  - this checklist, before the first signed release and before the first unsigned one: install on a Mac that has
    never had Drosera; find Drosera as an AU in Logic and as a VST3 in another host; open the Standalone app and
    confirm it asks for microphone access; uninstall by following the Conclusion screen.
- `2026-11-01-repository-and-release-design.md` gets a note at the top that this design replaces its zip and its
  non-goals on installers and on AU and Standalone releases.
- `CLAUDE.md`: the `build_pkg.sh` and `check_pkg.sh` commands, and `tools/package/` in the code map.
- `CHANGELOG.md`, under Unreleased: the installer, the AU and Standalone formats, and the note for 0.1.x users.

## Risks

- **The signed path cannot run until the secrets exist.** CI exercises everything else on every push. The first
  signed release may need debugging, and `RELEASING.md` says so.
- **Gatekeeper may tighten.** The probe holds for macOS 27.2. If a later macOS removes Open Anyway, unsigned releases
  stop being installable without Terminal, and paying for the account becomes the only route.
