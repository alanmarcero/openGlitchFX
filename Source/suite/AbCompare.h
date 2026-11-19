#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace openglitch::suite {

// Two full state slots. The live editor state is captured into the active
// slot when you toggle or copy.
class AbCompare {
public:
    void reset(const juce::ValueTree& state);
    void setSlots(juce::ValueTree slotA, juce::ValueTree slotB, bool activeA);
    void rememberActive(const juce::ValueTree& current);

    bool isA() const { return activeA_; }
    juce::ValueTree slotA() const { return slotA_; }
    juce::ValueTree slotB() const { return slotB_; }

    // Stores the live state in the active slot and returns the other slot.
    juce::ValueTree toggle(const juce::ValueTree& current);

    // Copies one slot onto the other. When the destination is active, the
    // returned tree should replace the live state. An invalid tree means the
    // live state stays as it is.
    juce::ValueTree copyAtoB(const juce::ValueTree& current);
    juce::ValueTree copyBtoA(const juce::ValueTree& current);

private:
    void storeActive(const juce::ValueTree& current);

    juce::ValueTree slotA_;
    juce::ValueTree slotB_;
    bool activeA_ = true;
};

} // namespace openglitch::suite
