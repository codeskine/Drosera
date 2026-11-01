cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(Drosera VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DROSERA_BUILD_PLUGIN "Build the JUCE plugin (needs network for JUCE)" ON)

# --- DaisySP (MIT), pinned to the commit used by the original firmware (7f67768) ---
file(GLOB_RECURSE DAISYSP_SRC CONFIGURE_DEPENDS third_party/DaisySP/Source/*.cpp)
add_library(DaisySP STATIC ${DAISYSP_SRC})
target_include_directories(DaisySP PUBLIC
    third_party/DaisySP/Source
    third_party/DaisySP/Source/Utility)
target_compile_options(DaisySP PRIVATE -w)
# NOTE: vendored headers were patched to include <cstddef>/<cstdint> (the 2023 DaisySP
# commit relied on transitive includes that modern libc++ no longer provides).

# --- Drosera DSP (host independent) ---
add_library(DroseraDSP STATIC
    src/dsp/BiquadFilters.cpp
    src/dsp/FeedbackSynthEngine.cpp
    src/dsp/KarplusString.cpp)
target_include_directories(DroseraDSP PUBLIC src/dsp)
target_link_libraries(DroseraDSP PUBLIC DaisySP)

# --- Offline render test ---
add_executable(render_test tests/render_test.cpp)
target_include_directories(render_test PRIVATE tests src/plugin)
target_link_libraries(render_test PRIVATE DroseraDSP)

# Measures the pitch produced for MIDI notes (see tests/pitch_probe.cpp)
add_executable(pitch_probe tests/pitch_probe.cpp)
target_include_directories(pitch_probe PRIVATE tests)
target_link_libraries(pitch_probe PRIVATE DroseraDSP)

# Renders reference WAVs for the hardware comparison (tools/hardware-comparison/)
add_executable(render_cli tests/render_cli.cpp)
target_link_libraries(render_cli PRIVATE DroseraDSP)

if(DROSERA_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    set(DROSERA_PLUGIN_SOURCES
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/ui/DroseraLookAndFeel.cpp
        src/plugin/ui/PlantView.cpp)

    juce_add_plugin(Drosera
        COMPANY_NAME "codeskine"
        PLUGIN_MANUFACTURER_CODE Cdsk
        PLUGIN_CODE Dros
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "Drosera"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(Drosera PRIVATE ${DROSERA_PLUGIN_SOURCES})
    target_include_directories(Drosera PRIVATE src/plugin)
    target_compile_definitions(Drosera PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Drosera PRIVATE
        DroseraDSP
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # On a clean build the bundles' seals come out broken: JUCE adds files (the VST3's
    # moduleinfo.json) after signing. Sign again (ad hoc) once each bundle is complete.
    if(APPLE)
        foreach(format VST3 AU Standalone)
            add_custom_command(TARGET Drosera_${format} POST_BUILD
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:Drosera_${format}>" VERBATIM)
        endforeach()
    endif()

    # Loads the built VST3 in a JUCE host and renders audio (see tests/plugin_host_test.cpp)
    juce_add_console_app(plugin_host_test PRODUCT_NAME "plugin_host_test")
    target_sources(plugin_host_test PRIVATE tests/plugin_host_test.cpp)
    target_include_directories(plugin_host_test PRIVATE tests)
    target_compile_definitions(plugin_host_test PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(plugin_host_test PRIVATE
        juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_dependencies(plugin_host_test Drosera_VST3)

    # Draws the custom editor straight to a PNG (a hosted VST3 editor is a native view and
    # cannot be snapshotted): editor_snapshot <out.png> [paramId=normalizedValue ...]
    juce_add_console_app(editor_snapshot PRODUCT_NAME "editor_snapshot")
    target_sources(editor_snapshot PRIVATE
        tests/editor_snapshot.cpp ${DROSERA_PLUGIN_SOURCES})
    target_compile_definitions(editor_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(editor_snapshot PRIVATE src/plugin)
    target_link_libraries(editor_snapshot PRIVATE DroseraDSP
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    # The interface's logic and the editor's behaviour (see tests/ui_test.cpp)
    juce_add_console_app(ui_test PRODUCT_NAME "ui_test")
    target_sources(ui_test PRIVATE tests/ui_test.cpp ${DROSERA_PLUGIN_SOURCES})
    target_include_directories(ui_test PRIVATE tests src/plugin)
    target_compile_definitions(ui_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ui_test PRIVATE DroseraDSP
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
