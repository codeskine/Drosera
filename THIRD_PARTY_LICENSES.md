# Third-party code and licenses

Read this before distributing a binary. Nothing here is legal advice.

| Component | Where | License | Notes |
|---|---|---|---|
| Audrey II firmware (Synthux Academy) | `src/dsp/` (engine, string, echo, filters) | MIT, see `LICENSE-AudreyII` | The code only: Drosera uses neither the Audrey II name as a product name nor its panel or images. |
| DaisySP, commit 7f67768 | `third_party/DaisySP/` | MIT, see its `LICENSE` | Headers patched, see `PATCHES.txt`. |
| `ReverbSc`, `Tone` from that DaisySP | `third_party/DaisySP/Source/Effects/reverbsc.*`, `Filters/tone.*` | **Treat as LGPL-2.1** | See below. |
| JUCE 8.0.9 | fetched by CMake | **AGPLv3 or commercial** | A binary built on it must comply with one of the two. |
| VST3 SDK | inside JUCE | Steinberg proprietary or GPLv3 | See JUCE's `LICENSE.md`. |
| Inter 4.1 (The Inter Project Authors) | `assets/fonts/Inter-*.ttf`, embedded in the plugin | SIL Open Font License 1.1, see `assets/fonts/OFL-Inter.txt` | The interface's text. Unmodified. |
| Cormorant Garamond 4.002 (the Cormorant Project Authors) | `assets/fonts/CormorantGaramond-Medium.otf`, embedded in the plugin | SIL Open Font License 1.1, see `assets/fonts/OFL-Cormorant.txt` | The name in the header. Unmodified. |

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
