cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(RSU10 VERSION 0.2.0)             # VERSION = unica fonte di verità (mostrata sulla targhetta)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Per usare un JUCE già scaricato: -DFETCHCONTENT_SOURCE_DIR_JUCE=/percorso/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(RSU10_FORMATS AU VST3 Standalone)
else()
    set(RSU10_FORMATS VST3 Standalone)
endif()

# Identità CONGELATA (PLUGIN_SPEC.md §1): cambiarla fa perdere il plugin ai progetti di FL
juce_add_plugin(RSU10
    COMPANY_NAME             "MyPlugins"
    BUNDLE_ID                "com.myplugins.rsu10"
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE              Rsu1
    FORMATS                  ${RSU10_FORMATS}
    PRODUCT_NAME             "RSU-10"
    DESCRIPTION              "Restomod sampling unit"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    VST3_CATEGORIES          Instrument Sampler Drum
    AU_SANDBOX_SAFE          FALSE
    HARDENED_RUNTIME_ENABLED FALSE)

juce_generate_juce_header(RSU10)

# Asset FINALI (generati da ui/assets.manifest.json; i sorgenti di Gemini in ui/src NON vanno qui) + layout
file(GLOB RSU10_ASSETS CONFIGURE_DEPENDS source/assets/*.png source/assets/*.jpg)
juce_add_binary_data(RSU10Assets SOURCES ${RSU10_ASSETS} source/assets/layout.json)

set(RSU10_CORE source/PadParams.cpp source/Sample.cpp source/Engine.cpp)
target_include_directories(RSU10 PRIVATE ../shared)   # Scales.h, condiviso con Mikro Bridge
target_sources(RSU10 PRIVATE ${RSU10_CORE} source/PluginProcessor.cpp source/PluginEditor.cpp
    source/ui/Skin.cpp source/ui/Widgets.cpp)

target_compile_definitions(RSU10 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RSU10 PRIVATE RSU10Assets juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# --- Banco di prova del motore: suona, choke, strati, stretch, grade, scratch, NaN, CPU ---
juce_add_console_app(RSU10Measure PRODUCT_NAME "RSU10Measure")
juce_generate_juce_header(RSU10Measure)
target_sources(RSU10Measure PRIVATE tests/measure.cpp)
target_include_directories(RSU10Measure PRIVATE source ../shared)
target_link_libraries(RSU10Measure PRIVATE RSU10 RSU10Assets juce::juce_audio_utils
    juce::juce_recommended_config_flags)

# --- Render headless dell'editor: ./RSU10Shoot ui_shot.png ---
juce_add_console_app(RSU10Shoot PRODUCT_NAME "RSU10Shoot")
juce_generate_juce_header(RSU10Shoot)
target_sources(RSU10Shoot PRIVATE ../tools/shoot/main.cpp)
target_include_directories(RSU10Shoot PRIVATE source ../shared)
target_compile_definitions(RSU10Shoot PRIVATE PLUGIN_PROCESSOR_CLASS=RSU10AudioProcessor)
target_link_libraries(RSU10Shoot PRIVATE RSU10 RSU10Assets juce::juce_audio_utils juce::juce_gui_extra
    juce::juce_recommended_config_flags)
