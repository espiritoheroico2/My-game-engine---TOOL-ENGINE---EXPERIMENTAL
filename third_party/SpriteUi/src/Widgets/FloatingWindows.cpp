#include "SpriteUi/Widgets/FloatingWindows.hpp"

#include "SpriteUi/Widgets/DockOps.hpp"

#include <algorithm>
#include <limits>

namespace ToolEngine::SpriteUi::Widgets {

namespace {

constexpr std::uint32_t kMaxZ = std::numeric_limits<std::uint32_t>::max();

bool IsUsableRect(Rect r) noexcept {
    return IsFinite(r.position) && IsFinite(r.size) && r.size.x > 0.0f && r.size.y > 0.0f;
}

// Mantem a janela dentro do canvas: sobra uma faixa horizontal visivel e a barra de titulo inteira na vertical.
Rect ClampToCanvas(Rect r, Rect canvas, float titleBarHeight) noexcept {
    if (!(canvas.size.x > 0.0f) || !(canvas.size.y > 0.0f)) return r;

    const float grab = std::min({kFloatingGrabWidth, r.size.x, canvas.size.x});
    const float minX = canvas.position.x - r.size.x + grab;
    const float maxX = canvas.position.x + canvas.size.x - grab;
    r.position.x = std::clamp(r.position.x, minX, maxX);

    const float title = std::min(titleBarHeight, canvas.size.y);
    const float minY = canvas.position.y;
    const float maxY = std::max(minY, canvas.position.y + canvas.size.y - title);
    r.position.y = std::clamp(r.position.y, minY, maxY);
    return r;
}

// Folha mais a esquerda (no topo) da arvore: alvo padrao quando a janela nao tem grupo de origem.
UIDockNode& FirstLeaf(UIDockNode& root) noexcept {
    UIDockNode* node = &root;
    while (node->isSplit && node->firstChild) node = node->firstChild.get();
    return *node;
}

// Outra janela do mesmo grupo de abas da folha que contem `window` (ou o conteudo unico irmao). Vazio se nao ha.
UIHandle GroupMateOf(UIDockNode& root, UIHandle window) noexcept {
    UIDockNode* leaf = FindLeafContaining(root, window);
    if (!leaf) return UIHandle{};
    for (const UITab& tab : leaf->tabs.GetTabs()) {
        if (tab.contentHandle != window) return tab.contentHandle;
    }
    if (leaf->content.IsValid() && leaf->content != window) return leaf->content;
    return UIHandle{};
}

} // namespace

FloatingWindowManager::FloatingWindowManager(Rect canvas, std::uint32_t firstZ) noexcept
    : canvas_{canvas}, nextZ_{firstZ == 0 ? 1u : firstZ} {}

void FloatingWindowManager::SetCanvas(Rect canvas) noexcept {
    canvas_ = canvas;
    for (FloatingWindow& w : windows_) {
        if (w.state == FloatState::Maximized) {
            w.rect = canvas_;
        } else {
            w.rect = ClampToCanvas(w.rect, canvas_, w.titleBarHeight);
        }
    }
}

FloatingWindow* FloatingWindowManager::FindMutable(UIHandle window) noexcept {
    for (FloatingWindow& w : windows_) {
        if (w.window == window) return &w;
    }
    return nullptr;
}

const FloatingWindow* FloatingWindowManager::Find(UIHandle window) const noexcept {
    for (const FloatingWindow& w : windows_) {
        if (w.window == window) return &w;
    }
    return nullptr;
}

std::uint32_t FloatingWindowManager::TakeZ() noexcept {
    if (nextZ_ == kMaxZ) RenumberZ();
    return nextZ_++;
}

void FloatingWindowManager::RenumberZ() noexcept {
    // Mantem a ordem relativa atual e compacta os valores em 1..n.
    std::vector<std::size_t> order(windows_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        return windows_[a].zOrder < windows_[b].zOrder;
    });
    std::uint32_t z = 1;
    for (std::size_t i : order) windows_[i].zOrder = z++;
    nextZ_ = z;
}

