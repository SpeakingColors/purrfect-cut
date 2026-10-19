// Purrfect Cut: the sampler engine and the FX chain. Pure C++, no JUCE, no allocation while processing (everything is
// sized in prepare()). One sample (the audio, or a region of it) is played by up to 16 voices, then goes through Drive,
// Transistor and Fuzz (ported from PurrOne), Squash, Duck, Delay and Reverb (the shimmer engine).
// The JavaScript version of this lives in docs/mockup/purrfectcut.html; the two are kept in step by hand.
#pragma once
#include "EditDoc.h"
#include "Params.h"
#include "Reverb.h"
#include <array>

namespace purrfect {

// ---------- helpers ----------
inline float hermite(const float* x, double p, size_t n) {
    if (n == 0) return 0.f;
    const long i = (long) std::floor(p); const float f = (float) (p - (double) i);
    auto at = [&](long k) { return x[k < 0 ? 0 : (size_t) k >= n ? n - 1 : (size_t) k]; };
    const float a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
    return b + 0.5f * f * (c - a + f * (2.f * a - 5.f * b + 4.f * c - d + f * (3.f * (b - c) + d - a)));
}
inline float smoothstep01(float t) { t = t < 0.f ? 0.f : t > 1.f ? 1.f : t; return t * t * (3.f - 2.f * t); }
inline float asymTanh(float u, float nl) { return u >= 0.f ? std::tanh(u) : nl * std::tanh(u / nl); }

// PurrOne's Drive, Transistor and Fuzz (include/purrone/Engine.h). Each knob is 0..1; past 0.8 they get nastier through smooth curves.
inline float purrShaper(float x, float drive, float xs, float fuzz) {
    const float dn = smoothstep01((drive - 0.8f) * 5.f), xn = smoothstep01((xs - 0.8f) * 5.f), fn = smoothstep01((fuzz - 0.8f) * 5.f);
    float y = x;
    if (drive > 0.001f) { y = std::tanh(x * (1.f + drive * 5.f + dn * 30.f)); if (dn > 0.f) y += dn * (std::tanh(y * (2.f + 10.f * dn)) - y); }
    if (xs > 0.001f) {
        const float nl = 1.f - 0.3f * xs - 0.25f * xn, u = y * (1.f + xs * 5.f + xn * 30.f);
        y = asymTanh(u, nl); y += (0.3f * xs + 0.25f * xn) * y * y;
        if (xn > 0.f) { const float y2 = asymTanh(y * (2.f + 10.f * xn) + 0.15f * xn, nl); y += xn * (y2 - y); }
    }
    if (fuzz > 0.001f) {
        const float fz = 1.f + fuzz * 14.f + fn * 70.f, bf = 0.5f * fuzz;
        y = std::tanh(y * fz + bf) - std::tanh(bf);
        if (fn > 0.f) { const float r = std::sqrt(y * y + 0.02f) - 0.1414f; y += fn * 0.7f * (r - 0.3f); y = std::tanh(y * (1.f + 0.8f * fn)); }
    }
    return y;
}
// Gain that keeps the level about the same as the clean path, so the three knobs add colour and not just volume.
inline float purrMakeup(float d, float x, float f) {
    double a = 0, b = 0;
    for (int i = 0; i < 64; ++i) { const float s = 0.5f * std::sin(6.2831853f * (float) i / 64.f), y0 = purrShaper(s, 0, 0, 0), y1 = purrShaper(s, d, x, f); a += (double) y0 * y0; b += (double) y1 * y1; }
    const float m = b > 1e-9 ? (float) std::sqrt(a / b) : 1.f; return m < 0.25f ? 0.25f : m > 4.f ? 4.f : m;
}

// Duck shapes: how far the level dips (0 = nothing, 1 = full) at a phase 0..1 of the cycle.
inline float duckShape(int s, float p) {
    switch (s) {
        case SH_PUMP: return p < 0.6f ? std::pow(1.f - p / 0.6f, 2.2f) : 0.f;
        case SH_SAW: return 1.f - p;
        case SH_SWELL: return p * p;
        case SH_SINE: return 0.5f + 0.5f * std::cos(6.2831853f * p);
        case SH_TRI: return std::fabs(1.f - 2.f * p);
        case SH_GATE: return p < 0.5f ? 1.f : 0.f;
        case SH_HOLD: return p < 0.1f ? 1.f : p < 0.55f ? 0.55f * (1.f - (p - 0.1f) / 0.45f) : 0.f;
        default: { const float q = std::fmod(p * 4.f, 1.f); return q < 0.5f ? 1.f : 0.1f; }
    }
}

// Where the slices of a sample are: even pieces, or cut where the sound hits. Returns count+1 points, first = a, last = b.
inline std::vector<size_t> slicePoints(const float* L, const float* R, size_t a, size_t b, int count, bool transients) {
    count = std::max(2, std::min(24, count)); std::vector<size_t> pts { a };
    if (b <= a + 1) { pts.push_back(b); return pts; }
    if (!transients || b - a < 4096) { for (int i = 1; i < count; ++i) pts.push_back(a + (size_t) ((double) (b - a) * i / count)); pts.push_back(b); return pts; }
    const size_t hop = 256, nb = (b - a) / hop; std::vector<float> e(nb);
    for (size_t k = 0; k < nb; ++k) { double s = 0; for (size_t i = 0; i < hop; i += 2) { const size_t p = a + k * hop + i; const float v = 0.5f * (L[p] + R[p]); s += (double) v * v; } e[k] = (float) std::sqrt(s / (double) (hop / 2)); }
    struct On { size_t k; float st; }; std::vector<On> on;
    for (size_t k = 2; k + 1 < nb; ++k) { const float pr = (e[k - 1] + e[k - 2]) * 0.5f + 1e-4f, st = (e[k] - pr) / pr; if (st > 0.5f && e[k] >= e[k + 1] * 0.8f) on.push_back({ k, st * std::sqrt(e[k]) }); }
    std::sort(on.begin(), on.end(), [](const On& x, const On& y) { return x.st > y.st; });
    const size_t minGap = (size_t) (0.05 * 44100 / (double) hop) + 1; std::vector<size_t> ch;
    for (const On& o : on) { if ((int) ch.size() >= count - 1) break; bool ok = o.k > minGap; for (size_t c : ch) if ((c > o.k ? c - o.k : o.k - c) <= minGap) ok = false; if (ok) ch.push_back(o.k); }
    std::sort(ch.begin(), ch.end()); for (size_t c : ch) pts.push_back(a + c * hop - hop);
    pts.push_back(b); std::sort(pts.begin(), pts.end());
    while ((int) pts.size() - 1 < count) { size_t bi = 0, bl = 0; for (size_t i = 0; i + 1 < pts.size(); ++i) if (pts[i + 1] - pts[i] > bl) { bl = pts[i + 1] - pts[i]; bi = i; } pts.insert(pts.begin() + (long) bi + 1, pts[bi] + bl / 2); }
    return pts;
}

struct SliceMap { int n = 0; size_t pts[26] {}; };   // n slices, n+1 points

// ---------- the FX chain ----------
class FxChain {
public:
    void prepare(double rate) {
        sr_ = (float) rate; sd_.prepare(sr_); ru_.prepare(sr_); reset();
    }
    void reset() { sd_.reset(); ru_.reset(); for (auto& c : sq_) c = SqState(); for (int c = 0; c < 2; ++c) { xp_[c] = dcx_[c] = dcy_[c] = 0; } dEnv_ = 0; dPh_ = 0; curT_ = 375.f; }
    void restartDuck() { dPh_ = 0; }
    // Process one stereo sample. v is the parameter array. scL/scR is the sidechain level source (0 when none).
    inline void tick(const float* v, float& l, float& r, float scL, float scR) {
        float x[2] = { l, r };
        if (v[dr_on] > 0.5f && (v[dr_d] > 0.001f || v[dr_x] > 0.001f || v[dr_f] > 0.001f)) {
            const float mix = v[dr_mix];
            for (int c = 0; c < 2; ++c) {
                const float xi = x[c], mid = 0.5f * (xp_[c] + xi); float y = 0.5f * (purrShaper(mid, v[dr_d], v[dr_x], v[dr_f]) + purrShaper(xi, v[dr_d], v[dr_x], v[dr_f])) * mk_; xp_[c] = xi;
                const float dcy = y - dcx_[c] + 0.9995f * dcy_[c]; dcx_[c] = y; dcy_[c] = dcy; x[c] = xi * (1.f - mix) + dcy * mix;
            }
        }
        if (v[sq_on] > 0.5f && v[sq_mix] > 0.001f) { const float m = v[sq_mix]; for (int c = 0; c < 2; ++c) x[c] = x[c] * (1.f - m) + std::tanh(squash(c, x[c], v[sq_depth])) * m; }
        // duck
        float gd = 1.f; const float sc = std::max(std::fabs(scL), std::fabs(scR));
        dEnv_ += (sc > dEnv_ ? dAtk_ : dRel_) * (sc - dEnv_);
        const bool duckOn = v[du_on] > 0.5f && v[du_amt] > 0.001f;
        dPh_ += dHz_ / sr_; dPh_ -= std::floor(dPh_);
        if (duckOn) gd = v[du_src] > 0.5f ? 1.f - v[du_amt] * std::min(1.f, dEnv_ * 3.f) : 1.f - v[du_amt] * duckShape((int) v[du_shape], dPh_);
        const int where = (int) v[du_where]; const float gPre = where == DUCK_DRY ? gd : 1.f; x[0] *= gPre; x[1] *= gPre;
        const float d0 = x[0], d1 = x[1]; float y0 = d0, y1 = d1;
        if (v[de_on] > 0.5f) { curT_ += (dp_.timeMs - curT_) * kT_; wm::engine::ShimmerDelayParams p = dp_; p.timeMs = curT_; sd_.process(y0, y1, y0, y1, p); }
        if (v[rv_on] > 0.5f) ru_.process(y0, y1, y0, y1, rs_);
        const float w0 = y0 - d0 * dryG_, w1 = y1 - d1 * dryG_, gT = where == DUCK_TAILS ? gd : 1.f, gA = where == DUCK_ALL ? gd : 1.f;
        float o0 = (d0 + w0 * gT) * gA, o1 = (d1 + w1 * gT) * gA;
        const float m = 0.5f * (o0 + o1), sd = 0.5f * (o0 - o1) * v[out_width]; o0 = (m + sd) * og_; o1 = (m - sd) * og_;
        l = o0; r = o1;
    }
    // call once per block, before the first tick()
    void refresh(const float* v) {
        rs_.type = (wm::engine::ReverbType) (int) v[rv_type]; rs_.shimmerSemis = v[rv_shift]; rs_.shimmerAmount = v[rv_shim]; rs_.mix = v[rv_on] > 0.5f ? v[rv_mix] : 0.f;
        rs_.p.decay = v[rv_decay]; rs_.p.dampHz = v[rv_damp]; rs_.p.preDelayMs = v[rv_pre]; rs_.p.size = v[rv_size]; rs_.p.modDepth = v[rv_mod];
        dp_.feedback = v[de_fb]; dp_.shiftSemis = v[de_shift]; dp_.shimmerAmount = v[de_shim]; dp_.dampHz = v[de_damp]; dp_.mix = v[de_on] > 0.5f ? v[de_mix] : 0.f; dp_.timeMs = v[de_time];
        if (v[dr_d] != mkD_ || v[dr_x] != mkX_ || v[dr_f] != mkF_) { mkD_ = v[dr_d]; mkX_ = v[dr_x]; mkF_ = v[dr_f]; mk_ = purrMakeup(mkD_, mkX_, mkF_); }
        const int rate = (int) v[du_rate]; static const float beats[6] = { 4.f, 2.f, 1.f, 0.5f, 0.25f, 0.f };
        dHz_ = beats[rate < 0 ? 0 : rate > 5 ? 5 : rate] > 0.f ? v[du_bpm] / 60.f / beats[rate < 0 ? 0 : rate > 5 ? 5 : rate] : v[du_hz];
        const float dm = v[de_on] > 0.5f ? v[de_mix] : 0.f, rm = v[rv_on] > 0.5f ? v[rv_mix] : 0.f; dryG_ = (1.f - dm) * (1.f - rm);
        og_ = std::pow(10.f, v[out_gain] / 20.f); kT_ = 1.f - std::exp(-1.f / (0.08f * sr_)); dAtk_ = 1.f - std::exp(-1.f / (0.002f * sr_)); dRel_ = 1.f - std::exp(-1.f / (0.2f * sr_));
    }
private:
    struct SqState { float l1 = 0, l2 = 0, env[3] = { 0, 0, 0 }; };
    float squash(int c, float x, float depth) {
        SqState& s = sq_[c]; const float k1 = 1.f - std::exp(-6.2831853f * 130.f / sr_), k2 = 1.f - std::exp(-6.2831853f * 2800.f / sr_);
        s.l1 += k1 * (x - s.l1); s.l2 += k2 * (x - s.l2); const float bands[3] = { s.l1, s.l2 - s.l1, x - s.l2 };
        const float atk = 1.f - std::exp(-1.f / (0.004f * sr_)), rel = 1.f - std::exp(-1.f / (0.07f * sr_)); float out = 0;
        for (int b = 0; b < 3; ++b) {
            const float a = std::fabs(bands[b]); float& e = s.env[b]; e += (a > e ? atk : rel) * (a - e);
            const float L = 20.f * std::log10(e + 1e-7f); float g = 0;
            if (L > -18.f) g = -(L + 18.f) * 0.6f; else if (L < -38.f) g = std::min(22.f, (-38.f - L) * 0.55f);
            g *= depth; out += bands[b] * std::pow(10.f, g / 20.f);
        }
        return out;
    }
    float sr_ = 48000, mk_ = 1, mkD_ = -1, mkX_ = -1, mkF_ = -1, xp_[2] {}, dcx_[2] {}, dcy_[2] {}, dEnv_ = 0, dPh_ = 0, dHz_ = 2, curT_ = 375, kT_ = 0.001f, dAtk_ = 0.01f, dRel_ = 0.0001f, dryG_ = 1, og_ = 1;
    SqState sq_[2]; wm::engine::ShimmerDelay sd_; wm::engine::ReverbUnit ru_; wm::engine::ShimmerDelayParams dp_; wm::engine::ReverbSettings rs_;
};

// ---------- the sampler ----------
class SampleEngine {
public:
    static constexpr int kVoices = 16;
    void prepare(double rate) { sr_ = rate; fx_.prepare(rate); held_.reserve(64); allOff(); for (auto& v : voices_) v.on = false; }
    // The sample. Audio thread only. srcRate is the sample's own rate; playback is corrected for it.
    void setSource(const Audio* a, double srcRate) { for (auto& v : voices_) v.on = false; src_ = a; srcRate_ = srcRate > 0 ? srcRate : sr_; }
    void setSlices(const SliceMap& m) { slices_ = m; }
    bool hasSource() const { return src_ && src_->size() > 16; }
    int activeVoices() const { int n = 0; for (auto& v : voices_) if (v.on) ++n; return n; }
    void noteOn(int note, float vel, const float* P) {
        if (!hasSource()) return; const size_t n = src_->size(); size_t a = (size_t) ((double) n * P[sm_rs]), b = (size_t) ((double) n * P[sm_re]); a = std::min(a, n - 1); b = std::min(std::max(b, a + 1), n);
        if ((int) P[sm_mode] == MODE_SLICES) { const int ix = note - 60; if (ix < 0 || ix >= slices_.n) return; a = slices_.pts[ix]; b = slices_.pts[ix + 1]; }
        if (b <= a + 16) return;
        if (P[du_key] > 0.5f) fx_.restartDuck();
        auto& held = held_; held.erase(std::remove(held.begin(), held.end(), note), held.end()); if (held.size() < 32) held.push_back(note);
        if ((int) P[sm_poly] == MONO) for (auto& o : voices_) if (o.on && o.gate) { o.glo += (o.note + o.glo) - (float) note; o.note = note; o.vel = vel; return; }
        Voice* v = nullptr; for (auto& o : voices_) if (!o.on) { v = &o; break; }
        if (!v) { v = &voices_[0]; for (auto& o : voices_) if (o.age < v->age) v = &o; }
        const double len = (double) (b - a); *v = Voice();
        v->on = true; v->note = note; v->vel = vel; v->a = (double) a; v->b = (double) b; v->loop = (int) P[sm_mode] == MODE_KEYS && P[sm_loop] > 0.5f;
        v->ls = v->a + len * std::min((double) P[sm_ls], (double) P[sm_le] - 0.002); v->le = v->a + len * std::max((double) P[sm_le], (double) P[sm_ls] + 0.002);
        v->dir = P[sm_rev] > 0.5f ? -1 : 1; v->pos = v->dir < 0 ? v->b - 1 - P[sm_start] * len : v->a + P[sm_start] * len; v->age = ++age_;
        sleeping_ = false; quiet_ = 0;
    }
    void noteOff(int note, const float* P) {
        held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());
        if (sustain_) { return; }
        if ((int) P[sm_poly] == MONO) { for (auto& o : voices_) { if (!o.on || !o.gate) continue; if (!held_.empty()) { const int nn = held_.back(); o.glo += (o.note + o.glo) - (float) nn; o.note = nn; } else o.gate = false; } return; }
        for (auto& o : voices_) if (o.on && o.gate && o.note == note) o.gate = false;
    }
    void allOff() { for (auto& o : voices_) { o.gate = false; o.kill = true; } held_.clear(); }
    void setSustain(bool on) { sustain_ = on; }
    void setBend(float semis) { bendLive_ = semis; }
    // Renders n samples into oL/oR. sc may be null (no sidechain input). P is the parameter array (kParamCount values).
    void process(const float* P, const float* scL, const float* scR, float* oL, float* oR, int n) {
        fx_.refresh(P); const bool prev = P[fx_prev] > 0.5f;
        const float gainV = std::pow(10.f, P[sm_gain] / 20.f), velA = P[sm_vel], pl = std::cos((P[sm_pan] + 1.f) * 0.25f * 3.14159265f) * 1.414f, pr = std::sin((P[sm_pan] + 1.f) * 0.25f * 3.14159265f) * 1.414f;
        const float aA = 1.f / std::max(2.f, P[sm_a] * 0.001f * (float) sr_), kD = 1.f - std::exp(-1.f / (std::max(1.f, P[sm_d]) * 0.001f * (float) sr_ / 3.5f)), kR = std::exp(-1.f / (std::max(2.f, P[sm_r]) * 0.001f * (float) sr_ / 4.6f)), sus = P[sm_s];
        const float tuneC = P[sm_tune] + P[sm_fine] / 100.f + P[sm_bend] + bendLive_; const bool slice = (int) P[sm_mode] == MODE_SLICES; const int ft = (int) P[sm_ft];
        const float kgl = P[sm_glide] > 0.f ? std::exp(-32.f / (P[sm_glide] * 0.001f * (float) sr_ / 3.f)) : 0.f, rateFix = (float) (srcRate_ / sr_);
        float energy = 0;
        for (int i = 0; i < n; ++i) {
            if ((i & 31) == 0) for (auto& v : voices_) {
                if (!v.on) continue; v.glo = kgl > 0.f ? v.glo * kgl : 0.f; if (std::fabs(v.glo) < 1e-4f) v.glo = 0.f;
                const float semi = (slice ? 0.f : (float) v.note - P[sm_root]) + v.glo + tuneC; v.rate = std::pow(2.f, semi / 12.f) * rateFix;
                if (ft != FT_OFF) { float fc = P[sm_fc] * std::pow(2.f, P[sm_fe] * v.lvl * 5.f + P[sm_fk] * ((float) v.note - 60.f) / 12.f); fc = std::max(20.f, std::min(fc, 0.45f * (float) sr_)); const float g = std::tan(3.14159265f * fc / (float) sr_), kk = 1.f / P[sm_fq]; v.fk = kk; v.fa = 1.f / (1.f + g * (g + kk)); v.fb = g * v.fa; v.fc = g * v.fb; }
            }
            float sL = 0, sR = 0;
            for (auto& v : voices_) {
                if (!v.on) continue;
                if (!v.gate && v.st != 3) v.st = 3;
                if (v.st == 0) { v.lvl += aA; if (v.lvl >= 1.f) { v.lvl = 1.f; v.st = 1; } } else if (v.st == 1) v.lvl += (sus - v.lvl) * kD; else { v.lvl *= v.kill ? 0.995f : kR; if (v.lvl < 1e-4f) { v.on = false; continue; } }
                const size_t sz = src_->size(); float xL = hermite(src_->L.data(), v.pos, sz), xR = hermite(src_->R.data(), v.pos, sz);
                if (v.loop) {
                    const double ll = v.le - v.ls, xf = std::min(480.0, ll * 0.25);
                    if (v.dir > 0 && v.pos > v.le - xf && v.pos - ll >= v.a) { const float t = (float) ((v.pos - (v.le - xf)) / xf); const double q = v.pos - ll; xL = xL * (1 - t) + hermite(src_->L.data(), q, sz) * t; xR = xR * (1 - t) + hermite(src_->R.data(), q, sz) * t; }
                    else if (v.dir < 0 && v.pos < v.ls + xf && v.pos + ll < v.b) { const float t = (float) ((v.ls + xf - v.pos) / xf); const double q = v.pos + ll; xL = xL * (1 - t) + hermite(src_->L.data(), q, sz) * t; xR = xR * (1 - t) + hermite(src_->R.data(), q, sz) * t; }
                }
                v.pos += v.dir * (double) v.rate;
                if (v.loop) { if (v.dir > 0 && v.pos >= v.le) v.pos -= v.le - v.ls; else if (v.dir < 0 && v.pos < v.ls) v.pos += v.le - v.ls; }
                else if (v.pos >= v.b || v.pos < v.a) { v.on = false; continue; }
                if (ft != FT_OFF) for (int c = 0; c < 2; ++c) {
                    const float xi = c ? xR : xL, v3 = xi - v.f2[c], v1 = v.fa * v.f1[c] + v.fb * v3, v2 = v.f2[c] + v.fb * v.f1[c] + v.fc * v3;
                    v.f1[c] = 2.f * v1 - v.f1[c]; v.f2[c] = 2.f * v2 - v.f2[c]; const float z = ft == FT_LP ? v2 : ft == FT_BP ? v1 : xi - v.fk * v1 - v2; (c ? xR : xL) = z;
                }
                float amp = v.lvl * ((1.f - velA) + velA * v.vel) * gainV;
                if (!v.loop) { const double edge = v.dir > 0 ? v.b - v.pos : v.pos - v.a; if (edge < 96) amp *= (float) std::max(0.0, edge / 96.0); }
                sL += xL * amp; sR += xR * amp;
            }
            sL *= pl; sR *= pr;
            const float aL = scL ? scL[i] : 0.f, aR = scR ? scR[i] : 0.f;
            float l = sL, r = sR; if (prev) { l += aL; r += aR; }
            fx_.tick(P, l, r, aL, aR);
            // soft safety above -1 dB
            auto soft = [](float y) { const float a = std::fabs(y); return a > 0.9f ? (y < 0 ? -1.f : 1.f) * (0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f)) : y; };
            l = soft(l); r = soft(r); oL[i] = l; oR[i] = r; energy += l * l + r * r;
        }
        if (activeVoices() == 0 && energy < 1e-12f * (float) n) { quiet_ += n; if (quiet_ > (long) (sr_ * 12)) sleeping_ = true; } else quiet_ = 0;
    }
    bool asleep() const { return sleeping_ && activeVoices() == 0; }
    void reset() { fx_.reset(); allOff(); for (auto& v : voices_) v.on = false; quiet_ = 0; sleeping_ = false; }
private:
    struct Voice {
        bool on = false, gate = true, kill = false, loop = false; int note = 60, st = 0; float glo = 0, vel = 1, lvl = 0, rate = 1, f1[2] { 0, 0 }, f2[2] { 0, 0 }, fa = 0, fb = 0, fc = 0, fk = 0.7f;
        double pos = 0, a = 0, b = 0, ls = 0, le = 0; int dir = 1; unsigned long age = 0;
    };
    double sr_ = 48000, srcRate_ = 48000; const Audio* src_ = nullptr; SliceMap slices_; Voice voices_[kVoices]; std::vector<int> held_; unsigned long age_ = 0;
    bool sustain_ = false, sleeping_ = false; float bendLive_ = 0; long quiet_ = 0; FxChain fx_;
};

}  // namespace purrfect
