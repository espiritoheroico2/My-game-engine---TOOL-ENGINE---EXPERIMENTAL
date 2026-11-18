// window_manager.cpp - Implementacao das regras de janelas (ver window_manager.hpp).
#include "window_manager.hpp"

#include <algorithm>
#include <cmath>

namespace editor::wm {

namespace {
// Chave de ordem: camada, depois zOrder, depois id (desempate deterministico).
bool drawsAbove(const WindowState& a, Layer la, const WindowState& b, Layer lb) {
  if (la != lb) return static_cast<int>(la) > static_cast<int>(lb);
  if (a.zOrder != b.zOrder) return a.zOrder > b.zOrder;
  return a.id > b.id;
}
}  // namespace

void WindowManager::upsert(const WindowState& w) {
  auto it = std::find_if(windows_.begin(), windows_.end(), [&](const WindowState& o) { return o.id == w.id; });
  if (it != windows_.end()) *it = w;
  else windows_.push_back(w);
}

void WindowManager::remove(int id) {
  windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [&](const WindowState& o) { return o.id == id; }), windows_.end());
  openPopups_.erase(std::remove(openPopups_.begin(), openPopups_.end(), id), openPopups_.end());
  if (drag_.active && drag_.id == id) drag_ = DragState{};
}

void WindowManager::setOpenPopup(int id, bool open) {
  auto it = std::find(openPopups_.begin(), openPopups_.end(), id);
  if (open && it == openPopups_.end()) openPopups_.push_back(id);
  if (!open && it != openPopups_.end()) openPopups_.erase(it);
}

const WindowState* WindowManager::find(int id) const {
  auto it = std::find_if(windows_.begin(), windows_.end(), [&](const WindowState& o) { return o.id == id; });
  return it == windows_.end() ? nullptr : &*it;
}

Layer WindowManager::effectiveLayer(const WindowState& w) const noexcept {
  if (drag_.active && drag_.moving && drag_.id == w.id) return Layer::DragPreview;
  if (w.popup) return Layer::Popup;
  return w.docked ? Layer::Docked : Layer::Floating;
}

int WindowManager::pick(float x, float y) const {
  const bool popupOpen = !openPopups_.empty();
  if (popupOpen) {
    // Popup aberto: so os popups abertos recebem o clique. Fora deles, o clique fecha o popup e nao atinge nada.
    const WindowState* best = nullptr;
    Layer bestLayer = Layer::Popup;
    for (const WindowState& w : windows_) {
      if (!w.visible || !w.popup) continue;
      if (std::find(openPopups_.begin(), openPopups_.end(), w.id) == openPopups_.end()) continue;
      if (!w.rect.contains(x, y)) continue;
      if (!best || drawsAbove(w, Layer::Popup, *best, bestLayer)) best = &w;
    }
    return best ? best->id : kCapturedByPopup;
  }
  const WindowState* best = nullptr;
  Layer bestLayer = Layer::Background;
  for (const WindowState& w : windows_) {
    if (!w.visible || w.popup) continue;
    if (!w.rect.contains(x, y)) continue;
    const Layer l = effectiveLayer(w);
    if (!best || drawsAbove(w, l, *best, bestLayer)) { best = &w; bestLayer = l; }
  }
  return best ? best->id : kNoTarget;
}

void WindowManager::bringToFront(int id) {
  // Acima do maior zOrder atual: a janela clicada fica por cima de todas as outras da camada.
  int top = 0;
  for (const WindowState& w : windows_) top = std::max(top, w.zOrder);
  for (WindowState& w : windows_) if (w.id == id) { w.zOrder = top + 1; return; }
}

Rect WindowManager::gripRect(const WindowState& w) const noexcept {
  return Rect{w.rect.x, w.rect.y, std::min(w.gripWidth, w.rect.w), std::min(w.titleHeight, w.rect.h)};
}

void WindowManager::applyDock(WindowState& w, bool docked) { w.docked = docked; }

bool WindowManager::beginPress(int id, float x, float y) {
  if (drag_.active) return false;
  WindowState* w = nullptr;
  for (WindowState& o : windows_) if (o.id == id) { w = &o; break; }
  if (!w || !w->visible) return false;
  if (!gripRect(*w).contains(x, y)) return false;   // so o icone de alca inicia arraste
  drag_.active = true;
  drag_.id = id;
  drag_.wasDocked = w->docked;
  drag_.moving = false;
  drag_.startX = x;   // ultimo ponto conhecido (movePointer atualiza)
  drag_.startY = y;
  bringToFront(id);
  return true;
}

bool WindowManager::movePointer(float x, float y) {
  if (!drag_.active) return false;
  WindowState* w = nullptr;
  for (WindowState& o : windows_) if (o.id == drag_.id) { w = &o; break; }
  if (!w) { drag_ = DragState{}; return false; }
  if (!drag_.moving) {
    const float dx = x - drag_.startX, dy = y - drag_.startY;
    // Janela encaixada precisa passar do limiar para sair do dock. Flutuante move desde o primeiro pixel.
    const float limit = drag_.wasDocked ? kUndockThreshold : 0.f;
    if (std::sqrt(dx * dx + dy * dy) <= limit) return false;
    drag_.moving = true;
    if (drag_.wasDocked) applyDock(*w, false);
  }
  const float dx = x - drag_.startX, dy = y - drag_.startY;
  if (dx == 0.f && dy == 0.f) return false;
  w->rect.x += dx;
  w->rect.y += dy;
  drag_.startX = x;
  drag_.startY = y;
  return true;
}

bool WindowManager::endPress() {
  if (!drag_.active) return false;
  const bool moved = drag_.moving;
  drag_ = DragState{};
  return moved;
}

}  // namespace editor::wm
