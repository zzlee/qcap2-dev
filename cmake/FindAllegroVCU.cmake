# FindAllegroVCU.cmake
# --------------------
# Find the Xilinx Allegro VCU (vcu-ctrl-sw) SDK for hardware-accelerated
# video encoding and decoding on Xilinx FPGA/MPSoC platforms.
#
# This module searches for:
#   - Header directory containing "lib_encode/lib_encoder.h"
#     and "lib_decode/lib_decode.h"
#   - Shared libraries: liballegro_encode.so, liballegro_decode.so,
#     liballegro_common.so, libfpga_dma.so
#
# Usage:
#   find_package(AllegroVCU)
#
# Variables set (whether in cache or not):
#   AllegroVCU_FOUND        - TRUE if all required components are found
#   AllegroVCU_INCLUDE_DIR  - include directory (cache variable)
#   AllegroVCU_INCLUDE_DIRS - convenience list of include directories
#   AllegroVCU_LIBRARIES    - list of all required library file paths
#   AllegroVCU_ENCODE_LIB   - path to liballegro_encode.so
#   AllegroVCU_DECODE_LIB   - path to liballegro_decode.so
#   AllegroVCU_COMMON_LIB   - path to liballegro_common.so
#   AllegroVCU_FPGA_DMA_LIB - path to libfpga_dma.so
#
# Imported targets (always available when found):
#   AllegroVCU::AllegroVCU - interface library bundling headers & libraries
#
# Environment variables that influence the search:
#   VCU_CTRL_SW_DIR  - hint pointing to the vcu-ctrl-sw installation root
#
# ---------------------------------------------------------------------------
# The vcu-ctrl-sw SDK is typically installed at one of:
#   /opt/xilinx/vcu-ctrl-sw
#   /opt/vcu-ctrl-sw
#   /usr/local/vcu-ctrl-sw
# or in a Yocto SDK sysroot under paths such as:
#   /opt/sc6f0/sysroots/<target>/usr
#
# Set VCU_CTRL_SW_DIR to point directly at the root of the SDK tree
# (the parent of "include/" and "lib/" directories).
# ---------------------------------------------------------------------------

# Guard against multiple inclusion
if(AllegroVCU_FOUND)
    return()
endif()

# ---------------------------------------------------------------------------
# Helper macro to emit standard find-module status messages
# ---------------------------------------------------------------------------
macro(_allegrovcu_status_text)
    if(NOT AllegroVCU_FIND_QUIETLY)
        message(STATUS "FindAllegroVCU: ${ARGV}")
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Define root search hints
# ---------------------------------------------------------------------------
set(_AllegroVCU_ROOT_HINTS
    "$ENV{VCU_CTRL_SW_DIR}"
    "$ENV{ALLEGRO_VCU_DIR}"
)

set(_AllegroVCU_ROOT_PATHS
    /opt/xilinx/vcu-ctrl-sw
    /opt/vcu-ctrl-sw
    /usr/local/vcu-ctrl-sw
    /usr
    /usr/local
)

# Common subdirectory suffixes used in SDK installations
set(_AllegroVCU_SUBDIRS
    ""
    "/include"
)

# ---------------------------------------------------------------------------
# Find include directory
# ---------------------------------------------------------------------------
find_path(AllegroVCU_INCLUDE_DIR
    NAMES
        "lib_encode/lib_encoder.h"
        "lib_decode/lib_decode.h"
    HINTS
        ${_AllegroVCU_ROOT_HINTS}
    PATHS
        ${_AllegroVCU_ROOT_PATHS}
    PATH_SUFFIXES
        include
        Include
        ${_AllegroVCU_SUBDIRS}
    DOC "Path to vcu-ctrl-sw include directory (parent of lib_encode/ and lib_decode/)"
)

# If we found the header underneath a subdirectory, compute the actual root
# (the directory containing lib_encode/ and lib_decode/).
if(AllegroVCU_INCLUDE_DIR)
    get_filename_component(_AllegroVCU_INCLUDE_CANDIDATE "${AllegroVCU_INCLUDE_DIR}" REALPATH)
    # Check whether the found path directly contains lib_encode/lib_encoder.h
    if(EXISTS "${_AllegroVCU_INCLUDE_CANDIDATE}/lib_encode/lib_encoder.h")
        # Already at the right level
    elseif(EXISTS "${_AllegroVCU_INCLUDE_CANDIDATE}/include/lib_encode/lib_encoder.h")
        set(_AllegroVCU_INCLUDE_CANDIDATE "${_AllegroVCU_INCLUDE_CANDIDATE}/include")
    elseif(EXISTS "${_AllegroVCU_INCLUDE_CANDIDATE}/../lib_encode/lib_encoder.h")
        get_filename_component(_AllegroVCU_INCLUDE_CANDIDATE "${_AllegroVCU_INCLUDE_CANDIDATE}/.." REALPATH)
    endif()
    set(AllegroVCU_INCLUDE_DIR "${_AllegroVCU_INCLUDE_CANDIDATE}" CACHE PATH "Allegro VCU include directory" FORCE)
    mark_as_advanced(AllegroVCU_INCLUDE_DIR)
