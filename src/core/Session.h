// Purrfect Cut: one pitch editing session (the audio being edited, its notes and the rendered result). Pure C++, thread aware but no JUCE.
//
// The plugin keeps one of these per instance. The message thread edits notes, a worker thread analyses and renders, the audio thread only
// reads the finished buffer (a shared pointer that is swapped when a render is done). After a small edit only the part around the changed
// notes is rendered again, so dragging a note on a long take stays quick; a change of engine, formant or harmony setting renders everything.
#pragma once
#include "Notes.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace purrfect {

struct StereoBuf { std::vector<float> L, R; double sr = 44100.0; size_t size() const { return L.size(); } };

class PitchSession {
public:
    using BufPtr = std::shared_ptr<const StereoBuf>;

    void setSource(BufPtr s) {
        std::lock_guard<std::mutex> g(m_); src_ = std::move(s); docValid_ = false; rendered_.reset(); lastValid_ = false;
        if (!doc_.notes.empty()) { pending_ = doc_.notes; havePending_ = true; }   // edits survive a new take and are matched to its notes
        doc_ = PitchDoc(); ++editSerial_; ++sourceSerial_;
    }
    BufPtr source() const { std::lock_guard<std::mutex> g(m_); return src_; }
    BufPtr rendered() const { std::lock_guard<std::mutex> g(m_); return rendered_; }
    bool hasDoc() const { std::lock_guard<std::mutex> g(m_); return docValid_; }
    PitchDoc doc() const { std::lock_guard<std::mutex> g(m_); return doc_; }
    uint64_t editSerial() const { std::lock_guard<std::mutex> g(m_); return editSerial_; }
    uint64_t renderedSerial() const { std::lock_guard<std::mutex> g(m_); return renderedSerial_; }
    uint64_t sourceSerial() const { std::lock_guard<std::mutex> g(m_); return sourceSerial_; }
    template <class F> void edit(F&& f) { std::lock_guard<std::mutex> g(m_); if (!docValid_) return; f(doc_); ++editSerial_; }

    // blocking; call from the worker thread
    void analyse(int mode, float refHz) {
        BufPtr s; { std::lock_guard<std::mutex> g(m_); s = src_; } if (!s || s->size() < 2048) return;
        std::vector<float> mono(s->size()); for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (s->L[i] + s->R[i]);
        PitchDoc d = analyseDoc(mono.data(), mono.size(), s->sr, refHz, mode);
        std::lock_guard<std::mutex> g(m_); if (s != src_) return;   // the source changed while we were listening
        if (havePending_) { reapply(d, pending_); havePending_ = false; }
        doc_ = std::move(d); docValid_ = true; rendered_.reset(); lastValid_ = false; ++editSerial_;
    }

    // Renders when something changed since the last render; returns true when it did work. cfgKey must change whenever cfg does.
    bool renderIfDirty(const RenderCfg& cfg, uint64_t cfgKey) {
        BufPtr src, prev; PitchDoc d, last; uint64_t serial, lastKey; bool haveLast;
        { std::lock_guard<std::mutex> g(m_); if (!src_ || !docValid_) return false; if (rendered_ && renderedSerial_ == editSerial_ && lastKey_ == cfgKey) return false;
          src = src_; prev = rendered_; d = doc_; last = lastDoc_; serial = editSerial_; lastKey = lastKey_; haveLast = lastValid_ && prev; }
        auto out = std::make_shared<StereoBuf>(); out->sr = src->sr;
        bool full = !haveLast || lastKey != cfgKey || last.notes.size() != d.notes.size() || last.poly != d.poly;
        std::vector<std::pair<long, long>> ranges;
        if (!full) {
            const long pad = (long) (0.2 * src->sr);
            for (size_t i = 0; i < d.notes.size(); ++i) {
                if (sameEdit(d.notes[i], last.notes[i])) continue;
                if (d.notes[i].moved() || last.notes[i].moved()) { full = true; break; }
                ranges.push_back({ std::max(0L, d.notes[i].sa - pad), std::min((long) src->size(), d.notes[i].sb + pad) });
            }
            if (ranges.empty() && !full) { std::lock_guard<std::mutex> g(m_); renderedSerial_ = serial; lastDoc_ = d; return true; }
        }
        if (full) { renderDoc(d, src->L, src->R, cfg, out->L, out->R); }
        else {
            out->L = prev->L; out->R = prev->R; std::sort(ranges.begin(), ranges.end());
            std::vector<std::pair<long, long>> mrg; for (auto& r : ranges) { if (!mrg.empty() && r.first <= mrg.back().second) mrg.back().second = std::max(mrg.back().second, r.second); else mrg.push_back(r); }
            const long margin = (long) (0.5 * src->sr), align = 16384, n = (long) src->size();
            for (auto& r : mrg) {
                long w0 = std::max(0L, (r.first - margin) / align * align), w1 = std::min(n, ((r.second + margin + align - 1) / align) * align);
                PitchDoc sub = subDoc(d, w0, w1); std::vector<float> sl(src->L.begin() + w0, src->L.begin() + w1), sr_(src->R.begin() + w0, src->R.begin() + w1), ol, orr;
                renderDoc(sub, sl, sr_, cfg, ol, orr);
                const long fade = std::max(8L, (long) (0.01 * src->sr));
                for (long i = r.first; i < r.second; ++i) {
                    float g = 1.f; if (r.first > 0 && i - r.first < fade) g = (float) (i - r.first) / fade; if (r.second < n && r.second - i < fade) g = std::min(g, (float) (r.second - i) / fade);
                    out->L[(size_t) i] += g * (ol[(size_t) (i - w0)] - out->L[(size_t) i]); out->R[(size_t) i] += g * (orr[(size_t) (i - w0)] - out->R[(size_t) i]);
                }
            }
        }
        std::lock_guard<std::mutex> g(m_); if (src != src_) return false;   // source replaced meanwhile
        rendered_ = out; renderedSerial_ = serial; lastDoc_ = d; lastKey_ = cfgKey; lastValid_ = true; return true;
    }

