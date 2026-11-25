#pragma once

// Janela "Export game" em SpriteUI.
// Caixa centralizada com titulo e X, lista de plataformas (as indisponiveis ficam apagadas e nao clicam), a pasta de
// saida, os campos faltantes do Editor info, o botao "Export game" e o resultado. A exportacao em si fica em runExport
// (mesmos passos de antes: para a cena se estiver tocando, salva a cena, grava o projeto e chama xp::exportGame).
#include "editor_state.hpp"
#include "export.hpp"
#include "i18n.hpp"
#include "project_settings.hpp"
#include "sui_panel.hpp"
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {

// Nome do executavel do editor neste sistema (o que o CMake gera).
inline std::string editorExeName() {
#if defined(_WIN32)
  return "ToolEditor.exe";
#else
  return "ToolEditor";
#endif
}

inline fs::path editorFolder() {
  const char* base = SDL_GetBasePath();
  return base ? fs::path(base) : fs::current_path();
}

namespace exportui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

// Resultado de um evento: Platform quando a plataforma mudou (o host grava), Export quando o botao foi clicado.
enum class Action : std::uint8_t { None, Consumed, Platform, Export };

// Resultado de uma exportacao, para a janela mostrar e o log guardar.
struct Outcome {
  bool ok = false;
  std::string text;
};

inline constexpr float kWidth = 540.f;
inline constexpr float kMargin = 24.f;
inline constexpr float kTitleH = 36.f;
inline constexpr float kCloseSize = 26.f;
inline constexpr float kCloseInset = 6.f;
inline constexpr float kPad = 16.f;
inline constexpr float kGap = 8.f;
inline constexpr float kFont = 14.f;
inline constexpr float kTitleFont = 18.f;
inline constexpr float kRowH = 26.f;
inline constexpr float kButtonH = 32.f;
inline constexpr float kRowInset = 10.f;

struct Colors {
  SU::Vec4 row = sui::Srgb(0.20f, 0.22f, 0.27f);
  SU::Vec4 rowSelected = sui::Srgb(0.16f, 0.36f, 0.62f);
  SU::Vec4 button = sui::Srgb(0.12f, 0.42f, 0.72f);
  SU::Vec4 buttonOff = sui::Srgb(0.22f, 0.23f, 0.26f);
  SU::Vec4 warning = sui::Srgb(1.f, 0.8f, 0.3f);
  SU::Vec4 ok = sui::Srgb(0.4f, 0.9f, 0.5f);
};

// Runs the export with the current settings. Stops the play first, as the scene is saved as it is.
inline Outcome runExport(Editor& ed) {
  auto& ex = ed.projSettings.exportPreset;
  if (ed.playing) ed.stopPlay();   // exporta a cena parada, como esta salva
  ed.saveScene();
  ed.projSettings.save(ed.project);
  const fs::path outRoot = editorFolder() / "Export";
  const xp::Result r = xp::exportGame(ed.project, ed.projSettings, ex.platform, editorFolder(), editorExeName(), outRoot);
  Outcome out;
  out.ok = r.ok;
  if (r.ok) out.text = F("Exported %zu files (%.1f MB) to %s", r.files, static_cast<double>(r.bytes) / 1048576.0, r.folder.string().c_str());
  else out.text = std::string(T("Export failed:")) + " " + T(r.err.c_str());
  tool::logInfo(out.text);
  return out;
}

class Panel {
 public:
  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : sdl_{sdl}, canvas_{text} {}
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // Resultado da ultima exportacao (o host chama depois de runExport).
  void setResult(const Outcome& out) {
    result_ = out.text;
    resultOk_ = out.ok;
  }

  // Eventos do SDL. `open` = janela aberta; `platform` = indice escolhido (muda ao clicar).
  Action handleEvent(const SDL_Event& e, bool& open, int& platform) {
    if (!open) return Action::None;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !contains(e.button.x, e.button.y)) return Action::None;
        return press(e.button.x, e.button.y, open, platform);
      case SDL_EVENT_MOUSE_WHEEL:
        return contains(e.wheel.mouse_x, e.wheel.mouse_y) ? Action::Consumed : Action::None;
      default:
        return Action::None;
    }
  }

  // Desenha a janela quando `open`. `missing` = campos obrigatorios que faltam. Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool open, int platform, const std::vector<std::string>& missing,
            const std::string& language) {
    if (!open) return true;
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada
    const float w = std::min(kWidth, static_cast<float>(ww) - 2.f * kMargin);
    std::string key = std::to_string(platform) + "|" + language + "|" + std::to_string(w) + "|" + result_ + (resultOk_ ? "1" : "0");
    for (const std::string& m : missing) key += "|" + m;
    if (!built_ || key != builtKey_) rebuild(platform, missing, language, w, key);
    panel_ = {(static_cast<float>(ww) - w) * 0.5f, (static_cast<float>(wh) - contentH_) * 0.5f, w, contentH_};
    return drawPacket(renderer);
  }

 private:
  struct Row {
    float y = 0.f, h = kRowH;
    bool enabled = false;
  };

  [[nodiscard]] bool contains(float x, float y) const {
    return x >= panel_.x && y >= panel_.y && x < panel_.x + panel_.w && y < panel_.y + panel_.h;
  }

  [[nodiscard]] static bool inside(float x, float y, float left, float top, float w, float h) {
    return x >= left && y >= top && x < left + w && y < top + h;
  }

  Action press(float x, float y, bool& open, int& platform) {
    const float lx = x - panel_.x, ly = y - panel_.y;
    if (inside(lx, ly, panel_.w - kCloseSize - kCloseInset, (kTitleH - kCloseSize) * 0.5f, kCloseSize, kCloseSize)) {
      open = false;
      return Action::Consumed;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      const Row& r = rows_[i];
      if (!r.enabled || !inside(lx, ly, kPad, r.y, width_, r.h)) continue;
      const int chosen = static_cast<int>(i);
      if (platform == chosen) return Action::Consumed;
      platform = chosen;
      return Action::Platform;
    }
    if (buttonEnabled_ && inside(lx, ly, kPad, button_.y, width_, kButtonH)) return Action::Export;
    return Action::Consumed;
  }

  // Monta o texto e a posicao dos controles. Roda so quando algo que aparece muda (plataforma, idioma, faltantes, resultado).
  void rebuild(int platform, const std::vector<std::string>& missing, const std::string& language, float panelW, const std::string& key) {
    canvas_.clear();
    built_ = true;
    builtKey_ = key;
    width_ = panelW - 2.f * kPad;
    const float width = width_;
    const float titleLh = canvas_.lineHeight(kTitleFont);
    canvas_.addText(kPad, (kTitleH - titleLh) * 0.5f, T("Export game"), kTitleFont, theme_.text, width - kCloseSize);
    canvas_.addSolid(0.f, kTitleH - 1.f, panelW, 1.f, theme_.divider, 0.2f);
    const ST::TextLayoutResult x = canvas_.layout("X", kTitleFont);
    canvas_.addText(panelW - kCloseSize - kCloseInset + (kCloseSize - x.size.x) * 0.5f, (kTitleH - x.lineHeight) * 0.5f, "X", kTitleFont, theme_.text, kCloseSize);

    float y = kTitleH + kPad;
    const float lh = canvas_.lineHeight(kFont);
    canvas_.addText(kPad, y, T("Target platform"), kFont, theme_.textDim, width);
    y += lh + kGap;

    bool anyUnavailable = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      const bool ok = platformAvailable(static_cast<int>(i));
      anyUnavailable |= !ok;
      rows_[i] = {y, kRowH, ok};
      const bool selected = platform == static_cast<int>(i);
      if (selected) canvas_.addSolid(kPad, y, width, kRowH, colors_.rowSelected, 0.3f);
      const SU::Vec4 color = !ok ? theme_.textDim : theme_.text;
      canvas_.addText(kPad + kRowInset, y + (kRowH - lh) * 0.5f, kExportPlatforms[i], kFont, color, width - 2.f * kRowInset);
      y += kRowH + 2.f;
    }
    if (anyUnavailable) {
      y += kGap;
      y += canvas_.addParagraph(kPad, y, T("Not available in this build yet. Android, Web and UWP need their own toolchains."), kFont - 1.f, theme_.textDim, width);
    }

    y += kPad;
    canvas_.addSolid(kPad, y, width, 1.f, theme_.divider, 0.2f);
    y += kPad;
    canvas_.addText(kPad, y, T("Output folder:"), kFont, theme_.textDim, width);
    y += lh + 2.f;
    const fs::path outRoot = editorFolder() / "Export";
    y += canvas_.addParagraph(kPad, y, outRoot.string(), kFont, theme_.text, width) + kGap;

    if (!missing.empty()) {
      std::string list;
      for (const std::string& m : missing) {
        if (!list.empty()) list += ", ";
        list += T(m.c_str());
      }
      y += kGap;
      y += canvas_.addParagraph(kPad, y, std::string(T("Fill in the Editor info first (File > Editor info). Missing:")) + " " + list, kFont, colors_.warning, width) + kGap;
    }

    buttonEnabled_ = missing.empty() && platformAvailable(platform);
    y += kGap;
    button_ = {kPad, y};
    canvas_.addSolid(kPad, y, width, kButtonH, buttonEnabled_ ? colors_.button : colors_.buttonOff, 0.3f);
    const std::string label = T("Export game");
    const ST::TextLayoutResult labelLayout = canvas_.layout(label, kFont);
    canvas_.addText(kPad + (width - labelLayout.size.x) * 0.5f, y + (kButtonH - labelLayout.lineHeight) * 0.5f, label, kFont,
                    buttonEnabled_ ? theme_.text : theme_.textDim, width);
    y += kButtonH + kGap;

    if (!result_.empty()) {
      y += kGap;
      y += canvas_.addParagraph(kPad, y, result_, kFont, resultOk_ ? colors_.ok : theme_.error, width) + kGap;
    }

    y += kPad;
    canvas_.addSolid(kPad, y, width, 1.f, theme_.divider, 0.2f);
    y += kPad;
    y += canvas_.addParagraph(kPad, y, T("Open the output folder and double-click Play.bat (Windows) or run play.sh (Linux). The Data/Project folder holds the game files in plain text, so you can open them in other tools."), kFont - 1.f, theme_.textDim, width);
    contentH_ = y + kPad;
    (void)language;   // o texto muda com o idioma: a chave do cache ja inclui o idioma
  }

  bool drawPacket(SDL_Renderer* renderer) {
    std::vector<SR::UIRenderItem> items;
    const std::vector<SR::ClipRectGPU> clips{{panel_.x, panel_.y, panel_.x + panel_.w, panel_.y + panel_.h}};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    items.push_back(sui::Canvas::solidItem(panel_.x, panel_.y, panel_.w, panel_.h, theme_.panel, 0.f, owner, 0u, paint++));
    canvas_.appendItems(items, panel_.x, panel_.y, 0.f, panel_.y, panel_.y + panel_.h, owner, 0u, paint);
    packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

  sui::Theme theme_;
  Colors colors_;
  sui::SpriteUiSdlRenderer& sdl_;
  sui::Canvas canvas_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  std::array<Row, kExportPlatforms.size()> rows_{};
  SU::Vec2 button_{};
  bool buttonEnabled_ = false;
  float width_ = 0.f;
  sui::Box panel_;
  float contentH_ = 0.f;
  bool built_ = false;
  std::string builtKey_;
  std::string result_;
  bool resultOk_ = false;
};

}  // namespace exportui
}  // namespace editor
