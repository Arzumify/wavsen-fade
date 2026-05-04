export module wavsen.video.video_decoder;

import rstd.cppstd;
import rstd;
import vulkan;
import wavsen.video.vk_device;        // Error, Producer

export namespace wavsen::video {

struct Nv12Frame {
    // Layout: Y plane (`width * height` bytes) directly followed by
    // interleaved UV plane (`width * height / 2` bytes). Total size is
    // therefore `width * height * 3 / 2`.
    std::vector<std::uint8_t> data;
    std::uint32_t             width  { 0 };
    std::uint32_t             height { 0 };
    // Stream-time PTS in seconds; -1.0 if unavailable.
    double                    pts_seconds { -1.0 };
    // Source colorspace / range — caller feeds these into the YuvToRgba
    // colour matrix builder. Defaults to BT.709 limited range when the
    // stream doesn't tag them.
    std::uint32_t             colorspace { 0 };
    std::uint32_t             color_range { 0 };
};

// Probe result for VideoDecoder::probe_native.
struct ProbeResult {
    std::uint32_t width;
    std::uint32_t height;
};

// Outcome of a successful frame pull. `Error` is reserved for the Err
// arm of Result; clean stream end is Eof in the Ok arm.
enum class NextFrame {
    Ok,
    Eof,
};

// View onto the AVVkFrame yielded by `next_vk_frame` — one entry per
// plane. Pointers alias the underlying AVVkFrame; valid until the next
// call to `next_vk_frame`.
struct VkFrameView {
    VkImage*       img;
    VkImageLayout* layout;
    VkSemaphore*   sem;
    std::uint64_t* sem_value;
    std::uint32_t* queue_family;
    std::uint32_t  plane_count;
    std::uint32_t  width;
    std::uint32_t  height;
    double         pts_seconds;
    std::uint32_t  colorspace  { 0 };
    std::uint32_t  color_range { 0 };
    std::uint32_t  bit_depth   { 8 };
};

class VideoDecoder {
public:
    // Read the native video resolution from the file's first video
    // stream without committing to a decoder.
    static auto probe_native(const std::string& path) -> rstd::Result<ProbeResult, Error>;

    // `target_w`/`target_h` are the wallpaper extent. Both are rounded
    // up to even pixel boundaries (NV12 chroma is 4:2:0). Setting
    // `loop=true` causes EOF to seek back to the start automatically.
    static auto open(const std::string& path,
                     std::uint32_t target_w, std::uint32_t target_h, bool loop)
        -> rstd::Result<std::unique_ptr<VideoDecoder>, Error>;

    // Shared-device variant: bring up AV_HWDEVICE_TYPE_VULKAN on top of
    // the Producer's VkInstance/VkDevice. On success, decoded frames stay
    // GPU-resident; the caller uses `next_vk_frame()`. On any setup
    // failure the decoder falls back transparently to sw decode and
    // `using_vk_frames()` returns false.
    static auto open_with_vk(const std::string& path,
                             std::uint32_t target_w, std::uint32_t target_h, bool loop,
                             const Producer& vk)
        -> rstd::Result<std::unique_ptr<VideoDecoder>, Error>;

    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    auto next_frame(Nv12Frame& out) -> rstd::Result<NextFrame, Error>;

    bool using_vk_frames() const { return using_vk_frames_; }

    auto next_vk_frame(VkFrameView& out) -> rstd::Result<NextFrame, Error>;

    std::uint32_t width() const  { return target_w_; }
    std::uint32_t height() const { return target_h_; }
    void          set_loop(bool loop) { loop_ = loop; }

    struct State;

private:
    VideoDecoder() = default;

    // Internal builder kept in the legacy out-param style to minimize
    // code churn — `pre_built_hwdev` is AVBufferRef* type-erased to void*.
    static std::unique_ptr<VideoDecoder>
    build_internal(const std::string& path,
                   std::uint32_t target_w, std::uint32_t target_h,
                   bool loop, void* pre_built_hwdev,
                   Error* err);

    // Internal frame-pull helpers using the legacy in/out style. The
    // returned int encodes: 0 = ok, 1 = eof, -1 = error.
    int next_frame_(Nv12Frame& out, Error* err);
    int next_vk_frame_(VkFrameView& out, Error* err);

    std::unique_ptr<State> st_;
    std::uint32_t target_w_ { 0 };
    std::uint32_t target_h_ { 0 };
    bool          loop_     { false };
    bool          using_vk_frames_ { false };
};

} // namespace wavsen::video
