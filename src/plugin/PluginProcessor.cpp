#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace purrfect;

juce::AudioProcessorValueTreeState::ParameterLayout PurrfectSamplerProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout; const ParamSpec* t = paramTable();
    for (int i = 0; i < kParamCount; ++i)
    {
        const ParamSpec& s = t[i]; const juce::ParameterID id { s.id, 1 };
        if (s.kind == Kind::Switch) layout.add(std::make_unique<juce::AudioParameterBool>(id, s.name, s.def > 0.5f));
        else if (s.kind == Kind::Choice) layout.add(std::make_unique<juce::AudioParameterChoice>(id, s.name, juce::StringArray::fromTokens(s.choices, "|", ""), (int) s.def));
        else
        {
            juce::NormalisableRange<float> r(s.min, s.max, s.step);
            if (s.log) { r = juce::NormalisableRange<float>(s.min, s.max, [](float lo, float hi, float v) { return lo * std::pow(hi / lo, v); }, [](float lo, float hi, float v) { return std::log(v / lo) / std::log(hi / lo); }); }
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, s.name, r, s.def));
        }
    }
    return layout;
}

PurrfectSamplerProcessor::PurrfectSamplerProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true).withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PurrfectCutSampler", makeLayout())
{
    for (int i = 0; i < kParamCount; ++i) raw[i] = apvts.getRawParameterValue(paramTable()[i].id);
    defaultParams(P); fm.registerBasicFormats(); startTimerHz(20);
}
PurrfectSamplerProcessor::~PurrfectSamplerProcessor() { stopTimer(); }

bool PurrfectSamplerProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = l.getMainInputChannelSet(); return in.isDisabled() || in == juce::AudioChannelSet::stereo();
}

void PurrfectSamplerProcessor::prepareToPlay(double sr, int block)
{
    hostRate = sr; engine.prepare(sr); scratch.setSize(2, std::max(block, 1)); seenSerial = -1;
    const juce::SpinLock::ScopedLockType l(lock); if (ui) { pending = ui; pendingFlag = true; pendingRate = sourceRate; }
}

void PurrfectSamplerProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd; const int n = buf.getNumSamples();
    // new sample from the message thread (try lock: if the message thread holds it we try again next block)
    { const juce::SpinLock::ScopedTryLockType l(lock); if (l.isLocked() && pendingFlag) { std::swap(cur, pending); pendingFlag = false; engine.setSource(cur.get(), pendingRate); seenSerial = -1; } }
    if (seenSerial != sliceSerial.load()) { seenSerial = sliceSerial.load(); engine.setSlices(maps[live.load()]); }
    for (int i = 0; i < kParamCount; ++i) P[i] = raw[i]->load();
    keyState.processNextMidiBuffer(midi, 0, n, true);

    const float* scL = nullptr; const float* scR = nullptr; auto in = getBusBuffer(buf, true, 0);
    const bool haveSc = getBus(true, 0) && getBus(true, 0)->isEnabled() && in.getNumChannels() >= 2;
    if (haveSc) { scratch.setSize(2, n, false, false, true); scratch.copyFrom(0, 0, in, 0, 0, n); scratch.copyFrom(1, 0, in, 1, 0, n); scL = scratch.getReadPointer(0); scR = scratch.getReadPointer(1); }
    auto out = getBusBuffer(buf, false, 0); float* oL = out.getWritePointer(0); float* oR = out.getWritePointer(1);

    if (engine.asleep() && midi.isEmpty() && !(haveSc && P[fx_prev] > 0.5f)) { out.clear(); return; }   // idle for 12 s: nothing to render

    int pos = 0;
    auto render = [&](int upTo) { if (upTo > pos) { engine.process(P, scL ? scL + pos : nullptr, scR ? scR + pos : nullptr, oL + pos, oR + pos, upTo - pos); pos = upTo; } };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage(); render(std::min(n, meta.samplePosition));
        if (m.isNoteOn()) engine.noteOn(m.getNoteNumber(), m.getFloatVelocity(), P);
        else if (m.isNoteOff()) engine.noteOff(m.getNoteNumber(), P);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allOff();
        else if (m.isPitchWheel()) engine.setBend(((float) m.getPitchWheelValue() - 8192.f) / 8192.f * 2.f);
        else if (m.isController() && m.getControllerNumber() == 64) { sustainDown = m.getControllerValue() >= 64; engine.setSustain(sustainDown); }
    }
    render(n);
    midi.clear();
}

bool PurrfectSamplerProcessor::loadSample(const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r || r->lengthInSamples < 32 || r->lengthInSamples > (juce::int64) r->sampleRate * 600) return false;   // up to 10 minutes
    const int len = (int) r->lengthInSamples; juce::AudioBuffer<float> b((int) std::max<unsigned>(2, r->numChannels), len); b.clear(); r->read(&b, 0, len, 0, true, true);
    if (r->numChannels == 1) b.copyFrom(1, 0, b, 0, 0, len);
    auto a = std::make_shared<Audio>(); a->L.assign(b.getReadPointer(0), b.getReadPointer(0) + len); a->R.assign(b.getReadPointer(1), b.getReadPointer(1) + len);
    { const juce::SpinLock::ScopedLockType l(lock); ui = a; pending = a; pendingRate = r->sampleRate; sourceRate = r->sampleRate; pendingFlag = true; path = f.getFullPathName(); }
    ++version; return true;
}

void PurrfectSamplerProcessor::timerCallback()
{
    { const juce::SpinLock::ScopedLockType l(lock); if (!pendingFlag) pending.reset(); }   // free the buffer the audio thread has let go of
    recomputeSlices();
    const bool pc = apvts.getRawParameterValue("pc_on")->load() > 0.5f || apvts.getRawParameterValue("hm_on")->load() > 0.5f;   // Auto-Tune and Harmony delay the sound by a fixed amount: tell the host
    const int lat = pc ? engine.pitchLatency() : 0; if (lat != getLatencySamples()) setLatencySamples(lat);
}

void PurrfectSamplerProcessor::recomputeSlices()
{
    const float key[4] { raw[sm_slices]->load(), raw[sm_smode]->load(), raw[sm_rs]->load(), raw[sm_re]->load() }; const int ver = version.load();
    if (ver == lastSliceVer && std::equal(key, key + 4, lastSliceKey)) return;
    std::copy(key, key + 4, lastSliceKey); lastSliceVer = ver; auto a = sample(); SliceMap m;
    if (a && a->size() > 32)
    {
        const size_t n = a->size(), s = std::min(n - 1, (size_t) ((double) n * key[2])), e = std::min(n, std::max(s + 1, (size_t) ((double) n * key[3])));
        const auto pts = slicePoints(a->L.data(), a->R.data(), s, e, (int) key[0], (int) key[1] == SLICE_TRANS);
        m.n = (int) std::min<size_t>(24, pts.size() - 1); for (int i = 0; i <= m.n; ++i) m.pts[i] = pts[(size_t) i];
    }
    const int next = 1 - live.load(); maps[next] = m; live.store(next); ++sliceSerial;
}

void PurrfectSamplerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState(); state.setProperty("samplePath", path, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void PurrfectSamplerProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(apvts.state.getType()))
    {
        auto vt = juce::ValueTree::fromXml(*xml); const juce::String p = vt.getProperty("samplePath").toString(); apvts.replaceState(vt);
        if (p.isNotEmpty() && p != path) loadSample(juce::File(p));   // the sample itself is not stored in the project, only where it came from
    }
}

juce::AudioProcessorEditor* PurrfectSamplerProcessor::createEditor() { return new PurrfectSamplerEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PurrfectSamplerProcessor(); }
