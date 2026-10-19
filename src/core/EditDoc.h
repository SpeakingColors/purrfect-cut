// Purrfect Cut: the editing model (pure C++, no JUCE). One stereo document with a selection, a cursor, a clipboard and
// undo / redo. Every edit builds a new immutable audio buffer, so undo is just going back to the previous one.
#pragma once
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace purrfect {

struct Audio { std::vector<float> L, R; size_t size() const { return L.size(); } };
using AudioPtr = std::shared_ptr<const Audio>;

enum class FadeCurve { Linear, Exponential, SCurve };
inline float fadeShape(FadeCurve c, float x) {
    x = std::min(1.f, std::max(0.f, x));
    if (c == FadeCurve::Exponential) return x * x;
    if (c == FadeCurve::SCurve) return 0.5f - 0.5f * std::cos(3.14159265f * x);
    return x;
}

class EditDoc {
public:
    static constexpr size_t kUndoDepth = 25;
    double sampleRate = 44100;

    void set(std::vector<float> l, std::vector<float> r, double sr) {
        if (r.size() != l.size()) r = l;
        cur_ = std::make_shared<Audio>(Audio { std::move(l), std::move(r) }); sampleRate = sr; undo_.clear(); redo_.clear(); selA = selB = 0; cursor = 0; dirty = false;
    }
    bool empty() const { return !cur_ || cur_->size() == 0; }
    size_t size() const { return cur_ ? cur_->size() : 0; }
    AudioPtr audio() const { return cur_; }
    double seconds() const { return (double) size() / sampleRate; }

    // selection [selA, selB); empty selection means "the whole thing" for effects, "the cursor" for paste
    size_t selA = 0, selB = 0, cursor = 0; bool dirty = false;
    void select(size_t a, size_t b) { if (a > b) std::swap(a, b); selA = std::min(a, size()); selB = std::min(b, size()); }
    void selectAll() { selA = 0; selB = size(); }
    bool hasSel() const { return selB > selA + 1; }
    void setCursor(size_t p) { cursor = std::min(p, size()); }

    bool canUndo() const { return !undo_.empty(); } bool canRedo() const { return !redo_.empty(); }
    bool hasClip() const { return clip_ && clip_->size() > 0; }
    AudioPtr clipboard() const { return clip_; }

    // ---- edits ----
    bool copy() { if (!hasSel()) return false; clip_ = slice(selA, selB); return true; }
    bool cut() { if (!hasSel()) return false; clip_ = slice(selA, selB); return del(); }
    bool del() {
        if (!hasSel()) return false; const size_t a = selA, b = selB; auto n = std::make_shared<Audio>();
        n->L.reserve(size() - (b - a)); n->R.reserve(size() - (b - a));
        n->L.insert(n->L.end(), cur_->L.begin(), cur_->L.begin() + (long) a); n->L.insert(n->L.end(), cur_->L.begin() + (long) b, cur_->L.end());
        n->R.insert(n->R.end(), cur_->R.begin(), cur_->R.begin() + (long) a); n->R.insert(n->R.end(), cur_->R.begin() + (long) b, cur_->R.end());
        push(n); selA = selB = a; cursor = a; return true;
    }
    // Paste replaces the selection, or goes in at the cursor.
    bool paste() {
        if (!hasClip() || !cur_) return false; const size_t a = hasSel() ? selA : cursor, b = hasSel() ? selB : cursor; auto n = std::make_shared<Audio>();
        n->L.insert(n->L.end(), cur_->L.begin(), cur_->L.begin() + (long) a); n->L.insert(n->L.end(), clip_->L.begin(), clip_->L.end()); n->L.insert(n->L.end(), cur_->L.begin() + (long) b, cur_->L.end());
        n->R.insert(n->R.end(), cur_->R.begin(), cur_->R.begin() + (long) a); n->R.insert(n->R.end(), clip_->R.begin(), clip_->R.end()); n->R.insert(n->R.end(), cur_->R.begin() + (long) b, cur_->R.end());
        push(n); selA = a; selB = a + clip_->size(); cursor = selB; return true;
    }
    bool trim() { if (!hasSel()) return false; auto n = slice(selA, selB); push(n); selA = selB = 0; cursor = 0; return true; }
    bool silence() { return fx([](float*, float*, size_t) {}, true, true); }
    bool fadeIn(FadeCurve c = FadeCurve::Linear) { return fx([c](float* l, float* r, size_t n) { for (size_t i = 0; i < n; ++i) { const float g = fadeShape(c, (float) i / (float) n); l[i] *= g; r[i] *= g; } }); }
    bool fadeOut(FadeCurve c = FadeCurve::Linear) { return fx([c](float* l, float* r, size_t n) { for (size_t i = 0; i < n; ++i) { const float g = fadeShape(c, 1.f - (float) i / (float) n); l[i] *= g; r[i] *= g; } }); }
    bool gain(float db) { const float g = std::pow(10.f, db / 20.f); return fx([g](float* l, float* r, size_t n) { for (size_t i = 0; i < n; ++i) { l[i] *= g; r[i] *= g; } }); }
    // Scale so the loudest sample of the range reaches targetDb. False if the range is silent.
    bool normalize(float targetDb = -1.f) {
        const auto [a, b] = range(); float pk = 0; for (size_t i = a; i < b; ++i) pk = std::max(pk, std::max(std::fabs(cur_->L[i]), std::fabs(cur_->R[i])));
        if (pk < 1e-6f) return false; return gain(targetDb - 20.f * std::log10(pk));
    }
    bool reverse() { return fx([](float* l, float* r, size_t n) { std::reverse(l, l + n); std::reverse(r, r + n); }); }
    bool invert() { return fx([](float* l, float* r, size_t n) { for (size_t i = 0; i < n; ++i) { l[i] = -l[i]; r[i] = -r[i]; } }); }
    bool removeDc() { return fx([](float* l, float* r, size_t n) { double sl = 0, sr = 0; for (size_t i = 0; i < n; ++i) { sl += l[i]; sr += r[i]; } sl /= (double) n; sr /= (double) n; for (size_t i = 0; i < n; ++i) { l[i] -= (float) sl; r[i] -= (float) sr; } }); }

    bool undo() { if (undo_.empty()) return false; redo_.push_back({ cur_, selA, selB, cursor }); auto s = undo_.back(); undo_.pop_back(); restore(s); return true; }
    bool redo() { if (redo_.empty()) return false; undo_.push_back({ cur_, selA, selB, cursor }); auto s = redo_.back(); redo_.pop_back(); restore(s); return true; }

    // Peak pairs for drawing: min and max of each of `columns` equal slices of [from, to), mono mix of both channels.
    void peaks(size_t from, size_t to, int columns, std::vector<float>& mins, std::vector<float>& maxs) const {
        mins.assign((size_t) std::max(0, columns), 0.f); maxs = mins; if (empty() || to <= from || columns <= 0) return; to = std::min(to, size());
        for (int c = 0; c < columns; ++c) {
            const size_t a = from + (size_t)((double)(to - from) * c / columns), b = std::max(a + 1, from + (size_t)((double)(to - from) * (c + 1) / columns)); float lo = 1e9f, hi = -1e9f;
            for (size_t i = a; i < b && i < size(); ++i) { const float v = 0.5f * (cur_->L[i] + cur_->R[i]); lo = std::min(lo, v); hi = std::max(hi, v); }
            mins[(size_t) c] = lo > hi ? 0.f : lo; maxs[(size_t) c] = lo > hi ? 0.f : hi;
        }
    }
    float peak() const { float p = 0; if (cur_) for (size_t i = 0; i < size(); ++i) p = std::max(p, std::max(std::fabs(cur_->L[i]), std::fabs(cur_->R[i]))); return p; }

private:
    struct State { AudioPtr a; size_t selA, selB, cursor; };
    AudioPtr cur_, clip_; std::vector<State> undo_, redo_;
    std::pair<size_t, size_t> range() const { return hasSel() ? std::make_pair(selA, selB) : std::make_pair((size_t) 0, size()); }
    std::shared_ptr<Audio> slice(size_t a, size_t b) const { auto n = std::make_shared<Audio>(); n->L.assign(cur_->L.begin() + (long) a, cur_->L.begin() + (long) b); n->R.assign(cur_->R.begin() + (long) a, cur_->R.begin() + (long) b); return n; }
    void push(AudioPtr n) { undo_.push_back({ cur_, selA, selB, cursor }); if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin()); redo_.clear(); cur_ = std::move(n); dirty = true; }
    void restore(const State& s) { cur_ = s.a; selA = std::min(s.selA, size()); selB = std::min(s.selB, size()); cursor = std::min(s.cursor, size()); dirty = true; }
    template <class F> bool fx(F f, bool zero = false, bool needSel = false) {
        if (empty()) return false; if (needSel && !hasSel()) return false;
        const auto [a, b] = range(); if (b <= a) return false; auto n = std::make_shared<Audio>(*cur_);
        if (zero) { std::fill(n->L.begin() + (long) a, n->L.begin() + (long) b, 0.f); std::fill(n->R.begin() + (long) a, n->R.begin() + (long) b, 0.f); }
        else f(n->L.data() + a, n->R.data() + a, b - a);
        push(n); return true;
    }
};

