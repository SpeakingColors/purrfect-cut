// Purrfect Cut: pitch tools. Pure C++, no JUCE, no allocation while processing (everything is sized in prepare()).
//
//  * yinPeriod / yinTrack : pitch detection (YIN: difference function, cumulative mean normalisation, parabolic refinement).
//  * LivePitch            : streaming auto-tune / pitch corrector / harmony / robot voice. Time-domain PSOLA: grains two periods long,
//                           placed one output period apart, read from the input at pitch-period spacing. Because the grains are
//                           cut from the original waveform, the spectral envelope (the formants) is kept when the pitch moves;
//                           "Formant preserve" blends towards plain resampling (formants follow the pitch) and "Formant shift"
//                           moves the formants on their own.
//  * segmentNotes / psolaRender : the Melodyne-style editor: pitch track -> note blobs -> re-render with per-note pitch, vibrato,
//                           formant and time edits.
//
// Honest limits: one voice at a time (monophonic material: voice, lead, bass). Pitch detection below about 75 Hz is unreliable.
// Very breathy or noisy material is passed through unchanged (unvoiced). Results are good, not Melodyne-grade.
// The JavaScript version of the offline path lives in docs/mockup/purrfectcut.html; the two are kept in step by hand.
#pragma once
#include "Params.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace purrfect {

// ---------------------------------------------------------------- helpers
inline float cubicAt(const float* buf, unsigned mask, double pos) {
    const long i = (long) std::floor(pos); const float f = (float) (pos - (double) i);
    const float a = buf[(unsigned) (i - 1) & mask], b = buf[(unsigned) i & mask], c = buf[(unsigned) (i + 1) & mask], d = buf[(unsigned) (i + 2) & mask];
    return b + 0.5f * f * (c - a + f * (2.f * a - 5.f * b + 4.f * c - d + f * (3.f * (b - c) + d - a)));
}
inline float cubicVec(const std::vector<float>& x, double pos) {
    const long n = (long) x.size(); const long i = (long) std::floor(pos); const float f = (float) (pos - (double) i);
    auto at = [&](long k) { return k < 0 || k >= n ? 0.f : x[(size_t) k]; };
    const float a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
    return b + 0.5f * f * (c - a + f * (2.f * a - 5.f * b + 4.f * c - d + f * (3.f * (b - c) + d - a)));
}
inline float hzToMidi(float hz, float ref) { return 69.f + 12.f * std::log2(hz / ref); }
inline float midiToHz(float m, float ref) { return ref * std::exp2((m - 69.f) / 12.f); }

// ---------------------------------------------------------------- scales
inline unsigned scaleBits(int scale) {
    static const int notes[11][8] = {
        { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 2, 4, 5, 7, 9, 11, -1 }, { 0, 2, 3, 5, 7, 8, 10, -1 }, { 0, 2, 3, 5, 7, 9, 10, -1 }, { 0, 1, 3, 5, 7, 8, 10, -1 },
        { 0, 2, 4, 6, 7, 9, 11, -1 }, { 0, 2, 4, 5, 7, 9, 10, -1 }, { 0, 2, 3, 5, 7, 8, 11, -1 }, { 0, 2, 4, 7, 9, -1, -1, -1 }, { 0, 3, 5, 7, 10, -1, -1, -1 }, { 0, 3, 5, 6, 7, 10, -1, -1 } };
    if (scale <= 0 || scale > 10) return 0xFFF;
    unsigned b = 0; for (int i = 0; i < 8; ++i) if (notes[scale][i] >= 0) b |= 1u << notes[scale][i]; return b;
}
// bit k set = pitch class k (0 = C) is allowed. Scale rotated to the key, then masked by the user's note switches.
inline unsigned allowedMask(int key, int scale, unsigned userMask) {
    const unsigned s = scaleBits(scale); unsigned m = 0;
    for (int k = 0; k < 12; ++k) if (s & (1u << k)) m |= 1u << ((k + key) % 12);
    m &= userMask & 0xFFFu; return m ? m : 0xFFFu;
}
// nearest allowed note (a MIDI note number, may be fractional input) to m
inline float nearestAllowed(float m, unsigned mask) {
    const int base = (int) std::floor(m); float best = (float) base, bd = 1e9f;
    for (int c = base - 7; c <= base + 8; ++c) { const int pc = ((c % 12) + 12) % 12; if (!(mask & (1u << pc))) continue; const float d = std::fabs((float) c - m); if (d < bd) { bd = d; best = (float) c; } }
    return best;
}

