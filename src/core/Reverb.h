// Copied from https://github.com/SpeakingColors/shimmerstuff (src/Reverb.h), same author, AGPL-3.0-or-later; this first line is the only change.
// Reverbs, shimmer and pitch shifting. Pure C++, no allocation after prepare().
//  - PlateReverb: Dattorro plate (J. Dattorro, "Effect Design Part 1", JAES 1997, a published figure-eight tank
//    design in the Griesinger/Lexicon lineage). Constants are from the paper, scaled from its 29761 Hz rate.
//  - HallReverb: 8-line feedback delay network, Householder mixing, per-line decay and damping, slow modulation.
//  - PitchShifter: two-head crossfading delay-line shifter (octave up for shimmer).
//  - ShimmerDelay and ShimmerReverb build on those.
// These are original implementations from public literature. Lexicon's own code is proprietary and is not used.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace wm::engine {

class DelayLine {
public:
    void prepare(int maxSamples) { buf_.assign(size_t(maxSamples) + 4, 0.0f); w_ = 0; }
    void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    void push(float x) { buf_[w_] = x; w_ = (w_ + 1) % buf_.size(); }
    float read(float d) const {   // d samples ago (d >= 1), linear interpolation
        float p = float(w_) - d; while (p < 0) p += float(buf_.size());
        size_t i = size_t(p), j = (i + 1) % buf_.size(); float f = p - float(i);
        return buf_[i % buf_.size()] * (1 - f) + buf_[j] * f;
    }
    float readInt(int d) const { return buf_[(w_ + buf_.size() - size_t(d)) % buf_.size()]; }
    int capacity() const { return int(buf_.size()) - 4; }
private:
    std::vector<float> buf_; size_t w_ = 0;
};

// Schroeder allpass with optional LFO modulation of its delay.
class Allpass {
public:
    void prepare(int len, int maxMod = 0) { len_ = len; dl_.prepare(len + maxMod + 2); }
    void clear() { dl_.clear(); }
    float readInt(int d) const { return dl_.readInt(d); }   // output taps
    float tick(float x, float g, float mod = 0.0f) {
        float d = dl_.read(float(len_) + mod);
        float v = x - g * d;
        dl_.push(v);
        return d + g * v;
    }
private:
    DelayLine dl_; int len_ = 1;
};

struct ReverbParams {
    float decay = 0.6f;      // 0..1 (tail length)
    float dampHz = 6000.0f;  // high-frequency damping
    float preDelayMs = 0.0f;
    float size = 1.0f;       // hall/room scale
    float modDepth = 0.5f;   // 0..1
    float color = 0.0f;      // -1 dark .. +1 bright (Velvet only)
};

