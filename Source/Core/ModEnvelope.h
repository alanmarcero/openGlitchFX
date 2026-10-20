#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace openglitch::core {

struct EnvelopePoint {
    float x = 0.0f; // normalized time [0 .. 1]
    float y = 0.0f; // normalized value [0 .. 1]
};

enum class EnvTarget : int { Volume = 0, Pan, Filter, Pitch, Count };

struct ModEnvelopeSettings {
    bool enabled = false;
    float depth = 1.0f;
    double lengthBeats = 0.0; // 0 = use the pad span
};

// The audio thread reads the table without a lock.
class ModEnvelope {
public:
    static constexpr int kTableSize = 256;
    static constexpr int kMaxPoints = 32;

    ModEnvelope();

    void setPoints(std::vector<EnvelopePoint> points);
    float valueAt(float phase01) const;

    static std::vector<EnvelopePoint> defaultPoints(EnvTarget target);

private:
    std::array<std::atomic<float>, kTableSize> table_;
};

} // namespace openglitch::core
