module wavsen.video.video_decoder;

import rstd.cppstd;
import rstd;
import rstd.log;
import vulkan;
import wavsen.video.vk_device;
import avutil;
import avcodec;
import avformat;
import swscale;

namespace wavsen::video {

namespace {

struct FmtCtxDeleter {
    void operator()(AVFormatContext* p) const noexcept {
        if (p) avformat_close_input(&p);
    }
};
struct CodecCtxDeleter {
    void operator()(AVCodecContext* p) const noexcept {
        if (p) avcodec_free_context(&p);
    }
};
struct FrameDeleter {
    void operator()(AVFrame* p) const noexcept {
        if (p) av_frame_free(&p);
    }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const noexcept {
        if (p) av_packet_free(&p);
    }
};
struct SwsDeleter {
    void operator()(SwsContext* p) const noexcept {
        if (p) sws_freeContext(p);
    }
};
struct BufRefDeleter {
    void operator()(AVBufferRef* p) const noexcept {
        if (p) av_buffer_unref(&p);
    }
};

using FmtCtxPtr   = std::unique_ptr<AVFormatContext, FmtCtxDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext, CodecCtxDeleter>;
using FramePtr    = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr   = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsPtr      = std::unique_ptr<SwsContext, SwsDeleter>;
using BufRefPtr   = std::unique_ptr<AVBufferRef, BufRefDeleter>;

/* Defined further down — forward-declared so the helpers above the
 * definitions can use them. */
bool fail(Error* err, std::string m);
std::string av_err_str(int rc);

/* Translate FFmpeg's colorspace/range enums into our ColorSpace /
 * ColorRange ints (which the public Nv12Frame / VkFrameView carry).
 * Unknowns default to BT.709 limited — the most common case. */
uint32_t map_colorspace(int cs) {
    switch (cs) {
    case AVCOL_SPC_BT709:        return 0;
    case AVCOL_SPC_BT470BG:      // PAL / BT.601 625
    case AVCOL_SPC_SMPTE170M:    return 1;
    case AVCOL_SPC_BT2020_NCL:   return 2;
    case AVCOL_SPC_BT2020_CL:    return 2;
    default:                     return 0;
    }
}
uint32_t map_range(int r) {
    return (r == AVCOL_RANGE_JPEG) ? 1u : 0u;
}

/* `get_format` callback: prefer AV_PIX_FMT_VULKAN whenever the codec
 * offers it; fall back to whatever FFmpeg picks by default otherwise.
 *
 * Do NOT pre-allocate cctx->hw_frames_ctx here. FFmpeg's
 * ff_decode_get_hw_frames_ctx short-circuits when hw_frames_ctx is
 * already set, which skips the vulkan hwaccel's frame_params callback
 * — and that callback is what bootstraps FFVulkanDecodeContext::
 * shared_ctx. Skipping it makes ff_vk_decode_init dereference a NULL
 * shared_ctx (crash in ff_vk_init via &NULL->s == 0x0). Letting
 * FFmpeg own the hw_frames_ctx means we accept whatever VkFormat it
 * picks (typically the multi-plane VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
 * on AMD/RADV); the sw download path in next_frame doesn't care. */
AVPixelFormat get_format_prefer_vulkan(AVCodecContext* cctx,
                                       const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VULKAN) return AV_PIX_FMT_VULKAN;
    }
    return avcodec_default_get_format(cctx, fmts);
}

#if defined(WAVSEN_HAS_VAAPI)
/* VAAPI counterpart: prefer AV_PIX_FMT_VAAPI when offered. The codec
 * bootstraps an internal AVHWFramesContext from the AVHWDeviceContext
 * we attached to cctx->hw_device_ctx, so we don't pre-allocate frames
 * either. */
AVPixelFormat get_format_prefer_vaapi(AVCodecContext* cctx,
                                      const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VAAPI) return AV_PIX_FMT_VAAPI;
    }
    return avcodec_default_get_format(cctx, fmts);
}

/* Best-effort AV_HWDEVICE_TYPE_VAAPI context. FFmpeg owns the libva
 * VADisplay; we just hand it a render-node path (or NULL for default).
 * Returns NULL on any failure with *err populated. */
