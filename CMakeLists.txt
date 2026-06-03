cmake_minimum_required(VERSION 3.15)
project(qcap2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compile options
add_compile_options(-Wall -Wextra -g)

# ==============================================================================
# Git build info (injected at configure time)
# ==============================================================================
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git branch --show-current
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
# Fallback to CI environment variables
if(NOT GIT_COMMIT_SHORT)
    if(DEFINED ENV{CI_COMMIT_SHORT_SHA})
        set(GIT_COMMIT_SHORT $ENV{CI_COMMIT_SHORT_SHA})
    elseif(DEFINED ENV{GITHUB_SHA})
        string(SUBSTRING $ENV{GITHUB_SHA} 0 7 GIT_COMMIT_SHORT)
    else()
        set(GIT_COMMIT_SHORT "unknown")
    endif()
endif()
if(NOT GIT_BRANCH OR GIT_BRANCH STREQUAL "HEAD")
    if(DEFINED ENV{CI_COMMIT_BRANCH})
        set(GIT_BRANCH $ENV{CI_COMMIT_BRANCH})
    elseif(DEFINED ENV{CI_COMMIT_TAG})
        set(GIT_BRANCH $ENV{CI_COMMIT_TAG})
    elseif(DEFINED ENV{GITHUB_REF_NAME})
        set(GIT_BRANCH $ENV{GITHUB_REF_NAME})
    else()
        set(GIT_BRANCH "unknown")
    endif()
endif()
message(STATUS "Build info: branch=${GIT_BRANCH}  commit=${GIT_COMMIT_SHORT}")

# Includes
set(QCAP2_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Thread dependency
find_package(Threads REQUIRED)

# The git build info compile definitions are injected after qcap2_utils is defined
# (see below: "Git build info injection")

# Optional dependencies
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FREETYPE freetype2)
    pkg_check_modules(HARFBUZZ harfbuzz)
    pkg_check_modules(DRM libdrm)
endif()

# Find FFmpeg development libraries
find_library(AVFORMAT_LIB NAMES avformat)
find_library(AVCODEC_LIB NAMES avcodec)
find_library(AVUTIL_LIB NAMES avutil)
find_library(SWRESAMPLE_LIB NAMES swresample)
find_library(SWSCALE_LIB NAMES swscale)
find_library(AVFILTER_LIB NAMES avfilter)

function(qcap2_add_module target output_name source_file)
    add_library(${target} SHARED ${source_file})
    target_include_directories(${target} PUBLIC ${QCAP2_INCLUDE_DIR})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME ${output_name}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH ON
    )
endfunction()

