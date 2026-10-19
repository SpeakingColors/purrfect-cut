// Purrfect Cut: the app. Editing model in src/core/EditDoc.h. This window is a plain JUCE one for now (buttons, a waveform, play);
// the cat-ui window from docs/mockup is on the ROADMAP.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../core/EditDoc.h"
#include "../core/View.h"
#include <atomic>

using namespace purrfect;

// Plays the current document (or the selection) straight from the model; the audio thread only reads an immutable buffer.
class DocPlayer : public juce::AudioSource
{
public:
    void prepareToPlay(int, double sr) override { deviceRate = sr; }
    void releaseResources() override {}
    // The player reads the document's buffer directly (no copy), so stop() must be called before every edit. It takes the lock,
    // so once it returns the audio thread is not touching the buffer and will not until start() is called again.
    void start(const Audio* a, double docRate, size_t from, size_t to) { const juce::SpinLock::ScopedLockType l(lock); audio = a; ratio = docRate / deviceRate; pos = (double) from; end = (double) to; playing = true; }
    void stop() { const juce::SpinLock::ScopedLockType l(lock); playing = false; audio = nullptr; }
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
    juce::SpinLock lock; const Audio* audio = nullptr; double deviceRate = 44100, ratio = 1, pos = 0, end = 0; std::atomic<bool> playing { false };
};