AVBufferRef* make_vaapi_hwdevice(const std::string& render_node, Error* err) {
    AVBufferRef* hwd = nullptr;
    const char*  dev = render_node.empty() ? nullptr : render_node.c_str();
    int rc = av_hwdevice_ctx_create(&hwd, AV_HWDEVICE_TYPE_VAAPI, dev, nullptr, 0);
    if (rc < 0 || !hwd) {
        fail(err, "av_hwdevice_ctx_create(VAAPI" +
                  (render_node.empty() ? std::string{} : ", " + render_node) +
                  "): " + av_err_str(rc));
        if (hwd) av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}
#endif

/* Build an AV_HWDEVICE_TYPE_VULKAN context wrapping the caller's
 * Producer-owned VkInstance/VkDevice. Returns a populated AVBufferRef
 * on success, or null + populated *err on any failure. */
AVBufferRef* make_shared_vulkan_hwdevice(const Producer& vk, Error* err) {
    AVBufferRef* hwd = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (!hwd) {
        fail(err, "av_hwdevice_ctx_alloc(VULKAN) failed");
        return nullptr;
    }
    auto* dctx = reinterpret_cast<AVHWDeviceContext*>(hwd->data);
    auto* vctx = reinterpret_cast<AVVulkanDeviceContext*>(dctx->hwctx);

    vctx->get_proc_addr = vkGetInstanceProcAddr;
    vctx->inst          = vk.instance();
    vctx->phys_dev      = vk.physical_device();
    vctx->act_dev       = vk.device();

    const auto& iexts = vk.enabled_instance_extensions();
    const auto& dexts = vk.enabled_device_extensions();
    vctx->enabled_inst_extensions    = iexts.empty() ? nullptr : iexts.data();
    vctx->nb_enabled_inst_extensions = static_cast<int>(iexts.size());
    vctx->enabled_dev_extensions     = dexts.empty() ? nullptr : dexts.data();
    vctx->nb_enabled_dev_extensions  = static_cast<int>(dexts.size());

    const auto& qfs = vk.queue_families();
    vctx->nb_qf = 0;
    for (const auto& q : qfs) {
        if (vctx->nb_qf >= static_cast<int>(sizeof(vctx->qf) / sizeof(vctx->qf[0])))
            break;
        AVVulkanDeviceQueueFamily entry {};
        entry.idx        = static_cast<int>(q.index);
        entry.num        = 1;
        entry.flags      = static_cast<VkQueueFlagBits>(q.flags);
        entry.video_caps = static_cast<VkVideoCodecOperationFlagBitsKHR>(q.video_caps);
        vctx->qf[vctx->nb_qf++] = entry;
    }

    if (int rc = av_hwdevice_ctx_init(hwd); rc < 0) {
        fail(err, "av_hwdevice_ctx_init(shared VULKAN): " + av_err_str(rc));
        av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}

bool fail(Error* err, std::string m) {
    if (err) err->message = std::move(m);
    return false;
}

std::string av_err_str(int rc) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace

struct VideoDecoder::State {
    FmtCtxPtr     fmt;
    CodecCtxPtr   cctx;
    PacketPtr     pkt;
    FramePtr      src_frame;
    /* Sw landing frame for vulkan→sw downloads via
     * av_hwframe_transfer_data. Allocated lazily on first hw frame. */
    FramePtr      sw_frame;
    /* DRM_PRIME mapping target frame; allocated lazily for the VAAPI
     * zero-copy path. Holds the dup'd dma-buf fds via av_hwframe_map. */
    FramePtr      drm_frame;
    SwsPtr        sws;
    /* Hwdevice context owned by the codec when present. Best-effort: a
     * NULL `hwd` here just means we run sw decode. */
    BufRefPtr     hwd;
    AVPixelFormat sws_src_fmt { AV_PIX_FMT_NONE };
    int           sws_src_w   { 0 };
    int           sws_src_h   { 0 };
    int           video_idx   { -1 };
    AVRational    stream_tb   { 0, 1 };
    bool          flushing    { false };
};

namespace {

bool ensure_sws(VideoDecoder::State& st, int src_w, int src_h, AVPixelFormat src_fmt,
                uint32_t target_w, uint32_t target_h) {
    if (st.sws && st.sws_src_w == src_w && st.sws_src_h == src_h
        && st.sws_src_fmt == src_fmt) {
        return true;
    }
    /* Always emit NV12 — that's what YuvToRgba consumes. */
    st.sws.reset(sws_getContext(src_w, src_h, src_fmt,
                                static_cast<int>(target_w),
                                static_cast<int>(target_h),
                                AV_PIX_FMT_NV12,
                                SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!st.sws) return false;
    st.sws_src_w = src_w;
    st.sws_src_h = src_h;
    st.sws_src_fmt = src_fmt;
    return true;
}

bool seek_to_start(VideoDecoder::State& st) {
    int rc = av_seek_frame(st.fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) return false;
    avcodec_flush_buffers(st.cctx.get());
    st.flushing = false;
    return true;
}

} // namespace

namespace {
bool probe_native_impl(const std::string& path,
                       uint32_t* native_w, uint32_t* native_h,
                       Error* err) {
    *native_w = 0;
    *native_h = 0;
    AVFormatContext* raw_fmt = nullptr;
    if (int rc = avformat_open_input(&raw_fmt, path.c_str(), nullptr, nullptr);
        rc < 0) {
        fail(err, "avformat_open_input: " + av_err_str(rc));
        return false;
    }
    std::unique_ptr<AVFormatContext, void(*)(AVFormatContext*)> fmt(
        raw_fmt,
        [](AVFormatContext* p) { if (p) avformat_close_input(&p); });
    if (int rc = avformat_find_stream_info(fmt.get(), nullptr); rc < 0) {
        fail(err, "avformat_find_stream_info: " + av_err_str(rc));
        return false;
    }
    int idx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) { fail(err, "no video stream in file"); return false; }
    AVCodecParameters* par = fmt->streams[idx]->codecpar;
    if (par->width <= 0 || par->height <= 0) {
        fail(err, "video stream has invalid native dimensions");
        return false;
    }
    *native_w = static_cast<uint32_t>(par->width);
    *native_h = static_cast<uint32_t>(par->height);
    return true;
}
} // namespace (probe_native_impl)

VideoDecoder::~VideoDecoder() = default;

auto VideoDecoder::probe_native(const std::string& path)
    -> rstd::Result<ProbeResult, Error> {
    Error    err;
    uint32_t w = 0, h = 0;
    if (!probe_native_impl(path, &w, &h, &err)) {
        return rstd::Err(std::move(err));
    }
    return rstd::Ok(ProbeResult { w, h });
}

auto VideoDecoder::open(const std::string& path,
                        uint32_t target_w, uint32_t target_h, bool loop)
    -> rstd::Result<std::unique_ptr<VideoDecoder>, Error> {
    Error err;
    auto  p = build_internal(path, target_w, target_h, loop,
                             /*pre_built_hwdev=*/nullptr,
                             /*requested_kind=*/FrameKind::Sw, &err);
    if (!p) return rstd::Err(std::move(err));
    return rstd::Ok(std::move(p));
}

auto VideoDecoder::open_with_vk(const std::string& path,
                                uint32_t target_w, uint32_t target_h, bool loop,
                                const Producer& vk,
                                const OpenOpts& opts)
    -> rstd::Result<std::unique_ptr<VideoDecoder>, Error> {
    /* Resolve trial order. Auto = Vulkan first, then VAAPI; explicit
     * single-mode skips the others; None goes straight to sw. */
    HwAccel order[2] = { HwAccel::None, HwAccel::None };
    int     n_order  = 0;
    switch (opts.hwaccel) {
    case HwAccel::Auto:   order[0] = HwAccel::Vulkan; order[1] = HwAccel::Vaapi; n_order = 2; break;
    case HwAccel::Vulkan: order[0] = HwAccel::Vulkan; n_order = 1; break;
    case HwAccel::Vaapi:  order[0] = HwAccel::Vaapi;  n_order = 1; break;
    case HwAccel::None:   n_order = 0; break;
    }

    for (int i = 0; i < n_order; ++i) {
        Error        local_err;
        AVBufferRef* hwd = nullptr;
        FrameKind    kind = FrameKind::Sw;
        if (order[i] == HwAccel::Vulkan) {
            hwd  = make_shared_vulkan_hwdevice(vk, &local_err);
            kind = FrameKind::VulkanShared;
        } else if (order[i] == HwAccel::Vaapi) {
#if defined(WAVSEN_HAS_VAAPI)
            hwd  = make_vaapi_hwdevice(opts.render_node, &local_err);
            kind = FrameKind::VaapiDrm;
#else
            local_err.message = "wavsen built without VAAPI support";
#endif
        }
        if (!hwd) {
            rstd::log::info("VideoDecoder: hwaccel attempt {} skipped: {}",
                            order[i] == HwAccel::Vulkan ? "vulkan" : "vaapi",
                            local_err.message.c_str());
            continue;
        }
        Error err;
        auto  p = build_internal(path, target_w, target_h, loop, hwd, kind, &err);
        if (p) return rstd::Ok(std::move(p));
        rstd::log::info("VideoDecoder: hwaccel {} build_internal failed: {} — trying next",
                        order[i] == HwAccel::Vulkan ? "vulkan" : "vaapi",
                        err.message.c_str());
        /* build_internal already unref'd `hwd` on failure via state. */
    }

    /* Final fallback: pure sw decode. */
    Error err;
    auto  p = build_internal(path, target_w, target_h, loop,
                             /*pre_built_hwdev=*/nullptr,
                             /*requested_kind=*/FrameKind::Sw, &err);
    if (!p) return rstd::Err(std::move(err));
    return rstd::Ok(std::move(p));
}

std::unique_ptr<VideoDecoder>
VideoDecoder::build_internal(const std::string& path,
                             uint32_t target_w, uint32_t target_h,
                             bool loop, void* pre_built_hwdev_v,
                             FrameKind requested_kind,
                             Error* err) {
    AVBufferRef* pre_built_hwdev = static_cast<AVBufferRef*>(pre_built_hwdev_v);
    if (target_w == 0 || target_h == 0) {
        fail(err, "target dimensions must be non-zero");
        if (pre_built_hwdev) av_buffer_unref(&pre_built_hwdev);
        return nullptr;
    }
    /* NV12 chroma is half-resolution → both dims must be even. */
    if (target_w & 1u) ++target_w;
    if (target_h & 1u) ++target_h;

    auto self = std::unique_ptr<VideoDecoder>(new VideoDecoder());
    self->target_w_ = target_w;
    self->target_h_ = target_h;
    self->loop_     = loop;
    self->st_       = std::make_unique<VideoDecoder::State>();
    /* Provisional; downgraded to Sw below if hwdevice attach fails. */
    self->kind_     = requested_kind;
    /* Take ownership of the caller's hwdevice ref immediately so that
     * any early-return path below (avformat_open_input failure etc.)
     * unrefs it via state.hwd's deleter rather than leaking. The codec
     * gets its own ref later. */
    if (pre_built_hwdev) self->st_->hwd.reset(pre_built_hwdev);

    AVFormatContext* raw_fmt = nullptr;
    if (int rc = avformat_open_input(&raw_fmt, path.c_str(), nullptr, nullptr);
        rc < 0) {
        fail(err, "avformat_open_input: " + av_err_str(rc));
        return nullptr;
    }
    self->st_->fmt.reset(raw_fmt);

    if (int rc = avformat_find_stream_info(self->st_->fmt.get(), nullptr); rc < 0) {
        fail(err, "avformat_find_stream_info: " + av_err_str(rc));
        return nullptr;
    }

    int idx = av_find_best_stream(self->st_->fmt.get(),
                                  AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) { fail(err, "no video stream in file"); return nullptr; }
    self->st_->video_idx = idx;
    AVStream*           st  = self->st_->fmt->streams[idx];
    AVCodecParameters*  par = st->codecpar;
    self->st_->stream_tb = st->time_base;

    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        fail(err, std::string("no decoder for codec ") + avcodec_get_name(par->codec_id));
        return nullptr;
    }
    self->st_->cctx.reset(avcodec_alloc_context3(dec));
    if (!self->st_->cctx) { fail(err, "avcodec_alloc_context3 failed"); return nullptr; }
    if (int rc = avcodec_parameters_to_context(self->st_->cctx.get(), par); rc < 0) {
        fail(err, "avcodec_parameters_to_context: " + av_err_str(rc));
        return nullptr;
    }

    /* Hand the codec its own ref on the hwdevice the trial loop picked
     * (if any). Sw mode has hwd == nullptr — codec stays sw. */
    if (self->st_->hwd) {
        self->st_->cctx->hw_device_ctx = av_buffer_ref(self->st_->hwd.get());
        if (requested_kind == FrameKind::VulkanShared) {
            self->st_->cctx->get_format = get_format_prefer_vulkan;
            rstd::log::info(
                "VideoDecoder: AV_HWDEVICE_TYPE_VULKAN attached for codec {}.",
                avcodec_get_name(par->codec_id));
        }
#if defined(WAVSEN_HAS_VAAPI)
        else if (requested_kind == FrameKind::VaapiDrm) {
            self->st_->cctx->get_format = get_format_prefer_vaapi;
            rstd::log::info(
                "VideoDecoder: AV_HWDEVICE_TYPE_VAAPI attached for codec {}.",
                avcodec_get_name(par->codec_id));
        }
#endif
    } else {
        rstd::log::info(
            "VideoDecoder: sw decode for codec {}.",
            avcodec_get_name(par->codec_id));
    }

    if (int rc = avcodec_open2(self->st_->cctx.get(), dec, nullptr); rc < 0) {
        fail(err, "avcodec_open2: " + av_err_str(rc));
        return nullptr;
    }

    /* The zero-copy next_vk_frame path requires DISABLE_MULTIPLANE
     * AVVkFrames (img[0]=Y, img[1]=UV) to match the GPU YUV→RGB
     * shader's R8 + R8G8 sampler bindings. We can't set that flag
     * without bypassing FFmpeg's frame_params bootstrap (see comment
     * on get_format_prefer_vulkan), so the Vulkan-shared path always
     * downgrades to Sw at frame-pump time. The VAAPI path stays as
     * VaapiDrm — av_hwframe_map handles modifier negotiation. */
    if (requested_kind == FrameKind::VulkanShared) {
        self->kind_ = FrameKind::Sw;
    }

    self->st_->pkt.reset(av_packet_alloc());
    self->st_->src_frame.reset(av_frame_alloc());
    if (!self->st_->pkt || !self->st_->src_frame) {
        fail(err, "av_packet_alloc / av_frame_alloc failed");
        return nullptr;
    }
    return self;
}

int VideoDecoder::next_vk_frame_(VkFrameView& out, Error* err) {
    if (kind_ != FrameKind::VulkanShared) {
        fail(err, "next_vk_frame called on non-shared-device decoder");
        return -1;
    }
    State& st = *st_;

    /* Release the previously-yielded AVVkFrame back to the pool. The
     * caller's GPU work that referenced it has been queue-submitted by
     * now (the contract of next_vk_frame), so it's safe to unref —
     * the AVVkFrame survives in the pool's hwframe context. */
    av_frame_unref(st.src_frame.get());

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            if (st.src_frame->format != AV_PIX_FMT_VULKAN) {
                fail(err, "next_vk_frame: decoder produced non-vulkan frame");
                return -1;
            }
            auto* vkf = reinterpret_cast<AVVkFrame*>(st.src_frame->data[0]);
            out.img          = vkf->img;
            out.layout       = vkf->layout;
            out.sem          = vkf->sem;
            out.sem_value    = vkf->sem_value;
            out.queue_family = vkf->queue_family;
            out.plane_count  = (vkf->img[1] != VK_NULL_HANDLE) ? 2u : 1u;
            out.width        = static_cast<uint32_t>(st.src_frame->width);
            out.height       = static_cast<uint32_t>(st.src_frame->height);
            out.colorspace   = map_colorspace(st.src_frame->colorspace);
            out.color_range  = map_range(st.src_frame->color_range);
            /* Look up the AVHWFramesContext's sw_format to know whether
             * the GPU images we're about to sample are 8-bit (NV12) or
             * 10-bit (P010). Both are 2-image disjoint formats here. */
            out.bit_depth = 8;
            if (st.src_frame->hw_frames_ctx) {
                auto* hwfc = reinterpret_cast<AVHWFramesContext*>(
                    st.src_frame->hw_frames_ctx->data);
                if (hwfc->sw_format == AV_PIX_FMT_P010
                    || hwfc->sw_format == AV_PIX_FMT_P016) {
                    out.bit_depth = 16;
                }
            }
            const int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                ? st.src_frame->best_effort_timestamp
                : st.src_frame->pts;
            out.pts_seconds = (pts == AV_NOPTS_VALUE)
                ? -1.0
                : static_cast<double>(pts) * ffi::av_q2d(st.stream_tb);
            return 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (!seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed");
                    return -1;
                }
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, "avcodec_receive_frame: " + av_err_str(rc));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, "av_read_frame: " + av_err_str(rc));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, "avcodec_send_packet: " + av_err_str(rc));
            return -1;
        }
    }
}

