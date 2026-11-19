#include "ParameterUndo.h"

namespace openglitch::suite {

class ParameterUndo::Action : public juce::UndoableAction {
public:
    Action(ParameterUndo& owner, Snapshot before, Snapshot after)
        : owner_(owner), before_(std::move(before)), after_(std::move(after)) {}

    bool perform() override {
        if (!appliedOnce_) {
            appliedOnce_ = true;
            return true;
        }
        owner_.apply(after_);
        return true;
    }

    bool undo() override {
        owner_.apply(before_);
        return true;
    }

    int getSizeInUnits() override { return 64; }

private:
    ParameterUndo& owner_;
    Snapshot before_;
    Snapshot after_;
    bool appliedOnce_ = false;
};

ParameterUndo::ParameterUndo(juce::AudioProcessor& processor, ExtraCapture captureExtra, ExtraApply applyExtra)
    : processor_(processor), captureExtra_(std::move(captureExtra)), applyExtra_(std::move(applyExtra)) {
    for (auto* parameter : processor_.getParameters()) {
        parameter->addListener(this);
    }
}

ParameterUndo::~ParameterUndo() {
    for (auto* parameter : processor_.getParameters()) {
        parameter->removeListener(this);
    }
}

bool ParameterUndo::onMessageThread() const {
    auto* messages = juce::MessageManager::getInstanceWithoutCreating();
    return messages != nullptr && messages->isThisTheMessageThread();
}

void ParameterUndo::beginGesture(const juce::String& name) {
    if (suspendCount_ > 0 || applying_ || !onMessageThread() || parameterDepth_ > 0) {
        return;
    }
    if (apiDepth_++ == 0) {
        start_ = capture();
        dirty_ = false;
        startName_ = name;
    }
}

void ParameterUndo::endGesture() {
    if (suspendCount_ > 0 || applying_ || !onMessageThread() || apiDepth_ <= 0) {
        return;
    }
    if (--apiDepth_ == 0) {
        commit(startName_);
    }
}

void ParameterUndo::perform(const juce::String& name, const std::function<void()>& edit) {
    if (suspendCount_ > 0 || applying_ || !onMessageThread()) {
        edit();
        return;
    }
    beginGesture(name);
    edit();
    endGesture();
}

void ParameterUndo::markDirty() {
    if (!applying_ && (apiDepth_ > 0 || parameterDepth_ > 0)) {
        dirty_ = true;
    }
}

void ParameterUndo::commit(const juce::String& name) {
    if (!dirty_) {
        return;
    }
    auto end = capture();
    dirty_ = false;
    if (!same(start_, end)) {
        undo_.beginNewTransaction(name);
        undo_.perform(new Action(*this, std::move(start_), std::move(end)), name);
    }
}

bool ParameterUndo::undo() {
    return onMessageThread() && undo_.undo();
}

bool ParameterUndo::redo() {
    return onMessageThread() && undo_.redo();
}

void ParameterUndo::suspend() { ++suspendCount_; }

void ParameterUndo::resume() { suspendCount_ = std::max(0, suspendCount_ - 1); }

void ParameterUndo::parameterValueChanged(int, float) {
    markDirty();
}

void ParameterUndo::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    if (suspendCount_ > 0 || applying_ || !onMessageThread() || apiDepth_ > 0) {
        return;
    }
    if (gestureIsStarting) {
        if (parameterDepth_++ == 0) {
            start_ = capture();
            dirty_ = false;
            const auto* parameter = processor_.getParameters()[parameterIndex];
            startName_ = parameter != nullptr ? parameter->getName(48) : juce::String("Edit");
        }
        return;
    }
    if (parameterDepth_ > 0 && --parameterDepth_ == 0) {
        commit(startName_);
    }
}

ParameterUndo::Snapshot ParameterUndo::capture() const {
    Snapshot snapshot;
    snapshot.values.reserve(static_cast<size_t>(processor_.getParameters().size()));
    for (auto* parameter : processor_.getParameters()) {
        snapshot.values.push_back(parameter->getValue());
    }
    if (captureExtra_ != nullptr) {
        snapshot.extra = captureExtra_();
    }
    return snapshot;
}

void ParameterUndo::apply(const Snapshot& snapshot) {
    const juce::ScopedValueSetter<bool> guard(applying_, true);
    auto& parameters = processor_.getParameters();
    if (static_cast<int>(snapshot.values.size()) != parameters.size()) {
        return;
    }
    for (int i = 0; i < parameters.size(); ++i) {
        parameters[i]->setValueNotifyingHost(snapshot.values[static_cast<size_t>(i)]);
    }
    if (applyExtra_ != nullptr && snapshot.extra.isValid()) {
        applyExtra_(snapshot.extra);
    }
}

bool ParameterUndo::same(const Snapshot& a, const Snapshot& b) {
    if (a.values.size() != b.values.size()) {
        return false;
    }
    for (size_t i = 0; i < a.values.size(); ++i) {
        if (!juce::approximatelyEqual(a.values[i], b.values[i])) {
            return false;
        }
    }
    if (a.extra.isValid() != b.extra.isValid()) {
        return false;
    }
    return !a.extra.isValid() || a.extra.isEquivalentTo(b.extra);
}

} // namespace openglitch::suite