class PlateReverb {
public:
    void prepare(float sr) {
        sr_ = sr; k_ = sr / 29761.0f;
        pre_.prepare(int(0.5f * sr));
        const int idl[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) in_[i].prepare(sc(idl[i]));
        for (int b = 0; b < 2; ++b) {
            ap1_[b].prepare(sc(b ? 908 : 672), 64);
            d1_[b].prepare(sc(b ? 4217 : 4453) + 8);
            ap2_[b].prepare(sc(b ? 2656 : 1800), sc(300));   // spare room so output taps can sit past the nominal length
            d2_[b].prepare(sc(b ? 3163 : 3720) + 8);
        }
        reset();
    }
    void reset() {
        pre_.clear(); for (auto& a : in_) a.clear();
        for (int b = 0; b < 2; ++b) { ap1_[b].clear(); d1_[b].clear(); ap2_[b].clear(); d2_[b].clear(); lp_[b] = 0; fb_[b] = 0; }
        bw_ = 0; lfo_ = 0;
    }
    void process(float inL, float inR, float& outL, float& outR, const ReverbParams& p) {
        pre_.push(0.5f * (inL + inR));
        float x = p.preDelayMs > 0 ? pre_.read(std::max(1.0f, p.preDelayMs * 0.001f * sr_)) : pre_.readInt(1);
        float damp = std::exp(-2.0f * 3.14159265f * p.dampHz / sr_);      // one-pole lowpass coefficient
        x = bw_ = bw_ * 0.0f + x;                                        // (input bandwidth left open)
        x = in_[0].tick(x, 0.75f); x = in_[1].tick(x, 0.75f); x = in_[2].tick(x, 0.625f); x = in_[3].tick(x, 0.625f);
        float decay = std::min(0.97f, 0.2f + 0.77f * p.decay);
        lfo_ += 2.0f * 3.14159265f * 0.8f / sr_; if (lfo_ > 6.2831853f) lfo_ -= 6.2831853f;
        float mod = p.modDepth * 16.0f * k_ * std::sin(lfo_);
        float nb[2];
        for (int b = 0; b < 2; ++b) {
            float in = x + decay * fb_[1 - b];                            // figure-eight cross feedback
            float s = ap1_[b].tick(in, -0.7f, b ? -mod : mod);
            d1_[b].push(s);
            float v = d1_[b].read(float(d1len(b)));
            lp_[b] = v * (1 - damp) + lp_[b] * damp;
            float t = ap2_[b].tick(lp_[b] * decay, 0.5f);
            d2_[b].push(t);
            nb[b] = d2_[b].read(float(d2len(b)));
        }
        fb_[0] = nb[0]; fb_[1] = nb[1];
        // Output taps (positions from the paper, scaled).
        outL = 0.3f * (d1_[0].readInt(sc(266)) + d1_[0].readInt(sc(2974)) - ap2_[0].readInt(sc(1913)) + d2_[0].readInt(sc(1996))
                       - d1_[1].readInt(sc(1990)) - ap2_[1].readInt(sc(187)) - d2_[1].readInt(sc(1066)));
        outR = 0.3f * (d1_[1].readInt(sc(353)) + d1_[1].readInt(sc(3627)) - ap2_[1].readInt(sc(1228)) + d2_[1].readInt(sc(2673))
                       - d1_[0].readInt(sc(2111)) - ap2_[0].readInt(sc(335)) - d2_[0].readInt(sc(121)));
    }
private:
    int sc(int n) const { return std::max(2, int(std::lround(float(n) * k_))); }
    int d1len(int b) const { return sc(b ? 4217 : 4453); }
    int d2len(int b) const { return sc(b ? 3163 : 3720); }
    float sr_ = 48000, k_ = 1.6f, lp_[2] = {}, fb_[2] = {}, bw_ = 0, lfo_ = 0;
    DelayLine pre_, d1_[2], d2_[2];
    Allpass in_[4], ap1_[2], ap2_[2];
};

class HallReverb {
public:
    static constexpr int N = 8;
    void prepare(float sr) {
        sr_ = sr;
        for (int i = 0; i < N; ++i) dl_[i].prepare(int(0.25f * sr));
        for (int i = 0; i < 4; ++i) { diffL_[i].prepare(int(kDiff[i] * 0.001f * sr)); diffR_[i].prepare(int((kDiff[i] + 1.7f) * 0.001f * sr)); }
        pre_.prepare(int(0.5f * sr));
        reset();
    }
    void reset() { for (auto& d : dl_) d.clear(); for (int i = 0; i < 4; ++i) { diffL_[i].clear(); diffR_[i].clear(); } pre_.clear(); for (float& v : lp_) v = 0; ph_ = 0; }
    void process(float inL, float inR, float& outL, float& outR, const ReverbParams& p) {
        pre_.push(0.5f * (inL + inR));
        float x = p.preDelayMs > 0 ? pre_.read(std::max(1.0f, p.preDelayMs * 0.001f * sr_)) : pre_.readInt(1);
        float dl = x, dr = x;
        for (int i = 0; i < 4; ++i) { dl = diffL_[i].tick(dl, 0.6f); dr = diffR_[i].tick(dr, 0.6f); }
        float rt60 = 0.3f + 9.7f * p.decay * p.decay;                    // seconds
        float damp = std::exp(-2.0f * 3.14159265f * p.dampHz / sr_);
        ph_ += 2.0f * 3.14159265f * 0.37f / sr_; if (ph_ > 6.2831853f) ph_ -= 6.2831853f;
        float y[N], sum = 0;
        for (int i = 0; i < N; ++i) {
            float len = kLines[i] * 0.001f * sr_ * p.size;
            len = std::min(len, float(dl_[i].capacity()) - 40.0f);
            float mod = p.modDepth * 6.0f * std::sin(ph_ * (1.0f + 0.13f * float(i)) + float(i));
            float v = dl_[i].read(std::max(2.0f, len + mod));
            float g = std::pow(10.0f, -3.0f * (len / sr_) / rt60);          // per-pass gain for the chosen RT60
            lp_[i] = v * (1 - damp) + lp_[i] * damp;
            y[i] = lp_[i] * g; sum += y[i];
        }
        float mix = sum * (2.0f / N);                                       // Householder: x - (2/N) * sum
        for (int i = 0; i < N; ++i) {
            float in = (i % 2 ? dr : dl) * 0.5f;
            dl_[i].push(y[i] - mix + in);
        }
        outL = outR = 0;
        for (int i = 0; i < N; ++i) { (i % 2 ? outR : outL) += y[i] * (i & 2 ? -1.0f : 1.0f); }
        outL *= 0.5f; outR *= 0.5f;
    }
private:
    static constexpr float kLines[N] = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 71.3f};   // ms, mutually near-prime
    static constexpr float kDiff[4] = {5.1f, 7.7f, 11.3f, 13.9f};
    float sr_ = 48000, lp_[N] = {}, ph_ = 0;
    DelayLine dl_[N], pre_;
    Allpass diffL_[4], diffR_[4];
};

