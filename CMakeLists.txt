cmake_minimum_required(VERSION 3.15)
project(qcap2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compile options
add_compile_options(-Wall -Wextra -g)

# Includes
set(QCAP2_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Thread dependency
find_package(Threads REQUIRED)

# Optional dependencies
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FREETYPE freetype2)
    pkg_check_modules(HARFBUZZ harfbuzz)
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
    )
endfunction()

function(qcap2_add_test target)
    add_executable(${target} tests/${target}.cpp)
    target_include_directories(${target} PRIVATE ${QCAP2_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE ${ARGN})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        BUILD_RPATH ${CMAKE_BINARY_DIR}/lib
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# qcap2 modules as Linux shared objects (.so)
qcap2_add_module(qcap2_buffer qcap2.buffer src/qcap2.buffer.cpp)
qcap2_add_module(qcap2_formats qcap2.formats src/qcap2.formats.cpp)
qcap2_add_module(qcap2_sync qcap2.sync src/qcap2.sync.cpp)
qcap2_add_module(qcap2_devices qcap2.devices src/qcap2.devices.cpp)
qcap2_add_module(qcap2_utils qcap2.utils src/qcap2.utils.cpp)

target_link_libraries(qcap2_formats PUBLIC qcap2_buffer)
target_link_libraries(qcap2_sync PUBLIC qcap2_buffer Threads::Threads)
target_link_libraries(qcap2_devices PUBLIC qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
target_link_libraries(qcap2_utils PUBLIC qcap2_buffer)

if(AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB)
    message(STATUS "FFmpeg avformat/avcodec/avutil found. Building libqcap2.demuxer.so.")
    qcap2_add_module(qcap2_demuxer qcap2.demuxer src/qcap2.demuxer.cpp)
    target_link_libraries(qcap2_demuxer PUBLIC
        qcap2_devices
        qcap2_sync
        qcap2_buffer
        qcap2_formats
        Threads::Threads
        ${AVFORMAT_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
    )
else()
    message(WARNING "FFmpeg avformat/avcodec/avutil NOT found. Skipping libqcap2.demuxer.so and test_qcap2_demuxer.")
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

if(TARGET qcap2_demuxer)
    qcap2_add_test(test_qcap2_demuxer qcap2_demuxer qcap2_devices qcap2_sync qcap2_buffer qcap2_formats Threads::Threads)
endif()

if(TARGET qcap2_processing)
    qcap2_add_test(test_qcap2_processing qcap2_processing qcap2_buffer qcap2_sync qcap2_formats Threads::Threads)
endif()

if(TARGET qcap2_graphics)
    qcap2_add_test(test_qcap2_graphics qcap2_graphics qcap2_buffer qcap2_utils Threads::Threads)
endif()
