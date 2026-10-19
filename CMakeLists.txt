cmake_minimum_required(VERSION 3.22)
project(purrfectcut VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core tests (pure C++, no JUCE): ctest, or build/test_core and build/test_more
enable_testing()
add_executable(test_core tests/test_core.cpp)
add_executable(test_more tests/test_more.cpp)
add_executable(test_pitch tests/test_pitch.cpp)
add_executable(test_track tests/test_track.cpp)
add_executable(test_poly tests/test_poly.cpp)
add_executable(test_spectral tests/test_spectral.cpp)
add_executable(test_notes tests/test_notes.cpp)
add_executable(test_session tests/test_session.cpp)
add_test(NAME core COMMAND test_core)
add_test(NAME sampler_and_view COMMAND test_more)
add_test(NAME pitch COMMAND test_pitch)
add_test(NAME track COMMAND test_track)
add_test(NAME poly COMMAND test_poly)
add_test(NAME spectral COMMAND test_spectral)
add_test(NAME notes COMMAND test_notes)
add_test(NAME session COMMAND test_session)

# Desktop app and sampler plugin. Pass -DPC_JUCE_DIR=/path/to/JUCE to use a local JUCE, otherwise JUCE is fetched.
option(PC_BUILD_JUCE "Build the desktop app and the VST3 sampler" ON)
if(PC_BUILD_JUCE)
  if(PC_JUCE_DIR)
    add_subdirectory(${PC_JUCE_DIR} juce)
  else()
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.16-security GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
  endif()

  # 1. The audio editor (desktop app)
  juce_add_gui_app(PurrfectCut COMPANY_NAME "Studio 87" PRODUCT_NAME "Purrfect Cut")
  target_sources(PurrfectCut PRIVATE src/app/Main.cpp)
  target_include_directories(PurrfectCut PRIVATE src)
  target_compile_definitions(PurrfectCut PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="Purrfect Cut" JUCE_APPLICATION_VERSION_STRING="0.2.0" JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(PurrfectCut PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra
      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

  # 2. The sampler with FX (VST3 and standalone). Every control is an automatable parameter.
  juce_add_plugin(PurrfectCutSampler
    COMPANY_NAME "Studio 87"
    PLUGIN_MANUFACTURER_CODE Spkc
    PLUGIN_CODE PcSm
    FORMATS VST3 Standalone
    PRODUCT_NAME "Purrfect Cut Sampler"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(PurrfectCutSampler PRIVATE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
  target_compile_definitions(PurrfectCutSampler PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(PurrfectCutSampler PRIVATE juce::juce_audio_utils juce::juce_audio_formats
      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