// ---- WAV files (16 / 24 bit PCM and 32 bit float, mono or stereo) ----
enum class WavFormat { Pcm16, Pcm24, Float32 };
inline bool writeWav(const std::string& path, const Audio& a, double sr, WavFormat fmt, bool dither = true) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc); if (!o) return false;
    const int bytes = fmt == WavFormat::Pcm16 ? 2 : fmt == WavFormat::Pcm24 ? 3 : 4; const uint32_t n = (uint32_t) a.size(), dataBytes = n * 2u * (uint32_t) bytes;
    auto w32 = [&](uint32_t v) { o.write((const char*) &v, 4); }; auto w16 = [&](uint16_t v) { o.write((const char*) &v, 2); };
    o.write("RIFF", 4); w32(36 + dataBytes); o.write("WAVEfmt ", 8); w32(16); w16(fmt == WavFormat::Float32 ? 3 : 1); w16(2); w32((uint32_t) sr); w32((uint32_t) sr * 2u * (uint32_t) bytes); w16((uint16_t)(2 * bytes)); w16((uint16_t)(bytes * 8));
    o.write("data", 4); w32(dataBytes);
    uint32_t seed = 22222; auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.f; };      // 0..1
    for (size_t i = 0; i < a.size(); ++i) for (int c = 0; c < 2; ++c) {
        const float v = c == 0 ? a.L[i] : a.R[i];
        if (fmt == WavFormat::Float32) o.write((const char*) &v, 4);
        else {
            const double scale = fmt == WavFormat::Pcm16 ? 32767.0 : 8388607.0; double x = (double) v * scale; if (dither) x += (double) (rnd() - rnd());       // triangular dither, +-1 LSB
            const int32_t q = (int32_t) std::max(-scale - 1, std::min(scale, std::round(x)));
            o.write((const char*) &q, bytes);
        }
    }
    return (bool) o;
}
inline bool readWav(const std::string& path, Audio& out, double& sr) {
    std::ifstream in(path, std::ios::binary); if (!in) return false; char id[4]; uint32_t sz;
    in.read(id, 4); in.read((char*) &sz, 4); char wave[4]; in.read(wave, 4); if (std::memcmp(id, "RIFF", 4) || std::memcmp(wave, "WAVE", 4)) return false;
    uint16_t fmtTag = 0, ch = 0, bits = 0; uint32_t rate = 0; bool haveFmt = false;
    while (in.read(id, 4) && in.read((char*) &sz, 4)) {
        if (!std::memcmp(id, "fmt ", 4)) { uint32_t br; uint16_t ba; in.read((char*) &fmtTag, 2); in.read((char*) &ch, 2); in.read((char*) &rate, 4); in.read((char*) &br, 4); in.read((char*) &ba, 2); in.read((char*) &bits, 2); if (sz > 16) in.seekg(sz - 16, std::ios::cur); haveFmt = true; }
        else if (!std::memcmp(id, "data", 4)) {
            if (!haveFmt || (ch != 1 && ch != 2) || !((fmtTag == 1 && (bits == 16 || bits == 24)) || (fmtTag == 3 && bits == 32))) return false;
            const int bytes = bits / 8; const size_t frames = sz / (size_t)(bytes * ch); out.L.assign(frames, 0.f); out.R.assign(frames, 0.f);
            for (size_t i = 0; i < frames; ++i) for (int c = 0; c < ch; ++c) {
                float v; if (fmtTag == 3) in.read((char*) &v, 4); else { int32_t q = 0; in.read((char*) &q, bytes); if (bytes == 3 && (q & 0x800000)) q |= ~0xFFFFFF; if (bytes == 2 && (q & 0x8000)) q |= ~0xFFFF; v = (float) q / (bytes == 2 ? 32768.f : 8388608.f); }
                (c == 0 ? out.L : out.R)[i] = v; if (ch == 1) out.R[i] = v;
            }
            if (!in) return false; sr = rate; return true;
        } else in.seekg(sz + (sz & 1), std::ios::cur);
    }
    return false;
}
}
