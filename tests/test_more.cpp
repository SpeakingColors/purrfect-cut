// More core tests for Purrfect Cut: cheap undo, the peak pyramid, zoom, the sampler and the FX chain. Pure C++, no JUCE.
#include "../src/core/EditDoc.h"
#include "../src/core/View.h"
#include "../src/core/Sampler.h"
#include <cstdio>
#include <random>
using namespace purrfect;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static EditDoc ramp(size_t n) { EditDoc d; std::vector<float> l(n), r(n); for (size_t i = 0; i < n; ++i) { l[i] = (float) i / (float) n; r[i] = -(float) i / (float) n; } d.set(l, r, 48000); return d; }

static void testCheapUndo() {
    EditDoc d = ramp(2000000); d.select(1000, 1100); d.gain(-6.f);
    CHECK(d.undoBytes() < 2000, "an edit of 100 samples stores about 100 samples, not the file (%zu bytes)", d.undoBytes());
    d.undo(); CHECK(std::fabs(d.audio()->L[1050] - 1050.f / 2000000.f) < 1e-9f, "undo of a gain restores the audio bit for bit");
    d.redo(); CHECK(std::fabs(d.audio()->L[1050] - 0.5011872f * 1050.f / 2000000.f) < 1e-8f, "redo applies it again");
    EditDoc t = ramp(1000); t.select(200, 400); t.trim(); CHECK(t.size() == 200 && t.undo() && t.size() == 1000 && t.audio()->L[300] == 0.3f && t.audio()->L[999] == 0.999f, "undo of a trim brings the whole file back");
    CHECK(t.redo() && t.size() == 200 && std::fabs(t.audio()->L[0] - 0.2f) < 1e-7f, "redo of a trim");
    EditDoc c = ramp(1000); c.select(100, 200); Audio ins; ins.L.assign(500, 0.25f); ins.R.assign(500, 0.25f); CHECK(c.replace(ins) && c.size() == 1400 && c.selA == 100 && c.selB == 600, "replace swaps the selection for new audio and selects it");
    CHECK(c.undo() && c.size() == 1000 && c.audio()->L[150] == 0.15f, "undo of replace");
    EditDoc m = ramp(100000); m.undoByteLimit = 10000; for (int i = 0; i < 10; ++i) { m.select(0, 5000); m.invert(); } CHECK(m.undoBytes() <= 80000 + 1 && m.canUndo(), "undo memory is capped (%zu bytes)", m.undoBytes());
}
static void testPyramid() {
    const size_t n = 3000000; EditDoc d; std::vector<float> l(n); std::mt19937 g(1); std::uniform_real_distribution<float> u(-1.f, 1.f); for (auto& v : l) v = 0.3f * u(g); l[1234567] = 0.95f; l[2222222] = -0.9f; d.set(l, l, 48000);
    std::vector<float> mn, mx; d.peaks(0, n, 100, mn, mx);
    CHECK(mx[41] > 0.94f && mn[74] < -0.89f, "the pyramid keeps isolated peaks (%.3f %.3f)", mx[41], mn[74]);
    float hi = 0; for (size_t i = 0; i < 100; ++i) hi = std::max(hi, std::fabs(mx[i])); CHECK(hi <= 0.96f, "no value above the real peak");
    d.select(1234000, 1235000); d.gain(-20.f); d.peaks(0, n, 100, mn, mx); CHECK(mx[41] < 0.5f, "pyramid follows edits (%.3f)", mx[41]);
}
static void testView() {
    View v; v.setSize(1000000, 1000); v.fit(); CHECK(v.spp == 1000.0 && v.start == 0, "fit shows everything");
    v.zoomAt(0.5, 500); CHECK(std::fabs(v.sampleAt(500) - 500000.0) < 1.0, "zoom keeps the sample under the pointer still");
    for (int i = 0; i < 40; ++i) v.zoomAt(0.5, 10); CHECK(v.spp == View::kMinSpp, "zoom stops at 1/32 sample per pixel (%g)", v.spp);
    v.pan(-1e12); CHECK(v.start == 0, "cannot pan before the start"); v.pan(1e12); CHECK(v.start + v.visible() <= 1000000.0 + 1e-6, "cannot pan past the end");
    v.fit(); v.show(250000, 260000); CHECK(v.sampleAt(0) < 250000 && v.sampleAt(1000) > 260000 && v.visible() < 12000, "show a selection");
    v.zoomAt(1000, 0); CHECK(v.zoomedOut(), "zooming out stops at fit");
}

