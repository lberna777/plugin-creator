cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(MixCompass VERSION 0.1.0)                     # VERSION = unica fonte di verità
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Se un altro progetto dello scaffold ha già scaricato JUCE 8.0.8, lo riusiamo (niente download).
set(_mc_local_juce "${CMAKE_CURRENT_SOURCE_DIR}/../AtmosForge/build/_deps/juce-src")
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${_mc_local_juce}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${_mc_local_juce}")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- Identità: CONGELATA (vedi PLUGIN_SPEC.md) ----
juce_add_plugin(MixCompass
    COMPANY_NAME             "MyPlugins"
    BUNDLE_ID                "com.myplugins.mixcompass"
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE              Mxcp
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "MIX COMPASS"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Analyzer
    AU_SANDBOX_SAFE          TRUE
    HARDENED_RUNTIME_ENABLED FALSE)

juce_generate_juce_header(MixCompass)

# Asset FINALI (MVP: solo il layout, la UI è vettoriale)
juce_add_binary_data(MixCompassAssets SOURCES
    source/assets/layout.json)

set(MC_SOURCES
    source/PluginProcessor.cpp
    source/ReferenceLoader.cpp
    source/PluginEditor.cpp
    source/Views.cpp)

set(MC_DEFINES
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    MC_VERSION_STRING="${PROJECT_VERSION}"
    MC_PRODUCT_NAME="MIX COMPASS")

target_sources(MixCompass PRIVATE ${MC_SOURCES})
target_include_directories(MixCompass PRIVATE source)
target_compile_definitions(MixCompass PUBLIC ${MC_DEFINES})

target_link_libraries(MixCompass PRIVATE
    MixCompassAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ---- Tool headless: stessi sorgenti, senza wrapper plugin ----
# shim/JuceHeader.h sostituisce l'header generato (che esiste solo per il target plugin).
function(mc_add_tool name main)
    juce_add_console_app(${name})
    target_sources(${name} PRIVATE ${main} ${MC_SOURCES})
    target_include_directories(${name} PRIVATE shim source)
    target_compile_definitions(${name} PRIVATE ${MC_DEFINES}
        JUCE_STANDALONE_APPLICATION=1
        PLUGIN_PROCESSOR_CLASS=MixCompassProcessor)
    target_link_libraries(${name} PRIVATE MixCompassAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endfunction()

mc_add_tool(shoot ../tools/shoot/main.cpp)     # render reale dell'editor → PNG
mc_add_tool(plugintest tests/plugintest.cpp)   # null-test, A/B, level match, alloc, SR/bus
mc_add_tool(shoot_demo tools/shoot_demo.cpp)   # render dell editor con dati veri

# Libreria di analisi: test senza JUCE
add_executable(analysis_test tests/analysis_test.cpp)
target_include_directories(analysis_test PRIVATE source)
