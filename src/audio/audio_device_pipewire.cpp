module;

#include <optional>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import pipewire;
import :core;

namespace wavsen::audio {

namespace {

std::once_flag g_pw_init_once;
void ensure_pw_init() {
    std::call_once(g_pw_init_once, [] { pw_init(nullptr, nullptr); });
}

constexpr std::uint32_t kDefaultRate     = 48000;
constexpr std::uint32_t kDefaultChannels = 2;
constexpr std::uint32_t kQuantum         = 1024;

} // namespace

class AudioDevice::Impl {
public:
    ~Impl() { uninit(); }

    bool init() {
        if (is_inited()) return true;

        ensure_pw_init();

        loop_ = pw_thread_loop_new("wavsen-audio", nullptr);
        if (! loop_) {
            rstd::log::error("wavsen::audio: pw_thread_loop_new failed");
            return false;
        }

        if (pw_thread_loop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: pw_thread_loop_start failed");
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }

        static const ::pw_stream_events stream_events = {
            .version       = PW_VERSION_STREAM_EVENTS,
            .destroy       = nullptr,
            .state_changed = &Impl::on_state_changed,
            .control_info  = nullptr,
            .io_changed    = nullptr,
            .param_changed = nullptr,
            .add_buffer    = nullptr,
            .remove_buffer = nullptr,
            .process       = &Impl::on_process,
            .drained       = nullptr,
            .command       = nullptr,
            .trigger_done  = nullptr,
        };

        pw_thread_loop_lock(loop_);

        auto* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE,     "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE,     "Music",
            PW_KEY_APP_NAME,       "wavsen",
            PW_KEY_NODE_NAME,      "wavsen-out",
            PW_KEY_NODE_DESCRIPTION, "wavsen audio output",
            nullptr);
        pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u", kQuantum, kDefaultRate);
        pw_properties_setf(props, PW_KEY_NODE_RATE,    "1/%u",  kDefaultRate);

        stream_ = pw_stream_new_simple(
            pw_thread_loop_get_loop(loop_),
            "wavsen-out",
            props,
            &stream_events,
            this);
        if (! stream_) {
            pw_thread_loop_unlock(loop_);
            rstd::log::error("wavsen::audio: pw_stream_new_simple failed");
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

        if (pw_stream_connect(stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags,
                              params, 1) < 0)
        {
            rstd::log::error("wavsen::audio: pw_stream_connect failed");
            pw_stream_destroy(stream_);
            stream_ = nullptr;
            pw_thread_loop_unlock(loop_);
            pw_thread_loop_stop(loop_);
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }

        pw_thread_loop_unlock(loop_);

        desc_ = { kDefaultChannels, kDefaultRate };

        {
            std::lock_guard<std::mutex> lk(channels_mu_);
            for (auto& c : channels_) {
                c->pass_desc(desc_);
            }
        }

        rstd::log::info("wavsen::audio: pipewire device inited ({} ch @ {} Hz)",
                        desc_.channels, desc_.sample_rate);
        return true;
    }