int VideoDecoder::next_drm_frame_(DrmFrameView& out, Error* err) {
    if (kind_ != FrameKind::VaapiDrm) {
        fail(err, "next_drm_frame called on non-VAAPI decoder");
        return -1;
    }
    State& st = *st_;

    if (!st.drm_frame) st.drm_frame.reset(av_frame_alloc());
    if (!st.drm_frame) { fail(err, "av_frame_alloc(drm_frame) failed"); return -1; }

    /* Release prior pull's mapped fds before grabbing the next surface. */
    av_frame_unref(st.src_frame.get());
    av_frame_unref(st.drm_frame.get());

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
#if defined(WAVSEN_HAS_VAAPI)
            if (st.src_frame->format != AV_PIX_FMT_VAAPI) {
                fail(err, "next_drm_frame: decoder produced non-VAAPI frame");
                return -1;
            }
#else
            fail(err, "next_drm_frame: VAAPI support not built");
            return -1;
#endif
            st.drm_frame->format = AV_PIX_FMT_DRM_PRIME;
            int mrc = av_hwframe_map(st.drm_frame.get(), st.src_frame.get(),
                                     AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
            if (mrc < 0) {
                fail(err, "av_hwframe_map(DRM_PRIME): " + av_err_str(mrc));
                return -1;
            }
            const auto* desc = reinterpret_cast<const AVDRMFrameDescriptor*>(
                st.drm_frame->data[0]);
            if (!desc) {
                fail(err, "av_hwframe_map: DRM_PRIME descriptor null");
                return -1;
            }
            const int n_obj = desc->nb_objects < 4 ? desc->nb_objects : 4;
            const int n_lay = desc->nb_layers  < 4 ? desc->nb_layers  : 4;
            out.object_count = static_cast<uint32_t>(n_obj);
            for (int i = 0; i < n_obj; ++i) {
                out.objects[i].fd              = desc->objects[i].fd;
                out.objects[i].size            = desc->objects[i].size;
                out.objects[i].format_modifier = desc->objects[i].format_modifier;
            }
            out.layer_count = static_cast<uint32_t>(n_lay);
            for (int li = 0; li < n_lay; ++li) {
                const auto& la = desc->layers[li];
                out.layers[li].fourcc      = la.format;
                const int np = la.nb_planes < 4 ? la.nb_planes : 4;
                out.layers[li].plane_count = static_cast<uint32_t>(np);
                for (int p = 0; p < np; ++p) {
                    out.layers[li].planes[p].object_index =
                        static_cast<uint32_t>(la.planes[p].object_index);
                    out.layers[li].planes[p].offset = la.planes[p].offset;
                    out.layers[li].planes[p].pitch  = la.planes[p].pitch;
                }
            }
            out.width  = static_cast<uint32_t>(st.src_frame->width);
            out.height = static_cast<uint32_t>(st.src_frame->height);
            out.colorspace  = map_colorspace(st.src_frame->colorspace);
            out.color_range = map_range(st.src_frame->color_range);
            /* VAAPI 8-bit profiles land as NV12; 10-bit as P010. We only
             * support 8-bit on the DRM_PRIME zero-copy path for now. */
            out.bit_depth = 8;
            const int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                ? st.src_frame->best_effort_timestamp
                : st.src_frame->pts;
            out.pts_seconds = (pts == AV_NOPTS_VALUE)
                ? -1.0
                : static_cast<double>(pts) * ffi::av_q2d(st.stream_tb);
            return 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (!seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed");
                    return -1;
                }
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, "avcodec_receive_frame: " + av_err_str(rc));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, "av_read_frame: " + av_err_str(rc));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, "avcodec_send_packet: " + av_err_str(rc));
            return -1;
        }
    }
}

