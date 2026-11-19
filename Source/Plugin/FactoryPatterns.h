#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace openglitch::patterns {

int count();
int defaultIndex();
juce::String name(int index);
juce::String group(int index);
bool stepsAreValid();
void apply(juce::AudioProcessorValueTreeState& state, int index);

} // namespace openglitch::patterns
