cmake_minimum_required(VERSION 3.22)

project(openGlitchFX VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(OGFX_BUILD_PLUGIN "Build the openGlitchFX VST3 plugin" ON)
option(OGFX_BUILD_TESTS "Build the unit tests" ON)

include(FetchContent)

add_library(openglitch_core STATIC
    Source/Core/BeatStepSequencer.cpp
    Source/Core/GlitchEffects.cpp
    Source/Core/GlitchProcessorCore.cpp
    Source/Core/ModEnvelope.cpp
)
target_include_directories(openglitch_core PUBLIC Source/Core)
set_target_properties(openglitch_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(OGFX_BUILD_PLUGIN)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)

    juce_add_binary_data(OpenGlitchFXAssets
        SOURCES
            assets/alan_marcero_logo.jpg
            assets/analog-cyber-faceplate.png)
    set_target_properties(OpenGlitchFXAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

    juce_add_plugin(openGlitchFX
        COMPANY_NAME "Alan Marcero"
        BUNDLE_ID "com.alanmarcero.openglitchfx"
        PRODUCT_NAME "openGlitchFX"
        PLUGIN_MANUFACTURER_CODE Amrc
        PLUGIN_CODE Ogfx
        FORMATS VST3
        VST3_CATEGORIES Fx Distortion Modulation
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(openGlitchFX PRIVATE
        Source/Plugin/Parameters.cpp
        Source/Plugin/GlitchLookAndFeel.cpp
        Source/Plugin/StepSequencerComponent.cpp
        Source/Plugin/EnvelopeEditorComponent.cpp
        Source/Plugin/DetailPanelComponent.cpp
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp)

    target_include_directories(openGlitchFX PRIVATE
        Source/Core
        Source/Plugin)

    target_compile_definitions(openGlitchFX PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(openGlitchFX
        PRIVATE
            openglitch_core
            OpenGlitchFXAssets
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(OGFX_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.9.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(openglitch_tests
        Tests/BeatSequencerTests.cpp
        Tests/GlitchCoreTests.cpp)

    target_link_libraries(openglitch_tests PRIVATE openglitch_core Catch2::Catch2WithMain)

    enable_testing()
    include(Catch)
    catch_discover_tests(openglitch_tests)
endif()
