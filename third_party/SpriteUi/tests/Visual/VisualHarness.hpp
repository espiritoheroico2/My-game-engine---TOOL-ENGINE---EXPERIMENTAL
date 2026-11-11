#pragma once

// Visual test harness: a CPU "GPU" for SpriteUi.
//
//  - SoftwareRenderBackend implements IRenderBackend and rasterizes a FramePacket with the same
//    math as shaders/SpriteUI.hlsl: unit-quad affine transform, uvRect, premultiplied tint, texture
//    slices, ClipRects and the analytic rounded panel. It consumes exactly what a real backend gets.
//  - Scene builds UIRenderItems (rectangles, outlines, text glyph boxes, carets) and runs them
//    through the REAL RenderExtractor, so a screenshot proves the whole CPU side of the pipeline.
//  - WritePng needs no zlib (stored deflate blocks); tests save a PNG AND assert on pixels, so a
//    screenshot test is also a regression test (no fragile golden files).

#include "SpriteUi/Rendering/PanelSdf.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/SpriteUi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace SpriteUiTests::Visual {

namespace SU = ToolEngine::SpriteUi;
namespace R = ToolEngine::SpriteUi::Rendering;

[[nodiscard]] inline float LinearToSrgbChannel(float v) noexcept {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// ---------------------------------------------------------------- image
struct Image {
    int width = 0;
    int height = 0;
    std::vector<float> data; // RGBA, premultiplied, LINEAR (the space the shader blends in)

    Image(int w, int h, SU::Vec4 clearLinearPremultiplied) : width(w), height(h) {
        data.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
        for (std::size_t i = 0; i < data.size(); i += 4) {
            data[i] = clearLinearPremultiplied.x;
            data[i + 1] = clearLinearPremultiplied.y;
            data[i + 2] = clearLinearPremultiplied.z;
            data[i + 3] = clearLinearPremultiplied.w;
        }
    }

    [[nodiscard]] float* Pixel(int x, int y) noexcept {
        return &data[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u];
    }
    [[nodiscard]] const float* Pixel(int x, int y) const noexcept {
        return &data[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u];
    }

    // Straight-alpha sRGB8, what a viewer shows.
    [[nodiscard]] std::array<std::uint8_t, 4> Srgb8(int x, int y) const noexcept {
        const float* p = Pixel(x, y);
        const float a = p[3];
        std::array<std::uint8_t, 4> out{};
        for (int c = 0; c < 3; ++c) {
            const float straight = a > 1.0e-6f ? p[c] / a : 0.0f;
            out[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::lround(LinearToSrgbChannel(straight) * 255.0f));
        }
        out[3] = static_cast<std::uint8_t>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
        return out;
    }
};

// True when the pixel at (x, y) is within `tolerance` (0..255 per channel) of the sRGB colour.
[[nodiscard]] inline bool PixelIs(const Image& image, int x, int y, SU::Vec4 srgb, int tolerance = 3) noexcept {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return false;
    const auto px = image.Srgb8(x, y);
    const float expect[3] = {srgb.x, srgb.y, srgb.z};
    for (int c = 0; c < 3; ++c) {
        const int e = static_cast<int>(std::lround(std::clamp(expect[c], 0.0f, 1.0f) * 255.0f));
        if (std::abs(static_cast<int>(px[static_cast<std::size_t>(c)]) - e) > tolerance) return false;
    }
    return true;
}

// Number of pixels whose colour differs from `srgb` by more than `tolerance`.
[[nodiscard]] inline std::size_t CountPixelsNot(const Image& image, SU::Vec4 srgb, int tolerance = 3) noexcept {
    std::size_t n = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (!PixelIs(image, x, y, srgb, tolerance)) ++n;
        }
    }
    return n;
}

// ---------------------------------------------------------------- PNG (no zlib)
namespace detail {

inline std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) noexcept {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

inline void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

inline void AppendChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& payload) {
    PutU32(out, static_cast<std::uint32_t>(payload.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), payload.begin(), payload.end());
    out.insert(out.end(), body.begin(), body.end());
    PutU32(out, Crc32(body.data(), body.size()));
}

} // namespace detail

