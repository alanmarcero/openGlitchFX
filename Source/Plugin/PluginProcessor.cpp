#include "PluginProcessor.h"
#include "FactoryPatterns.h"
#include "PluginEditor.h"
#include <cmath>

namespace {

const juce::Identifier kEnvelopesNode{ "ENVELOPES" };

juce::Identifier envelopePropertyId(openglitch::core::EnvTarget target) {
    return juce::Identifier("env" + juce::String(static_cast<int>(target)));
}

juce::String encodePoints(const std::vector<openglitch::core::EnvelopePoint>& points) {
    juce::StringArray pairs;
    for (const auto& p : points) {
        pairs.add(juce::String(p.x, 4) + "," + juce::String(p.y, 4));
    }
    return pairs.joinIntoString(";");
}

std::vector<openglitch::core::EnvelopePoint> decodePoints(const juce::String& text) {
    std::vector<openglitch::core::EnvelopePoint> points;
    for (const auto& pair : juce::StringArray::fromTokens(text, ";", {})) {
        const auto x = pair.upToFirstOccurrenceOf(",", false, false).getFloatValue();
        const auto y = pair.fromFirstOccurrenceOf(",", false, false).getFloatValue();
        points.push_back({ std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f) });
    }
    return points;
}

} // namespace

namespace {
std::vector<openglitch::suite::FactoryPresetInfo> factoryPresetInfos() {
    std::vector<openglitch::suite::FactoryPresetInfo> infos;
    infos.reserve(static_cast<size_t>(openglitch::patterns::count()));
    for (int i = 0; i < openglitch::patterns::count(); ++i) {
        infos.push_back({ openglitch::patterns::name(i), openglitch::patterns::group(i) });
    }
    return infos;
}
} // namespace

OpenGlitchFXProcessor::OpenGlitchFXProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("OpenGlitchFXParameters"), openglitch::params::createParameterLayout()),
      undo_(*this,
            [this] { return captureExtra(); },
            [this](const juce::ValueTree& extra) { applyExtra(extra); }),
      presets_("openGlitchFX", factoryPresetInfos()) {
    using openglitch::core::EnvTarget;
    for (int env = 0; env < static_cast<int>(EnvTarget::Count); ++env) {
        const auto target = static_cast<EnvTarget>(env);
        setEnvelopePoints(target, openglitch::core::ModEnvelope::defaultPoints(target));
    }
    for (int i = 0; i < 32; ++i) {
        stepParams_[static_cast<size_t>(i)] = parameters.getRawParameterValue(openglitch::params::stepParamId(i));
        stepFxParams_[static_cast<size_t>(i)] = parameters.getRawParameterValue(openglitch::params::stepFxParamId(i));
        stepExtraFxParams_[static_cast<size_t>(i)] = parameters.getRawParameterValue(openglitch::params::stepExtraFxParamId(i));
    }
    undo_.suspend();
    applyFactory(openglitch::patterns::defaultIndex());
    ab_.reset(slotState());
    undo_.resume();
    undo_.onEdit = [this] {
        if (!loadingPreset_) {
            presetEdited_ = true;
        }
    };
}

std::vector<openglitch::core::EnvelopePoint> OpenGlitchFXProcessor::getEnvelopePoints(openglitch::core::EnvTarget target) const {
    const auto node = parameters.state.getChildWithName(kEnvelopesNode);
    const auto points = decodePoints(node.getProperty(envelopePropertyId(target)).toString());
    return points.empty() ? openglitch::core::ModEnvelope::defaultPoints(target) : points;
}

void OpenGlitchFXProcessor::setEnvelopePoints(openglitch::core::EnvTarget target,
                                              const std::vector<openglitch::core::EnvelopePoint>& points) {
    auto node = parameters.state.getOrCreateChildWithName(kEnvelopesNode, nullptr);
    node.setProperty(envelopePropertyId(target), encodePoints(points), nullptr);
    core_.setEnvelopePoints(target, points);
    undo_.markDirty();
}

