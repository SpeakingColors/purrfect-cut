#include "PluginEditor.h"

using namespace purrfect;
static const juce::Colour kBg(0xff17181d), kPanel(0xff22242b), kAccent(0xffff5ca8), kText(0xffe8e8ee), kDim(0xff9a9cab), kWave(0xff7fd8c0);

void WaveStrip::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat(); g.setColour(juce::Colour(0xff0f1013)); g.fillRoundedRectangle(r, 6.f);
    auto a = proc.sample(); if (!a || a->size() < 2) { g.setColour(kDim); g.drawText("Load a sample to play it", getLocalBounds(), juce::Justification::centred); return; }
    const int w = getWidth(), h = getHeight(); const size_t n = a->size();
    g.setColour(kWave);
    for (int x = 0; x < w; ++x)
    {
        const size_t s0 = (size_t) ((double) x / w * (double) n), s1 = std::max(s0 + 1, std::min(n, (size_t) ((double) (x + 1) / w * (double) n))); const size_t step = std::max<size_t>(1, (s1 - s0) / 64);
        float mn = 0, mx = 0; for (size_t i = s0; i < s1; i += step) { mn = std::min(mn, a->L[i]); mx = std::max(mx, a->L[i]); }
        g.drawVerticalLine(x, (float) h * (0.5f - 0.5f * mx), (float) h * (0.5f - 0.5f * mn) + 1.f);
    }
    auto v = [&](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
    const float rs = v("sm_rs"), re = v("sm_re");
    g.setColour(juce::Colour(0x99000000)); g.fillRect(0.f, 0.f, rs * (float) w, (float) h); g.fillRect(re * (float) w, 0.f, (1.f - re) * (float) w, (float) h);   // outside the region
    if ((int) v("sm_mode") == MODE_SLICES)
    {
        const SliceMap m = proc.currentSlices(); g.setColour(kAccent); g.setFont(11.f);
        for (int i = 0; i <= m.n; ++i) { const int x = (int) ((double) m.pts[i] / (double) n * w); g.drawVerticalLine(x, 0.f, (float) h); if (i < m.n) g.drawText(juce::String(i + 1), x + 3, 2, 24, 12, juce::Justification::left); }
    }
    else if (v("sm_loop") > 0.5f)
    {
        const float len = re - rs, ls = rs + len * v("sm_ls"), le = rs + len * v("sm_le"); g.setColour(juce::Colour(0x4400d8ff)); g.fillRect(ls * (float) w, 0.f, std::max(1.f, (le - ls) * (float) w), (float) h);
    }
    g.setColour(juce::Colours::white); const float st = rs + (re - rs) * v("sm_start"); g.drawVerticalLine((int) (st * (float) w), 0.f, (float) h);
}

// One control with its name above it
struct PurrfectSamplerEditor::Ctl : juce::Component
{
    Ctl(juce::AudioProcessorValueTreeState& s, const ParamSpec& sp)
    {
        label.setText(sp.name, juce::dontSendNotification); label.setJustificationType(juce::Justification::centred); label.setFont(juce::FontOptions(12.f)); label.setColour(juce::Label::textColourId, kDim); addAndMakeVisible(label);
        if (sp.kind == Kind::Float)
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16); slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent); slider.setColour(juce::Slider::textBoxTextColourId, kText);
            slider.setNumDecimalPlacesToDisplay(sp.step >= 1.f ? 0 : (sp.step >= 0.1f ? 1 : 2)); slider.setDoubleClickReturnValue(true, sp.def); addAndMakeVisible(slider); sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, sp.id, slider);
        }
        else if (sp.kind == Kind::Switch) { toggle.setButtonText("On"); toggle.setColour(juce::ToggleButton::textColourId, kText); toggle.setColour(juce::ToggleButton::tickColourId, kAccent); addAndMakeVisible(toggle); ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, sp.id, toggle); }
        else { combo.addItemList(juce::StringArray::fromTokens(sp.choices, "|", ""), 1); addAndMakeVisible(combo); ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, sp.id, combo); }
        setTitle(sp.name);
    }
    void resized() override
    {
        auto r = getLocalBounds(); label.setBounds(r.removeFromTop(16));
        slider.setBounds(r);
        toggle.setBounds(r.withSizeKeepingCentre(r.getWidth() - 4, 24)); combo.setBounds(r.withSizeKeepingCentre(r.getWidth() - 6, 26));
    }
    juce::Label label; juce::Slider slider; juce::ToggleButton toggle; juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
};