bool FloatingWindowManager::Float(UIDockNode& root, UIHandle window, Rect rect, Vec2 minSize,
                                  float titleBarHeight) {
    if (!window.IsValid() || Find(window)) return false;
    if (!IsUsableRect(rect) || !IsFinite(minSize) || !(minSize.x > 0.0f) || !(minSize.y > 0.0f)) return false;
    if (!(titleBarHeight > 0.0f) || !IsFinite(titleBarHeight)) return false;

    // O grupo de origem precisa ser lembrado antes de a janela sair da arvore.
    const UIHandle anchor = GroupMateOf(root, window);
    (void)RemoveFromDock(root, window);   // false quando a janela ja nao estava no dock: segue como flutuante

    rect.size.x = std::max(rect.size.x, minSize.x);
    rect.size.y = std::max(rect.size.y, minSize.y);
    FloatingWindow w;
    w.window = window;
    w.minSize = minSize;
    w.titleBarHeight = titleBarHeight;
    w.rect = ClampToCanvas(rect, canvas_, titleBarHeight);
    w.restoreRect = w.rect;
    w.zOrder = TakeZ();
    w.dockAnchor = anchor;
    windows_.push_back(w);
    return true;
}

bool FloatingWindowManager::Dock(UIDockNode& root, UIHandle window, DockSlot slot, UIHandle targetLeaf,
                                 float ratio) {
    FloatingWindow* w = FindMutable(window);
    if (!w || slot == DockSlot::None) return false;
    if (FindLeafContaining(root, window)) return false;   // ja esta no dock: nada a fazer

    const UIHandle anchor = targetLeaf.IsValid() ? targetLeaf : w->dockAnchor;
    UIDockNode* target = anchor.IsValid() ? FindLeafContaining(root, anchor) : nullptr;
    if (!target) target = &FirstLeaf(root);

    const UIHandle visible = target->VisibleContent();
    bool ok = false;
    if (!visible.IsValid()) {
        // Folha vazia (raiz sem conteudo): a janela vira o conteudo, sem dividir.
        ok = target->Split(DockSlot::Center, ratio, window);
    } else {
        const DockDropResult drop{slot == DockSlot::Center ? DockAction::Tab : DockAction::Dock, slot, visible};
        ok = ApplyDropResult(root, window, drop, ratio);
    }
    if (!ok) return false;

    windows_.erase(windows_.begin() + (w - windows_.data()));
    return true;
}

bool FloatingWindowManager::Move(UIHandle window, float dx, float dy) noexcept {
    FloatingWindow* w = FindMutable(window);
    if (!w || w->state == FloatState::Maximized) return false;
    if (!IsFinite(dx) || !IsFinite(dy)) return false;
    w->rect.position.x += dx;
    w->rect.position.y += dy;
    w->rect = ClampToCanvas(w->rect, canvas_, w->titleBarHeight);
    return true;
}

bool FloatingWindowManager::Resize(UIHandle window, ResizeEdge edge, float dx, float dy) noexcept {
    FloatingWindow* w = FindMutable(window);
    if (!w || w->state != FloatState::Normal || edge == ResizeEdge::None) return false;
    if (!IsFinite(dx) || !IsFinite(dy)) return false;

    const bool left = edge == ResizeEdge::Left || edge == ResizeEdge::TopLeft || edge == ResizeEdge::BottomLeft;
    const bool right = edge == ResizeEdge::Right || edge == ResizeEdge::TopRight || edge == ResizeEdge::BottomRight;
    const bool top = edge == ResizeEdge::Top || edge == ResizeEdge::TopLeft || edge == ResizeEdge::TopRight;
    const bool bottom = edge == ResizeEdge::Bottom || edge == ResizeEdge::BottomLeft || edge == ResizeEdge::BottomRight;

    const float l = w->rect.position.x;
    const float t = w->rect.position.y;
    const float r = l + w->rect.size.x;
    const float b = t + w->rect.size.y;

    float nl = l, nr = r, nt = t, nb = b;
    if (left) nl = std::min(l + dx, r - w->minSize.x);     // a borda direita fica parada no minimo
    if (right) nr = std::max(r + dx, l + w->minSize.x);    // a borda esquerda fica parada no minimo
    if (top) nt = std::min(t + dy, b - w->minSize.y);
    if (bottom) nb = std::max(b + dy, t + w->minSize.y);

    w->rect = Rect{{nl, nt}, {nr - nl, nb - nt}};
    w->rect = ClampToCanvas(w->rect, canvas_, w->titleBarHeight);
    w->restoreRect = w->rect;
    return true;
}

