cmake_minimum_required(VERSION 3.22)

project(cigoL VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CIGOL_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to a local JUCE checkout")

if (NOT EXISTS "${CIGOL_JUCE_PATH}/CMakeLists.txt" AND EXISTS "/Users/user/Documents/Fabric/JUCE/CMakeLists.txt")
    set(CIGOL_JUCE_PATH "/Users/user/Documents/Fabric/JUCE" CACHE PATH "Path to a local JUCE checkout" FORCE)
endif()

if (EXISTS "${CIGOL_JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${CIGOL_JUCE_PATH}" JUCE)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

juce_add_gui_app(cigoL
    PRODUCT_NAME "cigoL"
    COMPANY_NAME "cigoL"
    BUNDLE_ID "com.cigol.app"
)

target_sources(cigoL
    PRIVATE
        Source/App/Application.cpp
        Source/App/MainWindow.cpp
        Source/Core/SessionModel.cpp
        Source/Engine/AudioEngine.cpp
        Source/Engine/SuperColliderBridge.cpp
        Source/Main.cpp
        Source/UI/MainComponent.cpp
)

target_compile_definitions(cigoL
    PRIVATE
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(cigoL
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_osc
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
