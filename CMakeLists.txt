cmake_minimum_required(VERSION 3.22)

project(Assblaster VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if you have one:  cmake -B build -DJUCE_DIR=/path/to/JUCE
# otherwise it is downloaded automatically.
set(JUCE_DIR "" CACHE PATH "Path to an existing JUCE checkout (optional)")
set(JUCE_GIT_TAG "8.0.9" CACHE STRING "JUCE tag to fetch when JUCE_DIR is not set")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JUCE_GIT_TAG}
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Assblaster
    COMPANY_NAME "Unofficial"
    PLUGIN_MANUFACTURER_CODE Unof
    PLUGIN_CODE Asbl
    FORMATS VST3 Standalone
    PRODUCT_NAME "Assblaster"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

target_sources(Assblaster PRIVATE
    Source/AssblasterDSP.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(Assblaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Assblaster
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional: JUCE-free DSP sanity tests (levels, pitch, finiteness, gate).
#   cmake -B build -DASSBLASTER_BUILD_TESTS=ON && cmake --build build --target dsp_test pitch_test
option(ASSBLASTER_BUILD_TESTS "Build the standalone DSP tests" OFF)
if(ASSBLASTER_BUILD_TESTS)
    add_executable(dsp_test   test/dsp_test.cpp)
    add_executable(pitch_test test/pitch_test.cpp)
endif()
