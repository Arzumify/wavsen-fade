module wavsen.audio.core;

import rstd.cppstd;
import rstd;
import rstd.log;
import cubeb;

namespace wavsen::audio {

CubebDevice::CubebDevice() = default;

CubebDevice::~CubebDevice() { uninit(); }

bool CubebDevice::init() {
    if (is_inited()) return true;

    if (cubeb_init(&ctx_, "wavsen", nullptr) != CUBEB_OK) {
        rstd::log::error("wavsen::audio: cubeb_init failed");
        ctx_ = nullptr;
        return false;
    }

    // Pick output device parameters cubeb's preferred output supports.
    std::uint32_t rate     = 48000;
    if (cubeb_get_preferred_sample_rate(ctx_, &rate) != CUBEB_OK || rate == 0) {
        rate = 48000;
    }
    std::uint32_t channels = 2;

    cubeb_stream_params params {};
    params.format   = CUBEB_SAMPLE_FLOAT32LE;
    params.rate     = rate;
    params.channels = channels;
    params.layout   = (channels == 2) ? CUBEB_LAYOUT_STEREO : CUBEB_LAYOUT_MONO;
    params.prefs    = CUBEB_STREAM_PREF_NONE;

    std::uint32_t latency_frames = 4096;
    cubeb_get_min_latency(ctx_, &params, &latency_frames);

    if (cubeb_stream_init(ctx_, &stream_, "wavsen-out",
                          /*input_device=*/nullptr,  /*input_params=*/nullptr,
                          /*output_device=*/nullptr, &params,
                          latency_frames,
                          &CubebDevice::data_cb, &CubebDevice::state_cb,
                          this) != CUBEB_OK)
    {
        rstd::log::error("wavsen::audio: cubeb_stream_init failed");
        cubeb_destroy(ctx_);
        ctx_    = nullptr;
        stream_ = nullptr;
        return false;
    }

    desc_ = { channels, rate };

    // Re-broadcast the device desc to mounted streams.
    {
        std::lock_guard<std::mutex> lk(channels_mu_);
        for (auto& c : channels_) {
            c->pass_desc(desc_);
        }
    }

    rstd::log::info("wavsen::audio: cubeb device inited ({} ch @ {} Hz)",
                    desc_.channels, desc_.sample_rate);
    return true;
}

void CubebDevice::uninit() {
    if (stream_) {
        cubeb_stream_stop(stream_);
        cubeb_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (ctx_) {
        cubeb_destroy(ctx_);
        ctx_ = nullptr;
    }
}

void CubebDevice::start() {
    if (stream_) cubeb_stream_start(stream_);
}

void CubebDevice::stop() {
    if (stream_) cubeb_stream_stop(stream_);
}

void CubebDevice::mount(std::unique_ptr<IPullChannel> ch) {
    if (is_inited()) {
        ch->pass_desc(desc_);
    }
    std::lock_guard<std::mutex> lk(channels_mu_);
    channels_.push_back(std::move(ch));
}

void CubebDevice::unmount_all() {
    std::lock_guard<std::mutex> lk(channels_mu_);
    channels_.clear();
}

std::uint64_t CubebDevice::stream_position_frames() const {
    if (!stream_) return 0;
    std::uint64_t pos = 0;
    if (cubeb_stream_get_position(stream_, &pos) != CUBEB_OK) {
        return 0;
    }
    return pos;
}

long CubebDevice::data_cb(::cubeb_stream*, void* user, const void* /*in*/,
                          void* output_buffer, long nframes) {
    auto* self = static_cast<CubebDevice*>(user);

    auto*       out_f      = static_cast<float*>(output_buffer);
    const auto  channels   = self->desc_.channels;
    const auto  total_samples = static_cast<std::size_t>(nframes) * channels;
    std::memset(out_f, 0, total_samples * sizeof(float));

    if (self->muted_.load(std::memory_order_relaxed)) {
        return nframes;
    }

    const float gain = self->volume_.load(std::memory_order_relaxed);

    // Per-stream scratch — float interleaved, sized per call (rare alloc;
    // realtime audio thread should ideally avoid heap. Iter 0.1 prioritizes
    // correctness over zero-alloc; tighten this in 0.2.)
    std::vector<float> scratch(total_samples);

    std::lock_guard<std::mutex> lk(self->channels_mu_);
    for (auto& ch : self->channels_) {
        std::memset(scratch.data(), 0, total_samples * sizeof(float));
        const auto produced = ch->next_pcm(scratch.data(),
                                           static_cast<std::uint32_t>(nframes));
        const auto produced_samples = static_cast<std::size_t>(produced) * channels;
        for (std::size_t i = 0; i < produced_samples; ++i) {
            out_f[i] += gain * scratch[i];
        }
    }

    return nframes;
}

void CubebDevice::state_cb(::cubeb_stream*, void* /*user*/, ::cubeb_state state) {
    switch (state) {
    case CUBEB_STATE_STARTED: rstd::log::debug("wavsen::audio: stream STARTED"); break;
    case CUBEB_STATE_STOPPED: rstd::log::debug("wavsen::audio: stream STOPPED"); break;
    case CUBEB_STATE_DRAINED: rstd::log::debug("wavsen::audio: stream DRAINED"); break;
    case CUBEB_STATE_ERROR:   rstd::log::error("wavsen::audio: stream ERROR"); break;
    }
}

} // namespace wavsen::audio