void OpenGlitchFXProcessor::syncEnvelopePointsToCore() {
    using openglitch::core::EnvTarget;
    for (int env = 0; env < static_cast<int>(EnvTarget::Count); ++env) {
        const auto target = static_cast<EnvTarget>(env);
        core_.setEnvelopePoints(target, getEnvelopePoints(target));
    }
}

void OpenGlitchFXProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    core_.prepare(sampleRate);
    visualBeat_.store(0.0f, std::memory_order_relaxed);
    syncParametersToCore();
}

void OpenGlitchFXProcessor::releaseResources() {
    core_.reset();
}

bool OpenGlitchFXProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo()) {
        return false;
    }

    return mainIn == mainOut;
}

void OpenGlitchFXProcessor::syncParametersToCore() {
    using namespace openglitch::params;
    using openglitch::core::GlitchFxId;

    // 1. 32-Step Beat Sequencer Pattern + Per-Pad 15-FX Routing Bitmask
    for (int i = 0; i < 32; ++i) {
        const auto idx = static_cast<size_t>(i);
        if (stepParams_[idx] != nullptr) {
            core_.setStep(i, stepParams_[idx]->load() > 0.5f);
        }
        uint16_t mask = 0u;
        if (stepFxParams_[idx] != nullptr) {
            mask |= static_cast<uint16_t>(std::lround(stepFxParams_[idx]->load())) & 0x3FFu;
        }
        if (stepExtraFxParams_[idx] != nullptr) {
            mask |= static_cast<uint16_t>((static_cast<uint32_t>(std::lround(stepExtraFxParams_[idx]->load())) & 0x1Fu) << 10u);
        }
        core_.setStepFxMask(i, mask);
    }

    // 2. Gate smoothing
    const float smoothMs = *parameters.getRawParameterValue(kSmoothMs);
    core_.setGateSmoothingMs(smoothMs);
    core_.setSequenceLength(static_cast<int>(std::round(*parameters.getRawParameterValue(kSequenceLength))));
    core_.setSequencerSwing(*parameters.getRawParameterValue(kSequencerSwing));
    constexpr float kRates[] = { 0.5f, 1.0f, 2.0f };
    const int rateIdx = std::clamp(static_cast<int>(std::round(*parameters.getRawParameterValue(kPatternRate))), 0, 2);
    core_.setPatternRate(kRates[rateIdx]);
    core_.setPatternChance(*parameters.getRawParameterValue(kPatternChance));
    core_.setKeepSubEnabled(*parameters.getRawParameterValue(kKeepSub) > 0.5f);

    // 3. All 15 Glitch FX
    auto syncFx = [&](GlitchFxId id, const char* amtId, const char* onId) {
        const float amt = *parameters.getRawParameterValue(amtId) * 0.01f;
        const bool on = *parameters.getRawParameterValue(onId) > 0.5f;
        core_.setEffectAmount(id, amt);
        core_.setEffectEnabled(id, on);
    };

    // Row 1 (Phase 1: 5 FX)
    syncFx(GlitchFxId::Bitcrush,    kBitcrushAmt,    kBitcrushOn);
    syncFx(GlitchFxId::Downsample,  kDownsampleAmt,  kDownsampleOn);
    syncFx(GlitchFxId::Stutter,     kStutterAmt,     kStutterOn);
    syncFx(GlitchFxId::TapeStop,    kTapeStopAmt,    kTapeStopOn);
    syncFx(GlitchFxId::RingMod,     kRingModAmt,     kRingModOn);

    // Row 2 (Phase 2: 5 Additional FX -> 10 Total)
    syncFx(GlitchFxId::Reverse,     kReverseAmt,     kReverseOn);
    syncFx(GlitchFxId::Chopper,     kChopperAmt,     kChopperOn);
    syncFx(GlitchFxId::PitchGlitch, kPitchGlitchAmt, kPitchGlitchOn);
    syncFx(GlitchFxId::CombReso,    kCombResoAmt,    kCombResoOn);
    syncFx(GlitchFxId::Wavefold,    kWavefoldAmt,    kWavefoldOn);

    syncFx(GlitchFxId::Chorus, kChorusAmt, kChorusOn);
    syncFx(GlitchFxId::Flanger, kFlangerAmt, kFlangerOn);
    syncFx(GlitchFxId::Phaser, kPhaserAmt, kPhaserOn);
    syncFx(GlitchFxId::PingPong, kPingPongAmt, kPingPongOn);
    syncFx(GlitchFxId::Saturation, kSaturationAmt, kSaturationOn);

    // 4. Buffer, tape, filter, envelopes, trigger
    using namespace openglitch::core;
    auto choice = [&](const char* id) { return static_cast<int>(std::round(parameters.getRawParameterValue(id)->load())); };
    auto flag = [&](const char* id) { return parameters.getRawParameterValue(id)->load() > 0.5f; };
    auto value = [&](const char* id) { return parameters.getRawParameterValue(id)->load(); };

    StutterSettings stutter;
    stutter.syncedSliceBeats = kStutterSliceBeats[static_cast<size_t>(std::clamp(choice(kStutterSync), 0, 5))];
    stutter.advance = flag(kStutterAdvance);
    stutter.reverse = static_cast<StutterReverse>(choice(kStutterReverse));
    stutter.gate = value(kStutterGate) * 0.01f;
    stutter.pitchStepSemitones = value(kStutterPitch);
    core_.setStutterSettings(stutter);

    TapeStopSettings tape;
    tape.direction = static_cast<TapeDirection>(choice(kTapeDirection));
    tape.stepped = choice(kTapeCurve) == 1;
    tape.slip = flag(kTapeSlip);
    tape.lowpass = flag(kTapeLowpass);
    tape.fade = flag(kTapeFade);
    core_.setTapeStopSettings(tape, kNoteLengthBeats[static_cast<size_t>(std::clamp(choice(kTapeLength), 0, 6))]);

    core_.setFilter(static_cast<FilterMode>(choice(kFilterMode)), value(kFilterCutoff), value(kFilterReso) * 0.01f);

    for (size_t env = 0; env < kEnvDescriptors.size(); ++env) {
        const auto& desc = kEnvDescriptors[env];
        const auto lengthBeats = kNoteLengthBeats[static_cast<size_t>(std::clamp(choice(desc.rateParamId), 0, 6))];
        core_.setEnvelopeSettings(static_cast<EnvTarget>(env), { flag(desc.onParamId), value(desc.depthParamId) * 0.01f, lengthBeats });
    }

    core_.setTriggerMode(static_cast<TriggerMode>(choice(kTriggerMode)), choice(kTriggerStart) == 1);
    core_.setMixMode(static_cast<MixMode>(choice(kMixMode)));

    // 5. Master
    core_.setMasterMix(*parameters.getRawParameterValue(kMasterMix) * 0.01f);
    core_.setMasterGainDb(*parameters.getRawParameterValue(kMasterGain));
    core_.setMasterBypassed(*parameters.getRawParameterValue(kMasterPower) < 0.5f);
}

void OpenGlitchFXProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    syncParametersToCore();

    // Query Host Playhead for Beat Sync
    const bool useHostSync = *parameters.getRawParameterValue(openglitch::params::kHostSync) > 0.5f;
    double bpm = static_cast<double>(*parameters.getRawParameterValue(openglitch::params::kInternalBpm));
    bool transportStopped = false;
    bool hasValidHostPpq = false;
    double hostPpq = 0.0;

    if (auto* playHead = getPlayHead()) {
        if (const auto pos = playHead->getPosition()) {
            transportStopped = useHostSync && !pos->getIsPlaying();
            if (const auto hostBpm = pos->getBpm(); useHostSync && hostBpm) {
                if (*hostBpm > 10.0) {
                    bpm = *hostBpm;
                }
            }
            // Hosts keep reporting a frozen PPQ while stopped. Use it only while playing, so the internal clock runs otherwise.
            if (const auto ppq = pos->getPpqPosition(); ppq && pos->getIsPlaying()) {
                hostPpq = *ppq;
                hasValidHostPpq = true;
            }
        }
    }

    core_.setTransport(bpm, useHostSync, hasValidHostPpq, hostPpq);

    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (message.isNoteOnOrOff()) {
            core_.queueMidiNote(metadata.samplePosition, message.getNoteNumber(), message.isNoteOn());
        }
    }

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch) {
        buffer.clear(ch, 0, numSamples);
    }

    if (numSamples == 0 || totalNumInputChannels == 0) {
        return;
    }

    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = (totalNumInputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    core_.processBlock(leftChannel, rightChannel, numSamples);
    if (!transportStopped) {
        const double ppq = core_.getClockPpq();
        visualBeat_.store(static_cast<float>(ppq - std::floor(ppq)), std::memory_order_relaxed);
    }
}

