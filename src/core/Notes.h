// Purrfect Cut: the pitch document behind the Pitch Editor (plugin and tests). Pure C++, no JUCE.
//
// analyseDoc(): finds the pitch of the audio (one voice: pYIN style tracker; chords: multi-pitch detector) and cuts it into notes.
// renderDoc(): applies the edits of every note and returns new audio. Per note: pitch (semitones), vibrato size, drift, formant, level,
// position and length (single voices only), and which harmony voices sound on it. Two engines:
//   Smooth: the spectral shifter (Spectral.h). Cleanest for pitch moves, and the only one that can move single notes of a chord.
//   Tight : PSOLA (Pitch.h). Keeps the time structure of the take (best for small moves of dry, close-miked voices) and is the one used
//           when a note is moved or stretched in time.
// Honest limits: chords work for clean, sustained voices (up to 6); notes an octave apart cannot be separated; in chord mode only pitch and formant
// (one value per moment) are edited (no vibrato, drift, level, mute, time or harmony per note). Frames of the spectral engine
// are about 93 ms, so very sharp attacks are softened a little.
#pragma once
#include "Poly.h"
#include "Spectral.h"
#include "Track.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace purrfect {

struct EdNote {
    long sa = 0, sb = 0;          // where the note sits in the source (samples)
    long ta = 0, tb = 0;          // where it is placed in the result (samples); same as sa / sb when not moved
    float midi = 60.f, hz = 261.6256f;   // pitch as found (median), fractional MIDI note and Hz
    float dp = 0.f;               // pitch move, semitones
    float vib = 1.f;              // vibrato size, 1 = as sung
    float drift = 0.f;            // 0..1 how much of the slow drift away from the note centre is straightened
    float fs = 0.f;               // formant shift, semitones
    float gainDb = 0.f;          // level change
    unsigned char harm = 7;       // bit v set: harmony voice v+1 sounds on this note
    bool mute = false;
    bool moved() const { return ta != sa || tb != sb; }
    bool edited() const { return std::fabs(dp) > 1e-4f || std::fabs(vib - 1.f) > 1e-3f || drift > 1e-3f || std::fabs(fs) > 1e-3f || std::fabs(gainDb) > 1e-3f || moved() || mute; }
    void reset() { ta = sa; tb = sb; dp = 0.f; vib = 1.f; drift = 0.f; fs = 0.f; gainDb = 0.f; mute = false; }
};

struct PitchDoc {
    double sr = 44100.0; long n = 0; int hop = 256; float refHz = 440.f;
    bool poly = false;                         // chords found (polyphonic analysis)
    std::vector<float> f0, rms;                // single voice: pitch in Hz per hop frame (0 = unvoiced) and level
    int pHop = 0, pFrame = 0; std::vector<PolyFrame> pf;   // chords: detector frames
    std::vector<EdNote> notes;
    int voicedPercent() const { size_t v = 0; for (float f : f0) if (f > 0.f) ++v; return f0.empty() ? 0 : (int) (100 * v / f0.size()); }
    bool anyEdit() const { for (auto& e : notes) if (e.edited()) return true; return false; }
    // every note to the nearest allowed note (mask: bit per pitch class)
    void snapAll(unsigned mask) { for (auto& e : notes) e.dp = nearestAllowed(e.midi, mask) - e.midi; }
    void resetAll() { for (auto& e : notes) e.reset(); }
};

enum { AN_AUTO = 0, AN_VOICE = 1, AN_CHORDS = 2 };

inline PitchDoc analyseDoc(const float* mono, size_t n, double sr, float refHz, int mode = AN_AUTO, int hop = 256) {
    PitchDoc d; d.sr = sr; d.n = (long) n; d.hop = hop; d.refHz = refHz;
    if (n < 2048) return d;
    TrackFrames tf = pyinTrack(mono, n, sr, hop); d.f0 = tf.f0; d.rms = tf.rms;
    auto voiceNotes = [&]() {
        std::vector<EdNote> out; const auto nn = segmentNotes(toPitchFrames(tf), sr, refHz);
        for (auto& q : nn) { EdNote e; e.sa = e.ta = (long) q.a * hop; e.sb = e.tb = std::min((long) n, (long) q.b * hop); e.midi = q.midi; e.hz = midiToHz(q.midi, refHz); out.push_back(e); }
        return out; };
    auto chordNotes = [&](PitchDoc& dd) {
        PolyDetector pd(sr, 6); dd.pHop = pd.hop(); dd.pFrame = pd.frameSize(); dd.pf = pd.analyse(mono, n);
        std::vector<EdNote> out; for (auto& q : polyNotes(dd.pf, dd.pHop, dd.pFrame, refHz)) { EdNote e; e.sa = e.ta = std::max(0L, (long) q.sampleA); e.sb = e.tb = std::min((long) n, (long) q.sampleB); e.midi = q.midi; e.hz = midiToHz(q.midi, refHz); out.push_back(e); }
        return out; };
    if (mode == AN_VOICE) { d.notes = voiceNotes(); return d; }
    if (mode == AN_CHORDS) { d.poly = true; d.notes = chordNotes(d); return d; }
    // auto: chords when more than one voice is found in a good share of the frames that have any
    PitchDoc probe = d; auto cn = chordNotes(probe); size_t any = 0, many = 0; for (auto& f : probe.pf) { if (!f.f0.empty()) ++any; if (f.f0.size() >= 2) ++many; }
    if (any > 8 && many * 100 > any * 35) { d.poly = true; d.pf = probe.pf; d.pHop = probe.pHop; d.pFrame = probe.pFrame; d.notes = cn; }
    else d.notes = voiceNotes();
    return d;
}

