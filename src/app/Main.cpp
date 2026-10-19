// Purrfect Cut: the app. Editing model in src/core/EditDoc.h. This window is a plain JUCE one for now (buttons, a waveform, play);
// the cat-ui window from docs/mockup is on the ROADMAP.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../core/EditDoc.h"
#include <atomic>

using namespace purrfect;

// Plays the current document (or the selection) straight from the model; the audio thread only reads an immutable buffer.
class DocPlayer : public juce::AudioSource
{
public:
    void prepareToPlay(int, double sr) override { deviceRate = sr; }
    void releaseResources() override {}
    void start(AudioPtr a, double docRate, size_t from, size_t to) { const juce::SpinLock::ScopedLockType l(lock); audio = std::move(a); ratio = docRate / deviceRate; pos = (double) from; end = (double) to; playing = true; }
    void stop() { playing = false; }
    bool isPlaying() const { return playing; }
    double position() const { return pos; }
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();
        const juce::SpinLock::ScopedTryLockType l(lock); if (!l.isLocked() || !playing || !audio) return;
        auto* L = info.buffer->getWritePointer(0, info.startSample); auto* R = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
        for (int i = 0; i < info.numSamples; ++i)
        {
            if (pos >= end - 1) { playing = false; break; }
            const size_t k = (size_t) pos; const float f = (float) (pos - (double) k);
            L[i] = audio->L[k] + f * (audio->L[k + 1] - audio->L[k]); if (R) R[i] = audio->R[k] + f * (audio->R[k + 1] - audio->R[k]);
            pos += ratio;
        }
    }
private:
    juce::SpinLock lock; AudioPtr audio; double deviceRate = 44100, ratio = 1, pos = 0, end = 0; std::atomic<bool> playing { false };
};

