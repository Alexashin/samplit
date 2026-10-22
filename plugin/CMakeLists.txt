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
    PadEngine.h
    PadEngine.cpp
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp)

target_compile_definitions(Samplit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Samplit
    PRIVATE
        SamplitAssets
        juce::juce_audio_utils
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