// ---------------------------------------------------------------- detection (YIN)
struct YinState { std::vector<float> d, cm; void init(int tauMax) { d.assign((size_t) tauMax + 2, 0.f); cm.assign((size_t) tauMax + 2, 0.f); } };
// x holds W + tauMax samples. Returns the period in samples (fractional), 0 = no clear pitch. conf = 1 - normalised difference.
inline float yinPeriod(const float* x, int W, int tauMin, int tauMax, float thr, YinState& st, float* conf) {
    float* d = st.d.data(); float* cm = st.cm.data(); *conf = 0.f;
    float en = 0.f; for (int j = 0; j < W; ++j) en += x[j] * x[j]; if (en < 1e-7f * (float) W) return 0.f;
    for (int tau = 1; tau <= tauMax; ++tau) { float s = 0.f; const float* y = x + tau; for (int j = 0; j < W; ++j) { const float df = x[j] - y[j]; s += df * df; } d[tau] = s; }
    cm[0] = 1.f; float run = 0.f; for (int tau = 1; tau <= tauMax; ++tau) { run += d[tau]; cm[tau] = run > 1e-12f ? d[tau] * (float) tau / run : 1.f; }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau) if (cm[tau] < thr) { while (tau + 1 < tauMax && cm[tau + 1] < cm[tau]) ++tau; best = tau; break; }
    if (best < 0) { best = tauMin; for (int tau = tauMin; tau < tauMax; ++tau) if (cm[tau] < cm[best]) best = tau; if (cm[best] > 0.3f) return 0.f; }
    *conf = 1.f - cm[best]; float t = (float) best;
    if (best > 1 && best < tauMax) { const float a = cm[best - 1], b = cm[best], c = cm[best + 1], den = a - 2.f * b + c; if (std::fabs(den) > 1e-9f) t += 0.5f * (a - c) / den; }
    return t;
}

// One detection run on a plain signal (no decimation): x ends at index n (exclusive). Used by tests and offline tools.
inline float yinF0(const float* x, size_t n, double sr, float fmin = 70.f, float fmax = 1000.f) {
    const int tauMax = (int) std::ceil(sr / fmin), tauMin = std::max(2, (int) std::floor(sr / fmax)), W = std::max(tauMax, 1024);
    if (n < (size_t) (W + tauMax)) return 0.f; YinState st; st.init(tauMax); float c; const float p = yinPeriod(x + n - (size_t) (W + tauMax), W, tauMin, tauMax, 0.15f, st, &c);
    return p > 0.f ? (float) (sr / p) : 0.f;
}

