cmake_minimum_required(VERSION 3.22)
project(Chorus VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

# Steinberg ASIO SDK (Windows only). Download from Steinberg developer site and set
# ASIO_SDK_PATH to the SDK root (the folder that contains common/iasiodrv.h).
if(WIN32)
    set(ASIO_SDK_PATH "" CACHE PATH "Path to Steinberg ASIO SDK root")

    if(ASIO_SDK_PATH STREQUAL "" AND DEFINED ENV{ASIO_SDK_PATH})
        set(ASIO_SDK_PATH "$ENV{ASIO_SDK_PATH}" CACHE PATH "Path to Steinberg ASIO SDK root" FORCE)
    endif()

    if(ASIO_SDK_PATH STREQUAL "" AND EXISTS "C:/asiosdk/common/iasiodrv.h")
        set(ASIO_SDK_PATH "C:/asiosdk" CACHE PATH "Path to Steinberg ASIO SDK root" FORCE)
    endif()

    if(ASIO_SDK_PATH AND EXISTS "${ASIO_SDK_PATH}/common/iasiodrv.h")
        set(CHORUS_ASIO_SDK_INCLUDE "${ASIO_SDK_PATH}/common")
        message(STATUS "ASIO SDK: ${ASIO_SDK_PATH}")
    else()
        message(WARNING
            "JUCE_ASIO will be enabled but ASIO SDK was not found. "
            "Set ASIO_SDK_PATH (or env ASIO_SDK_PATH) to the SDK root, e.g. "
            "-DASIO_SDK_PATH=\"C:/asiosdk\"")
    endif()
endif()

# JUCE discovery: Windows builds from source; Linux prefers /usr/local install.
if(WIN32)
    set(JUCE_PATH "${CMAKE_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE source")
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
        if(EXISTS "D:/source/JUCE/CMakeLists.txt")
            set(JUCE_PATH "D:/source/JUCE")
        else()
            message(FATAL_ERROR
                "JUCE not found. Set JUCE_PATH to a JUCE 7+ source tree with CMake support.")
        endif()
    endif()
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    list(PREPEND CMAKE_PREFIX_PATH "/usr/local")
    find_package(JUCE CONFIG QUIET)

    if(JUCE_FOUND)
        message(STATUS "Using installed JUCE: ${JUCE_DIR}")
    else()
        set(JUCE_PATH "${CMAKE_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE source")
        if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
            message(FATAL_ERROR
                "JUCE not found under /usr/local and no source tree at ${JUCE_PATH}. "
                "Install JUCE to /usr/local or set JUCE_PATH.")
        endif()
        message(STATUS "Using JUCE source tree: ${JUCE_PATH}")
        add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
    endif()
endif()

# ---------------------------------------------------------------------------
# AtomTheme (https://github.com/guthmanny/AtomTheme) — JUCE UI theme module
#
# Resolution order:
#   1. $ENV{HOME}/source/AtomTheme   — local development checkout
#   2. FetchContent from GitHub      — automatic download for third-party builds
# ---------------------------------------------------------------------------
include(FetchContent)

set(ATOM_THEME_LOCAL "$ENV{HOME}/source/AtomTheme")
if(EXISTS "${ATOM_THEME_LOCAL}/CMakeLists.txt")
    message(STATUS "AtomTheme found at ${ATOM_THEME_LOCAL} — using local checkout")
    juce_add_module("${ATOM_THEME_LOCAL}/modules/juce_atom_theme"
                    ALIAS_NAMESPACE juce)
    set(ATOM_COLLECTIONS_APP_DIR "${ATOM_THEME_LOCAL}/demos/collections_app")
else()
    message(STATUS "AtomTheme not found locally — fetching from GitHub")
    FetchContent_Declare(
        AtomTheme
        GIT_REPOSITORY  https://github.com/guthmanny/AtomTheme.git
        GIT_TAG         main
        GIT_SHALLOW     TRUE
    )
    FetchContent_MakeAvailable(AtomTheme)
    juce_add_module("${AtomTheme_SOURCE_DIR}/modules/juce_atom_theme"
                    ALIAS_NAMESPACE juce)
    set(ATOM_COLLECTIONS_APP_DIR "${AtomTheme_SOURCE_DIR}/demos/collections_app")
endif()

# ---------------------------------------------------------------------------
# minibuss (https://github.com/guthmanny/minibuss) — audio plugin host engine
#
# Resolution order:
#   1. $ENV{HOME}/source/minibuss   — local development checkout
#   2. FetchContent from GitHub     — automatic download for third-party builds
# ---------------------------------------------------------------------------
include(FetchContent)

set(MINIBUSS_LOCAL "$ENV{HOME}/source/minibuss")
if(EXISTS "${MINIBUSS_LOCAL}/CMakeLists.txt")
    message(STATUS "minibuss found at ${MINIBUSS_LOCAL} — using local checkout")
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MINIBUSS_LOCAL}"
                     "${CMAKE_BINARY_DIR}/minibuss"
                     EXCLUDE_FROM_ALL)
