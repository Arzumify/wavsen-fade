export module wavsen.audio:capture;

import rstd.cppstd;

export namespace wavsen::audio {

// 64-bin log-spaced magnitude spectrum, EMA-smoothed. Roughly 0..1 with
// peaks slightly above for loud transients. Layout/size matches owe's
// `FrameInputs::audio_average`. `publish_ms` is a steady_clock timestamp
// (ms since epoch) of the last RT-side update, used by readers to detect
// stale snapshots (backend stopped delivering, sink disconnected, ...).
// Zero means "never primed".
struct AudioSpectrum {
    std::array<float, 64> bins {};
    std::int64_t publish_ms { 0 };
};

// Taps the system default sink's monitor source, runs a 1024-point
// Hann-windowed FFT on the audio thread, log-bins into 64 bands,
// EMA-smooths, and publishes a lock-free snapshot for renderers.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&)            = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    auto init() -> bool;
    void uninit();
    auto is_inited() const -> bool;

    // Lock-free read. Returns true if at least one capture buffer has
    // been processed; out is zero-filled until then.
    bool snapshot(AudioSpectrum& out) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wavsen::audio
