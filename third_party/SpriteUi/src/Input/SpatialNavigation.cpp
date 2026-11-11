#include "SpriteUi/Input/SpatialNavigation.hpp"

namespace ToolEngine::SpriteUi::Input {
namespace {

[[nodiscard]] bool HasUsableFocusBounds(const UIFocusCandidate& candidate) noexcept {
    return candidate.focusable && candidate.enabled && candidate.visible &&
           IsFinite(candidate.bounds.position) &&
           IsFinite(candidate.bounds.size) && candidate.bounds.size.x > 0.0f &&
           candidate.bounds.size.y > 0.0f;
}

[[nodiscard]] bool IsUsableFocusCandidate(const UIFocusCandidate& candidate) noexcept {
    return candidate.handle.IsValid() && HasUsableFocusBounds(candidate);
}

} // namespace

// Seleciona o candidato mais proximo dentro de um cone de 90 graus (+-45) na direcao pedida.
UIHandle FindSpatialNeighbor(
    UIHandle current,
    NavDirection direction,
    std::span<const UIFocusCandidate> candidates) noexcept
{
    const UIFocusCandidate* origin = nullptr;
    for (const auto& candidate : candidates) {
        if (candidate.handle == current && HasUsableFocusBounds(candidate)) {
            origin = &candidate;
            break;
        }
    }
    if (!origin) return InvalidUIHandle;

    Vec2 axis{};
    switch (direction) {
    case NavDirection::Up:    axis = {0.0f, -1.0f}; break;
    case NavDirection::Down:  axis = {0.0f, 1.0f}; break;
    case NavDirection::Left:  axis = {-1.0f, 0.0f}; break;
    case NavDirection::Right: axis = {1.0f, 0.0f}; break;
    }

    const Vec2 originCenter = origin->bounds.position + origin->bounds.size * 0.5f;
    UIHandle best = InvalidUIHandle;
    float bestScore = 0.0f;

    for (const auto& candidate : candidates) {
        if (candidate.handle == current || !IsUsableFocusCandidate(candidate)) continue;

        const Vec2 center = candidate.bounds.position + candidate.bounds.size * 0.5f;
        const Vec2 delta = center - originCenter;
        const float forward = Dot(delta, axis);
        const float side = std::abs(delta.x * axis.y - delta.y * axis.x);
        if (forward <= 0.0f || side > forward) continue;

        const float score = forward + side * 2.0f;
        if (!best.IsValid() || score < bestScore ||
            (score == bestScore && candidate.handle.Pack() < best.Pack())) {
            best = candidate.handle;
            bestScore = score;
        }
    }
    return best;
}

std::vector<UISemanticEvent> UINavigationRouter::Process(
    UINavigationCommand command,
    std::span<const UIFocusCandidate> candidates)
{
    std::vector<UISemanticEvent> events;
    const UIHandle previous = focused_;
    const UIFocusCandidate* focusedCandidate = FindCandidate(focused_, candidates);

    if (!focusedCandidate) {
        focused_ = FindFirst(candidates);
        if (focused_ != previous) {
            events.push_back({UISemanticEventType::FocusChanged, focused_, previous});
        }
        // Acquiring focus consumes Activate/Cancel so it cannot activate a target
        // that was not visibly focused yet.
        if (command == UINavigationCommand::Activate ||
            command == UINavigationCommand::Cancel) {
            return events;
        }
    }

    UIHandle next = focused_;
    switch (command) {
    case UINavigationCommand::Up:
        next = FindSpatialNeighbor(focused_, NavDirection::Up, candidates);
        break;
    case UINavigationCommand::Down:
        next = FindSpatialNeighbor(focused_, NavDirection::Down, candidates);
        break;
    case UINavigationCommand::Left:
        next = FindSpatialNeighbor(focused_, NavDirection::Left, candidates);
        break;
    case UINavigationCommand::Right:
        next = FindSpatialNeighbor(focused_, NavDirection::Right, candidates);
        break;
    case UINavigationCommand::None:
    case UINavigationCommand::Activate:
    case UINavigationCommand::Cancel:
        break;
    }

    if (next.IsValid() && next != focused_) {
        const UIHandle old = focused_;
        focused_ = next;
        events.push_back({UISemanticEventType::FocusChanged, focused_, old});
    }

    if (command == UINavigationCommand::Activate && focused_.IsValid()) {
        events.push_back({UISemanticEventType::Activate, focused_, focused_});
    } else if (command == UINavigationCommand::Cancel && focused_.IsValid()) {
        events.push_back({UISemanticEventType::Cancel, focused_, focused_});
    }

    return events;
}

bool UINavigationRouter::IsUsable(const UIFocusCandidate& candidate) noexcept
{
    return IsUsableFocusCandidate(candidate);
}

const UIFocusCandidate* UINavigationRouter::FindCandidate(
    UIHandle handle, std::span<const UIFocusCandidate> candidates) noexcept
{
    if (!handle.IsValid()) return nullptr;
    for (const auto& candidate : candidates) {
        if (candidate.handle == handle && IsUsable(candidate)) return &candidate;
    }
    return nullptr;
}

UIHandle UINavigationRouter::FindFirst(
    std::span<const UIFocusCandidate> candidates) noexcept
{
    const UIFocusCandidate* best = nullptr;
    for (const auto& candidate : candidates) {
        if (!IsUsable(candidate)) continue;
        if (!best || candidate.navigationOrder < best->navigationOrder ||
            (candidate.navigationOrder == best->navigationOrder &&
             candidate.handle.Pack() < best->handle.Pack())) {
            best = &candidate;
        }
    }
    return best ? best->handle : InvalidUIHandle;
}

} // namespace ToolEngine::SpriteUi::Input
