// Purrfect Cut: better pitch tracking for the editor. A pYIN style tracker: every frame keeps several pitch candidates (the dips of the
// YIN difference function, each with a confidence) plus an "unvoiced" state, and a Viterbi search picks the most believable path over
// time. That removes octave slips and one-frame blips without a hard rule, and decides voiced / unvoiced from the whole context.
// Also gives per-frame loudness so the editor can show and edit the amplitude of every note.
#pragma once
#include "Pitch.h"
#include <array>

namespace purrfect {

struct TrackFrames { int hop = 256; std::vector<float> f0, conf, rms; };   // f0 in Hz (0 = unvoiced), conf 0..1, rms linear

struct Cand { float tau; float p; };
// up to maxC candidates, best first: dips of the cumulative mean normalised difference below `top`
inline int yinCandidates(const float* x, int W, int tauMin, int tauMax, YinState& st, Cand* out, int maxC, float top = 0.5f) {
    float* d = st.d.data(); float* cm = st.cm.data(); float en = 0.f; for (int j = 0; j < W; ++j) en += x[j] * x[j]; if (en < 1e-7f * (float) W) return 0;
    for (int tau = 1; tau <= tauMax; ++tau) { float s = 0.f; const float* y = x + tau; for (int j = 0; j < W; ++j) { const float df = x[j] - y[j]; s += df * df; } d[tau] = s; }
    cm[0] = 1.f; float run = 0.f; for (int tau = 1; tau <= tauMax; ++tau) { run += d[tau]; cm[tau] = run > 1e-12f ? d[tau] * (float) tau / run : 1.f; }
    int n = 0;
    for (int tau = std::max(2, tauMin); tau < tauMax; ++tau) {
        if (cm[tau] < top && cm[tau] <= cm[tau - 1] && cm[tau] <= cm[tau + 1]) {
            float t = (float) tau; const float a = cm[tau - 1], b = cm[tau], c = cm[tau + 1], den = a - 2.f * b + c; if (std::fabs(den) > 1e-9f) t += 0.5f * (a - c) / den;
            const Cand c1 { t, std::max(0.f, 1.f - cm[tau]) };
            if (n < maxC) out[n++] = c1; else { int w = 0; for (int i = 1; i < n; ++i) if (out[i].p < out[w].p) w = i; if (c1.p > out[w].p) out[w] = c1; }
        }
    }
    std::sort(out, out + n, [](const Cand& a, const Cand& b) { return a.p > b.p; });
    return n;
}

inline TrackFrames pyinTrack(const float* x, size_t n, double sr, int hop = 256, float fmin = 70.f, float fmax = 1000.f) {
    TrackFrames tf; tf.hop = hop; const size_t nf = n / (size_t) hop + 1; tf.f0.assign(nf, 0.f); tf.conf.assign(nf, 0.f); tf.rms.assign(nf, 0.f); if (n < 64) return tf;
    const double sd = sr / 2.0; const int tauMax = (int) std::ceil(sd / fmin), tauMin = std::max(2, (int) std::floor(sd / fmax)), W = 512, tot = W + tauMax;
    std::vector<float> y(n / 2 + 1, 0.f); for (size_t i = 0; i + 1 < n; i += 2) y[i / 2] = 0.5f * (x[i] + x[i + 1]);
    YinState st; st.init(tauMax); std::vector<float> win((size_t) tot); constexpr int K = 5;
    std::vector<std::vector<Cand>> cands(nf);
    for (size_t f = 0; f < nf; ++f) {
        const long c = (long) (f * (size_t) hop / 2), s0 = c - tot / 2;
        for (int k = 0; k < tot; ++k) { const long i = s0 + k; win[(size_t) k] = i >= 0 && i < (long) y.size() ? y[(size_t) i] : 0.f; }
        Cand cc[K]; const int m = yinCandidates(win.data(), W, tauMin, tauMax, st, cc, K); cands[f].assign(cc, cc + m);
        double e = 0.0; const long r0 = (long) (f * (size_t) hop) - hop, r1 = r0 + 2 * hop; long cnt = 0; for (long i = std::max(0L, r0); i < std::min((long) n, r1); ++i) { e += (double) x[(size_t) i] * x[(size_t) i]; ++cnt; }
        tf.rms[f] = cnt ? (float) std::sqrt(e / (double) cnt) : 0.f;
    }
    // Viterbi: states 0..K-1 = candidate i of the frame, state K = unvoiced
    const float INF = 1e30f; std::vector<std::array<float, K + 1>> cost(nf); std::vector<std::array<signed char, K + 1>> back(nf);
    auto obs = [&](size_t f, int s) -> float {
        if (s == K) { const float best = cands[f].empty() ? 0.f : cands[f][0].p; return -std::log(1.f - 0.92f * best + 1e-3f) + 0.15f; }
        if (s >= (int) cands[f].size()) return INF; return -std::log(cands[f][(size_t) s].p + 1e-3f);
    };
    auto freq = [&](size_t f, int s) { return (float) (sd / cands[f][(size_t) s].tau); };
    for (size_t f = 0; f < nf; ++f) for (int s = 0; s <= K; ++s) {
        cost[f][(size_t) s] = INF; back[f][(size_t) s] = 0; const float o = obs(f, s); if (o >= INF) continue;
        if (f == 0) { cost[f][(size_t) s] = o; continue; }
        float bestC = INF; int bi = 0;
        for (int q = 0; q <= K; ++q) {
            const float pc = cost[f - 1][(size_t) q]; if (pc >= INF) continue; float tr;
            if (s == K && q == K) tr = 0.f; else if (s == K || q == K) tr = 1.2f;
            else { const float dm = std::fabs(12.f * std::log2(freq(f, s) / freq(f - 1, q))); tr = dm < 0.5f ? 0.02f * dm : std::min(6.f, 0.25f + 0.55f * dm) + (dm > 10.5f ? 3.f : 0.f); }
            const float c = pc + tr; if (c < bestC) { bestC = c; bi = q; }
        }
        if (bestC < INF) { cost[f][(size_t) s] = bestC + o; back[f][(size_t) s] = (signed char) bi; }
    }
    int s = K; float bc = INF; for (int q = 0; q <= K; ++q) if (cost[nf - 1][(size_t) q] < bc) { bc = cost[nf - 1][(size_t) q]; s = q; }
    for (size_t f = nf; f-- > 0;) {
        if (s != K && s < (int) cands[f].size()) { tf.f0[f] = freq(f, s); tf.conf[f] = cands[f][(size_t) s].p; }
        s = back[f][(size_t) s];
    }
    // a voiced run shorter than 3 frames is noise
    for (size_t i = 0; i < nf;) { if (!(tf.f0[i] > 0.f)) { ++i; continue; } size_t e = i; while (e < nf && tf.f0[e] > 0.f) ++e; if (e - i < 3) for (size_t q = i; q < e; ++q) tf.f0[q] = 0.f; i = e; }
    return tf;
}
inline PitchFrames toPitchFrames(const TrackFrames& t) { PitchFrames p; p.hop = t.hop; p.f0 = t.f0; return p; }

}  // namespace purrfect