[[nodiscard]] inline std::vector<std::uint8_t> EncodePng(const Image& image) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(image.height) * (static_cast<std::size_t>(image.width) * 4u + 1u));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0); // filter: none
        for (int x = 0; x < image.width; ++x) {
            const auto px = image.Srgb8(x, y);
            raw.insert(raw.end(), px.begin(), px.end());
        }
    }
    // zlib stream made of stored (uncompressed) deflate blocks.
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : raw) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    detail::PutU32(z, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    detail::PutU32(ihdr, static_cast<std::uint32_t>(image.width));
    detail::PutU32(ihdr, static_cast<std::uint32_t>(image.height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
    detail::AppendChunk(png, "IHDR", ihdr);
    detail::AppendChunk(png, "IDAT", z);
    detail::AppendChunk(png, "IEND", {});
    return png;
}

// Directory for screenshots: $SPRITEUI_SNAPSHOT_DIR, else ./snapshots.
[[nodiscard]] inline std::filesystem::path SnapshotDir() {
#if defined(_MSC_VER)
    char* env = nullptr;
    std::size_t envSize = 0;
    if (_dupenv_s(&env, &envSize, "SPRITEUI_SNAPSHOT_DIR") == 0 && env != nullptr) {
        const std::filesystem::path path(env);
        std::free(env);
        if (!path.empty()) return path;
    }
#else
    const char* env = std::getenv("SPRITEUI_SNAPSHOT_DIR");
    if (env && *env) return std::filesystem::path(env);
#endif
    return "snapshots";
}

// Returns the written path (empty on failure; a failed save never fails a test by itself).
inline std::string SaveSnapshot(const Image& image, const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(SnapshotDir(), ec);
    const auto path = SnapshotDir() / (name + ".png");
    std::ofstream file(path, std::ios::binary);
    if (!file) return {};
    const auto png = EncodePng(image);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) return {};
    return path.string();
}

// ---------------------------------------------------------------- CPU "GPU"
using TextureFunction = std::function<SU::Vec4(std::uint32_t slice, SU::Vec2 uv)>;

[[nodiscard]] inline SU::Vec4 WhiteTexture(std::uint32_t, SU::Vec2) noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }

class SoftwareRenderBackend final : public R::IRenderBackend {
public:
    SoftwareRenderBackend(Image& target, float dpiScale = 1.0f, TextureFunction texture = WhiteTexture)
        : target_(target), dpi_(dpiScale > 0.0f ? dpiScale : 1.0f), texture_(std::move(texture)) {}

    bool Submit(const R::FramePacket& packet) override {
        if (!R::IsFramePacketValid(packet)) return false;
        for (const R::DrawBatch& batch : packet.batches) {
            for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
                Rasterize(packet.instances[batch.firstInstance + i], packet.clipRects);
            }
        }
        ++frames_;
        return true;
    }

    [[nodiscard]] std::size_t FramesSubmitted() const noexcept { return frames_; }

