# Purrfect Cut ✂️🐈

<img src="assets/purrfect-cut-logo.png" width="160" alt="logo">

Audio editor as VST and desktop app from one install (Linux, Windows, macOS).

## What it is
An audio editor first: open a file, cut, copy, paste, trim, silence, fade, gain, normalise, reverse, invert, remove DC, undo, save WAV. It also has three pitch pages for single voices and leads: Auto-Tune (key, scale, retune speed, humanize, note mask, formant keep, vibrato keep, robot), a Melodyne-style Pitch Editor (YIN note blobs on a piano roll: drag pitch and time, snap to scale, vibrato, drift, formant per note), and a Harmonizer. One note at a time only (no chords); pitch detection is unreliable below about 75 Hz.
It is also a **sampler with FX**: load a sample, play it from MIDI (keys or slices) with an amp envelope, filter, loop, and a chain of drive, squash, duck, delay and reverb.

## Status
Early. What exists today:
- **Core (`src/core`, pure C++, tested):** the editing model with cheap undo (25 steps, an edit stores only the part it changed), a peak pyramid for long files, zoom maths, WAV read and write (16 bit with dither, 24 bit, 32 bit float), and the sampler engine (16 voices, ADSR, state variable filter, loop with crossfade, even or transient slices, mono glide) with the FX chain.
- **Desktop app (`src/app`):** open a file, waveform with a dB scale, zoom and scroll (mouse wheel zooms around the pointer, shift or sideways wheel scrolls, buttons for zoom in / out / fit / selection; zoom goes down to 1/32 sample per pixel), select, edit, play, undo, save as 24 bit WAV.
- **VST3 sampler and standalone (`src/plugin`):** every control is a host automatable parameter (65 of them, listed in `src/core/Params.h`). Load a sample, play it by MIDI, optional sidechain input for the duck. Checked with pluginval level 5 on Linux and Windows in CI. Plain JUCE controls for now; the sample path is saved in the project, the audio is not.
- **Not yet:** the cat-ui window from the [mockup](docs/mockup/purrfectcut.html), spectrogram, recording, the hand-off to and from CATalog, the editor inside the plugin. Nothing here has been tried by ear or on very long files yet.

Run the core tests: `sh tests/run.sh` (needs only g++), or `ctest` after a CMake build.

<p align="center">
  <a href="https://ko-fi.com/studio87"><img src="https://img.shields.io/badge/Support_Studio_87-on_Ko--fi-FF5E5B?style=for-the-badge&logo=kofi&logoColor=white" alt="Support Studio 87 on Ko-fi"></a>
</p>

☕ Free and open source, made by one person. If it helps your music, a coffee on [Ko-fi](https://ko-fi.com/studio87) keeps it coming. 💜

## Docs
- [Interactive mockups](docs/README.md)
- [Roadmap](ROADMAP.md)

## License
AGPL-3.0-or-later, see [LICENSE](LICENSE).

Not affiliated with or endorsed by any hardware maker named here. All trademarks belong to their owners.
