cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(AtmosForge VERSION 1.0.0)                      # VERSION = unica fonte di verità
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AtmosForge
    COMPANY_NAME             "MyPlugins"
    BUNDLE_ID                "com.myplugins.atmosforge"
    PLUGIN_MANUFACTURER_CODE Mypl                      # 4 char, congelato
    PLUGIN_CODE              Atfg                      # 4 char, congelato
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "ATMOS FORGE"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE          TRUE
    HARDENED_RUNTIME_ENABLED FALSE)

juce_generate_juce_header(AtmosForge)

# Asset FINALI (layout normalizzato). Nessun PNG: la UI è vettoriale, disegnata a codice.
juce_add_binary_data(AtmosForgeAssets SOURCES
    source/assets/layout.json)

target_sources(AtmosForge PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/LookAndFeel.cpp
    source/engine/Engine.cpp
    source/engine/SceneRoad.cpp
    source/engine/SceneFire.cpp
    source/engine/SceneWeather.cpp
    source/engine/SceneSteps.cpp)

target_compile_definitions(AtmosForge PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_include_directories(AtmosForge PRIVATE source)

target_link_libraries(AtmosForge
    PRIVATE AtmosForgeAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# --- Harness screenshot headless (loop di feedback visivo) ---
juce_add_console_app(shoot)
target_sources(shoot PRIVATE
    ../tools/shoot/main.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/LookAndFeel.cpp
    source/engine/Engine.cpp
    source/engine/SceneRoad.cpp
    source/engine/SceneFire.cpp
    source/engine/SceneWeather.cpp
    source/engine/SceneSteps.cpp)
juce_generate_juce_header(shoot)
target_include_directories(shoot PRIVATE source)
target_compile_definitions(shoot PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JucePlugin_Name="ATMOS FORGE"
    JucePlugin_VersionString="1.0.0"
    PLUGIN_PROCESSOR_CLASS=AtmosForgeAudioProcessor)
target_link_libraries(shoot PRIVATE AtmosForgeAssets juce::juce_audio_utils
                                    juce::juce_dsp juce::juce_gui_extra
                                    juce::juce_recommended_config_flags)

# --- Banco di prova offline (render WAV di ogni scena + misure) ---
juce_add_console_app(probe)
target_sources(probe PRIVATE
    tools/probe/main.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/LookAndFeel.cpp
    source/engine/Engine.cpp
    source/engine/SceneRoad.cpp
    source/engine/SceneFire.cpp
    source/engine/SceneWeather.cpp
    source/engine/SceneSteps.cpp)
juce_generate_juce_header(probe)
target_include_directories(probe PRIVATE source)
target_compile_definitions(probe PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JucePlugin_Name="ATMOS FORGE"
    JucePlugin_VersionString="1.0.0"
)
target_link_libraries(probe PRIVATE AtmosForgeAssets juce::juce_audio_utils
                                    juce::juce_dsp juce::juce_gui_extra
                                    juce::juce_recommended_config_flags)