struct PitchFrames { int hop = 256; std::vector<float> f0; };   // f0[i]: pitch in Hz at sample i*hop, 0 = unvoiced
inline PitchFrames yinTrack(const float* x, size_t n, double sr, int hop = 256, float fmin = 75.f, float fmax = 1000.f) {
    PitchFrames pf; pf.hop = hop; const size_t nf = n / (size_t) hop + 1; pf.f0.assign(nf, 0.f); if (n < 64) return pf;
    const double sd = sr / 2.0; const int tauMax = (int) std::ceil(sd / fmin), tauMin = std::max(2, (int) std::floor(sd / fmax)), W = 512, tot = W + tauMax;
    std::vector<float> y(n / 2 + 1, 0.f); for (size_t i = 0; i + 1 < n; i += 2) y[i / 2] = 0.5f * (x[i] + x[i + 1]);
    YinState st; st.init(tauMax); std::vector<float> win((size_t) tot);
    for (size_t f = 0; f < nf; ++f) {
        const long c = (long) (f * (size_t) hop / 2), s0 = c - tot / 2;
        for (int k = 0; k < tot; ++k) { const long i = s0 + k; win[(size_t) k] = i >= 0 && i < (long) y.size() ? y[(size_t) i] : 0.f; }
        float conf; const float p = yinPeriod(win.data(), W, tauMin, tauMax, 0.15f, st, &conf); pf.f0[f] = p > 0.f ? (float) (sd / p) : 0.f;
    }
    // remove single-frame octave jumps and blips
    auto& f = pf.f0; for (size_t i = 1; i + 1 < f.size(); ++i) {
        if (f[i] > 0.f && f[i - 1] > 0.f && f[i + 1] > 0.f) { const float a = f[i - 1], b = f[i], c = f[i + 1]; const float mid = std::max(std::min(a, b), std::min(std::max(a, b), c)); if (std::fabs(std::log2(b / mid)) > 0.4f) f[i] = mid; }
        else if (f[i] > 0.f && f[i - 1] <= 0.f && f[i + 1] <= 0.f) f[i] = 0.f;
    }
    return pf;
}

// ---------------------------------------------------------------- the decision part: pitch -> shift ratios
struct PitchParams {
    bool mainOn = false, harmOn = false, robot = false, hsnap = true;
    int key = 0, scale = 0, robotPc = 0; unsigned userMask = 0xFFF;
    float speedMs = 25.f, amount = 1.f, humanize = 0.3f, preserve = 1.f, fshift = 0.f, vib = 0.6f, refHz = 440.f, hwet = 0.7f;
    float hint[3] { 4.f, 7.f, -12.f }, hlvl[3] { 0.6f, 0.5f, 0.f };
    bool active() const { return mainOn || harmOn; }
    static PitchParams fromParams(const float* P) {
        PitchParams p; p.mainOn = P[pc_on] > 0.5f; p.harmOn = P[hm_on] > 0.5f; p.robot = P[rb_on] > 0.5f && p.mainOn; p.robotPc = (int) P[rb_note]; p.hsnap = P[hm_snap] > 0.5f;
        p.key = (int) P[pc_key]; p.scale = (int) P[pc_scale]; p.userMask = 0; for (int i = 0; i < 12; ++i) if (P[pc_n0 + i] > 0.5f) p.userMask |= 1u << i;
        p.speedMs = P[pc_speed]; p.amount = P[pc_amt]; p.humanize = P[pc_hum]; p.preserve = P[pc_form]; p.fshift = P[pc_fshift]; p.vib = P[pc_vib]; p.refHz = P[pc_ref]; p.hwet = P[hm_wet];
        p.hint[0] = P[hm_i1]; p.hint[1] = P[hm_i2]; p.hint[2] = P[hm_i3]; p.hlvl[0] = P[hm_l1]; p.hlvl[1] = P[hm_l2]; p.hlvl[2] = P[hm_l3]; return p;
    }
};

