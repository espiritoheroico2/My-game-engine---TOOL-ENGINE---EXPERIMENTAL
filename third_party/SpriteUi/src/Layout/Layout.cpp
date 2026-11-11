#include "SpriteUi/Layout/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ToolEngine::SpriteUi {

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

float FiniteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

// Replaces non-finite style values (NaN/Inf from corrupt data or scripts) so they can never leak into
// the computed rectangles: lengths/margins/padding/gap/grow/min -> 0, shrink -> 1 (its default),
// max -> unbounded, baseline -> "none". Finite values, including a +Inf maxSize, are left untouched.
void SanitizeStyle(FlexItem& style) noexcept {
    const auto zero = [](float& v) { if (!std::isfinite(v)) v = 0.0f; };
    zero(style.width.value);
    zero(style.height.value);
    zero(style.minSize.x);
    zero(style.minSize.y);
    zero(style.margin.left);
    zero(style.margin.top);
    zero(style.margin.right);
    zero(style.margin.bottom);
    zero(style.padding.left);
    zero(style.padding.top);
    zero(style.padding.right);
    zero(style.padding.bottom);
    zero(style.gap);
    zero(style.grow);
    if (!std::isfinite(style.shrink)) style.shrink = 1.0f;
    const float unbounded = std::numeric_limits<float>::infinity();
    if (std::isnan(style.maxSize.x)) style.maxSize.x = unbounded;
    if (std::isnan(style.maxSize.y)) style.maxSize.y = unbounded;
    if (!std::isfinite(style.baseline)) style.baseline = -1.0f;
}

// One line of a flex container (the whole child list when the container does not wrap).
struct LineSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    float main = 0.0f;     // Sum of the outer main sizes + gaps (hypothetical, before grow/shrink).
    float cross = 0.0f;    // Natural cross size of the line (baseline-aware).
    float ascent = 0.0f;   // Distance from the line's cross start to the shared baseline.
};

float MainMargins(const FlexItem& style, bool isRow) noexcept {
    return isRow ? style.margin.left + style.margin.right : style.margin.top + style.margin.bottom;
}

float CrossMargins(const FlexItem& style, bool isRow) noexcept {
    return isRow ? style.margin.top + style.margin.bottom : style.margin.left + style.margin.right;
}

bool UsesBaseline(const LayoutNode& child, bool isRow) noexcept {
    return isRow && child.style.alignItems == AlignItems::Baseline;
}

float BaselineFromTop(const LayoutNode& child, float height) noexcept {
    const float baseline = child.style.baseline;
    if (!std::isfinite(baseline) || baseline < 0.0f) return height; // No baseline: bottom edge.
    return std::min(baseline, height);
}

// Splits the children into lines using their MEASURED sizes. Without wrap there is exactly one line.
void BuildLines(
    const std::vector<LayoutNode>& kids, bool isRow, float availableMain, float gap, bool wrap,
    std::vector<LineSpan>& lines)
{
    lines.clear();
    if (kids.empty()) return;

    std::size_t begin = 0;
    float lineMain = 0.0f;
    for (std::size_t i = 0; i < kids.size(); ++i) {
        const LayoutNode& child = kids[i];
        const float outer = std::max(0.0f, isRow ? child.measured.x : child.measured.y) + MainMargins(child.style, isRow);
        if (i == begin) {
            lineMain = outer;
            continue;
        }
        if (wrap && std::isfinite(availableMain) && lineMain + gap + outer > availableMain + 1.0e-4f) {
            LineSpan closed;
            closed.begin = begin;
            closed.end = i;
            closed.main = lineMain;
            lines.push_back(closed);
            begin = i;
            lineMain = outer;
        } else {
            lineMain += gap + outer;
        }
    }
    LineSpan last;
    last.begin = begin;
    last.end = kids.size();
    last.main = lineMain;
    lines.push_back(last);

    for (LineSpan& line : lines) {
        float maxAscent = 0.0f;
        float maxDescent = 0.0f;
        float otherCross = 0.0f;
        for (std::size_t i = line.begin; i < line.end; ++i) {
            const LayoutNode& child = kids[i];
            const float crossSize = std::max(0.0f, isRow ? child.measured.y : child.measured.x);
            if (UsesBaseline(child, isRow)) {
                const float baseline = BaselineFromTop(child, crossSize);
                maxAscent = std::max(maxAscent, child.style.margin.top + baseline);
                maxDescent = std::max(maxDescent, (crossSize - baseline) + child.style.margin.bottom);
            } else {
                otherCross = std::max(otherCross, crossSize + CrossMargins(child.style, isRow));
            }
        }
        line.ascent = maxAscent;
        line.cross = std::max(otherCross, maxAscent + maxDescent);
    }
}