class Wave : public juce::Component
{
public:
    explicit Wave(EditDoc& d) : doc(d) {}
    std::function<void()> onChange;
    View view;
    void sync() { view.setSize(doc.size(), std::max(1, getWidth())); }
    void resized() override { const bool fitted = view.zoomedOut(); sync(); if (fitted) view.fit(); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141a1c)); if (doc.empty()) { g.setColour(juce::Colours::grey); g.drawText("Open a file to start", getLocalBounds(), juce::Justification::centred); return; }
        sync(); const int w = getWidth(), h = getHeight();
        if (doc.hasSel()) { const int a = xOf(doc.selA), b = xOf(doc.selB); g.setColour(juce::Colour(0x5540c8a0)); g.fillRect(juce::jmax(0, a), 0, juce::jmax(1, juce::jmin(w, b) - juce::jmax(0, a)), h); }
        g.setColour(juce::Colour(0xff7fd8c0));
        if (view.spp >= 1.0)
        {
            const size_t a = (size_t) std::max(0.0, view.start), b = (size_t) std::min((double) doc.size(), view.start + view.visible());
            std::vector<float> mn, mx; doc.peaks(a, b, w, mn, mx);
            for (int x = 0; x < w; ++x) g.drawVerticalLine(x, (float) h * (0.5f - 0.5f * mx[(size_t) x]), (float) h * (0.5f - 0.5f * mn[(size_t) x]) + 1.f);
        }
        else   // zoomed past one sample per pixel: draw the samples as a line, with dots
        {
            const Audio* au = doc.audio(); juce::Path p; bool first = true;
            for (size_t i = (size_t) std::max(0.0, view.start); i < doc.size() && view.xOf((double) i) < w + 1; ++i)
            { const float x = (float) view.xOf((double) i), y = (float) h * (0.5f - 0.5f * au->L[i]); if (first) { p.startNewSubPath(x, y); first = false; } else p.lineTo(x, y); if (view.spp < 0.2) g.fillEllipse(x - 2.f, y - 2.f, 4.f, 4.f); }
            g.strokePath(p, juce::PathStrokeType(1.2f));
        }
        const int cx = xOf(doc.cursor); if (cx >= 0 && cx < w) { g.setColour(juce::Colours::white); g.drawVerticalLine(cx, 0.f, (float) h); }
        // dB scale: 0, -6, -12, -24 (both sides)
        g.setColour(juce::Colour(0x66ffffff)); g.setFont(11.f);
        for (float db : { 0.f, -6.f, -12.f, -24.f }) { const float a = std::pow(10.f, db / 20.f); for (int sgn : { -1, 1 }) { const float y = (float) h * (0.5f - 0.5f * a * (float) sgn); g.drawHorizontalLine((int) y, 0.f, (float) w); } g.drawText(juce::String((int) db) + " dB", 3, (int) ((float) h * (0.5f - 0.5f * a)) + 1, 50, 12, juce::Justification::left); }
    }
    void mouseDown(const juce::MouseEvent& e) override { sync(); anchor = sOf(e.x); doc.setCursor(anchor); doc.select(anchor, anchor); notify(); }
    void mouseDrag(const juce::MouseEvent& e) override { doc.select(anchor, sOf(e.x)); notify(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { doc.selectAll(); notify(); }
    // wheel: zoom around the pointer; with shift (or a sideways wheel) it scrolls
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override
    {
        if (doc.empty()) return; sync();
        if (e.mods.isShiftDown() || std::abs(d.deltaX) > std::abs(d.deltaY)) view.pan(-(std::abs(d.deltaX) > std::abs(d.deltaY) ? d.deltaX : d.deltaY) * 200.0);
        else view.zoomAt(d.deltaY > 0 ? 0.8 : 1.25, e.x);
        repaint();
    }
    void zoom(double factor) { sync(); view.zoomAt(factor, getWidth() * 0.5); repaint(); }
    void fit() { sync(); view.fit(); repaint(); }
    void showSelection() { sync(); if (doc.hasSel()) view.show(doc.selA, doc.selB); repaint(); }
    void follow() { if (doc.empty() || view.zoomedOut()) return; sync(); const double x = view.xOf((double) doc.cursor); if (x < 0 || x > getWidth()) view.centreOn((double) doc.cursor); }
    void notify() { repaint(); if (onChange) onChange(); }
private:
    int xOf(size_t s) const { return (int) std::floor(view.xOf((double) s)); }
    size_t sOf(int x) const { return (size_t) juce::jlimit(0.0, (double) doc.size(), view.sampleAt((double) x)); }
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
        add("Zoom in", [this] { wave.zoom(0.5); }); add("Zoom out", [this] { wave.zoom(2.0); }); add("Fit", [this] { wave.fit(); }); add("Zoom to selection", [this] { wave.showSelection(); });
        add("Cut", [this] { edit([&] { doc.cut(); }); }); add("Copy", [this] { edit([&] { doc.copy(); }); }); add("Paste", [this] { edit([&] { doc.paste(); }); }); add("Delete", [this] { edit([&] { doc.del(); }); }); add("Trim", [this] { edit([&] { doc.trim(); }); });
        add("Fade in", [this] { edit([&] { doc.fadeIn(FadeCurve::SCurve); }); }); add("Fade out", [this] { edit([&] { doc.fadeOut(FadeCurve::SCurve); }); });
        add("Normalize", [this] { edit([&] { doc.normalize(-1.f); }); }); add("Reverse", [this] { edit([&] { doc.reverse(); }); });
        add("Undo", [this] { edit([&] { doc.undo(); }); }); add("Redo", [this] { edit([&] { doc.redo(); }); });
        setSize(980, 520); startTimerHz(20); refresh();
    }
    ~MainComponent() override { dm.removeAudioCallback(&player); player.setSource(nullptr); }
    void resized() override { auto r = getLocalBounds().reduced(8); const int per = 8; int i = 0; auto row = r.removeFromTop(30); for (auto& b : buttons) { if (i && i % per == 0) { r.removeFromTop(4); row = r.removeFromTop(30); } b->setBounds(row.removeFromLeft(110).reduced(2)); ++i; } status.setBounds(r.removeFromBottom(22)); wave.setBounds(r.reduced(0, 6)); }
    void timerCallback() override { if (docPlayer.isPlaying()) { doc.setCursor((size_t) docPlayer.position()); wave.follow(); wave.repaint(); } }
private:
    void add(const char* name, std::function<void()> f) { auto* b = buttons.add(new juce::TextButton(name)); b->onClick = std::move(f); addAndMakeVisible(b); }
    void changed() { wave.repaint(); refresh(); }
    template <class F> void edit(F f) { docPlayer.stop(); f(); changed(); }
    void refresh() { status.setText(doc.empty() ? "No file" : juce::String(doc.seconds(), 2) + " s" + (doc.hasSel() ? ", selection " + juce::String((double) (doc.selB - doc.selA) / doc.sampleRate, 3) + " s" : "") + (doc.dirty ? " (edited)" : ""), juce::dontSendNotification); }
    void play() { if (doc.empty()) return; const size_t a = doc.hasSel() ? doc.selA : doc.cursor, b = doc.hasSel() ? doc.selB : doc.size(); docPlayer.start(doc.audio(), doc.sampleRate, a, b); }
    void open()
    {
        chooser = std::make_unique<juce::FileChooser>("Open audio", juce::File(), fm.getWildcardForAllFormats());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            docPlayer.stop(); if (!c.getResult().existsAsFile()) return; std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(c.getResult())); if (!r || r->lengthInSamples > (juce::int64) r->sampleRate * 1800) return;
            juce::AudioBuffer<float> b((int) std::max<unsigned>(2, r->numChannels), (int) r->lengthInSamples); b.clear(); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true);
            if (r->numChannels == 1) b.copyFrom(1, 0, b, 0, 0, b.getNumSamples());
            doc.set(std::vector<float>(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples()), std::vector<float>(b.getReadPointer(1), b.getReadPointer(1) + b.getNumSamples()), r->sampleRate); wave.fit(); changed(); });
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
