#pragma once

#include "i18n.hpp"
#include "SpriteUi/SpriteUi.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace editor {

namespace SpriteUi = ToolEngine::SpriteUi;
namespace SpriteRendering = ToolEngine::SpriteUi::Rendering;

class SpriteUiDiagnostics {
 public:
  SpriteUiDiagnostics() {
    SpriteUi::FlexItem panel;
    panel.width = {SpriteUi::SizeMode::Fixed, 380.f};
    panel.height = {SpriteUi::SizeMode::Fixed, 156.f};
    panel.direction = SpriteUi::LayoutDirection::Column;
    panel.alignItems = SpriteUi::AlignItems::Stretch;
    panel.padding = {12.f, 58.f, 12.f, 12.f};
    panel.gap = 8.f;
    panel_ = tree_.Create({}, panel);

    SpriteUi::FlexItem row;
    row.width = {SpriteUi::SizeMode::Percent, 1.f};
    row.height = {SpriteUi::SizeMode::Fixed, 32.f};
    row.direction = SpriteUi::LayoutDirection::Row;
    row.alignItems = SpriteUi::AlignItems::Stretch;
    row.gap = 8.f;
    const auto rowHandle = tree_.Create(panel_, row);

    SpriteUi::FlexItem button;
    button.width = {SpriteUi::SizeMode::Flex, 1.f};
    button.height = {SpriteUi::SizeMode::Fixed, 32.f};
    button.grow = 1.f;
    pauseButton_ = tree_.Create(rowHandle, button);
    closeButton_ = tree_.Create(rowHandle, button);

    SpriteUi::ElementVisual panelVisual;
    panelVisual.tintPremultipliedLinear = SpriteRendering::SrgbStraightToLinearPremultiplied(
        {0.055f, 0.075f, 0.11f, 0.97f});
    panelVisual.interactable = false;
    panelVisual.clipOverflow = true;
    bridge_.SetVisual(panel_, panelVisual);
    updateButtonVisuals({});
  }

  void beginFrame() {
    pointerPressed_ = false;
    pointerReleased_ = false;
    pointerCancelled_ = false;
  }

  bool handleEvent(const SDL_Event& event, bool& visible) {
    if (!visible) return false;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
      visible = false;
      router_.Reset();
      return true;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      focused_ = false;
      pointerCancelled_ = true;
      return router_.GetState().capturedEntity.IsValid();
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
      focused_ = true;
      return false;
    }

