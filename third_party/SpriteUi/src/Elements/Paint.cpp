#include "SpriteUi/Elements/Paint.hpp"

#include "SpriteUi/Rendering/Primitives.hpp"

#include <algorithm>

namespace ToolEngine::SpriteUi::Elements {

Vec4 LightenColor(Vec4 color, float amount) noexcept {
    return {color.x + (1.0f - color.x) * amount, color.y + (1.0f - color.y) * amount,
            color.z + (1.0f - color.z) * amount, color.w};
}

Vec4 DarkenColor(Vec4 color, float amount) noexcept {
    return {color.x * (1.0f - amount), color.y * (1.0f - amount), color.z * (1.0f - amount), color.w};
}

void AppendOutlineItems(UIHandle owner, Rect box, float width, Vec4 color, float zOrder, std::uint64_t& paint,
                        std::vector<Rendering::UIRenderItem>& out) {
    const Vec2 p = box.position;
    const Vec2 s = box.size;
    const float innerHeight = std::max(0.0f, s.y - 2.0f * width);
    const Rect edges[] = {
        {p, {s.x, width}},
        {{p.x, p.y + s.y - width}, {s.x, width}},
        {{p.x, p.y + width}, {width, innerHeight}},
        {{p.x + s.x - width, p.y + width}, {width, innerHeight}},
    };
    for (const Rect& edge : edges) {
        out.push_back(Rendering::MakeSolidRect(owner, edge, color, Rendering::NoClipIndex, zOrder, paint++));
    }
}

void AppendRoundedItem(UIHandle owner, Rect rect, Vec4 color, float zOrder, std::uint64_t& paint,
                       std::vector<Rendering::UIRenderItem>& out) {
    const float radius = 0.5f * std::min(rect.size.x, rect.size.y);
    if (auto item = Rendering::MakeRoundedRect(owner, rect, radius, color, Rendering::NoClipIndex, zOrder, paint++)) {
        out.push_back(*item);
    }
}

Rect GrowRect(Rect rect, float amount) noexcept {
    return {{rect.position.x - amount, rect.position.y - amount}, {rect.size.x + 2.0f * amount, rect.size.y + 2.0f * amount}};
}

Rect RowInsideBorder(Rect row, Rect popup, float borderWidth) noexcept {
    const Rect inner = GrowRect(popup, -borderWidth);
    const float left = std::max(row.position.x, inner.position.x);
    const float top = std::max(row.position.y, inner.position.y);
    const float right = std::min(row.position.x + row.size.x, inner.position.x + inner.size.x);
    const float bottom = std::min(row.position.y + row.size.y, inner.position.y + inner.size.y);
    return {{left, top}, {std::max(0.0f, right - left), std::max(0.0f, bottom - top)}};
}

void AppendFill(UIHandle owner, Rect rect, float radius, Vec4 color, std::uint32_t clipIndex, float zOrder,
                std::uint64_t& paint, std::vector<Rendering::UIRenderItem>& out) {
    if (radius > 0.0f) {
        if (auto item = Rendering::MakeRoundedRect(owner, rect, radius, color, clipIndex, zOrder, paint++)) {
            out.push_back(*item);
        }
        return;
    }
    out.push_back(Rendering::MakeSolidRect(owner, rect, color, clipIndex, zOrder, paint++));
}

void AppendBox(UIHandle owner, Rect box, float radius, float borderWidth, Vec4 fill, Vec4 border,
               std::uint32_t clipIndex, float zOrder, std::uint64_t& paint,
               std::vector<Rendering::UIRenderItem>& out) {
    if (!(radius > 0.0f)) {  // Inclui NaN: sem raio valido, caixa reta.
        AppendFill(owner, box, 0.0f, fill, clipIndex, zOrder, paint, out);
        AppendOutlineItems(owner, box, borderWidth, border, zOrder, paint, out);
        return;
    }
    AppendFill(owner, box, radius, border, clipIndex, zOrder, paint, out);
    const Rect inner = GrowRect(box, -borderWidth);
    if (inner.size.x <= 0.0f || inner.size.y <= 0.0f) return;
    AppendFill(owner, inner, std::max(0.0f, radius - borderWidth), fill, clipIndex, zOrder, paint, out);
}

} // namespace ToolEngine::SpriteUi::Elements
