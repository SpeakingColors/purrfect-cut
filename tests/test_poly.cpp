// Multi-pitch tests: synthetic chords (harmonic stacks) detected within a few cents, note tracking, and the honest failure cases.
#include "../src/core/Poly.h"
#include <cstdio>
using namespace purrfect;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double SR = 44100.0, PI = 3.14159265358979;
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
static std::vector<float> chord(const std::vector<double>& fs, double secs, int nh = 10, double dec = 1.0) {
    const size_t n = (size_t) (secs * SR); std::vector<float> x(n, 0.f);
    for (double f : fs) for (int h = 1; h <= nh; ++h) { const double hf = f * h; if (hf > 9000) break; const double a = 0.2 / std::pow(h, dec); for (size_t i = 0; i < n; ++i) x[i] += (float) (a * std::sin(2 * PI * hf * (double) i / SR + 0.37 * h)); }
    return x;
}
static int matched(const std::vector<float>& found, const std::vector<double>& truth, double tolCents, double* worst) {
    int m = 0; *worst = 0; for (double t : truth) { double bd = 1e9; for (float f : found) bd = std::min(bd, std::fabs(cents(f, t))); if (bd <= tolCents) ++m; *worst = std::max(*worst, bd); } return m;
}
static void testChord(const char* name, std::vector<double> fs, int extraAllowed = 0) {
    PolyDetector pd(SR, 6); auto x = chord(fs, 1.0); PolyFrame f = pd.detect(x.data() + 8000);
    for (float q : f.f0) std::printf("   found %.2f Hz\n", q);
    double worst; const int m = matched(f.f0, fs, 20.0, &worst);
    CHECK(m == (int) fs.size(), "%s: found %d of %zu notes (detected %zu)", name, m, fs.size(), f.f0.size());
    CHECK((int) f.f0.size() <= (int) fs.size() + extraAllowed, "%s: %zu detections for %zu notes", name, f.f0.size(), fs.size());
    std::printf("%-22s notes %zu detected %zu matched %d worst %.1f cents\n", name, fs.size(), f.f0.size(), m, worst);
}
int main() {
    testChord("single A3", { 220.0 });
    testChord("C major C4 E4 G4", { 261.63, 329.63, 392.00 });
    testChord("A minor A3 C4 E4", { 220.0, 261.63, 329.63 });
    testChord("Cmaj7 C3 E3 G3 B3", { 130.81, 164.81, 196.00, 246.94 });
    testChord("D major voicing D3 A3 F#4", { 146.83, 220.0, 369.99 });
    testChord("G7 G3 B3 D4 F4", { 196.0, 246.94, 293.66, 349.23 });
    // tracking: two chords one after the other
    { PolyDetector pd(SR, 6); auto a = chord({ 261.63, 329.63, 392.0 }, 1.0), b = chord({ 220.0, 261.63, 329.63 }, 1.0); a.insert(a.end(), b.begin(), b.end());
      auto fr = pd.analyse(a.data(), a.size()); auto notes = polyNotes(fr, pd.hop(), pd.frameSize());
      int c4 = 0, e4 = 0, g4 = 0, a3 = 0; for (auto& n : notes) { const double f = midiToHz(n.midi, 440.f); if (std::fabs(cents(f, 261.63)) < 25) ++c4; if (std::fabs(cents(f, 329.63)) < 25) ++e4; if (std::fabs(cents(f, 392.0)) < 25) ++g4; if (std::fabs(cents(f, 220.0)) < 25) ++a3; }
      std::printf("tracking: %zu notes: C4 x%d (continues through both chords), E4 x%d, G4 x%d, A3 x%d\n", notes.size(), c4, e4, g4, a3);
      CHECK(g4 == 1 && a3 == 1, "G4 should end once and A3 start once (got %d, %d)", g4, a3); CHECK(c4 >= 1 && e4 >= 1, "shared notes must exist"); }
    // honest failure case, documented: an octave pair loses the upper note (not required to pass, only reported)
    { PolyDetector pd(SR, 6); auto x = chord({ 130.81, 261.63 }, 1.0); PolyFrame f = pd.detect(x.data() + 8000); std::printf("octave pair C3+C4: detected %zu voice(s) (the upper one is expected to be missed)\n", f.f0.size()); }
    // noise only: should detect nothing or very little
    { PolyDetector pd(SR, 6); std::vector<float> n(20000); unsigned s = 1; for (auto& v : n) { s = s * 1664525u + 1013904223u; v = ((float) (s >> 8) / 8388608.f - 1.f) * 0.2f; } PolyFrame f = pd.detect(n.data() + 2000); std::printf("white noise: %zu detections\n", f.f0.size()); CHECK(f.f0.size() <= 2, "noise gave %zu voices", f.f0.size()); }
    if (fails) { std::printf("%d failures\n", fails); return 1; } std::printf("poly tests passed\n"); return 0;
}