// Iterative grow/shrink with "freeze" of items violating min/max (simplified CSS Flexbox "resolve
// flexible lengths") for the children [begin, end). Results go to sizes[begin..end).
void ResolveMainSizes(
    const std::vector<LayoutNode>& kids, std::size_t begin, std::size_t end, bool isRow,
    float innerMain, float gap, std::vector<float>& sizes)
{
    const std::size_t count = end - begin;
    const float totalGaps = gap * static_cast<float>(count - 1);

    std::vector<float> bases(count), minMain(count), maxMain(count), grow(count), shrink(count);
    std::vector<float> local(count), clampedSize(count), violation(count, 0.0f); // Hoisted out of the pass loop.
    std::vector<bool> frozen(count, false);
    float margins = 0.0f;

    for (std::size_t k = 0; k < count; ++k) {
        const LayoutNode& child = kids[begin + k];
        margins += MainMargins(child.style, isRow);
        bases[k] = std::max(0.0f, isRow ? child.measured.x : child.measured.y);
        minMain[k] = std::max(0.0f, isRow ? child.style.minSize.x : child.style.minSize.y);
        maxMain[k] = std::max(minMain[k], isRow ? child.style.maxSize.x : child.style.maxSize.y);
        grow[k] = std::max(0.0f, child.style.grow);
        shrink[k] = std::max(0.0f, child.style.shrink);
        local[k] = bases[k];
    }

    float baseSum = 0.0f;
    for (float b : bases) baseSum += b;
    const bool growing = (innerMain - (baseSum + margins + totalGaps)) > 0.0f;

    for (std::size_t pass = 0; pass <= count; ++pass) {
        float frozenSum = 0.0f;
        float weightSum = 0.0f;
        float unfrozenBase = 0.0f;
        for (std::size_t k = 0; k < count; ++k) {
            if (frozen[k]) {
                frozenSum += local[k];
            } else {
                unfrozenBase += bases[k];
                weightSum += growing ? grow[k] : shrink[k] * bases[k];
            }
        }
        const float freeSpace = innerMain - margins - totalGaps - frozenSum - unfrozenBase;

        float totalViolation = 0.0f;
        bool anyUnfrozen = false;
        for (std::size_t k = 0; k < count; ++k) {
            if (frozen[k]) continue;
            anyUnfrozen = true;
            float target = bases[k];
            if (weightSum > 0.0f && ((growing && freeSpace > 0.0f) || (!growing && freeSpace < 0.0f))) {
                const float weight = growing ? grow[k] : shrink[k] * bases[k];
                target += freeSpace * weight / weightSum;
            }
            target = std::max(0.0f, target);
            clampedSize[k] = std::clamp(target, minMain[k], maxMain[k]);
            violation[k] = clampedSize[k] - target;
            totalViolation += violation[k];
        }
        if (!anyUnfrozen) break;

        for (std::size_t k = 0; k < count; ++k) {
            if (frozen[k]) continue;
            local[k] = clampedSize[k];
            if (totalViolation == 0.0f ||
                (totalViolation > 0.0f && violation[k] > 0.0f) ||
                (totalViolation < 0.0f && violation[k] < 0.0f)) {
                frozen[k] = true;
            }
        }
        if (totalViolation == 0.0f) break;
    }

    for (std::size_t k = 0; k < count; ++k) sizes[begin + k] = local[k];
}