class PitchBrain {
public:
    void reset() { voiced_ = false; mAvg_ = c_ = 0.f; note_ = -1000.f; held_ = 0.f; }
    // Called once per analysis hop. f0 = 0 for unvoiced. ratios[0] = main voice, ratios[1..3] = harmony voices.
    void update(float f0, float hopMs, const PitchParams& p, float* ratios) {
        ratios[0] = ratios[1] = ratios[2] = ratios[3] = 1.f;
        if (!(f0 > 0.f)) { voiced_ = false; return; }
        const float mIn = hzToMidi(f0, p.refHz); const unsigned mask = allowedMask(p.key, p.scale, p.userMask);
        if (!voiced_) { mAvg_ = mIn; c_ = 0.f; note_ = -1000.f; held_ = 0.f; voiced_ = true; }
        mAvg_ += (1.f - std::exp(-hopMs / 120.f)) * (mIn - mAvg_);
        float note;
        if (p.robot) { const float pc = (float) p.robotPc; note = pc + 12.f * std::round((mAvg_ - pc) / 12.f); }
        else {
            note = note_;   // hysteresis: keep the note while the average stays near it
            if (!(note > -999.f) || !(mask & (1u << (((int) std::lround(note) % 12 + 12) % 12))) || std::fabs(mAvg_ - note) > 0.7f) note = nearestAllowed(mAvg_, mask);
        }
        if (note != note_) { note_ = note; held_ = 0.f; } else held_ += hopMs;
        const float tau = p.speedMs * (1.f + 3.f * p.humanize * std::min(1.f, held_ / 400.f)), a = tau < 0.5f ? 1.f : 1.f - std::exp(-hopMs / tau);
        c_ += (note - mAvg_ - c_) * a;
        const float amt = p.mainOn ? p.amount : 0.f, s = amt * (c_ - (1.f - p.vib) * (mIn - mAvg_)), mo = mIn + s;
        ratios[0] = clampRatio(s);
        if (p.harmOn) for (int i = 0; i < 3; ++i) {
            float shift = p.hint[i];
            if (p.hsnap) shift = nearestAllowed(note_ + p.hint[i], mask) - note_;   // diatonic: a third stays a third of the scale
            ratios[i + 1] = clampRatio(mo + shift - mIn);
        }
    }
    float currentNote() const { return note_; }
private:
    static float clampRatio(float semis) { return std::exp2(std::max(-12.f, std::min(12.f, semis)) / 12.f); }
    bool voiced_ = false; float mAvg_ = 0.f, c_ = 0.f, note_ = -1000.f, held_ = 0.f;
};

