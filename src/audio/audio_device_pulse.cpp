module;

#include <pulse/pulseaudio.h>

module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import pulse;
import :core;

namespace wavsen::audio {

namespace {

constexpr std::uint32_t kDefaultRate     = 48000;
constexpr std::uint32_t kDefaultChannels = 2;
constexpr std::uint32_t kQuantum         = 1024;

} // namespace

class AudioDevice::Impl {
public:
    ~Impl() { uninit(); }

    bool init() {
        if (is_inited()) return true;

        loop_ = pa_threaded_mainloop_new();
        if (! loop_) {
            rstd::log::error("wavsen::audio: pa_threaded_mainloop_new failed");
            return false;
        }
        if (pa_threaded_mainloop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: pa_threaded_mainloop_start failed");
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }

        pa_threaded_mainloop_lock(loop_);

        ctx_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "wavsen");
        if (! ctx_) {
            pa_threaded_mainloop_unlock(loop_);
            rstd::log::error("wavsen::audio: pa_context_new failed");
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }
        pa_context_set_state_callback(ctx_, &Impl::on_context_state, this);

        if (pa_context_connect(ctx_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
            rstd::log::error("wavsen::audio: pa_context_connect failed: {}",
                             pa_strerror(pa_context_errno(ctx_)));
            pa_context_unref(ctx_);
            ctx_ = nullptr;
            pa_threaded_mainloop_unlock(loop_);
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }

        for (;;) {
            const auto st = pa_context_get_state(ctx_);
            if (st == PA_CONTEXT_READY) break;
            if (! PA_CONTEXT_IS_GOOD(st)) {
                rstd::log::error("wavsen::audio: pa_context failed: {}",
                                 pa_strerror(pa_context_errno(ctx_)));
                pa_context_disconnect(ctx_);
                pa_context_unref(ctx_);
                ctx_ = nullptr;
                pa_threaded_mainloop_unlock(loop_);
                pa_threaded_mainloop_stop(loop_);
                pa_threaded_mainloop_free(loop_);
                loop_ = nullptr;
                return false;
            }
            pa_threaded_mainloop_wait(loop_);
        }

        pa_sample_spec ss {};
        ss.format   = PA_SAMPLE_FLOAT32LE;
        ss.rate     = kDefaultRate;
        ss.channels = static_cast<std::uint8_t>(kDefaultChannels);

        pa_channel_map cm {};
        pa_channel_map_init_stereo(&cm);

        stream_ = pa_stream_new(ctx_, "wavsen-out", &ss, &cm);
        if (! stream_) {
            rstd::log::error("wavsen::audio: pa_stream_new failed: {}",
                             pa_strerror(pa_context_errno(ctx_)));
            pa_context_disconnect(ctx_);
            pa_context_unref(ctx_);
            ctx_ = nullptr;
            pa_threaded_mainloop_unlock(loop_);
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }
        pa_stream_set_state_callback(stream_, &Impl::on_stream_state, this);
        pa_stream_set_write_callback(stream_, &Impl::on_write, this);

        // Must be set before connect: the write callback can fire as soon as
        // the stream is writable (during the READY wait below). If desc_ is
        // still {0,0} there, on_write bails on stride==0 without ever calling
        // pa_stream_write, and PulseAudio never re-invokes it — the stream
        // stalls at silence for its whole lifetime.
        desc_ = { kDefaultChannels, kDefaultRate };

        const auto frame_bytes = kDefaultChannels * static_cast<std::uint32_t>(sizeof(float));
        pa_buffer_attr ba {};
        ba.maxlength = static_cast<std::uint32_t>(-1);
        ba.tlength   = kQuantum * frame_bytes * 4;
        ba.prebuf    = static_cast<std::uint32_t>(-1);
        ba.minreq    = kQuantum * frame_bytes;
        ba.fragsize  = static_cast<std::uint32_t>(-1);

        const auto flags = static_cast<pa_stream_flags_t>(
            PA_STREAM_ADJUST_LATENCY |
            PA_STREAM_AUTO_TIMING_UPDATE |
            PA_STREAM_INTERPOLATE_TIMING |
            PA_STREAM_START_CORKED);

        if (pa_stream_connect_playback(stream_, nullptr, &ba, flags, nullptr, nullptr) < 0) {
            rstd::log::error("wavsen::audio: pa_stream_connect_playback failed: {}",
                             pa_strerror(pa_context_errno(ctx_)));
            pa_stream_unref(stream_);
            stream_ = nullptr;
            pa_context_disconnect(ctx_);
            pa_context_unref(ctx_);
            ctx_ = nullptr;
            pa_threaded_mainloop_unlock(loop_);
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }

        for (;;) {
            const auto st = pa_stream_get_state(stream_);
            if (st == PA_STREAM_READY) break;
            if (! PA_STREAM_IS_GOOD(st)) {
                rstd::log::error("wavsen::audio: pa_stream failed: {}",
                                 pa_strerror(pa_context_errno(ctx_)));
                pa_stream_unref(stream_);
                stream_ = nullptr;
                pa_context_disconnect(ctx_);
                pa_context_unref(ctx_);
                ctx_ = nullptr;
                pa_threaded_mainloop_unlock(loop_);
                pa_threaded_mainloop_stop(loop_);
                pa_threaded_mainloop_free(loop_);
                loop_ = nullptr;
                return false;
            }
            pa_threaded_mainloop_wait(loop_);
        }

        pa_threaded_mainloop_unlock(loop_);

        {
            std::lock_guard<std::mutex> lk(channels_mu_);
            for (auto& c : channels_) {
                c->pass_desc(desc_);
            }
        }

        rstd::log::info("wavsen::audio: pulse device inited ({} ch @ {} Hz)",
                        desc_.channels, desc_.sample_rate);
        return true;
    }

