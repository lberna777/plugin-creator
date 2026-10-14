# VOCAL FORGE — build AU + VST3 + Standalone.
#
#   macOS (quello che serve per Logic):
#       cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
#       cmake --build build --config Release
#   oppure, più semplice: ./build_mac.sh
#
# JUCE: se non lo trovi già sul disco viene scaricato da GitHub (FetchContent).
cmake_minimum_required(VERSION 3.22)
project(VOCAL_FORGE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- JUCE
set(VF_JUCE_PATH "" CACHE PATH "Percorso di una copia locale di JUCE (opzionale)")
if(VF_JUCE_PATH AND EXISTS "${VF_JUCE_PATH}/CMakeLists.txt")
    message(STATUS "VOCAL FORGE: uso JUCE da ${VF_JUCE_PATH}")
    add_subdirectory("${VF_JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    message(STATUS "VOCAL FORGE: scarico JUCE 8.0.4 (solo la prima volta)")
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------- regole imbarcate
# Il motore C++ legge LO STESSO rules.json del compilatore Python: è così che la parità
# resta vera senza doverla mantenere a mano.
juce_add_binary_data(VocalForgeData
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../tools/data/rules.json)

# ------------------------------------------------------------ plugin
juce_add_plugin(VocalForge
    PRODUCT_NAME                "VOCAL FORGE"
    COMPANY_NAME                "MyPlugins"
    BUNDLE_ID                   "com.myplugins.vocalforge"
    PLUGIN_MANUFACTURER_CODE    Mypl
    PLUGIN_CODE                 Vfrg
    FORMATS                     AU VST3 Standalone
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Dynamics EQ
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE          # c'è un campo di testo: serve davvero
    COPY_PLUGIN_AFTER_BUILD     TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "VOCAL FORGE usa il microfono solo nella versione standalone."
    DESCRIPTION                 "Vocal chain compilata da una richiesta testuale")

target_sources(VocalForge PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/RulesEngine.cpp
    source/ChainDsp.cpp)

target_compile_definitions(VocalForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(VocalForge PRIVATE
    VocalForgeData
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------- test di parità
juce_add_console_app(vocalforge_parity PRODUCT_NAME "vocalforge_parity")
target_sources(vocalforge_parity PRIVATE tools/parity_main.cpp source/RulesEngine.cpp)
target_compile_definitions(vocalforge_parity PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(vocalforge_parity PRIVATE juce::juce_core juce::juce_events)

# ------------------------------------------------- self test (headless)
juce_add_console_app(vocalforge_selftest PRODUCT_NAME "vocalforge_selftest")
target_sources(vocalforge_selftest PRIVATE
    tools/selftest_main.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/ChainDsp.cpp
    source/RulesEngine.cpp)
target_compile_definitions(vocalforge_selftest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    JucePlugin_Name="VOCAL FORGE" JucePlugin_IsSynth=0 JucePlugin_IsMidiEffect=0
    JucePlugin_WantsMidiInput=0 JucePlugin_ProducesMidiOutput=0)
target_link_libraries(vocalforge_selftest PRIVATE
    VocalForgeData juce::juce_audio_utils juce::juce_dsp)
