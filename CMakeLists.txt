cmake_minimum_required(VERSION 3.22)
project(KyotoVST3QuickBuilder VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(KyotoVST3QuickBuilder
    PRODUCT_NAME "Kyoto VST3 Quick Builder"
    VERSION "1.0.1"
    COMPANY_NAME "Kyoto"
    BUNDLE_ID "com.kyoto.vst3quickbuilder"
    NEEDS_CURL FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE
    BLUETOOTH_PERMISSION_ENABLED FALSE
)

juce_generate_juce_header(KyotoVST3QuickBuilder)

target_sources(KyotoVST3QuickBuilder PRIVATE Source/Main.cpp)

target_compile_definitions(KyotoVST3QuickBuilder PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(KyotoVST3QuickBuilder PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(KyotoVST3QuickBuilder PRIVATE /O2 /Oi /Ot /GL)
    target_link_options(KyotoVST3QuickBuilder PRIVATE /LTCG)
endif()
