#pragma once

// Paleta da interface SpriteUI: tokens de cor em um lugar so. Sem dependencia de SpriteUI nem de ImGui, para o
// teste (tests/test_sui_palette.cpp) rodar sem janela. sui_panel.hpp converte para o espaco de cor da SpriteUI.
//
// Padrao: "Azul escuro" (tom das referencias em docs/imagens). A troca pelo usuario vai na aba
// Configuracoes > Appearance (ligada: as molduras e os paineis SpriteUI leem palette::active() a cada quadro).
#include <array>
#include <cstddef>

namespace editor::palette {

struct Rgb {
  float r = 0.f, g = 0.f, b = 0.f, a = 1.f;   // sRGB, sem pre-multiplicar
};

struct Palette {
  const char* name;   // chave de traducao (T(name))
  Rgb panel;          // fundo do corpo da janela
  Rgb surface;        // campos, cartoes e trilhos
  Rgb text;           // texto principal
  Rgb textDim;        // rotulos e dicas
  Rgb accent;         // botoes marcados e destaque
  Rgb scroll;         // polegar da barra de rolagem
  Rgb border;         // contorno e separadores
  Rgb error;          // erros e avisos
};

inline constexpr std::size_t kCount = 4;

// Presets. Ordem fixa: o indice e o que a aba Appearance grava.
inline constexpr std::array<Palette, kCount> kPresets{{
    // Azul escuro (padrao): tom das referencias do planejamento.
    {"Dark blue",
     {0.040f, 0.060f, 0.200f}, {0.080f, 0.120f, 0.340f}, {0.930f, 0.940f, 0.970f}, {0.600f, 0.660f, 0.800f},
     {0.000f, 0.470f, 0.780f}, {0.000f, 0.900f, 0.800f}, {0.160f, 0.220f, 0.480f}, {1.000f, 0.450f, 0.400f}},
    // Grafite: o tema escuro neutro que ja existia nas janelas SpriteUI.
    {"Graphite",
     {0.055f, 0.075f, 0.110f}, {0.100f, 0.120f, 0.160f}, {0.910f, 0.918f, 0.933f}, {0.560f, 0.565f, 0.600f},
     {0.260f, 0.550f, 0.960f}, {0.260f, 0.550f, 0.960f}, {0.200f, 0.240f, 0.300f}, {1.000f, 0.450f, 0.400f}},
    // Claro: para quem prefere fundo claro.
    {"Light",
     {0.940f, 0.940f, 0.950f}, {0.990f, 0.990f, 1.000f}, {0.100f, 0.110f, 0.130f}, {0.380f, 0.400f, 0.450f},
     {0.160f, 0.450f, 0.900f}, {0.160f, 0.450f, 0.900f}, {0.760f, 0.770f, 0.800f}, {0.800f, 0.150f, 0.150f}},
    // Noturno: quase preto e azulado, texto suave e destaque frio, para uso no escuro. Fica no fim (indice 3).
    {"Night",
     {0.020f, 0.022f, 0.030f}, {0.050f, 0.055f, 0.072f}, {0.890f, 0.900f, 0.920f}, {0.560f, 0.580f, 0.640f},
     {0.350f, 0.560f, 1.000f}, {0.350f, 0.560f, 1.000f}, {0.120f, 0.130f, 0.165f}, {1.000f, 0.450f, 0.400f}},
}};

// Indice da paleta em uso. Padrao 0 (Azul escuro).
inline std::size_t& activeIndex() {
  static std::size_t index = 0;
  return index;
}

// Paleta em uso. Indice invalido cai no padrao.
inline const Palette& active() {
  const std::size_t i = activeIndex();
  return kPresets[i < kCount ? i : 0];
}

}  // namespace editor::palette
