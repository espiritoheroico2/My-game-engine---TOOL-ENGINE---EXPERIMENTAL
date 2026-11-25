#pragma once

// Modelo de tokens de aparencia da interface SpriteUI: uma cor por elemento (painel, texto, divisor, botao...) e a
// forma (raio dos cantos, espaco entre linhas, recuo do painel e escala de interface). O painel de Configuracoes,
// a aplicacao visual e a persistencia ainda nao estao ligados a este modelo.
//
// Regras: cores ficam em sRGB reto (0 a 1), como o seletor mostra; a conversao para o espaco linear da SpriteUI e
// feita no adaptador da SpriteUI. Valores fora da faixa sao presos (Clamp*). Sem SDL e sem JSON; roda no
// ctest como Editor.UiTokens.
//
// Exemplo: Tokens::setColor(Color::Accent, {0.9f, 0.4f, 0.1f, 1.f}) altera o modelo do token de destaque.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace editor::ui_tokens {

// Cor em sRGB reto, 0 a 1 em cada canal.
struct Rgba {
  float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
  [[nodiscard]] bool operator==(const Rgba& other) const noexcept = default;
};

// Um token de cor: o nome guardado no arquivo (key), o texto mostrado (label, chave de traducao) e o padrao.
enum class Color : std::uint8_t {
  Panel,      // fundo das janelas SpriteUI
  Text,       // texto principal
  TextDim,    // texto secundario (desabilitado, legendas)
  Divider,    // linhas e contornos
  Error,      // mensagens de erro
  Heading,    // titulos de secao
  Accent,     // destaque: linha escolhida, trilha preenchida, marca
  Button,     // fundo de botoes e de passos (- e +)
  Field,      // fundo de campos de texto e de numeros
  Track,      // trilha da barra de rolagem
  Thumb,      // polegar da barra de rolagem
  TabActive,  // aba escolhida na barra de abas e cabecalho aberto do menu
  Popup,      // fundo das listas abertas (combos e menus)
  MenuBar,    // faixa da barra de menus
  Count
};

inline constexpr std::size_t kColorCount = static_cast<std::size_t>(Color::Count);

struct ColorInfo {
  const char* key;    // nome no arquivo (estavel, sem traducao)
  const char* label;  // texto mostrado (chave de traducao, em ingles)
  Rgba fallback;      // padrao do editor
};

// Tabela dos tokens, na ordem do enum. O padrao reproduz as cores que cada janela usava antes dos tokens.
[[nodiscard]] inline const ColorInfo& infoOf(Color c) noexcept {
  static const ColorInfo kInfo[kColorCount] = {
      {"panel", "Panel background", {0.055f, 0.075f, 0.11f, 0.97f}},
      {"text", "Text", {0.91f, 0.918f, 0.933f, 1.f}},
      {"textDim", "Secondary text", {0.56f, 0.565f, 0.6f, 1.f}},
      {"divider", "Lines and borders", {0.2f, 0.24f, 0.3f, 1.f}},
      {"error", "Error text", {1.f, 0.45f, 0.4f, 1.f}},
      {"heading", "Section headings", {0.45f, 0.72f, 0.96f, 1.f}},
      {"accent", "Accent (selection, filled track, check)", {0.12f, 0.3f, 0.6f, 1.f}},
      {"button", "Buttons", {0.13f, 0.16f, 0.21f, 1.f}},
      {"field", "Text and number fields", {0.14f, 0.14f, 0.16f, 1.f}},
      {"track", "Scrollbar track", {0.09f, 0.11f, 0.15f, 1.f}},
      {"thumb", "Scrollbar thumb", {0.3f, 0.4f, 0.55f, 1.f}},
      {"tabActive", "Active tab and open menu", {0.16f, 0.2f, 0.28f, 1.f}},
      {"popup", "Open lists (combo and menu)", {0.07f, 0.09f, 0.12f, 1.f}},
      {"menuBar", "Menu bar", {0.055f, 0.075f, 0.11f, 1.f}},
  };
  const std::size_t index = static_cast<std::size_t>(c);
  static constexpr ColorInfo kInvalidInfo{nullptr, nullptr, {}};
  return index < kColorCount ? kInfo[index] : kInvalidInfo;
}