    // ---- saving the edits with the project (the audio itself is not stored)
    std::string saveEdits() const {
        std::lock_guard<std::mutex> g(m_); const auto& v = docValid_ ? doc_.notes : pending_; std::ostringstream o; o.precision(9);
        for (auto& e : v) o << e.sa << ' ' << e.sb << ' ' << e.ta << ' ' << e.tb << ' ' << e.midi << ' ' << e.hz << ' ' << e.dp << ' ' << e.vib << ' ' << e.drift << ' ' << e.fs << ' ' << e.gainDb << ' ' << (int) e.harm << ' ' << (int) e.mute << '\n';
        return o.str();
    }
    void loadEdits(const std::string& s) {
        std::lock_guard<std::mutex> g(m_); pending_.clear(); std::istringstream in(s); std::string line;
        while (std::getline(in, line)) { std::istringstream l(line); EdNote e; int h = 7, mu = 0; if (l >> e.sa >> e.sb >> e.ta >> e.tb >> e.midi >> e.hz >> e.dp >> e.vib >> e.drift >> e.fs >> e.gainDb >> h >> mu) { e.harm = (unsigned char) h; e.mute = mu != 0; pending_.push_back(e); } }
        havePending_ = !pending_.empty();
        if (docValid_) { reapply(doc_, pending_); havePending_ = false; ++editSerial_; }
    }

    // edits of an older analysis carried onto a new one: same place (they overlap in time) and same pitch (within 60 cents)
    static void reapply(PitchDoc& d, const std::vector<EdNote>& old) {
        for (auto& e : d.notes) { const EdNote* best = nullptr; double bo = 0;
            for (auto& o : old) { const long lo = std::max(e.sa, o.sa), hi = std::min(e.sb, o.sb); if (hi <= lo) continue; if (std::fabs(1200.0 * std::log2(e.hz / o.hz)) > 60.0) continue; const double ov = (double) (hi - lo); if (ov > bo) { bo = ov; best = &o; } }
            if (!best) continue; e.dp = best->dp; e.vib = best->vib; e.drift = best->drift; e.fs = best->fs; e.gainDb = best->gainDb; e.harm = best->harm; e.mute = best->mute;
            e.ta = e.sa + (best->ta - best->sa); e.tb = e.sb + (best->tb - best->sb); }
    }

private:
    static bool sameEdit(const EdNote& a, const EdNote& b) { return a.dp == b.dp && a.vib == b.vib && a.drift == b.drift && a.fs == b.fs && a.gainDb == b.gainDb && a.harm == b.harm && a.mute == b.mute && a.ta == b.ta && a.tb == b.tb; }
    static PitchDoc subDoc(const PitchDoc& d, long w0, long w1) {
        PitchDoc s; s.sr = d.sr; s.n = w1 - w0; s.hop = d.hop; s.refHz = d.refHz; s.poly = d.poly; s.pHop = d.pHop; s.pFrame = d.pFrame;
        const size_t f0i = (size_t) (w0 / d.hop), f1i = std::min(d.f0.size(), (size_t) (w1 / d.hop) + 2);
        if (f0i < d.f0.size()) { s.f0.assign(d.f0.begin() + (long) f0i, d.f0.begin() + (long) f1i); if (f0i < d.rms.size()) s.rms.assign(d.rms.begin() + (long) f0i, d.rms.begin() + (long) std::min(d.rms.size(), f1i)); }
        if (d.poly && d.pHop > 0) { const size_t p0 = (size_t) (w0 / d.pHop), p1 = std::min(d.pf.size(), (size_t) (w1 / d.pHop) + 1); if (p0 < d.pf.size()) s.pf.assign(d.pf.begin() + (long) p0, d.pf.begin() + (long) p1); }
        for (auto e : d.notes) { if (e.sb <= w0 || e.sa >= w1) continue; e.sa -= w0; e.sb -= w0; e.ta -= w0; e.tb -= w0; s.notes.push_back(e); }
        return s;
    }
    mutable std::mutex m_; BufPtr src_, rendered_; PitchDoc doc_, lastDoc_; bool docValid_ = false, lastValid_ = false, havePending_ = false; std::vector<EdNote> pending_;
    uint64_t editSerial_ = 0, renderedSerial_ = 0, lastKey_ = 0, sourceSerial_ = 0;
};

}  // namespace purrfect
