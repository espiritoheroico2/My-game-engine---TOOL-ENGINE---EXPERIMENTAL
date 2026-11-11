#include "BitmapFont.hpp"

#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Text/LineBreak.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace SpriteUIVisualizer {

namespace {

constexpr int RasterFontPixels = 48;
constexpr int AtlasColumns = 16;
constexpr int CellSize = 64;
constexpr int GlyphPadding = 2;
constexpr int GlyphCount = 256;

#if defined(_WIN32)

[[nodiscard]] std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) return 0;
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8u) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

[[nodiscard]] std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return 0;
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

[[nodiscard]] bool ReadFamilyName(const std::filesystem::path& path, std::wstring& family) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || bytes.size() > 32u * 1024u * 1024u) return false;

    const std::uint16_t tableCount = ReadU16(bytes, 4);
    std::size_t nameOffset = 0;
    std::size_t nameLength = 0;
    for (std::size_t i = 0; i < tableCount; ++i) {
        const std::size_t record = 12 + i * 16;
        if (record > bytes.size() || bytes.size() - record < 16) return false;
        if (bytes[record] == 'n' && bytes[record + 1] == 'a' &&
            bytes[record + 2] == 'm' && bytes[record + 3] == 'e') {
            nameOffset = ReadU32(bytes, record + 8);
            nameLength = ReadU32(bytes, record + 12);
            break;
        }
    }
    if (nameLength < 6 || nameOffset > bytes.size() || nameLength > bytes.size() - nameOffset) {
        return false;
    }

    const std::size_t nameEnd = nameOffset + nameLength;
    const std::uint16_t recordCount = ReadU16(bytes, nameOffset + 2);
    const std::size_t stringOffset = nameOffset + ReadU16(bytes, nameOffset + 4);
    if (stringOffset > nameEnd) return false;

    std::wstring preferred;
    std::wstring fallback;
    for (std::size_t i = 0; i < recordCount; ++i) {
        const std::size_t record = nameOffset + 6 + i * 12;
        if (record > nameEnd || nameEnd - record < 12) return false;
        const std::uint16_t platform = ReadU16(bytes, record);
        const std::uint16_t nameId = ReadU16(bytes, record + 6);
        const std::uint16_t length = ReadU16(bytes, record + 8);
        const std::uint16_t offset = ReadU16(bytes, record + 10);
        if ((nameId != 1 && nameId != 16) || (platform != 0 && platform != 3) ||
            static_cast<std::size_t>(offset) > nameEnd - stringOffset ||
            static_cast<std::size_t>(length) > nameEnd - stringOffset - offset) {
            continue;
        }
        std::wstring value;
        const std::size_t start = stringOffset + offset;
        for (std::size_t j = 0; j + 1 < length; j += 2) {
            const wchar_t c = static_cast<wchar_t>(ReadU16(bytes, start + j));
            if (c == L'\0') break;
            value.push_back(c);
        }
        if (value.empty()) continue;
        if (nameId == 16 && preferred.empty()) preferred = value;
        if (nameId == 1 && fallback.empty()) fallback = value;
    }
    family = preferred.empty() ? fallback : preferred;
    return !family.empty();
}

