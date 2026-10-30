# VST3 target for UPICuitous (v1.27).
#
# This is a separate CMake project from the repo-root CMakeLists.txt (which
# builds the stand-alone app and, via Builds/Android, the APK). It only builds
# the plugin; the UI and engine come from ../Source unchanged.
#
#   cmake -S Plugin -B Builds/Plugin -G "Visual Studio 18 2026" -A x64
#   cmake --build Builds/Plugin --config Release --parallel 4
#
# Artefact: Builds/Plugin/UPICuitousPlugin_artefacts/Release/VST3/UPICuitous.vst3

cmake_minimum_required(VERSION 3.22)

project(UPICuitousPlugin VERSION 0.28.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the JUCE copy used by the app builds
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

juce_add_plugin(UPICuitousPlugin
    PROJECT_NAME "UPICuitous"
    PRODUCT_NAME "UPICuitous"
    COMPANY_NAME "UPICuitous"
    BUNDLE_ID "org.upicuitous"
    PLUGIN_MANUFACTURER_CODE Xenk
    PLUGIN_CODE Upc1
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(UPICuitousPlugin PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/MainComponent.cpp)

target_include_directories(UPICuitousPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)

# same module configuration as the Projucer app project
target_compile_definitions(UPICuitousPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(UPICuitousPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(UPICuitousPlugin)

# Optional dev smoke test: drives the wrapper like a host would (no DAW
# needed). Excluded from normal builds - run it explicitly with
#   cmake --build Builds/Plugin --config Release --target UPICuitousPluginSmokeTest
juce_add_console_app(UPICuitousPluginSmokeTest PRODUCT_NAME "UPICuitous smoke")
set_target_properties(UPICuitousPluginSmokeTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_sources(UPICuitousPluginSmokeTest PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    PluginSmokeTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/MainComponent.cpp)

target_include_directories(UPICuitousPluginSmokeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)

target_compile_definitions(UPICuitousPluginSmokeTest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(UPICuitousPluginSmokeTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(UPICuitousPluginSmokeTest)
