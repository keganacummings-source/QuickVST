cmake_minimum_required(VERSION 3.24)
project(KyotoVST3QuickHost VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# JUCE provides the native Windows windowing/audio/plugin-hosting layer.
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# VST3 SDK is fetched by JUCE's CMake integration when VST3 hosting is enabled.
juce_add_gui_app(KyotoVST3QuickHost
    PRODUCT_NAME "Kyoto VST3 Quick Host"
    COMPANY_NAME "Kyoto"
    BUNDLE_ID "com.kyoto.vst3quickhost"
    VERSION "1.0.0"
)

target_sources(KyotoVST3QuickHost PRIVATE
    src/Main.cpp
)

target_compile_definitions(KyotoVST3QuickHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="Kyoto VST3 Quick Host"
    JUCE_APPLICATION_VERSION_STRING="1.0.0"
)

target_link_libraries(KyotoVST3QuickHost PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_extra
)

if (WIN32)
    target_compile_definitions(KyotoVST3QuickHost PRIVATE
        JUCE_PLUGINHOST_VST3=1
    )
endif()