[[nodiscard]] bool LoadWindowsFont(
    const std::filesystem::path& path,
    std::string& error,
    ToolEngine::SpriteUi::Text::FontMetrics& metrics,
    std::vector<float>& coverage,
    std::vector<ToolEngine::SpriteUi::Text::GlyphInstance>& glyphs,
    int& atlasWidth,
    int& atlasHeight,
    std::wstring& familyName) {
    std::wstring family;
    if (!ReadFamilyName(path, family)) {
        error = "Could not read the TTF/OTF family name from the font file.";
        return false;
    }

    const int loadedFonts = AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    if (loadedFonts == 0) {
        error = "Windows could not load the font file.";
        return false;
    }

    bool success = false;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT handle = nullptr;
    HGDIOBJ previous = nullptr;
    if (!dc) {
        error = "Could not create a font rasterization context.";
    } else {
        handle = CreateFontW(-RasterFontPixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str());
        if (!handle) {
            error = "Could not create a font at the requested rasterization size.";
        } else {
            previous = SelectObject(dc, handle);
            std::array<wchar_t, LF_FACESIZE> selectedFamily{};
            if (!GetTextFaceW(dc, static_cast<int>(selectedFamily.size()), selectedFamily.data()) ||
                _wcsicmp(family.c_str(), selectedFamily.data()) != 0) {
                error = "The requested font family was not selected by the Windows rasterizer.";
            } else {
                TEXTMETRICW textMetrics{};
                MAT2 transform{};
                transform.eM11.value = 1;
                transform.eM22.value = 1;
                if (!GetTextMetricsW(dc, &textMetrics)) {
                    error = "Could not read font metrics.";
                } else {
                    atlasWidth = AtlasColumns * CellSize;
                    atlasHeight = ((GlyphCount + AtlasColumns - 1) / AtlasColumns) * CellSize;
                    coverage.assign(static_cast<std::size_t>(atlasWidth) *
                                    static_cast<std::size_t>(atlasHeight), 0.0f);
                    glyphs.resize(GlyphCount);
                    metrics.fontSize = static_cast<float>(RasterFontPixels);
                    metrics.unitsPerEm = static_cast<float>(RasterFontPixels);
                    metrics.ascent = static_cast<float>(textMetrics.tmAscent);
                    metrics.descent = -static_cast<float>(textMetrics.tmDescent);
                    metrics.lineGap = static_cast<float>(std::max<LONG>(textMetrics.tmExternalLeading, 0));
                    metrics.pxRange = 1.0f;
                    metrics.atlasWidth = static_cast<float>(atlasWidth);
                    metrics.atlasHeight = static_cast<float>(atlasHeight);
                    success = metrics.IsValid();

                    for (int codepoint = 0; success && codepoint < GlyphCount; ++codepoint) {
                        auto& glyph = glyphs[static_cast<std::size_t>(codepoint)];
                        glyph.glyphId = static_cast<std::uint32_t>(codepoint);
                        glyph.xAdvance = static_cast<float>(textMetrics.tmAveCharWidth);
                        if (codepoint < 32 || (codepoint >= 127 && codepoint < 160)) continue;

                        GLYPHMETRICS glyphMetrics{};
                        const DWORD bitmapBytes = GetGlyphOutlineW(
                            dc, static_cast<UINT>(codepoint), GGO_GRAY8_BITMAP, &glyphMetrics,
                            0, nullptr, &transform);
                        if (bitmapBytes == GDI_ERROR) {
                            success = false;
                            error = "The Windows rasterizer could not load a glyph from the font.";
                            break;
                        }
                        glyph.xAdvance = static_cast<float>(glyphMetrics.gmCellIncX);
                        const int cellX = (codepoint % AtlasColumns) * CellSize;
                        const int cellY = (codepoint / AtlasColumns) * CellSize;
                        const int width = static_cast<int>(glyphMetrics.gmBlackBoxX);
                        const int height = static_cast<int>(glyphMetrics.gmBlackBoxY);
                        if (bitmapBytes == 0 || width <= 0 || height <= 0) continue;

                        if (width + GlyphPadding * 2 > CellSize ||
                            height + GlyphPadding * 2 > CellSize) {
                            success = false;
                            error = "A glyph exceeds the bitmap atlas cell; use a different font.";
                            break;
                        }
                        std::vector<std::uint8_t> bitmap(bitmapBytes);
                        const DWORD readBytes = GetGlyphOutlineW(
                            dc, static_cast<UINT>(codepoint), GGO_GRAY8_BITMAP, &glyphMetrics,
                            bitmapBytes, bitmap.data(), &transform);
                        if (readBytes == GDI_ERROR || readBytes != bitmapBytes) {
                            success = false;
                            error = "Could not rasterize a glyph from the font.";
                            break;
                        }
                        const int pitch = (width + 3) & ~3;
                        if (static_cast<std::size_t>(bitmapBytes) <
                            static_cast<std::size_t>(pitch) * static_cast<std::size_t>(height)) {
                            success = false;
                            error = "The font rasterizer returned an incomplete bitmap for glyph " +
                                    std::to_string(codepoint) + " (bytes=" +
                                    std::to_string(bitmapBytes) + ", expected at least " +
                                    std::to_string(pitch * height) + ").";
                            break;
                        }
                        const int atlasX = cellX + GlyphPadding;
                        const int atlasY = cellY + GlyphPadding;
                        for (int y = 0; y < height; ++y) {
                            for (int x = 0; x < width; ++x) {
                                const std::size_t source =
                                    static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch) +
                                    static_cast<std::size_t>(x);
                                const std::size_t destination =
                                    static_cast<std::size_t>(atlasY + y) *
                                        static_cast<std::size_t>(atlasWidth) +
                                    static_cast<std::size_t>(atlasX + x);
                                coverage[destination] =
                                    static_cast<float>(bitmap[source]) / 64.0f;
                            }
                        }
                        glyph.planeBounds[0] = static_cast<float>(glyphMetrics.gmptGlyphOrigin.x);
                        glyph.planeBounds[1] =
                            static_cast<float>(glyphMetrics.gmptGlyphOrigin.y) -
                            static_cast<float>(height);
                        glyph.planeBounds[2] =
                            glyph.planeBounds[0] + static_cast<float>(width);
                        glyph.planeBounds[3] =
                            static_cast<float>(glyphMetrics.gmptGlyphOrigin.y);
                        glyph.uvRect[0] = static_cast<float>(atlasX) / static_cast<float>(atlasWidth);
                        glyph.uvRect[1] = static_cast<float>(atlasY) / static_cast<float>(atlasHeight);
                        glyph.uvRect[2] = static_cast<float>(width) / static_cast<float>(atlasWidth);
                        glyph.uvRect[3] = static_cast<float>(height) / static_cast<float>(atlasHeight);
                    }
                }
            }
        }
    }

    if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
    if (handle) DeleteObject(handle);
    if (dc) DeleteDC(dc);
    RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    if (!success && error.empty()) error = "Font metrics are invalid.";
    familyName = std::move(family);
    return success;
}

