# Roadmap: Purrfect Cut

Audio editor.

## Done
- [x] Editing model in pure C++ with unit tests (edits, undo / redo, peaks, WAV read and write)
- [x] JUCE desktop app with waveform, selection, edits, play and save, CI (core tests, configure, build)
- [x] Cheap edits (store only the changed part), peak pyramid, zoom and scroll
- [x] Sampler engine with FX chain (drive, squash, duck, delay, reverb), tested
- [x] VST3 and standalone sampler, all controls automatable, pluginval level 5 on Linux and Windows in CI
- [x] Interactive mockup v3 with Editor, Sampler and FX tabs
- [x] Pitch tools in the core with tests: YIN and pYIN pitch tracking, PSOLA auto-tune, harmony, robot, note segmentation, chord detector, spectral shifter, note document and session, offline renderer (34 more automatable parameters)
- [x] Release packaging: installers (Windows, macOS, Linux Flatpak), uninstallers, PDF manual (test builds, not tested on real machines)
- [x] Sampler plugin window on cat-ui (wood cheeks, tabs Sampler / FX / Auto-Tune / Harmony, waveform with markers, on-screen keys); written 2026-10-29, compiled by CI, not yet looked at in a real window
- [ ] Plugin window: info, spectrum and level panels, undo and automation list buttons (as in the mockup)

## Next
- [ ] Try it on real files and long files by ear
- [ ] Selection snapping to zero crossings
- [ ] Sample editing inside the plugin (load, trim and loop on the waveform), then the editor as a VST3 too
- [ ] cat-ui window from the mockup (app and plugin) (wood cheeks, spectrogram, meters with dB scales) drawn through `catui::RenderGate`
- [ ] Other export formats (FLAC, OGG) and a settings page
- [ ] Hand-off to and from CATalog

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%
