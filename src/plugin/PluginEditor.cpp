#include "PluginEditor.h"
#include <catui/CatLookAndFeel.h>
#include <catui/RenderGate.h>
#include <catui/Logo.h>
#include <cmath>

using namespace purrfect;
using juce::Colour; using juce::Graphics; using juce::String;

namespace {
const Colour kBg(0xff101114), kPanel(0xff17181c), kEdge(0xff2c2e35), kText(0xffe8e6e1), kDim(0xff8c909a), kGold(0xffffd100), kPink(0xffff4fa0), kTeal(0xff3fd9c0), kPurple(0xffa97bff);
constexpr int kCheek = 30, kContentW = 1140, kBaseW = kContentW + 2 * kCheek, kBaseH = 996;
juce::Font mono(float h, bool bold = false) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font sans(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }

// Text shown under a knob, with the unit, as in the mockup
String valueText(const String& id, double v)
{
    static const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto pct = [&] { return String(juce::roundToInt(v * 100)) + " %"; };
    auto hz = [&] { return v >= 1000 ? String(v / 1000.0, 1) + " kHz" : String(juce::roundToInt(v)) + " Hz"; };
    auto st = [&] { const int i = juce::roundToInt(v); return (i > 0 ? "+" : "") + String(i) + " st"; };
    if (id == "sm_root") { const int m = juce::roundToInt(v); return String(nn[((m % 12) + 12) % 12]) + String(m / 12 - 1); }
    if (id == "sm_tune") return st();
    if (id == "sm_fine") return String(juce::roundToInt(v)) + " ct";
    if (id == "sm_bend") return String(v, 2) + " st";
    if (id == "sm_glide" || id == "sm_d" || id == "sm_r" || id == "de_time" || id == "rv_pre" || id == "pc_speed") return String(juce::roundToInt(v)) + " ms";
    if (id == "sm_a") return String(v, 1) + " ms";
    if (id == "sm_gain" || id == "out_gain") return String(v, 1) + " dB";
    if (id == "sm_pan") return std::abs(v) < 0.005 ? String("C") : (v < 0 ? "L " : "R ") + String(juce::roundToInt(std::abs(v) * 100));
    if (id == "sm_fc" || id == "de_damp" || id == "rv_damp") return hz();
    if (id == "sm_fq") return "Q " + String(v, 1);
    if (id == "sm_slices") return String(juce::roundToInt(v));
    if (id == "du_bpm") return String(juce::roundToInt(v)) + " BPM";
    if (id == "du_hz") return String(v, 2) + " Hz";
    if (id == "de_shift" || id == "rv_shift" || id == "pc_fshift" || id == "hm_i1" || id == "hm_i2" || id == "hm_i3") return st();
    if (id == "rv_size") return "x" + String(v, 2);
    if (id == "pc_ref") return String(v, 1) + " Hz";
    return pct();
}

const char* tipFor(const String& id)
{
    struct T { const char* id; const char* tip; };
    static const T t[] = {
        { "sm_mode", "Keys plays the sample across the keyboard. Slices maps one slice to each key from C3." },
        { "sm_poly", "Poly plays chords. Mono plays one note at a time." },
        { "sm_root", "The note at which the sample plays at its original pitch." },
        { "sm_tune", "Pitch in semitones." }, { "sm_fine", "Pitch in cents." }, { "sm_bend", "Pitch wheel range in semitones." }, { "sm_glide", "Slide time between notes in Mono." },
        { "sm_rs", "Where the played region starts." }, { "sm_re", "Where the played region ends." }, { "sm_start", "Where playback starts inside the region." },
        { "sm_loop", "Repeat the loop part while a key is held." }, { "sm_ls", "Loop start inside the region." }, { "sm_le", "Loop end inside the region." },
        { "sm_rev", "Play the sample backwards." }, { "sm_smode", "How slices are found: an even grid or at transients." }, { "sm_slices", "How many slices to cut the region into." },
        { "sm_a", "Fade in time." }, { "sm_d", "Fall time to the sustain level." }, { "sm_s", "Level held while the key is down." }, { "sm_r", "Fade out time after the key is released." },
        { "sm_gain", "Level of the sample in dB." }, { "sm_vel", "How much key velocity changes the level." }, { "sm_pan", "Stereo position." },
        { "sm_ft", "Filter type." }, { "sm_fc", "Filter cutoff frequency." }, { "sm_fq", "Resonance at the cutoff." }, { "sm_fe", "How much the amp envelope moves the cutoff." }, { "sm_fk", "How much the cutoff follows the key." },
        { "dr_d", "Drive amount." }, { "dr_x", "Transistor style saturation." }, { "dr_f", "Fuzz amount." }, { "dr_mix", "Dry and drive balance." },
        { "sq_depth", "How hard the sound is squashed." }, { "sq_mix", "Dry and squashed balance." },
        { "du_amt", "How deep the duck dips." }, { "du_shape", "Shape of the volume dip." }, { "du_rate", "Duck speed in note lengths, or Free in Hz." }, { "du_bpm", "Tempo used for the duck." }, { "du_hz", "Free duck speed." },
        { "du_where", "Which part of the sound is ducked." }, { "du_src", "Duck from the drawn shape or from the sidechain input." }, { "du_key", "Restart the duck shape at every key." },
        { "de_time", "Echo time." }, { "de_fb", "How much of the echo feeds back." }, { "de_shift", "Pitch shift of each repeat." }, { "de_shim", "Shimmer in the repeats." }, { "de_damp", "Darkens the repeats." }, { "de_mix", "Dry and echo balance." },
        { "rv_type", "Reverb type." }, { "rv_decay", "Tail length." }, { "rv_size", "Room size." }, { "rv_pre", "Gap before the reverb starts." }, { "rv_mod", "Movement in the tail." }, { "rv_damp", "Darkens the tail." },
        { "rv_shift", "Pitch of the shimmer in semitones." }, { "rv_shim", "Shimmer amount." }, { "rv_mix", "Dry and reverb balance." },
        { "out_width", "Stereo width, 0 is mono." }, { "out_gain", "Output level." }, { "fx_prev", "Send the sidechain input through the effects too." },
        { "pc_key", "Key of the song." }, { "pc_scale", "Scale used to pick the notes." }, { "pc_speed", "How fast the pitch snaps to the note." }, { "pc_amt", "How much correction is applied." },
        { "pc_hum", "Leaves a little natural drift." }, { "pc_form", "Keeps the vowel colour while the pitch moves." }, { "pc_fshift", "Moves the vowel colour in semitones." },
        { "pc_vib", "How much of the singer's vibrato is kept." }, { "pc_ref", "Tuning of A4." }, { "rb_note", "Note the robot voice sings." },
        { "hm_wet", "Harmony mix." }, { "hm_snap", "Keep the harmony notes inside the scale." },
    };
    for (const auto& e : t) if (id == e.id) return e.tip;
    return "";
}

class Pill : public juce::Button
{
public:
    explicit Pill(const String& t) : juce::Button(t) {}
    void paintButton(Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f); const bool on = getToggleState();
        g.setColour(on ? kPink.withAlpha(0.2f) : (over ? Colour(0xff25272e) : Colour(0xff1b1c21))); g.fillRoundedRectangle(r, 3.f);
        g.setColour(on ? kPink : Colour(0xff3a3d46)); g.drawRoundedRectangle(r, 3.f, on ? 1.5f : 1.f);
        g.setColour(on ? kPink : (down ? kGold : kText)); g.setFont(sans(13.f, true)); g.drawFittedText(getButtonText().toUpperCase(), getLocalBounds().reduced(3, 0), juce::Justification::centred, 2);
    }
};

