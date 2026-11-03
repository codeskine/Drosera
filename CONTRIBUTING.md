# Contributing

Contributions are welcome, and are accepted under the project's license, AGPLv3.

See [docs/BUILDING.md](docs/BUILDING.md) for how to build and test. CI must pass. If you change `tools/package/` or
the workflows, also run `tools/package/test_package.sh build/Drosera_artefacts/Release`.

## Rules

1. No allocation, locks or file access on the audio thread.
2. With the plugin-only features off, renders stay byte-identical to `tools/hardware-comparison/reference/SHA256SUMS`.
   A change that alters them needs a stated reason and new checksums.
3. MIDI tuning is judged by the dominant partial, never by a fit to the note's harmonic grid: every partial of a
   note fits that grid, so such a test passes while the wrong harmonic sounds.
4. `third_party/` changes are recorded in that component's `PATCHES.txt`.
5. Parameter IDs never change, or saved projects break. Parameter names stay in English everywhere.

Add user-visible changes to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md).