    void uninit() {
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

    bool is_inited() const { return loop_ != nullptr && stream_ != nullptr; }

    void start() {
        if (! stream_) return;
        pw_thread_loop_lock(loop_);
        pw_stream_set_active(stream_, true);
        pw_thread_loop_unlock(loop_);
    }

    void stop() {
        if (! stream_) return;
        pw_thread_loop_lock(loop_);
        pw_stream_set_active(stream_, false);
        pw_thread_loop_unlock(loop_);
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
    void  start_fade_in(uint32_t fade_ms)
    {
        std::lock_guard<std::shared_mutex> lk(fade_info_mu_);
        fade_info_.state = FadeIn;
        fade_info_.start = std::chrono::steady_clock::now();
        fade_info_.time = std::chrono::milliseconds(fade_ms);
        fading_.store(true, std::memory_order_relaxed);
    }
    void  start_fade_out(uint32_t fade_ms)
    {
        std::lock_guard<std::shared_mutex> lk(fade_info_mu_);
        fade_info_.state = FadeOut;
        fade_info_.start = std::chrono::steady_clock::now();
        fade_info_.time = std::chrono::milliseconds(fade_ms);
        fading_.store(true, std::memory_order_relaxed);
    }

    std::uint64_t stream_position_frames() const {
        if (! stream_) return 0;
        pw_time t {};
        if (pw_stream_get_time_n(stream_, &t, sizeof(t)) < 0) return 0;
        if (t.ticks <= static_cast<std::uint64_t>(t.delay)) return 0;
        return t.ticks - static_cast<std::uint64_t>(t.delay);
    }

private:
    static void on_process(void* user) {
        auto* self = static_cast<Impl*>(user);
        if (! self->stream_) return;

        pw_buffer* b = pw_stream_dequeue_buffer(self->stream_);
        if (! b) return;

        auto* sb = b->buffer;
        if (! sb || sb->n_datas == 0 || ! sb->datas[0].data) {
            pw_stream_queue_buffer(self->stream_, b);
            return;
        }

        const auto channels = self->desc_.channels;
        const auto stride   = channels * static_cast<std::uint32_t>(sizeof(float));
        auto*      out_f    = static_cast<float*>(sb->datas[0].data);

        std::uint32_t n_frames = sb->datas[0].maxsize / stride;
        if (b->requested != 0) {
            const auto req = static_cast<std::uint32_t>(b->requested);
            if (req < n_frames) n_frames = req;
        }

        const auto total_samples = static_cast<std::size_t>(n_frames) * channels;
        std::memset(out_f, 0, total_samples * sizeof(float));

        if (! self->muted_.load(std::memory_order_relaxed)) {
            const float gain = self->volume_.load(std::memory_order_relaxed);
            if (self->fading_.load(std::memory_order_relaxed)) {
                std::shared_lock<std::shared_mutex> = lk(self->fade_info_mu_);
                std::chrono::milliseconds difference = std::chrono::steady_clock::now() - self->fade_info_.start;
                float passed = static_cast<float>(difference.count()) / static_cast<float>(self->fade_info_.time.count());
                if (self->fade_info_.state == FadeIn) {
                    gain *= passed;
                } else {
                    gain *= 1.0 - passed;
                }
            }

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

        sb->datas[0].chunk->offset = 0;
        sb->datas[0].chunk->stride = static_cast<std::int32_t>(stride);
        sb->datas[0].chunk->size   = n_frames * stride;

        pw_stream_queue_buffer(self->stream_, b);
    }

    static void on_state_changed(void* /*user*/, ::pw_stream_state /*old*/,
                                 ::pw_stream_state state, const char* error) {
        switch (state) {
        case PW_STREAM_STATE_ERROR:
            rstd::log::error("wavsen::audio: stream ERROR{}",
                             error ? std::string(": ") + error : std::string{});
            break;
        case PW_STREAM_STATE_UNCONNECTED:
            rstd::log::debug("wavsen::audio: stream UNCONNECTED");
            break;
        case PW_STREAM_STATE_CONNECTING:
            rstd::log::debug("wavsen::audio: stream CONNECTING");
            break;
        case PW_STREAM_STATE_PAUSED:
            rstd::log::debug("wavsen::audio: stream PAUSED");
            break;
        case PW_STREAM_STATE_STREAMING:
            rstd::log::debug("wavsen::audio: stream STREAMING");
            break;
        }
    }

    ::pw_thread_loop* loop_   = nullptr;
    ::pw_stream*      stream_ = nullptr;
    DeviceDesc        desc_ {};

    std::mutex                                 channels_mu_;
    std::vector<std::unique_ptr<IPullChannel>> channels_;

    std::atomic<float> volume_ { 1.0f };
    std::atomic<bool>  muted_ { false };
    std::shared_mutex fade_info_mu_;
    FadeInfo fade_info_;
    std::atomic<bool> fading_ { false };
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
void AudioDevice::start_fade_in(uint32_t fade_ms) { impl_->start_fade_in(fade_ms); }
void AudioDevice::start_fade_out(uint32_t fade_ms) { impl_->start_fade_out(fade_ms); }

DeviceDesc AudioDevice::desc() const      { return impl_->desc(); }
std::uint64_t AudioDevice::stream_position_frames() const {
    return impl_->stream_position_frames();
}

} // namespace wavsen::audio