class WavePane : public juce::Component
{
public:
    explicit WavePane(PurrfectSamplerProcessor& p) : proc(p) {}
    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xff060608)); g.setColour(kEdge); g.drawRect(getLocalBounds(), 1);
        auto a = proc.sample();
        if (!a || a->size() < 2) { g.setColour(kDim); g.setFont(sans(16.f)); g.drawFittedText("No sound loaded. Press Open file.", getLocalBounds(), juce::Justification::centred, 1); return; }
        const int w = getWidth() - 2, h = getHeight(); const size_t n = a->size(); const bool st = a->R.size() == n;
        auto lane = [&](const std::vector<float>& d, int top, int hh, Colour c)
        {
            g.setColour(c);
            for (int x = 0; x < w; ++x)
            {
                const size_t s0 = (size_t) ((double) x / w * (double) n), s1 = std::max(s0 + 1, std::min(n, (size_t) ((double) (x + 1) / w * (double) n))); const size_t step = std::max<size_t>(1, (s1 - s0) / 64);
                float mn = 0, mx = 0; for (size_t i = s0; i < s1; i += step) { mn = std::min(mn, d[i]); mx = std::max(mx, d[i]); }
                g.fillRect((float) x + 1, top + hh * (0.5f - 0.5f * mx), 1.f, std::max(1.f, hh * 0.5f * (mx - mn)));
            }
        };
        if (st) { lane(a->L, 0, h / 2, kPink); lane(a->R, h / 2, h / 2, kPurple); } else lane(a->L, 0, h, kPink);
        auto v = [&](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
        const float rs = v("sm_rs"), re = v("sm_re"), fw = (float) w;
        g.setColour(Colour(0xaa000000)); g.fillRect(1.f, 0.f, rs * fw, (float) h); g.fillRect(1.f + re * fw, 0.f, (1.f - re) * fw, (float) h);
        g.setColour(kGold.withAlpha(0.8f)); g.drawVerticalLine(1 + (int) (rs * fw), 0.f, (float) h); g.drawVerticalLine(1 + (int) (re * fw), 0.f, (float) h);
        if ((int) v("sm_mode") == MODE_SLICES)
        {
            const SliceMap m = proc.currentSlices(); g.setColour(kTeal); g.setFont(mono(11.f));
            for (int i = 0; i <= m.n; ++i) { const int x = 1 + (int) ((double) m.pts[i] / (double) n * w); g.drawVerticalLine(x, 0.f, (float) h); if (i < m.n) g.drawText(String(i + 1), x + 3, 2, 24, 12, juce::Justification::left); }
        }
        else if (v("sm_loop") > 0.5f)
        {
            const float len = re - rs, ls = rs + len * v("sm_ls"), le = rs + len * v("sm_le"); g.setColour(kTeal.withAlpha(0.25f)); g.fillRect(1.f + ls * fw, 0.f, std::max(1.f, (le - ls) * fw), (float) h);
        }
        g.setColour(juce::Colours::white); g.drawVerticalLine(1 + (int) ((rs + (re - rs) * v("sm_start")) * fw), 0.f, (float) h);
    }
