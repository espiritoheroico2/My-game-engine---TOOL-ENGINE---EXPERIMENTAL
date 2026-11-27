#pragma once

// Janela "Export game" em SpriteUI.
// Moldura padrao do editor (framed::Chrome: titulo, "?", X, grip e dock) com o corpo rolavel por dentro: lista de plataformas (as indisponiveis ficam apagadas e nao clicam), a pasta de
// saida, os campos faltantes do Editor info, o botao "Export game" e o resultado. A exportacao em si fica em runExport
// (mesmos passos de antes: para a cena se estiver tocando, salva a cena, grava o projeto e chama xp::exportGame).
#include "editor_state.hpp"
#include "framed_window.hpp"
#include "export.hpp"
#include "i18n.hpp"
#include "project_settings.hpp"
#include "sui_panel.hpp"
#include "theme_colors.hpp"
#include "ui_scrollbar.hpp"
#include "usability_hints.hpp"   // motivo de o botao Export estar bloqueado
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

// Pasta do executavel do editor (base do SDL); se nao houver, a pasta atual. De la saem o motor e a pasta Export/.
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
inline constexpr float kStartContentH = 460.f;   // altura do corpo antes da primeira montagem
inline constexpr float kScrollStep = 40.f;
inline constexpr float kPad = 16.f;
inline constexpr float kGap = 8.f;
inline constexpr float kFont = 14.f;
inline constexpr float kTitleFont = 18.f;
inline constexpr float kRowH = 26.f;
inline constexpr float kButtonH = 32.f;
inline constexpr float kRowInset = 10.f;

// Da paleta em uso (theme_colors.hpp), refeito em draw(). warning e ok (amarelo e verde de estado) ficam fixos.
struct Colors {
  SU::Vec4 row, rowSelected, button, buttonOff;
  SU::Vec4 warning = sui::Srgb(1.f, 0.8f, 0.3f);
  SU::Vec4 ok = sui::Srgb(0.4f, 0.9f, 0.5f);
  Colors() {
    const editor::theme_colors::Colors t = editor::theme_colors::Active();
    row = sui::PaletteColor(t.surface);
    rowSelected = sui::PaletteColor(t.rowSel);
    button = sui::PaletteColor(t.btnOn);
    buttonOff = sui::PaletteColor(t.btn);
  }
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

// Janela "Export game": escolhe a plataforma, mostra campos faltantes e o botao de exportar. Desenha com SpriteUI e so reconstroi quando algo visivel muda.
class Panel {
 public:
  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : sdl_{sdl}, canvas_{text}, chrome_{text, sdl} {}
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // Moldura padrao (titulo, "?", X, grip, dock). O host registra no dock e le consumeClose()/consumeHelp().
  framed::Chrome& chrome() { return chrome_; }

  // Resultado da ultima exportacao (o host chama depois de runExport).
  void setResult(const Outcome& out) {
    result_ = out.text;
    resultOk_ = out.ok;
  }

