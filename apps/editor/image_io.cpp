#include "image_io.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

namespace editor {

namespace {
constexpr std::uintmax_t kMaxFileBytes = 64ull * 1024 * 1024;   // arquivo de imagem: no maximo 64 MB
constexpr int kMaxSide = 8192;                                     // lado maximo em pixels
std::atomic<std::uint64_t> gNextId{1};
}

bool loadImageFile(const std::string& path, tool::Texture& out, std::string* err) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(path, ec);
  if (ec || sz == 0 || sz > kMaxFileBytes) { if (err) *err = "image file missing or too large"; return false; }
  std::ifstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "cannot open image"; return false; }
  std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  int w = 0, h = 0, n = 0;
  stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
  if (!px) { if (err) *err = stbi_failure_reason() ? stbi_failure_reason() : "unknown image format"; return false; }
  if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) { stbi_image_free(px); if (err) *err = "image size not supported (max 8192 px per side)"; return false; }
  out.w = static_cast<tool::u32>(w); out.h = static_cast<tool::u32>(h);
  out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
  stbi_image_free(px);
  out.id = gNextId++;
  return out.valid();
}

} // namespace editor
