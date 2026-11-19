#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace openglitch::suite {

// Groups parameter edits into single UndoManager steps. A slider drag, a pad
// stroke, or a randomize pass each become one undo. Host automation on the
// audio thread is left out of the history.
class ParameterUndo : private juce::AudioProcessorParameter::Listener {
public:
    using ExtraCapture = std::function<juce::ValueTree()>;
    using ExtraApply = std::function<void(const juce::ValueTree&)>;

    ParameterUndo(juce::AudioProcessor& processor, ExtraCapture captureExtra, ExtraApply applyExtra);
    ~ParameterUndo() override;

    juce::UndoManager& manager() { return undo_; }

    void beginGesture(const juce::String& name);
    void endGesture();
    void perform(const juce::String& name, const std::function<void()>& edit);
    void markDirty();

    bool undo();
    bool redo();
    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }

    void suspend();
    void resume();

private:
    struct Snapshot {
        std::vector<float> values;
        juce::ValueTree extra;
    };

    class Action;

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    bool onMessageThread() const;
    Snapshot capture() const;
    void apply(const Snapshot& snapshot);
    static bool same(const Snapshot& a, const Snapshot& b);

    juce::AudioProcessor& processor_;
    ExtraCapture captureExtra_;
    ExtraApply applyExtra_;
    void commit(const juce::String& name);

    juce::UndoManager undo_;
    int apiDepth_ = 0;
    int parameterDepth_ = 0;
    int suspendCount_ = 0;
    bool dirty_ = false;
    bool applying_ = false;
    Snapshot start_;
    juce::String startName_;
};

} // namespace openglitch::suite
