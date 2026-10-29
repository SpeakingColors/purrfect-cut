// Purrfect Cut sampler (VST3 and standalone). The audio engine is src/core/Sampler.h; this file only connects it to JUCE:
// parameters (every row of the table in src/core/Params.h is an automatable host parameter), MIDI, the sample and state.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include "../core/Params.h"
#include "../core/Sampler.h"

class PurrfectSamplerProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PurrfectSamplerProcessor();
    ~PurrfectSamplerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Purrfect Cut Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // sample handling (message thread)
    bool loadSample(const juce::File& f);
    std::shared_ptr<const purrfect::Audio> sample() const { const juce::SpinLock::ScopedLockType l(lock); return ui; }
    juce::String samplePath() const { return path; }
    int sampleVersion() const { return version.load(); }
    // where the slices are right now (for drawing); empty unless the mode is Slices
    purrfect::SliceMap currentSlices() const { return maps[live.load()]; }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyState;   // the on-screen keys; merged into the MIDI input in processBlock

private:
    void timerCallback() override;
    void recomputeSlices();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    mutable juce::SpinLock lock;
    std::shared_ptr<const purrfect::Audio> ui, pending, cur;   // ui: message thread copy; pending/cur: hand-over to the audio thread
    double pendingRate = 44100, sourceRate = 44100; bool pendingFlag = false; std::atomic<int> version { 0 };
    juce::String path;

    purrfect::SampleEngine engine; purrfect::SliceMap maps[2]; std::atomic<int> live { 0 }, sliceSerial { 0 }; int seenSerial = 0;
    std::atomic<float>* raw[purrfect::kParamCount] {}; float P[purrfect::kParamCount] {};
    float lastSliceKey[4] { -1, -1, -1, -1 }; int lastSliceVer = -1;
    juce::AudioFormatManager fm; double hostRate = 44100; bool sustainDown = false;
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PurrfectSamplerProcessor)
};