double OpenGlitchFXProcessor::getTailLengthSeconds() const {
    const bool echoEnabled = parameters.getRawParameterValue(openglitch::params::kPingPongOn)->load() > 0.5f;
    const float amount = parameters.getRawParameterValue(openglitch::params::kPingPongAmt)->load();
    return echoEnabled && amount > 0.01f ? 40.0 : 0.0;
}

juce::AudioProcessorEditor* OpenGlitchFXProcessor::createEditor() {
    return new OpenGlitchFXEditor(*this);
}

int OpenGlitchFXProcessor::getNumPrograms() {
    return openglitch::patterns::count();
}

int OpenGlitchFXProcessor::getCurrentProgram() {
    return juce::jlimit(0, std::max(0, getNumPrograms() - 1), currentProgram_);
}

void OpenGlitchFXProcessor::setCurrentProgram(int index) {
    if (!juce::isPositiveAndBelow(index, getNumPrograms())) {
        return;
    }
    // A host often repeats the current program after restoring a session.
    // Applying it again would replace the saved sound with the factory pattern.
    if (programApplied_ && index == currentProgram_) {
        return;
    }
    loadFactoryProgram(index);
}

const juce::String OpenGlitchFXProcessor::getProgramName(int index) {
    return openglitch::patterns::name(index);
}

void OpenGlitchFXProcessor::changeProgramName(int, const juce::String&) {}

void OpenGlitchFXProcessor::applyFactory(int index) {
    openglitch::patterns::apply(parameters, index);
    resetEnvelopes();
    currentProgram_ = index;
    programApplied_ = true;
    presetIsFactory_ = true;
    presetLabel_ = openglitch::patterns::name(index);
    presetEdited_ = false;
    syncParametersToCore();
    ++stateVersion_;
}

void OpenGlitchFXProcessor::resetEnvelopes() {
    using openglitch::core::EnvTarget;
    for (int env = 0; env < static_cast<int>(EnvTarget::Count); ++env) {
        const auto target = static_cast<EnvTarget>(env);
        setEnvelopePoints(target, openglitch::core::ModEnvelope::defaultPoints(target));
    }
}

void OpenGlitchFXProcessor::loadFactoryProgram(int index) {
    if (!juce::isPositiveAndBelow(index, openglitch::patterns::count())) {
        return;
    }
    const juce::ScopedValueSetter<bool> loading(loadingPreset_, true);
    undo_.perform("Program", [this, index] { applyFactory(index); });
}

void OpenGlitchFXProcessor::loadUserPreset(const juce::String& name) {
    const auto tree = presets_.loadUser(name);
    if (!tree.isValid()) {
        return;
    }
    const juce::ScopedValueSetter<bool> loading(loadingPreset_, true);
    undo_.perform("Preset", [this, tree, name] {
        restoreMusicalState(tree);
        presetIsFactory_ = false;
        presetLabel_ = name.trim();
        presetEdited_ = false;
        programApplied_ = true;
    });
}

bool OpenGlitchFXProcessor::saveUserPreset(const juce::String& name, juce::String& error) {
    const auto saved = presets_.saveUser(name, musicalState(), error);
    if (saved) {
        presetIsFactory_ = false;
        presetLabel_ = name.trim();
        presetEdited_ = false;
    }
    return saved;
}