class Wave : public juce::Component
{
public:
    explicit Wave(EditDoc& d) : doc(d) {}
    std::function<void()> onChange;
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141a1c)); if (doc.empty()) { g.setColour(juce::Colours::grey); g.drawText("Open a file to start", getLocalBounds(), juce::Justification::centred); return; }
        const int w = getWidth(), h = getHeight(); std::vector<float> mn, mx; doc.peaks(0, doc.size(), w, mn, mx);
        if (doc.hasSel()) { g.setColour(juce::Colour(0x5540c8a0)); g.fillRect(xOf(doc.selA), 0, std::max(1, xOf(doc.selB) - xOf(doc.selA)), h); }
        g.setColour(juce::Colour(0xff7fd8c0)); for (int x = 0; x < w; ++x) g.drawVerticalLine(x, (float) h * (0.5f - 0.5f * mx[(size_t) x]), (float) h * (0.5f - 0.5f * mn[(size_t) x]) + 1.f);
        g.setColour(juce::Colours::white); g.drawVerticalLine(xOf(doc.cursor), 0.f, (float) h);
        // dB scale on the left: 0, -6, -12, -24 (both sides)
        g.setColour(juce::Colour(0x66ffffff)); g.setFont(11.f);
        for (float db : { 0.f, -6.f, -12.f, -24.f }) { const float a = std::pow(10.f, db / 20.f); for (int sgn : { -1, 1 }) { const float y = (float) h * (0.5f - 0.5f * a * (float) sgn); g.drawHorizontalLine((int) y, 0.f, (float) w); } g.drawText(juce::String((int) db) + " dB", 3, (int) ((float) h * (0.5f - 0.5f * a)) + 1, 50, 12, juce::Justification::left); }
    }
    void mouseDown(const juce::MouseEvent& e) override { anchor = sOf(e.x); doc.setCursor(anchor); doc.select(anchor, anchor); notify(); }
    void mouseDrag(const juce::MouseEvent& e) override { doc.select(anchor, sOf(e.x)); notify(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { doc.selectAll(); notify(); }
    void notify() { repaint(); if (onChange) onChange(); }
private:
    int xOf(size_t s) const { return doc.size() ? (int) ((double) s / (double) doc.size() * (getWidth() - 1)) : 0; }
    size_t sOf(int x) const { return (size_t) juce::jlimit(0.0, 1.0, (double) x / (double) std::max(1, getWidth() - 1)) * doc.size(); }
    EditDoc& doc; size_t anchor = 0;
};

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent() : wave(doc)
    {
        fm.registerBasicFormats(); dm.initialiseWithDefaultDevices(0, 2); player.setSource(&docPlayer); dm.addAudioCallback(&player);
        addAndMakeVisible(wave); addAndMakeVisible(status); wave.onChange = [this] { refresh(); };
        add("Open", [this] { open(); }); add("Save WAV", [this] { save(); });
        add("Play", [this] { play(); }); add("Stop", [this] { docPlayer.stop(); });
        add("Cut", [this] { doc.cut(); changed(); }); add("Copy", [this] { doc.copy(); changed(); }); add("Paste", [this] { doc.paste(); changed(); }); add("Delete", [this] { doc.del(); changed(); }); add("Trim", [this] { doc.trim(); changed(); });
        add("Fade in", [this] { doc.fadeIn(FadeCurve::SCurve); changed(); }); add("Fade out", [this] { doc.fadeOut(FadeCurve::SCurve); changed(); });
        add("Normalize", [this] { doc.normalize(-1.f); changed(); }); add("Reverse", [this] { doc.reverse(); changed(); });
        add("Undo", [this] { doc.undo(); changed(); }); add("Redo", [this] { doc.redo(); changed(); });
        setSize(980, 520); startTimerHz(20); refresh();
    }
    ~MainComponent() override { dm.removeAudioCallback(&player); player.setSource(nullptr); }
    void resized() override { auto r = getLocalBounds().reduced(8); const int per = 8; int i = 0; auto row = r.removeFromTop(30); for (auto& b : buttons) { if (i && i % per == 0) { r.removeFromTop(4); row = r.removeFromTop(30); } b->setBounds(row.removeFromLeft(110).reduced(2)); ++i; } status.setBounds(r.removeFromBottom(22)); wave.setBounds(r.reduced(0, 6)); }
    void timerCallback() override { if (docPlayer.isPlaying()) { doc.setCursor((size_t) docPlayer.position()); wave.repaint(); } }
private:
    void add(const char* name, std::function<void()> f) { auto* b = buttons.add(new juce::TextButton(name)); b->onClick = std::move(f); addAndMakeVisible(b); }
    void changed() { wave.repaint(); refresh(); }
    void refresh() { status.setText(doc.empty() ? "No file" : juce::String(doc.seconds(), 2) + " s" + (doc.hasSel() ? ", selection " + juce::String((double) (doc.selB - doc.selA) / doc.sampleRate, 3) + " s" : "") + (doc.dirty ? " (edited)" : ""), juce::dontSendNotification); }
    void play() { if (doc.empty()) return; const size_t a = doc.hasSel() ? doc.selA : doc.cursor, b = doc.hasSel() ? doc.selB : doc.size(); docPlayer.start(doc.audio(), doc.sampleRate, a, b); }
    void open()
    {
        chooser = std::make_unique<juce::FileChooser>("Open audio", juce::File(), fm.getWildcardForAllFormats());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            if (!c.getResult().existsAsFile()) return; std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(c.getResult())); if (!r || r->lengthInSamples > (juce::int64) r->sampleRate * 1800) return;
            juce::AudioBuffer<float> b((int) std::max<unsigned>(2, r->numChannels), (int) r->lengthInSamples); b.clear(); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true);
            if (r->numChannels == 1) b.copyFrom(1, 0, b, 0, 0, b.getNumSamples());
            doc.set(std::vector<float>(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples()), std::vector<float>(b.getReadPointer(1), b.getReadPointer(1) + b.getNumSamples()), r->sampleRate); changed(); });
    }
    void save()
    {
        if (doc.empty()) return; chooser = std::make_unique<juce::FileChooser>("Save as 24 bit WAV", juce::File(), "*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& c) {
            if (c.getResult() == juce::File()) return; const auto f = c.getResult().withFileExtension("wav");
            status.setText(writeWav(f.getFullPathName().toStdString(), *doc.audio(), doc.sampleRate, WavFormat::Pcm24) ? "Saved " + f.getFileName() : "Could not save " + f.getFileName(), juce::dontSendNotification); doc.dirty = false; });
    }
    EditDoc doc; Wave wave; juce::Label status; juce::OwnedArray<juce::TextButton> buttons; juce::AudioFormatManager fm; juce::AudioDeviceManager dm; DocPlayer docPlayer; juce::AudioSourcePlayer player; std::unique_ptr<juce::FileChooser> chooser;
};

class PurrfectCutApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Purrfect Cut"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window = nullptr; }
private:
    struct Window : juce::DocumentWindow
    {
        Window() : DocumentWindow("Purrfect Cut", juce::Colours::darkslategrey, allButtons) { setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true); setResizable(true, true); centreWithSize(getWidth(), getHeight()); setVisible(true); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(PurrfectCutApp)