bool FloatingWindowManager::BringToFront(UIHandle window) noexcept {
    FloatingWindow* w = FindMutable(window);
    if (!w) return false;
    bool isTop = true;
    for (const FloatingWindow& other : windows_) {
        if (other.zOrder > w->zOrder) {
            isTop = false;
            break;
        }
    }
    if (isTop) return true;
    w->zOrder = TakeZ();   // RenumberZ, se rodar aqui, so troca valores: o vetor e o ponteiro w continuam validos
    return true;
}

bool FloatingWindowManager::Maximize(UIHandle window) noexcept {
    FloatingWindow* w = FindMutable(window);
    if (!w) return false;
    if (w->state == FloatState::Maximized) return true;
    if (w->state == FloatState::Normal) w->restoreRect = w->rect;
    w->rect = canvas_;
    w->state = FloatState::Maximized;
    return true;
}

bool FloatingWindowManager::Restore(UIHandle window) noexcept {
    FloatingWindow* w = FindMutable(window);
    if (!w || w->state == FloatState::Normal) return false;
    if (w->state == FloatState::Maximized) w->rect = w->restoreRect;
    w->state = FloatState::Normal;
    return true;
}

bool FloatingWindowManager::Minimize(UIHandle window) noexcept {
    FloatingWindow* w = FindMutable(window);
    if (!w) return false;
    if (w->state == FloatState::Minimized) return true;
    if (w->state == FloatState::Maximized) w->rect = w->restoreRect;
    w->state = FloatState::Minimized;
    return true;
}

FloatingSnap FloatingWindowManager::SuggestSnap(Vec2 pointer, std::span<const DockLeafRect> leaves,
                                                float edgeFraction, float canvasMargin) const noexcept {
    FloatingSnap snap{};
    if (!IsFinite(pointer) || !(canvas_.size.x > 0.0f) || !(canvas_.size.y > 0.0f)) return snap;

    // Borda do canvas: a menor distancia dentro da margem decide o lado.
    if (canvasMargin > 0.0f) {
        const float dl = pointer.x - canvas_.position.x;
        const float dr = canvas_.Max().x - pointer.x;
        const float dt = pointer.y - canvas_.position.y;
        const float db = canvas_.Max().y - pointer.y;
        float best = canvasMargin;
        bool found = false;
        auto consider = [&](float d, DockSlot s) {
            if (d >= 0.0f && d < best) {
                best = d;
                snap.slot = s;
                found = true;
            }
        };
        consider(dl, DockSlot::Left);
        consider(dr, DockSlot::Right);
        consider(dt, DockSlot::Top);
        consider(db, DockSlot::Bottom);
        if (found) {
            snap.canvasEdge = true;
            return snap;
        }
    }

    // Folhas visiveis: o slot vem de ComputeDropSlot, como no arrasto do dock.
    for (const DockLeafRect& leaf : leaves) {
        if (!leaf.leaf.IsValid() || !leaf.bounds.Contains(pointer)) continue;
        snap.slot = ComputeDropSlot(leaf.bounds, pointer, edgeFraction);
        snap.targetLeaf = snap.slot == DockSlot::None ? UIHandle{} : leaf.leaf;
        return snap;
    }
    return snap;
}

} // namespace ToolEngine::SpriteUi::Widgets
