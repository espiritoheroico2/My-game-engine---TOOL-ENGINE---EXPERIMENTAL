#include "LegacyLayout.hpp"

#include <algorithm>
#include <cmath>

namespace ToolEngine::SpriteUi::Legacy {

namespace {

float ResolveLength(Length length, float available, float measured) noexcept {
    switch (length.mode) {
    case SizeMode::Fixed:
        return std::max(0.0f, length.value);
    case SizeMode::Percent:
        return std::max(0.0f, available * length.value);
    case SizeMode::Flex:
    case SizeMode::Auto:
    default:
        return std::max(0.0f, measured);
    }
}

float ClampSize(float value, float minVal, float maxVal) noexcept {
    if (!std::isfinite(value)) value = 0.0f;
    const float lo = std::max(0.0f, minVal);
    const float hi = std::max(lo, maxVal);
    return std::clamp(value, lo, hi);
}

} // namespace

Vec2 LegacyLayoutEngine::Measure(LayoutNode& node, Vec2 available, LayoutStats* stats) const {
    return MeasureImpl(node, available, stats);
}

Vec2 LegacyLayoutEngine::MeasureImpl(LayoutNode& node, Vec2 available, LayoutStats* stats) const {
    if (stats) ++stats->measured;

    float mainAxis = 0.0f;
    float crossAxis = 0.0f;
    const bool isRow = (node.style.direction == LayoutDirection::Row);

    const float padX = node.style.padding.left + node.style.padding.right;
    const float padY = node.style.padding.top + node.style.padding.bottom;

    // Percentuais dos filhos sao relativos a area interna DESTE no (e nao ao viewport raiz).
    // Quando o tamanho proprio ja e conhecido (Fixed/Percent) ele define essa area;
    // caso contrario (Auto/Flex) herda-se o espaco disponivel recebido.
    const auto knownExtent = [](Length length, float avail, float minV, float maxV) -> float {
        switch (length.mode) {
        case SizeMode::Fixed:   return ClampSize(length.value, minV, maxV);
        case SizeMode::Percent: return ClampSize(avail * length.value, minV, maxV);
        default:                return avail;
        }
    };
    const Vec2 childAvailable{
        std::max(0.0f, knownExtent(node.style.width, available.x,
                                   node.style.minSize.x, node.style.maxSize.x) - padX),
        std::max(0.0f, knownExtent(node.style.height, available.y,
                                   node.style.minSize.y, node.style.maxSize.y) - padY)
    };

    for (auto& child : node.children) {
        const Vec2 childMeasured = MeasureImpl(child, childAvailable, stats);
        const float childMain = isRow ? childMeasured.x : childMeasured.y;
        const float childCross = isRow ? childMeasured.y : childMeasured.x;

        const float mainMargins = isRow
            ? child.style.margin.left + child.style.margin.right
            : child.style.margin.top + child.style.margin.bottom;
        const float crossMargins = isRow
            ? child.style.margin.top + child.style.margin.bottom
            : child.style.margin.left + child.style.margin.right;

        mainAxis += childMain + mainMargins;
        crossAxis = std::max(crossAxis, childCross + crossMargins);
    }

    if (!node.children.empty()) {
        mainAxis += node.style.gap * static_cast<float>(node.children.size() - 1);
    }

    Vec2 content = isRow ? Vec2{mainAxis, crossAxis} : Vec2{crossAxis, mainAxis};
    content.x += padX;
    content.y += padY;

    node.measured = {
        ClampSize(ResolveLength(node.style.width, available.x, content.x),
                  node.style.minSize.x, node.style.maxSize.x),
        ClampSize(ResolveLength(node.style.height, available.y, content.y),
                  node.style.minSize.y, node.style.maxSize.y)
    };

    return node.measured;
}

void LegacyLayoutEngine::Arrange(LayoutNode& node, Rect bounds, LayoutStats* stats) const {
    ArrangeImpl(node, bounds, stats);
}

void LegacyLayoutEngine::ArrangeImpl(LayoutNode& node, Rect bounds, LayoutStats* stats) const {
    if (stats) ++stats->arranged;
    node.rect = bounds;
    if (node.children.empty()) return;

    const bool isRow = (node.style.direction == LayoutDirection::Row);

    const float padBeforeMain = isRow ? node.style.padding.left : node.style.padding.top;
    const float padAfterMain  = isRow ? node.style.padding.right : node.style.padding.bottom;
    const float padBeforeCross = isRow ? node.style.padding.top : node.style.padding.left;
    const float padAfterCross  = isRow ? node.style.padding.bottom : node.style.padding.right;

    float innerMain = (isRow ? bounds.size.x : bounds.size.y) - (padBeforeMain + padAfterMain);
    float innerCross = (isRow ? bounds.size.y : bounds.size.x) - (padBeforeCross + padAfterCross);
    innerMain = std::max(0.0f, innerMain);
    innerCross = std::max(0.0f, innerCross);

    const std::size_t count = node.children.size();
    const float totalGaps = node.style.gap * static_cast<float>(count - 1);

    std::vector<float> bases(count), minMain(count), maxMain(count), grow(count), shrink(count);
    std::vector<float> sizes(count);
    std::vector<bool> frozen(count, false);
    float margins = 0.0f;

    for (std::size_t i = 0; i < count; ++i) {
        const auto& child = node.children[i];
        const float margin = isRow
            ? child.style.margin.left + child.style.margin.right
            : child.style.margin.top + child.style.margin.bottom;
        margins += margin;
        bases[i] = std::max(0.0f, isRow ? child.measured.x : child.measured.y);
        minMain[i] = std::max(0.0f, isRow ? child.style.minSize.x : child.style.minSize.y);
        maxMain[i] = std::max(minMain[i], isRow ? child.style.maxSize.x : child.style.maxSize.y);
        grow[i] = std::max(0.0f, child.style.grow);
        shrink[i] = std::max(0.0f, child.style.shrink);
        sizes[i] = bases[i];
    }

    // Distribuicao iterativa grow/shrink com "freeze" de itens que violam min/max
    // (algoritmo de resolucao de comprimentos flexiveis do CSS Flexbox, simplificado).
    float baseSum = 0.0f;
    for (float b : bases) baseSum += b;
    const bool growing = (innerMain - (baseSum + margins + totalGaps)) > 0.0f;

    for (std::size_t pass = 0; pass <= count; ++pass) {
        float frozenSum = 0.0f;
        float weightSum = 0.0f;
        float unfrozenBase = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            if (frozen[i]) {
                frozenSum += sizes[i];
            } else {
                unfrozenBase += bases[i];
                weightSum += growing ? grow[i] : shrink[i] * bases[i];
            }
        }
        const float freeSpace = innerMain - margins - totalGaps - frozenSum - unfrozenBase;

        float totalViolation = 0.0f;
        std::vector<float> clampedSize(count), violation(count, 0.0f);
        bool anyUnfrozen = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (frozen[i]) continue;
            anyUnfrozen = true;
            float target = bases[i];
            if (weightSum > 0.0f && ((growing && freeSpace > 0.0f) || (!growing && freeSpace < 0.0f))) {
                const float weight = growing ? grow[i] : shrink[i] * bases[i];
                target += freeSpace * weight / weightSum;
            }
            target = std::max(0.0f, target);
            clampedSize[i] = std::clamp(target, minMain[i], maxMain[i]);
            violation[i] = clampedSize[i] - target;
            totalViolation += violation[i];
        }
        if (!anyUnfrozen) break;

