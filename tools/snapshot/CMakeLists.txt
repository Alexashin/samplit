# samplit-snapshot: снимки окна плагина в PNG на тестовом пакете (для проверки интерфейса и для записки)
juce_add_console_app(samplit-snapshot PRODUCT_NAME "samplit-snapshot")

set(SAMPLIT_PLUGIN_DIR ${CMAKE_SOURCE_DIR}/plugin)
target_sources(samplit-snapshot PRIVATE
    main.cpp
    ${SAMPLIT_ENGINE_SOURCES}
    ${SAMPLIT_PLUGIN_DIR}/PluginEditor.cpp
    ${SAMPLIT_PLUGIN_DIR}/HeaderBar.cpp
    ${SAMPLIT_PLUGIN_DIR}/LicensePanel.cpp
    ${SAMPLIT_PLUGIN_DIR}/MixerPanel.cpp
    ${SAMPLIT_PLUGIN_DIR}/KitView.cpp
    ${SAMPLIT_PLUGIN_DIR}/KeyboardView.cpp)

target_include_directories(samplit-snapshot PRIVATE ${SAMPLIT_PLUGIN_DIR})
target_compile_options(samplit-snapshot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(samplit-snapshot PRIVATE
    JucePlugin_Name="Samplit"
    SAMPLIT_LICENSE_ENABLED=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(samplit-snapshot
    PRIVATE
        SamplitAssets
        samplit_core
        samplit_signing
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
