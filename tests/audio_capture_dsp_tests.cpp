#include "audio_capture_dsp.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace
{

constexpr float kSampleRate = 48000.0f;

using Buffer = std::array<std::complex<float>, wavsen::audio::dsp::kFftSize>;

void fill_sine(Buffer& left, Buffer& right, float hz, float left_amp, float right_amp) {
    for (std::size_t i = 0; i < wavsen::audio::dsp::kFftSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float s = std::sin(2.0f * std::numbers::pi_v<float> * hz * t);
        const float w = wavsen::audio::dsp::hann_window(i, wavsen::audio::dsp::kFftSize);
        left[i] = std::complex<float>(left_amp * s * w, 0.0f);
        right[i] = std::complex<float>(right_amp * s * w, 0.0f);
    }
    wavsen::audio::dsp::fft_inplace(left.data(), left.size());
    wavsen::audio::dsp::fft_inplace(right.data(), right.size());
}

std::size_t expected_band(const wavsen::audio::dsp::BandLayout& layout, float hz) {
    const auto bin = wavsen::audio::dsp::hz_to_upper_bin(hz, kSampleRate);
    for (std::size_t k = 0; k < wavsen::audio::dsp::kNumBins; ++k) {
        if (bin >= layout.edges[k] && bin < layout.edges[k + 1])
            return k;
    }
    return wavsen::audio::dsp::kNumBins - 1;
}

std::size_t peak_band(const std::array<float, wavsen::audio::dsp::kNumBins>& values) {
    std::size_t band = 0;
    float peak = values[0];
    for (std::size_t k = 1; k < values.size(); ++k) {
        if (values[k] > peak) {
            band = k;
            peak = values[k];
        }
    }
    return band;
}

bool near_band(std::size_t actual, std::size_t expected, std::size_t tolerance) {
    return actual <= expected + tolerance && expected <= actual + tolerance;
}

bool test_frequency_mapping() {
    const auto layout = wavsen::audio::dsp::make_mel_layout(kSampleRate);
    const std::array<float, 5> tones{60.0f, 250.0f, 1000.0f, 6000.0f, 12000.0f};

    std::size_t previous = 0;
    bool first = true;
    for (float hz : tones) {
        Buffer left{};
        Buffer right{};
        fill_sine(left, right, hz, 0.5f, 0.5f);
        const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
            left.data(), right.data(), layout, 2.0f / static_cast<float>(left.size()));
        const auto actual = peak_band(raw.average);
        const auto expected = expected_band(layout, hz);
        if (!near_band(actual, expected, 2)) {
            std::cerr << "tone " << hz << " Hz peaked at band " << actual << ", expected near "
                      << expected << "\n";
            return false;
        }
        if (!first && actual <= previous) {
            std::cerr << "tone bands are not increasing at " << hz << " Hz\n";
            return false;
        }
        first = false;
        previous = actual;
    }
    return true;
}

bool test_channel_split_and_average() {
    const auto layout = wavsen::audio::dsp::make_mel_layout(kSampleRate);
    Buffer left{};
    Buffer right{};
    fill_sine(left, right, 1000.0f, 1.0f, 0.0f);
    const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
        left.data(), right.data(), layout, 2.0f / static_cast<float>(left.size()));
    const auto band = peak_band(raw.left);

    if (raw.left[band] <= 0.0f || raw.left[band] > wavsen::audio::dsp::kResponseCeil) {
        std::cerr << "left response outside expected range: " << raw.left[band] << "\n";
        return false;
    }
    if (raw.right[band] != 0.0f) {
        std::cerr << "silent right channel produced " << raw.right[band] << "\n";
        return false;
    }
    const float expected_average = raw.left[band] * 0.5f;
    if (std::abs(raw.average[band] - expected_average) > 1.0e-5f) {
        std::cerr << "average mismatch: " << raw.average[band] << " vs " << expected_average
                  << "\n";
        return false;
    }
    return true;
}

bool test_response_cap() {
    const auto layout = wavsen::audio::dsp::make_mel_layout(kSampleRate);
    Buffer left{};
    Buffer right{};
    fill_sine(left, right, 1000.0f, 4.0f, 4.0f);
    const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
        left.data(), right.data(), layout, 2.0f / static_cast<float>(left.size()));
    for (float v : raw.average) {
        if (v > wavsen::audio::dsp::kResponseCeil) {
            std::cerr << "response exceeded cap: " << v << "\n";
            return false;
        }
    }
    return true;
}

float response_for_unit(const wavsen::audio::dsp::BandLayout& layout, std::size_t band,
                        float unit) {
    const float db = wavsen::audio::dsp::kDbFloor +
                     unit * (wavsen::audio::dsp::kDbCeil - wavsen::audio::dsp::kDbFloor);
    const float compensated = std::pow(10.0f, db / 20.0f);
    return wavsen::audio::dsp::visual_response(compensated / layout.gain[band], layout, band);
}

bool test_response_contrast() {
    const auto layout = wavsen::audio::dsp::make_mel_layout(kSampleRate);
    const auto band = expected_band(layout, 1000.0f);
    const float low = response_for_unit(layout, band, 0.4f);
    const float mid = response_for_unit(layout, band, 0.5f);
    const float high = response_for_unit(layout, band, 0.6f);

    if (low >= 0.4f) {
        std::cerr << "low response was not reduced: " << low << "\n";
        return false;
    }
    if (std::abs(mid - 0.5f) > 1.0e-5f) {
        std::cerr << "mid response moved: " << mid << "\n";
        return false;
    }
    if (high <= 0.6f || high - mid <= 0.1f) {
        std::cerr << "high response was not expanded: " << high << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!test_frequency_mapping())
        return EXIT_FAILURE;
    if (!test_channel_split_and_average())
        return EXIT_FAILURE;
    if (!test_response_cap())
        return EXIT_FAILURE;
    if (!test_response_contrast())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