// Limites da forma (a aba Aparencia mostra a mesma faixa nos deslizantes).
inline constexpr float kRoundingMin = 0.f, kRoundingMax = 12.f, kRoundingDefault = 4.f;
inline constexpr float kSpacingMin = 0.f, kSpacingMax = 16.f, kSpacingDefault = 4.f;
inline constexpr float kPaddingMin = 4.f, kPaddingMax = 32.f, kPaddingDefault = 14.f;
inline constexpr float kUiScaleMin = 0.7f, kUiScaleMax = 2.0f, kUiScaleDefault = 1.25f;    // 125 por cento

// Forma da interface: arredondamento dos cantos, espaco entre linhas, recuo das bordas e escala de interface.
struct Shape {
  float rounding = kRoundingDefault;
  float spacing = kSpacingDefault;
  float padding = kPaddingDefault;
  float uiScale = kUiScaleDefault;
  [[nodiscard]] bool operator==(const Shape& other) const noexcept = default;
};

// Prende um valor na faixa dada. NaN volta ao padrao.
[[nodiscard]] inline float ClampOr(float value, float lo, float hi, float fallback) noexcept {
  if (!std::isfinite(value)) return fallback;
  return std::clamp(value, lo, hi);
}

// Forma com todos os valores dentro da faixa (e NaN trocado pelo padrao).
[[nodiscard]] inline Shape Clamped(Shape s) noexcept {
  s.rounding = ClampOr(s.rounding, kRoundingMin, kRoundingMax, kRoundingDefault);
  s.spacing = ClampOr(s.spacing, kSpacingMin, kSpacingMax, kSpacingDefault);
  s.padding = ClampOr(s.padding, kPaddingMin, kPaddingMax, kPaddingDefault);
  s.uiScale = ClampOr(s.uiScale, kUiScaleMin, kUiScaleMax, kUiScaleDefault);
  return s;
}

// Conjunto de tokens em uso: 14 cores e a forma.
class Tokens {
 public:
  Tokens() { resetColors(); }

  [[nodiscard]] const Rgba& color(Color c) const noexcept {
    const std::size_t index = static_cast<std::size_t>(c);
    static constexpr Rgba kInvalidColor{};
    return index < kColorCount ? colors_[index] : kInvalidColor;
  }

  // Grava uma cor (cada canal preso em 0 a 1; NaN vira 0). Devolve true se mudou.
  bool setColor(Color c, Rgba value) noexcept {
    const std::size_t index = static_cast<std::size_t>(c);
    if (index >= kColorCount) return false;
    Rgba clean{ClampOr(value.r, 0.f, 1.f, 0.f), ClampOr(value.g, 0.f, 1.f, 0.f), ClampOr(value.b, 0.f, 1.f, 0.f),
               ClampOr(value.a, 0.f, 1.f, 1.f)};
    Rgba& slot = colors_[index];
    if (slot == clean) return false;
    slot = clean;
    return true;
  }

  // Volta todas as cores ao padrao.
  void resetColors() noexcept {
    for (std::size_t i = 0; i < kColorCount; ++i) colors_[i] = infoOf(static_cast<Color>(i)).fallback;
  }

  [[nodiscard]] const Shape& shape() const noexcept { return shape_; }

  // Grava a forma presa na faixa. Devolve true se mudou.
  bool setShape(Shape next) noexcept {
    next = Clamped(next);
    if (next == shape_) return false;
    shape_ = next;
    return true;
  }

  void resetShape() noexcept { shape_ = Shape{}; }

  // Volta cores e forma ao padrao.
  void resetAll() noexcept {
    resetColors();
    resetShape();
  }

 private:
  std::array<Rgba, kColorCount> colors_{};
  Shape shape_{};
};

// Tokens do editor (um so, lido pelos paineis a cada quadro).
[[nodiscard]] inline Tokens& tokens() noexcept {
  static Tokens instance;
  return instance;
}

}  // namespace editor::ui_tokens
