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
    ${SAMPLIT_ENGINE_SOURCES}
    editor_stub.cpp
    juce_setup.cpp
    kit_fixture.h
    kit_engine_tests.cpp
    kit_loader_tests.cpp
    license_manager_tests.cpp
    processor_tests.cpp
    core_tests.cpp
    pack_tests.cpp
    streaming_tests.cpp
    real_library_tests.cpp
    license_tests.cpp)

target_include_directories(SamplitTests PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
target_compile_options(SamplitTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(SamplitTests PRIVATE
    JucePlugin_Name="Samplit"
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    SAMPLIT_LICENSE_ENABLED=${SAMPLIT_LICENSE_ENABLED_VALUE}
    SAMPLIT_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures"
    JUCE_USE_CURL=0)

target_link_libraries(SamplitTests
    PRIVATE
        Catch2::Catch2WithMain
        samplit_core
        samplit_signing
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

catch_discover_tests(SamplitTests)
