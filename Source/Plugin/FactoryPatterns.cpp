#include "FactoryPatterns.h"
#include "Parameters.h"
#include <array>
#include <cstring>
#include <vector>

namespace openglitch::patterns {
namespace {

struct Pattern {
    const char* name;
    const char* group;
    const char* steps;
    const char* extra;
    int length;
    float swing;
    int rate;
    int chance;
    bool keepSub;
    int stutterSync;
    int stutterReverse;
    float stutterGate;
    float stutterPitch;
    int tapeDirection;
    int tapeLength;
    int mixMode;
    float level;
};

Pattern base(const char* name, const char* group, const char* steps, const char* extra = nullptr) {
    return { name, group, steps, extra, 32, 0.0f, 1, 100, false, 3, 0, 100.0f, 0.0f, 0, 0, 0, 60.0f };
}

const std::vector<Pattern>& allPatterns() {
    static const std::vector<Pattern> patterns = [] {
        std::vector<Pattern> list;
        auto add = [&](Pattern pattern) { list.push_back(pattern); };

        auto club = base("Club Stutter", "Stutters", "..SS..SS..SS..SS..SS..SS..SS..SS");
        club.stutterSync = 3;
        club.stutterGate = 80.0f;
        club.level = 64.0f;
        add(club);

        add(base("Straight Gate", "Gates", "C.C.C.C.C.C.C.C.C.C.C.C.C.C.C.C."));

        auto half = base("Half-Time Hold", "Half-time", "SSSSSSSS........SSSSSSSS........");
        half.stutterSync = 1;
        half.level = 58.0f;
        add(half);

        auto roll = base("Last Beat Roll", "Fills", "........................SSSSSSSS");
        roll.stutterSync = 4;
        roll.level = 72.0f;
        add(roll);

        auto tapeFill = base("Tape Stop Fill", "Tape stops", "........................TTTTTTTT");
        tapeFill.tapeLength = 4;
        tapeFill.level = 85.0f;
        add(tapeFill);

        auto tapeStart = base("Tape Start Rise", "Tape stops", "TTTTTTTT........................");
        tapeStart.tapeDirection = 1;
        tapeStart.tapeLength = 4;
        tapeStart.level = 75.0f;
        add(tapeStart);

        auto fullStop = base("Full Bar Stop", "Tape stops", "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT");
        fullStop.tapeLength = 6;
        fullStop.level = 80.0f;
        add(fullStop);

        auto endStop = base("End Accent Stop", "Tape stops", ".......T.......T.......T.......T");
        endStop.tapeLength = 1;
        endStop.level = 90.0f;
        add(endStop);

        auto reverseSweep = base("Reverse Sweep", "Risers", "................VVVVVVVVVVVVVVVV");
        reverseSweep.level = 78.0f;
        add(reverseSweep);

        add(base("Reverse Hits", "Fills", ".......V.......V.......V.......V"));
        add(base("Four Beat Tour", "Drop chops", "CCCC....SSSS....VVVV....TTTT...."));

        auto build = base("Build Crush", "Builds", "B.......BB......BBBB....BBBBBBBB");
        build.level = 48.0f;
        add(build);

        auto pitch = base("Riser Pitch", "Risers", "................PPPPPPPPPPPPPPPP");
        pitch.level = 66.0f;
        add(pitch);

        add(base("Ping Throws", "Risers", ".......G.......G.......G.......G"));

        auto echo = base("Riser Echo", "Risers", "................GGGGGGGGGGGGGGGG");
        echo.mixMode = 1;
        echo.level = 55.0f;
        add(echo);

        auto crush = base("Offbeat Crush", "Gates", "..BB..BB..BB..BB..BB..BB..BB..BB");
        crush.keepSub = true;
        crush.level = 42.0f;
        add(crush);

        add(base("Downsample Grid", "Gates", "D...D...D...D...D...D...D...D..."));

        auto ring = base("Ring Pings", "Fills", ".......R.......R.......R.......R");
        ring.level = 40.0f;
        add(ring);

        add(base("Comb Throws", "Fills", "......MM......MM......MM......MM"));

        auto fold = base("Wavefold Backbeat", "Half-time", "........WWWWWWWW........WWWWWWWW",
                         "........UUUUUUUU........UUUUUUUU");
        fold.level = 46.0f;
        add(fold);

        auto chorus = base("Chorus Wash", "Washes", "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH");
        chorus.mixMode = 1;
        chorus.level = 36.0f;
        add(chorus);

        auto flanger = base("Flanger Steps", "Washes", "F.F.F.F.F.F.F.F.F.F.F.F.F.F.F.F.");
        flanger.rate = 0;
        flanger.level = 44.0f;
        add(flanger);

        add(base("Phaser Gates", "Washes", "A...A...A...A...A...A...A...A..."));

        auto glue = base("Saturation Glue", "Washes", "UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU");
        glue.mixMode = 1;
        glue.level = 28.0f;
        add(glue);

        add(base("Glitch Fill", "Fills", "........................SPVCTWRB"));

        auto stutterRoll = base("Stutter Roll", "Stutters", "............................SSSS");
        stutterRoll.stutterSync = 4;
        stutterRoll.stutterReverse = 2;
        stutterRoll.stutterGate = 55.0f;
        stutterRoll.level = 78.0f;
        add(stutterRoll);

        auto beatRepeat = base("Beat Repeat", "Stutters", "SS......SS......SS......SS......");
        beatRepeat.stutterSync = 2;
        beatRepeat.level = 66.0f;
        add(beatRepeat);

        auto swing = base("Swing Chop", "Gates", "..CC..CC..CC..CC..CC..CC..CC..CC");
        swing.swing = 62.0f;
        swing.level = 70.0f;
        add(swing);

        auto halfLoop = base("Half Loop Gate", "Half-time", "C.C.C.C.C.C.C.C.................");
        halfLoop.length = 16;
        add(halfLoop);

        auto doubled = base("Double Time Sprinkle", "Stutters", "S.......S.......S.......S.......");
        doubled.rate = 2;
        doubled.stutterSync = 2;
        doubled.level = 52.0f;
        add(doubled);

        auto chance = base("Chance Gates", "Gates", "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC");
        chance.chance = 50;
        chance.level = 58.0f;
        add(chance);

        auto sub = base("Sub-Safe Crush", "Builds", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
        sub.keepSub = true;
        sub.level = 32.0f;
        add(sub);

        auto broken = base("Broken Beat", "Stutters", "S..S.S..S..S.S..S..S.S..S..S.S..");
        broken.swing = 48.0f;
        broken.stutterSync = 3;
        add(broken);

        auto drop = base("Drop Silence", "Drop chops", "SSSSSSSS................TTTTTTTT");
        drop.stutterSync = 2;
        drop.tapeLength = 4;
        drop.level = 70.0f;
        add(drop);

        auto reverseBuild = base("Reverse Build", "Builds", "V...VV..VVV.VVVVVVVVVVVVVVVVVVVV");
        reverseBuild.level = 64.0f;
        add(reverseBuild);

        auto finalChop = base("Final Chop Drop", "Drop chops", "C.C.C.C.C.C.C.C.CCCCCCCCCCCCCCCC");
        finalChop.level = 72.0f;
        add(finalChop);

        return list;
    }();
    return patterns;
}

int fxFromChar(char symbol) {
    switch (symbol) {
        case 'B': return 0;
        case 'D': return 1;
        case 'S': return 2;
        case 'T': return 3;
        case 'R': return 4;
        case 'V': return 5;
        case 'C': return 6;
        case 'P': return 7;
        case 'M': return 8;
        case 'W': return 9;
        case 'H': return 10;
        case 'F': return 11;
        case 'A': return 12;
        case 'G': return 13;
        case 'U': return 14;
        case '.': return -1;
        default: return -2;
    }
}

void setDenormalised(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value) {
    if (auto* parameter = state.getParameter(id)) {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
}

} // namespace

int count() { return static_cast<int>(allPatterns().size()); }

int defaultIndex() { return 0; }

juce::String name(int index) {
    return juce::isPositiveAndBelow(index, count()) ? allPatterns()[static_cast<size_t>(index)].name : juce::String();
}

juce::String group(int index) {
    return juce::isPositiveAndBelow(index, count()) ? allPatterns()[static_cast<size_t>(index)].group : juce::String();
}

bool stepsAreValid() {
    for (const auto& pattern : allPatterns()) {
        if (std::strlen(pattern.steps) != 32) {
            return false;
        }
        if (pattern.extra != nullptr && std::strlen(pattern.extra) != 32) {
            return false;
        }
        for (int step = 0; step < 32; ++step) {
            if (fxFromChar(pattern.steps[step]) < -1) {
                return false;
            }
            if (pattern.extra != nullptr && fxFromChar(pattern.extra[step]) < -1) {
                return false;
            }
        }
    }
    return count() >= 30 && count() <= 40;
}

void apply(juce::AudioProcessorValueTreeState& state, int index) {
    if (!juce::isPositiveAndBelow(index, count())) {
        return;
    }
    const auto& pattern = allPatterns()[static_cast<size_t>(index)];
    std::array<bool, 15> used{};
    for (int step = 0; step < 32; ++step) {
        uint16_t mask = 0;
        auto include = [&](char symbol) {
            const int fx = fxFromChar(symbol);
            if (fx >= 0) {
                mask = static_cast<uint16_t>(mask | (1u << fx));
                used[static_cast<size_t>(fx)] = true;
            }
        };
        if (pattern.steps != nullptr && std::strlen(pattern.steps) == 32) {
            include(pattern.steps[step]);
        }
        if (pattern.extra != nullptr && std::strlen(pattern.extra) == 32) {
            include(pattern.extra[step]);
        }
        using namespace openglitch::params;
        setDenormalised(state, stepParamId(step), mask != 0 ? 1.0f : 0.0f);
        setDenormalised(state, stepFxParamId(step), static_cast<float>(mask & 1023u));
        setDenormalised(state, stepExtraFxParamId(step), static_cast<float>(mask >> 10));
    }

    for (int fx = 0; fx < 15; ++fx) {
        const auto& descriptor = openglitch::params::kFxDescriptors[static_cast<size_t>(fx)];
        const float amount = used[static_cast<size_t>(fx)] ? pattern.level : 0.0f;
        setDenormalised(state, descriptor.amtParamId, amount);
        setDenormalised(state, descriptor.onParamId, amount > 0.5f ? 1.0f : 0.0f);
    }

    using namespace openglitch::params;
    setDenormalised(state, kSequenceLength, static_cast<float>(pattern.length));
    setDenormalised(state, kSequencerSwing, pattern.swing);
    setDenormalised(state, kPatternRate, static_cast<float>(pattern.rate));
    setDenormalised(state, kPatternChance, static_cast<float>(pattern.chance));
    setDenormalised(state, kKeepSub, pattern.keepSub ? 1.0f : 0.0f);
    setDenormalised(state, kStutterSync, static_cast<float>(pattern.stutterSync));
    setDenormalised(state, kStutterReverse, static_cast<float>(pattern.stutterReverse));
    setDenormalised(state, kStutterGate, pattern.stutterGate);
    setDenormalised(state, kStutterPitch, pattern.stutterPitch);
    setDenormalised(state, kStutterAdvance, 0.0f);
    setDenormalised(state, kTapeDirection, static_cast<float>(pattern.tapeDirection));
    setDenormalised(state, kTapeCurve, 0.0f);
    setDenormalised(state, kTapeLength, static_cast<float>(pattern.tapeLength));
    setDenormalised(state, kTapeSlip, 0.0f);
    setDenormalised(state, kTapeLowpass, 1.0f);
    setDenormalised(state, kTapeFade, 1.0f);
    setDenormalised(state, kFilterMode, 0.0f);
    setDenormalised(state, kFilterCutoff, 2000.0f);
    setDenormalised(state, kFilterReso, 20.0f);
    setDenormalised(state, kMixMode, static_cast<float>(pattern.mixMode));
    setDenormalised(state, kMasterMix, 100.0f);
    setDenormalised(state, kMasterPower, 1.0f);
}

} // namespace openglitch::patterns
