// ToolEngine - Textura RGBA8 para o albedo dos materiais.
//
// Amostragem "mais proximo" com repeticao (uv fora de 0..1 repete). Origem do UV no canto SUPERIOR esquerdo
// (como no glTF). Os valores sao usados como estao (sem conversao sRGB), igual a cor do MeshRenderer: CPU e GPU
// dao o mesmo resultado. O carregamento de arquivos (PNG, JPG, BMP, TGA) fica no editor (apps/editor/image_io.hpp).
#pragma once
#include "tool/core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace tool {

struct Texture {
  u32 w = 0, h = 0;
  u64 id = 0;                 // identificador unico (chave do cache da GPU); 0 = nao carregada
  std::vector<u8> rgba;       // w * h * 4 bytes, linha a linha de cima para baixo

  bool valid() const { return w > 0 && h > 0 && rgba.size() == static_cast<size_t>(w) * h * 4; }

  // Le o texel de (u, v) em RGBA. Vazio se a textura nao for valida (out fica com branco).
  void texel(f32 u, f32 v, u8 out[4]) const {
    if (!valid()) { out[0] = out[1] = out[2] = out[3] = 255; return; }
    const f32 fu = u - std::floor(u), fv = v - std::floor(v);
    const int x = std::clamp(static_cast<int>(fu * static_cast<f32>(w)), 0, static_cast<int>(w) - 1);
    const int y = std::clamp(static_cast<int>(fv * static_cast<f32>(h)), 0, static_cast<int>(h) - 1);
    const u8* p = &rgba[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4];
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
  }
};

} // namespace tool
