cmake_minimum_required(VERSION 3.22)
project(Chorus VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

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

juce_add_plugin(Chorus
    PLUGIN_MANUFACTURER_CODE Juce
    PLUGIN_CODE Chrs
    FORMATS Standalone
    PRODUCT_NAME "Chorus"
)

juce_generate_juce_header(Chorus)

target_sources(Chorus PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginParameter.h
    Source/StandaloneMain.cpp
)

target_compile_definitions(Chorus
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    PRIVATE
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_link_libraries(Chorus PRIVATE
    juce::juce_audio_utils
    juce::juce_atom_theme
    nudsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)



