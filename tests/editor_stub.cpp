#include "PluginProcessor.h"

// В тестах интерфейса нет: настоящий редактор живёт в plugin/PluginEditor.cpp
juce::AudioProcessorEditor* samplit::SamplitProcessor::createEditor()
{
    return nullptr;
}
