# wavsen::ffi::cubeb — C++20 module wrapper for <cubeb/cubeb.h>.
# Consumers `import cubeb;` instead of `#include <cubeb/cubeb.h>`.

add_library(wavsen_ffi_cubeb STATIC)
add_library(wavsen::ffi::cubeb ALIAS wavsen_ffi_cubeb)

target_compile_features(wavsen_ffi_cubeb PUBLIC cxx_std_20)
target_compile_options(wavsen_ffi_cubeb PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
)

target_sources(wavsen_ffi_cubeb
    PUBLIC FILE_SET CXX_MODULES FILES
        cubeb.cppm
)

target_link_libraries(wavsen_ffi_cubeb
    PUBLIC
        cubeb
)

set_property(TARGET wavsen_ffi_cubeb PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(wavsen_ffi_cubeb PROPERTIES CXX_SCAN_FOR_MODULES ON)
