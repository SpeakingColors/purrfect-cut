// Purrfect Cut: small radix-2 complex FFT (double precision), no allocation in transform(). Pure C++.
#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace purrfect {

class Fft {
public:
    explicit Fft(int n = 2048) { init(n); }
    void init(int n) {
        n_ = n; rev_.assign((size_t) n, 0); tw_.assign((size_t) n / 2, {}); int lg = 0; while ((1 << lg) < n) ++lg;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < lg; ++b) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev_[(size_t) i] = r; }
        for (int i = 0; i < n / 2; ++i) tw_[(size_t) i] = std::polar(1.0, -2.0 * 3.14159265358979323846 * i / n);
    }
    int size() const { return n_; }
    // in place; inverse = true gives the unnormalised inverse (divide by n yourself)
    void transform(std::complex<double>* x, bool inverse) const {
        for (int i = 0; i < n_; ++i) { const int j = rev_[(size_t) i]; if (j > i) std::swap(x[i], x[j]); }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len / 2, step = n_ / len;
            for (int i = 0; i < n_; i += len) for (int k = 0; k < half; ++k) {
                std::complex<double> w = tw_[(size_t) (k * step)]; if (inverse) w = std::conj(w);
                const std::complex<double> a = x[i + k], b = x[i + k + half] * w; x[i + k] = a + b; x[i + k + half] = a - b;
            }
        }
    }
private:
    int n_ = 0; std::vector<int> rev_; std::vector<std::complex<double>> tw_;
};

}  // namespace purrfect
