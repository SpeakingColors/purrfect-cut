// Spectral shifter tests: single voice shift accuracy and formant keeping, chord note-by-note editing.
#include "../src/core/Spectral.h"
#include "../src/core/Poly.h"
#include <cstdio>
using namespace purrfect;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double SR = 44100.0, PI = 3.14159265358979;
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
static std::vector<float> vowel(double f0, double secs, double vs = 1.0) {
    const size_t n = (size_t) (secs * SR); std::vector<float> x(n, 0.f);
    for (int h = 1; h <= 40; ++h) { const double hf = h * f0; if (hf > 9000) break; const double e1 = 1.0 / (1.0 + std::pow((hf - 700.0 * vs) / (110.0 * vs), 2.0)), e2 = 0.6 / (1.0 + std::pow((hf - 1700.0 * vs) / (160.0 * vs), 2.0)), a = (e1 + e2 + 0.02) / 1.0;
        for (size_t i = 0; i < n; ++i) x[i] += (float) (0.3 * a * std::sin(2 * PI * hf * (double) i / SR + 0.21 * h)); }
    return x;
}
static std::vector<float> chordOf(const std::vector<double>& fs, double secs) {
    const size_t n = (size_t) (secs * SR); std::vector<float> x(n, 0.f);
    for (double f : fs) for (int h = 1; h <= 10; ++h) { const double hf = f * h; if (hf > 9000) break; const double a = 0.2 / h; for (size_t i = 0; i < n; ++i) x[i] += (float) (a * std::sin(2 * PI * hf * (double) i / SR + 0.37 * h)); }
    return x;
}
// pitch of a steady harmonic sound near an expected fundamental: scan +-120 cents for the best sum of the first 6 harmonic amplitudes
static double harmPitch(const std::vector<float>& x, size_t from, size_t len, double expect) {
    double best = -1, bf = expect;
    for (double c = -120; c <= 120; c += 0.5) { const double f0 = expect * std::exp2(c / 1200.0); double sum = 0;
        for (int h = 1; h <= 6; ++h) { double re = 0, im = 0; for (size_t i = 0; i < len; i += 2) { const double w = 0.5 - 0.5 * std::cos(2 * PI * (double) i / len), v = x[from + i] * w, a = 2 * PI * f0 * h * (double) i / SR; re += v * std::cos(a); im += v * std::sin(a); } sum += re * re + im * im; }
        if (sum > best) { best = sum; bf = f0; } }
    return bf;
}
int main() {
    SpectralShifter sh(4096);
    // 1) single voice, +5 semitones: pitch exact; with formants kept the harmonic amplitudes follow the ORIGINAL vowel envelope at the new
    //    frequencies; with formants following they keep the amplitudes they had (the whole spectrum just moves up)
    { auto x = vowel(220.0, 1.5); const double r = std::exp2(5.0 / 12.0);
      auto env = [](double hf) { const double e1 = 1.0 / (1.0 + std::pow((hf - 700.0) / 110.0, 2.0)), e2 = 0.6 / (1.0 + std::pow((hf - 1700.0) / 160.0, 2.0)); return 0.3 * (e1 + e2 + 0.02); };
      auto amp = [](const std::vector<float>& v, double f) { double re = 0, im = 0; const size_t len = 16384; for (size_t i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * PI * (double) i / len), q = v[20000 + i] * w, a = 2 * PI * f * (double) i / SR; re += q * std::cos(a); im += q * std::sin(a); } return 2 * std::sqrt(re * re + im * im) / (len * 0.5); };
      for (int keep = 1; keep >= 0; --keep) {
        auto y = sh.process(x, SR, [&](double, SpecCtl& c) { c.defaultRatio = (float) r; c.preserve = (float) keep; });
        const double f = harmPitch(y, 20000, 16384, 220.0 * r); double worst = 0;
        for (int h = 1; h <= 10; ++h) { const double got = amp(y, 220.0 * r * h), want = keep ? env(220.0 * r * h) : env(220.0 * h); worst = std::max(worst, std::fabs(20.0 * std::log10(got / want))); }
        std::printf("shift +5, formants %s: pitch error %.1f cents; worst harmonic level error %.1f dB\n", keep ? "kept" : "following", cents(f, 220.0 * r), worst);
        CHECK(std::fabs(cents(f, 220.0 * r)) < 6.0, "pitch error %.1f cents", cents(f, 220.0 * r)); CHECK(worst < 3.0, "formants %s: harmonic level error %.1f dB", keep ? "kept" : "following", worst); } }
    // 2) down an octave and up a fifth accuracy on a different pitch
    for (double semi : { -12.0, -3.0, 7.0 }) { auto x = vowel(165.0, 1.5); const double r = std::exp2(semi / 12.0); auto y = sh.process(x, SR, [&](double, SpecCtl& c) { c.defaultRatio = (float) r; });
      const double f = harmPitch(y, 20000, 16384, 165.0 * r); std::printf("shift %+.0f: pitch %.2f Hz error %.1f cents\n", semi, f, cents(f, 165.0 * r)); CHECK(std::fabs(cents(f, 165.0 * r)) < 8.0, "shift %.0f error %.1f", semi, cents(f, 165.0 * r)); }
    // 3) chord: move only the E4 of C E G up one semitone (to F4); C4 and G4 must stay
    { auto x = chordOf({ 261.63, 329.63, 392.0 }, 2.0); const double rE = std::exp2(1.0 / 12.0);
      auto y = sh.process(x, SR, [&](double, SpecCtl& c) { c.voices = { { 261.63f, 1.f }, { 329.63f, (float) rE }, { 392.0f, 1.f } }; c.defaultRatio = 1.f; });
      PolyDetector pd(SR, 6); PolyFrame f = pd.detect(y.data() + 30000); const double want[3] = { 261.63, 329.63 * rE, 392.0 }; double worst = 0; int m = 0;
      for (double t : want) { double bd = 1e9; for (float q : f.f0) bd = std::min(bd, std::fabs(cents(q, t))); if (bd < 15.0) ++m; worst = std::max(worst, bd); }
      std::printf("chord C-E-G with only E raised one semitone: matched %d of 3, worst %.1f cents, detected %zu voices\n", m, worst, f.f0.size()); CHECK(m == 3, "chord edit matched %d of 3 (worst %.1f cents)", m, worst); }
    // 4) transparency: no change requested returns the input
    { auto x = vowel(200.0, 1.0); auto y = sh.process(x, SR, [&](double, SpecCtl&) {}); double e = 0, r = 0; for (size_t i = 6000; i + 6000 < x.size(); ++i) { const double d = (double) y[i] - x[i]; e += d * d; r += (double) x[i] * x[i]; } std::printf("no edit: relative error %.2e\n", e / r); CHECK(e / r < 1e-3, "identity error %.2e", e / r); }
    if (fails) { std::printf("%d failures\n", fails); return 1; } std::printf("spectral tests passed\n"); return 0;
}