bool OpenGlitchFXProcessor::renameUserPreset(const juce::String& from, const juce::String& to, juce::String& error) {
    const auto renamed = presets_.renameUser(from, to, error);
    if (renamed && presetLabel_ == from.trim()) {
        presetLabel_ = to.trim();
    }
    return renamed;
}

bool OpenGlitchFXProcessor::deleteUserPreset(const juce::String& name, juce::String& error) {
    return presets_.deleteUser(name, error);
}

juce::ValueTree OpenGlitchFXProcessor::musicalState() {
    auto tree = parameters.copyState();
    tree.removeChild(tree.getChildWithName("SUITE_AB"), nullptr);
    for (const char* property : { "suiteProgram", "abActive", "presetLabel", "presetFactory", "presetEdited" }) {
        tree.removeProperty(property, nullptr);
    }
    return tree;
}

// The musical state plus the preset name, so each A/B slot keeps its own.
juce::ValueTree OpenGlitchFXProcessor::slotState() {
    auto tree = musicalState();
    tree.setProperty("suiteProgram", currentProgram_, nullptr);
    tree.setProperty("presetLabel", presetLabel_, nullptr);
    tree.setProperty("presetFactory", presetIsFactory_, nullptr);
    tree.setProperty("presetEdited", presetEdited_, nullptr);
    return tree;
}

void OpenGlitchFXProcessor::restoreSlot(const juce::ValueTree& slot) {
    restoreMusicalState(slot);
    currentProgram_ = static_cast<int>(slot.getProperty("suiteProgram", currentProgram_));
    presetLabel_ = slot.getProperty("presetLabel", presetLabel_).toString();
    presetIsFactory_ = static_cast<bool>(slot.getProperty("presetFactory", presetIsFactory_));
    presetEdited_ = static_cast<bool>(slot.getProperty("presetEdited", presetEdited_));
}

juce::ValueTree OpenGlitchFXProcessor::captureExtra() const {
    juce::ValueTree extra("SUITE_EXTRA");
    extra.appendChild(parameters.state.getChildWithName(kEnvelopesNode).createCopy(), nullptr);
    extra.setProperty("program", currentProgram_, nullptr);
    extra.setProperty("applied", programApplied_, nullptr);
    extra.setProperty("factory", presetIsFactory_, nullptr);
    extra.setProperty("label", presetLabel_, nullptr);
    extra.setProperty("edited", presetEdited_, nullptr);
    return extra;
}

void OpenGlitchFXProcessor::applyExtra(const juce::ValueTree& extra) {
    if (const auto envelopes = extra.getChildWithName(kEnvelopesNode); envelopes.isValid()) {
        auto existing = parameters.state.getChildWithName(kEnvelopesNode);
        if (existing.isValid()) {
            parameters.state.removeChild(existing, nullptr);
        }
        parameters.state.appendChild(envelopes.createCopy(), nullptr);
        syncEnvelopePointsToCore();
    }
    currentProgram_ = static_cast<int>(extra.getProperty("program", currentProgram_));
    programApplied_ = static_cast<bool>(extra.getProperty("applied", programApplied_));
    presetIsFactory_ = static_cast<bool>(extra.getProperty("factory", presetIsFactory_));
    presetLabel_ = extra.getProperty("label", presetLabel_).toString();
    presetEdited_ = static_cast<bool>(extra.getProperty("edited", presetEdited_));
    ++stateVersion_;
}

void OpenGlitchFXProcessor::restoreMusicalState(const juce::ValueTree& state) {
    if (!state.isValid()) {
        return;
    }
    undo_.suspend();
    auto restored = state.createCopy();
    restored.removeChild(restored.getChildWithName("SUITE_AB"), nullptr);
    parameters.replaceState(restored);
    syncParametersToCore();
    syncEnvelopePointsToCore();
    ++stateVersion_;
    undo_.resume();
}

void OpenGlitchFXProcessor::selectAbSlot(bool slotA) {
    if (slotA == ab_.isA()) {
        return;
    }
    restoreSlot(ab_.toggle(slotState()));
    undo_.useHistory(slotA ? 0 : 1);
}