        for (std::size_t i = 0; i < count; ++i) {
            if (frozen[i]) continue;
            sizes[i] = clampedSize[i];
            if (totalViolation == 0.0f ||
                (totalViolation > 0.0f && violation[i] > 0.0f) ||
                (totalViolation < 0.0f && violation[i] < 0.0f)) {
                frozen[i] = true;
            }
        }
        if (totalViolation == 0.0f) break;
    }

    float usedMain = margins + totalGaps;
    for (float size : sizes) usedMain += size;
    const float free = innerMain - usedMain; // Espaco remanescente apos grow/shrink/clamps.

    float initialOffset = 0.0f;
    float extraGap = 0.0f;

    if (free > 0.0f) {
        switch (node.style.justifyContent) {
        case JustifyContent::Center:
            initialOffset = free * 0.5f;
            break;
        case JustifyContent::FlexEnd:
            initialOffset = free;
            break;
        case JustifyContent::SpaceBetween:
            if (count > 1) extraGap = free / static_cast<float>(count - 1);
            break;
        case JustifyContent::SpaceAround: {
            const float share = free / static_cast<float>(count);
            initialOffset = share * 0.5f;
            extraGap = share;
            break;
        }
        case JustifyContent::SpaceEvenly: {
            const float step = free / static_cast<float>(count + 1);
            initialOffset = step;
            extraGap = step;
            break;
        }
        case JustifyContent::FlexStart:
        default:
            break;
        }
    }

    float cursor = (isRow ? bounds.position.x : bounds.position.y) + padBeforeMain + initialOffset;

    for (std::size_t i = 0; i < node.children.size(); ++i) {
        auto& child = node.children[i];

        const float beforeMarginMain = isRow ? child.style.margin.left : child.style.margin.top;
        const float afterMarginMain  = isRow ? child.style.margin.right : child.style.margin.bottom;

        const float mainSize = sizes[i];

        const float beforeMarginCross = isRow ? child.style.margin.top : child.style.margin.left;
        const float afterMarginCross  = isRow ? child.style.margin.bottom : child.style.margin.right;
        const float crossAvailable = std::max(0.0f, innerCross - beforeMarginCross - afterMarginCross);

        const float measuredCross = isRow ? child.measured.y : child.measured.x;
        const float minCross = isRow ? child.style.minSize.y : child.style.minSize.x;
        const float maxCross = isRow ? child.style.maxSize.y : child.style.maxSize.x;

        // Stretch so se aplica quando o tamanho transversal nao foi definido explicitamente
        // (Auto/Flex), como no CSS; Fixed/Percent mantem o valor medido.
        const Length crossLength = isRow ? child.style.height : child.style.width;
        const bool explicitCross = crossLength.mode == SizeMode::Fixed ||
                                   crossLength.mode == SizeMode::Percent;
        float crossSize = (child.style.alignItems == AlignItems::Stretch && !explicitCross)
            ? ClampSize(crossAvailable, minCross, maxCross)
            : ClampSize(measuredCross, minCross, maxCross);
        crossSize = std::min(crossSize, crossAvailable);

        float crossOffset = beforeMarginCross;
        if (child.style.alignItems == AlignItems::Center) {
            crossOffset += std::max(0.0f, crossAvailable - crossSize) * 0.5f;
        } else if (child.style.alignItems == AlignItems::End) {
            crossOffset += std::max(0.0f, crossAvailable - crossSize);
        }

        cursor += beforeMarginMain;

        Rect childBounds;
        if (isRow) {
            childBounds = {
                {cursor, bounds.position.y + padBeforeCross + crossOffset},
                {mainSize, crossSize}
            };
        } else {
            childBounds = {
                {bounds.position.x + padBeforeCross + crossOffset, cursor},
                {crossSize, mainSize}
            };
        }

        ArrangeImpl(child, childBounds, stats);
        cursor += mainSize + afterMarginMain + node.style.gap + extraGap;
    }
}

} // namespace ToolEngine::SpriteUi::Legacy
