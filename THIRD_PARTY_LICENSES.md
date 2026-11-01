# Third-party code and licenses

Read this before distributing a binary. Nothing here is legal advice.

| Component | Where | License | Notes |
|---|---|---|---|
| Audrey II firmware (Synthux Academy) | `Source/DSP/` (engine, string, echo, filters) | MIT, see `LICENSE-AudreyII` | The code only: Drosera uses neither the Audrey II name as a product name nor its panel or images. |
| DaisySP, commit 7f67768 | `third_party/DaisySP/` | MIT, see its `LICENSE` | Headers patched, see `PATCHES.txt`. |
| `ReverbSc`, `Tone` from that DaisySP | `third_party/DaisySP/Source/Effects/reverbsc.*`, `Filters/tone.*` | **Treat as LGPL-2.1** | See below. |
| octamad tape echo (Sam Banks), commit b8deefc | `third_party/tapeecho/` | MIT, see its `LICENSE` | Changes in `PATCHES.txt`. Its docs say it was voiced against measurements of a commercial plugin, with no code or samples taken, and name another project's `TapeEchoBlock` as a behavioural reference (licence not checked). Do not market it as a clone. |
| JUCE 8.0.9 | fetched by CMake | **AGPLv3 or commercial** | A binary built on it must comply with one of the two. |
| VST3 SDK | inside JUCE | Steinberg proprietary or GPLv3 | See JUCE's `LICENSE.md`. |

## ReverbSc and Tone

The pinned commit labels these files MIT. DaisySP removed them in December 2023 (commit `cfcb239`,
"Remove LGPL Modules") and now publishes them in DaisySP-LGPL under LGPL-2.1, crediting Sean Costello,
Istvan Varga and Paul Batchelor. The two versions have not been diffed; the safe reading is that the code
was LGPL all along. Audrey II's own README still says everything is MIT.

## Project license

This project is open source. The plugin as a whole is distributed under **AGPLv3** (`LICENSE`), the
open-source option of JUCE's dual license. In practice:

- Anyone who receives a binary must also be able to get the complete corresponding source, including
  `third_party/`, the build files and any changes.
- The MIT parts keep their own notices (`LICENSE-AudreyII`, `third_party/*/LICENSE`), and those files stay
  usable under MIT on their own.
- `ReverbSc` and `Tone`, if LGPL-2.1, may be used in a work under these terms; since the full source ships,
  a recipient can replace and rebuild them. The VST3 SDK comes in through its GPLv3 option, which GPLv3
  section 13 lets you combine with AGPLv3 code.

Still needed before publishing: a check of the tape echo's `TapeEchoBlock` reference noted above.
