set(SAMPLIT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SAMPLIT_FORMATS AU)
endif()

# Копировать собранный плагин в пользовательскую папку VST3, чтобы DAW увидела его без прав администратора
option(SAMPLIT_COPY_PLUGIN "Copy the VST3 to the user plugin folder after build" ON)
if(WIN32)
    set(SAMPLIT_VST3_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
endif()

juce_add_binary_data(SamplitAssets
    SOURCES ${CMAKE_SOURCE_DIR}/brand/logo/lockup-h-on-dark.svg)
set_target_properties(SamplitAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Samplit
    COMPANY_NAME                "Samplit"
    PLUGIN_MANUFACTURER_CODE    Smpl
    PLUGIN_CODE                 Spl1
    FORMATS                     ${SAMPLIT_FORMATS}
    PRODUCT_NAME                "Samplit"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    VST3_CATEGORIES             Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD     ${SAMPLIT_COPY_PLUGIN}
    VST3_COPY_DIR               "${SAMPLIT_VST3_DIR}")

target_sources(Samplit PRIVATE
    Brand.h
    LookAndFeel.h
    Sample.h
    Instrument.h
    SamplerEngine.h
    SamplerEngine.cpp
    Kit.h
    KitLoader.h
    KitLoader.cpp
    LicenseManager.h
    LicenseManager.cpp
    PresetLibrary.h
    PresetLibrary.cpp
    KitEngine.h
    KitEngine.cpp
    DiskStreamer.cpp
    PluginProcessor.h
    PluginProcessor.cpp
    KeyboardView.h
    KeyboardView.cpp
    PresetPanel.h
    PresetPanel.cpp
    LicensePanel.h
    LicensePanel.cpp
    MixerPanel.h
    MixerPanel.cpp
    KitView.h
    KitView.cpp
    PluginEditor.h
    PluginEditor.cpp)

# Исходники в UTF-8: русские строки интерфейса
target_compile_options(Samplit PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# Служебный режим (ручная раскладка сэмплов) есть в Debug всегда, в Release только по опции
option(SAMPLIT_SERVICE_MODE "Show the service (manual mapping) screen in Release builds" OFF)
# Локальная лицензия без сервера (код компьютера и файл лицензии). Временно, пока нет сервера: в проде выключить
option(SAMPLIT_OFFLINE_LICENSE "Offline licence: machine code and licence file import" ON)
target_compile_definitions(Samplit PRIVATE
    SAMPLIT_OFFLINE_LICENSE=$<BOOL:${SAMPLIT_OFFLINE_LICENSE}>
    SAMPLIT_SERVICE_MODE=$<IF:$<OR:$<CONFIG:Debug>,$<BOOL:${SAMPLIT_SERVICE_MODE}>>,1,0>)

target_compile_definitions(Samplit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Samplit
    PRIVATE
        SamplitAssets
        samplit_core
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
