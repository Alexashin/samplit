# samplit-packer: сборка и проверка пакетов .splpack. Формат и логика в core/pack (samplit_core)
juce_add_console_app(samplit-packer PRODUCT_NAME "samplit-packer")

target_sources(samplit-packer PRIVATE main.cpp)
target_compile_options(samplit-packer PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(samplit-packer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(samplit-packer
    PRIVATE
        samplit_core
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
