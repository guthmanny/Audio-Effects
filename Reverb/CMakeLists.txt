cmake_minimum_required(VERSION 3.22)
project(Reverb VERSION 1.0 LANGUAGES CXX)
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
        set(REVERB_ASIO_SDK_INCLUDE "${ASIO_SDK_PATH}/common")
        message(STATUS "ASIO SDK: ${ASIO_SDK_PATH}")
    else()
        message(WARNING
            "JUCE_ASIO will be enabled but ASIO SDK was not found. "
            "Set ASIO_SDK_PATH (or env ASIO_SDK_PATH) to the SDK root, e.g. "
            "-DASIO_SDK_PATH=\"C:/asiosdk\"")
    endif()
endif()

# JUCE discovery
if(WIN32)
    set(JUCE_PATH "${CMAKE_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE source")
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
        if(EXISTS "D:/source/JUCE/CMakeLists.txt")
            set(JUCE_PATH "D:/source/JUCE")
        else()
            message(FATAL_ERROR "JUCE not found. Set JUCE_PATH to a JUCE 7+ source tree.")
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
            message(FATAL_ERROR "JUCE not found.")
        endif()
        add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
    endif()
endif()

juce_add_module(${CMAKE_SOURCE_DIR}/../third_party/AtomTheme/modules/juce_atom_theme
                ALIAS_NAMESPACE juce)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/../third_party/NuDSP"
               "${CMAKE_BINARY_DIR}/NuDSP")

set_property(TARGET nudsp PROPERTY POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    set(REVERB_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
elseif(APPLE)
    set(REVERB_VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(REVERB_VST3_COPY_DIR "$ENV{HOME}/.vst3")
endif()

juce_add_plugin(Reverb
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Revb
    FORMATS Standalone
    PRODUCT_NAME "Reverb"
)

juce_generate_juce_header(Reverb)

set(ATOM_COLLECTIONS_APP_DIR "${CMAKE_SOURCE_DIR}/../third_party/AtomTheme/demos/collections_app")

target_sources(Reverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ReverbHeaderComponent.cpp
    Source/ReverbHeaderComponent.h
    Source/ReverbFooterComponent.cpp
    Source/ReverbFooterComponent.h
    Source/PluginParameter.h
    Source/StandaloneMain.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.h
)

target_include_directories(Reverb PRIVATE "${ATOM_COLLECTIONS_APP_DIR}")

target_compile_definitions(Reverb
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_JACK=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_compile_options(Reverb PRIVATE
    -Wno-switch-enum
    -Wno-overloaded-virtual
)

if(WIN32)
    target_compile_definitions(Reverb PRIVATE JUCE_ASIO=1)
    if(REVERB_ASIO_SDK_INCLUDE)
        target_include_directories(Reverb PRIVATE "${REVERB_ASIO_SDK_INCLUDE}")
    endif()
endif()

target_link_libraries(Reverb PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_atom_theme
    nudsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
