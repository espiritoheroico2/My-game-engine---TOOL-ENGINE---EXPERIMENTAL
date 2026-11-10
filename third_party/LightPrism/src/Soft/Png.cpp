#include "LightPrism/Soft/Png.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace LightPrism::Soft {
namespace {

const std::array<uint32_t, 256>& CrcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    const auto& t = CrcTable();
    for (size_t i = 0; i < n; ++i) crc = t[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

void PutBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}

void WriteChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    PutBE32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    PutBE32(out, Crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

} // namespace

bool WritePng(const char* path, const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height) {
    if (!path || width == 0 || height == 0 || rgb.size() != size_t(width) * height * 3) return false;

    // Dados crus: filtro 0 + linha RGB.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (size_t(width) * 3 + 1));
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + size_t(y) * width * 3, rgb.begin() + size_t(y + 1) * width * 3);
    }

    // zlib com blocos "stored" (≤ 65535 bytes cada) + Adler-32.
    std::vector<uint8_t> z{0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n & 0xFF)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n & 0xFF)); z.push_back(uint8_t((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (last) break;
    }
    PutBE32(z, (b << 16) | a);

    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    PutBE32(ihdr, width);
    PutBE32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8 bits, RGB, deflate, filtro padrão, sem entrelaçamento
    WriteChunk(png, "IHDR", ihdr);
    WriteChunk(png, "IDAT", z);
    WriteChunk(png, "IEND", {});

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace LightPrism::Soft