struct RenderCfg {
    int engine = 0;                 // 0 Smooth (spectral), 1 Tight (PSOLA)
    float keep = 1.f;               // formant keep 0..1
    float glideMs = 40.f;           // glide between edited notes
    bool harmOn = false; bool hSnap = true; unsigned mask = 0xFFF; float hInt[3] { 4.f, 7.f, -12.f }, hLvl[3] { 0.6f, 0.5f, 0.f }; float hWet = 0.7f;
};

namespace detail {
inline std::vector<float> smoothTwice(std::vector<float> a, double tauFrames) {
    if (a.empty() || tauFrames < 0.2) return a; const double k = 1.0 - std::exp(-1.0 / tauFrames); float z = a[0];
    for (size_t i = 0; i < a.size(); ++i) { z += (float) k * (a[i] - z); a[i] = z; }
    z = a.back(); for (size_t i = a.size(); i-- > 0;) { z += (float) k * (a[i] - z); a[i] = z; } return a;
}
inline double at(const std::vector<float>& a, double pos, double dflt) { if (a.empty()) return dflt; const long i = (long) std::floor(pos); const double f = pos - (double) i; auto g = [&](long k) { return (double) a[(size_t) std::max(0L, std::min((long) a.size() - 1, k))]; }; return g(i) * (1.0 - f) + g(i + 1) * f; }
}

// Per-frame curves of one render: pitch move (semitones), formant (semitones), input time (in frames), level (dB), and for harmony the
// interval shift and presence of each voice.
struct Curves { std::vector<float> semi, fsh, tm, gain, mute; std::vector<float> hshift[3], hon[3]; bool timeEdit = false; };