    const bool captured = router_.GetState().capturedEntity.IsValid();
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
      if (!contains(event.button.x, event.button.y)) return false;
      pointerPressed_ = true;
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
      if (!captured && !contains(event.button.x, event.button.y)) return false;
      pointerReleased_ = true;
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
      return captured || contains(event.motion.x, event.motion.y);
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
      return contains(event.wheel.mouse_x, event.wheel.mouse_y);
    }
    return false;
  }

  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool& visible, float fps,
            float sceneMs, float postFxMs) {
    if (!visible) return true;
    if (!renderer || !window) return false;
    if (!SDL_SetRenderClipRect(renderer, nullptr)) return false;

    int windowWidth = 0, windowHeight = 0;
    if (!SDL_GetWindowSize(window, &windowWidth, &windowHeight)) return false;

    const float panelWidth = std::max(1.f, std::min(380.f, static_cast<float>(windowWidth) - 32.f));
    const float panelHeight = std::max(1.f, std::min(156.f, static_cast<float>(windowHeight) - 16.f));
    const auto* panel = tree_.Get(panel_);
    if (!panel) return false;
    SpriteUi::FlexItem panelStyle = panel->node.style;
    panelStyle.width.value = panelWidth;
    panelStyle.height.value = panelHeight;
    if (!tree_.SetStyle(panel_, panelStyle)) return false;
    panelBounds_ = {16.f, std::max(0.f, std::min(52.f, static_cast<float>(windowHeight) - panelHeight)),
                    panelWidth, panelHeight};
    tree_.Layout({{panelBounds_.x, panelBounds_.y}, {panelBounds_.w, panelBounds_.h}});

    auto scene = bridge_.Extract(tree_);
    float mouseX = 0.f, mouseY = 0.f;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(&mouseX, &mouseY);
    const std::uint64_t frameId = ++frameId_;
    SpriteUi::Input::UIInputSnapshot input;
    input.pointerPositionLogical = {mouseX, mouseY};
    input.pointerDeltaLogical = hasPointer_ ? SpriteUi::Vec2{mouseX - lastMouseX_, mouseY - lastMouseY_}
                                             : SpriteUi::Vec2{};
    input.primaryDown = (mouse & SDL_BUTTON_LMASK) != 0;
    input.primaryPressed = pointerPressed_;
    input.primaryReleased = pointerReleased_;
    input.pointerCancelled = pointerCancelled_;
    input.windowFocused = focused_;
    input.frameId = frameId;
    lastMouseX_ = mouseX;
    lastMouseY_ = mouseY;
    hasPointer_ = true;

    const auto events = router_.Process(input, scene.hitTargets);
    pointerPressed_ = false;
    pointerReleased_ = false;
    pointerCancelled_ = false;
    for (const auto& event : events) {
      if (event.type != SpriteUi::Input::UIEventType::Click) continue;
      if (event.target == pauseButton_) paused_ = !paused_;
      if (event.target == closeButton_) {
        visible = false;
        return true;
      }
    }

    if (!paused_) {
      shownFps_ = fps;
      shownSceneMs_ = sceneMs;
      shownPostFxMs_ = postFxMs;
    }
    updateButtonVisuals(router_.GetState().hoveredEntity);
    for (auto& item : scene.renderItems) {
      if (const auto* visual = bridge_.GetVisual(item.handle)) {
        item.tintPremultipliedLinear = visual->tintPremultipliedLinear;
      }
    }
    if (!scene.clipHierarchy.valid) return false;
    const auto packet = extractor_.Build(frameId, scene.renderItems, nullptr,
                                         scene.clipHierarchy.effectiveRects);
    if (!SpriteRendering::IsFramePacketValid(packet)) return false;
    if (!renderPacket(renderer, packet)) return false;
    return drawText(renderer);
  }

 private:
  struct Bounds { float x = 0.f, y = 0.f, w = 0.f, h = 0.f; };

  bool contains(float x, float y) const {
    return x >= panelBounds_.x && y >= panelBounds_.y &&
           x < panelBounds_.x + panelBounds_.w && y < panelBounds_.y + panelBounds_.h;
  }

  void updateButtonVisuals(SpriteUi::UIHandle hovered) {
    SpriteUi::ElementVisual pause;
    const auto pauseColor = paused_ ? SpriteUi::Vec4{0.78f, 0.48f, 0.12f, 1.f}
                                   : SpriteUi::Vec4{0.12f, 0.42f, 0.72f, 1.f};
    pause.tintPremultipliedLinear =
        SpriteRendering::SrgbStraightToLinearPremultiplied(
            hovered == pauseButton_ ? SpriteUi::Vec4{0.2f, 0.58f, 0.9f, 1.f} : pauseColor);
    pause.focusable = true;
    bridge_.SetVisual(pauseButton_, pause);

    SpriteUi::ElementVisual close;
    close.tintPremultipliedLinear =
        SpriteRendering::SrgbStraightToLinearPremultiplied(
            hovered == closeButton_ ? SpriteUi::Vec4{0.82f, 0.22f, 0.2f, 1.f}
                                    : SpriteUi::Vec4{0.62f, 0.15f, 0.15f, 1.f});
    close.focusable = true;
    bridge_.SetVisual(closeButton_, close);
  }

  static float linearToSrgb(float value) {
    value = std::clamp(value, 0.f, 1.f);
    return value <= 0.0031308f ? value * 12.92f
                               : 1.055f * std::pow(value, 1.f / 2.4f) - 0.055f;
  }

  static SDL_FColor sdlColor(const SpriteRendering::SpriteInstanceGPU& instance) {
    const float alpha = std::clamp(instance.tint[3], 0.f, 1.f);
    const float inverseAlpha = alpha > 1e-6f ? 1.f / alpha : 0.f;
    return {
        linearToSrgb(instance.tint[0] * inverseAlpha),
        linearToSrgb(instance.tint[1] * inverseAlpha),
        linearToSrgb(instance.tint[2] * inverseAlpha),
        alpha
    };
  }

  static SDL_FPoint transform(const SpriteRendering::SpriteInstanceGPU& instance, float u, float v) {
    return {
        instance.transformRow0[0] * u + instance.transformRow0[1] * v + instance.transformRow0[2],
        instance.transformRow1[0] * u + instance.transformRow1[1] * v + instance.transformRow1[2]
    };
  }

  bool renderPacket(SDL_Renderer* renderer, const SpriteRendering::FramePacket& packet) {
    if (!SDL_SetRenderClipRect(renderer, nullptr)) return false;
    vertices_.reserve(packet.instances.size() * 4);
    indices_.reserve(packet.instances.size() * 6);

    for (const auto& batch : packet.batches) {
      if (batch.textureSlice != SpriteRendering::WhiteTextureSlice) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
      }
      if (batch.clipIndex != SpriteRendering::NoClipIndex) {
        const auto& clip = packet.clipRects[batch.clipIndex];
        const int left = static_cast<int>(std::floor(clip.minX));
        const int top = static_cast<int>(std::floor(clip.minY));
        const int right = static_cast<int>(std::ceil(clip.maxX));
        const int bottom = static_cast<int>(std::ceil(clip.maxY));
        const SDL_Rect rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
        if (!SDL_SetRenderClipRect(renderer, &rect)) {
          SDL_SetRenderClipRect(renderer, nullptr);
          return false;
        }
      } else if (!SDL_SetRenderClipRect(renderer, nullptr)) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
      }

      vertices_.clear();
      indices_.clear();
      for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
        const auto& instance = packet.instances[batch.firstInstance + i];
        const SDL_FColor color = sdlColor(instance);
        const SDL_FPoint points[] = {
            transform(instance, 0.f, 0.f), transform(instance, 1.f, 0.f),
            transform(instance, 1.f, 1.f), transform(instance, 0.f, 1.f)};
        const int base = static_cast<int>(vertices_.size());
        for (const SDL_FPoint point : points) vertices_.push_back({point, color, {}});
        indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
      }
      if (!SDL_RenderGeometry(renderer, nullptr, vertices_.data(),
                             static_cast<int>(vertices_.size()), indices_.data(),
                             static_cast<int>(indices_.size()))) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
      }
    }
    return SDL_SetRenderClipRect(renderer, nullptr);
  }

  bool drawText(SDL_Renderer* renderer) const {
    char line[128];
    if (!SDL_SetRenderDrawColor(renderer, 235, 240, 248, 255) ||
        !SDL_RenderDebugText(renderer, panelBounds_.x + 12.f, panelBounds_.y + 10.f,
                             T("SpriteUI diagnostics"))) return false;
    std::snprintf(line, sizeof(line), T("Frame %.1f FPS | Scene %.2f ms"),
                  shownFps_, shownSceneMs_);
    if (!SDL_RenderDebugText(renderer, panelBounds_.x + 12.f, panelBounds_.y + 34.f, line)) return false;
    std::snprintf(line, sizeof(line), T("PostFX %.2f ms | Metrics %s"), shownPostFxMs_,
                  T(paused_ ? "paused" : "live"));
    if (!SDL_RenderDebugText(renderer, panelBounds_.x + 12.f, panelBounds_.y + 48.f, line)) return false;

    const auto* pause = tree_.Get(pauseButton_);
    const auto* close = tree_.Get(closeButton_);
    if (pause) {
      const auto& rect = pause->node.rect;
      if (!SDL_RenderDebugText(renderer, rect.position.x + 8.f, rect.position.y + 11.f,
                               T(paused_ ? "Resume metrics" : "Pause metrics"))) return false;
    }
    if (close) {
      const auto& rect = close->node.rect;
      if (!SDL_RenderDebugText(renderer, rect.position.x + 12.f, rect.position.y + 11.f,
                               T("Close"))) return false;
    }
    return true;
  }

  SpriteUi::LayoutTree tree_;
  SpriteUi::LayoutSceneBridge bridge_;
  SpriteUi::Input::UIEventRouter router_;
  SpriteRendering::RenderExtractor extractor_;
  SpriteUi::UIHandle panel_{}, pauseButton_{}, closeButton_{};
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
  Bounds panelBounds_{16.f, 52.f, 380.f, 156.f};
  std::uint64_t frameId_ = 0;
  float lastMouseX_ = 0.f, lastMouseY_ = 0.f;
  float shownFps_ = 0.f, shownSceneMs_ = 0.f, shownPostFxMs_ = 0.f;
  bool pointerPressed_ = false, pointerReleased_ = false, pointerCancelled_ = false;
  bool hasPointer_ = false, focused_ = true, paused_ = false;
};

} // namespace editor