  // Eventos do SDL. `open` = janela aberta; `platform` = indice escolhido (muda ao clicar).
  Action handleEvent(const SDL_Event& e, bool& open, int& platform) {
    if (!open || !chrome_.visible()) return Action::None;
    if (chrome_.handleEvent(e)) return Action::Consumed;   // X, "?" e minimizar da moldura
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !body_.contains(e.button.x, e.button.y)) return Action::None;
        if (bar_.pointerDown(e.button.x, e.button.y, scroll_)) {   // barra de rolagem unica: arrasta ou pagina
          scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, body_.h);
          return Action::Consumed;
        }
        return press(e.button.x, e.button.y, platform);
      case SDL_EVENT_MOUSE_MOTION:
        if (bar_.pointerMove(e.motion.x, e.motion.y, scroll_)) scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, body_.h);
        return bar_.dragging() ? Action::Consumed : Action::None;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        return e.button.button == SDL_BUTTON_LEFT && bar_.pointerUp() ? Action::Consumed : Action::None;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!body_.contains(e.wheel.mouse_x, e.wheel.mouse_y)) return Action::None;
        scroll_ = ui_scrollbar::clampScroll(scroll_ - e.wheel.y * kScrollStep, contentH_, body_.h);
        return Action::Consumed;
      default:
        return Action::None;
    }
  }

  // Desenha a moldura e o corpo quando `open`. `missing` = campos obrigatorios que faltam. Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, bool open, int platform, const std::vector<std::string>& missing,
            const std::string& language) {
    if (!open) {
      chrome_.hide();
      return true;
    }
    if (!renderer || !window) return false;
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada
    const sui::Box body = chrome_.prepare(static_cast<float>(ww), static_cast<float>(wh), kWidth, framed::frameHeightFor(contentH_ > 0.f ? contentH_ : kStartContentH));
    if (!chrome_.draw(renderer, T("Export game"))) return false;
    if (body.w <= 0.f || body.h <= 0.f) {   // minimizada: so o cabecalho
      body_ = {};
      return true;
    }
    body_ = body;
    theme_ = sui::Theme{};   // paleta em uso (Configuracoes > Appearance)
    colors_ = Colors{};
    const float w = body.w;
    std::string key = std::to_string(editor::theme_colors::Signature()) + "#" + std::to_string(platform) + "|" + language + "|" + std::to_string(w) + "|" + result_ + (resultOk_ ? "1" : "0");
    for (const std::string& m : missing) key += "|" + m;
    if (!built_ || key != builtKey_) rebuild(platform, missing, language, w, key);
    scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, body_.h);
    return drawPacket(renderer);
  }

 private:
  // Linha de plataforma: posicao Y, altura e se esta disponivel (clicavel).
  struct Row {
    float y = 0.f, h = kRowH;
    bool enabled = false;
  };

  [[nodiscard]] static bool inside(float x, float y, float left, float top, float w, float h) {
    return x >= left && y >= top && x < left + w && y < top + h;
  }

  // Clique esquerdo no corpo: escolhe plataforma ou pede exportar. Clique em area vazia e consumido.
  Action press(float x, float y, int& platform) {
    const float lx = x - body_.x, ly = y - body_.y + scroll_;
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

  // Monta o texto e a posicao dos controles (coordenadas do corpo, sem rolagem). Roda so quando algo que aparece muda (plataforma, idioma, faltantes, resultado).
  void rebuild(int platform, const std::vector<std::string>& missing, const std::string& language, float panelW, const std::string& key) {
    canvas_.clear();
    built_ = true;
    builtKey_ = key;
    width_ = panelW - 2.f * kPad;
    const float width = width_;
    float y = kPad;
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

    // Botao apagado sem aviso de campos faltando = a plataforma escolhida nao e suportada: diz isso junto ao botao.
    if (missing.empty()) {
      if (const char* why = editor::usability::ExportBlockedReason(false, platformAvailable(platform))) {
        y += canvas_.addParagraph(kPad, y, T(why), kFont - 1.f, colors_.warning, width) + kGap;
      }
    }

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

  // Monta os itens de desenho (canvas em cache e barra de rolagem) e envia ao renderizador SpriteUI. Retorna false so se o SDL falhar.
  bool drawPacket(SDL_Renderer* renderer) {
    std::vector<SR::UIRenderItem> items;
    const std::vector<SR::ClipRectGPU> clips{{body_.x, body_.y, body_.x + body_.w, body_.y + body_.h}};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    canvas_.appendItems(items, body_.x, body_.y, scroll_, body_.y, body_.y + body_.h, owner, 0u, paint);
    // Barra de rolagem unica do editor (ui_scrollbar.hpp): so aparece quando o conteudo passa do corpo.
    bar_.layout({body_.x + body_.w - ui_scrollbar::kWidth - 2.f, body_.y, ui_scrollbar::kWidth, body_.h}, contentH_, body_.h);
    for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), scroll_)) {
      if (q.rect.w > 0.f && q.rect.h > 0.f) items.push_back(sui::Canvas::solidItem(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), 0.8f, owner, 0u, paint++));
    }
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
  framed::Chrome chrome_;
  sui::Box body_;   // corpo da moldura neste quadro (vazio: escondida ou minimizada)
  ui_scrollbar::Scrollbar bar_;
  float scroll_ = 0.f;
  float contentH_ = 0.f;
  bool built_ = false;
  std::string builtKey_;
  std::string result_;
  bool resultOk_ = false;
};

}  // namespace exportui
}  // namespace editor
