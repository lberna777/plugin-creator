cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)   # universal
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(MikroBridge VERSION 0.1.0)       # VERSION = unica fonte di verità (titolo della finestra)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Per usare copie già scaricate: -DFETCHCONTENT_SOURCE_DIR_JUCE=... -DFETCHCONTENT_SOURCE_DIR_HIDAPI=...
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_Declare(hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG hidapi-0.14.0 GIT_SHALLOW TRUE)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)              # hidapi 0.14 dichiara cmake 3.1.3: CMake 4 lo rifiuta
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)    # Linux: solo hidraw
set(HIDAPI_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JUCE hidapi)

# --- Cuore senza JUCE: protocollo, pad, display. Testato da MikroCoreTests ---
add_executable(MikroCoreTests tests/core_tests.cpp)
target_include_directories(MikroCoreTests PRIVATE source ../shared)

# --- App ponte ---
juce_add_gui_app(MikroBridge
    PRODUCT_NAME   "Mikro Bridge"
    COMPANY_NAME   "MyPlugins"
    BUNDLE_ID      "com.myplugins.mikrobridge"
    VERSION        ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED FALSE)

target_sources(MikroBridge PRIVATE source/Main.cpp source/MainComponent.cpp source/Bridge.cpp)
target_include_directories(MikroBridge PRIVATE source ../shared)

target_compile_definitions(MikroBridge PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MikroBridge,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MikroBridge,JUCE_VERSION>")

target_link_libraries(MikroBridge PRIVATE hidapi::hidapi juce::juce_gui_extra juce::juce_audio_devices
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

if(APPLE)
    target_include_directories(MikroBridge PRIVATE ${hidapi_SOURCE_DIR}/mac)   # hidapi_darwin.h
endif()
