cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(MacchinePisello VERSION 1.0.0)               # VERSION = unica fonte di verità
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- Identità: CONGELATA (vedi PLUGIN_SPEC.md) ----
juce_add_plugin(MacchinePisello
    COMPANY_NAME             "MyPlugins"
    BUNDLE_ID                "com.myplugins.macchinepisello"
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE              Mcps
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "Macchine Pisello"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    VST3_CATEGORIES          Instrument Synth
    AU_SANDBOX_SAFE          TRUE
    HARDENED_RUNTIME_ENABLED FALSE)

juce_generate_juce_header(MacchinePisello)

# Asset FINALI (qui solo il layout: la UI è vettoriale)
juce_add_binary_data(MacchinePiselloAssets SOURCES
    source/assets/layout.json)

set(MP_SOURCES
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/LookAndFeel.cpp
    source/Params.cpp
    source/dsp/Machine.cpp)

set(MP_DEFINES
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    MP_VERSION_STRING="${PROJECT_VERSION}"
    MP_PRODUCT_NAME="Macchine Pisello")

target_sources(MacchinePisello PRIVATE ${MP_SOURCES})
target_include_directories(MacchinePisello PRIVATE source)
target_compile_definitions(MacchinePisello PUBLIC ${MP_DEFINES})

target_link_libraries(MacchinePisello PRIVATE
    MacchinePiselloAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ---- Harness headless: stessi sorgenti, senza wrapper plugin ----
# shim/JuceHeader.h sostituisce l'header generato (che esiste solo per il target plugin).
function(mp_add_tool name main)
    juce_add_console_app(${name})
    target_sources(${name} PRIVATE ${main} ${MP_SOURCES})
    target_include_directories(${name} PRIVATE shim source)
    target_compile_definitions(${name} PRIVATE ${MP_DEFINES}
        JUCE_STANDALONE_APPLICATION=1
        PLUGIN_PROCESSOR_CLASS=MacchinePiselloProcessor)
    target_link_libraries(${name} PRIVATE MacchinePiselloAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endfunction()

mp_add_tool(shoot ../tools/shoot/main.cpp)      # render reale dell'editor → PNG
mp_add_tool(synthtest tests/synthtest.cpp)      # silenzio, determinismo, alloc, SR/bus, demo WAV
