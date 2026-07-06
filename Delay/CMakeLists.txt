cmake_minimum_required(VERSION 3.22)
project(Delay VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

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
        set(DELAY_ASIO_SDK_INCLUDE "${ASIO_SDK_PATH}/common")
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

juce_add_module(${CMAKE_SOURCE_DIR}/../third_party/AtomTheme/modules/juce_atom_theme
                ALIAS_NAMESPACE juce)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/../third_party/NuDSP"
               "${CMAKE_BINARY_DIR}/NuDSP")

juce_add_plugin(Delay
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Dlay
    FORMATS Standalone
    PRODUCT_NAME "Delay"
)

juce_generate_juce_header(Delay)

set(ATOM_COLLECTIONS_APP_DIR "${CMAKE_SOURCE_DIR}/../third_party/AtomTheme/demos/collections_app")

target_sources(Delay PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DelayHeaderComponent.cpp
    Source/DelayHeaderComponent.h
    Source/DelayFooterComponent.cpp
    Source/DelayFooterComponent.h
    Source/PluginParameter.h
    Source/StandaloneMain.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.cpp
    ${ATOM_COLLECTIONS_APP_DIR}/AudioSettingsPanel.h
)

target_include_directories(Delay PRIVATE "${ATOM_COLLECTIONS_APP_DIR}")

target_compile_definitions(Delay
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_JACK=1
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

if(WIN32)
    target_compile_definitions(Delay PRIVATE JUCE_ASIO=1)

    if(DELAY_ASIO_SDK_INCLUDE)
        target_include_directories(Delay PRIVATE "${DELAY_ASIO_SDK_INCLUDE}")
    endif()
endif()

target_compile_options(Delay PRIVATE
    -Wno-switch-enum
    -Wno-overloaded-virtual
)

target_link_libraries(Delay PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_atom_theme
    nudsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
