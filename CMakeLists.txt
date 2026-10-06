cmake_minimum_required(VERSION 4.0)

set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)
project(NativeWaterLightStabilizer VERSION 1.4 LANGUAGES CXX)

set(PLUGIN_FILENAME "Native_Water_Light_Stabilizer")

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(FetchContent)

set(COMMON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/common")
set(SKSE64_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/skse64")
set(NWLS_IERRORS_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/extern-overrides/IErrors.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NWLS_IERRORS_OVERRIDE}")

FetchContent_Declare(
    common
    GIT_REPOSITORY https://github.com/ianpatt/common.git
    GIT_TAG 64e233c096735551f6ac9a773726a8a3960e46cd
    SOURCE_DIR "${COMMON_ROOT}"
)

FetchContent_Declare(
    skse64
    GIT_REPOSITORY https://github.com/ianpatt/skse64.git
    GIT_TAG 4cd2e34face74d5247fa38888c7542ad45d4a1d2
    SOURCE_DIR "${SKSE64_ROOT}"
    SOURCE_SUBDIR "_cmake_no_build"
)

set(FETCHCONTENT_LOCK_FILE "${CMAKE_BINARY_DIR}/fetchcontent.lock")
file(LOCK "${FETCHCONTENT_LOCK_FILE}" GUARD PROCESS TIMEOUT 300)
set(CMAKE_SKIP_INSTALL_RULES TRUE)

if(EXISTS "${COMMON_ROOT}/CMakeLists.txt" AND EXISTS "${COMMON_ROOT}/common")
    set(FETCHCONTENT_SOURCE_DIR_COMMON "${COMMON_ROOT}")
endif()

if(EXISTS "${SKSE64_ROOT}/skse64_common/BranchTrampoline.cpp")
    set(FETCHCONTENT_SOURCE_DIR_SKSE64 "${SKSE64_ROOT}")
endif()

FetchContent_MakeAvailable(common skse64)

if(NOT EXISTS "${NWLS_IERRORS_OVERRIDE}" OR NOT EXISTS "${COMMON_ROOT}/common/IErrors.h")
    message(FATAL_ERROR "Required NWLS assertion-path override or CommonLib IErrors.h is missing")
endif()

file(COPY_FILE "${NWLS_IERRORS_OVERRIDE}" "${COMMON_ROOT}/common/IErrors.h" ONLY_IF_DIFFERENT)

file(LOCK "${FETCHCONTENT_LOCK_FILE}" RELEASE)

add_library(NativeWaterLightStabilizer SHARED
    src/main.cpp
    src/RuntimeAddresses.cpp
    src/WaterSelection.cpp
    src/CameraReflectionFix.cpp
    src/Hooks.cpp
    ${SKSE64_ROOT}/skse64_common/BranchTrampoline.cpp
    ${SKSE64_ROOT}/skse64_common/Relocation.cpp
    ${SKSE64_ROOT}/skse64_common/SafeWrite.cpp
    ${SKSE64_ROOT}/skse64_common/Utilities.cpp
)

set_source_files_properties(
    ${SKSE64_ROOT}/skse64_common/BranchTrampoline.cpp
    ${SKSE64_ROOT}/skse64_common/Relocation.cpp
    ${SKSE64_ROOT}/skse64_common/SafeWrite.cpp
    ${SKSE64_ROOT}/skse64_common/Utilities.cpp
    PROPERTIES COMPILE_OPTIONS "/FI${COMMON_ROOT}/common/IPrefix.h"
)

target_include_directories(NativeWaterLightStabilizer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${COMMON_ROOT}
    ${SKSE64_ROOT}
)

target_compile_features(NativeWaterLightStabilizer PRIVATE cxx_std_20)
target_compile_definitions(NativeWaterLightStabilizer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NWLS_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(NativeWaterLightStabilizer PRIVATE
    xse::common
    Shell32
)

set_target_properties(NativeWaterLightStabilizer PROPERTIES
    OUTPUT_NAME "${PLUGIN_FILENAME}"
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

set(PLUGIN_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
if(PLUGIN_VERSION_PATCH STREQUAL "")
    set(PLUGIN_VERSION_PATCH 0)
endif()
set(PLUGIN_AUTHOR "nicola89b")
set(PLUGIN_COPYRIGHT "GPL-3.")
set(PLUGIN_DESCRIPTION "Stable water lighting for Skyrim SE, AE and VR.")
set(PLUGIN_COMMENTS "Deterministic, geometry-aware water light selection with camera-centred reflections.")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY
)
target_sources(NativeWaterLightStabilizer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

if(MSVC)
    target_link_options(NativeWaterLightStabilizer PRIVATE "/PDBALTPATH:%_PDB%")
endif()

set(NWLS_DIST_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/SKSE/Plugins")
add_custom_command(TARGET NativeWaterLightStabilizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NWLS_DIST_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:NativeWaterLightStabilizer>" "${NWLS_DIST_PLUGIN_DIR}/${PLUGIN_FILENAME}.dll"
    VERBATIM
)
