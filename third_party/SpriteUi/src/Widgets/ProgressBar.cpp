#include "SpriteUi/Widgets/ProgressBar.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

[[nodiscard]] bool IsValidRange(float minimumValue, float maximumValue) noexcept {
    return IsFinite(minimumValue) && IsFinite(maximumValue) && maximumValue > minimumValue;
}

[[nodiscard]] bool IsValidTrack(Rect track) noexcept {
    return IsFinite(track.position) && IsFinite(track.size);
}

} // namespace

bool ProgressBarControl::SetRange(float minimumValue, float maximumValue) noexcept {
    if (!IsValidRange(minimumValue, maximumValue)) return false;
    minimum_ = minimumValue;
    maximum_ = maximumValue;
    value_ = std::clamp(value_, minimum_, maximum_);
    return true;
}

bool ProgressBarControl::SetValue(float newValue) noexcept {
    const float sanitized = std::isnan(newValue) ? 0.0f : newValue;
    const float clamped = std::clamp(sanitized, minimum_, maximum_);
    if (clamped == value_) return false;
    value_ = clamped;
    return true;
}

float ProgressBarControl::Fraction() const noexcept {
    const float range = maximum_ - minimum_;
    if (!IsFinite(range) || range <= 0.0f) return 0.0f;
    return Clamp01((value_ - minimum_) / range);
}

Rect ProgressBarControl::FillRect(Rect track) const noexcept {
    if (!IsValidTrack(track)) return {};
    return {track.position, {track.size.x * Fraction(), track.size.y}};
}

Rect ProgressBarControl::IndeterminateRect(Rect track, float phase, float segmentFraction) const noexcept {
    if (!IsValidTrack(track) || !IsFinite(phase) || !IsFinite(segmentFraction)) return {};

    const float segmentWidth = track.size.x * Clamp01(segmentFraction);
    const float cycle = phase - std::floor(phase); // Sempre em [0, 1): a fase repete a cada volta.
    // O trecho entra pela esquerda (fora do trilho) e sai pela direita: o caminho e o trilho mais o trecho.
    const float travel = track.size.x + segmentWidth;
    const float left = track.position.x - segmentWidth + cycle * travel;
    return {{left, track.position.y}, {segmentWidth, track.size.y}};
}

} // namespace ToolEngine::SpriteUi::Widgets
