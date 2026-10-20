#include "ModEnvelope.h"
#include <algorithm>

namespace openglitch::core {

ModEnvelope::ModEnvelope() {
    setPoints({ { 0.0f, 1.0f }, { 1.0f, 1.0f } });
}

void ModEnvelope::setPoints(std::vector<EnvelopePoint> points) {
    if (points.empty()) {
        points = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
    }
    if (points.size() > static_cast<size_t>(kMaxPoints)) {
        points.resize(static_cast<size_t>(kMaxPoints));
    }
    std::ranges::sort(points, {}, &EnvelopePoint::x);

    for (int i = 0; i < kTableSize; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(kTableSize - 1);
        const auto next = std::ranges::find_if(points, [x](const EnvelopePoint& p) { return p.x >= x; });
        float y = points.back().y;
        if (next == points.begin()) {
            y = points.front().y;
        } else if (next != points.end()) {
            const auto& a = *(next - 1);
            const auto& b = *next;
            const float t = (b.x - a.x) > 1e-6f ? (x - a.x) / (b.x - a.x) : 1.0f;
            y = a.y + (b.y - a.y) * t;
        }
        table_[static_cast<size_t>(i)].store(std::clamp(y, 0.0f, 1.0f), std::memory_order_relaxed);
    }
}

float ModEnvelope::valueAt(float phase01) const {
    const float pos = std::clamp(phase01, 0.0f, 1.0f) * static_cast<float>(kTableSize - 1);
    const int i0 = std::min(static_cast<int>(pos), kTableSize - 2);
    const float frac = pos - static_cast<float>(i0);
    const float y0 = table_[static_cast<size_t>(i0)].load(std::memory_order_relaxed);
    const float y1 = table_[static_cast<size_t>(i0 + 1)].load(std::memory_order_relaxed);
    return y0 + (y1 - y0) * frac;
}

std::vector<EnvelopePoint> ModEnvelope::defaultPoints(EnvTarget target) {
    switch (target) {
        case EnvTarget::Volume: return { { 0.0f, 1.0f }, { 0.6f, 0.8f }, { 1.0f, 0.0f } };
        case EnvTarget::Pan:    return { { 0.0f, 0.5f }, { 0.25f, 0.0f }, { 0.75f, 1.0f }, { 1.0f, 0.5f } };
        case EnvTarget::Filter: return { { 0.0f, 1.0f }, { 1.0f, 0.2f } };
        case EnvTarget::Pitch:  return { { 0.0f, 0.5f }, { 1.0f, 0.0f } };
        case EnvTarget::Count:  break;
    }
    return { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
}

} // namespace openglitch::core