private:
    PurrfectSamplerProcessor& proc;
};

// One parameter: a knob, a drop-down or a switch, with its caption
struct Ctl : juce::Component
{
    Ctl(juce::AudioProcessorValueTreeState& s, const ParamSpec& sp, Colour accent) : spec(sp)
    {
        const String id(sp.id), tip(tipFor(id));
        if (sp.kind == Kind::Float)
        {
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag); slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 18); slider.setTooltip(tip);
            slider.setColour(juce::Slider::textBoxTextColourId, accent); slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::rotarySliderFillColourId, accent); slider.setDoubleClickReturnValue(true, sp.def);
            addAndMakeVisible(slider); sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, sp.id, slider);
            slider.textFromValueFunction = [id](double v) { return valueText(id, v); }; slider.updateText();
        }
        else if (sp.kind == Kind::Switch)
        {
            btn.setButtonText(String(sp.name)); btn.setClickingTogglesState(true); btn.setTooltip(tip); addAndMakeVisible(btn); ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, sp.id, btn);
        }
        else
        {
            combo.addItemList(juce::StringArray::fromTokens(sp.choices, "|", ""), 1); combo.setTooltip(tip); addAndMakeVisible(combo); ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, sp.id, combo);
        }
        setTitle(sp.name);
    }
    int cellW() const { return spec.kind == Kind::Float ? 80 : (spec.kind == Kind::Switch ? 92 : 140); }
    int cellH() const { return spec.kind == Kind::Float ? 100 : 52; }
    void paint(Graphics& g) override
    {
        if (spec.kind == Kind::Switch) return;
        g.setColour(kDim); g.setFont(sans(11.5f, true)); g.drawText(String(spec.name).toUpperCase(), 0, 0, getWidth(), 14, juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        if (spec.kind == Kind::Float) slider.setBounds(r.withTrimmedTop(14));
        else if (spec.kind == Kind::Switch) btn.setBounds(r.withSizeKeepingCentre(r.getWidth() - 4, 38));
        else combo.setBounds(r.withTrimmedTop(18).withHeight(28));
    }
    ParamSpec spec; juce::Slider slider; Pill btn { "" }; juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
};

