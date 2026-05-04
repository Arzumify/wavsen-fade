// Aggregator interface unit. Re-exports the four sibling modules so
// consumers can `import wavsen.video;` and use Producer / VideoDecoder
// / YuvToRgba / Presenter without naming each piece individually.

export module wavsen.video;

export import wavsen.video.vk_device;
export import wavsen.video.video_decoder;
export import wavsen.video.yuv_to_rgba;
export import wavsen.video.presenter;
