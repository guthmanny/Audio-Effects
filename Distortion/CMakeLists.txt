cmake_minimum_required(VERSION 3.22)
project(Distortion VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# Steinberg ASIO SDK (Windows only).
if(WIN32)
    set(ASIO_SDK_PATH "" CACHE PATH "Path to Steinberg ASIO SDK root")

    if(ASIO_SDK_PATH STREQUAL "" AND DEFINED ENV{ASIO_SDK_PATH})
        set(ASIO_SDK_PATH "$ENV{ASIO_SDK_PATH}" CACHE PATH "Path to Steinberg ASIO SDK root" FORCE)
    endif()

    if(ASIO_SDK_PATH STREQUAL "" AND EXISTS "C:/asiosdk/common/iasiodrv.h")
        set(ASIO_SDK_PATH "C:/asiosdk" CACHE PATH "Path to Steinberg ASIO SDK root" FORCE)
    endif()

    if(ASIO_SDK_PATH AND EXISTS "${ASIO_SDK_PATH}/common/iasiodrv.h")
        set(DISTORTION_ASIO_SDK_INCLUDE "${ASIO_SDK_PATH}/common")
        message(STATUS "ASIO SDK: ${ASIO_SDK_PATH}")
    else()
        message(WARNING
            "JUCE_ASIO will be enabled but ASIO SDK was not found. "
            "Set ASIO_SDK_PATH (or env ASIO_SDK_PATH) to the SDK root, e.g. "
            "-DASIO_SDK_PATH=\"C:/asiosdk\"")
    endif()
endif()

# JUCE discovery (Windows / macOS / Linux).
set(JUCE_PATH "${CMAKE_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE 7+ source tree")

set(_distortion_juce_candidates "${JUCE_PATH}")

if(DEFINED ENV{JUCE_PATH})
    list(PREPEND _distortion_juce_candidates "$ENV{JUCE_PATH}")
endif()

list(APPEND _distortion_juce_candidates
    "${CMAKE_SOURCE_DIR}/../JUCE"
    "$ENV{HOME}/JUCE"
    "$ENV{HOME}/source/JUCE")

if(WIN32)
    list(APPEND _distortion_juce_candidates "D:/source/JUCE" "C:/source/JUCE")
elseif(APPLE)
    list(APPEND _distortion_juce_candidates "/opt/homebrew/opt/juce" "/usr/local/opt/juce")
endif()

set(_distortion_juce_resolved "")
foreach(_distortion_juce_candidate IN LISTS _distortion_juce_candidates)
    if(_distortion_juce_candidate AND EXISTS "${_distortion_juce_candidate}/CMakeLists.txt")
        set(_distortion_juce_resolved "${_distortion_juce_candidate}")
        break()
    endif()
endforeach()

if(_distortion_juce_resolved)
    set(JUCE_PATH "${_distortion_juce_resolved}" CACHE PATH "Path to JUCE 7+ source tree" FORCE)
    message(STATUS "Using JUCE source tree: ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    if(APPLE)
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
    else()
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local")
    endif()

    find_package(JUCE CONFIG QUIET)

    if(JUCE_FOUND)
        message(STATUS "Using installed JUCE: ${JUCE_DIR}")
    else()
        message(FATAL_ERROR
            "JUCE not found.\n"
            "  - Clone JUCE 7+ to: ${CMAKE_SOURCE_DIR}/../JUCE\n"
            "  - Or install JUCE and ensure CMake can find it (/usr/local, /opt/homebrew)\n"
            "  - Or pass: -DJUCE_PATH=/path/to/JUCE")
    endif()
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ALSA QUIET alsa)
        if(NOT ALSA_FOUND)
            message(WARNING
                "ALSA development libraries not found (pkg-config: alsa).\n"
                "On Debian/Ubuntu: sudo apt install libasound2-dev\n"
                "On Fedora: sudo dnf install alsa-lib-devel")
        endif()
    endif()
endif()

juce_add_module(${CMAKE_SOURCE_DIR}/../third_party/AtomTheme/modules/juce_atom_theme
                ALIAS_NAMESPACE juce)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/../third_party/NuDSP"
               "${CMAKE_BINARY_DIR}/NuDSP")

# NuDSP 作为静态库被链接到 PIE 可执行文件和 VST3 共享库，
# 必须启用位置无关代码（-fPIC）。
set_property(TARGET nudsp PROPERTY POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    set(DISTORTION_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
elseif(APPLE)
    set(DISTORTION_VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(DISTORTION_VST3_COPY_DIR "$ENV{HOME}/.vst3")
endif()

message(STATUS "VST3 install dir: ${DISTORTION_VST3_COPY_DIR}")

juce_add_plugin(Distortion
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Dstr
    FORMATS Standalone VST3
    PRODUCT_NAME "Distortion"
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${DISTORTION_VST3_COPY_DIR}"
)

juce_generate_juce_header(Distortion)

set(ATOM_COLLECTIONS_APP_DIR "${CMAKE_SOURCE_DIR}/../third_party/AtomTheme/demos/collections_app")

target_sources(Distortion PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DistortionHeaderComponent.cpp
    Source/DistortionHeaderComponent.h
    Source/DistortionFooterComponent.cpp
    Source/DistortionFooterComponent.h
    Source/PluginParameter.h
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.h
)

target_sources(Distortion_Standalone PRIVATE
    Source/StandaloneMain.cpp
)

target_include_directories(Distortion PRIVATE "${ATOM_COLLECTIONS_APP_DIR}")

target_compile_definitions(Distortion
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_JACK=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

if(WIN32)
    target_compile_definitions(Distortion PRIVATE JUCE_ASIO=1)

    if(DISTORTION_ASIO_SDK_INCLUDE)
        target_include_directories(Distortion PRIVATE "${DISTORTION_ASIO_SDK_INCLUDE}")
    endif()
endif()

target_link_libraries(Distortion PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_atom_theme
    nudsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