// A test sample: a sine of freq f, seconds long, stereo
static Audio sine(double f, double sec, double rate = 48000, float amp = 0.5f) { Audio a; const size_t n = (size_t) (sec * rate); a.L.resize(n); a.R.resize(n); for (size_t i = 0; i < n; ++i) a.L[i] = a.R[i] = amp * (float) std::sin(6.283185307 * f * (double) i / rate); return a; }
static float rmsOf(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return (float) std::sqrt(s / (double) (b - a)); }
static int crossings(const std::vector<float>& x, size_t a, size_t b) { int c = 0; for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) ++c; return c; }
struct Rig {
    SampleEngine e; Audio a; float P[kParamCount]; double sr = 48000;
    Rig(Audio au) : a(std::move(au)) { defaultParams(P); e.prepare(sr); e.setSource(&a, sr); P[sm_r] = 20; P[sm_a] = 0.5f; P[sm_vel] = 0; }
    void slices() { SliceMap m; auto pts = slicePoints(a.L.data(), a.R.data(), 0, a.size(), (int) P[sm_slices], (int) P[sm_smode] == SLICE_TRANS); m.n = (int) pts.size() - 1; for (size_t i = 0; i < pts.size() && i < 26; ++i) m.pts[i] = pts[i]; e.setSlices(m); }
    std::vector<float> run(int n) { std::vector<float> l((size_t) n), r((size_t) n); for (int i = 0; i < n; i += 512) { const int m = std::min(512, n - i); e.process(P, nullptr, nullptr, l.data() + i, r.data() + i, m); } return l; }
};
static void testSamplerPitch() {
    Rig r(sine(440.0, 2.0)); r.e.noteOn(60, 1.f, r.P); auto x = r.run(24000);
    CHECK(rmsOf(x, 2000, 20000) > 0.2f, "a note at the root plays the sample (rms %.3f)", rmsOf(x, 2000, 20000));
    const int c0 = crossings(x, 2000, 20000); Rig r2(sine(440.0, 2.0)); r2.e.noteOn(72, 1.f, r2.P); auto y = r2.run(24000); const int c1 = crossings(y, 2000, 20000);
    CHECK(std::abs(c1 - 2 * c0) <= 3, "an octave above the root doubles the pitch (%d vs %d)", c0, c1);
    Rig r3(sine(440.0, 2.0)); r3.P[sm_tune] = -12.f; r3.e.noteOn(60, 1.f, r3.P); auto z = r3.run(24000); CHECK(std::abs(crossings(z, 2000, 20000) * 2 - c0) <= 3, "tune -12 halves the pitch");
    Rig r4(sine(440.0, 1.0, 22050.0)); r4.e.noteOn(60, 1.f, r4.P); /* source rate differs: no change here because setSource used host rate */
    Rig rf(sine(440.0, 1.0, 24000.0)); rf.e.setSource(&rf.a, 24000.0); rf.e.noteOn(60, 1.f, rf.P); auto w = rf.run(24000); CHECK(std::abs(crossings(w, 2000, 20000) - c0) <= 4, "a sample recorded at another rate plays at the right pitch (%d vs %d)", crossings(w, 2000, 20000), c0);
}
static void testSamplerEnvelopeLoop() {
    Rig r(sine(220.0, 0.2)); r.e.noteOn(60, 1.f, r.P); auto x = r.run(24000);
    CHECK(r.e.activeVoices() == 0 && rmsOf(x, 15000, 24000) < 1e-6f, "a one shot ends with its sample and frees the voice");
    Rig l(sine(220.0, 0.2)); l.P[sm_loop] = 1; l.P[sm_ls] = 0.2f; l.P[sm_le] = 0.9f; l.e.noteOn(60, 1.f, l.P); auto y = l.run(48000);
    CHECK(l.e.activeVoices() == 1 && rmsOf(y, 40000, 48000) > 0.2f, "a looped note keeps sounding past the sample end");
    l.e.noteOff(60, l.P); auto z = l.run(24000); CHECK(rmsOf(z, 12000, 24000) < 1e-4f && l.e.activeVoices() == 0, "release fades out and frees the voice");
    Rig s(sine(220.0, 1.0)); s.P[sm_a] = 200.f; s.e.noteOn(60, 1.f, s.P); auto w = s.run(24000); CHECK(rmsOf(w, 0, 480) < 0.1f * rmsOf(w, 9000, 11000), "attack ramps up");
    Rig v(sine(220.0, 1.0)); v.P[sm_vel] = 1.f; v.e.noteOn(60, 0.25f, v.P); Rig v2(sine(220.0, 1.0)); v2.P[sm_vel] = 1.f; v2.e.noteOn(60, 1.f, v2.P); CHECK(rmsOf(v.run(4000), 0, 4000) < 0.4f * rmsOf(v2.run(4000), 0, 4000), "velocity sets the level");
    Rig g(sine(220.0, 1.0)); g.P[sm_poly] = MONO; g.P[sm_glide] = 100; g.P[sm_loop] = 1; g.e.noteOn(60, 1.f, g.P); g.run(4000); g.e.noteOn(72, 1.f, g.P); CHECK(g.e.activeVoices() == 1, "mono mode plays one voice and glides");
    Rig p(sine(220.0, 1.0)); for (int i = 0; i < 40; ++i) p.e.noteOn(40 + i, 1.f, p.P); CHECK(p.e.activeVoices() <= SampleEngine::kVoices, "never more than 16 voices");
}
static void testSamplerFilterSlices() {
    Audio hf = sine(8000.0, 1.0); Rig f(hf); f.P[sm_root] = 60; f.e.noteOn(60, 1.f, f.P); const float open = rmsOf(f.run(12000), 2000, 12000);
    Rig g(hf); g.P[sm_ft] = FT_LP; g.P[sm_fc] = 400.f; g.e.noteOn(60, 1.f, g.P); const float shut = rmsOf(g.run(12000), 2000, 12000); CHECK(shut < 0.1f * open, "low pass at 400 Hz removes an 8 kHz tone (%.4f vs %.4f)", shut, open);
    Rig h(hf); h.P[sm_ft] = FT_HP; h.P[sm_fc] = 2000.f; h.e.noteOn(60, 1.f, h.P); CHECK(rmsOf(h.run(12000), 2000, 12000) > 0.7f * open, "high pass at 2 kHz keeps an 8 kHz tone");
    // four clicks one second apart, transient slicing finds them
    Audio cl; cl.L.assign(48000 * 4, 0.f); for (int k = 0; k < 4; ++k) for (int i = 0; i < 2000; ++i) cl.L[(size_t) k * 48000 + (size_t) i + 3000] = 0.8f * std::sin(0.4f * (float) i) * std::exp(-(float) i / 400.f); cl.R = cl.L;
    auto pts = slicePoints(cl.L.data(), cl.R.data(), 0, cl.size(), 5, true); bool ok = pts.size() == 6; for (int k = 0; k < 4 && ok; ++k) { const long d = (long) pts[(size_t) k + 1] - (long) (k * 48000 + 3000); ok = std::labs(d) < 1500; }
    CHECK(ok, "transient slicing cuts at the four hits (%zu points)", pts.size());
    auto ev = slicePoints(cl.L.data(), cl.R.data(), 0, 8000, 8, false); CHECK(ev.size() == 9 && ev[0] == 0 && ev[8] == 8000 && ev[1] == 1000, "even slices");
    Rig s(cl); s.P[sm_mode] = MODE_SLICES; s.P[sm_slices] = 4; s.P[sm_smode] = SLICE_TRANS; s.slices(); s.e.noteOn(61, 1.f, s.P); auto x = s.run(60000); CHECK(rmsOf(x, 0, 60000) > 0.01f, "slice 2 sits on C#4 and plays");
    Rig n(cl); n.P[sm_mode] = MODE_SLICES; n.P[sm_slices] = 4; n.slices(); n.e.noteOn(30, 1.f, n.P); CHECK(n.e.activeVoices() == 0, "keys below the slice range do nothing");
}
static void testFx() {
    float P[kParamCount]; defaultParams(P); FxChain f; f.prepare(48000); P[dr_on] = 1; P[dr_d] = 0.f; f.refresh(P);
    float l = 0.4f, r = -0.4f; for (int i = 0; i < 3000; ++i) { l = 0.4f; r = -0.4f; f.tick(P, l, r, 0, 0); } CHECK(std::fabs(l - 0.4f) < 0.01f && std::fabs(r + 0.4f) < 0.01f, "everything at zero leaves the signal alone (%.4f)", l);
    CHECK(std::fabs(purrShaper(0.5f, 0, 0, 0) - 0.5f) < 1e-6f && purrShaper(1.f, 1.f, 0, 0) < 1.01f, "shaper identity and limit");
    P[dr_x] = 0.6f; f.refresh(P); double dc = 0; float mx = 0; for (int i = 0; i < 48000; ++i) { l = 0.5f * std::sin(0.05f * (float) i); r = l; f.tick(P, l, r, 0, 0); if (i > 24000) { dc += l; mx = std::max(mx, std::fabs(l)); } } CHECK(std::fabs(dc / 24000.0) < 0.02 && mx < 1.5f && mx > 0.2f, "transistor adds colour, no DC (dc %.4f, peak %.3f)", dc / 24000.0, mx);
    // duck: phase restarts, the pump shape dips right after the restart and recovers
    float Q[kParamCount]; defaultParams(Q); Q[du_on] = 1; Q[du_amt] = 1.f; Q[du_shape] = SH_PUMP; Q[du_rate] = 2; Q[du_bpm] = 120; FxChain d; d.prepare(48000); d.refresh(Q); d.restartDuck();
    float first = 1, mid = 0; for (int i = 0; i < 48000; ++i) { l = 1.f; r = 1.f; d.tick(Q, l, r, 0, 0); if (i == 50) first = l; if (i == 20000) mid = l; } CHECK(first < 0.2f && mid > 0.9f, "pump duck: dips at the start of the cycle (%.3f) and has recovered later (%.3f)", first, mid);
    Q[du_src] = SRC_SIDECHAIN; FxChain s; s.prepare(48000); s.refresh(Q); float a = 1; for (int i = 0; i < 4800; ++i) { l = 1.f; r = 1.f; s.tick(Q, l, r, 0.8f, 0.8f); a = l; } CHECK(a < 0.2f, "sidechain duck follows the input level (%.3f)", a);
    float Rv[kParamCount]; defaultParams(Rv); Rv[rv_on] = 1; Rv[rv_decay] = 0.8f; Rv[rv_mix] = 0.5f; Rv[de_on] = 1; FxChain w; w.prepare(48000); w.refresh(Rv); double tail = 0; for (int i = 0; i < 96000; ++i) { l = i == 0 ? 1.f : 0.f; r = l; w.tick(Rv, l, r, 0, 0); if (i > 24000) tail += (double) l * l; } CHECK(tail > 1e-6, "reverb and delay produce a tail (%.2e)", tail);
}
static void testFuzzNoNaN() {
    std::mt19937 g(7); const ParamSpec* t = paramTable(); int bad = 0;
    for (int round = 0; round < 60; ++round) {
        Audio a = sine(100.0 + 50.0 * (double) round, 0.5); SampleEngine e; e.prepare(48000); e.setSource(&a, 48000); float P[kParamCount];
        for (int i = 0; i < kParamCount; ++i) { std::uniform_real_distribution<float> u(t[i].min, t[i].max); P[i] = u(g); if (t[i].kind != Kind::Float) P[i] = std::floor(P[i] + (t[i].kind == Kind::Switch ? 0.5f : 0.f)); }
        std::vector<float> l(512), r(512);
        for (int blk = 0; blk < 60; ++blk) { if (blk % 7 == 0) e.noteOn(36 + (int) (g() % 48), 0.9f, P); if (blk % 11 == 5) e.noteOff(36 + (int) (g() % 48), P); e.process(P, nullptr, nullptr, l.data(), r.data(), 512); for (int i = 0; i < 512; ++i) if (!std::isfinite(l[(size_t) i]) || !std::isfinite(r[(size_t) i]) || std::fabs(l[(size_t) i]) > 4.f) ++bad; }
    }
    CHECK(bad == 0, "no NaN or runaway level with random settings (%d bad samples)", bad);
}
static void testParamTable() {
    const ParamSpec* t = paramTable(); bool ok = true; for (int i = 0; i < kParamCount; ++i) { if (!t[i].id || !t[i].name || t[i].min > t[i].max || t[i].def < t[i].min || t[i].def > t[i].max) { ok = false; std::printf("bad row %d\n", i); } if (t[i].kind == Kind::Choice && !t[i].choices) ok = false; if (t[i].log && t[i].min <= 0) ok = false; }
    CHECK(ok, "parameter table rows are valid"); CHECK(std::string(t[sm_mode].id) == "sm_mode" && std::string(t[fx_prev].id) == "fx_prev" && std::string(t[du_key].id) == "du_key" && std::string(t[rv_mix].id) == "rv_mix", "ids match the enum order");
}
int main() {
    testCheapUndo(); testPyramid(); testView(); testSamplerPitch(); testSamplerEnvelopeLoop(); testSamplerFilterSlices(); testFx(); testFuzzNoNaN(); testParamTable();
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("all tests passed\n"); return 0;
}
