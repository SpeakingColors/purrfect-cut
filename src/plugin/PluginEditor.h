// Purrfect Cut sampler editor: a plain JUCE panel generated from the parameter table, so every automatable control is on screen.
// The cat-ui look from the mockup is on the roadmap.
#pragma once
#include "PluginProcessor.h"

class WaveStrip : public juce::Component
{
public:
    explicit WaveStrip(PurrfectSamplerProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
private:
    PurrfectSamplerProcessor& proc;
};

class PurrfectSamplerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PurrfectSamplerEditor(PurrfectSamplerProcessor&);
    ~PurrfectSamplerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Ctl;
    struct Panel;
    void timerCallback() override;
    PurrfectSamplerProcessor& proc;
    juce::TextButton load { "Load sample" }; juce::Label name; WaveStrip wave;
    std::unique_ptr<Panel> panel; juce::Viewport view; std::unique_ptr<juce::FileChooser> chooser; int seenVersion = -1;
};
