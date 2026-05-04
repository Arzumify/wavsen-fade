module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import wavsen.audio.cubeb;
import wavsen.audio.stream_decoder;

namespace wavsen::audio {

namespace {

// SoundStream backed by libav* decoder. Created via make_stream(); the
// CubebDevice pulls PCM through next_pcm in the audio thread.
class DecoderStream : public SoundStream {
public:
    explicit DecoderStream(detail::StreamDecoder dec)
        : dec_(std::move(dec)) {}

    auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t override {
        return dec_.next_pcm(dst, frames);
    }
    void pass_desc(const Desc& d) override {
        dec_.retarget({ d.channels, d.sample_rate });
    }

private:
    detail::StreamDecoder dec_;
};

// Adapter exposing a SoundStream to CubebDevice's IPullChannel interface.
class StreamPullChannel : public detail::IPullChannel {
public:
    explicit StreamPullChannel(std::unique_ptr<SoundStream> ss)
        : ss_(std::move(ss)) {}

    auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t override {
        return ss_->next_pcm(dst, frames);
    }
    void pass_desc(const detail::DeviceDesc& d) override {
        ss_->pass_desc({ d.channels, d.sample_rate });
    }

private:
    std::unique_ptr<SoundStream> ss_;
};

} // namespace

auto make_stream(std::shared_ptr<IByteStream> source, const SoundStream::Desc& desc)
    -> std::unique_ptr<SoundStream> {
    detail::StreamDecoder dec;
    if (!dec.open(std::move(source), { desc.channels, desc.sample_rate })) {
        return nullptr;
    }
    return std::make_unique<DecoderStream>(std::move(dec));
}

class SoundManager::Impl {
public:
    detail::CubebDevice device;
};

SoundManager::SoundManager() : impl_(std::make_unique<Impl>()) {}
SoundManager::~SoundManager() = default;

void SoundManager::mount(std::unique_ptr<SoundStream> ss) {
    if (!ss) return;
    impl_->device.mount(std::make_unique<StreamPullChannel>(std::move(ss)));
}

void SoundManager::unmount_all() { impl_->device.unmount_all(); }

bool SoundManager::init() {
    if (muted()) {
        rstd::log::info("wavsen::audio: muted, not initializing device");
        return false;
    }
    return impl_->device.init();
}

bool SoundManager::is_inited() const { return impl_->device.is_inited(); }

void SoundManager::play()  { impl_->device.start(); }
void SoundManager::pause() { impl_->device.stop(); }

float SoundManager::volume() const     { return impl_->device.volume(); }
bool  SoundManager::muted() const      { return impl_->device.muted(); }
void  SoundManager::set_volume(float v) { impl_->device.set_volume(v); }

void SoundManager::set_muted(bool m) {
    impl_->device.set_muted(m);
    if (!m) {
        // re-init the device if previously muted-uninited
        impl_->device.init();
    } else {
        impl_->device.uninit();
    }
}

} // namespace wavsen::audio
