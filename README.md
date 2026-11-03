# Drosera

![Drosera's window: a sundew whose tentacles show the sound's spectrum, Feedback at its heart, Frequency and Body beside it, and one row of filter, reverb and echo controls below](docs/images/drosera.png)

A feedback drone synth for macOS 11 or newer, on Apple silicon and Intel. It comes as a VST3 plugin, an Audio Unit and a
standalone app.

Drosera makes sound on its own: a noise floor feeds two string resonators inside a saturating loop, and you steer
what the loop settles on. Play it with its knobs, excite it with audio, or put it on a note with MIDI, up to three
notes at once. It comes with 16 presets.

## Install

1. Download `Drosera-X.Y.Z-macos.pkg` from [Releases](https://github.com/codeskine/Drosera/releases).
2. Open it and follow the installer. It asks for an administrator password, because it installs for every user on
   the Mac:

   | Format | Installed to |
   |---|---|
   | VST3 | `/Library/Audio/Plug-Ins/VST3/Drosera.vst3` |
   | Audio Unit | `/Library/Audio/Plug-Ins/Components/Drosera.component` |
   | Standalone app | `/Applications/Drosera.app` |

   All three are selected. To leave one out, click **Customize** at the Installation Type step.
3. Rescan plugins in your host. Drosera loads as an instrument.

### If macOS blocks the installer

Releases are not notarized by Apple yet, so the first time you open the installer macOS says it could not verify that
the installer is free of malware, and does not open it. Then:

1. Click **Done** in that warning. Do not move the file to the Bin.
2. Open **System Settings → Privacy & Security** and scroll down to Security. A line says the installer was blocked.
   Click **Open Anyway** and confirm with your password or Touch ID.
3. The installer opens. Install as above.

You do this once per downloaded installer. The plugins and the app it installs open normally, with no Terminal
command.

### Upgrading from 0.1.x

Version 0.1.x came as a zip, and its VST3 was copied by hand to `~/Library/Audio/Plug-Ins/VST3/`, in your home
folder. Delete `Drosera.vst3` there, or your host finds two copies of Drosera. `~/Library` is hidden in Finder: use
**Go → Go to Folder** and type the path.

Later versions install over the earlier one. Open the new installer and nothing else is needed.

### Uninstalling

Delete these three items, and empty the Bin:

- `/Library/Audio/Plug-Ins/VST3/Drosera.vst3`
- `/Library/Audio/Plug-Ins/Components/Drosera.component`
- `/Applications/Drosera.app`

The standalone app also keeps its audio settings in `~/Library/Application Support/Drosera.settings`. Delete it too to
leave nothing behind. Projects saved in your host keep their own copy of Drosera's settings.

## Using it

Everything is on one page. In the middle, a sundew shows the sound's real spectrum: each tentacle is a frequency band,
from the lowest at the top, clockwise, and it unfurls as its band sounds. Feedback sits at its heart, with Frequency and
Body beside it. Below, one row holds the filters, the reverb and the echo, with its PONG controls. Red controls are
inside the feedback loop and change what the drone sustains; amber ones come after it and colour what comes out.
Strings, Output and MIDI open from the SETUP field. Drag the window's corner to scale it from 75 % to 150 %.

The standalone app plays Drosera without a host. Its **Options** button, top left, opens the audio and MIDI settings,
where you choose the output, an audio input and MIDI devices. The audio input starts muted, to avoid a loop from the
speakers to the microphone: untick **Mute audio input** there to use it. The first time the app opens, macOS asks
whether it may use the microphone.

See the [manual](docs/MANUAL.md) for every control, MIDI, and the known limits.

## Status

Validated on macOS by automated tests, by `pluginval` on the VST3 and the Audio Unit, and by Apple's `auval` on the Audio
Unit. Not yet compared with a hardware unit. Releases are not yet signed and notarized by Apple.

## For developers

- [Building and testing](docs/BUILDING.md)
- [Contributing](CONTRIBUTING.md)
- [Releasing](docs/RELEASING.md)
- [Changelog](CHANGELOG.md)

## Credits

Derived from the MIT-licensed firmware of [Audrey II](https://github.com/Synthux-Academy/Audrey-II), by Nick Donaldson
(Synthux Academy), for Daisy Seed. Drosera is an independent project, not affiliated with or endorsed by Synthux
Academy, and uses none of their artwork.

## License

Copyright 2026 codeskine. AGPLv3, see [LICENSE](LICENSE). Third-party code keeps its own terms: see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
