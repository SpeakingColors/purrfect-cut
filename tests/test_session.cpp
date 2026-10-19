// Session tests: analysis, full and partial renders agree, edits survive saving and a new take.
#include "../src/core/Session.h"
#include <cstdio>
using namespace purrfect;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double SR = 44100.0, PI = 3.14159265358979;
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
static std::vector<float> note(double f, double secs, double vib) { const size_t n = (size_t) (secs * SR); std::vector<float> x(n); double ph[30] = {}; for (size_t i = 0; i < n; ++i) { const double t = (double) i / SR, ff = f * std::exp2(vib * std::sin(2 * PI * 5.5 * t) / 1200.0), env = std::min({ 1.0, (double) i / (0.012 * SR), (double) (n - i) / (0.012 * SR) }); double v = 0; for (int h = 1; h <= 25; ++h) { ph[h] += 2 * PI * ff * h / SR; const double hf = ff * h, a = 1.0 / (1.0 + std::pow((hf - 700.0) / 130.0, 2.0)) + 0.02; v += a * std::sin(ph[h]); } x[i] = (float) (0.3 * env * v); } return x; }
static double pitchIn(const std::vector<float>& y, size_t a, size_t b, double expect) { TrackFrames t = pyinTrack(y.data(), y.size(), SR); double s = 0; int c = 0; for (size_t k = a / 256; k < b / 256 && k < t.f0.size(); ++k) if (t.f0[k] > 0.f && std::fabs(cents(t.f0[k], expect)) < 300) { s += cents(t.f0[k], expect); ++c; } return c > 3 ? expect * std::exp2(s / c / 1200.0) : 0.0; }
int main() {
    auto buf = std::make_shared<StereoBuf>(); buf->sr = SR; std::vector<std::pair<size_t, size_t>> bd; const double fs[5] = { 196.0, 220.0, 247.0, 261.63, 293.66 };
    for (int i = 0; i < 5; ++i) { auto n = note(fs[i], 0.8, 25); bd.push_back({ buf->L.size(), buf->L.size() + n.size() }); buf->L.insert(buf->L.end(), n.begin(), n.end()); buf->L.insert(buf->L.end(), (size_t) (0.15 * SR), 0.f); }
    buf->R = buf->L;
    PitchSession s; s.setSource(buf); s.analyse(AN_AUTO, 440.f); CHECK(s.hasDoc(), "no doc"); auto d = s.doc(); std::printf("%zu notes found in a five note phrase\n", d.notes.size()); CHECK(d.notes.size() == 5, "expected 5 notes");
    RenderCfg cfg; CHECK(s.renderIfDirty(cfg, 1), "first render should work"); CHECK(!s.renderIfDirty(cfg, 1), "nothing changed: no work expected");
    // identity render
    { auto r = s.rendered(); double e = 0; for (size_t i = 0; i < buf->size(); ++i) e = std::max(e, (double) std::fabs(r->L[i] - buf->L[i])); CHECK(e < 1e-6, "no edits should return the audio unchanged (%.2g)", e); }
    // edit note 4 (index 3) by +3, render (this is a full render: first one with edits? no, last doc had no edits so partial)
    s.edit([](PitchDoc& dd) { dd.notes[3].dp = 3.f; }); CHECK(s.renderIfDirty(cfg, 1), "edit should render"); auto r1 = s.rendered();
    const double p3 = pitchIn(r1->L, bd[3].first + 3000, bd[3].second - 3000, fs[3] * std::exp2(3.0 / 12)); std::printf("partial render: note 4 error %.1f ct\n", cents(p3, fs[3] * std::exp2(3.0 / 12))); CHECK(p3 > 0 && std::fabs(cents(p3, fs[3] * std::exp2(3.0 / 12))) < 12.0, "note 4 at %.1f Hz", p3);
    { double e = 0; for (size_t i = 0; i < bd[1].second; ++i) e = std::max(e, (double) std::fabs(r1->L[i] - buf->L[i])); CHECK(e < 1e-6, "notes far from the edit changed (%.2g)", e); }
    // the partial render equals a full render of the same document (away from the window edges)
    { PitchDoc dd = s.doc(); std::vector<float> fl, fr; renderDoc(dd, buf->L, buf->R, cfg, fl, fr); double e = 0, ref = 0; for (size_t i = bd[3].first + 1000; i < bd[3].second - 1000; ++i) { e += std::pow((double) fl[i] - r1->L[i], 2.0); ref += std::pow((double) fl[i], 2.0); }
      std::printf("partial against full render: relative error %.2e\n", e / ref); CHECK(e / ref < 2e-3, "partial render differs from the full one: %.2e", e / ref); }
    // a second edit elsewhere keeps the first
    s.edit([](PitchDoc& dd) { dd.notes[0].dp = -2.f; }); s.renderIfDirty(cfg, 1); auto r2 = s.rendered();
    { const double a = pitchIn(r2->L, bd[0].first + 3000, bd[0].second - 3000, fs[0] * std::exp2(-2.0 / 12)), b = pitchIn(r2->L, bd[3].first + 3000, bd[3].second - 3000, fs[3] * std::exp2(3.0 / 12)); CHECK(a > 0 && std::fabs(cents(a, fs[0] * std::exp2(-2.0 / 12))) < 12.0 && b > 0 && std::fabs(cents(b, fs[3] * std::exp2(3.0 / 12))) < 12.0, "two edits: %.1f / %.1f", a, b); }
    // a change of setting renders all
    cfg.engine = 1; CHECK(s.renderIfDirty(cfg, 2), "settings change should render"); cfg.engine = 0;
    // save, new session, load, analyse again: edits come back
    const std::string saved = s.saveEdits(); PitchSession t; t.loadEdits(saved); t.setSource(buf); t.analyse(AN_AUTO, 440.f); auto d2 = t.doc(); CHECK(d2.notes.size() == 5 && std::fabs(d2.notes[3].dp - 3.f) < 1e-4f && std::fabs(d2.notes[0].dp + 2.f) < 1e-4f && d2.notes[1].dp == 0.f, "edits did not come back (%zu notes)", d2.notes.size());
    // a new take 0.3 s later: the same edits still land on the right notes
    { auto b2 = std::make_shared<StereoBuf>(); b2->sr = SR; b2->L.assign((size_t) (0.3 * SR), 0.f); b2->L.insert(b2->L.end(), buf->L.begin(), buf->L.end()); b2->R = b2->L; s.setSource(b2); s.analyse(AN_AUTO, 440.f); auto d3 = s.doc(); CHECK(d3.notes.size() == 5 && std::fabs(d3.notes[3].dp - 3.f) < 1e-4f, "edits lost on a new take"); }
    if (fails) { std::printf("%d failures\n", fails); return 1; } std::printf("session tests passed\n"); return 0;
}
