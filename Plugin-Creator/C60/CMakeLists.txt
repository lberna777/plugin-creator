cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(C60 VERSION 0.2.0)               # VERSION = unica fonte di verità
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Per usare un JUCE già scaricato: -DFETCHCONTENT_SOURCE_DIR_JUCE=/percorso/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(C60_FORMATS AU VST3 Standalone)
else()
    set(C60_FORMATS VST3 Standalone)
endif()

# Identità: PROPOSTA, da congelare prima del primo uso in un progetto (vedi PLUGIN_SPEC.md §1)
juce_add_plugin(C60
    COMPANY_NAME             "MyPlugins"
    BUNDLE_ID                "com.myplugins.c60"
    PLUGIN_MANUFACTURER_CODE Mypl
    PLUGIN_CODE              Cs60
    FORMATS                  ${C60_FORMATS}
    PRODUCT_NAME             "C-60"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    AU_SANDBOX_SAFE          TRUE
    HARDENED_RUNTIME_ENABLED FALSE)

juce_generate_juce_header(C60)

# Asset FINALI + layout.json (generati da ui/assets.manifest.json; i sorgenti Gemini in ui/src NON vanno qui)
file(GLOB C60_ASSETS CONFIGURE_DEPENDS source/assets/*.png source/assets/*.jpg)
juce_add_binary_data(C60Assets SOURCES ${C60_ASSETS} source/assets/layout.json)

target_sources(C60 PRIVATE source/PluginProcessor.cpp source/PluginEditor.cpp)

target_compile_definitions(C60 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(C60 PRIVATE C60Assets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# --- Banco di misura offline: null-test, risposta, THD, fruscio, wow & flutter, robustezza ---
juce_add_console_app(C60Measure PRODUCT_NAME "C60Measure")
juce_generate_juce_header(C60Measure)
target_sources(C60Measure PRIVATE tests/measure.cpp)
target_include_directories(C60Measure PRIVATE source)
target_link_libraries(C60Measure PRIVATE C60 C60Assets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)

# --- Render headless dell'editor (loop di feedback visivo): ./shoot ui_shot.png ---
juce_add_console_app(C60Shoot PRODUCT_NAME "C60Shoot")
juce_generate_juce_header(C60Shoot)
target_sources(C60Shoot PRIVATE ../tools/shoot/main.cpp)
target_include_directories(C60Shoot PRIVATE source)
target_compile_definitions(C60Shoot PRIVATE PLUGIN_PROCESSOR_CLASS=C60AudioProcessor)
target_link_libraries(C60Shoot PRIVATE C60 C60Assets juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    juce::juce_recommended_config_flags)
