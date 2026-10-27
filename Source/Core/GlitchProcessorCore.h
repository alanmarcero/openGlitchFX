#pragma once

#include "BeatStepSequencer.h"
#include "GlitchEffects.h"
#include "ModEnvelope.h"
#include <array>
#include <cstdint>

namespace openglitch::core {

enum class GlitchFxId : int {
    // Phase 1: Initial 5 Glitch FX
    Bitcrush = 0,
    Downsample = 1,
    Stutter = 2,
    TapeStop = 3,
    RingMod = 4,

    // Phase 2: 5 Additional Glitch FX
    Reverse = 5,
    Chopper = 6,
    PitchGlitch = 7,
    CombReso = 8,
    Wavefold = 9,

    Chorus = 10,
    Flanger = 11,
    Phaser = 12,
    PingPong = 13,
    Saturation = 14,

    Count = 15
};

enum class MixMode : int { Insert = 0, Parallel };
enum class TriggerMode : int { Sequencer = 0, Midi };

// In beats. Index 0 uses the pad span.
inline constexpr std::array<double, 7> kNoteLengthBeats = { 0.0, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };

// In beats. Index 0 lets the amount knob set the slice.
inline constexpr std::array<double, 6> kStutterSliceBeats = { 0.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };

// Notes 60..74 trigger one FX each. Other notes trigger all FX.
inline constexpr int kMidiSoloBaseNote = 60;

struct MeterTelemetry {
    float inputPeakL = 0.0f;
    float inputPeakR = 0.0f;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float activeGate = 0.0f;
    int currentStep = 0;
    float barPhase = 0.0f;
    bool stepEnabled = true;
    bool clipOut = false;
    uint16_t currentStepFxMask = BeatStepSequencer::kAllFxMask;
    std::array<float, BeatStepSequencer::kNumFxSlots> fxEnvelopes{};
    std::array<float, static_cast<size_t>(EnvTarget::Count)> modEnvelopePhases{};
};

class GlitchProcessorCore {
public:
    static constexpr int kNumEffects = static_cast<int>(GlitchFxId::Count);

    GlitchProcessorCore();

    void prepare(double sampleRate);
    void reset();

    // 32-Step Sequencer access (global step enable + per-step 15-FX assignment bitmask)
    void setStep(int stepIndex, bool enabled);
    bool getStep(int stepIndex) const;
    void setStepFxMask(int stepIndex, uint16_t fxMask);
    uint16_t getStepFxMask(int stepIndex) const;
    void setStepFxEnabled(int stepIndex, GlitchFxId fxId, bool enabled);
    bool isStepFxEnabled(int stepIndex, GlitchFxId fxId) const;
    void setAllStepsFxMask(uint16_t fxMask);

    void setAllSteps(bool enabled);
    void setPatternBitmask(uint32_t mask);
    uint32_t getPatternBitmask() const;
    void applyPatternPreset(StepPatternPreset preset);

    // Transport / Tempo Sync
    void setTransport(double bpm, bool useHostSync, bool hasValidHostPpq, double hostPpqPosition);
    void setGateSmoothingMs(float ms);

    // Notes apply only in Midi trigger mode.
    void setTriggerMode(TriggerMode mode, bool syncedStart);
    void queueMidiNote(int sampleOffset, int noteNumber, bool isNoteOn);

    void setStutterSettings(const StutterSettings& settings) { stutterSettings_ = settings; }
    void setTapeStopSettings(const TapeStopSettings& settings, double lengthBeats);
    void setFilter(FilterMode mode, float cutoffHz, float resonance01);
    void setEnvelopeSettings(EnvTarget target, const ModEnvelopeSettings& settings);
    void setEnvelopePoints(EnvTarget target, std::vector<EnvelopePoint> points);
    void setMixMode(MixMode mode) { mixMode_ = mode; }

    // Glitch FX parameters
    void setEffectEnabled(GlitchFxId id, bool enabled);
    bool isEffectEnabled(GlitchFxId id) const;
    void setEffectAmount(GlitchFxId id, float amount01);
    float getEffectAmount(GlitchFxId id) const;

    // Master parameters
    void setMasterMix(float mix01);
    float getMasterMix() const { return masterMix_; }
    void setMasterGainDb(float db);
    float getMasterGainDb() const { return masterGainDb_; }
    void setMasterBypassed(bool bypassed) { masterBypassed_ = bypassed; }
    bool isMasterBypassed() const { return masterBypassed_; }

    void processBlock(float* leftChannel, float* rightChannel, int numSamples);

    MeterTelemetry getTelemetry() const { return telemetry_; }

private:
    struct MidiTrigger {
        int sampleOffset = 0;
        int noteNumber = 0;
        bool isNoteOn = false;
    };
    static constexpr int kMaxQueuedMidi = 256;
    static constexpr int kNumEnvelopes = static_cast<int>(EnvTarget::Count);

    void applyMidiTrigger(const MidiTrigger& trigger);
    float envelopePhase(EnvTarget target, float spanPhase, double ppq) const;

    double sampleRate_ = 44100.0;

    BeatStepSequencer sequencer_;

    // Transport state for current block
    double bpm_ = 120.0;
    bool useHostSync_ = true;
    bool hasValidHostPpq_ = false;
    double blockStartPpq_ = 0.0;

    // Phase 1: 5 Glitch FX
    BitrateCrusher bitcrusher_;
    SampleRateDecimator decimator_;
    BufferStutter stutter_;
    TapeStopEffect tapeStop_;
    RingModEffect ringMod_;

    // Phase 2: 5 Additional Glitch FX (10 Total)
    ReverseGrainEffect reverseGrain_;
    double reverseSpanSec_ = 0.0;
    float reverseSpanPhase_ = 0.0f;
    ChopperGateEffect chopperGate_;
    PitchGlitchEffect pitchGlitch_;
    CombResonatorEffect combResonator_;
    WavefolderEffect wavefolder_;
    ModulatedDelayEffect chorus_;
    ModulatedDelayEffect flanger_;
    PhaserEffect phaser_;
    PingPongEchoEffect pingPong_;
    SaturationEffect saturation_;
    WetFilter wetFilter_;

    StutterSettings stutterSettings_{};
    TapeStopSettings tapeStopSettings_{};
    double tapeLengthBeats_ = 0.0;
    FilterMode filterMode_ = FilterMode::Off;
    float filterCutoffHz_ = 2000.0f;
    float filterResonance_ = 0.2f;
    std::array<ModEnvelope, kNumEnvelopes> envelopes_;
    std::array<ModEnvelopeSettings, kNumEnvelopes> envelopeSettings_{};
    MixMode mixMode_ = MixMode::Insert;

    TriggerMode triggerMode_ = TriggerMode::Sequencer;
    std::array<MidiTrigger, kMaxQueuedMidi> midiQueue_{};
    int midiQueueSize_ = 0;
    std::array<uint8_t, 128> heldNotes_{};

    std::array<bool, kNumEffects> fxEnabled_{};
    std::array<float, kNumEffects> fxAmount_{};

    float masterMix_ = 1.0f;
    float currentMasterMix_ = 1.0f;
    float masterGainDb_ = 0.0f;
    float masterGainLinear_ = 1.0f;
    float currentMasterGainLinear_ = 1.0f;
    bool masterBypassed_ = false;
    bool hasProcessedAudio_ = false;

    MeterTelemetry telemetry_{};
};

} // namespace openglitch::core