class PitchShifter {
public:
    void prepare(float sr, float windowMs = 60.0f) {
        win_ = windowMs * 0.001f * sr; buf_.prepare(int(win_ * 2) + 16); phase_ = 0;
    }
    void reset() { buf_.clear(); phase_ = 0; }
    float tick(float x, float ratio) {
        buf_.push(x);
        phase_ += (1.0f - ratio) / win_;
        phase_ -= std::floor(phase_);
        float p2 = phase_ + 0.5f; p2 -= std::floor(p2);
        auto head = [&](float ph) {
            float w = std::sin(3.14159265f * ph); w *= w;
            return w * buf_.read(std::max(1.0f, ph * win_ + 1.0f));
        };
        return head(phase_) + head(p2);
    }
private:
    DelayLine buf_; float win_ = 2880, phase_ = 0;
};

struct ShimmerDelayParams {
    float timeMs = 450, feedback = 0.55f, shiftSemis = 12.0f, dampHz = 5000, mix = 0.5f, shimmerAmount = 1.0f;
};

class ShimmerDelay {
public:
    void prepare(float sr) { sr_ = sr; dl_[0].prepare(int(2.5f * sr)); dl_[1].prepare(int(2.5f * sr)); ps_[0].prepare(sr); ps_[1].prepare(sr); reset(); }
    void reset() { for (int c = 0; c < 2; ++c) { dl_[c].clear(); ps_[c].reset(); lp_[c] = 0; } }
    void process(float inL, float inR, float& outL, float& outR, const ShimmerDelayParams& p) {
        float ratio = std::pow(2.0f, p.shiftSemis / 12.0f), damp = std::exp(-2.0f * 3.14159265f * p.dampHz / sr_);
        float in[2] = {inL, inR}, out[2];
        for (int c = 0; c < 2; ++c) {
            float d = dl_[c].read(std::max(2.0f, p.timeMs * 0.001f * sr_ * (c ? 1.0f : 1.0f)));
            lp_[c] = d * (1 - damp) + lp_[c] * damp;
            float shifted = ps_[c].tick(lp_[c], ratio);
            float fb = (lp_[c] * (1 - p.shimmerAmount) + shifted * p.shimmerAmount) * p.feedback;
            dl_[c].push(in[c] + std::tanh(fb));
            out[c] = in[c] * (1 - p.mix) + lp_[c] * p.mix;
        }
        outL = out[0]; outR = out[1];
    }
private:
    float sr_ = 48000, lp_[2] = {}; DelayLine dl_[2]; PitchShifter ps_[2];
};

enum class ReverbType { Plate, Hall, ShimmerPlate, ShimmerHall, Velvet };

struct ReverbSettings {
    ReverbType type = ReverbType::Plate;
    ReverbParams p;
    float shimmerSemis = 12.0f, shimmerAmount = 0.35f;   // amount of shifted tail fed back into the input
    float shimmer2Semis = 7.0f, shimmer2Amount = 0.0f;   // second shimmer voice (0 = off), own pitch and amount
    float wetHpHz = 20.0f, wetLpHz = 20000.0f;           // filters on the wet signal only (20 / 20000 = off)
    float mix = 0.3f;
};

