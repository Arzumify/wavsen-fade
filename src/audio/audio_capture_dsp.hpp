#pragma once

// Shared DSP bits between the PulseAudio and PipeWire backends of
// wavsen::audio::AudioCapture. Pure CPU code; no backend headers.

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace wavsen::audio::dsp {

inline constexpr std::size_t kFftSize  = 1024;
inline constexpr std::size_t kNumBins  = 64;
inline constexpr std::size_t kHalfFft  = kFftSize / 2;

// Attack-biased EMA: fast rise, slow decay — better for visual
// reactivity than symmetric smoothing.
inline constexpr float kAlphaUp   = 0.6f;
inline constexpr float kAlphaDown = 0.2f;

// Log-spaced edges over the FFT half-spectrum (bins 1..N/2). Index k
// covers FFT bins [edges[k], edges[k+1]). Each band edge grows
// geometrically with a floor of +1 so consecutive bands never collide
// at the low end where bin spacing is tight.
inline const std::array<std::size_t, kNumBins + 1> kBandEdges = [] {
    std::array<std::size_t, kNumBins + 1> e {};
    e[0] = 1;
    for (std::size_t k = 1; k <= kNumBins; ++k) {
        const double t = std::pow(static_cast<double>(kHalfFft),
                                  static_cast<double>(k) / static_cast<double>(kNumBins));
        std::size_t next = static_cast<std::size_t>(std::ceil(t));
        if (next <= e[k - 1]) next = e[k - 1] + 1;
        if (next > kHalfFft)  next = kHalfFft;
        e[k] = next;
    }
    return e;
}();

// In-place radix-2 Cooley-Tukey FFT (forward).
inline void fft_inplace(std::complex<float>* data, std::size_t n) {
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.f * std::numbers::pi_v<float> / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            const std::size_t half = len >> 1;
            for (std::size_t k = 0; k < half; ++k) {
                const auto u = data[i + k];
                const auto v = data[i + k + half] * w;
                data[i + k]        = u + v;
                data[i + k + half] = u - v;
                w *= wlen;
            }
        }
    }
}

inline float hann_window(std::size_t i, std::size_t n) {
    return 0.5f * (1.0f - std::cos(2.f * std::numbers::pi_v<float>
                                   * static_cast<float>(i)
                                   / static_cast<float>(n - 1)));
}

} // namespace wavsen::audio::dsp