// A framed section for one group of the parameter table. A switch named "... on" becomes the section's power button.
struct Section : juce::Component
{
    Section(juce::AudioProcessorValueTreeState& s, const String& title_, const juce::StringArray& groups, Colour accent) : title(title_), col(accent)
    {
        const ParamSpec* t = paramTable();
        for (int i = 0; i < kParamCount; ++i)
            if (groups.contains(t[i].group))
            {
                const String nm(t[i].name);
                if (t[i].kind == Kind::Switch && (nm.endsWithIgnoreCase(" on"))) { power = std::make_unique<Ctl>(s, t[i], accent); power->btn.setButtonText("On"); addAndMakeVisible(*power); }
                else { auto* c = ctls.add(new Ctl(s, t[i], accent)); addAndMakeVisible(c); }
            }
    }
    int widthFor() const { int w = 0; for (auto* c : ctls) w += c->cellW(); return w + 24; }
    int layoutFor(int w, bool apply)   // flows the controls and returns the needed height
    {
        int x = 12, y = 34, rowH = 0;
        for (auto* c : ctls)
        {
            if (x + c->cellW() > w - 8 && x > 12) { x = 12; y += rowH + 4; rowH = 0; }
            if (apply) c->setBounds(x, y, c->cellW(), c->cellH());
            x += c->cellW(); rowH = std::max(rowH, c->cellH());
        }
        if (apply && power) power->setBounds(w - 66, 6, 58, 26);
        return y + rowH + 10;
    }
    void paint(Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f); g.setColour(kPanel); g.fillRoundedRectangle(r, 4.f); g.setColour(kEdge); g.drawRoundedRectangle(r, 4.f, 1.f);
        g.setColour(col); g.setFont(sans(13.f, true)); g.drawText(title.toUpperCase(), 12, 8, getWidth() - 80, 18, juce::Justification::left);
    }
    String title; Colour col; juce::OwnedArray<Ctl> ctls; std::unique_ptr<Ctl> power;
};

// A tab page: its sections are flowed into rows across the page
struct Page : juce::Component
{
    struct Def { const char* title; juce::StringArray groups; Colour col; };
    Page(juce::AudioProcessorValueTreeState& s, std::vector<Def> defs) { for (auto& d : defs) { auto* sec = secs.add(new Section(s, d.title, d.groups, d.col)); addAndMakeVisible(sec); } }
    void resized() override
    {
        const int W = getWidth(); int x = 0, y = 0, rowH = 0; juce::Array<Section*> row;
        auto flush = [&] { if (row.isEmpty()) return; int sum = 0; for (auto* s : row) sum += s->widthFor(); const int extra = std::max(0, W - 8 * (row.size() - 1) - sum); int xx = 0;
            for (int i = 0; i < row.size(); ++i) { const int w = row[i]->widthFor() + extra / row.size(); row[i]->setBounds(xx, y, w, rowH); row[i]->layoutFor(w, true); xx += w + 8; } y += rowH + 8; row.clear(); rowH = 0; x = 0; };
        for (auto* s : secs)
        {
            const int w = s->widthFor();
            if (x + w > W && !row.isEmpty()) flush();
            rowH = std::max(rowH, s->layoutFor(w, false)); row.add(s); x += w + 8;
        }
        flush();
    }
    juce::OwnedArray<Section> secs;
};
}