inline Curves buildCurves(const PitchDoc& d, const RenderCfg& cfg) {
    const int hop = d.hop; const size_t nf = (size_t) (d.n / hop) + 2; Curves c; c.semi.assign(nf, 0.f); c.fsh.assign(nf, 0.f); c.gain.assign(nf, 0.f); c.mute.assign(nf, 0.f); c.tm.assign(nf, 0.f);
    std::vector<float> base(nf, 0.f), vibc(nf, 0.f); for (int v = 0; v < 3; ++v) { c.hshift[v].assign(nf, 0.f); c.hon[v].assign(nf, 0.f); }
    std::vector<size_t> order(d.notes.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i; std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return d.notes[a].ta < d.notes[b].ta; });
    std::vector<std::pair<double, double>> knots; knots.push_back({ 0.0, 0.0 }); double lastT = 0, lastS = 0;
    for (size_t oi : order) {
        const EdNote& e = d.notes[oi]; if (d.poly) { c.timeEdit = c.timeEdit || e.moved(); }
        const double a = (double) e.sa / hop, b = std::max(a + 1.0, (double) e.sb / hop); double ta = (double) e.ta / hop, tb = std::max(ta + 1.0, (double) e.tb / hop);
        if (ta < lastT) { tb += lastT - ta; ta = lastT; }   // notes may not run into each other in the result
        const double len = tb - ta; if (e.moved()) c.timeEdit = true;
        // slow and fast parts of the sung pitch inside the note
        const int m0 = (int) std::floor(a), m1 = std::max(m0 + 1, (int) std::ceil(b)); std::vector<float> m((size_t) (m1 - m0)), slow(m.size());
        for (int k = m0; k < m1; ++k) { const float f = (size_t) k < d.f0.size() ? d.f0[(size_t) k] : 0.f; m[(size_t) (k - m0)] = f > 0.f ? hzToMidi(f, d.refHz) : e.midi; }
        const long half = std::max(3L, (long) std::lround(0.1 * d.sr / hop));   // the slow part is the average over 200 ms: about one vibrato period, so vibrato itself averages out
        for (size_t i = 0; i < m.size(); ++i) { double s = 0; int cn = 0; for (long j = std::max(0L, (long) i - half); j <= std::min((long) m.size() - 1, (long) i + half); ++j) { s += m[(size_t) j]; ++cn; } slow[i] = (float) (s / cn); }
        const float target = e.midi + e.dp;
        for (long k = (long) std::floor(ta); k < (long) std::ceil(tb) && k < (long) nf; ++k) {
            if (k < 0) continue; const double fin = a + ((double) k - ta) / len * (b - a); const long q = std::max(0L, std::min((long) m.size() - 1, (long) std::lround(fin - m0)));
            const float s = slow[(size_t) q], fast = m[(size_t) q] - s;
            base[(size_t) k] = e.dp - e.drift * (s - e.midi); vibc[(size_t) k] = (e.vib - 1.f) * fast; c.fsh[(size_t) k] = e.fs; c.gain[(size_t) k] = e.gainDb; c.mute[(size_t) k] = e.mute ? 1.f : 0.f;
            for (int v = 0; v < 3; ++v) {
                float sh = cfg.hInt[v]; if (cfg.hSnap) sh = nearestAllowed(target + cfg.hInt[v], cfg.mask) - target;
                c.hshift[v][(size_t) k] = sh; c.hon[v][(size_t) k] = ((e.harm >> v) & 1) ? 1.f : 0.f;
            }
        }
        knots.push_back({ ta, a }); knots.push_back({ tb, b }); lastT = tb; lastS = b;
    }
    (void) lastS; knots.push_back({ (double) nf - 1, (double) nf - 1 });
    // knots must rise in the result time; where an edit would fold time back, fall back to the identity
    for (size_t i = 1; i < knots.size(); ++i) if (knots[i].first < knots[i - 1].first) knots[i].first = knots[i - 1].first;
    { size_t i = 0; for (size_t k = 0; k < nf; ++k) { while (i + 1 < knots.size() - 1 && knots[i + 1].first <= (double) k) ++i; const auto A = knots[i], B = knots[std::min(i + 1, knots.size() - 1)]; const double t = B.first > A.first ? ((double) k - A.first) / (B.first - A.first) : 0.0; c.tm[k] = (float) ((A.second + (B.second - A.second) * std::max(0.0, std::min(1.0, t))) * hop); } }
    const double tau = std::max(0.0, (double) cfg.glideMs) / (1000.0 * hop / d.sr);
    base = detail::smoothTwice(base, tau);   // glide between notes (the vibrato part keeps its speed)
    for (size_t k = 0; k < nf; ++k) c.semi[k] = base[k] + vibc[k];
    for (int v = 0; v < 3; ++v) { c.hshift[v] = detail::smoothTwice(c.hshift[v], tau); c.hon[v] = detail::smoothTwice(c.hon[v], 1.5); }
    c.gain = detail::smoothTwice(c.gain, 1.0); c.mute = detail::smoothTwice(c.mute, 1.0);
    return c;
}