void OpenGlitchFXProcessor::copyAbAtoB() {
    if (const auto loaded = ab_.copyAtoB(slotState()); loaded.isValid()) {
        restoreSlot(loaded);
    }
    undo_.clearHistory(1);
}

void OpenGlitchFXProcessor::copyAbBtoA() {
    if (const auto loaded = ab_.copyBtoA(slotState()); loaded.isValid()) {
        restoreSlot(loaded);
    }
    undo_.clearHistory(0);
}

void OpenGlitchFXProcessor::getStateInformation(juce::MemoryBlock& destData) {
    ab_.rememberActive(slotState());
    auto tree = slotState();
    tree.setProperty("abActive", ab_.isA() ? "A" : "B", nullptr);
    tree.setProperty("editorScale", editorScale_.load(), nullptr);
    juce::ValueTree slots("SUITE_AB");
    auto slotA = ab_.slotA().createCopy();
    slotA.setProperty("slot", "A", nullptr);
    auto slotB = ab_.slotB().createCopy();
    slotB.setProperty("slot", "B", nullptr);
    slots.appendChild(slotA, nullptr);
    slots.appendChild(slotB, nullptr);
    tree.appendChild(slots, nullptr);
    if (auto xmlState = tree.createXml()) {
        copyXmlToBinary(*xmlState, destData);
    }
}

void OpenGlitchFXProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes)) {
        if (xmlState->hasTagName(parameters.state.getType())) {
            auto restored = juce::ValueTree::fromXml(*xmlState);
            // Missing v1.2 parameters must not retain values from the previously loaded session.
            for (auto* parameter : getParameters()) {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
                if (ranged != nullptr && ranged->getVersionHint() == 2
                    && !restored.getChildWithProperty("id", ranged->paramID).isValid()) {
                    juce::ValueTree value("PARAM");
                    value.setProperty("id", ranged->paramID, nullptr);
                    const float fallback = ranged->paramID.startsWith("step_fx_extra_") ? 0.0f
                        : ranged->convertFrom0to1(ranged->getDefaultValue());
                    value.setProperty("value", fallback, nullptr);
                    restored.appendChild(value, nullptr);
                }
            }
            const auto slots = restored.getChildWithName("SUITE_AB");
            const bool hadProgram = restored.hasProperty("suiteProgram");
            const int savedProgram = static_cast<int>(restored.getProperty("suiteProgram", 0));
            const auto savedLabel = restored.getProperty("presetLabel").toString();
            const bool savedFactory = restored.hasProperty("presetFactory")
                ? static_cast<bool>(restored.getProperty("presetFactory"))
                : false;
            const bool savedA = restored.getProperty("abActive", "A").toString() != "B";
            const bool savedEdited = static_cast<bool>(restored.getProperty("presetEdited", false));
            const auto savedScale = static_cast<float>(restored.getProperty("editorScale", editorScale_.load()));
            editorScale_.store(std::clamp(savedScale, EditorContent::kMinScale, EditorContent::kMaxScale));
            restored.removeChild(restored.getChildWithName("SUITE_AB"), nullptr);
            for (const char* property : { "suiteProgram", "abActive", "presetLabel", "presetFactory", "presetEdited", "editorScale" }) {
                restored.removeProperty(property, nullptr);
            }
            undo_.suspend();
            parameters.replaceState(restored);
            syncParametersToCore();
            syncEnvelopePointsToCore();
            ++stateVersion_;
            undo_.resume();
            currentProgram_ = juce::jlimit(0, std::max(0, getNumPrograms() - 1), savedProgram);
            programApplied_ = true;
            presetIsFactory_ = hadProgram && savedFactory;
            presetEdited_ = savedEdited;
            presetLabel_ = savedLabel.isNotEmpty() ? savedLabel : (presetIsFactory_ ? getProgramName(currentProgram_) : juce::String("Session"));
            if (slots.isValid() && slots.getNumChildren() >= 2) {
                ab_.setSlots(slots.getChild(0).createCopy(), slots.getChild(1).createCopy(), savedA);
            } else {
                ab_.reset(slotState());
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new OpenGlitchFXProcessor();
}