struct PurrfectSamplerEditor::Impl
{
    Impl(PurrfectSamplerEditor& o, PurrfectSamplerProcessor& p) : owner(o), proc(p), gate(o), wave(p),
        keys(p.keyState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        o.setLookAndFeel(&laf);
        auto& c = content; o.addAndMakeVisible(c);
        for (auto* b : { &open, &guide, &about }) c.addAndMakeVisible(*b);
        open.setTooltip("Load a sample (wav, aiff, flac, ogg, mp3, up to 10 minutes)."); open.onClick = [this] { choose(); };
        guide.onClick = [] { juce::URL("https://github.com/SpeakingColors/purrfect-cut/blob/main/docs/purrfect-cut-manual.pdf").launchInDefaultBrowser(); };
        about.onClick = [] { juce::URL("https://ko-fi.com/studio87").launchInDefaultBrowser(); };
        c.addAndMakeVisible(wave); c.addAndMakeVisible(keys); keys.setKeyWidth(26.f); keys.setAvailableRange(36, 96); keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, kPink.withAlpha(0.6f));
        auto& ap = proc.apvts;
        pages.add(new Page(ap, { { "Pitch and play", { "Pitch and play" }, kPink }, { "Sample", { "Sample" }, kTeal }, { "Amp", { "Amp" }, kPink }, { "Filter", { "Filter" }, Colour(0xff4aa8ff) } }));
        pages.add(new Page(ap, { { "Drive", { "Drive" }, kGold }, { "Squash", { "Squash" }, kPurple }, { "Duck", { "Duck" }, kTeal }, { "Delay", { "Delay" }, Colour(0xff4aa8ff) },
                                 { "Reverb", { "Reverb" }, kPurple }, { "Output", { "Output" }, kPink } }));
        pages.add(new Page(ap, { { "Auto-Tune", { "Auto-Tune" }, kTeal }, { "Notes allowed", { "Auto-Tune notes" }, kPink }, { "Robot", { "Robot" }, kGold } }));
        pages.add(new Page(ap, { { "Harmony", { "Harmony" }, kPurple } }));
        const char* names[] = { "Sampler", "FX", "Auto-Tune", "Harmony" }; const char* subs[] = { "play it from keys or MIDI", "drive, squash, duck, echo, reverb", "key, speed, formants", "stack voices, robot" };
        for (int i = 0; i < 4; ++i) { c.addAndMakeVisible(*pages[i]); tabName[i] = names[i]; tabSub[i] = subs[i]; }
        showTab(0);
        tip = std::make_unique<juce::TooltipWindow>(&o, 500);
        constrainer.setFixedAspectRatio((double) kBaseW / kBaseH); constrainer.setSizeLimits(kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
        o.setConstrainer(&constrainer); o.setResizable(true, true); o.setSize(kBaseW, kBaseH);
        gate.onFrame = [this] { tick(); }; gate.start(12);
        if (const char* f = std::getenv("CAT_TOUR_LOAD"))   // documentation tool for the screenshots, never set for users
            juce::Timer::callAfterDelay(1500, [sp = juce::Component::SafePointer<PurrfectSamplerEditor>(&o), path = String(f)] { if (sp != nullptr) sp->impl->proc.loadSample(juce::File(path)); });
        if (const char* t = std::getenv("CAT_TOUR_TAB")) showTab(juce::jlimit(0, 3, String(t).getIntValue()));
    }
    ~Impl() { owner.setLookAndFeel(nullptr); }

    struct Content : juce::Component { Impl& i; explicit Content(Impl& x) : i(x) {} void paint(Graphics& g) override { i.paintContent(g); } void mouseDown(const juce::MouseEvent& e) override { i.clickTab(e.getPosition()); } } content { *this };

    PurrfectSamplerEditor& owner; PurrfectSamplerProcessor& proc; catui::CatLookAndFeel laf; juce::ComponentBoundsConstrainer constrainer; catui::RenderGate gate;
    Pill open { "Open file" }, guide { "Guide" }, about { "About & Support" };
    WavePane wave; juce::MidiKeyboardComponent keys; juce::OwnedArray<Page> pages; std::unique_ptr<juce::TooltipWindow> tip;
    String tabName[4], tabSub[4]; int tab = 0, seenVersion = -1; String shownName; std::unique_ptr<juce::FileChooser> chooser;

