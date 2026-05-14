export module wavsen.audio:capture;

import rstd.cppstd;
import pipewire;

export namespace wavsen::audio {

// 16-bin log-spaced magnitude spectrum, EMA-smoothed. Roughly 0..1 with
// peaks slightly above for loud transients. Layout/size matches owe's
// `FrameInputs::audio_average`.
struct AudioSpectrum {
    std::array<float, 16> bins {};
};

// Taps the default sink's monitor via a PipeWire INPUT stream with
// PW_KEY_STREAM_CAPTURE_SINK=true, runs a 1024-point Hann-windowed FFT
// on the RT thread, log-bins into 16 bands, EMA-smooths, and publishes a
// lock-free snapshot for renderers to read once per frame.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&)            = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    auto init() -> bool;
    void uninit();
    auto is_inited() const -> bool {
        return loop_ != nullptr && stream_ != nullptr;
    }

    // Lock-free read. Returns true if at least one capture buffer has
    // been processed; out is zero-filled until then.
    bool snapshot(AudioSpectrum& out) const;

private:
    static void on_process(void* user);
    static void on_state_changed(void* user, ::pw_stream_state old,
                                 ::pw_stream_state state, const char* error);

    ::pw_thread_loop* loop_   = nullptr;
    ::pw_stream*      stream_ = nullptr;
    ::spa_hook        stream_listener_ {};

    // Ring of f32 mono samples, sized to the FFT window.
    static constexpr std::size_t kFftSize  = 1024;
    static constexpr std::size_t kNumBins  = 16;

    std::array<float, kFftSize> ring_ {};
    std::size_t                 ring_head_     = 0;
    std::size_t                 samples_since_fft_ = 0;

    // EMA state lives on the audio thread only.
    std::array<float, kNumBins> smoothed_ {};

    // Seqlock snapshot. seq starts at 0 (never primed); each completed
    // write increments by 2 (writer briefly sets odd while copying).
    mutable std::atomic<std::uint32_t> seq_ { 0 };
    AudioSpectrum                       published_ {};
};

} // namespace wavsen::audio
