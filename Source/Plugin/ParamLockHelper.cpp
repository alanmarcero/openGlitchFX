#include "ParamLockHelper.h"

namespace openglitch::ui {

ParamLockOverlayHelper::ParamLockOverlayHelper(OpenGlitchFXProcessor& processor, juce::Component& host)
    : processor_(processor), host_(host) {}

ParamLockOverlayHelper::~ParamLockOverlayHelper() {
    for (const auto& entry : entries_) {
        if (entry.component != nullptr) {
            entry.component->removeMouseListener(this);
        }
    }
}

void ParamLockOverlayHelper::registerControl(juce::Component& control, const juce::String& paramId, bool isComboBox) {
    registerDynamicControl(control, [paramId] { return paramId; }, isComboBox);
}

void ParamLockOverlayHelper::registerDynamicControl(juce::Component& control,
                                                    std::function<juce::String()> getParamId,
                                                    bool isComboBox) {
    control.addMouseListener(this, true);
    entries_.push_back({ &control, std::move(getParamId), isComboBox });
}

const ParamLockOverlayHelper::Entry* ParamLockOverlayHelper::findEntryFor(const juce::Component* source) const {
    for (const auto* comp = source; comp != nullptr && comp != &host_; comp = comp->getParentComponent()) {
        for (const auto& entry : entries_) {
            if (entry.component == comp) {
                return &entry;
            }
        }
    }
    return nullptr;
}

void ParamLockOverlayHelper::mouseDown(const juce::MouseEvent& event) {
    if (!event.mods.isPopupMenu()) {
        return;
    }
    const auto* entry = findEntryFor(event.eventComponent);
    if (entry == nullptr) {
        return;
    }
    const auto paramId = entry->getParamId();
    if (paramId.isEmpty()) {
        return;
    }

    auto* param = processor_.parameters.getParameter(paramId);
    const auto name = param != nullptr ? param->getName(64) : entry->component->getTitle();
    const bool locked = processor_.isParamLocked(paramId);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&host_.getLookAndFeel());
    menu.addItem(1, (locked ? "Unlock " : "Lock ") + name);
    if (processor_.hasAnyLockedParams()) {
        menu.addSeparator();
        menu.addItem(2, "Unlock all parameters");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(entry->component).withDeletionCheck(host_),
                       [this, paramId](int result) {
        if (result == 1) {
            processor_.setParamLocked(paramId, !processor_.isParamLocked(paramId));
            host_.repaint();
        } else if (result == 2) {
            processor_.unlockAllParams();
            host_.repaint();
        }
    });
}

void ParamLockOverlayHelper::paintLocks(juce::Graphics& g, const juce::Colour& accent, const juce::Colour& bg) const {
    for (const auto& entry : entries_) {
        if (entry.component == nullptr || !entry.component->isVisible()) {
            continue;
        }
        const auto paramId = entry.getParamId();
        if (paramId.isEmpty() || !processor_.isParamLocked(paramId)) {
            continue;
        }
        const auto bounds = host_.getLocalArea(entry.component, entry.component->getLocalBounds()).toFloat();
        const float rightInset = entry.isComboBox ? 25.0f : 13.0f;
        const juce::Rectangle<float> iconBounds(bounds.getRight() - rightInset, bounds.getY() + 2.0f, 11.0f, 11.0f);
        drawPadlock(g, iconBounds, accent, bg);
    }
}

void ParamLockOverlayHelper::drawPadlock(juce::Graphics& g,
                                         juce::Rectangle<float> iconBounds,
                                         const juce::Colour& accent,
                                         const juce::Colour& bg) {
    g.setColour(bg.withAlpha(0.88f));
    g.fillRoundedRectangle(iconBounds, 2.5f);
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(iconBounds, 2.5f, 0.8f);

    const auto inner = iconBounds.reduced(2.0f, 1.5f);
    const float w = inner.getWidth();
    const float h = inner.getHeight();
    const float shackleW = w * 0.62f;
    const float shackleH = h * 0.45f;
    const float shackleX = inner.getCentreX() - shackleW * 0.5f;

    juce::Path shackle;
    shackle.addArc(shackleX, inner.getY(), shackleW, shackleH * 1.6f,
                   -juce::MathConstants<float>::halfPi,
                   juce::MathConstants<float>::halfPi,
                   true);
    g.setColour(accent);
    g.strokePath(shackle, juce::PathStrokeType(1.3f));

    const juce::Rectangle<float> body(inner.getX(), inner.getY() + shackleH * 0.9f, w, h - shackleH * 0.9f);
    g.fillRoundedRectangle(body, 1.2f);
}

} // namespace openglitch::ui
