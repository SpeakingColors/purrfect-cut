// Purrfect Cut: spectral pitch shifting with formant control, for single voices and for chords. Pure C++, offline (the plugin runs it in a
// background thread on captured audio).
//
// A phase vocoder with peak locking: every spectral peak (a partial) is moved to its new frequency and its phase is advanced at the new
// frequency, so each partial stays a clean sine; the bins around the peak travel with it. Because partials are moved one by one, a chord can
// be edited note by note: every peak is assigned to the note whose harmonic series it belongs to, and each note gets its own ratio.
// The spectral envelope (true envelope from a cepstrum) is divided out and put back at the new place, so the voice keeps its formants (or
// moves them on their own with the formant control) instead of turning into a chipmunk.
//
// Honest limits: partials are processed in frames of about 93 ms, so sharp attacks smear a little (the editor keeps the original attack by
// cross-fading, see Notes.h); notes whose harmonics overlap exactly (octaves) cannot be separated; noisy or inharmonic material is moved as a
// whole with the nearest peak. Very large shifts (more than an octave) get more artefacts.
#pragma once
#include "Fft.h"
#include "Pitch.h"
#include <complex>
#include <functional>

namespace purrfect {

struct SpecVoice { float f0 = 0.f; float ratio = 1.f; };          // a note in this frame: fundamental in Hz and the pitch ratio to apply to its partials
struct SpecCtl {
    std::vector<SpecVoice> voices;   // notes present in this frame (empty: everything uses defaultRatio)
    float defaultRatio = 1.f;        // for partials that belong to no listed note
    float fshift = 0.f;              // extra formant shift in semitones
    float preserve = 1.f;            // 1 = formants stay where they are, 0 = formants move with the pitch
};

class SpectralShifter {
public:
    explicit SpectralShifter(int n = 4096) { init(n); }
    void init(int n) {
        N_ = n; hop_ = n / 4; fft_.init(n); win_.assign((size_t) n, 0.f); for (int i = 0; i < n; ++i) win_[(size_t) i] = 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979 * i / n));
        X_.assign((size_t) n, {}); Y_.assign((size_t) n, {}); mag_.assign((size_t) n / 2 + 1, 0.0); ph_.assign((size_t) n / 2 + 1, 0.0); prevPh_ = ph_; env_.assign((size_t) n / 2 + 1, 1.0); tmp_.assign((size_t) n, {}); E_.assign((size_t) n, {});
    }
    int frameSize() const { return N_; }
    // ctl is asked once per frame with the time (in samples, the frame centre) and fills in what to do there.
    std::vector<float> process(const std::vector<float>& x, double sr, const std::function<void(double, SpecCtl&)>& ctl) {
        const size_t n = x.size(); std::vector<float> out(n + (size_t) N_, 0.f); if (n < 64) return std::vector<float>(x);
        prevPeaks_.clear(); std::fill(prevPh_.begin(), prevPh_.end(), 0.0); bool havePrev = false;
        const long first = -(long) (N_ - hop_), last = (long) n;
        for (long s0 = first; s0 < last; s0 += hop_) {
            for (int i = 0; i < N_; ++i) { const long q = s0 + i; X_[(size_t) i] = { q >= 0 && q < (long) n ? (double) x[(size_t) q] * win_[(size_t) i] : 0.0, 0.0 }; }
            fft_.transform(X_.data(), false);
            const int nb = N_ / 2; for (int k = 0; k <= nb; ++k) { mag_[(size_t) k] = std::abs(X_[(size_t) k]); ph_[(size_t) k] = std::arg(X_[(size_t) k]); }
            SpecCtl c; ctl((double) s0 + 0.5 * N_, c);
            const bool trivial = std::fabs(c.defaultRatio - 1.f) < 1e-4f && std::fabs(c.fshift) < 1e-3f && std::all_of(c.voices.begin(), c.voices.end(), [](const SpecVoice& v) { return std::fabs(v.ratio - 1.f) < 1e-4f; });
            synth(c, trivial, sr, havePrev);
            for (int i = 0; i < N_; ++i) { const long q = s0 + i; if (q >= 0 && q < (long) out.size()) out[(size_t) q] += (float) (tmp_[(size_t) i].real() * win_[(size_t) i] / (1.5 * N_)); }
            prevPh_ = ph_; havePrev = true;
        }
        out.resize(n); return out;
    }
