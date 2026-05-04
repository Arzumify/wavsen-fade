export module wavsen.audio.stream_decoder;

import rstd.cppstd;
import wavsen.audio;        // IByteStream
import wavsen.audio.cubeb;  // DeviceDesc

export namespace wavsen::audio::detail {

// libav*-backed audio decoder + resampler. Reads bytes from `IByteStream`
// (via a custom AVIOContext), decodes via libavformat/libavcodec, and
// resamples through libswresample to the device's negotiated f32 LE
// interleaved format.
class StreamDecoder {
public:
    StreamDecoder();
    ~StreamDecoder();
    StreamDecoder(const StreamDecoder&)            = delete;
    StreamDecoder& operator=(const StreamDecoder&) = delete;
    StreamDecoder(StreamDecoder&&) noexcept;
    StreamDecoder& operator=(StreamDecoder&&) noexcept;

    // Open the source. Returns false on parser/codec error; details logged
    // via rstd::log.
    auto open(std::shared_ptr<IByteStream> src, const DeviceDesc& target) -> bool;

    // Update target descriptor (channels / sample rate). Caller invokes
    // this after the cubeb device negotiates a different format than what
    // was originally requested.
    void retarget(const DeviceDesc& target);

    // Pull `frames` interleaved f32 frames into `dst`. Returns frames
    // actually produced (less than `frames` only on EOF).
    auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wavsen::audio::detail
