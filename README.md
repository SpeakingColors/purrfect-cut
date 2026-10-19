# Purrfect Cut ✂️🐈

<img src="assets/purrfect-cut-logo.png" width="160" alt="logo">

Audio editor as VST and desktop app from one install (Linux, Windows, macOS).

## Status
Early. What exists today:
- A tested pure C++ editing model (`src/core`): cut, copy, paste, delete, trim, silence, fades (linear, exponential, s-curve), gain, normalise, reverse, invert, remove DC, 25 step undo and redo, peak data for drawing, and WAV read / write (16 bit with dither, 24 bit, 32 bit float).
- A JUCE desktop app (`src/app`): open a file, see the waveform with a dB scale, select, edit, play, undo, save as 24 bit WAV. Plain JUCE buttons for now.
- Not yet: the cat-ui window from the [mockup](docs/mockup/) (wood cheeks, zoom, spectrogram, meters), the VST3 version, zoom and scroll, recording, the hand-off to and from CATalog. Nothing here has been tried by ear or on long files yet; every edit makes a full copy of the audio, so very long files use a lot of memory.

Run the model tests: `sh tests/run.sh` (needs only g++).

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