// All controls, grouped by the "group" column of the table, flowing left to right
struct PurrfectSamplerEditor::Panel : juce::Component
{
    explicit Panel(juce::AudioProcessorValueTreeState& s)
    {
        const ParamSpec* t = paramTable(); juce::String last;
        for (int i = 0; i < kParamCount; ++i)
        {
            if (last != t[i].group) { last = t[i].group; auto* l = heads.add(new juce::Label({}, last)); l->setFont(juce::FontOptions(15.f, juce::Font::bold)); l->setColour(juce::Label::textColourId, kAccent); addAndMakeVisible(l); items.push_back({ l, nullptr }); }
            auto* c = ctls.add(new Ctl(s, t[i])); addAndMakeVisible(c); items.push_back({ nullptr, c });
        }
    }
    int layoutFor(int w)
    {
        int x = 0, y = 0, rowH = 0; const int cw = 92, ch = 96; bool newGroup = true;
        for (auto& it : items)
        {
            if (it.head) { if (x > 0) { y += rowH; } x = 0; rowH = 0; it.head->setBounds(0, y + 4, w, 22); y += 28; newGroup = true; continue; }
            const int need = it.ctl->combo.getNumItems() > 0 ? 150 : cw;
            if (x + need > w && x > 0) { x = 0; y += rowH; rowH = 0; }
            it.ctl->setBounds(x, y, need, ch); x += need; rowH = ch; (void) newGroup;
        }
        return y + rowH + 10;
    }
    struct Item { juce::Label* head; Ctl* ctl; };
    std::vector<Item> items; juce::OwnedArray<juce::Label> heads; juce::OwnedArray<Ctl> ctls;
};

PurrfectSamplerEditor::PurrfectSamplerEditor(PurrfectSamplerProcessor& p) : AudioProcessorEditor(&p), proc(p), wave(p)
{
    panel = std::make_unique<Panel>(proc.apvts); view.setViewedComponent(panel.get(), false); view.setScrollBarsShown(true, false);
    addAndMakeVisible(load); addAndMakeVisible(name); addAndMakeVisible(wave); addAndMakeVisible(view);
    name.setColour(juce::Label::textColourId, kText); name.setJustificationType(juce::Justification::centredLeft);
    load.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load a sample", juce::File(proc.samplePath()).getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            if (c.getResult().existsAsFile() && !proc.loadSample(c.getResult())) name.setText("Could not read that file (up to 10 minutes of audio)", juce::dontSendNotification); });
    };
    setResizable(true, true); setResizeLimits(560, 420, 1600, 1200); setSize(820, 640); startTimerHz(10);
}
PurrfectSamplerEditor::~PurrfectSamplerEditor() { stopTimer(); }

void PurrfectSamplerEditor::paint(juce::Graphics& g) { g.fillAll(kBg); }
void PurrfectSamplerEditor::resized()
{
    auto r = getLocalBounds().reduced(10); auto top = r.removeFromTop(30); load.setBounds(top.removeFromLeft(120)); top.removeFromLeft(10); name.setBounds(top);
    r.removeFromTop(8); wave.setBounds(r.removeFromTop(120)); r.removeFromTop(8); view.setBounds(r);
    const int w = std::max(100, view.getMaximumVisibleWidth()); panel->setSize(w, panel->layoutFor(w));
}
void PurrfectSamplerEditor::timerCallback()
{
    if (seenVersion != proc.sampleVersion()) { seenVersion = proc.sampleVersion(); const juce::File f(proc.samplePath()); name.setText(f.getFileName().isEmpty() ? "No sample loaded" : f.getFileName(), juce::dontSendNotification); }
    wave.repaint();
}
