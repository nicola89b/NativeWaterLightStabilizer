cmake_minimum_required(VERSION 4.2)
project(NativeWaterLightStabilizer VERSION 1.2 LANGUAGES CXX)

set(PLUGIN_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
if(PLUGIN_VERSION_PATCH STREQUAL "")
    set(PLUGIN_VERSION_PATCH 0)
endif()

set(PLUGIN_AUTHOR "nicola89b")
set(PLUGIN_COPYRIGHT "Copyright © 2026 ${PLUGIN_AUTHOR}.")
set(PLUGIN_DESCRIPTION "Stable water lighting for Skyrim SE, AE and VR.")
set(PLUGIN_COMMENTS "Deterministic, geometry-aware water light selection with camera-centred reflections.")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "Enable Skyrim VR" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

include(FetchContent)
if(POLICY CMP0168)
    cmake_policy(SET CMP0168 NEW)
endif()
set(EXTERN_DIR "${CMAKE_SOURCE_DIR}/extern")
file(MAKE_DIRECTORY "${EXTERN_DIR}")

function(declare_dependency name folder repository tag)
    set(source_dir "${EXTERN_DIR}/${folder}")
    set(binary_dir "${CMAKE_BINARY_DIR}/${folder}")
    if(EXISTS "${source_dir}/CMakeLists.txt")
        message(STATUS "NativeWaterLightStabilizer: using local ${name} from ${source_dir}")
        FetchContent_Declare(${name}
            SOURCE_DIR "${source_dir}"
            BINARY_DIR "${binary_dir}"
            ${ARGN})
    else()
        message(STATUS "NativeWaterLightStabilizer: downloading ${name} ${tag}")
        FetchContent_Declare(${name}
            GIT_REPOSITORY "${repository}"
            GIT_TAG "${tag}"
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
            SOURCE_DIR "${source_dir}"
            BINARY_DIR "${binary_dir}"
            ${ARGN})
    endif()
endfunction()

declare_dependency(
    spdlog
    spdlog
    https://github.com/gabime/spdlog.git
    v1.16.0
    OVERRIDE_FIND_PACKAGE)
declare_dependency(CommonLibSSE CommonLibSSE-NG https://github.com/alandtse/CommonLibVR.git v4.37.0)

find_package(fmt CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY NAMES detours REQUIRED)
FetchContent_MakeAvailable(spdlog)
set(_saved_skip "${CMAKE_SKIP_INSTALL_RULES}")
set(CMAKE_SKIP_INSTALL_RULES TRUE)
FetchContent_MakeAvailable(CommonLibSSE)
set(CMAKE_SKIP_INSTALL_RULES "${_saved_skip}")
unset(_saved_skip)

if(NOT TARGET CommonLibSSE::CommonLibSSE)
    message(FATAL_ERROR "CommonLibSSE-NG target was not created")
endif()
if(NOT COMMAND add_commonlibsse_plugin)
    include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
endif()
if(NOT COMMAND add_commonlibsse_plugin)
    message(FATAL_ERROR "add_commonlibsse_plugin() is unavailable")
endif()

set(SOURCES
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/Settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/Settings.h"
    "${CMAKE_SOURCE_DIR}/src/WaterSelection.cpp"
    "${CMAKE_SOURCE_DIR}/src/WaterSelection.h"
    "${CMAKE_SOURCE_DIR}/src/CameraReflectionFix.cpp"
    "${CMAKE_SOURCE_DIR}/src/CameraReflectionFix.h"
    "${CMAKE_SOURCE_DIR}/include/NativeWaterLightStabilizerAPI.h"
    "${CMAKE_SOURCE_DIR}/include/PCH.h")

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "NativeWaterLightStabilizer"
    AUTHOR "${PLUGIN_AUTHOR}"
    VERSION ${PROJECT_VERSION}
    USE_ADDRESS_LIBRARY
    SOURCES ${SOURCES})

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY)
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src"
    ${DETOURS_INCLUDE_DIRS})
target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NWLS_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(${PROJECT_NAME} PRIVATE
    spdlog::spdlog
    fmt::fmt
    ${DETOURS_LIBRARY})

set(NWLS_DEPLOY_ROOT
    "C:/vanillaskyrim/mods/NativeWaterLightStabilizer"
    CACHE PATH "Primary MO2 mod directory")
set(NWLS_SECONDARY_DEPLOY_ROOT
    "C:/ARCHON_/mods/NativeWaterLightStabilizer"
    CACHE PATH "Secondary MO2 mod directory")
option(NWLS_AUTO_DEPLOY "Deploy DLL and INI after build" ON)
set(NWLS_PACKED_INI
    "${CMAKE_SOURCE_DIR}/packed/SKSE/Plugins/NativeWaterLightStabilizer.ini")
set(NWLS_DIST_ROOT "${CMAKE_SOURCE_DIR}/dist/NativeWaterLightStabilizer")
set(NWLS_DIST_PLUGIN_DIR "${NWLS_DIST_ROOT}/SKSE/Plugins")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NWLS_DIST_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${NWLS_DIST_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NWLS_PACKED_INI}"
        "${NWLS_DIST_PLUGIN_DIR}/NativeWaterLightStabilizer.ini"
    VERBATIM)

if(NWLS_AUTO_DEPLOY)
    set(primary_plugin_dir "${NWLS_DEPLOY_ROOT}/SKSE/Plugins")
    set(secondary_plugin_dir "${NWLS_SECONDARY_DEPLOY_ROOT}/SKSE/Plugins")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${primary_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${primary_plugin_dir}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NWLS_PACKED_INI}"
            "${primary_plugin_dir}/NativeWaterLightStabilizer.ini"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${secondary_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>"
            "${secondary_plugin_dir}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NWLS_PACKED_INI}"
            "${secondary_plugin_dir}/NativeWaterLightStabilizer.ini"
        VERBATIM)
endif()