#endif

[[nodiscard]] std::uint32_t DecodeCodepoint(std::string_view text, std::size_t& offset) {
    const auto first = static_cast<std::uint8_t>(text[offset]);
    if (first < 0x80u) {
        ++offset;
        return first;
    }
    std::uint32_t value = 0xFFFDu;
    std::size_t count = 0;
    if ((first & 0xE0u) == 0xC0u) { value = first & 0x1Fu; count = 1; }
    else if ((first & 0xF0u) == 0xE0u) { value = first & 0x0Fu; count = 2; }
    else if ((first & 0xF8u) == 0xF0u) { value = first & 0x07u; count = 3; }
    ++offset;
    for (std::size_t i = 0; i < count && offset < text.size(); ++i, ++offset) {
        const auto next = static_cast<std::uint8_t>(text[offset]);
        if ((next & 0xC0u) != 0x80u) return 0xFFFDu;
        value = (value << 6u) | (next & 0x3Fu);
    }
    return value;
}

} // namespace

bool BitmapFont::Load(
    const std::filesystem::path& path,
    BitmapFont& font,
    std::string& error) {
    error.clear();
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(path, fileError)) {
        error = fileError
            ? "Could not inspect font file '" + path.string() + "': " + fileError.message()
            : "Font file does not exist: " + path.string();
        return false;
    }

