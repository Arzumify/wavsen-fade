export module wavsen.audio;

// Aggregator: re-exports the split audio submodules so existing consumers
// (e.g. OWE's WPSoundParser.cpp doing `import wavsen.audio;`) continue to
// build unchanged after the split. Video plugin code that wants A/V sync
// imports `wavsen.audio.av_sync` directly.
export import wavsen.audio.byte_stream;  // IByteStream
export import wavsen.audio.core;         // DeviceDesc, IPullChannel, CubebDevice
export import wavsen.audio.mixer;        // SoundStream, SoundManager
export import wavsen.audio.file;         // StreamDecoder, make_stream