namespace detail {
// One channel through the chosen engine; semi/fsh per frame.
inline std::vector<float> shiftMono(const std::vector<float>& x, const PitchDoc& d, const Curves& c, const std::vector<float>& semi, bool allowSpectral, const RenderCfg& cfg, float detuneCents = 0.f) {
    const size_t n = x.size();
    std::vector<float> sm = semi; if (detuneCents != 0.f) for (auto& s : sm) s += detuneCents * 0.01f;
    if (!allowSpectral || cfg.engine == 1 || c.timeEdit) {
        PitchFrames pf; pf.hop = d.hop; pf.f0 = d.f0; if (pf.f0.size() < n / (size_t) d.hop + 2) pf.f0.resize(n / (size_t) d.hop + 2, 0.f);
        RenderEdit ed; ed.hop = d.hop; ed.ratio.resize(sm.size()); for (size_t i = 0; i < sm.size(); ++i) ed.ratio[i] = std::exp2(std::max(-24.f, std::min(24.f, sm[i])) / 12.f); ed.fshift = c.fsh; ed.tmap = c.tm;
        // psolaRender takes tmap in samples of input time
        return psolaRender(x, d.sr, pf, ed, cfg.keep, n);
    }
    float lowest = 1e9f; for (size_t i = 0; i < sm.size() && i < d.f0.size(); ++i) if (d.f0[i] > 0.f) lowest = std::min(lowest, d.f0[i] * std::exp2(sm[i] / 12.f));
    SpectralShifter sh(lowest < 110.f ? 8192 : 4096);
    return sh.process(x, d.sr, [&](double t, SpecCtl& ctl) {
        const double k = t / d.hop; const double s = detail::at(sm, k, 0.0); const size_t fi = (size_t) std::max(0L, std::min((long) d.f0.size() - 1, (long) std::lround(k)));
        const float f0 = d.f0.empty() ? 0.f : d.f0[fi]; ctl.preserve = cfg.keep; ctl.fshift = (float) detail::at(c.fsh, k, 0.0);
        if (f0 > 0.f) { const float r = std::exp2((float) s / 12.f); ctl.voices = { { f0, r } }; ctl.defaultRatio = r; } else { ctl.defaultRatio = 1.f; }
    });
}
inline std::vector<float> shiftChords(const std::vector<float>& x, const PitchDoc& d, const RenderCfg& cfg) {
    SpectralShifter sh(4096);
    return sh.process(x, d.sr, [&](double t, SpecCtl& ctl) {
        ctl.preserve = cfg.keep; ctl.defaultRatio = 1.f;
        const long i = (long) std::lround((t - d.pFrame * 0.5) / d.pHop); if (i < 0 || (size_t) i >= d.pf.size()) return;
        double fsSum = 0; int fsN = 0;
        for (float f : d.pf[(size_t) i].f0) {
            const EdNote* best = nullptr; double bd = 80.0;   // the note this voice belongs to: it is sounding now and its pitch is within 80 cents
            for (auto& e : d.notes) { if ((double) e.sa > t + d.pFrame * 0.25 || (double) e.sb < t - d.pFrame * 0.25) continue; const double dc = std::fabs(1200.0 * std::log2(f / e.hz)); if (dc < bd) { bd = dc; best = &e; } }
            if (!best) { ctl.voices.push_back({ f, 1.f }); continue; }
            ctl.voices.push_back({ f, best->mute ? 0.f : std::exp2(best->dp / 12.f) }); fsSum += best->fs; ++fsN;
        }
        ctl.fshift = fsN ? (float) (fsSum / fsN) : 0.f;
    });
}
}

inline void renderDoc(const PitchDoc& d, const std::vector<float>& L, const std::vector<float>& R, const RenderCfg& cfg, std::vector<float>& outL, std::vector<float>& outR) {
    const size_t n = L.size(); outL = L; outR = R; if (d.notes.empty() && !cfg.harmOn) return;
    if (d.poly) {
        if (!d.anyEdit()) return;
        outL = detail::shiftChords(L, d, cfg); outR = detail::shiftChords(R, d, cfg); return;
    }
    Curves c = buildCurves(d, cfg);
    const bool moved = d.anyEdit();
    if (moved) { outL = detail::shiftMono(L, d, c, c.semi, true, cfg); outR = detail::shiftMono(R, d, c, c.semi, true, cfg); }
    // level per note
    { bool any = false; for (size_t k = 0; k < c.gain.size(); ++k) if (std::fabs(c.gain[k]) > 1e-3f || c.mute[k] > 1e-3f) { any = true; break; }
      if (any) for (size_t i = 0; i < n; ++i) { const double k = (double) i / d.hop; const double g = detail::at(c.gain, k, 0.0), m = detail::at(c.mute, k, 0.0); const float lin = (float) (std::pow(10.0, g / 20.0) * (1.0 - std::min(1.0, m))); outL[i] *= lin; outR[i] *= lin; } }
    if (cfg.harmOn) {
        static const float det[3] { 4.f, -5.f, 6.f }; static const double dly[3] { 0.007, 0.011, 0.014 };   // tiny detune and delay per voice: no phasey stacking of identical copies
        for (int v = 0; v < 3; ++v) {
            if (cfg.hLvl[v] < 0.001f) continue; std::vector<float> sv(c.semi.size()); for (size_t k = 0; k < sv.size(); ++k) sv[k] = c.semi[k] + c.hshift[v][k];
            std::vector<float> hl = detail::shiftMono(L, d, c, sv, true, cfg, det[v]), hr = detail::shiftMono(R, d, c, sv, true, cfg, det[v]);
            const long dl = (long) (dly[v] * d.sr); const float g0 = cfg.hLvl[v] * cfg.hWet;
            for (size_t i = 0; i < n; ++i) { const long j = (long) i - dl; if (j < 0) continue; const float g = g0 * (float) detail::at(c.hon[v], (double) j / d.hop, 1.0); outL[i] += g * hl[(size_t) j]; outR[i] += g * hr[(size_t) j]; }
        }
    }
}

}  // namespace purrfect