// ---------------------------------------------------------------- streaming corrector (the plugin)
class LivePitch {
public:
    static constexpr int kVoices = 4;           // voice 0: the corrected voice, 1..3: harmony voices
    static constexpr unsigned kMask = 8191;     // ring size 8192
    void prepare(double sr) {
        sr_ = sr; const double k = sr / 44100.0; dout_ = (int) std::lround(1536.0 * k); hop_ = std::max(64, (int) std::lround(256.0 * k) & ~1);
        const double sd = sr / 2.0; tauMax_ = (int) std::ceil(sd / 75.0); tauMin_ = std::max(2, (int) std::floor(sd / 1000.0)); W_ = 512; tot_ = W_ + tauMax_;
        yin_.init(tauMax_); win_.assign((size_t) tot_, 0.f); dec_.assign(8192, 0.f);
        inL_.assign(kMask + 1, 0.f); inR_.assign(kMask + 1, 0.f); for (int v = 0; v < kVoices; ++v) { for (int c = 0; c < 2; ++c) ob_[v][c].assign(kMask + 1, 0.f); wb_[v].assign(kMask + 1, 0.f); }
        T0_ = 0.005 * sr; reset();
    }
    int latency() const { return dout_; }
    void reset() {
        n_ = 0; nDec_ = 0; nEst_ = 0; brain_.reset(); for (int v = 0; v < kVoices; ++v) { vinit_[v] = false; for (int c = 0; c < 2; ++c) std::fill(ob_[v][c].begin(), ob_[v][c].end(), 0.f); std::fill(wb_[v].begin(), wb_[v].end(), 0.f); }
        std::fill(inL_.begin(), inL_.end(), 0.f); std::fill(inR_.begin(), inR_.end(), 0.f); lastDec_ = 0.f;
    }
    // One stereo sample in, one out (delayed by latency()).
    inline void tick(float& l, float& r, const PitchParams& p) {
        const unsigned wi = (unsigned) n_ & kMask; inL_[wi] = l; inR_[wi] = r;
        if (n_ & 1) { dec_[(size_t) nDec_ & 8191] = 0.5f * (lastDec_ + 0.5f * (l + r)); ++nDec_; } else lastDec_ = 0.5f * (l + r);
        if ((n_ % hop_) == hop_ - 1 && nDec_ >= tot_) detect(p);
        const long e = n_ - dout_; float oL = 0.f, oR = 0.f;
        for (int v = 0; v < kVoices; ++v) {
            const bool act = v == 0 ? true : (p.harmOn && p.hlvl[v - 1] > 0.001f);
            if (!act) { if (vinit_[v]) { vinit_[v] = false; std::fill(ob_[v][0].begin(), ob_[v][0].end(), 0.f); std::fill(ob_[v][1].begin(), ob_[v][1].end(), 0.f); std::fill(wb_[v].begin(), wb_[v].end(), 0.f); } continue; }
            if (!vinit_[v]) { vinit_[v] = true; vsm_[v] = (double) e + 1.0; vam_[v] = vsm_[v]; vmark_[v] = false; }
            while (grain(v, p, e)) {}
            if (e >= 0) {
                const unsigned ri = (unsigned) e & kMask; const float nw = 1.f / std::max(0.2f, wb_[v][ri]); const float gl = ob_[v][0][ri] * nw, gr = ob_[v][1][ri] * nw; ob_[v][0][ri] = 0.f; ob_[v][1][ri] = 0.f; wb_[v][ri] = 0.f;
                const float g = v == 0 ? 1.f : p.hlvl[v - 1] * p.hwet; oL += gl * g; oR += gr * g;
            }
        }
        ++n_; if (e >= 0) { l = oL; r = oR; } else { l = 0.f; r = 0.f; }
    }
private:
    struct Est { double tc = 0; float tin = 0; float ratio[4] { 1, 1, 1, 1 }; bool voiced = false; };
    void detect(const PitchParams& p) {
        for (int k = 0; k < tot_; ++k) win_[(size_t) k] = dec_[(size_t) (nDec_ - tot_ + k) & 8191];
        float conf; const float per = yinPeriod(win_.data(), W_, tauMin_, tauMax_, 0.15f, yin_, &conf);
        float f0 = per > 0.f ? (float) ((sr_ / 2.0) / per) : 0.f;
        // a one-frame blip of an octave is more likely a detection slip than music: ignore it against the previous estimate
        if (f0 > 0.f && prevF0_ > 0.f && std::fabs(std::log2(f0 / prevF0_)) > 0.75f && !jumpSeen_) { jumpSeen_ = true; f0 = prevF0_; } else jumpSeen_ = false;
        prevF0_ = f0;
        Est& e = ests_[(size_t) nEst_ & 63]; ++nEst_; e.tc = (double) n_ - (double) tot_; e.voiced = f0 > 0.f; e.tin = f0 > 0.f ? (float) (sr_ / f0) : 0.f;
        brain_.update(f0, (float) (1000.0 * hop_ / sr_), p, e.ratio);
    }
    const Est* estAt(double t) const {
        if (nEst_ == 0) return nullptr; const int cnt = std::min(nEst_, 64); const Est* best = nullptr; double bd = 1e18;
        for (int k = 0; k < cnt; ++k) { const Est& e = ests_[(size_t) (nEst_ - 1 - k) & 63]; const double d = std::fabs(e.tc - t); if (d < bd) { bd = d; best = &e; } else if (e.tc < t) break; }
        return best;
    }
    // Places the next grain of voice v if its window reaches the output sample e that is being finished now. Returns false when it does not.
    bool grain(int v, const PitchParams& p, long e) {
        const double sm = vsm_[v]; const Est* es = estAt(sm); double Tin, r = 1.0, step = 1.0; const bool voiced = es && es->voiced;
        if (voiced) { Tin = es->tin; r = es->ratio[v]; step = std::pow(r, 1.0 - (double) p.preserve) * std::exp2((double) p.fshift / 12.0); } else Tin = T0_;
        const double Tout = std::max(0.0007 * sr_, std::min(0.02 * sr_, Tin / r));
        const double Hw = std::max(0.0007 * sr_, std::min(0.02 * sr_, Tin / step));   // window half width = one period of the content that is read
        if (sm - Hw > (double) e) return false;
        double am = vam_[v];
        auto refine = [&](double c) {   // pitch mark: the highest peak of the waveform near the predicted position, so every grain is centred on the same feature of the cycle
            const long ctr = (long) std::lround(c), rad = std::max(1L, (long) (0.25 * Tin)); long best = ctr; float bv = -1e9f; const long top = n_ - 1;
            for (long s = ctr - rad; s <= ctr + rad; ++s) { const long q = std::min(s, top); const float val = 0.5f * (inL_[(unsigned) q & kMask] + inR_[(unsigned) q & kMask]); if (val > bv) { bv = val; best = s; } }
            return (double) best; };
        if (!voiced) { am = sm; vmark_[v] = false; }
        else {
            if (!vmark_[v]) { am = refine(sm); vmark_[v] = true; }
            while (am + 0.5 * Tin < sm) am = refine(am + Tin);
            while (am - 0.5 * Tin > sm) am = refine(am - Tin);
            if (std::fabs(am - sm) > 4.0 * Tin) am = refine(sm);
        }
        vam_[v] = am; const long lo = (long) std::ceil(sm - Hw), hi = (long) std::floor(sm + Hw);
        const double maxRead = (double) (n_ - 2), minRead = (double) (n_ - (long) kMask + 8);
        for (long o = lo; o <= hi; ++o) {
            const double t = (double) o - sm; const double w = 0.5 * (1.0 + std::cos(3.14159265358979 * t / Hw));
            double rp = am + t * step; if (rp > maxRead) rp = maxRead; if (rp < minRead) rp = minRead;
            const unsigned oi = (unsigned) o & kMask; ob_[v][0][oi] += (float) w * cubicAt(inL_.data(), kMask, rp); ob_[v][1][oi] += (float) w * cubicAt(inR_.data(), kMask, rp); wb_[v][oi] += (float) w;
        }
        vsm_[v] = sm + Tout; return true;
    }
    double sr_ = 48000, T0_ = 240; int dout_ = 1536, hop_ = 256, tauMax_ = 300, tauMin_ = 22, W_ = 512, tot_ = 812;
    long n_ = 0, nDec_ = 0; int nEst_ = 0; float lastDec_ = 0.f, prevF0_ = 0.f; bool jumpSeen_ = false;
    std::vector<float> inL_, inR_, dec_, win_, ob_[kVoices][2], wb_[kVoices]; YinState yin_; Est ests_[64]; PitchBrain brain_;
    bool vinit_[kVoices] {}, vmark_[kVoices] {}; double vsm_[kVoices] {}, vam_[kVoices] {};
};

