cmake_minimum_required(VERSION 3.15)
project(qcap2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile options
add_compile_options(-Wall -Wextra -g)

# Includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)


# Thread dependency
find_package(Threads REQUIRED)

# PkgConfig for Freetype and Harfbuzz
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)

# Find FFmpeg development libraries
find_library(AVFORMAT_LIB NAMES avformat)
find_library(AVCODEC_LIB NAMES avcodec)
find_library(AVUTIL_LIB NAMES avutil)
find_library(SWRESAMPLE_LIB NAMES swresample)
find_library(SWSCALE_LIB NAMES swscale)
find_library(AVFILTER_LIB NAMES avfilter)

# 1. test_qcap2_buffer
add_executable(test_qcap2_buffer
    tests/test_qcap2_buffer.cpp
    src/qcap2.buffer.cpp
)

# 2. test_qcap2_utils
add_executable(test_qcap2_utils
    tests/test_qcap2_utils.cpp
    src/qcap2.utils.cpp
    src/qcap2.buffer.cpp
)

# 3. test_qcap2_sync
add_executable(test_qcap2_sync
    tests/test_qcap2_sync.cpp
    src/qcap2.sync.cpp
    src/qcap2.buffer.cpp
)
target_link_libraries(test_qcap2_sync PRIVATE Threads::Threads)

# 4. test_qcap2_demuxer (optional based on FFmpeg presence)
if(AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB)
    message(STATUS "FFmpeg libraries found. Building test_qcap2_demuxer.")
    add_executable(test_qcap2_demuxer
        tests/test_qcap2_demuxer.cpp
        src/qcap2.demuxer.cpp
        src/qcap2.devices.cpp
        src/qcap2.sync.cpp
        src/qcap2.buffer.cpp
        src/qcap2.formats.cpp
        src/qcap2.utils.cpp
    )
    target_link_libraries(test_qcap2_demuxer PRIVATE
        Threads::Threads
        ${AVFORMAT_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
    )
else()
    message(WARNING "FFmpeg libraries NOT found. Skipping test_qcap2_demuxer.")
endif()

# 5. test_qcap2_processing (optional based on FFmpeg components presence)
if(SWRESAMPLE_LIB AND SWSCALE_LIB AND AVFILTER_LIB AND AVUTIL_LIB AND AVCODEC_LIB)
    message(STATUS "FFmpeg swresample, swscale, avfilter, and avcodec found. Building test_qcap2_processing.")
    add_executable(test_qcap2_processing
        tests/test_qcap2_processing.cpp
        src/qcap2.processing.cpp
        src/qcap2.buffer.cpp
        src/qcap2.sync.cpp
        src/qcap2.formats.cpp
    )
    target_link_libraries(test_qcap2_processing PRIVATE
        Threads::Threads
        ${SWRESAMPLE_LIB}
        ${SWSCALE_LIB}
        ${AVFILTER_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
    )
else()
    message(WARNING "FFmpeg swresample/swscale/avfilter/avcodec/avutil NOT found. Skipping test_qcap2_processing.")
endif()


# 6. test_qcap2_graphics
if(FREETYPE_FOUND AND HARFBUZZ_FOUND)
    message(STATUS "FreeType and HarfBuzz found. Building test_qcap2_graphics.")
    add_executable(test_qcap2_graphics
        tests/test_qcap2_graphics.cpp
        src/qcap2.graphics.cpp
        src/qcap2.buffer.cpp
        src/qcap2.utils.cpp
    )
    target_include_directories(test_qcap2_graphics PRIVATE
        ${FREETYPE_INCLUDE_DIRS}
        ${HARFBUZZ_INCLUDE_DIRS}
    )
    target_link_libraries(test_qcap2_graphics PRIVATE
        Threads::Threads
        ${FREETYPE_LIBRARIES}
        ${HARFBUZZ_LIBRARIES}
    )
else()
    message(WARNING "FreeType and HarfBuzz NOT found. Skipping test_qcap2_graphics.")
endif()

# Register CTest
enable_testing()
add_test(NAME test_qcap2_buffer COMMAND test_qcap2_buffer)
add_test(NAME test_qcap2_utils COMMAND test_qcap2_utils)
add_test(NAME test_qcap2_sync COMMAND test_qcap2_sync)
if(TARGET test_qcap2_demuxer)
    add_test(NAME test_qcap2_demuxer COMMAND test_qcap2_demuxer)
endif()
if(TARGET test_qcap2_processing)
    add_test(NAME test_qcap2_processing COMMAND test_qcap2_processing)
endif()
if(TARGET test_qcap2_graphics)
    add_test(NAME test_qcap2_graphics COMMAND test_qcap2_graphics)
endif()
