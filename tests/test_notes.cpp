// Pitch document tests: analysis of a sung phrase and a chord, and every per-note edit measured on the rendered audio.
#include "../src/core/Notes.h"
#include <cstdio>
using namespace purrfect;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double SR = 44100.0, PI = 3.14159265358979;
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
struct Seg { double f0, secs, vibCents, driftCents; };
// a sung phrase: vowel notes with vibrato (5.5 Hz) and optional slow sag, 12 ms attack/release, an "s" noise burst between notes
static std::vector<float> phrase(const std::vector<Seg>& segs, std::vector<std::pair<size_t, size_t>>* bounds, bool noiseGap = true) {
    std::vector<float> x; unsigned rs = 7; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 8388608.f - 1.f; };
    for (auto& s : segs) {
        const size_t n = (size_t) (s.secs * SR), start = x.size(); double ph[40] = {}; x.resize(start + n, 0.f);
        for (size_t i = 0; i < n; ++i) {
            const double t = (double) i / SR; const double cts = s.vibCents * std::sin(2 * PI * 5.5 * t) - s.driftCents * (t / s.secs); const double f = s.f0 * std::exp2(cts / 1200.0);
            const double env = std::min({ 1.0, (double) i / (0.012 * SR), (double) (n - i) / (0.012 * SR) }); double v = 0;
            for (int h = 1; h <= 30; ++h) { const double hf = h * f; if (hf > 9000) break; ph[h] += 2 * PI * hf / SR; const double a = 1.0 / (1.0 + std::pow((hf - 700.0) / 130.0, 2.0)) + 0.6 / (1.0 + std::pow((hf - 1700.0) / 220.0, 2.0)) + 0.02; v += a * std::sin(ph[h] + 0.21 * h); }
            x[start + i] = (float) (0.3 * env * v);
        }
        if (bounds) bounds->push_back({ start, start + n });
        if (noiseGap) { const size_t g = (size_t) (0.08 * SR); for (size_t i = 0; i < g; ++i) x.push_back(0.12f * rnd() * (float) std::sin(PI * (double) i / g)); }
    }
    return x;
}
static double meanPitch(const std::vector<float>& y, size_t a, size_t b, double expect) {   // mean pitch (Hz) of voiced frames in [a,b)
    TrackFrames t = pyinTrack(y.data(), y.size(), SR); double s = 0; int c = 0;
    for (size_t k = a / 256; k < b / 256 && k < t.f0.size(); ++k) if (t.f0[k] > 0.f && std::fabs(cents(t.f0[k], expect)) < 300) { s += cents(t.f0[k], expect); ++c; }
    return c > 3 ? expect * std::exp2(s / c / 1200.0) : 0.0;
}
static double devCents(const std::vector<float>& y, size_t a, size_t b, double expect) {   // spread of the pitch in the note (cents, standard deviation)
    TrackFrames t = pyinTrack(y.data(), y.size(), SR); std::vector<double> v; for (size_t k = a / 256 + 6; k + 6 < b / 256 && k < t.f0.size(); ++k) if (t.f0[k] > 0.f) v.push_back(cents(t.f0[k], expect));
    if (v.size() < 8) return 1e9; double m = 0; for (double q : v) m += q; m /= (double) v.size(); double s = 0; for (double q : v) s += (q - m) * (q - m); return std::sqrt(s / (double) v.size());
}
static double rmsOf(const std::vector<float>& y, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < y.size(); ++i) s += (double) y[i] * y[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double amp(const std::vector<float>& v, size_t from, size_t len, double f) { double re = 0, im = 0; for (size_t i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * PI * (double) i / len), q = v[from + i] * w, a = 2 * PI * f * (double) i / SR; re += q * std::cos(a); im += q * std::sin(a); } return 2 * std::sqrt(re * re + im * im) / (len * 0.5); }
static double hnr(const std::vector<float>& y, size_t from, double f0) {
    const int N = 16384; Fft fft; fft.init(N); std::vector<std::complex<double>> X((size_t) N); for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * PI * i / N); X[(size_t) i] = { y[from + (size_t) i] * w, 0 }; }
    fft.transform(X.data(), false); double eh = 0, er = 0; for (int k = 1; k < N / 2; ++k) { const double f = k * SR / N; if (f < 200 || f > 6000) continue; const double h = f / f0, dd = std::fabs(h - std::round(h)), e = std::norm(X[(size_t) k]); if (dd < 0.1) eh += e; else er += e; }
    return 10.0 * std::log10(eh / (er + 1e-30));
}
int main() {
    RenderCfg cfg;
    // ---- a phrase of three notes
    std::vector<std::pair<size_t, size_t>> bd; auto x = phrase({ { 220.0, 0.7, 30, 0 }, { 261.63, 0.7, 30, 0 }, { 329.63, 0.7, 30, 0 } }, &bd);
    PitchDoc d = analyseDoc(x.data(), x.size(), SR, 440.f, AN_AUTO);
    std::printf("phrase: %zu notes found (chords: %d), %d %% voiced\n", d.notes.size(), (int) d.poly, d.voicedPercent());
    CHECK(!d.poly && d.notes.size() == 3, "expected 3 single-voice notes, got %zu (poly %d)", d.notes.size(), (int) d.poly);
    if (d.notes.size() != 3) { std::printf("%d failures\n", fails + 1); return 1; }
    const double want[3] = { 220.0, 261.63, 329.63 };
    for (int i = 0; i < 3; ++i) CHECK(std::fabs(cents(d.notes[(size_t) i].hz, want[i])) < 25.0, "note %d at %.1f Hz, wanted %.1f", i, d.notes[(size_t) i].hz, want[i]);
    std::vector<float> yl, yr;
    // ---- pitch of the middle note +2 semitones, others stay (Smooth)
    { PitchDoc e = d; e.notes[1].dp = 2.f; renderDoc(e, x, x, cfg, yl, yr);
      const double a = meanPitch(yl, bd[1].first + 3000, bd[1].second - 3000, want[1] * std::exp2(2.0 / 12)), b = meanPitch(yl, bd[0].first + 3000, bd[0].second - 3000, want[0]), c = meanPitch(yl, bd[2].first + 3000, bd[2].second - 3000, want[2]);
      std::printf("edit +2 st on note 2: note 2 error %.1f ct, note 1 %.1f ct, note 3 %.1f ct\n", cents(a, want[1] * std::exp2(2.0 / 12)), cents(b, want[0]), cents(c, want[2]));
      CHECK(a > 0 && std::fabs(cents(a, want[1] * std::exp2(2.0 / 12))) < 12.0, "note 2 moved to %.1f Hz", a); CHECK(b > 0 && std::fabs(cents(b, want[0])) < 12.0, "note 1 changed (%.1f Hz)", b); CHECK(c > 0 && std::fabs(cents(c, want[2])) < 12.0, "note 3 changed (%.1f Hz)", c);
      // the s-burst between notes 1 and 2 stays as it was
      const size_t g0 = bd[0].second + 600, g1 = bd[0].second + 2900; const double r0 = rmsOf(x, g0, g1), r1 = rmsOf(yl, g0, g1); std::printf("   consonant gap level %.2f dB\n", 20 * std::log10(r1 / r0)); CHECK(std::fabs(20 * std::log10(r1 / r0)) < 2.0, "consonant changed by %.1f dB", 20 * std::log10(r1 / r0)); }
    // ---- vibrato to zero
    { PitchDoc e = d; e.notes[1].vib = 0.f; e.notes[1].dp = 0.f; renderDoc(e, x, x, cfg, yl, yr);
      const double s0 = devCents(x, bd[1].first, bd[1].second, want[1]), s1 = devCents(yl, bd[1].first, bd[1].second, want[1]); std::printf("vibrato 100%% -> 0%%: spread %.1f -> %.1f cents\n", s0, s1); CHECK(s1 < s0 * 0.45, "vibrato spread %.1f -> %.1f", s0, s1); }
    // ---- vibrato to 200 %
    { PitchDoc e = d; e.notes[1].vib = 2.f; renderDoc(e, x, x, cfg, yl, yr); const double s0 = devCents(x, bd[1].first, bd[1].second, want[1]), s1 = devCents(yl, bd[1].first, bd[1].second, want[1]); std::printf("vibrato 200%%: spread %.1f -> %.1f cents\n", s0, s1); CHECK(s1 > s0 * 1.5 && s1 < s0 * 2.5, "vibrato x2 gave %.1f -> %.1f", s0, s1); }
    // ---- level
    { PitchDoc e = d; e.notes[2].gainDb = -6.f; renderDoc(e, x, x, cfg, yl, yr); const double g = 20 * std::log10(rmsOf(yl, bd[2].first + 4000, bd[2].second - 4000) / rmsOf(x, bd[2].first + 4000, bd[2].second - 4000)), g1 = 20 * std::log10(rmsOf(yl, bd[0].first + 4000, bd[0].second - 4000) / rmsOf(x, bd[0].first + 4000, bd[0].second - 4000));
      std::printf("level -6 dB on note 3: %.2f dB (note 1: %.2f dB)\n", g, g1); CHECK(std::fabs(g + 6.0) < 0.6 && std::fabs(g1) < 0.3, "level edit %.2f / %.2f", g, g1);
      PitchDoc m = d; m.notes[1].mute = true; renderDoc(m, x, x, cfg, yl, yr); const double mu = rmsOf(yl, bd[1].first + 4000, bd[1].second - 4000); CHECK(mu < 0.01, "muted note still has level %.3f", mu); }
    // ---- formant shift alone: pitch stays, vowel colour moves
    { PitchDoc e = d; e.notes[1].fs = 4.f; renderDoc(e, x, x, cfg, yl, yr);
      const double p = meanPitch(yl, bd[1].first + 3000, bd[1].second - 3000, want[1]); const size_t f = bd[1].first + 8192; double c0 = 0, w0 = 0, c1 = 0, w1 = 0; for (int h = 1; h <= 14; ++h) { const double a0 = amp(x, f, 8192, want[1] * h), a1 = amp(yl, f, 8192, want[1] * h); c0 += a0 * h; w0 += a0; c1 += a1 * h; w1 += a1; }
      std::printf("formant +4 st: pitch error %.1f ct, spectral centre of the harmonics %.2f -> %.2f\n", cents(p, want[1]), c0 / w0, c1 / w1); CHECK(std::fabs(cents(p, want[1])) < 12.0, "pitch moved by the formant edit"); CHECK(c1 / w1 > c0 / w0 * 1.12, "formant did not move up (%.2f -> %.2f)", c0 / w0, c1 / w1); }
    // ---- move the last note 0.25 s later (PSOLA path)
    { PitchDoc e = d; const long sh = (long) (0.25 * SR); e.notes[2].ta += sh; e.notes[2].tb += sh; renderDoc(e, x, x, cfg, yl, yr);
      auto onset = [&](const std::vector<float>& v, size_t from) { for (size_t i = from; i + 256 < v.size(); i += 64) if (rmsOf(v, i, i + 256) > 0.12) return (long) i; return -1L; };
      const long o0 = onset(x, bd[1].second + 3000), o1 = onset(yl, bd[1].second + 3000); std::printf("moved note 3 by 250 ms: onset %ld -> %ld (shift %.0f ms)\n", o0, o1, (o1 - o0) * 1000.0 / SR);
      CHECK(o0 > 0 && std::fabs((o1 - o0) - (double) sh) < 0.03 * SR, "onset moved by %.0f ms", (o1 - o0) * 1000.0 / SR); }
    // ---- slow drift straightened
    { auto xd = phrase({ { 261.63, 1.2, 0, 90 } }, nullptr, false); PitchDoc dd = analyseDoc(xd.data(), xd.size(), SR, 440.f, AN_VOICE); CHECK(dd.notes.size() >= 1, "no note in the drifting take");
      if (!dd.notes.empty()) { for (auto& n : dd.notes) n.drift = 1.f; renderDoc(dd, xd, xd, cfg, yl, yr);
        auto trend = [&](const std::vector<float>& v) { return cents(meanPitch(v, (size_t) (0.85 * SR), (size_t) (1.15 * SR), 261.63 * std::exp2(-80.0 / 1200)), meanPitch(v, (size_t) (0.05 * SR), (size_t) (0.35 * SR), 261.63)); };
        const double t0 = trend(xd), t1 = trend(yl); std::printf("drift: end-minus-start %.0f -> %.0f cents\n", t0, t1); CHECK(std::fabs(t1) < std::fabs(t0) * 0.4, "drift %.0f -> %.0f", t0, t1); } }
    // ---- quality: Smooth against Tight on a plain +7 semitone move (harmonic to residual ratio, higher is cleaner)
    { auto xs = phrase({ { 165.0, 1.6, 0, 0 } }, nullptr, false); PitchDoc ds = analyseDoc(xs.data(), xs.size(), SR, 440.f, AN_VOICE); for (auto& n : ds.notes) n.dp = 7.f;
      std::vector<float> a, b, c; RenderCfg cs; cs.engine = 0; renderDoc(ds, xs, xs, cs, a, c); cs.engine = 1; renderDoc(ds, xs, xs, cs, b, c);
      const double hs = hnr(a, 24000, 165.0 * std::exp2(7.0 / 12)), ht = hnr(b, 24000, 165.0 * std::exp2(7.0 / 12)); std::printf("+7 st clean voice: Smooth %.1f dB, Tight %.1f dB (harmonic to residual)\n", hs, ht); CHECK(hs > ht + 8.0, "Smooth should be cleaner: %.1f vs %.1f", hs, ht); }
    // ---- harmony voices
    { RenderCfg h; h.harmOn = true; h.hSnap = false; h.hInt[0] = 4; h.hInt[1] = 7; h.hInt[2] = -12; h.hLvl[0] = 0.6f; h.hLvl[1] = 0.5f; h.hLvl[2] = 0.f; h.hWet = 1.f; renderDoc(d, x, x, h, yl, yr);
      const size_t f = bd[1].first + 10000; const double base = amp(yl, f, 8192, want[1]);
      const double third = amp(yl, f, 8192, want[1] * std::exp2(4.0 / 12)), fifth = amp(yl, f, 8192, want[1] * std::exp2(7.0 / 12)), none = amp(yl, f, 8192, want[1] * std::exp2(-12.0 / 12)), orig = amp(x, f, 8192, want[1] * std::exp2(4.0 / 12));
      std::printf("harmony: third %.3f fifth %.3f (source at the third: %.4f), voice 3 off: %.4f, main %.3f\n", third, fifth, orig, none, base);
      CHECK(third > 0.4 * base * 0.6 && fifth > 0.4 * base * 0.5 && third > orig * 8 + 0.005, "harmony voices missing"); CHECK(none < 0.02, "voice 3 should be off");
      PitchDoc o = d; o.notes[1].harm = 0; renderDoc(o, x, x, h, yl, yr); const double off = amp(yl, f, 8192, want[1] * std::exp2(4.0 / 12)); CHECK(off < 0.02, "per-note harmony switch did not silence the voices (%.3f)", off); }
    // ---- a chord: C E G held, E raised one semitone through the document
    { std::vector<float> ch(88200, 0.f); for (double fq : { 261.63, 329.63, 392.0 }) for (int h = 1; h <= 8; ++h) for (size_t i = 0; i < ch.size(); ++i) ch[i] += (float) (0.2 / h * std::sin(2 * PI * fq * h * (double) i / SR + 0.3 * h));
      PitchDoc dc = analyseDoc(ch.data(), ch.size(), SR, 440.f, AN_AUTO); std::printf("chord: %zu notes, chords mode %d\n", dc.notes.size(), (int) dc.poly); CHECK(dc.poly && dc.notes.size() == 3, "chord not found (%zu notes)", dc.notes.size());
      if (dc.poly) { for (auto& n : dc.notes) if (std::fabs(cents(n.hz, 329.63)) < 40) n.dp = 1.f; renderDoc(dc, ch, ch, cfg, yl, yr);
        PolyDetector pd(SR, 6); auto fr = pd.detect(yl.data() + 30000); int m = 0; const double w[3] = { 261.63, 329.63 * std::exp2(1.0 / 12), 392.0 }; for (double t : w) for (float q : fr.f0) if (std::fabs(cents(q, t)) < 15) { ++m; break; }
        std::printf("   after the edit: %d of 3 voices where they should be\n", m); CHECK(m == 3, "chord edit gave %d of 3", m); } }
    if (fails) { std::printf("%d failures\n", fails); return 1; } std::printf("notes tests passed\n"); return 0;
}
