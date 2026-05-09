export module wavsen.audio.core;

import rstd.cppstd;
import cubeb;

export namespace wavsen::audio {

// Negotiated stream format — wavsen always asks cubeb for f32 interleaved.
struct DeviceDesc {
    std::uint32_t channels;
    std::uint32_t sample_rate;
};

// Pulled by the audio thread to fill an output buffer. Implementations
// must NOT block (cubeb's audio thread is realtime-ish). `frames` is the
// number of interleaved frames to write into `dst`.
class IPullChannel {
public:
    virtual ~IPullChannel() = default;
    virtual auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t = 0;
    virtual void pass_desc(const DeviceDesc&)                               = 0;
};

class CubebDevice {
public:
    CubebDevice();
    ~CubebDevice();
    CubebDevice(const CubebDevice&)            = delete;
    CubebDevice& operator=(const CubebDevice&) = delete;

    auto init() -> bool;
    void uninit();
    auto is_inited() const -> bool { return ctx_ != nullptr && stream_ != nullptr; }

    void start();
    void stop();

    void mount(std::unique_ptr<IPullChannel>);
    void unmount_all();

    auto volume() const -> float { return volume_.load(std::memory_order_relaxed); }
    auto muted() const -> bool { return muted_.load(std::memory_order_relaxed); }
    void set_volume(float v) { volume_.store(v, std::memory_order_relaxed); }
    void set_muted(bool m) { muted_.store(m, std::memory_order_relaxed); }

    auto desc() const -> DeviceDesc { return desc_; }

    // Frames the audio device has actually played back since the cubeb
    // stream was created. Used by AvPlayer as the master clock for A/V
    // sync. Returns 0 before init() / on failure.
    auto stream_position_frames() const -> std::uint64_t;

private:
    static long data_cb(::cubeb_stream*, void* user, const void* in, void* out, long nframes);
    static void state_cb(::cubeb_stream*, void* user, ::cubeb_state state);

    ::cubeb*        ctx_    = nullptr;
    ::cubeb_stream* stream_ = nullptr;
    DeviceDesc      desc_ {};

    std::mutex                                 channels_mu_;
    std::vector<std::unique_ptr<IPullChannel>> channels_;

    std::atomic<float> volume_ { 1.0f };
    std::atomic<bool>  muted_ { false };
};

} // namespace wavsen::audio
