include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# Тесты собирают движок и процессор плагина без графики: редактор подменён заглушкой
juce_add_console_app(SamplitTests PRODUCT_NAME "SamplitTests")

target_sources(SamplitTests PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin/PadEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugin/PluginProcessor.cpp
    editor_stub.cpp
    juce_setup.cpp
    test_helpers.h
    pad_engine_tests.cpp
    processor_tests.cpp)

target_include_directories(SamplitTests PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
target_compile_options(SamplitTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(SamplitTests PRIVATE
    JucePlugin_Name="Samplit"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(SamplitTests
    PRIVATE
        Catch2::Catch2WithMain
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

catch_discover_tests(SamplitTests)
