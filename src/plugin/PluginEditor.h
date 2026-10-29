// Purrfect Cut sampler window: built from the Sampler, FX, Auto-Tune and Harmony tabs of docs/mockup/purrfectcut.html.
// Wood cheeks, header, tabs, waveform with the region, loop and slice markers, a section for every group of the parameter
// table (src/core/Params.h) and the on-screen keys. Fixed aspect ratio, resizable from every corner and edge.
// Not built yet (see ROADMAP): the info, spectrum and level panels, the Editor and Pitch Editor tabs (those are the desktop app).
#pragma once
#include "PluginProcessor.h"

class PurrfectSamplerEditor : public juce::AudioProcessorEditor
{
public:
    explicit PurrfectSamplerEditor(PurrfectSamplerProcessor&);
    ~PurrfectSamplerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
