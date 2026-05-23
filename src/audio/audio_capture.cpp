module;

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <chrono>
#include <cmath>
#include <complex>
#include <numbers>

module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import pipewire;
import :capture;

namespace wavsen::audio {

namespace {

std::once_flag g_pw_init_once_capture;
void ensure_pw_init() {
    std::call_once(g_pw_init_once_capture, [] { pw_init(nullptr, nullptr); });
}

constexpr std::uint32_t kDefaultRate     = 48000;
constexpr std::uint32_t kDefaultChannels = 2;
constexpr std::uint32_t kQuantum         = 1024;

constexpr std::size_t kNumBins  = 64;
constexpr std::size_t kHalfFft  = 512;

// Log-spaced edges over the FFT half-spectrum (bins 1..N/2). Index k
// covers FFT bins [edges[k], edges[k+1]). Generated once at startup;
// each band edge grows geometrically with a floor of +1 so consecutive
// bands never collide at the low end where bin spacing is tight.
const std::array<std::size_t, kNumBins + 1> kBandEdges = [] {
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
void fft_inplace(std::complex<float>* data, std::size_t n) {
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

float hann_window(std::size_t i, std::size_t n) {
    return 0.5f * (1.0f - std::cos(2.f * std::numbers::pi_v<float>
                                   * static_cast<float>(i)
                                   / static_cast<float>(n - 1)));
}

} // namespace

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() { uninit(); }

bool AudioCapture::init() {
    if (is_inited()) return true;

    ensure_pw_init();

    loop_ = pw_thread_loop_new("wavsen-capture", nullptr);
    if (! loop_) {
        rstd::log::error("wavsen::audio: capture pw_thread_loop_new failed");
        return false;
    }

    if (pw_thread_loop_start(loop_) < 0) {
        rstd::log::error("wavsen::audio: capture pw_thread_loop_start failed");
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    static const ::pw_stream_events stream_events = {
        .version       = PW_VERSION_STREAM_EVENTS,
        .destroy       = nullptr,
        .state_changed = &AudioCapture::on_state_changed,
        .control_info  = nullptr,
        .io_changed    = nullptr,
        .param_changed = nullptr,
        .add_buffer    = nullptr,
        .remove_buffer = nullptr,
        .process       = &AudioCapture::on_process,
        .drained       = nullptr,
        .command       = nullptr,
        .trigger_done  = nullptr,
    };

    pw_thread_loop_lock(loop_);

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,       "Audio",
        PW_KEY_MEDIA_CATEGORY,   "Capture",
        PW_KEY_MEDIA_ROLE,       "Music",
        PW_KEY_APP_NAME,         "wavsen",
        PW_KEY_NODE_NAME,        "wavsen-capture",
        PW_KEY_NODE_DESCRIPTION, "wavsen audio response capture",
        PW_KEY_STREAM_CAPTURE_SINK, "true",
        nullptr);
    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u", kQuantum, kDefaultRate);

    stream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        "wavsen-capture",
        props,
        &stream_events,
        this);
    if (! stream_) {
        pw_thread_loop_unlock(loop_);
        rstd::log::error("wavsen::audio: capture pw_stream_new_simple failed");
        pw_thread_loop_stop(loop_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    std::uint8_t   pod_buffer[1024];
    spa_pod_builder b {};
    b.data = pod_buffer;
    b.size = sizeof(pod_buffer);

    spa_audio_info_raw info {};
    info.format   = SPA_AUDIO_FORMAT_F32_LE;
    info.rate     = kDefaultRate;
    info.channels = kDefaultChannels;

    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    const auto flags = static_cast<pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS);

    if (pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY, flags,
                          params, 1) < 0)
    {
        rstd::log::error("wavsen::audio: capture pw_stream_connect failed");
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    pw_thread_loop_unlock(loop_);

    rstd::log::info("wavsen::audio: capture inited (monitor sink, "
                    "{} ch @ {} Hz)", kDefaultChannels, kDefaultRate);
    return true;
}

void AudioCapture::uninit() {
    if (stream_) {
        pw_thread_loop_lock(loop_);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_unlock(loop_);
    }
    if (loop_) {
        pw_thread_loop_stop(loop_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
}

bool AudioCapture::snapshot(AudioSpectrum& out) const {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::uint32_t s1 = seq_.load(std::memory_order_acquire);
        if (s1 == 0) {
            out.bins.fill(0.0f);
            return false;
        }
        if (s1 & 1u) continue;
        AudioSpectrum tmp;
        std::memcpy(&tmp, &published_, sizeof(AudioSpectrum));
        const std::uint32_t s2 = seq_.load(std::memory_order_acquire);
        if (s1 == s2) {
            out = tmp;
            return true;
        }
    }
    out.bins.fill(0.0f);
    return false;
}

void AudioCapture::on_process(void* user) {
    auto* self = static_cast<AudioCapture*>(user);
    if (! self->stream_) return;

    pw_buffer* b = pw_stream_dequeue_buffer(self->stream_);
    if (! b) return;

    auto* sb = b->buffer;
    if (! sb || sb->n_datas == 0 || ! sb->datas[0].data) {
        pw_stream_queue_buffer(self->stream_, b);
        return;
    }

    auto& d         = sb->datas[0];
    const auto stride = d.chunk->stride > 0
                            ? static_cast<std::uint32_t>(d.chunk->stride)
                            : kDefaultChannels * static_cast<std::uint32_t>(sizeof(float));
    const auto channels = stride / static_cast<std::uint32_t>(sizeof(float));
    const std::uint32_t offset = d.chunk->offset % d.maxsize;
    const std::uint32_t bytes  = std::min(d.chunk->size, d.maxsize - offset);
    const auto* src = reinterpret_cast<const float*>(
        static_cast<const std::uint8_t*>(d.data) + offset);
    const std::uint32_t n_frames = bytes / stride;

    // Downmix to mono and push into the ring.
    for (std::uint32_t f = 0; f < n_frames; ++f) {
        float sum = 0.f;
        for (std::uint32_t c = 0; c < channels; ++c) {
            sum += src[f * channels + c];
        }
        const float mono = channels > 0 ? sum / static_cast<float>(channels) : 0.f;
        self->ring_[self->ring_head_] = mono;
        self->ring_head_ = (self->ring_head_ + 1) % kFftSize;
        ++self->samples_since_fft_;
    }

    pw_stream_queue_buffer(self->stream_, b);

    // Run FFT at most once per process callback, after enough new samples
    // (>= half-window) have accumulated. Avoids redundant CPU on tiny
    // quanta while still keeping bin freshness ≥ 100 Hz.
    if (self->samples_since_fft_ < kFftSize / 2) return;
    self->samples_since_fft_ = 0;

    std::array<std::complex<float>, kFftSize> buf;
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const std::size_t idx = (self->ring_head_ + i) % kFftSize;
        const float w = hann_window(i, kFftSize);
        buf[i] = std::complex<float>(self->ring_[idx] * w, 0.f);
    }

    fft_inplace(buf.data(), kFftSize);

    // Aggregate magnitudes into 16 log-spaced bands.
    AudioSpectrum raw {};
    const float norm = 2.0f / static_cast<float>(kFftSize);
    for (std::size_t k = 0; k < kNumBins; ++k) {
        const std::size_t lo = kBandEdges[k];
        const std::size_t hi = kBandEdges[k + 1];
        float sum = 0.f;
        for (std::size_t i = lo; i < hi; ++i) {
            sum += std::abs(buf[i]);
        }
        const float mag = sum / static_cast<float>(hi - lo) * norm;
        // Soft compressor so typical music sits in 0..1.
        raw.bins[k] = std::tanh(mag * 4.0f);
    }

    // Attack-biased EMA: fast rise, slow decay — better for visual
    // reactivity than symmetric smoothing.
    constexpr float kAlphaUp   = 0.6f;
    constexpr float kAlphaDown = 0.2f;
    AudioSpectrum out {};
    for (std::size_t k = 0; k < kNumBins; ++k) {
        const float prev = self->smoothed_[k];
        const float cur  = raw.bins[k];
        const float a    = cur > prev ? kAlphaUp : kAlphaDown;
        const float v    = a * cur + (1.0f - a) * prev;
        self->smoothed_[k] = v;
        out.bins[k]        = v;
    }
    out.publish_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

    // Publish via seqlock — fetch_add by 1 toggles parity each call.
    self->seq_.fetch_add(1, std::memory_order_release);
    std::memcpy(&self->published_, &out, sizeof(AudioSpectrum));
    self->seq_.fetch_add(1, std::memory_order_release);
}

void AudioCapture::on_state_changed(void* /*user*/, ::pw_stream_state /*old*/,
                                    ::pw_stream_state state, const char* error) {
    switch (state) {
    case PW_STREAM_STATE_ERROR:
        rstd::log::error("wavsen::audio: capture stream ERROR{}",
                         error ? std::string(": ") + error : std::string{});
        break;
    case PW_STREAM_STATE_UNCONNECTED:
        rstd::log::debug("wavsen::audio: capture stream UNCONNECTED");
        break;
    case PW_STREAM_STATE_CONNECTING:
        rstd::log::debug("wavsen::audio: capture stream CONNECTING");
        break;
    case PW_STREAM_STATE_PAUSED:
        rstd::log::debug("wavsen::audio: capture stream PAUSED");
        break;
    case PW_STREAM_STATE_STREAMING:
        rstd::log::debug("wavsen::audio: capture stream STREAMING");
        break;
    }
}

} // namespace wavsen::audio
