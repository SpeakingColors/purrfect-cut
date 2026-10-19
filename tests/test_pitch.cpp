// Pitch tool tests for Purrfect Cut: detection accuracy in cents, shift accuracy, formant preservation, note segmentation,
// the offline renderer and robustness. Synthetic tones only (a harmonic stack, and a vowel-like stack with fixed formants).
#include "../src/core/Pitch.h"
#include <cstdio>
#include <random>
using namespace purrfect;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double SR = 44100.0, PI = 3.14159265358979;
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

// harmonic tone: n harmonics, 1/h amplitude, optional vowel envelope (two formants) and vibrato
static std::vector<float> tone(double f0, double secs, bool vowel = false, double vibCents = 0, double vibHz = 5.5, double vs = 1.0) {
    const size_t n = (size_t) (secs * SR); std::vector<float> x(n); double ph = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR, f = f0 * std::pow(2.0, vibCents / 1200.0 * std::sin(2 * PI * vibHz * t)); ph += f / SR; double s = 0;
        for (int h = 1; h <= 30; ++h) {
            const double hf = h * f; if (hf > 8000) break; double a = 1.0 / h;
            if (vowel) { const double e1 = 1.0 / (1.0 + std::pow((hf - 700.0 * vs) / (110.0 * vs), 2.0)), e2 = 0.6 / (1.0 + std::pow((hf - 1700.0 * vs) / (160.0 * vs), 2.0)), e3 = 0.3 / (1.0 + std::pow((hf - 2700.0 * vs) / (250.0 * vs), 2.0)); a = e1 + e2 + e3 + 0.02; }
            s += a * std::sin(2 * PI * h * ph);
        }
        x[i] = (float) (0.3 * s);
    }
    return x;
}
static double centroid(const std::vector<float>& x, size_t from, size_t len) {   // spectral centroid 100..5000 Hz, Hann, naive DFT
    double num = 0, den = 0; const int N = (int) len;
    for (double f = 100; f <= 5000; f += 20) {
        double re = 0, im = 0; for (int i = 0; i < N; i += 2) { const double w = 0.5 - 0.5 * std::cos(2 * PI * i / N), v = x[from + (size_t) i] * w, a = 2 * PI * f * (double) i / SR; re += v * std::cos(a); im -= v * std::sin(a); }
        const double m = re * re + im * im; num += f * m; den += m;
    }
    return num / den;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt(s / (double) (b - a)); }

static std::vector<float> runLive(const std::vector<float>& in, PitchParams p, int* latency = nullptr) {
    LivePitch lp; lp.prepare(SR); if (latency) *latency = lp.latency(); std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) { float l = in[i], r = in[i]; lp.tick(l, r, p); out[i] = l; }
    return out;
}

static double goertzel(const std::vector<float>& x, size_t from, size_t len, double f) {
    double re = 0, im = 0; for (size_t i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * PI * (double) i / (double) len), v = x[from + i] * w, a = 2 * PI * f * (double) i / SR; re += v * std::cos(a); im -= v * std::sin(a); }
    return std::sqrt(re * re + im * im);
}
static double centsStd(const std::vector<float>& x, size_t from, size_t to) {
    PitchFrames pf = yinTrack(x.data() + from, to - from, SR); std::vector<double> c; for (size_t i = 2; i + 2 < pf.f0.size(); ++i) if (pf.f0[i] > 0) c.push_back(cents(pf.f0[i], 440.0));
    if (c.size() < 4) return 9999; double m = 0; for (double v : c) m += v; m /= (double) c.size(); double s = 0; for (double v : c) s += (v - m) * (v - m); return std::sqrt(s / (double) c.size());
}

