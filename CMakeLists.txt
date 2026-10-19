cmake_minimum_required(VERSION 3.22)
project(purrfectcut VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.16-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_gui_app(PurrfectCut
    COMPANY_NAME "Studio 87"
    PRODUCT_NAME "Purrfect Cut")
target_sources(PurrfectCut PRIVATE src/app/Main.cpp)
target_include_directories(PurrfectCut PRIVATE src)
target_compile_definitions(PurrfectCut PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="Purrfect Cut" JUCE_APPLICATION_VERSION_STRING="0.1.0" JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(PurrfectCut PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
