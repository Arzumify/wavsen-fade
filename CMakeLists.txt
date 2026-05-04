
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAVSEN_AVFORMAT    REQUIRED IMPORTED_TARGET libavformat>=58)
pkg_check_modules(WAVSEN_AVCODEC     REQUIRED IMPORTED_TARGET libavcodec>=58)
pkg_check_modules(WAVSEN_AVUTIL      REQUIRED IMPORTED_TARGET libavutil>=56)
pkg_check_modules(WAVSEN_SWSCALE     REQUIRED IMPORTED_TARGET libswscale>=5)
pkg_check_modules(WAVSEN_SWRESAMPLE           IMPORTED_TARGET libswresample>=3)
find_package(Vulkan REQUIRED)


# ---------------------------------------------------------------------------
# wavsen::ffi::ffmpeg / wavsen::ffi::vulkan — C++20 module wrappers around
# the libav* + swscale (+ optional swresample) and Vulkan headers. Each
# is its own STATIC library so external consumers can pick & link them.
# ---------------------------------------------------------------------------
add_subdirectory(src/ffi/ffmpeg)
add_subdirectory(src/ffi/vulkan)
add_subdirectory(src/ffi/cubeb)


add_library(wavsen_decode STATIC)
add_library(wavsen::decode ALIAS wavsen_decode)

target_compile_features(wavsen_decode PUBLIC cxx_std_20)
target_compile_options(wavsen_decode PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
)

target_sources(wavsen_decode
    PUBLIC FILE_SET CXX_MODULES FILES
        src/decode/decoder.cppm
)
target_sources(wavsen_decode PRIVATE
    src/decode/decoder.cpp
)

target_link_libraries(wavsen_decode
    PUBLIC
        rstd::rstd
        rstd.cppstd
        wavsen::ffi::ffmpeg
)
set_target_properties(wavsen_decode PROPERTIES CXX_SCAN_FOR_MODULES ON)


find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator
    DOC   "GLSL → SPIR-V compiler used to bake wavsen::video shaders")
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "wavsen: glslangValidator not found in PATH — required to build "
        "wavsen::video's NV12→RGBA shader. Install Vulkan SDK or the "
        "`glslang` distro package, or set GLSLANG_VALIDATOR.")
endif()

set(_wavsen_shader_src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nv12_to_rgba.comp)
set(_wavsen_shader_hdr ${CMAKE_CURRENT_BINARY_DIR}/generated/nv12_to_rgba.spv.h)
add_custom_command(
    OUTPUT  ${_wavsen_shader_hdr}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${GLSLANG_VALIDATOR} -V --vn nv12_to_rgba_spv
            -o ${_wavsen_shader_hdr} ${_wavsen_shader_src}
    DEPENDS ${_wavsen_shader_src}
    COMMENT "wavsen: compiling NV12→RGBA compute shader"
    VERBATIM
)

add_library(wavsen_video STATIC
    ${_wavsen_shader_hdr}
)
add_library(wavsen::video ALIAS wavsen_video)

target_compile_features(wavsen_video PUBLIC cxx_std_20)
target_compile_options(wavsen_video PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
)
target_include_directories(wavsen_video
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_sources(wavsen_video
    PUBLIC FILE_SET CXX_MODULES FILES
        src/video/wavsen_video.cppm
        src/video/vk_device.cppm
        src/video/video_decoder.cppm
        src/video/yuv_to_rgba.cppm
        src/video/presenter.cppm
)
target_sources(wavsen_video PRIVATE
    src/video/vk_device.cpp
    src/video/video_decoder.cpp
    src/video/yuv_to_rgba.cpp
)
target_link_libraries(wavsen_video
    PUBLIC
        rstd::rstd
        rstd.cppstd
        wavsen::ffi::ffmpeg
        wavsen::ffi::vulkan
)
set_property(TARGET wavsen_video PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(wavsen_video PROPERTIES CXX_SCAN_FOR_MODULES ON)

if(NOT WAVSEN_SWRESAMPLE_FOUND)
    message(FATAL_ERROR
        "wavsen::audio requires libswresample (pkg-config). Install "
        "ffmpeg dev headers including libswresample.")
endif()

add_library(wavsen_audio STATIC)
add_library(wavsen::audio ALIAS wavsen_audio)

target_compile_features(wavsen_audio PUBLIC cxx_std_20)
target_compile_options(wavsen_audio PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
)

target_sources(wavsen_audio
    PUBLIC FILE_SET CXX_MODULES FILES
        src/audio/sound_manager.cppm
        src/audio/cubeb_device.cppm
        src/audio/stream_decoder.cppm
)
target_sources(wavsen_audio PRIVATE
    src/audio/cubeb_device.cpp
    src/audio/stream_decoder.cpp
    src/audio/sound_manager.cpp
)

target_link_libraries(wavsen_audio
    PUBLIC
        rstd::rstd
        rstd.cppstd
        wavsen::ffi::ffmpeg
        wavsen::ffi::cubeb
)
set_target_properties(wavsen_audio PROPERTIES CXX_SCAN_FOR_MODULES ON)