// smallest relative error between y(i + lag) and x(i) over lags of +-half a period around the nominal delay (the PSOLA grid has no fixed phase)
static double bestLagError(const std::vector<float>& y, const std::vector<float>& x, size_t a, size_t b, long nominal) {
    double ref = 0; for (size_t i = a; i < b; ++i) ref += (double) x[i] * x[i]; double best = 1e9;
    for (long lag = -150; lag <= 150; ++lag) { double e = 0; for (size_t i = a; i < b; ++i) { const double d = (double) y[(size_t) ((long) i + nominal + lag)] - x[i]; e += d * d; } best = std::min(best, std::sqrt(e / ref)); }
    return best;
}
static void testDetect() {
    const double fs[] = { 82.4, 110, 196, 261.63, 440, 659.26, 880 }; double worst = 0;
    for (double f : fs) {
        auto x = tone(f, 0.6); const float d = yinF0(x.data(), x.size(), SR); const double c = d > 0 ? cents(d, f) : 9999; worst = std::max(worst, std::fabs(c));
        CHECK(std::fabs(c) < 8.0, "YIN on a harmonic tone at %.1f Hz: %.2f Hz, %.1f cents off", f, d, c);
    }
    std::printf("  detection: worst error %.2f cents over %d tones\n", worst, (int) (sizeof fs / sizeof fs[0]));
    auto v = tone(147.0, 0.6, true); const float dv = yinF0(v.data(), v.size(), SR); CHECK(dv > 0 && std::fabs(cents(dv, 147.0)) < 10.0, "vowel-like tone at 147 Hz: %.2f Hz", dv);
    std::mt19937 g(3); std::uniform_real_distribution<float> u(-1.f, 1.f); std::vector<float> nz(30000); for (auto& s : nz) s = 0.3f * u(g);
    CHECK(yinF0(nz.data(), nz.size(), SR) == 0.f, "white noise is reported as unvoiced");
    std::vector<float> sil(30000, 0.f); CHECK(yinF0(sil.data(), sil.size(), SR) == 0.f, "silence is unvoiced");
}

