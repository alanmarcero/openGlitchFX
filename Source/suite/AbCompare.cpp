#include "AbCompare.h"

namespace openglitch::suite {

void AbCompare::reset(const juce::ValueTree& state) {
    slotA_ = state.createCopy();
    slotB_ = state.createCopy();
    activeA_ = true;
}

void AbCompare::setSlots(juce::ValueTree slotA, juce::ValueTree slotB, bool activeA) {
    slotA_ = std::move(slotA);
    slotB_ = std::move(slotB);
    activeA_ = activeA;
}

void AbCompare::rememberActive(const juce::ValueTree& current) {
    storeActive(current);
}

void AbCompare::storeActive(const juce::ValueTree& current) {
    if (activeA_) {
        slotA_ = current.createCopy();
    } else {
        slotB_ = current.createCopy();
    }
}

juce::ValueTree AbCompare::toggle(const juce::ValueTree& current) {
    storeActive(current);
    activeA_ = !activeA_;
    return (activeA_ ? slotA_ : slotB_).createCopy();
}

juce::ValueTree AbCompare::copyAtoB(const juce::ValueTree& current) {
    storeActive(current);
    slotB_ = slotA_.createCopy();
    return activeA_ ? juce::ValueTree() : slotB_.createCopy();
}

juce::ValueTree AbCompare::copyBtoA(const juce::ValueTree& current) {
    storeActive(current);
    slotA_ = slotB_.createCopy();
    return activeA_ ? slotA_.createCopy() : juce::ValueTree();
}

} // namespace openglitch::suite
