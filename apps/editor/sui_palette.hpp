#pragma once

// Paleta da interface SpriteUI: tokens de cor em um lugar so. Sem dependencia de SpriteUI, para o
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

// Paleta personalizada (Configuracoes > Appearance > Custom). 5 cores-base geram os 8 tokens;
// a lista avancada troca um token sem mexer nos outros. Trocar uma cor-base recalcula todos os tokens.
inline constexpr std::size_t kCustomIndex = kCount;   // indice da paleta Custom (depois das prontas)
inline constexpr std::size_t kTokenCount = 8;
inline constexpr std::size_t kBaseCount = 5;
inline constexpr const char* kBaseNames[kBaseCount] = {"Background", "Surface", "Text", "Accent", "Border"};
inline constexpr const char* kTokenNames[kTokenCount] = {"Panel", "Surface", "Text", "Dim text", "Accent", "Scroll", "Border", "Error"};

struct Custom {
  Rgb base[kBaseCount];     // bg, surface, text, accent, border
  Rgb token[kTokenCount];   // panel, surface, text, textDim, accent, scroll, border, error (ordem de Palette)
};

inline Rgb mixRgb(const Rgb& a, const Rgb& b, float t) {
  return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f};
}

// Recalcula os tokens a partir das cores-base (o erro fica como no preset). Usado ao trocar uma cor-base.
inline void deriveTokens(Custom& c) {
  const Rgb& bg = c.base[0];
  c.token[0] = bg;
  c.token[1] = c.base[1];
  c.token[2] = c.base[2];
  c.token[3] = mixRgb(bg, c.base[2], 0.5f);
  c.token[4] = c.base[3];
  c.token[5] = c.base[3];
  c.token[6] = c.base[4];
  c.token[7] = kPresets[0].error;
}

// Estado da paleta personalizada. Comeca igual ao padrao (Azul escuro).
inline Custom& custom() {
  static Custom c = [] {
    Custom out{};
    const Palette& p = kPresets[0];
    out.base[0] = p.panel; out.base[1] = p.surface; out.base[2] = p.text; out.base[3] = p.accent; out.base[4] = p.border;
    out.token[0] = p.panel; out.token[1] = p.surface; out.token[2] = p.text; out.token[3] = p.textDim;
    out.token[4] = p.accent; out.token[5] = p.scroll; out.token[6] = p.border; out.token[7] = p.error;
    return out;
  }();
  return c;
}

// Indice da paleta em uso. Padrao 0 (Azul escuro); kCustomIndex = personalizada.
inline std::size_t& activeIndex() {
  static std::size_t index = 0;
  return index;
}

// Paleta em uso. Indice invalido cai no padrao.
inline Palette active() {
  const std::size_t i = activeIndex();
  if (i == kCustomIndex) {
    const Custom& c = custom();
    return Palette{"Custom", c.token[0], c.token[1], c.token[2], c.token[3], c.token[4], c.token[5], c.token[6], c.token[7]};
  }
  return kPresets[i < kCount ? i : 0];
}

}  // namespace editor::palette
