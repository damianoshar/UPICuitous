cmake_minimum_required(VERSION 3.22)

project(UPICuitous VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

# --- JUCE ---------------------------------------------------------------
# If you have JUCE checked out locally at ./JUCE it will be used,
# otherwise it is downloaded as a zip (no git required).
set(JUCE_VERSION 8.0.6)

include(FetchContent)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using local JUCE at ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory(JUCE)
else()
    message(STATUS "Fetching JUCE ${JUCE_VERSION} as zip (no git needed)...")
    FetchContent_Declare(
        JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/${JUCE_VERSION}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# --- App ----------------------------------------------------------------
juce_add_gui_app(UPICuitous
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "UPICuitous"
    # NB: no hyphens — this doubles as the Android applicationId namespace
    BUNDLE_ID "org.upicuitous"
    PRODUCT_NAME "UPICuitous"
    ICON_BIG ""
    ICON_SMALL ""
)

# On Android juce_add_gui_app() produces libUPICuitous.so instead of an
# executable; the Gradle project under Builds/Android packages it into an APK
# (open Builds/Android in Android Studio, or run its gradlew).

juce_generate_juce_header(UPICuitous)

file(GLOB_RECURSE UPIC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)

target_sources(UPICuitous PRIVATE ${UPIC_SOURCES})

target_compile_definitions(UPICuitous PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(UPICuitous PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# MinGW / GCC specific: JUCE needs these system libs on Windows
if (MINGW)
    target_link_libraries(UPICuitous PRIVATE
        gdi32 user32 kernel32 comdlg32 shell32 ole32 uuid winmm wsock32 ws2_32
        comctl32setupapi version shlwapi dwmapi imm32 opengl32
    )
    target_compile_options(UPICuitous PRIVATE -Wa,-mbig-obj)
endif()

# Binary data (none required for now, placeholder)
# juce_add_binary_data(UPICData SOURCES Resources/*)

install(TARGETS UPICuitous RUNTIME DESTINATION bin)