// ---------------------------------------------------------------- notes (the editor)
struct Note { int a = 0, b = 0; float midi = 60.f; };   // frames [a, b), median pitch as a (fractional) MIDI note
inline std::vector<Note> segmentNotes(const PitchFrames& pf, double sr, float refHz = 440.f, float minMs = 70.f) {
    std::vector<Note> out; const int nf = (int) pf.f0.size(); const int minF = std::max(2, (int) std::lround(minMs * 0.001 * sr / pf.hop)); int i = 0;
    while (i < nf) {
        if (!(pf.f0[(size_t) i] > 0.f)) { ++i; continue; }
        std::vector<float> run; const int start = i; int last = i, gap = 0, off = 0;
        for (int j = i; j < nf; ++j) {
            const float f = pf.f0[(size_t) j];
            if (!(f > 0.f)) { if (++gap > 2) break; continue; }
            gap = 0; const float m = hzToMidi(f, refHz);
            if (run.empty()) { run.push_back(m); last = j; continue; }
            std::vector<float> s = run; std::sort(s.begin(), s.end()); const float med = s[s.size() / 2];
            if (std::fabs(m - med) > 0.65f) { if (++off >= 3) break; continue; }
            off = 0; run.push_back(m); last = j;
        }
        const int end = last + 1;
        if (end - start >= minF && !run.empty()) { std::sort(run.begin(), run.end()); out.push_back({ start, end, run[run.size() / 2] }); }
        i = std::max(end, start + 1);
    }
    return out;
}

