# Roadmap: Purrfect Cut

Audio editor.

## Done
- [x] Editing model in pure C++ with unit tests (edits, undo / redo, peaks, WAV read and write)
- [x] JUCE desktop app with waveform, selection, edits, play and save, CI (core tests, configure, build)

## Next
- [ ] Try it on real files and long files; make edits cheaper in memory (store changed ranges instead of full copies)
- [ ] Zoom and scroll, selection snapping to zero crossings
- [ ] cat-ui window from the mockup (wood cheeks, spectrogram, meters with dB scales) drawn through `catui::RenderGate`
- [ ] Other export formats (FLAC, OGG) and a settings page
- [ ] VST3 version from the same code
- [ ] Hand-off to and from CATalog

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%
