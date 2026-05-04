module;

extern "C" {
#include <cubeb/cubeb.h>
}

export module cubeb;

export {

// ---- handle / opaque types ----
using ::cubeb;
using ::cubeb_stream;
using ::cubeb_devid;

// ---- enums ----
using ::cubeb_sample_format;
using ::cubeb_state;
using ::cubeb_stream_prefs;
using ::cubeb_channel_layout;
using ::cubeb_device_type;
using ::cubeb_device_state;
using ::cubeb_device_pref;
using ::cubeb_log_level;

// ---- structs ----
using ::cubeb_stream_params;
using ::cubeb_device_info;

// ---- callback typedefs ----
using ::cubeb_data_callback;
using ::cubeb_state_callback;
using ::cubeb_device_changed_callback;
using ::cubeb_log_callback;

// ---- enum values (sample format) ----
using ::CUBEB_SAMPLE_S16LE;
using ::CUBEB_SAMPLE_S16BE;
using ::CUBEB_SAMPLE_FLOAT32LE;
using ::CUBEB_SAMPLE_FLOAT32BE;

// ---- enum values (state) ----
using ::CUBEB_STATE_STARTED;
using ::CUBEB_STATE_STOPPED;
using ::CUBEB_STATE_DRAINED;
using ::CUBEB_STATE_ERROR;

// ---- enum values (result codes — anonymous enum at namespace scope) ----
using ::CUBEB_OK;
using ::CUBEB_ERROR;
using ::CUBEB_ERROR_INVALID_FORMAT;
using ::CUBEB_ERROR_INVALID_PARAMETER;
using ::CUBEB_ERROR_NOT_SUPPORTED;
using ::CUBEB_ERROR_DEVICE_UNAVAILABLE;

// ---- enum values (channel layout) ----
using ::CUBEB_LAYOUT_UNDEFINED;
using ::CUBEB_LAYOUT_MONO;
using ::CUBEB_LAYOUT_STEREO;

// ---- enum values (stream prefs) ----
using ::CUBEB_STREAM_PREF_NONE;

// ---- functions (lifecycle) ----
using ::cubeb_init;
using ::cubeb_destroy;
using ::cubeb_get_backend_id;
using ::cubeb_get_max_channel_count;
using ::cubeb_get_min_latency;
using ::cubeb_get_preferred_sample_rate;

// ---- functions (stream) ----
using ::cubeb_stream_init;
using ::cubeb_stream_destroy;
using ::cubeb_stream_start;
using ::cubeb_stream_stop;
using ::cubeb_stream_get_position;
using ::cubeb_stream_set_volume;

}