// Velvet: two hall networks of different sizes in parallel with extra modulation. Denser and smoother than Hall.
// Starting point for a "big synth reverb" character; to be tuned by ear against reference recordings.
class VelvetReverb {
public:
    void prepare(float sr) { a_.prepare(sr); b_.prepare(sr); k_ = 1.0f - std::exp(-6.2831853f * 1200.0f / sr); reset(); }
    void reset() { a_.reset(); b_.reset(); loL_ = loR_ = 0; }
    void process(float inL, float inR, float& outL, float& outR, const ReverbParams& p) {
        ReverbParams pa = p, pb = p;
        pb.size = p.size * 1.37f; pb.modDepth = std::min(1.0f, p.modDepth * 1.5f + 0.15f);
        pa.dampHz = p.dampHz; pb.dampHz = p.dampHz * 0.8f;
        float l1, r1, l2, r2;
        a_.process(inL, inR, l1, r1, pa); b_.process(inR, inL, l2, r2, pb);   // second network gets swapped channels: wider
        outL = 0.6f * (l1 + l2); outR = 0.6f * (r1 + r2);
        // Coloration: tilt around 1.2 kHz, dark (-1) to bright (+1)
        const float t = std::max(-1.0f, std::min(1.0f, p.color)) * 0.7f;
        loL_ += k_ * (outL - loL_); loR_ += k_ * (outR - loR_);
        outL = loL_ * (1.0f - t) + (outL - loL_) * (1.0f + t);
        outR = loR_ * (1.0f - t) + (outR - loR_) * (1.0f + t);
    }
private:
    HallReverb a_, b_;
    float k_ = 0.1f, loL_ = 0, loR_ = 0;
};

// One reverb unit with all types. Shimmer = tail is pitch shifted and fed back into the reverb input.
class ReverbUnit {
public:
    void prepare(float sr) { plate_.prepare(sr); hall_.prepare(sr); velvet_.prepare(sr); ps_[0].prepare(sr, 80); ps_[1].prepare(sr, 80); qs_[0].prepare(sr, 80); qs_[1].prepare(sr, 80); sr_ = sr; reset(); }
    void reset() { plate_.reset(); hall_.reset(); velvet_.reset(); ps_[0].reset(); ps_[1].reset(); qs_[0].reset(); qs_[1].reset(); last_[0] = last_[1] = 0; for (float& v : f_) v = 0; }
    void process(float inL, float inR, float& outL, float& outR, const ReverbSettings& s) {
        bool shim = s.type == ReverbType::ShimmerPlate || s.type == ReverbType::ShimmerHall;
        float ratio = std::pow(2.0f, s.shimmerSemis / 12.0f);
        float fl = inL, fr = inR;
        if (shim) {
            // Loop gain stays below 1 whatever the knob says: amount 0..1 maps to 0..0.4, and the tail is soft limited below.
            float g = 0.4f * std::min(1.0f, std::max(0.0f, s.shimmerAmount));
            float g2 = 0.4f * std::min(1.0f, std::max(0.0f, s.shimmer2Amount));
            if (g + g2 > 0.4f) { float k = 0.4f / (g + g2); g *= k; g2 *= k; }   // both voices together never exceed the single-voice loop gain
            fl += g * ps_[0].tick(last_[0], ratio);
            fr += g * ps_[1].tick(last_[1], ratio);
            if (s.shimmer2Amount > 0.0f) {
                float r2 = std::pow(2.0f, s.shimmer2Semis / 12.0f);
                fl += g2 * qs_[0].tick(last_[0], r2);
                fr += g2 * qs_[1].tick(last_[1], r2);
            }
        }
        float wl, wr;
        if (s.type == ReverbType::Plate || s.type == ReverbType::ShimmerPlate) plate_.process(fl, fr, wl, wr, s.p);
        else if (s.type == ReverbType::Velvet) velvet_.process(fl, fr, wl, wr, s.p);
        else hall_.process(fl, fr, wl, wr, s.p);
        last_[0] = std::tanh(wl); last_[1] = std::tanh(wr);   // feedback tap taken before the output filters
        if (s.wetHpHz > 20.5f || s.wetLpHz < 19999.0f) {
            const float kh = 1.0f - std::exp(-6.2831853f * std::min(s.wetHpHz, 0.45f * sr_) / sr_);
            const float kl = 1.0f - std::exp(-6.2831853f * std::min(s.wetLpHz, 0.45f * sr_) / sr_);
            float* w[2] = {&wl, &wr};
            for (int c = 0; c < 2; ++c) {
                f_[c*2] += kl * (*w[c] - f_[c*2]); *w[c] = f_[c*2];            // low pass
                f_[c*2+1] += kh * (*w[c] - f_[c*2+1]); *w[c] -= f_[c*2+1];     // high pass
            }
        }
        outL = inL * (1 - s.mix) + wl * s.mix;
        outR = inR * (1 - s.mix) + wr * s.mix;
    }
private:
    PlateReverb plate_; HallReverb hall_; VelvetReverb velvet_; PitchShifter ps_[2], qs_[2]; float last_[2] = {}, f_[4] = {}, sr_ = 48000;
};

}  // namespace wm::engine