#if defined(_WIN32)
    BitmapFont loaded;
    std::wstring family;
    if (!LoadWindowsFont(path, error, loaded.metrics_, loaded.coverage_,
                         loaded.glyphs_, loaded.atlasWidth_, loaded.atlasHeight_, family)) {
        return false;
    }
    const int requiredGlyphs = GlyphCount;
    if (loaded.glyphs_.size() != static_cast<std::size_t>(requiredGlyphs)) {
        error = "Font atlas generation did not produce the expected glyph set.";
        return false;
    }
    const int familyBytes = WideCharToMultiByte(
        CP_UTF8, 0, family.data(), static_cast<int>(family.size()), nullptr, 0, nullptr, nullptr);
    if (familyBytes <= 0) {
        error = "Could not convert the font family name to UTF-8.";
        return false;
    }
    loaded.familyName_.resize(static_cast<std::size_t>(familyBytes));
    if (WideCharToMultiByte(CP_UTF8, 0, family.data(), static_cast<int>(family.size()),
                            loaded.familyName_.data(), familyBytes, nullptr, nullptr) != familyBytes) {
        error = "Could not convert the font family name to UTF-8.";
        return false;
    }
    loaded.sourcePath_ = std::filesystem::absolute(path);
    font = std::move(loaded);
    return true;
#else
    (void)font;
    error = "Loading TTF/OTF files in the visualizer currently requires Windows GDI.";
    return false;
#endif
}

std::vector<ToolEngine::SpriteUi::Text::GlyphInstance> BitmapFont::Shape(
    std::string_view utf8) const {
    namespace Text = ToolEngine::SpriteUi::Text;
    const std::vector<Text::TextCluster> clusters = Text::AnalyzeClusters(utf8);
    std::vector<Text::GlyphInstance> shaped;
    shaped.reserve(clusters.size());
    for (const Text::TextCluster& cluster : clusters) {
        std::size_t offset = cluster.byteBegin;
        std::uint32_t codepoint = DecodeCodepoint(utf8, offset);
        if (cluster.kind == Text::ClusterKind::Newline) codepoint = '\n';
        const std::uint32_t index = codepoint < static_cast<std::uint32_t>(glyphs_.size())
            ? codepoint
            : static_cast<std::uint32_t>('?');
        Text::GlyphInstance glyph = glyphs_[index];
        glyph.clusterByteOffset = static_cast<std::uint32_t>(cluster.byteBegin);
        if (codepoint == '\n') {
            glyph.xAdvance = 0.0f;
            glyph.uvRect[2] = 0.0f;
            glyph.uvRect[3] = 0.0f;
            glyph.planeBounds[2] = glyph.planeBounds[0];
            glyph.planeBounds[3] = glyph.planeBounds[1];
        } else if (codepoint == '\t') {
            glyph.xAdvance *= 4.0f;
            glyph.uvRect[2] = 0.0f;
            glyph.uvRect[3] = 0.0f;
            glyph.planeBounds[2] = glyph.planeBounds[0];
            glyph.planeBounds[3] = glyph.planeBounds[1];
        }
        shaped.push_back(glyph);
    }
    return shaped;
}

ToolEngine::SpriteUi::Vec4 BitmapFont::Sample(
    std::uint32_t slice,
    ToolEngine::SpriteUi::Vec2 uv) const noexcept {
    if (slice == ToolEngine::SpriteUi::Rendering::WhiteTextureSlice) return {1, 1, 1, 1};
    if (slice != TextureSlice || atlasWidth_ <= 0 || atlasHeight_ <= 0 || coverage_.empty()) return {};
    const float x = std::clamp(uv.x * static_cast<float>(atlasWidth_) - 0.5f,
                               0.0f, static_cast<float>(atlasWidth_ - 1));
    const float y = std::clamp(uv.y * static_cast<float>(atlasHeight_) - 0.5f,
                               0.0f, static_cast<float>(atlasHeight_ - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, atlasWidth_ - 1);
    const int y1 = std::min(y0 + 1, atlasHeight_ - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [this](int px, int py) {
        return coverage_[static_cast<std::size_t>(py) *
                         static_cast<std::size_t>(atlasWidth_) +
                         static_cast<std::size_t>(px)];
    };
    const float top = std::lerp(at(x0, y0), at(x1, y0), tx);
    const float bottom = std::lerp(at(x0, y1), at(x1, y1), tx);
    const float alpha = std::lerp(top, bottom, ty);
    return {alpha, alpha, alpha, alpha};
}

} // namespace SpriteUIVisualizer
