// Purrfect Cut: which part of the file is on screen. Pure C++. `start` is the first visible sample, `spp` the number of
// samples under one pixel (below 1 means zoomed in past single samples, down to 1/32).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace purrfect {

struct View {
    size_t total = 0; int width = 1; double start = 0, spp = 1;
    static constexpr double kMinSpp = 1.0 / 32.0;

    double fitSpp() const { return std::max(kMinSpp, (double) total / (double) std::max(1, width)); }
    double visible() const { return spp * (double) width; }
    void clamp() {
        spp = std::min(std::max(spp, kMinSpp), fitSpp()); const double maxStart = std::max(0.0, (double) total - visible()); start = std::min(std::max(start, 0.0), maxStart);
    }
    void setSize(size_t totalSamples, int widthPx) { total = totalSamples; width = std::max(1, widthPx); clamp(); }
    void fit() { start = 0; spp = fitSpp(); }
    // factor < 1 zooms in, > 1 zooms out; the sample under anchorPx stays where it is
    void zoomAt(double factor, double anchorPx) { const double s = sampleAt(anchorPx); spp *= factor; clamp(); start = s - anchorPx * spp; clamp(); }
    void pan(double pixels) { start += pixels * spp; clamp(); }
    void centreOn(double sample) { start = sample - visible() / 2; clamp(); }
    void show(size_t a, size_t b, double marginFraction = 0.05) {
        if (b <= a + 1) return; const double len = (double) (b - a), m = len * marginFraction; spp = (len + 2 * m) / (double) width; clamp(); start = (double) a - m; clamp();
    }
    double sampleAt(double px) const { return start + px * spp; }
    double xOf(double sample) const { return (sample - start) / spp; }
    bool zoomedOut() const { return spp >= fitSpp() * 0.999; }
};

}  // namespace purrfect