void ComputeJustify(JustifyContent mode, float free, std::size_t count, float& initialOffset, float& extraGap) noexcept {
    initialOffset = 0.0f;
    extraGap = 0.0f;
    if (free <= 0.0f) return;
    switch (mode) {
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

void ComputeAlignContent(
    AlignContent mode, float free, std::size_t lines,
    float& initialOffset, float& extraBetween, float& stretchEach) noexcept
{
    initialOffset = 0.0f;
    extraBetween = 0.0f;
    stretchEach = 0.0f;
    if (free <= 0.0f) return;
    switch (mode) {
    case AlignContent::Center:
        initialOffset = free * 0.5f;
        break;
    case AlignContent::End:
        initialOffset = free;
        break;
    case AlignContent::SpaceBetween:
        if (lines > 1) extraBetween = free / static_cast<float>(lines - 1);
        break;
    case AlignContent::SpaceAround: {
        const float share = free / static_cast<float>(lines);
        initialOffset = share * 0.5f;
        extraBetween = share;
        break;
    }
    case AlignContent::SpaceEvenly: {
        const float step = free / static_cast<float>(lines + 1);
        initialOffset = step;
        extraBetween = step;
        break;
    }
    case AlignContent::Stretch:
        stretchEach = free / static_cast<float>(lines);
        break;
    case AlignContent::Start:
    default:
        break;
    }
}

} // namespace

Vec2 LayoutEngine::Measure(LayoutNode& node, Vec2 available, LayoutStats* stats) const {
    return MeasureImpl(node, available, stats);
}

Vec2 LayoutEngine::MeasureImpl(LayoutNode& node, Vec2 available, LayoutStats* stats) const {
    if (stats) ++stats->measured;
    SanitizeStyle(node.style);

    float mainAxis = 0.0f;
    float crossAxis = 0.0f;
    const bool isRow = (node.style.direction == LayoutDirection::Row);

    const float padX = node.style.padding.left + node.style.padding.right;
    const float padY = node.style.padding.top + node.style.padding.bottom;
    const float gap = FiniteOr(node.style.gap, 0.0f);

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

    bool anyBaseline = false;
    for (auto& child : node.children) {
        (void)MeasureImpl(child, childAvailable, stats);
        anyBaseline = anyBaseline || UsesBaseline(child, isRow);
    }

    const bool wrap = node.style.wrap == FlexWrap::Wrap;
    if (wrap || anyBaseline) {
        // Line-aware measurement: the cross size depends on how many lines the children form
        // (wrap) and on the shared baseline (baseline alignment).
        std::vector<LineSpan> lines;
        BuildLines(node.children, isRow, isRow ? childAvailable.x : childAvailable.y, gap, wrap, lines);
        for (const LineSpan& line : lines) {
            mainAxis = std::max(mainAxis, line.main);
            crossAxis += line.cross;
        }
        if (lines.size() > 1) crossAxis += gap * static_cast<float>(lines.size() - 1);
    } else {
        for (const auto& child : node.children) {
            const float childMain = isRow ? child.measured.x : child.measured.y;
            const float childCross = isRow ? child.measured.y : child.measured.x;
            mainAxis += childMain + MainMargins(child.style, isRow);
            crossAxis = std::max(crossAxis, childCross + CrossMargins(child.style, isRow));
        }
        if (!node.children.empty()) {
            mainAxis += gap * static_cast<float>(node.children.size() - 1);
        }
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

void LayoutEngine::Arrange(LayoutNode& node, Rect bounds, LayoutStats* stats) const {
    ArrangeImpl(node, bounds, stats);
}

void LayoutEngine::ArrangeImpl(LayoutNode& node, Rect bounds, LayoutStats* stats) const {
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
    const float gap = FiniteOr(node.style.gap, 0.0f);
    const bool wrap = node.style.wrap == FlexWrap::Wrap;

    std::vector<LineSpan> lines;
    BuildLines(node.children, isRow, innerMain, gap, wrap, lines);
    const std::size_t lineCount = lines.size();

    // Cross-axis space of each line. Without wrap the single line owns the whole cross extent, which
    // is exactly the behaviour of the engine before wrap existed.
    std::vector<float> lineCross(lineCount, innerCross);
    float crossInitial = 0.0f;
    float crossExtra = 0.0f;
    if (wrap) {
        float natural = gap * static_cast<float>(lineCount - 1);
        for (const LineSpan& line : lines) natural += line.cross;
        float stretchEach = 0.0f;
        ComputeAlignContent(node.style.alignContent, innerCross - natural, lineCount, crossInitial, crossExtra, stretchEach);
        for (std::size_t l = 0; l < lineCount; ++l) lineCross[l] = lines[l].cross + stretchEach;
    }

    std::vector<float> sizes(count, 0.0f);
    float crossCursor = padBeforeCross + crossInitial; // Relative to the cross origin of `bounds`.

    for (std::size_t l = 0; l < lineCount; ++l) {
        const LineSpan& line = lines[l];
        ResolveMainSizes(node.children, line.begin, line.end, isRow, innerMain, gap, sizes);

        const std::size_t lineItems = line.end - line.begin;
        float usedMain = gap * static_cast<float>(lineItems - 1);
        for (std::size_t i = line.begin; i < line.end; ++i) {
            usedMain += sizes[i] + MainMargins(node.children[i].style, isRow);
        }
        float initialOffset = 0.0f;
        float extraGap = 0.0f;
        ComputeJustify(node.style.justifyContent, innerMain - usedMain, lineItems, initialOffset, extraGap);

        float cursor = (isRow ? bounds.position.x : bounds.position.y) + padBeforeMain + initialOffset;

        for (std::size_t i = line.begin; i < line.end; ++i) {
            auto& child = node.children[i];

            const float beforeMarginMain = isRow ? child.style.margin.left : child.style.margin.top;
            const float afterMarginMain  = isRow ? child.style.margin.right : child.style.margin.bottom;

            const float mainSize = sizes[i];

            const float beforeMarginCross = isRow ? child.style.margin.top : child.style.margin.left;
            const float afterMarginCross  = isRow ? child.style.margin.bottom : child.style.margin.right;
            const float crossAvailable = std::max(0.0f, lineCross[l] - beforeMarginCross - afterMarginCross);

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
            } else if (UsesBaseline(child, isRow)) {
                // Top margin edge sits at (shared ascent - own ascent) so every baseline coincides.
                const float ownAscent = child.style.margin.top + BaselineFromTop(child, crossSize);
                crossOffset += std::max(0.0f, line.ascent - ownAscent);
            }

            cursor += beforeMarginMain;

            const float crossOrigin = (isRow ? bounds.position.y : bounds.position.x) + crossCursor + crossOffset;
            Rect childBounds;
            if (isRow) {
                childBounds = {{cursor, crossOrigin}, {mainSize, crossSize}};
            } else {
                childBounds = {{crossOrigin, cursor}, {crossSize, mainSize}};
            }

            ArrangeImpl(child, childBounds, stats);
            cursor += mainSize + afterMarginMain + gap + extraGap;
        }
        crossCursor += lineCross[l] + gap + crossExtra;
    }
}

} // namespace ToolEngine::SpriteUi
