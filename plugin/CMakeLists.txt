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

# Движок и процессор без графики: общий список для плагина, тестов и tools/snapshot
set(SAMPLIT_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Kit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Interpolation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DiskStreamer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DiskStreamer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ClipSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PackSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/KitEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/KitEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KitLoader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/KitLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LicenseManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/LicenseManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Setup.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Setup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Pattern.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Pattern.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PatternPlayer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PatternPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PatternPalette.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PatternPalette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PackCatalog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PackCatalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp)
set(SAMPLIT_ENGINE_SOURCES ${SAMPLIT_ENGINE_SOURCES} PARENT_SCOPE)

target_sources(Samplit PRIVATE
    Brand.h
    LookAndFeel.h
    ${SAMPLIT_ENGINE_SOURCES}
    KeyboardView.h
    KeyboardView.cpp
    MidiDragSource.cpp
    HeaderBar.h
    HeaderBar.cpp
    LicensePanel.h
    LicensePanel.cpp
    MixerPanel.h
    MixerPanel.cpp
    InstrumentsPanel.h
    InstrumentsPanel.cpp
    KitView.h
    KitView.cpp
    PluginEditor.h
    PluginEditor.cpp)

# Исходники в UTF-8: русские строки интерфейса
target_compile_options(Samplit PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# Лицензия отложена: по умолчанию продукт работает без неё, звук берётся из папки WAV («Моя библиотека…»).
# Код лицензии и пакетов остаётся; SAMPLIT_LICENSE=ON включает проверку токена и экран лицензии
option(SAMPLIT_LICENSE "Enforce licence (encrypted .splpack packs); OFF: WAV library only" OFF)
set(SAMPLIT_LICENSE_ENABLED_VALUE 0)
if(SAMPLIT_LICENSE)
    set(SAMPLIT_LICENSE_ENABLED_VALUE 1)
endif()
target_compile_definitions(Samplit PRIVATE SAMPLIT_LICENSE_ENABLED=${SAMPLIT_LICENSE_ENABLED_VALUE})
set(SAMPLIT_LICENSE_ENABLED_VALUE ${SAMPLIT_LICENSE_ENABLED_VALUE} PARENT_SCOPE)

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