int VideoDecoder::next_frame_(Nv12Frame& out, Error* err) {
    State& st = *st_;

    /* Resize output buffer to NV12 size on first call (and on extent
     * change, but the extent is fixed for VideoDecoder lifetime). */
    const size_t want = size_t(target_w_) * target_h_ * 3 / 2;
    if (out.width != target_w_ || out.height != target_h_ || out.data.size() != want) {
        out.width  = target_w_;
        out.height = target_h_;
        out.data.assign(want, 0u);
    }

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            /* If the decoder produced a vulkan-typed frame (Iter 4 hw
             * path), download it to a sw frame first. The download lands
             * in whatever YUV format the AVHWFramesContext exposes —
             * typically NV12 — and swscale handles whatever it is. */
            AVFrame* feed = st.src_frame.get();
            if (feed->format == AV_PIX_FMT_VULKAN) {
                if (!st.sw_frame) st.sw_frame.reset(av_frame_alloc());
                if (!st.sw_frame) {
                    fail(err, "av_frame_alloc(sw_frame) failed");
                    return -1;
                }
                av_frame_unref(st.sw_frame.get());
                int trc = av_hwframe_transfer_data(st.sw_frame.get(), feed, 0);
                if (trc < 0) {
                    fail(err, "av_hwframe_transfer_data: " + av_err_str(trc));
                    av_frame_unref(st.src_frame.get());
                    return -1;
                }
                /* Preserve PTS across the transfer (transfer_data copies
                 * pixel data only). */
                st.sw_frame->pts                    = feed->pts;
                st.sw_frame->best_effort_timestamp  = feed->best_effort_timestamp;
                feed = st.sw_frame.get();
            }

            const auto src_fmt = static_cast<AVPixelFormat>(feed->format);
            const int  src_w   = feed->width;
            const int  src_h   = feed->height;
            if (src_w <= 0 || src_h <= 0 || src_fmt == AV_PIX_FMT_NONE) {
                fail(err, "decoded frame has invalid dimensions/format");
                return -1;
            }
            if (!ensure_sws(st, src_w, src_h, src_fmt, target_w_, target_h_)) {
                fail(err, std::string("sws_getContext failed (src=") +
                          av_get_pix_fmt_name(src_fmt) + ")");
                return -1;
            }
            uint8_t* y_dst  = out.data.data();
            uint8_t* uv_dst = out.data.data() + size_t(target_w_) * target_h_;
            uint8_t* dst_planes[4]  = { y_dst, uv_dst, nullptr, nullptr };
            int      dst_strides[4] = { static_cast<int>(target_w_),
                                        static_cast<int>(target_w_),  /* NV12 UV pitch == width */
                                        0, 0 };
            int scaled = sws_scale(st.sws.get(),
                                   feed->data, feed->linesize,
                                   0, src_h, dst_planes, dst_strides);
            if (scaled <= 0) {
                fail(err, "sws_scale produced no rows");
                return -1;
            }
            const int64_t pts = (feed->best_effort_timestamp != AV_NOPTS_VALUE)
                ? feed->best_effort_timestamp
                : feed->pts;
            out.pts_seconds = (pts == AV_NOPTS_VALUE)
                ? -1.0
                : static_cast<double>(pts) * ffi::av_q2d(st.stream_tb);
            out.colorspace  = map_colorspace(feed->colorspace);
            out.color_range = map_range(feed->color_range);
            av_frame_unref(st.src_frame.get());
            if (st.sw_frame) av_frame_unref(st.sw_frame.get());
            return 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (!seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed");
                    return -1;
                }
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, "avcodec_receive_frame: " + av_err_str(rc));
            return -1;
        }

        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, "av_read_frame: " + av_err_str(rc));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, "avcodec_send_packet: " + av_err_str(rc));
            return -1;
        }
    }
}

// ---------------------------------------------------------------------------
// Public Result wrappers for the per-frame pull
// ---------------------------------------------------------------------------

auto VideoDecoder::next_frame(Nv12Frame& out) -> rstd::Result<NextFrame, Error> {
    Error err;
    int   rc = next_frame_(out, &err);
    if (rc < 0) return rstd::Err(std::move(err));
    return rstd::Ok(rc == 1 ? NextFrame::Eof : NextFrame::Ok);
}

auto VideoDecoder::next_vk_frame(VkFrameView& out) -> rstd::Result<NextFrame, Error> {
    Error err;
    int   rc = next_vk_frame_(out, &err);
    if (rc < 0) return rstd::Err(std::move(err));
    return rstd::Ok(rc == 1 ? NextFrame::Eof : NextFrame::Ok);
}

auto VideoDecoder::next_drm_frame(DrmFrameView& out) -> rstd::Result<NextFrame, Error> {
    Error err;
    int   rc = next_drm_frame_(out, &err);
    if (rc < 0) return rstd::Err(std::move(err));
    return rstd::Ok(rc == 1 ? NextFrame::Eof : NextFrame::Ok);
}

} // namespace wavsen::video