endif()

# ---------------------------------------------------------------------------
# Find libraries
# ---------------------------------------------------------------------------
find_library(AllegroVCU_ENCODE_LIB
    NAMES
        allegro_encode
    HINTS
        ${_AllegroVCU_ROOT_HINTS}
        ${AllegroVCU_INCLUDE_DIR}
    PATHS
        ${_AllegroVCU_ROOT_PATHS}
    PATH_SUFFIXES
        lib
        lib64
        Lib
        "lib/linux"
    DOC "Path to liballegro_encode.so"
)

find_library(AllegroVCU_DECODE_LIB
    NAMES
        allegro_decode
    HINTS
        ${_AllegroVCU_ROOT_HINTS}
        ${AllegroVCU_INCLUDE_DIR}
    PATHS
        ${_AllegroVCU_ROOT_PATHS}
    PATH_SUFFIXES
        lib
        lib64
        Lib
        "lib/linux"
    DOC "Path to liballegro_decode.so"
)

find_library(AllegroVCU_COMMON_LIB
    NAMES
        allegro_common
    HINTS
        ${_AllegroVCU_ROOT_HINTS}
        ${AllegroVCU_INCLUDE_DIR}
    PATHS
        ${_AllegroVCU_ROOT_PATHS}
    PATH_SUFFIXES
        lib
        lib64
        Lib
        "lib/linux"
    DOC "Path to liballegro_common.so"
)

find_library(AllegroVCU_FPGA_DMA_LIB
    NAMES
        fpga_dma
    HINTS
        ${_AllegroVCU_ROOT_HINTS}
        ${AllegroVCU_INCLUDE_DIR}
    PATHS
        ${_AllegroVCU_ROOT_PATHS}
    PATH_SUFFIXES
        lib
        lib64
        Lib
        "lib/linux"
    DOC "Path to libfpga_dma.so"
)

# ---------------------------------------------------------------------------
# Determine if ALL required components were found
# ---------------------------------------------------------------------------
include(FindPackageHandleStandardArgs)

# We require the header directory and the four libraries
find_package_handle_standard_args(AllegroVCU
    REQUIRED_VARS
        AllegroVCU_INCLUDE_DIR
        AllegroVCU_ENCODE_LIB
        AllegroVCU_DECODE_LIB
        AllegroVCU_COMMON_LIB
        AllegroVCU_FPGA_DMA_LIB
    FAIL_MESSAGE
        "Allegro VCU (vcu-ctrl-sw) not found. Set VCU_CTRL_SW_DIR to the SDK installation root."
)

# ---------------------------------------------------------------------------
# Populate convenience variables
# ---------------------------------------------------------------------------
if(AllegroVCU_FOUND)
    set(AllegroVCU_INCLUDE_DIRS "${AllegroVCU_INCLUDE_DIR}")

    set(AllegroVCU_LIBRARIES
        "${AllegroVCU_ENCODE_LIB}"
        "${AllegroVCU_DECODE_LIB}"
        "${AllegroVCU_COMMON_LIB}"
        "${AllegroVCU_FPGA_DMA_LIB}"
    )

    # Mark libraries as advanced (they are derived, not typically hand-edited)
    mark_as_advanced(
        AllegroVCU_ENCODE_LIB
        AllegroVCU_DECODE_LIB
        AllegroVCU_COMMON_LIB
        AllegroVCU_FPGA_DMA_LIB
    )

    # -----------------------------------------------------------------------
    # Create imported target for convenient use
    # -----------------------------------------------------------------------
    if(NOT TARGET AllegroVCU::AllegroVCU)
        add_library(AllegroVCU::AllegroVCU INTERFACE IMPORTED)
        set_target_properties(AllegroVCU::AllegroVCU PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${AllegroVCU_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${AllegroVCU_LIBRARIES}"
        )
    endif()

    _allegrovcu_status_text("Found at ${AllegroVCU_INCLUDE_DIR}")
    _allegrovcu_status_text("  Encode lib:    ${AllegroVCU_ENCODE_LIB}")
    _allegrovcu_status_text("  Decode lib:    ${AllegroVCU_DECODE_LIB}")
    _allegrovcu_status_text("  Common lib:    ${AllegroVCU_COMMON_LIB}")
    _allegrovcu_status_text("  FPGA DMA lib:  ${AllegroVCU_FPGA_DMA_LIB}")
else()
    _allegrovcu_status_text("NOT found. Set VCU_CTRL_SW_DIR to the vcu-ctrl-sw installation root.")
endif()

# ---------------------------------------------------------------------------
# Clean up internal variables
# ---------------------------------------------------------------------------
unset(_AllegroVCU_ROOT_HINTS)
unset(_AllegroVCU_ROOT_PATHS)
unset(_AllegroVCU_SUBDIRS)
unset(_AllegroVCU_INCLUDE_CANDIDATE)
