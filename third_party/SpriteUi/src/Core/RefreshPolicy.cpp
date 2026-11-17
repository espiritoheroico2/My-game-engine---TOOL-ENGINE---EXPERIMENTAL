#include "SpriteUi/Core/RefreshPolicy.hpp"

#include <cmath>

namespace ToolEngine::SpriteUi {

namespace {

// Tolerancia para somas de ponto flutuante: 1/60 acumulado nao pode perder um quadro por arredondamento.
constexpr double kTimeEpsilon = 1e-9;

} // namespace

UIRefreshPolicy::UIRefreshPolicy(float rateHz) noexcept {
    SetRateHz(rateHz);
}

void UIRefreshPolicy::SetRateHz(float rateHz) noexcept {
    rateHz_ = (std::isfinite(rateHz) && rateHz > 0.0f) ? rateHz : 0.0f;
}

bool UIRefreshPolicy::ShouldRebuild(double nowSeconds, const UIDirtyState& dirty) const noexcept {
    if (!std::isfinite(nowSeconds)) return false;
    if (hasRebuilt_ && nowSeconds == lastRebuild_) return false;   // nunca duas vezes no mesmo instante
    if (dirty.bits != 0) return true;                              // suja: reconstroi
    if (rateHz_ <= 0.0f) return false;                             // taxa 0: so quando suja
    if (!hasRebuilt_) return true;
    if (nowSeconds < lastRebuild_) return true;                    // relogio voltou: reancora na proxima
    const double period = 1.0 / static_cast<double>(rateHz_);
    return (nowSeconds - lastRebuild_) >= period - kTimeEpsilon;
}

void UIRefreshPolicy::MarkRebuilt(double nowSeconds) noexcept {
    if (!std::isfinite(nowSeconds)) return;
    lastRebuild_ = nowSeconds;
    hasRebuilt_ = true;
}

bool UIRefreshPolicy::TryRebuild(double nowSeconds, UIDirtyState& dirty) noexcept {
    if (!ShouldRebuild(nowSeconds, dirty)) return false;
    MarkRebuilt(nowSeconds);
    (void)dirty.Consume();
    return true;
}

} // namespace ToolEngine::SpriteUi