    float scale() const { return (float) owner.getWidth() / (float) kBaseW; }
    juce::Rectangle<int> tabRect(int i) const { const int w = 270; return { 14 + i * (w + 10), 92, w, 52 }; }
    void showTab(int t) { tab = t; for (int i = 0; i < pages.size(); ++i) pages[i]->setVisible(i == t); content.repaint(); }
    void clickTab(juce::Point<int> p) { for (int i = 0; i < 4; ++i) if (tabRect(i).contains(p)) showTab(i); }
    void layout()
    {
        guide.setBounds(900, 20, 90, 34); about.setBounds(1000, 20, 130, 34); open.setBounds(14, 152, 130, 34);
        wave.setBounds(14, 196, 1112, 100);
        for (auto* p : pages) { p->setBounds(14, 306, 1112, 600); }
        keys.setBounds(14, 914, 1112, 58);
    }
    void choose()
    {
        chooser = std::make_unique<juce::FileChooser>("Pick a sound", juce::File(proc.samplePath()).getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadSample(fc.getResult()); });
    }
    void tick()
    {
        if (seenVersion != proc.sampleVersion()) { seenVersion = proc.sampleVersion(); const juce::File f(proc.samplePath()); shownName = f.getFileName().isEmpty() ? "No sample loaded" : f.getFileName(); content.repaint(); }
        wave.repaint();
    }
    void paintContent(Graphics& g)
    {
        g.fillAll(kBg);
        catui::drawCatHead(g, { 22, 14, 60, 56 }, kText, kBg);
        g.setColour(kText); g.setFont(sans(34.f, true)); g.drawText("PURRFECT CUT", 96, 8, 400, 46, juce::Justification::left);
        g.setColour(kGold); g.setFont(sans(14.f, true)); g.drawText("STUDIO 87", 98, 52, 200, 20, juce::Justification::left);
        g.setColour(kEdge); g.drawHorizontalLine(84, 14.f, 1126.f);
        for (int i = 0; i < 4; ++i)
        {
            auto r = tabRect(i).toFloat(); const bool on = i == tab;
            g.setColour(on ? kPink.withAlpha(0.15f) : kPanel); g.fillRoundedRectangle(r, 4.f); g.setColour(on ? kPink : kEdge); g.drawRoundedRectangle(r.reduced(0.5f), 4.f, on ? 1.5f : 1.f);
            g.setColour(on ? kPink : kText); g.setFont(sans(17.f, true)); g.drawText(tabName[i].toUpperCase(), r.withTrimmedBottom(18.f), juce::Justification::centred);
            g.setColour(kDim); g.setFont(sans(11.5f)); g.drawText(tabSub[i], r.withTrimmedTop(30.f), juce::Justification::centred);
        }
        g.setColour(kDim); g.setFont(mono(13.f)); g.drawText(shownName, 160, 152, 900, 34, juce::Justification::centredLeft);
        g.setFont(mono(11.f)); g.drawText("STUDIO 87 | PURRFECT CUT SAMPLER | hover a control for help", 14, 976, 700, 16, juce::Justification::left);
    }
};

// impl is still null while its constructor runs (setSize calls resized and paint), so those two skip until it exists
PurrfectSamplerEditor::PurrfectSamplerEditor(PurrfectSamplerProcessor& p) : AudioProcessorEditor(&p), impl(std::make_unique<Impl>(*this, p)) { resized(); }
PurrfectSamplerEditor::~PurrfectSamplerEditor() = default;
void PurrfectSamplerEditor::paint(Graphics& g) { g.fillAll(kBg); if (!impl) return; catui::drawWoodCheeks(g, getLocalBounds(), juce::roundToInt(kCheek * impl->scale())); }
void PurrfectSamplerEditor::resized()
{
    if (!impl) return;
    const float s = impl->scale();
    impl->content.setBounds(juce::roundToInt(kCheek * s), 0, kContentW, kBaseH); impl->content.setTransform(juce::AffineTransform::scale(s)); impl->layout();
}
void PurrfectSamplerEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!impl) return;
    const auto p = e.getEventRelativeTo(this).getPosition(); if (p.x < kCheek * impl->scale() || p.x > getWidth() - kCheek * impl->scale()) setSize(kBaseW, kBaseH);
}
