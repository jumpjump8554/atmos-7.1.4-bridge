cmake_minimum_required (VERSION 3.22)

project (AtmosBridge VERSION 2.0.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugin works without installing the VC++ redistributable.
set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include (FetchContent)
FetchContent_Declare (JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable (JUCE)

juce_add_plugin (AtmosBridge
    COMPANY_NAME                "Atmos Bridge"
    PRODUCT_NAME                "Atmos Bridge 7.1.4ch"
    DESCRIPTION                 "12ch (7.1.4) to Windows Spatial Audio (Dolby Atmos for home theater over HDMI)"
    PLUGIN_MANUFACTURER_CODE    Atbr
    PLUGIN_CODE                 Ab74
    FORMATS                     VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Spatial Tools
    BUNDLE_ID                   com.atmosbridge.atmosbridge714)

juce_generate_juce_header (AtmosBridge)

set (AB_SHARED_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UpmixerHost.cpp
    Source/engine/SpatialEngine.cpp
    Source/engine/SpatialFormat.cpp
    Source/engine/LoopbackCapture.cpp
    Source/engine/Platform.cpp)

target_sources (AtmosBridge PRIVATE ${AB_SHARED_SOURCES})

target_include_directories (AtmosBridge PRIVATE Source)

target_compile_definitions (AtmosBridge PUBLIC
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1)   # built-in upmixer slot hosts a VST3 upmixer

target_link_libraries (AtmosBridge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (WIN32)
    target_link_libraries (AtmosBridge PRIVATE ole32 avrt shell32 uuid runtimeobject)
endif()

if (MSVC)
    target_compile_options (AtmosBridge PRIVATE /utf-8)
endif()

#==============================================================================
# Atmos Bridge Host: the same engine as a standalone Windows app (no plugin host needed),
# with the plugin chain, 2ch Stereo output, routing and the tray.
juce_add_gui_app (AtmosBridgeHost
    PRODUCT_NAME    "Atmos Bridge Host"
    COMPANY_NAME    "Atmos Bridge"
    VERSION         ${PROJECT_VERSION}
    ICON_BIG        "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png"
    ICON_SMALL      "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.png")

juce_generate_juce_header (AtmosBridgeHost)

target_sources (AtmosBridgeHost PRIVATE Source/HostApp.cpp ${AB_SHARED_SOURCES})
target_include_directories (AtmosBridgeHost PRIVATE Source)

target_compile_definitions (AtmosBridgeHost PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    AB_HOST_UI=1
    "JucePlugin_Name=\"Atmos Bridge 7.1.4ch\""
    "JucePlugin_VersionString=\"${PROJECT_VERSION}\""
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:AtmosBridgeHost,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:AtmosBridgeHost,JUCE_VERSION>")

target_link_libraries (AtmosBridgeHost
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (WIN32)
    target_link_libraries (AtmosBridgeHost PRIVATE ole32 avrt shell32 uuid runtimeobject)
endif()

if (MSVC)
    target_compile_options (AtmosBridgeHost PRIVATE /utf-8)
endif()