static void testCorrect() {
    PitchParams p; p.mainOn = true; p.speedMs = 0; p.amount = 1; p.vib = 0; p.humanize = 0; p.preserve = 1;
    int lat = 0; auto x = tone(450.0, 1.5); auto y = runLive(x, p, &lat);
    CHECK(lat == 1536, "latency is reported (%d)", lat);
    float d = yinF0(y.data(), y.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 440.0)) < 10.0, "450 Hz (A4 + 39 cents) tuned to A4: %.2f Hz (%.1f cents)", d, d > 0 ? cents(d, 440.0) : 0.0);
    auto x2 = tone(457.0, 1.5); auto y2 = runLive(x2, p); d = yinF0(y2.data(), y2.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 466.16)) < 10.0, "457 Hz chromatic goes to A#4: %.2f Hz", d);
    p.scale = 1; p.key = 0; auto y3 = runLive(x2, p); d = yinF0(y3.data(), y3.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 440.0)) < 10.0, "457 Hz in C major goes to A4, not A#4: %.2f Hz", d);
    p.scale = 0; p.userMask = 0xFFF & ~(1u << 10); auto y4 = runLive(x2, p); d = yinF0(y4.data(), y4.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 440.0)) < 10.0, "note switch: with A# off, 457 Hz goes to A4: %.2f Hz", d);
    p.userMask = 0xFFF; p.amount = 0.5f; auto y5 = runLive(x, p); d = yinF0(y5.data(), y5.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 445.0 * 1.0)) < 14.0 && cents(d, 440.0) > 12.0 && cents(d, 450.0) < -12.0, "amount 0.5 lands in between: %.2f Hz", d);
    // robot: everything goes to the chosen pitch class
    p.amount = 1; p.robot = true; p.robotPc = 4; auto x6 = tone(220.0, 1.5); auto y6 = runLive(x6, p); d = yinF0(y6.data(), y6.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 164.81)) < 12.0, "robot note E: 220 Hz goes to %.2f Hz (E3 is 164.81)", d);
}
static void testVibrato() {
    auto x = tone(440.0, 2.0, false, 45.0); PitchParams p; p.mainOn = true; p.speedMs = 0; p.humanize = 0;
    const double sIn = centsStd(x, 20000, x.size()); p.vib = 0.f; auto flat = runLive(x, p); const double sFlat = centsStd(flat, 30000, flat.size());
    p.vib = 1.f; auto keep = runLive(x, p); const double sKeep = centsStd(keep, 30000, keep.size());
    std::printf("  vibrato std (cents): input %.1f, vib 0 -> %.1f, vib 1 -> %.1f\n", sIn, sFlat, sKeep);
    CHECK(sIn > 20 && sFlat < 0.35 * sIn && sKeep > 0.6 * sIn, "vibrato: flattened with Vibrato keep 0, kept with 1");
}
static void testHarmony() {
    PitchParams p; p.mainOn = false; p.harmOn = true; p.hsnap = false; p.hint[0] = 7; p.hlvl[0] = 1; p.hlvl[1] = 0; p.hlvl[2] = 0; p.hwet = 1;
    auto x = tone(220.0, 1.5); auto y = runLive(x, p); const size_t from = 30000, len = 16384;
    const double at = goertzel(y, from, len, 329.63), lo = goertzel(y, from, len, 329.63 * std::pow(2.0, -45.0 / 1200)), hi = goertzel(y, from, len, 329.63 * std::pow(2.0, 45.0 / 1200));
    CHECK(at > 2.5 * lo && at > 2.5 * hi, "harmony +7 semitones from 220 Hz puts a partial on 329.63 Hz (%.1f vs %.1f / %.1f)", at, lo, hi);
    p.hsnap = true; p.scale = 1; p.key = 0; p.hint[0] = 4;   // a major third above A is C# (not in C major): the scale pulls it down to C, the diatonic third
    auto x2 = tone(220.0, 1.5); auto y2 = runLive(x2, p); const double c4 = goertzel(y2, from, len, 261.63), cLo = goertzel(y2, from, len, 261.63 * std::pow(2.0, -45.0 / 1200)), cHi = goertzel(y2, from, len, 261.63 * std::pow(2.0, 45.0 / 1200));
    CHECK(c4 > 2.5 * cLo && c4 > 2.5 * cHi, "scale-aware harmony: A in C major, a third above is C (%.1f vs %.1f / %.1f)", c4, cLo, cHi);
}
static void testFormants() {
    auto x = tone(130.0, 1.2, true); PitchFrames pf = yinTrack(x.data(), x.size(), SR); RenderEdit ed; ed.hop = 256; ed.ratio.assign(x.size() / 256 + 2, (float) std::pow(2.0, 4.0 / 12)); ed.fshift.assign(ed.ratio.size(), 0.f);
    const size_t from = 20000, len = 16384; const double c0 = centroid(x, from, len);
    auto keep = psolaRender(x, SR, pf, ed, 1.f, x.size()), naive = psolaRender(x, SR, pf, ed, 0.f, x.size());
    const double c1 = centroid(keep, from, len), c2 = centroid(naive, from, len), rr = std::pow(2.0, 4.0 / 12);
    const double idealKeep = centroid(tone(130.0 * rr, 1.2, true), from, len), idealNaive = centroid(tone(130.0 * rr, 1.2, true, 0, 5.5, rr), from, len);   // what a perfect shifter would give
    const float d = yinF0(keep.data(), keep.size(), SR); CHECK(d > 0 && std::fabs(cents(d, 130.0 * rr)) < 15.0, "offline shift +4 semitones: %.2f Hz (%.1f cents off)", d, d > 0 ? cents(d, 130.0 * rr) : 0.0);
    std::printf("  spectral centroid: original %.0f Hz | formants kept %.0f (ideal %.0f) | plain resample %.0f (ideal %.0f)\n", c0, c1, idealKeep, c2, idealNaive);
    CHECK(std::fabs(c1 / idealKeep - 1.0) < 0.10, "formant preserve is within 10%% of an ideal shifter (%.0f vs %.0f Hz)", c1, idealKeep);
    CHECK(std::fabs(c2 / idealNaive - 1.0) < 0.06, "without preserve the formants follow the pitch like plain resampling (%.0f vs %.0f Hz)", c2, idealNaive);
    CHECK(c1 > 1.08 * c2, "the two modes sound different: %.0f vs %.0f Hz", c1, c2);
    for (auto& v : ed.fshift) v = 12.f; ed.ratio.assign(ed.ratio.size(), 1.f); auto up = psolaRender(x, SR, pf, ed, 1.f, x.size()); const double c3 = centroid(up, from, len); const float d3 = yinF0(up.data(), up.size(), SR);
    CHECK(c3 > 1.25 * c0 && d3 > 0 && std::fabs(cents(d3, 130.0)) < 15.0, "formant shift +12 moves the formants but not the pitch (centroid %.0f vs %.0f, pitch %.2f)", c3, c0, d3);
}
static void testNotes() {
    std::vector<float> m; auto add = [&](double f, double s) { auto t = tone(f, s); m.insert(m.end(), t.begin(), t.end()); }; auto gap = [&](double s) { m.insert(m.end(), (size_t) (s * SR), 0.f); };
    add(220.0, 0.35); gap(0.12); add(261.63, 0.35); add(329.63, 0.4);
    PitchFrames pf = yinTrack(m.data(), m.size(), SR); auto notes = segmentNotes(pf, SR);
    CHECK(notes.size() == 3, "three notes found (%d)", (int) notes.size());
    if (notes.size() == 3) { CHECK(std::fabs(notes[0].midi - 57.f) < 0.25f && std::fabs(notes[1].midi - 60.f) < 0.25f && std::fabs(notes[2].midi - 64.f) < 0.25f, "note pitches %.2f %.2f %.2f (want 57 60 64)", notes[0].midi, notes[1].midi, notes[2].midi);
        const double t1 = (double) notes[1].a * pf.hop / SR; CHECK(std::fabs(t1 - 0.47) < 0.03, "second note starts at %.3f s (0.47)", t1); }
}
static void testRender() {
    auto x = tone(196.0, 1.0); PitchFrames pf = yinTrack(x.data(), x.size(), SR); RenderEdit ed; auto y = psolaRender(x, SR, pf, ed, 1.f, x.size());
    const double e1 = bestLagError(y, x, 8000, 36000, 0); CHECK(e1 < 0.12, "ratio 1, no time edit: output is the input up to a shift of less than half a period (error %.3f)", e1);
    ed.hop = 256; const size_t outLen = x.size() * 2; ed.tmap.assign(outLen / 256 + 2, 0.f); for (size_t i = 0; i < ed.tmap.size(); ++i) ed.tmap[i] = (float) (0.5 * (double) i * 256);
    auto s = psolaRender(x, SR, pf, ed, 1.f, outLen); const float d = yinF0(s.data(), s.size() - 4000, SR);
    CHECK(d > 0 && std::fabs(cents(d, 196.0)) < 12.0, "time stretch x2 keeps the pitch (%.2f Hz)", d);
    CHECK(std::fabs(rms(s, 20000, 60000) / rms(x, 8000, 40000) - 1.0) < 0.15, "time stretch keeps the level (%.3f vs %.3f)", rms(s, 20000, 60000), rms(x, 8000, 40000));
}
static void testRobust() {
    std::mt19937 g(11); std::uniform_real_distribution<float> u(-1.f, 1.f); const ParamSpec* t = paramTable(); bool ok = true; float peak = 0;
    for (int round = 0; round < 6; ++round) {
        float P[kParamCount]; defaultParams(P); for (int i = pc_on; i < kParamCount; ++i) { std::uniform_real_distribution<float> r(t[i].min, t[i].max); P[i] = r(g); if (t[i].kind != Kind::Float) P[i] = std::floor(P[i] + (t[i].kind == Kind::Switch ? 0.5f : 0.f)); }
        P[pc_on] = 1; P[hm_on] = round & 1; PitchParams p = PitchParams::fromParams(P); LivePitch lp; lp.prepare(round < 3 ? 44100.0 : 96000.0);
        for (int i = 0; i < 200000; ++i) { const double tt = (double) i / 44100.0; float s = (i / 20000) % 2 ? 0.3f * u(g) : (float) (0.3 * std::sin(2 * PI * (150.0 + 200.0 * std::sin(tt * 2)) * tt)); float l = s, r = s; lp.tick(l, r, p); if (!std::isfinite(l) || !std::isfinite(r)) ok = false; peak = std::max(peak, std::max(std::fabs(l), std::fabs(r))); }
    }
    CHECK(ok, "random settings never produce NaN or infinity"); CHECK(peak < 6.f, "output stays bounded (peak %.2f)", peak);
    PitchParams p; p.mainOn = true; std::vector<float> nz(60000); for (auto& s : nz) s = 0.3f * u(g); auto y = runLive(nz, p);
    CHECK(std::fabs(rms(y, 20000, 55000) / rms(nz, 10000, 45000) - 1.0) < 0.3, "noise passes at about the same level (%.3f vs %.3f)", rms(y, 20000, 55000), rms(nz, 10000, 45000));
    auto x = tone(330.0, 1.0); p.amount = 0; auto z = runLive(x, p); const double e2 = bestLagError(z, x, 12000, 30000, 1536); CHECK(e2 < 0.25, "amount 0 is transparent after the reported latency, up to a shift of less than half a period (error %.3f)", e2);
}

int main() {
    std::printf("pitch tests\n");
    testDetect(); testCorrect(); testVibrato(); testHarmony(); testFormants(); testNotes(); testRender(); testRobust();
    std::printf(fails ? "%d FAILED\n" : "all pitch tests passed\n", fails);
    return fails ? 1 : 0;
}
