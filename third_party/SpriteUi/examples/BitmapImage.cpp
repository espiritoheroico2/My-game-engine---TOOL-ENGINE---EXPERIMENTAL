#include "BitmapImage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace SpriteUIVisualizer {
namespace {

[[nodiscard]] float SrgbToLinear(float value) noexcept {
    return value <= 0.04045f ? value / 12.92f
                            : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

#if defined(_WIN32)

[[nodiscard]] std::string HResultMessage(const char* operation, HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result) << ").";
    return stream.str();
}

class ComApartment {
public:
    ComApartment() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }

    [[nodiscard]] HRESULT Result() const noexcept { return result_; }

private:
    HRESULT result_;
};

#endif

} // namespace

bool BitmapImage::LoadPng(
    const std::filesystem::path& path, BitmapImage& output, std::string& error) {
#if defined(_WIN32)
    if (path.empty()) {
        error = "Image path is empty.";
        return false;
    }

    ComApartment apartment;
    if (FAILED(apartment.Result())) {
        error = HResultMessage("COM initialization", apartment.Result());
        return false;
    }

    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        error = HResultMessage("WIC factory creation", result);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) {
        error = HResultMessage("PNG decoding", result);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        error = HResultMessage("PNG frame loading", result);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    result = frame->GetSize(&width, &height);
    constexpr UINT MaxDimension = 16384;
    constexpr std::uint64_t MaxPixels = 64ull * 1024ull * 1024ull;
    if (FAILED(result) || width == 0 || height == 0 ||
        width > MaxDimension || height > MaxDimension ||
        static_cast<std::uint64_t>(width) * height > MaxPixels) {
        error = FAILED(result)
            ? HResultMessage("PNG dimensions", result)
            : "PNG dimensions are empty or exceed the visualizer image limits.";
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        error = HResultMessage("WIC pixel converter creation", result);
        return false;
    }
    result = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        error = HResultMessage("WIC RGBA conversion", result);
        return false;
    }

    const UINT stride = width * 4u;
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
    result = converter->CopyPixels(
        nullptr, stride, static_cast<UINT>(rgba.size()), rgba.data());
    if (FAILED(result)) {
        error = HResultMessage("PNG pixel copy", result);
        return false;
    }

    BitmapImage loaded;
    loaded.width_ = static_cast<int>(width);
    loaded.height_ = static_cast<int>(height);
    loaded.sourcePath_ = path;
    loaded.pixels_.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < loaded.pixels_.size(); ++i) {
        const std::size_t offset = i * 4u;
        const float alpha = static_cast<float>(rgba[offset + 3u]) / 255.0f;
        loaded.pixels_[i] = {
            SrgbToLinear(static_cast<float>(rgba[offset]) / 255.0f) * alpha,
            SrgbToLinear(static_cast<float>(rgba[offset + 1u]) / 255.0f) * alpha,
            SrgbToLinear(static_cast<float>(rgba[offset + 2u]) / 255.0f) * alpha,
            alpha
        };
    }
    output = std::move(loaded);
    error.clear();
    return true;
#else
    (void)path;
    (void)output;
    error = "PNG loading in the visualizer currently requires Windows Imaging Component.";
    return false;
#endif
}

ToolEngine::SpriteUi::Vec4 BitmapImage::Sample(
    ToolEngine::SpriteUi::Vec2 uv) const noexcept {
    using ToolEngine::SpriteUi::Vec4;
    if (pixels_.empty() || width_ <= 0 || height_ <= 0 ||
        !ToolEngine::SpriteUi::IsFinite(uv)) {
        return {};
    }

    const float x = std::clamp(uv.x, 0.0f, 1.0f) * static_cast<float>(width_ - 1);
    const float y = std::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(height_ - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [this](int px, int py) -> const Vec4& {
        return pixels_[static_cast<std::size_t>(py) * static_cast<std::size_t>(width_) +
                       static_cast<std::size_t>(px)];
    };
    const Vec4 top = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
    const Vec4 bottom = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
    return top * (1.0f - ty) + bottom * ty;
}

} // namespace SpriteUIVisualizer
