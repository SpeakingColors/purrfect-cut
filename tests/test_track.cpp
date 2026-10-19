// pYIN style tracker tests: accuracy, octave slips with noise, silence and noise stay unvoiced, glide followed.
#include "../src/core/Track.h"
#include <cstdio>
using namespace purrfect;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double SR = 44100.0, PI = 3.14159265358979;
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
int main() {
    unsigned rs = 3; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 8388608.f - 1.f; };
    // weak fundamental voice (strong 2nd and 3rd harmonics) with noise: the case where plain YIN slips an octave up
    for (double snrNoise : { 0.0, 0.05, 0.15, 0.4 }) {
        const size_t n = (size_t) (3.0 * SR); std::vector<float> x(n); double ph[12] = {};
        for (size_t i = 0; i < n; ++i) { const double t = (double) i / SR, f = 150.0 * std::exp2(((t < 1.5 ? 0.0 : 5.0) + 0.3 * std::sin(2 * PI * 5.5 * t)) / 12.0); double v = 0; const double a[8] = { 0.15, 0.6, 0.5, 0.3, 0.2, 0.1, 0.08, 0.05 };
            for (int h = 1; h <= 8; ++h) { ph[h] += 2 * PI * f * h / SR; v += a[h - 1] * std::sin(ph[h]); } x[i] = (float) (0.4 * v) + (float) snrNoise * rnd(); }
        TrackFrames tf = pyinTrack(x.data(), n, SR); PitchFrames yf = yinTrack(x.data(), n, SR); int ok = 0, tot = 0, okY = 0, oct = 0; double worst = 0;
        for (size_t k = 20; k + 20 < tf.f0.size(); ++k) { const double t = (double) k * 256 / SR; if (std::fabs(t - 1.5) < 0.05) continue; const double f = 150.0 * std::exp2(((t < 1.5 ? 0.0 : 5.0) + 0.3 * std::sin(2 * PI * 5.5 * t)) / 12.0); ++tot;
            if (tf.f0[k] > 0.f) { const double c = std::fabs(cents(tf.f0[k], f)); if (c < 40) { ++ok; worst = std::max(worst, c); } else if (std::fabs(c - 1200) < 80 || std::fabs(c - 700) < 80 || std::fabs(c - 1900) < 80) ++oct; }
            if (k < yf.f0.size() && yf.f0[k] > 0.f && std::fabs(cents(yf.f0[k], f)) < 40) ++okY; }
        std::printf("noise %.2f: pYIN right in %d %% of frames (worst %.1f ct, %d octave or twelfth slips); plain YIN %d %%\n", snrNoise, 100 * ok / tot, worst, oct, 100 * okY / tot);
        CHECK(100 * ok / tot > 90, "pYIN found the pitch in only %d %%", 100 * ok / tot); if (snrNoise > 0.3) CHECK(ok > okY + tot / 4, "pYIN (%d) should beat plain YIN (%d) in heavy noise", ok, okY); CHECK(oct * 100 < tot, "too many octave slips: %d", oct);
    }
    { std::vector<float> z((size_t) SR, 0.f); TrackFrames t = pyinTrack(z.data(), z.size(), SR); int v = 0; for (float f : t.f0) if (f > 0) ++v; CHECK(v == 0, "silence voiced in %d frames", v);
      std::vector<float> w((size_t) SR); for (auto& q : w) q = 0.3f * rnd(); t = pyinTrack(w.data(), w.size(), SR); v = 0; for (float f : t.f0) if (f > 0) ++v; std::printf("white noise voiced in %d of %zu frames\n", v, t.f0.size()); CHECK(v * 20 < (int) t.f0.size(), "noise voiced in %d frames", v); }
    if (fails) { std::printf("%d failures\n", fails); return 1; } std::printf("track tests passed\n"); return 0;
}
