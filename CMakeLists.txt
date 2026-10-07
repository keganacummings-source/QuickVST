cmake_minimum_required(VERSION 3.22)
project(KyotoVST3QuickBuilder VERSION 1.0.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

# Reconfigure should not re-clone JUCE. Pair this with a cached FetchContent dir.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
set(FETCHCONTENT_QUIET OFF)

# ccache/sccache cuts repeat builds of the JUCE amalgamation to seconds.
find_program(CCACHE_PROGRAM sccache)
if(NOT CCACHE_PROGRAM)
    find_program(CCACHE_PROGRAM ccache)
endif()
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "" FORCE)
    message(STATUS "Compiler cache: ${CCACHE_PROGRAM}")
endif()

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(KyotoVST3QuickBuilder
    PRODUCT_NAME "Kyoto VST3 Quick Builder"
    VERSION "1.0.4"
    COMPANY_NAME "Kyoto"
    BUNDLE_ID "com.kyoto.vst3quickbuilder"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
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
    JUCE_DISABLE_ASSERTIONS=$<CONFIG:Release>
)

target_link_libraries(KyotoVST3QuickBuilder PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# /GL + /LTCG were the slow path. Keep /O2, drop whole-program optimization.
# Incremental linking is also left off; Ninja + ccache is the fast rebuild path.
if(MSVC)
    target_compile_options(KyotoVST3QuickBuilder PRIVATE /O2 /Oi /MP /Zc:inline)
    target_link_options(KyotoVST3QuickBuilder PRIVATE /INCREMENTAL:NO)
endif()
