#pragma once
// Gravador de PNG sem dependências (RGB 8 bits, deflate sem compressão). Suficiente para inspecionar
// resultados dos testes e da ferramenta de software rendering.
#include <cstdint>
#include <vector>

namespace LightPrism::Soft {

// `rgb` tem width*height*3 bytes, linhas de cima para baixo. Retorna false se o tamanho não bate ou a escrita falha.
bool WritePng(const char* path, const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height);

} // namespace LightPrism::Soft