    void uninit() {
        if (loop_) {
            pa_threaded_mainloop_lock(loop_);
            if (stream_) {
                pa_stream_set_state_callback(stream_, nullptr, nullptr);
                pa_stream_set_write_callback(stream_, nullptr, nullptr);
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
            pa_threaded_mainloop_unlock(loop_);
            pa_threaded_mainloop_stop(loop_);
            pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
        }
    }

    bool is_inited() const { return loop_ != nullptr && stream_ != nullptr; }

    void start() {
        if (! stream_ || ! loop_) return;
        pa_threaded_mainloop_lock(loop_);
        auto* op = pa_stream_cork(stream_, 0, nullptr, nullptr);
        if (op) {} // fire-and-forget; mainloop frees it via callback=nullptr
        pa_threaded_mainloop_unlock(loop_);
    }

    void stop() {
        if (! stream_ || ! loop_) return;
        pa_threaded_mainloop_lock(loop_);
        auto* op = pa_stream_cork(stream_, 1, nullptr, nullptr);
        if (op) {}
        pa_threaded_mainloop_unlock(loop_);
    }

    void mount(std::unique_ptr<IPullChannel> ch) {
        if (is_inited()) {
            ch->pass_desc(desc_);
        }
        std::lock_guard<std::mutex> lk(channels_mu_);
        channels_.push_back(std::move(ch));
    }

    void unmount_all() {
        std::lock_guard<std::mutex> lk(channels_mu_);
        channels_.clear();
    }

    DeviceDesc desc() const { return desc_; }

    float volume() const { return volume_.load(std::memory_order_relaxed); }
    bool  muted()  const { return muted_.load(std::memory_order_relaxed); }
    void  set_volume(float v) { volume_.store(v, std::memory_order_relaxed); }
    void  set_muted(bool m)   { muted_.store(m,  std::memory_order_relaxed); }

    std::uint64_t stream_position_frames() const {
        if (! stream_ || ! loop_) return 0;
        // pa_stream_get_time is safe from any thread once the stream is
        // READY: it reads the locally-cached timing snapshot updated by
        // PA_STREAM_AUTO_TIMING_UPDATE + INTERPOLATE_TIMING. Avoid taking
        // the mainloop lock from the audio callback (it's held there).
        pa_usec_t usec = 0;
        if (pa_stream_get_time(stream_, &usec) < 0) return 0;
        const auto sr = desc_.sample_rate ? desc_.sample_rate : kDefaultRate;
        return static_cast<std::uint64_t>(usec) * sr / 1'000'000ULL;
    }

private:
    static void on_context_state(::pa_context* /*c*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_stream_state(::pa_stream* /*s*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_write(::pa_stream* s, size_t nbytes, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (nbytes == 0) return;

        const auto channels = self->desc_.channels;
        const auto stride   = channels * static_cast<std::uint32_t>(sizeof(float));
        if (stride == 0) return;

        size_t remaining = nbytes;
        while (remaining > 0) {
            void*  buf  = nullptr;
            size_t want = remaining;
            if (pa_stream_begin_write(s, &buf, &want) < 0 || ! buf || want == 0) {
                return;
            }

            const auto n_frames = static_cast<std::uint32_t>(want / stride);
            const auto total_samples = static_cast<std::size_t>(n_frames) * channels;
            auto* out_f = static_cast<float*>(buf);
            std::memset(out_f, 0, total_samples * sizeof(float));

            if (! self->muted_.load(std::memory_order_relaxed)) {
                const float gain = self->volume_.load(std::memory_order_relaxed);

                std::vector<float> scratch(total_samples);

                std::lock_guard<std::mutex> lk(self->channels_mu_);
                for (auto& ch : self->channels_) {
                    std::memset(scratch.data(), 0, total_samples * sizeof(float));
                    const auto produced = ch->next_pcm(scratch.data(), n_frames);
                    const auto produced_samples = static_cast<std::size_t>(produced) * channels;
                    for (std::size_t i = 0; i < produced_samples; ++i) {
                        out_f[i] += gain * scratch[i];
                    }
                }
            }

            const size_t written = static_cast<size_t>(n_frames) * stride;
            if (pa_stream_write(s, buf, written, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
                return;
            }
            if (written == 0) return;
            remaining = remaining > written ? remaining - written : 0;
        }
    }

    ::pa_threaded_mainloop* loop_   = nullptr;
    ::pa_context*           ctx_    = nullptr;
    ::pa_stream*            stream_ = nullptr;
    DeviceDesc              desc_ {};

    std::mutex                                 channels_mu_;
    std::vector<std::unique_ptr<IPullChannel>> channels_;

    std::atomic<float> volume_ { 1.0f };
    std::atomic<bool>  muted_ { false };
};

AudioDevice::AudioDevice() : impl_(std::make_unique<Impl>()) {}
AudioDevice::~AudioDevice() = default;

bool AudioDevice::init()                  { return impl_->init(); }
void AudioDevice::uninit()                { impl_->uninit(); }
bool AudioDevice::is_inited() const       { return impl_->is_inited(); }
void AudioDevice::start()                 { impl_->start(); }
void AudioDevice::stop()                  { impl_->stop(); }
void AudioDevice::mount(std::unique_ptr<IPullChannel> ch) { impl_->mount(std::move(ch)); }
void AudioDevice::unmount_all()           { impl_->unmount_all(); }
float AudioDevice::volume() const         { return impl_->volume(); }
bool  AudioDevice::muted()  const         { return impl_->muted(); }
void  AudioDevice::set_volume(float v)    { impl_->set_volume(v); }
void  AudioDevice::set_muted(bool m)      { impl_->set_muted(m); }
DeviceDesc AudioDevice::desc() const      { return impl_->desc(); }
std::uint64_t AudioDevice::stream_position_frames() const {
    return impl_->stream_position_frames();
}

} // namespace wavsen::audio