function(qcap2_add_test target)
    add_executable(${target} tests/${target}.cpp)
    target_include_directories(${target} PRIVATE ${QCAP2_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE ${ARGN})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        INSTALL_RPATH "$ORIGIN/lib"
        BUILD_WITH_INSTALL_RPATH ON
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# qcap2 modules as Linux shared objects (.so)
qcap2_add_module(qcap2_buffer qcap2.buffer src/qcap2.buffer.cpp)
qcap2_add_module(qcap2_formats qcap2.formats src/qcap2.formats.cpp)
qcap2_add_module(qcap2_sync qcap2.sync src/qcap2.sync.cpp)
qcap2_add_module(qcap2_devices qcap2.devices src/qcap2.devices.cpp)
qcap2_add_module(qcap2_utils qcap2.utils src/qcap2.utils.cpp)

# Git build info injection (target must exist first)
target_compile_definitions(qcap2_utils PRIVATE
    "QCAP2_BUILD_BRANCH=\"${GIT_BRANCH}\""
    "QCAP2_BUILD_COMMIT=\"${GIT_COMMIT_SHORT}\""
)

target_link_libraries(qcap2_formats PUBLIC qcap2_buffer)
target_link_libraries(qcap2_sync PUBLIC qcap2_buffer Threads::Threads)
target_link_libraries(qcap2_devices PUBLIC qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
if(DRM_FOUND)
    target_compile_definitions(qcap2_devices PRIVATE HAVE_DRM=1)
    target_include_directories(qcap2_devices PRIVATE ${DRM_INCLUDE_DIRS})
    target_compile_options(qcap2_devices PRIVATE ${DRM_CFLAGS_OTHER})
    target_link_libraries(qcap2_devices PRIVATE ${DRM_LIBRARIES})
endif()
target_link_libraries(qcap2_utils PUBLIC qcap2_buffer)

if(AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB)
    message(STATUS "FFmpeg avformat/avcodec/avutil found. Building libqcap2.demuxer.so.")
    qcap2_add_module(qcap2_demuxer qcap2.demuxer src/qcap2.demuxer.cpp)
    target_link_libraries(qcap2_demuxer PUBLIC
        qcap2_devices
        qcap2_processing
        qcap2_sync
        qcap2_buffer
        qcap2_formats
        Threads::Threads
        ${AVFORMAT_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
    )

    message(STATUS "FFmpeg avformat/avcodec/avutil found. Building libqcap2.muxer.so.")
    qcap2_add_module(qcap2_muxer qcap2.muxer src/qcap2.muxer.cpp)
    target_link_libraries(qcap2_muxer PUBLIC
        qcap2_devices
        qcap2_processing
        qcap2_sync
        qcap2_buffer
        qcap2_formats
        Threads::Threads
        ${AVFORMAT_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
    )
else()
    message(WARNING "FFmpeg avformat/avcodec/avutil NOT found. Skipping libqcap2.demuxer.so/muxer.so and tests.")
endif()

if(SWRESAMPLE_LIB AND SWSCALE_LIB AND AVFILTER_LIB AND AVUTIL_LIB AND AVCODEC_LIB)
    message(STATUS "FFmpeg swresample/swscale/avfilter/avcodec/avutil found. Building libqcap2.processing.so.")
    qcap2_add_module(qcap2_processing qcap2.processing src/qcap2.processing.cpp)
    target_link_libraries(qcap2_processing PUBLIC
        qcap2_buffer
        qcap2_sync
        qcap2_formats
        Threads::Threads
        ${SWRESAMPLE_LIB}
        ${SWSCALE_LIB}
        ${AVFILTER_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
    )
else()
    message(WARNING "FFmpeg swresample/swscale/avfilter/avcodec/avutil NOT found. Skipping libqcap2.processing.so and test_qcap2_processing.")
endif()

if(FREETYPE_FOUND AND HARFBUZZ_FOUND)
    message(STATUS "FreeType and HarfBuzz found. Building libqcap2.graphics.so.")
    qcap2_add_module(qcap2_graphics qcap2.graphics src/qcap2.graphics.cpp)
    target_include_directories(qcap2_graphics PUBLIC
        ${FREETYPE_INCLUDE_DIRS}
        ${HARFBUZZ_INCLUDE_DIRS}
    )
    target_compile_options(qcap2_graphics PRIVATE
        ${FREETYPE_CFLAGS_OTHER}
        ${HARFBUZZ_CFLAGS_OTHER}
    )
    target_link_libraries(qcap2_graphics PUBLIC
        qcap2_buffer
        qcap2_utils
        Threads::Threads
        ${FREETYPE_LIBRARIES}
        ${HARFBUZZ_LIBRARIES}
    )
else()
    message(WARNING "FreeType and HarfBuzz NOT found. Skipping libqcap2.graphics.so and test_qcap2_graphics.")
endif()

# Tests link against qcap2 shared objects instead of compiling module sources directly.
enable_testing()

qcap2_add_test(test_qcap2_buffer qcap2_buffer)
qcap2_add_test(test_qcap2_formats qcap2_formats)
qcap2_add_test(test_qcap2_utils qcap2_utils qcap2_buffer)
qcap2_add_test(test_qcap2_sync qcap2_sync qcap2_devices qcap2_formats qcap2_buffer Threads::Threads)
qcap2_add_test(test_qcap2_audio_sink qcap2_devices qcap2_formats qcap2_buffer Threads::Threads)
qcap2_add_test(test_qcap2_video_sink qcap2_devices qcap2_formats qcap2_buffer Threads::Threads)

if(TARGET qcap2_demuxer)
    qcap2_add_test(test_qcap2_demuxer qcap2_demuxer qcap2_processing qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
    qcap2_add_test(test_qcap2_demuxer_rtsp qcap2_demuxer qcap2_processing qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
endif()

if(TARGET qcap2_muxer)
    qcap2_add_test(test_qcap2_muxer qcap2_muxer qcap2_processing qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
    qcap2_add_test(test_qcap2_muxer_rtsp qcap2_muxer qcap2_processing qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
endif()

if(TARGET qcap2_demuxer AND TARGET qcap2_muxer)
    qcap2_add_test(test_qcap2_demux_mux qcap2_muxer qcap2_demuxer qcap2_processing qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
    qcap2_add_test(test_qcap2_sdp qcap2_muxer qcap2_demuxer qcap2_processing qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
endif()

if(TARGET qcap2_processing)
    qcap2_add_test(test_qcap2_processing qcap2_processing qcap2_buffer qcap2_sync qcap2_formats Threads::Threads)
endif()

if(TARGET qcap2_graphics)
    qcap2_add_test(test_qcap2_graphics qcap2_graphics qcap2_buffer qcap2_utils Threads::Threads)
endif()

# ==============================================================================
# Install rules
# ==============================================================================

# Collect all built modules (targets may not exist if deps were missing)
set(QCAP2_MODULES
    qcap2_buffer qcap2_formats qcap2_sync
    qcap2_devices qcap2_utils
    qcap2_processing qcap2_demuxer qcap2_muxer
    qcap2_graphics
)

foreach(mod ${QCAP2_MODULES})
    if(TARGET ${mod})
        install(TARGETS ${mod}
            LIBRARY DESTINATION lib/qcap2
        )
    endif()
endforeach()

# Install public headers
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ==============================================================================
# CPack configuration (DEB + TGZ)
# ==============================================================================
set(CPACK_PACKAGE_NAME "qcap2")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "QCAP2 - Multimedia capture and processing library")
set(CPACK_PACKAGE_VENDOR "Bill Yuan")
set(CPACK_PACKAGE_CONTACT "bill@yuan.com.tw")

set(CPACK_GENERATOR "DEB;TGZ")

# Package filename with commit signature postfix
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${GIT_COMMIT_SHORT}-${CMAKE_SYSTEM_NAME}")

set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libavcodec-dev, libavformat-dev, libavutil-dev,
     libswscale-dev, libavfilter-dev, libswresample-dev,
     libfreetype-dev, libharfbuzz-dev"
)
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

set(CPACK_STRIP_FILES TRUE)
set(CPACK_SET_DESTDIR TRUE)

include(CPack)
