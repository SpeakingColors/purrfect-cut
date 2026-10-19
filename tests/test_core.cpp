// Core tests for Purrfect Cut. Pure C++, no JUCE.
#include "../src/core/EditDoc.h"
#include <cstdio>
#include <filesystem>
using namespace purrfect;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static EditDoc ramp(size_t n = 1000) { EditDoc d; std::vector<float> l(n), r(n); for (size_t i = 0; i < n; ++i) { l[i] = (float) i / (float) n; r[i] = -(float) i / (float) n; } d.set(l, r, 48000); return d; }

static void testBasicEdits() {
    EditDoc d = ramp(); d.select(100, 300);
    CHECK(d.copy() && d.clipboard()->size() == 200 && d.size() == 1000, "copy keeps the document and fills the clipboard");
    CHECK(d.cut() && d.size() == 800 && d.clipboard()->size() == 200, "cut removes 200 samples");
    CHECK(std::fabs(d.audio()->L[100] - 0.3f) < 1e-6f, "after the cut, sample 100 is the old sample 300 (got %.3f)", d.audio()->L[100]);
    CHECK(d.cursor == 100 && !d.hasSel(), "cursor lands where the cut was");
    CHECK(d.paste() && d.size() == 1000, "paste at the cursor puts 200 back");
    CHECK(std::fabs(d.audio()->L[150] - 0.15f) < 1e-6f && d.selA == 100 && d.selB == 300, "pasted audio is selected");
    d.select(0, 50); CHECK(d.paste() && d.size() == 1000 - 50 + 200, "paste over a selection replaces it");
    d = ramp(); d.select(200, 400); CHECK(d.trim() && d.size() == 200 && std::fabs(d.audio()->L[0] - 0.2f) < 1e-6f, "trim keeps only the selection");
    d = ramp(); CHECK(!d.cut() && !d.copy() && !d.del() && !d.trim(), "no selection: cut, copy, delete and trim do nothing");
    d.select(10, 11); CHECK(!d.hasSel(), "a one sample selection counts as none");
    d = ramp(); d.select(900, 5000); CHECK(d.selB == 1000, "selection is clamped to the document");
    d.select(500, 100); CHECK(d.selA == 100 && d.selB == 500, "selection ends may come in either order");
}
static void testSilenceGainFade() {
    EditDoc d = ramp(); d.select(100, 200); d.silence();
    CHECK(d.audio()->L[150] == 0.f && std::fabs(d.audio()->L[99] - 0.099f) < 1e-6f && std::fabs(d.audio()->L[200] - 0.2f) < 1e-6f, "silence only touches the selection");
    d = ramp(); d.silence(); CHECK(d.audio()->L[500] != 0.f, "silence without a selection does nothing");
    d = ramp(); d.gain(-6.0206f); CHECK(std::fabs(d.audio()->L[500] - 0.25f) < 1e-3f, "-6 dB halves the level (%.4f)", d.audio()->L[500]);
    d = ramp(); d.select(0, 500); d.fadeIn(); CHECK(d.audio()->L[0] == 0.f && std::fabs(d.audio()->L[250] - 0.25f * 0.5f) < 2e-3f && std::fabs(d.audio()->L[600] - 0.6f) < 1e-6f, "linear fade in over the selection");
    d = ramp(); d.select(500, 1000); d.fadeOut(FadeCurve::SCurve); CHECK(std::fabs(d.audio()->L[999]) < 1e-3f && std::fabs(d.audio()->L[250] - 0.25f) < 1e-6f, "s-curve fade out ends at zero");
    CHECK(fadeShape(FadeCurve::Exponential, 0.5f) == 0.25f && std::fabs(fadeShape(FadeCurve::SCurve, 0.5f) - 0.5f) < 1e-6f && fadeShape(FadeCurve::Linear, 2.f) == 1.f, "fade shapes");
}
static void testNormalizeReverseDc() {
    EditDoc d = ramp(); d.normalize(-1.f); CHECK(std::fabs(d.peak() - std::pow(10.f, -1.f / 20.f)) < 1e-4f, "normalise to -1 dB, peak %.4f", d.peak());
    d = ramp(); d.select(0, 500); d.normalize(0.f); CHECK(std::fabs(d.audio()->L[499] - 1.f) < 1e-3f && d.audio()->L[600] == 0.6f, "normalise a selection only changes the selection, scaled to its own peak");
    EditDoc z; z.set(std::vector<float>(100, 0.f), std::vector<float>(100, 0.f), 48000); CHECK(!z.normalize(), "silence cannot be normalised");
    d = ramp(); d.select(0, 4); d.select(0, 10); d.reverse(); CHECK(std::fabs(d.audio()->L[0] - 0.009f) < 1e-6f && std::fabs(d.audio()->L[9] - 0.f) < 1e-6f && std::fabs(d.audio()->L[10] - 0.01f) < 1e-6f, "reverse a selection");
    d = ramp(); d.invert(); CHECK(d.audio()->L[500] == -0.5f && d.audio()->R[500] == 0.5f, "invert");
    EditDoc o; std::vector<float> l(1000, 0.3f), r(1000, -0.2f); o.set(l, r, 48000); o.removeDc(); double m = 0; for (float v : o.audio()->L) m += v; CHECK(std::fabs(m) < 1e-3, "DC removed (sum %.5f)", m);
}
static void testUndoRedo() {
    EditDoc d = ramp(); d.select(100, 300); d.cut(); d.select(0, 50); d.gain(-6.f); d.reverse();
    CHECK(d.canUndo() && !d.canRedo(), "undo is available after edits");
    CHECK(d.undo() && d.undo(), "two undos"); CHECK(d.size() == 800 && d.canRedo(), "back at 800 samples after the gain and reverse are undone, redo available");
    CHECK(d.undo() && d.size() == 1000 && d.selA == 100 && d.selB == 300, "undo of the cut brings the audio and the selection back");
    CHECK(d.audio()->L[150] == 0.15f, "audio is bit exact after undo");
    CHECK(!d.undo(), "nothing left to undo"); CHECK(d.redo() && d.size() == 800, "redo");
    d.select(0, 10); d.invert(); CHECK(!d.canRedo(), "a new edit clears redo");
    EditDoc e = ramp(); for (int i = 0; i < 40; ++i) { e.select(0, 10); e.invert(); } int n = 0; while (e.undo()) ++n; CHECK(n == (int) EditDoc::kUndoDepth, "undo depth is capped at %zu (got %d)", EditDoc::kUndoDepth, n);
    EditDoc f = ramp(); f.select(0, 100); f.copy(); f.cut(); f.undo(); CHECK(f.hasClip() && f.clipboard()->size() == 100, "the clipboard survives undo");
}
static void testPeaks() {
    EditDoc d; std::vector<float> l(1000, 0.f); l[250] = 0.8f; l[750] = -0.6f; d.set(l, l, 48000);
    std::vector<float> mn, mx; d.peaks(0, 1000, 4, mn, mx);
    CHECK(mx.size() == 4 && std::fabs(mx[1] - 0.8f) < 1e-6f && std::fabs(mn[3] + 0.6f) < 1e-6f && mx[0] == 0.f && mn[0] == 0.f, "peaks per column");
    d.peaks(0, 1000, 0, mn, mx); CHECK(mx.empty(), "zero columns"); EditDoc e; e.peaks(0, 10, 5, mn, mx); CHECK(mx.size() == 5 && mx[0] == 0.f, "empty document gives flat peaks");
}
static void testWav() {
    const auto p = (std::filesystem::temp_directory_path() / "purrfect_test.wav").string();
    EditDoc d; std::vector<float> l(5000), r(5000); for (size_t i = 0; i < l.size(); ++i) { l[i] = 0.7f * (float) std::sin(0.05 * (double) i); r[i] = 0.4f * (float) std::cos(0.031 * (double) i); } d.set(l, r, 44100);
    for (auto [fmt, tol] : { std::make_pair(WavFormat::Float32, 1e-7), std::make_pair(WavFormat::Pcm24, 4e-7), std::make_pair(WavFormat::Pcm16, 1.2e-4) }) {
        CHECK(writeWav(p, *d.audio(), 44100, fmt), "write"); Audio back; double sr = 0; CHECK(readWav(p, back, sr), "read back"); CHECK(sr == 44100 && back.size() == 5000, "rate and length survive (%g, %zu)", sr, back.size());
        double e = 0; for (size_t i = 0; i < back.size(); ++i) e = std::max(e, (double) std::max(std::fabs(back.L[i] - l[i]), std::fabs(back.R[i] - r[i])));
        CHECK(e <= tol, "format %d round trip error %.2e (limit %.1e)", (int) fmt, e, tol);
    }
    Audio loud; loud.L = { 2.f, -2.f }; loud.R = { 2.f, -2.f }; writeWav(p, loud, 44100, WavFormat::Pcm16, false); Audio b2; double sr2; readWav(p, b2, sr2); CHECK(b2.L[0] > 0.999f && b2.L[1] <= -0.999f, "16 bit export clips instead of wrapping around");
    { std::ofstream bad(p, std::ios::binary | std::ios::trunc); bad << "not a wav file at all"; } Audio b3; CHECK(!readWav(p, b3, sr2), "garbage is refused"); CHECK(!readWav(p + ".missing", b3, sr2), "missing file is refused");
    std::filesystem::remove(p);
}
int main() {
    testBasicEdits(); testSilenceGainFade(); testNormalizeReverseDc(); testUndoRedo(); testPeaks(); testWav();
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("all tests passed\n"); return 0;
}