private:
    void Rasterize(const R::SpriteInstanceGPU& inst, const std::vector<R::ClipRectGPU>& clips) {
        const float m00 = inst.transformRow0[0] * dpi_, m01 = inst.transformRow0[1] * dpi_, tx = inst.transformRow0[2] * dpi_;
        const float m10 = inst.transformRow1[0] * dpi_, m11 = inst.transformRow1[1] * dpi_, ty = inst.transformRow1[2] * dpi_;
        const float det = m00 * m11 - m01 * m10;
        if (std::abs(det) < 1.0e-9f) return;

        const float xs[4] = {tx, tx + m00, tx + m01, tx + m00 + m01};
        const float ys[4] = {ty, ty + m10, ty + m11, ty + m10 + m11};
        const int x0 = std::max(0, static_cast<int>(std::floor(*std::min_element(xs, xs + 4))));
        const int x1 = std::min(target_.width - 1, static_cast<int>(std::ceil(*std::max_element(xs, xs + 4))));
        const int y0 = std::max(0, static_cast<int>(std::floor(*std::min_element(ys, ys + 4))));
        const int y1 = std::min(target_.height - 1, static_cast<int>(std::ceil(*std::max_element(ys, ys + 4))));

        const std::uint32_t slice = inst.resourceIds[0];
        const std::uint32_t clipIndex = inst.resourceIds[2];
        const std::uint32_t flags = inst.resourceIds[3];
        const bool clipped = clipIndex != R::NoClipIndex && clipIndex < clips.size();
        const R::ClipRectGPU clip = clipped ? clips[clipIndex] : R::ClipRectGPU{};

        const float sizeX = std::sqrt(m00 * m00 + m10 * m10) / dpi_;   // logical px
        const float sizeY = std::sqrt(m01 * m01 + m11 * m11) / dpi_;
        const float radius = inst.transformRow1[3];
        const SU::Vec2 halfSize{sizeX * 0.5f, sizeY * 0.5f};

        static constexpr float offsets[2] = {0.25f, 0.75f}; // 2x2 supersampling = the edge anti-aliasing
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                SU::Vec4 sum{0.0f, 0.0f, 0.0f, 0.0f};
                for (float oy : offsets) {
                    for (float ox : offsets) {
                        const float px = static_cast<float>(x) + ox;
                        const float py = static_cast<float>(y) + oy;
                        const float dx = px - tx, dy = py - ty;
                        const float u = (m11 * dx - m01 * dy) / det;
                        const float v = (-m10 * dx + m00 * dy) / det;
                        if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
                        if (clipped && (px < clip.minX * dpi_ || px >= clip.maxX * dpi_ ||
                                        py < clip.minY * dpi_ || py >= clip.maxY * dpi_)) continue;
                        if ((flags & R::FlagRoundedPanel) != 0u) {
                            const SU::Vec2 local{(u - 0.5f) * sizeX, (v - 0.5f) * sizeY};
                            if (R::EvaluatePanelSdf(local, halfSize, radius) > 0.0f) continue;
                        }
                        const SU::Vec2 uv{inst.uvRect[0] + u * inst.uvRect[2], inst.uvRect[1] + v * inst.uvRect[3]};
                        const SU::Vec4 texel = texture_(slice, uv);
                        SU::Vec4 c{inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};
                        if ((flags & R::FlagMsdfText) != 0u) {
                            c = {c.x * texel.x, c.y * texel.x, c.z * texel.x, c.w * texel.x}; // r channel = coverage
                        } else if ((flags & R::FlagBitmapText) != 0u) {
                            c = {c.x * texel.x, c.y * texel.x, c.z * texel.x, c.w * texel.x};
                        } else {
                            c = {c.x * texel.x, c.y * texel.y, c.z * texel.z, c.w * texel.w};
                        }
                        sum = {sum.x + c.x, sum.y + c.y, sum.z + c.z, sum.w + c.w};
                    }
                }
                if (sum.w <= 0.0f && sum.x <= 0.0f && sum.y <= 0.0f && sum.z <= 0.0f) continue;
                const SU::Vec4 src{sum.x * 0.25f, sum.y * 0.25f, sum.z * 0.25f, sum.w * 0.25f};
                float* dst = target_.Pixel(x, y);
                const float inv = 1.0f - src.w; // premultiplied "over": ONE, INV_SRC_ALPHA
                dst[0] = src.x + dst[0] * inv;
                dst[1] = src.y + dst[1] * inv;
                dst[2] = src.z + dst[2] * inv;
                dst[3] = src.w + dst[3] * inv;
            }
        }
    }

    Image& target_;
    float dpi_;
    TextureFunction texture_;
    std::size_t frames_ = 0;
};

// ---------------------------------------------------------------- scene builder
class Scene {
public:
    explicit Scene(SU::Vec4 clearSrgb = {0.075f, 0.085f, 0.105f, 1.0f}) : clearSrgb_(clearSrgb) {}

    // All coordinates are logical pixels, colours are straight sRGB.
    Scene& Rect(const SU::Rect& rect, SU::Vec4 srgb, std::uint32_t slice = R::WhiteTextureSlice,
                std::uint32_t flags = 0, float cornerRadius = 0.0f, std::uint32_t clipIndex = R::NoClipIndex) {
        R::UIRenderItem item = MakeItem(rect, srgb);
        item.textureSlice = slice;
        item.flags = flags;
        item.cornerRadius = cornerRadius;
        item.clipIndex = clipIndex;
        items_.push_back(item);
        return *this;
    }

    Scene& Outline(const SU::Rect& r, float thickness, SU::Vec4 srgb, std::uint32_t clipIndex = R::NoClipIndex) {
        const float t = thickness;
        Rect({r.position, {r.size.x, t}}, srgb, 0, 0, 0, clipIndex);
        Rect({{r.position.x, r.position.y + r.size.y - t}, {r.size.x, t}}, srgb, 0, 0, 0, clipIndex);
        Rect({{r.position.x, r.position.y + t}, {t, r.size.y - 2 * t}}, srgb, 0, 0, 0, clipIndex);
        Rect({{r.position.x + r.size.x - t, r.position.y + t}, {t, r.size.y - 2 * t}}, srgb, 0, 0, 0, clipIndex);
        return *this;
    }

