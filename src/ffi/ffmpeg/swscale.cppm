module;

extern "C" {
#include <libswscale/swscale.h>
}

export module swscale;

export {

using ::SwsContext;

using ::sws_getContext;
using ::sws_freeContext;
using ::sws_scale;

// SWS_* are enumerators in modern libswscale, not macros — re-export by name.
using ::SWS_FAST_BILINEAR;
using ::SWS_BILINEAR;
using ::SWS_BICUBIC;
using ::SWS_LANCZOS;

}
