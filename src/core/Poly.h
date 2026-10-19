// Purrfect Cut: multi-pitch detection for chords. Pure C++.
//
// Method (a small version of the iterative harmonic salience idea): a long FFT frame, spectral whitening (so loud low partials do not
// dominate), then a salience for every candidate pitch on a 10 cent grid = weighted sum of the spectral peaks found at its harmonics.
// The best candidate is taken, its harmonics are damped in the spectrum, and the search repeats for the next voice. Each pitch is then
// refined from its own harmonic peaks (usually well under 5 cents on clean tones).
//
// Honest limits: it works on clean sustained chords of a few voices (up to 6). Two notes an octave apart share all harmonics, so the upper one
// is usually missed. Dense mixes, drums over the chord, heavy distortion and reverb tails confuse it. Notes shorter than about 140 ms are dropped.
#pragma once
#include "Fft.h"
#include "Pitch.h"
#include <algorithm>
#include <complex>

namespace purrfect {

struct PolyFrame { std::vector<float> f0; std::vector<float> level; };   // pitches in Hz found in one frame (loudest first), with their salience

class PolyDetector {
public:
    explicit PolyDetector(double sr = 44100.0, int maxVoices = 6) { prepare(sr, maxVoices); }
    void prepare(double sr, int maxVoices = 6) {
        sr_ = sr; maxV_ = std::max(1, std::min(8, maxVoices)); N_ = 8192; while (N_ * 0.5 > sr_ * 0.2) N_ >>= 1; while (sr_ / N_ > 6.0) N_ <<= 1;   // about 5 Hz per bin
        fft_.init(N_); hop_ = N_ / 4; win_.assign((size_t) N_, 0.f); for (int i = 0; i < N_; ++i) win_[(size_t) i] = 0.5f * (1.f - std::cos(6.28318530718f * (float) i / (float) N_));
        buf_.assign((size_t) N_, {}); mag_.assign((size_t) N_ / 2 + 1, 0.f); w_.assign((size_t) N_ / 2 + 1, 0.f);
    }
    int hop() const { return hop_; }
    int frameSize() const { return N_; }
    // one frame, N samples starting at x
    PolyFrame detect(const float* x) {
        PolyFrame out; const int nb = N_ / 2; const double bin = sr_ / N_;
        for (int i = 0; i < N_; ++i) buf_[(size_t) i] = { (double) x[i] * win_[(size_t) i], 0.0 };
        fft_.transform(buf_.data(), false);
        for (int k = 0; k <= nb; ++k) mag_[(size_t) k] = (float) std::abs(buf_[(size_t) k]);
        // whitening: divide by the local mean (about 700 Hz wide) and keep what stands out of it
        { const int W = std::max(8, (int) std::lround(350.0 / bin)); std::vector<double> cs((size_t) nb + 2, 0.0); for (int k = 0; k <= nb; ++k) cs[(size_t) k + 1] = cs[(size_t) k] + mag_[(size_t) k];
          double tot = 0; for (int k = 0; k <= nb; ++k) tot += mag_[(size_t) k]; const double floorv = 1e-4 * tot / (nb + 1) + 1e-9;
          for (int k = 0; k <= nb; ++k) { const int a = std::max(0, k - W), b = std::min(nb, k + W); const double m = (cs[(size_t) b + 1] - cs[(size_t) a]) / (b - a + 1) + floorv; w_[(size_t) k] = (float) std::max(0.0, mag_[(size_t) k] / m - 0.8); } }
        const double fmin = 60.0, fmax = std::min(1100.0, sr_ * 0.2); const int nc = (int) std::floor(1200.0 * std::log2(fmax / fmin) / 10.0);
        std::vector<float> sal((size_t) nc + 1, 0.f);
        auto salience = [&](double f0) {
            double s = 0; const int H = (int) std::min(12.0, (sr_ * 0.4) / f0);
            for (int h = 1; h <= H; ++h) {
                const double fc = f0 * h / bin, tol = std::max(1.3, f0 * h * 0.010 / bin); int a = (int) std::floor(fc - tol), b = (int) std::ceil(fc + tol); a = std::max(1, a); b = std::min(nb - 1, b);
                float m = 0.f; for (int k = a; k <= b; ++k) m = std::max(m, w_[(size_t) k]); s += std::pow((double) h, -0.75) * std::min(m, 40.f);
            }
            return s;
        };
        double first = 0;
        for (int v = 0; v < maxV_ + 3; ++v) {
            int best = -1; double bs = 0; for (int c = 0; c <= nc; ++c) { const double f0 = fmin * std::pow(2.0, 10.0 * c / 1200.0); sal[(size_t) c] = (float) salience(f0); if (sal[(size_t) c] > bs) { bs = sal[(size_t) c]; best = c; } }
            if (best < 0) break;
            // climb to the top of the local hump (the grid max can sit on a shoulder)
            const double fg = fmin * std::pow(2.0, 10.0 * best / 1200.0);
            if (v == 0) first = bs; else if (bs < 0.12 * first || bs < 0.9) break;
            int hits = 0; const double fr = refine(fg, &hits); if (!(fr > 0) || hits < 3) { dampAround(fg, 1.6); continue; }
            // a candidate that is only a sub- or super-harmonic of one already chosen is not a new voice
            bool dup = false; for (float q : out.f0) { const double r = fr / q; for (int m = 1; m <= 8; ++m) { if (std::fabs(1200.0 * std::log2(r * m)) < 35.0 || std::fabs(1200.0 * std::log2(r / m)) < 35.0) dup = true; } }
            dampAround(fr, 1.0);
            if (dup) continue; if ((int) out.f0.size() >= maxV_) break; out.f0.push_back((float) fr); out.level.push_back((float) bs);
        }
        return out;
    }
    // frames over a whole signal: frame i covers [i*hop, i*hop+N)
    std::vector<PolyFrame> analyse(const float* x, size_t n) {
        std::vector<PolyFrame> r; std::vector<float> tmp((size_t) N_);
        for (size_t s = 0; s + (size_t) N_ <= n; s += (size_t) hop_) { std::copy(x + s, x + s + N_, tmp.begin()); r.push_back(detect(tmp.data())); }
        return r;
    }
private:
    void dampAround(double f0, double widthMul) {
        const int nb = N_ / 2; const double bin = sr_ / N_; const int H = (int) std::min(14.0, (sr_ * 0.45) / f0);
        for (int h = 1; h <= H; ++h) { const double fc = f0 * h / bin, tol = std::max(1.6, f0 * h * 0.012 * widthMul / bin); const int a = std::max(1, (int) std::floor(fc - tol)), b = std::min(nb - 1, (int) std::ceil(fc + tol)); for (int k = a; k <= b; ++k) w_[(size_t) k] *= 0.3f; }
    }
    // f0 from the harmonic peaks near a grid candidate: amplitude weighted mean of peakFreq / h
    double refine(double f0, int* hits = nullptr) const {
        const int nb = N_ / 2; const double bin = sr_ / N_; double num = 0, den = 0; const int H = (int) std::min(10.0, (sr_ * 0.4) / f0);
        for (int h = 1; h <= H; ++h) {
            const double fc = f0 * h / bin, tol = std::max(1.3, f0 * h * 0.010 / bin); int a = std::max(2, (int) std::floor(fc - tol)), b = std::min(nb - 2, (int) std::ceil(fc + tol)); int pk = -1; float m = 0.f;
            for (int k = a; k <= b; ++k) if (w_[(size_t) k] > m) { m = w_[(size_t) k]; pk = k; }
            if (pk < 0 || m < 1.0f) continue;
            const double l = std::log(mag_[(size_t) pk - 1] + 1e-12), c = std::log(mag_[(size_t) pk] + 1e-12), r = std::log(mag_[(size_t) pk + 1] + 1e-12), dn = l - 2 * c + r;
            const double off = std::fabs(dn) > 1e-9 ? 0.5 * (l - r) / dn : 0.0; if (std::fabs(off) > 1.0) continue;
            const double fh = (pk + off) * bin / h, wgt = m / h; num += wgt * fh; den += wgt; if (hits && m >= 2.5f) ++*hits;
        }
        return den > 0 ? num / den : 0.0;
    }
    double sr_ = 44100; int N_ = 8192, hop_ = 2048, maxV_ = 6; Fft fft_; std::vector<float> win_, mag_, w_; std::vector<std::complex<double>> buf_;
};

// ---------------------------------------------------------------- notes from frames
struct PolyNote { int a = 0, b = 0; float midi = 60.f; std::vector<float> f0; double sampleA = 0, sampleB = 0; };   // frames [a,b) of the detector; f0 per frame (Hz)

inline std::vector<PolyNote> polyNotes(const std::vector<PolyFrame>& fr, int hop, int frameSize, float refHz = 440.f, int minFrames = 3) {
    struct Track { int a; int last; std::vector<float> f; }; std::vector<Track> act; std::vector<PolyNote> out;
    auto close = [&](Track& t) {
        if ((int) t.f.size() < minFrames) return; PolyNote n; n.a = t.a; n.b = t.last + 1; n.f0 = t.f; std::vector<float> s = t.f; std::sort(s.begin(), s.end()); n.midi = hzToMidi(s[s.size() / 2], refHz);
        n.sampleA = (double) t.a * hop + frameSize * 0.5 - hop * 0.5; n.sampleB = (double) (t.last + 1) * hop + frameSize * 0.5 - hop * 0.5; out.push_back(n); };
    for (int i = 0; i < (int) fr.size(); ++i) {
        std::vector<char> used(fr[(size_t) i].f0.size(), 0);
        for (auto& t : act) {
            if (t.last < i - 2) continue; const float tf = t.f.back(); int bi = -1; float bd = 0.8f;
            for (size_t k = 0; k < used.size(); ++k) if (!used[k]) { const float d = std::fabs(12.f * std::log2(fr[(size_t) i].f0[k] / tf)); if (d < bd) { bd = d; bi = (int) k; } }
            if (bi >= 0) { used[(size_t) bi] = 1; while (t.last < i - 1) { t.f.push_back(tf); ++t.last; } t.f.push_back(fr[(size_t) i].f0[(size_t) bi]); t.last = i; }
        }
        std::vector<Track> keep; for (auto& t : act) { if (t.last < i - 2) close(t); else keep.push_back(t); } act.swap(keep);
        for (size_t k = 0; k < used.size(); ++k) if (!used[k]) act.push_back({ i, i, { fr[(size_t) i].f0[k] } });
    }
    for (auto& t : act) close(t);
    std::sort(out.begin(), out.end(), [](const PolyNote& a, const PolyNote& b) { return a.a < b.a; });
    return out;
}

}  // namespace purrfect