private:
    struct Peak { double srcBin, outBin, outPhase; };
    void envelope(double /*sr*/, double /*lowestF0*/) {
        // spectral envelope: the partial peaks joined by straight lines in the log domain (hugs the partials exactly; works for one voice or several)
        const int nb = N_ / 2; double mx = 0; for (int k = 1; k < nb; ++k) mx = std::max(mx, mag_[(size_t) k]);
        std::vector<int> pk; std::vector<double> pv;
        for (int k = 2; k < nb - 2; ++k) { const double m = mag_[(size_t) k]; if (m > mx * 1e-3 && m > mag_[(size_t) k - 1] && m >= mag_[(size_t) k + 1] && m > mag_[(size_t) k - 2] && m >= mag_[(size_t) k + 2]) {
            const double l0 = std::log(mag_[(size_t) k - 1] + 1e-12), l1 = std::log(m), l2 = std::log(mag_[(size_t) k + 1] + 1e-12), den = l0 - 2 * l1 + l2;
            pk.push_back(k); pv.push_back(den < -1e-9 ? l1 - 0.125 * (l2 - l0) * (l2 - l0) / den : l1); } }
        if (pk.empty()) { for (int k = 0; k <= nb; ++k) env_[(size_t) k] = std::max(mx, 1e-9); return; }
        size_t j = 0;
        for (int k = 0; k <= nb; ++k) {
            while (j + 1 < pk.size() && pk[j + 1] <= k) ++j;
            double v; if (k <= pk.front()) v = pv.front(); else if (k >= pk.back()) v = pv.back(); else { const double t = (double) (k - pk[j]) / (pk[j + 1] - pk[j]); v = pv[j] * (1 - t) + pv[j + 1] * t; }
            env_[(size_t) k] = std::exp(v);
        }
    }
    double envAt(double bin) const { const int nb = N_ / 2; if (bin <= 0) return env_[0]; if (bin >= nb) return env_[(size_t) nb]; const int i = (int) bin; const double f = bin - i; return std::exp(std::log(env_[(size_t) i]) * (1 - f) + std::log(env_[(size_t) i + 1]) * f); }
    void synth(const SpecCtl& c, bool trivial, double sr, bool havePrev) {
        const int nb = N_ / 2; const double binHz = sr / N_; std::fill(Y_.begin(), Y_.end(), std::complex<double>(0, 0));
        if (trivial) { for (int k = 0; k <= nb; ++k) Y_[(size_t) k] = X_[(size_t) k]; prevPeaks_.clear(); mirrorAndInvert(); return; }
        double lowest = 400.0; for (auto& v : c.voices) if (v.f0 > 0) lowest = std::min(lowest, (double) v.f0); envelope(sr, c.voices.empty() ? 150.0 : lowest);
        double maxM = 0; for (int k = 1; k < nb; ++k) maxM = std::max(maxM, mag_[(size_t) k]);
        std::vector<int> pk; for (int k = 2; k < nb - 2; ++k) { const double m = mag_[(size_t) k]; if (m > maxM * 1e-4 && m > mag_[(size_t) k - 1] && m >= mag_[(size_t) k + 1] && m > mag_[(size_t) k - 2] && m >= mag_[(size_t) k + 2]) pk.push_back(k); }
        std::vector<Peak> now; const double expect = 2.0 * 3.14159265358979 * hop_ / N_;
        const double fOff = std::exp2((double) c.fshift / 12.0);
        for (size_t pi = 0; pi < pk.size(); ++pi) {
            const int k = pk[pi]; const int lo = pi == 0 ? 0 : (pk[pi - 1] + k) / 2 + 1, hi = pi + 1 == pk.size() ? nb : (k + pk[pi + 1]) / 2;
            // true frequency of the partial from the phase advance
            double dphi = ph_[(size_t) k] - prevPh_[(size_t) k] - expect * k; dphi -= 2.0 * 3.14159265358979 * std::round(dphi / (2.0 * 3.14159265358979));
            double fb = havePrev ? k + dphi / expect : (double) k; if (std::fabs(fb - k) > 1.5) fb = k;
            const double fHz = fb * binHz; double ratio = c.defaultRatio, bestD = 1e9;
            for (auto& v : c.voices) { if (!(v.f0 > 0)) continue; const double h = std::max(1.0, std::round(fHz / v.f0)); const double d = std::fabs(1200.0 * std::log2(fHz / (h * v.f0))); if (d < 70.0 && d < bestD) { bestD = d; ratio = v.ratio; } }
            if (std::fabs(ratio - 1.0) < 1e-5 && std::fabs(c.fshift) < 1e-3) { for (int j = lo; j <= hi; ++j) Y_[(size_t) j] += X_[(size_t) j]; now.push_back({ fb, fb, ph_[(size_t) k] }); continue; }
            const double fo = fb * ratio, rho = std::pow(ratio, 1.0 - (double) c.preserve) * fOff;
            // continue the phase of the matching partial of the previous frame
            double theta = ph_[(size_t) k]; for (auto& q : prevPeaks_) if (std::fabs(q.srcBin - fb) < 1.2) { theta = q.outPhase + expect * 0.5 * (q.outBin + fo); break; }
            const double dBin = std::round(fo - fb), pPhase = ph_[(size_t) k]; const double e0 = envAt(fb);
            for (int j = lo; j <= hi; ++j) {   // move the partial and the bins that belong to it (whole bins, so the window lobe keeps its shape; the exact frequency comes from the phase advance)
                const int j0 = j + (int) dBin; if (j0 < 0 || j0 > nb) continue;
                const double gain = std::min(16.0, std::max(0.0625, envAt(j0 / rho) / (e0 + 1e-12)));
                const double m = mag_[(size_t) j] * gain; const double rel = ph_[(size_t) j] - pPhase; Y_[(size_t) j0] += std::polar(m, theta + rel);
            }
            now.push_back({ fb, fo, theta });
        }
        prevPeaks_.swap(now); mirrorAndInvert();
    }
    void mirrorAndInvert() {
        const int nb = N_ / 2; Y_[0] = { Y_[0].real(), 0.0 }; Y_[(size_t) nb] = { Y_[(size_t) nb].real(), 0.0 };
        for (int k = 1; k < nb; ++k) Y_[(size_t) (N_ - k)] = std::conj(Y_[(size_t) k]);
        tmp_ = Y_; fft_.transform(tmp_.data(), true);
    }
    int N_ = 4096, hop_ = 1024; Fft fft_; std::vector<double> win_, mag_, ph_, prevPh_, env_; std::vector<std::complex<double>> X_, Y_, tmp_, E_; std::vector<Peak> prevPeaks_;
};

}  // namespace purrfect
