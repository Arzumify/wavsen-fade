#pragma once

// Shared DSP bits between the PulseAudio and PipeWire backends of
// wavsen::audio::AudioCapture. Pure CPU code; no backend headers.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace wavsen::audio::dsp
{

inline constexpr std::size_t kFftSize = 4096;
inline constexpr std::size_t kNumBins = 64;
inline constexpr std::size_t kHalfFft = kFftSize / 2;
inline constexpr std::size_t kHopSize = 1024;
inline constexpr float kMinFrequencyHz = 20.0f;
inline constexpr float kMaxFrequencyHz = 20000.0f;
inline constexpr float kTiltPivotHz = 200.0f;
inline constexpr float kTiltExp = 0.30f;
inline constexpr float kDbFloor = -80.0f;
inline constexpr float kDbCeil = -8.0f;
inline constexpr float kResponseContrast = 1.6f;
inline constexpr float kResponseScale = 1.0f;
inline constexpr float kResponseCeil = 1.0f;
inline constexpr float kAttackTimeSec = 0.030f;
inline constexpr float kReleaseTimeSec = 0.140f;

struct BandLayout {
    std::array<std::size_t, kNumBins + 1> edges{};
    std::array<float, kNumBins> gain{};
};

struct SpectrumBands {
    std::array<float, kNumBins> left{};
    std::array<float, kNumBins> right{};
    std::array<float, kNumBins> average{};
};

inline float hz_to_mel(float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); }

inline float mel_to_hz(float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); }

inline std::size_t hz_to_upper_bin(float hz, float sample_rate) {
    const auto bin =
        static_cast<std::size_t>(std::ceil(static_cast<double>(hz) * static_cast<double>(kFftSize) /
                                           static_cast<double>(sample_rate)));
    return std::clamp<std::size_t>(bin, 1, kHalfFft);
}

inline BandLayout make_mel_layout(float sample_rate) {
    BandLayout layout{};
    const float nyquist = sample_rate * 0.5f;
    const float max_hz = std::min(kMaxFrequencyHz, nyquist);
    const float min_hz = std::min(kMinFrequencyHz, max_hz);
    const float min_mel = hz_to_mel(min_hz);
    const float max_mel = hz_to_mel(max_hz);
    const auto max_bin = hz_to_upper_bin(max_hz, sample_rate);

    layout.edges[0] = hz_to_upper_bin(min_hz, sample_rate);
    for (std::size_t k = 1; k < kNumBins; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(kNumBins);
        const float hz = mel_to_hz(min_mel + (max_mel - min_mel) * t);
        std::size_t next = hz_to_upper_bin(hz, sample_rate);
        if (next <= layout.edges[k - 1])
            next = layout.edges[k - 1] + 1;
        const std::size_t remaining = kNumBins - k;
        if (next + remaining > max_bin)
            next = max_bin - remaining;
        layout.edges[k] = next;
    }
    layout.edges[kNumBins] = max_bin;
    for (std::size_t k = 0; k < kNumBins; ++k) {
        const float center_bin =
            0.5f * (static_cast<float>(layout.edges[k]) + static_cast<float>(layout.edges[k + 1]));
        const float center_hz = center_bin * sample_rate / static_cast<float>(kFftSize);
        layout.gain[k] = std::pow(center_hz / kTiltPivotHz, kTiltExp);
    }
    return layout;
}

inline float band_magnitude(const std::complex<float>* left, const BandLayout& layout,
                            std::size_t band, float norm) {
    const std::size_t lo = layout.edges[band];
    const std::size_t hi = layout.edges[band + 1];
    float peak = 0.f;
    for (std::size_t i = lo; i < hi; ++i) {
        const float v = std::abs(left[i]);
        if (v > peak)
            peak = v;
    }
    return peak * norm;
}

inline float shape_response(float unit) {
    const float x = std::clamp(unit, 0.0f, 1.0f);
    if (x <= 0.5f)
        return 0.5f * std::pow(x * 2.0f, kResponseContrast);
    return 1.0f - 0.5f * std::pow((1.0f - x) * 2.0f, kResponseContrast);
}

inline float visual_response(float mag, const BandLayout& layout, std::size_t band) {
    const float compensated = std::max(mag * layout.gain[band], 1.0e-12f);
    const float db = 20.0f * std::log10(compensated);
    const float unit = std::clamp((db - kDbFloor) / (kDbCeil - kDbFloor), 0.0f, 1.0f);
    const float shaped = shape_response(unit);
    return std::min(shaped * kResponseScale, kResponseCeil);
}

inline SpectrumBands analyze_stereo_spectrum(const std::complex<float>* left,
                                             const std::complex<float>* right,
                                             const BandLayout& layout, float norm) {
    SpectrumBands raw{};
    for (std::size_t k = 0; k < kNumBins; ++k) {
        raw.left[k] = visual_response(band_magnitude(left, layout, k, norm), layout, k);
        raw.right[k] = visual_response(band_magnitude(right, layout, k, norm), layout, k);
        raw.average[k] = 0.5f * (raw.left[k] + raw.right[k]);
    }
    return raw;
}

inline float smooth_value(float prev, float cur, float dt_sec) {
    const float tau = cur > prev ? kAttackTimeSec : kReleaseTimeSec;
    const float a = 1.0f - std::exp(-dt_sec / tau);
    return prev + a * (cur - prev);
}

inline SpectrumBands smooth_spectrum(const SpectrumBands& raw, SpectrumBands& state, float dt_sec) {
    SpectrumBands out{};
    for (std::size_t k = 0; k < kNumBins; ++k) {
        state.left[k] = smooth_value(state.left[k], raw.left[k], dt_sec);
        state.right[k] = smooth_value(state.right[k], raw.right[k], dt_sec);
        out.left[k] = state.left[k];
        out.right[k] = state.right[k];
        out.average[k] = 0.5f * (out.left[k] + out.right[k]);
    }
    return out;
}

// In-place radix-2 Cooley-Tukey FFT (forward).
inline void fft_inplace(std::complex<float>* data, std::size_t n) {
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
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
                data[i + k] = u + v;
                data[i + k + half] = u - v;
                w *= wlen;
            }
        }
    }
}

inline float hann_window(std::size_t i, std::size_t n) {
    return 0.5f * (1.0f - std::cos(2.f * std::numbers::pi_v<float> * static_cast<float>(i) /
                                   static_cast<float>(n - 1)));
}

} // namespace wavsen::audio::dsp
