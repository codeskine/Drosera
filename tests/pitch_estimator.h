// Fundamental-frequency estimation used by the pitch tests.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}

// Frequency of the strongest spectral peak above 15 Hz. Do not replace this with a fit to the
// note's harmonic grid: every partial of a note fits it, so a drone sounding on the 7th harmonic
// would still score as "in tune".
inline double dominantHz(const std::vector<float>& x, size_t start, double sampleRate = 48000.0) {
    const size_t N = 1 << 17;
    std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < N; ++i) a[i] = (double)x[start + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)N));
    fft(a);
    const double bin = sampleRate / (double)N;
    double mx = 0; size_t mk = 2;
    for (size_t k = (size_t)(15.0 / bin); k < N / 2 - 1; ++k) { const double m = std::abs(a[k]); if (m > mx) { mx = m; mk = k; } }
    const double l = std::log(std::abs(a[mk - 1]) + 1e-30), c = std::log(mx + 1e-30), r = std::log(std::abs(a[mk + 1]) + 1e-30);
    return ((double)mk + 0.5 * (l - r) / (l - 2 * c + r)) * bin;
}