else()
    message(STATUS "minibuss not found locally — fetching from GitHub")
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        minibuss
        GIT_REPOSITORY  https://github.com/guthmanny/minibuss.git
        GIT_TAG         main
        GIT_SHALLOW     TRUE
    )
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(minibuss)
endif()

juce_add_plugin(Chorus
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Chrs
    FORMATS Standalone
    PRODUCT_NAME "Chorus"
    NEEDS_MIDI_INPUT TRUE
)

juce_generate_juce_header(Chorus)

# MINIBUSS_DIR resolves to the source tree root of minibuss.
# When using a local checkout (MINIBUSS_LOCAL), point there directly.
# When fetched via FetchContent, use minibuss_SOURCE_DIR.
if(EXISTS "${MINIBUSS_LOCAL}/CMakeLists.txt")
    set(MINIBUSS_DIR "${MINIBUSS_LOCAL}")
else()
    set(MINIBUSS_DIR "${minibuss_SOURCE_DIR}")
endif()

target_sources(Chorus PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ChorusHeaderComponent.cpp
    Source/ChorusHeaderComponent.h
    Source/ChorusFooterComponent.cpp
    Source/ChorusFooterComponent.h
    Source/TunerOverlayComponent.cpp
    Source/TunerOverlayComponent.h
    Source/SpectrumOverlayComponent.cpp
    Source/SpectrumOverlayComponent.h
    Source/SpectrumCurveComponent.cpp
    Source/SpectrumCurveComponent.h
    Source/SpectrumAnalyzer.h
    Source/QPitchDetector.h
    Source/MinibussChorusEngine.cpp
    Source/MinibussChorusEngine.h
    Source/MonoChorusProcessor.cpp
    Source/MonoChorusProcessor.h
    Source/Phase90Processor.cpp
    Source/Phase90Processor.h
    Source/AppSettingsPanel.cpp
    Source/AppSettingsPanel.h
    Source/NoiseGateSettingsPanel.cpp
    Source/NoiseGateSettingsPanel.h
    Source/ChorusMidiCcSettingsPanel.cpp
    Source/ChorusMidiCcSettingsPanel.h
    Source/CompressorCurveComponent.cpp
    Source/CompressorCurveComponent.h
    Source/CompressorKnee.h
    Source/PluginParameter.h
    Source/StandaloneMain.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.h
)

target_include_directories(Chorus PRIVATE
    "${ATOM_COLLECTIONS_APP_DIR}"
    "${MINIBUSS_DIR}/include"
    "${MINIBUSS_DIR}/src"
    /home/guthman/source/q/q_lib/include
    /home/guthman/source/q/infra/include
)

target_compile_features(Chorus PRIVATE cxx_std_20)

target_compile_definitions(Chorus
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_JACK=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

if(WIN32)
    target_compile_definitions(Chorus PRIVATE JUCE_ASIO=1)

    if(CHORUS_ASIO_SDK_INCLUDE)
        target_include_directories(Chorus PRIVATE "${CHORUS_ASIO_SDK_INCLUDE}")
    endif()
endif()

target_compile_options(Chorus PRIVATE
    -Wno-switch-enum
    -Wno-overloaded-virtual
)

target_link_libraries(Chorus PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_atom_theme
    minibuss_plugin
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