    Scene& HLine(float x, float y, float length, SU::Vec4 srgb, float thickness = 1.0f) {
        return Rect({{x, y}, {length, thickness}}, srgb);
    }
    Scene& VLine(float x, float y, float length, SU::Vec4 srgb, float thickness = 1.0f) {
        return Rect({{x, y}, {thickness, length}}, srgb);
    }

    // Each glyph quad becomes a textured rectangle from the selected atlas page.
    Scene& Text(const SU::Text::TextLayoutResult& layout, SU::Vec2 origin, SU::Vec4 srgb,
                std::uint32_t textureSlice = R::WhiteTextureSlice,
                std::uint32_t flags = R::FlagMsdfText,
                std::uint32_t clipIndex = R::NoClipIndex) {
        for (const auto& q : layout.quads) {
            if (q.bounds.size.x <= 0.0f || q.bounds.size.y <= 0.0f) continue;
            Rect({{origin.x + q.bounds.position.x, origin.y + q.bounds.position.y}, q.bounds.size},
                 srgb, textureSlice, flags);
            std::copy(std::begin(q.uvRect), std::end(q.uvRect), items_.back().uvRect);
            items_.back().clipIndex = clipIndex;
        }
        return *this;
    }

    // Line boxes (cyan frame), baselines (magenta) of a text layout.
    Scene& TextGuides(const SU::Text::TextLayoutResult& layout, SU::Vec2 origin, SU::Vec4 box, SU::Vec4 baseline) {
        for (const auto& line : layout.lines) {
            Outline({{origin.x + line.x, origin.y + line.y}, {std::max(line.width, 1.0f), line.height}}, 1.0f, box);
            HLine(origin.x + line.x, origin.y + line.baseline, std::max(line.width, 1.0f), baseline);
        }
        return *this;
    }

    Scene& Caret(const SU::Rect& caretRect, SU::Vec2 origin, SU::Vec4 srgb, float width = 1.5f) {
        return Rect({{origin.x + caretRect.position.x - width * 0.5f, origin.y + caretRect.position.y}, {width, caretRect.size.y}}, srgb);
    }

    std::uint32_t AddClip(const R::ClipRectGPU& clip) {
        clips_.push_back(clip);
        return static_cast<std::uint32_t>(clips_.size() - 1);
    }

    // Escape hatch for tests that need explicit paintOrder/layer/material.
    R::UIRenderItem& Raw(const SU::Rect& rect, SU::Vec4 srgb) {
        items_.push_back(MakeItem(rect, srgb));
        return items_.back();
    }

    [[nodiscard]] std::vector<R::UIRenderItem>& Items() noexcept { return items_; }

    [[nodiscard]] R::FramePacket Build(R::ExtractionStats* stats = nullptr) const {
        return R::RenderExtractor{}.Build(1, items_, stats, clips_);
    }

    [[nodiscard]] Image Render(int width, int height, float dpi = 1.0f, TextureFunction texture = WhiteTexture,
                               R::ExtractionStats* stats = nullptr) const {
        Image image(width, height, R::SrgbStraightToLinearPremultiplied(clearSrgb_));
        SoftwareRenderBackend backend(image, dpi, std::move(texture));
        backend.Submit(Build(stats));
        return image;
    }

private:
    R::UIRenderItem MakeItem(const SU::Rect& rect, SU::Vec4 srgb) {
        R::UIRenderItem item;
        item.handle = {++handleCounter_, 1};
        item.layout = {rect.position, rect.size, true, false};
        item.transform.pivot = {0.0f, 0.0f};
        item.tintPremultipliedLinear = R::SrgbStraightToLinearPremultiplied(srgb);
        item.paintOrder = paintOrder_++;
        return item;
    }

    SU::Vec4 clearSrgb_;
    std::vector<R::UIRenderItem> items_;
    std::vector<R::ClipRectGPU> clips_;
    std::uint32_t handleCounter_ = 0;
    std::uint64_t paintOrder_ = 0;
};

} // namespace SpriteUiTests::Visual
