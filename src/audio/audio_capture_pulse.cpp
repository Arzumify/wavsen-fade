module;

#include <pulse/pulseaudio.h>

#include <chrono>
#include <cstring>
#include <string>

#include "audio_capture_dsp.hpp"

module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import pulse;
import :capture;

namespace wavsen::audio {

namespace {

constexpr std::uint32_t kDefaultRate     = 48000;
constexpr std::uint32_t kDefaultChannels = 2;
constexpr std::uint32_t kQuantum         = 1024;

} // namespace

class AudioCapture::Impl {
public:
    ~Impl() { uninit(); }

    bool init() {
        if (is_inited()) return true;

        loop_ = pa_threaded_mainloop_new();
        if (! loop_) {
            rstd::log::error("wavsen::audio: capture pa_threaded_mainloop_new failed");
            return false;
        }
        if (pa_threaded_mainloop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: capture pa_threaded_mainloop_start failed");
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }

        pa_threaded_mainloop_lock(loop_);

        ctx_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "wavsen-capture");
        if (! ctx_) {
            pa_threaded_mainloop_unlock(loop_);
            rstd::log::error("wavsen::audio: capture pa_context_new failed");
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }
        pa_context_set_state_callback(ctx_, &Impl::on_context_state, this);

        if (pa_context_connect(ctx_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
            rstd::log::error("wavsen::audio: capture pa_context_connect failed: {}",
                             pa_strerror(pa_context_errno(ctx_)));
            destroy_locked();
            pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }

        for (;;) {
            const auto st = pa_context_get_state(ctx_);
            if (st == PA_CONTEXT_READY) break;
            if (! PA_CONTEXT_IS_GOOD(st)) {
                rstd::log::error("wavsen::audio: capture pa_context failed: {}",
                                 pa_strerror(pa_context_errno(ctx_)));
                destroy_locked();
                pa_threaded_mainloop_unlock(loop_);
                shutdown_loop();
                return false;
            }
            pa_threaded_mainloop_wait(loop_);
        }

        // Resolve default sink → "<sink>.monitor" source name.
        default_sink_.clear();
        server_info_done_ = false;
        auto* op = pa_context_get_server_info(ctx_, &Impl::on_server_info, this);
        if (! op) {
            rstd::log::error("wavsen::audio: capture pa_context_get_server_info failed");
            destroy_locked();
            pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        while (! server_info_done_) {
            pa_threaded_mainloop_wait(loop_);
        }
        if (default_sink_.empty()) {
            rstd::log::error("wavsen::audio: capture could not resolve default sink");
            destroy_locked();
            pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        const std::string monitor_name = default_sink_ + ".monitor";

        pa_sample_spec ss {};
        ss.format   = PA_SAMPLE_FLOAT32LE;
        ss.rate     = kDefaultRate;
        ss.channels = static_cast<std::uint8_t>(kDefaultChannels);

        pa_channel_map cm {};
        pa_channel_map_init_stereo(&cm);

        stream_ = pa_stream_new(ctx_, "wavsen-capture", &ss, &cm);
        if (! stream_) {
            rstd::log::error("wavsen::audio: capture pa_stream_new failed: {}",
                             pa_strerror(pa_context_errno(ctx_)));
            destroy_locked();
            pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        pa_stream_set_state_callback(stream_, &Impl::on_stream_state, this);
        pa_stream_set_read_callback(stream_, &Impl::on_read, this);

        const auto frame_bytes = kDefaultChannels * static_cast<std::uint32_t>(sizeof(float));
        pa_buffer_attr ba {};
        ba.maxlength = static_cast<std::uint32_t>(-1);
        ba.tlength   = static_cast<std::uint32_t>(-1);
        ba.prebuf    = static_cast<std::uint32_t>(-1);
        ba.minreq    = static_cast<std::uint32_t>(-1);
        ba.fragsize  = kQuantum * frame_bytes;

        const auto flags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY);

        if (pa_stream_connect_record(stream_, monitor_name.c_str(), &ba, flags) < 0) {
            rstd::log::error("wavsen::audio: capture pa_stream_connect_record failed: {}",
                             pa_strerror(pa_context_errno(ctx_)));
            destroy_locked();
            pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }

        for (;;) {
            const auto st = pa_stream_get_state(stream_);
            if (st == PA_STREAM_READY) break;
            if (! PA_STREAM_IS_GOOD(st)) {
                rstd::log::error("wavsen::audio: capture pa_stream failed: {}",
                                 pa_strerror(pa_context_errno(ctx_)));
                destroy_locked();
                pa_threaded_mainloop_unlock(loop_);
                shutdown_loop();
                return false;
            }
            pa_threaded_mainloop_wait(loop_);
        }

        pa_threaded_mainloop_unlock(loop_);

        rstd::log::info("wavsen::audio: capture inited (pulse monitor '{}', "
                        "{} ch @ {} Hz)", monitor_name,
                        kDefaultChannels, kDefaultRate);
        return true;
    }

    void uninit() {
        if (loop_) {
            pa_threaded_mainloop_lock(loop_);
            destroy_locked();
            pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
        }
    }

    bool is_inited() const { return loop_ != nullptr && stream_ != nullptr; }

    bool snapshot(AudioSpectrum& out) const {
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

private:
    void destroy_locked() {
        if (stream_) {
            pa_stream_set_state_callback(stream_, nullptr, nullptr);
            pa_stream_set_read_callback(stream_, nullptr, nullptr);
            pa_stream_disconnect(stream_);
            pa_stream_unref(stream_);
            stream_ = nullptr;
        }
        if (ctx_) {
            pa_context_set_state_callback(ctx_, nullptr, nullptr);
            pa_context_disconnect(ctx_);
            pa_context_unref(ctx_);
            ctx_ = nullptr;
        }
    }
    void shutdown_loop() {
        if (loop_) {
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
        }
    }

    static void on_context_state(::pa_context* /*c*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_stream_state(::pa_stream* /*s*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_server_info(::pa_context* /*c*/, const ::pa_server_info* info, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (info && info->default_sink_name) {
            self->default_sink_ = info->default_sink_name;
        }
        self->server_info_done_ = true;
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_read(::pa_stream* s, size_t /*nbytes*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        while (pa_stream_readable_size(s) > 0) {
            const void* data = nullptr;
            size_t      sz   = 0;
            if (pa_stream_peek(s, &data, &sz) < 0) return;
            if (sz == 0) return;
            // A hole: data==nullptr means dropped samples; advance and continue.
            if (! data) {
                pa_stream_drop(s);
                continue;
            }
            constexpr std::uint32_t channels = kDefaultChannels;
            constexpr std::uint32_t stride   = channels * sizeof(float);
            const auto* src = static_cast<const float*>(data);
            const std::uint32_t n_frames = static_cast<std::uint32_t>(sz / stride);
            self->ingest(src, n_frames, channels);
            pa_stream_drop(s);
        }
    }

    void ingest(const float* src, std::uint32_t n_frames, std::uint32_t channels) {
        for (std::uint32_t f = 0; f < n_frames; ++f) {
            float sum = 0.f;
            for (std::uint32_t c = 0; c < channels; ++c) {
                sum += src[f * channels + c];
            }
            const float mono = channels > 0 ? sum / static_cast<float>(channels) : 0.f;
            ring_[ring_head_] = mono;
            ring_head_ = (ring_head_ + 1) % dsp::kFftSize;
            ++samples_since_fft_;
        }

        if (samples_since_fft_ < dsp::kFftSize / 2) return;
        samples_since_fft_ = 0;

        std::array<std::complex<float>, dsp::kFftSize> buf;
        for (std::size_t i = 0; i < dsp::kFftSize; ++i) {
            const std::size_t idx = (ring_head_ + i) % dsp::kFftSize;
            const float w = dsp::hann_window(i, dsp::kFftSize);
            buf[i] = std::complex<float>(ring_[idx] * w, 0.f);
        }

        dsp::fft_inplace(buf.data(), dsp::kFftSize);

        AudioSpectrum raw {};
        const float norm = 2.0f / static_cast<float>(dsp::kFftSize);
        for (std::size_t k = 0; k < dsp::kNumBins; ++k) {
            const std::size_t lo = dsp::kBandEdges[k];
            const std::size_t hi = dsp::kBandEdges[k + 1];
            float sum = 0.f;
            for (std::size_t i = lo; i < hi; ++i) {
                sum += std::abs(buf[i]);
            }
            const float mag = sum / static_cast<float>(hi - lo) * norm;
            raw.bins[k] = std::tanh(mag * 4.0f);
        }

        AudioSpectrum out {};
        for (std::size_t k = 0; k < dsp::kNumBins; ++k) {
            const float prev = smoothed_[k];
            const float cur  = raw.bins[k];
            const float a    = cur > prev ? dsp::kAlphaUp : dsp::kAlphaDown;
            const float v    = a * cur + (1.0f - a) * prev;
            smoothed_[k] = v;
            out.bins[k]  = v;
        }
        out.publish_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();

        seq_.fetch_add(1, std::memory_order_release);
        std::memcpy(&published_, &out, sizeof(AudioSpectrum));
        seq_.fetch_add(1, std::memory_order_release);
    }

    ::pa_threaded_mainloop* loop_   = nullptr;
    ::pa_context*           ctx_    = nullptr;
    ::pa_stream*            stream_ = nullptr;
    std::string             default_sink_;
    bool                    server_info_done_ = false;

    std::array<float, dsp::kFftSize> ring_ {};
    std::size_t                      ring_head_         = 0;
    std::size_t                      samples_since_fft_ = 0;
    std::array<float, dsp::kNumBins> smoothed_ {};

    mutable std::atomic<std::uint32_t> seq_ { 0 };
    AudioSpectrum                       published_ {};
};

AudioCapture::AudioCapture() : impl_(std::make_unique<Impl>()) {}
AudioCapture::~AudioCapture() = default;

bool AudioCapture::init()           { return impl_->init(); }
void AudioCapture::uninit()         { impl_->uninit(); }
bool AudioCapture::is_inited() const { return impl_->is_inited(); }
bool AudioCapture::snapshot(AudioSpectrum& out) const { return impl_->snapshot(out); }

} // namespace wavsen::audio