// ---------------------------------------------------------------- offline renderer (the editor)
struct RenderEdit {
    int hop = 256;                              // frame size of the three arrays below, in output samples
    std::vector<float> ratio, fshift, tmap;     // per output frame: pitch ratio, extra formant shift (semitones), input time in samples (empty = identity)
};
inline double lerpFrames(const std::vector<float>& a, double pos, int hop, double dflt) {
    if (a.empty()) return dflt; const double f = pos / hop; const long i = (long) std::floor(f); const double fr = f - (double) i;
    auto at = [&](long k) { return (double) a[(size_t) std::max(0L, std::min((long) a.size() - 1, k))]; }; return at(i) * (1.0 - fr) + at(i + 1) * fr;
}
inline std::vector<float> psolaRender(const std::vector<float>& x, double sr, const PitchFrames& pf, const RenderEdit& ed, float preserve, size_t outLen) {
    std::vector<float> out(outLen, 0.f), wsum(outLen, 0.f); const double T0 = 0.005 * sr; double sm = 0.0, am = 0.0; bool haveMark = false; const double nIn = (double) x.size();
    while (sm < (double) outLen + 0.02 * sr) {
        const double ratio = ed.ratio.empty() ? 1.0 : lerpFrames(ed.ratio, sm, ed.hop, 1.0), fs = lerpFrames(ed.fshift, sm, ed.hop, 0.0), tin = ed.tmap.empty() ? sm : lerpFrames(ed.tmap, sm, ed.hop, sm);
        const long fi = std::max(0L, std::min((long) pf.f0.size() - 1, (long) std::lround(tin / pf.hop))); const float f0 = pf.f0.empty() ? 0.f : pf.f0[(size_t) fi];
        const bool voiced = f0 > 0.f; const double Tin = voiced ? sr / f0 : T0, r = voiced ? ratio : 1.0; double step = 1.0;
        if (voiced) step = std::pow(r, 1.0 - (double) preserve) * std::exp2(fs / 12.0);
        const double Tout = std::max(0.0007 * sr, std::min(0.02 * sr, Tin / r)), Hw = std::max(0.0007 * sr, std::min(0.02 * sr, Tin / step));
        auto refine = [&](double c) {   // pitch mark: highest peak near the predicted position
            const long ctr = (long) std::lround(c), rad = std::max(1L, (long) (0.25 * Tin)); long best = ctr; float bv = -1e9f;
            for (long s = ctr - rad; s <= ctr + rad; ++s) { const long q = std::max(0L, std::min((long) x.size() - 1, s)); if (x[(size_t) q] > bv) { bv = x[(size_t) q]; best = s; } }
            return (double) best; };
        if (!voiced) { am = tin; haveMark = false; }
        else {
            if (!haveMark) { am = refine(tin); haveMark = true; }
            while (am + 0.5 * Tin < tin) am = refine(am + Tin);
            while (am - 0.5 * Tin > tin) am = refine(am - Tin);
            if (std::fabs(am - tin) > 4.0 * Tin) am = refine(tin);
        }
        const long lo = (long) std::ceil(sm - Hw), hi = (long) std::floor(sm + Hw);
        for (long o = lo; o <= hi; ++o) {
            if (o < 0 || o >= (long) outLen) continue;
            const double t = (double) o - sm, w = 0.5 * (1.0 + std::cos(3.14159265358979 * t / Hw)), rp = am + t * step;
            if (rp < 0.0 || rp > nIn - 1.0) continue; out[(size_t) o] += (float) w * cubicVec(x, rp); wsum[(size_t) o] += (float) w;
        }
        sm += Tout;
    }
    for (size_t i = 0; i < outLen; ++i) out[i] /= std::max(0.2f, wsum[i]);
    return out;
}

}  // namespace purrfect
