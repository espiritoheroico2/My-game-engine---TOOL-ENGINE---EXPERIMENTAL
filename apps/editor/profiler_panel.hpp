#pragma once

// Conteudo do Profiler feito com SpriteUI e hospedado na janela dockavel ImGui do editor.
//
// O que aparece vem do modelo (profiler_model.hpp): ele decide as linhas, os numeros, a escala dos graficos e a
// taxa de atualizacao. Este arquivo so faz layout, medida de texto e retangulos.
//
// Como funciona:
//   refresh(): roda o modelo e reconstroi o conteudo em coordenadas locais da lista (retangulos e textos ja
//              medidos). So e chamado quando due() diz que o proximo refresh chegou (4 vezes por segundo).
//   draw():    usa a area da janela hospedeira, aplica a rolagem e desenha com o SpriteUiSdlRenderer.
//              Por quadro nada e medido de novo: so os itens guardados sao deslocados.
//
// Layout: a janela hospedeira fornece titulo, docking, resize e fechamento; o conteudo tem rolagem propria.
// Ligacao com o SDL: spriteui_host.hpp (SpriteUiSdlRenderer, LoadTextAtlas). Fonte: assets/fonts/Xmind-Regular.ttf.
// Padrao de uso da SpriteUI: start_screen_spriteui.hpp (LayoutTree para os nos com clique; addDecorations para texto e retangulos).
//
// A roda sobre o conteudo rola a lista. Eventos de ponteiro seguem para o ImGui para foco e docking funcionarem.
#include "i18n.hpp"
#include "profiler.hpp"
#include "profiler_export.hpp"
#include "profiler_model.hpp"
#include "spriteui_host.hpp"

#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"
#include "window_frame.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace editor::profiler_panel {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace pm = editor::profiler_model;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace wf = editor::window_frame;

struct Theme {
  static constexpr float kToolbarH = 30.f;        // barra do topo: botao Exportar e status
  static constexpr float kBtnH = 24.f;
  static constexpr float kBtnPad = 12.f;
  static constexpr float kWidth = 420.f;          // largura inicial recomendada (px)
  static constexpr float kPad = sui::kPanelPadding; // margem lateral do conteudo
  static constexpr float kScrollLane = 12.f;      // reserva a direita da lista para a barra de rolagem
  static constexpr float kScrollW = 4.f;
  static constexpr float kHeadingFont = 14.f;
  static constexpr float kFont = 13.f;
  static constexpr float kSmallFont = 11.f;
  static constexpr float kEmphasisFont = 17.f;    // Prioridade: valores principais
  static constexpr float kEmphasisHeading = 19.f; // Prioridade: titulo da secao
  static constexpr float kLabelW = 170.f;         // Bar: largura do rotulo antes da barra
  static constexpr float kBarValueW = 64.f;       // Bar: largura reservada ao valor
  static constexpr float kBarH = 8.f;
  static constexpr float kGraphH = 56.f;
  static constexpr float kGap = 8.f;
  static constexpr float kWheelStep = 48.f;       // px por passo da roda do mouse
};

inline constexpr std::uint32_t kAtlasSlice = 1u;  // textureSlice dos glifos (0 = pixel branco)

inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}

// Cores do tema escuro (mesmos valores do painel de diagnostico do SpriteUI).
struct Colors {
  SU::Vec4 panel = Srgb(0.055f, 0.075f, 0.11f, 0.97f);
  SU::Vec4 text = Srgb(0.91f, 0.918f, 0.933f);
  SU::Vec4 textDim = Srgb(0.56f, 0.565f, 0.60f);
  SU::Vec4 heading = Srgb(0.45f, 0.72f, 0.96f);
  SU::Vec4 divider = Srgb(0.20f, 0.24f, 0.30f);
  SU::Vec4 track = Srgb(0.16f, 0.18f, 0.22f);
  SU::Vec4 barFill = Srgb(0.20f, 0.52f, 0.86f);
  SU::Vec4 graphBg = Srgb(0.08f, 0.10f, 0.14f);
  SU::Vec4 graphA = Srgb(0.30f, 0.70f, 0.98f);    // grafico 0: tempo de quadro
  SU::Vec4 graphB = Srgb(0.95f, 0.62f, 0.25f);    // grafico 1: trabalho da CPU
  SU::Vec4 scroll = Srgb(0.45f, 0.48f, 0.55f, 0.75f);
  SU::Vec4 btn = Srgb(0.13f, 0.16f, 0.21f);
  SU::Vec4 btnPressed = Srgb(0.09f, 0.11f, 0.15f);
  SU::Vec4 btnDisabled = Srgb(0.10f, 0.12f, 0.15f);
};

// Um retangulo ou um texto ja medido, em coordenadas locais da lista (sem rolagem).
struct Prim {
  enum class Kind : std::uint8_t { Solid, Text };
  Kind kind = Kind::Solid;
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  SU::Vec4 color{};
  float z = 0.f;
  std::uint32_t text = 0;   // Text: indice em texts_
};

class ProfilerPanel {
 public:
  // `sdl` e o renderizador compartilhado das janelas SpriteUI (o atlas ja foi enviado por quem o criou).
  ProfilerPanel(const ST::TruetypeAtlas& atlas, sui::SpriteUiSdlRenderer& sdl) : atlas_{atlas}, sdl_{sdl} {
    buildTree();
    // Moldura: mesmo visual do Console (corpo navy, aba de 30 px).
    frameColors_.body = colors_.panel;
    frameColors_.border = Srgb(0.160f, 0.220f, 0.330f);
    frameColors_.borderDragging = Srgb(0.420f, 0.600f, 0.980f);
    frameColors_.headerActive = Srgb(0.120f, 0.160f, 0.230f);
    frameColors_.headerInactive = Srgb(0.090f, 0.120f, 0.180f);
    frameColors_.headerDragging = Srgb(0.160f, 0.210f, 0.300f);
    frameColors_.glyph = Srgb(0.910f, 0.918f, 0.933f);
  }
  ProfilerPanel(const ProfilerPanel&) = delete;
  ProfilerPanel& operator=(const ProfilerPanel&) = delete;

  // true quando o proximo refresh ja deve acontecer (ou nunca aconteceu).
  bool due(double nowSec) const { return model_.due(nowSec); }

  // Roda o modelo e reconstroi o conteudo. Chamar so quando due() for true: entre dois refreshes nada muda.
  void refresh(double nowSec, const editor::FrameProfiler& prof, const pm::Extras& extras) {
    model_.refresh(nowSec, prof, prof.sys.get(), extras, [](const char* s) { return T(s); });
    rebuild();
  }

  bool exporting() const { return exporting_; }
  const std::string& exportStatus() const { return exportStatus_; }

  void beginExport(double nowSec) {
    if (exporting_) return;
    exportSamples_.clear();
    exportStart_ = nowSec;
    exportSchedule_.start(nowSec);
    exporting_ = true;
    exportStatus_ = "Collecting Profiler data (0/5)";
  }

  void tickExport(double nowSec) {
    if (!exporting_ || !exportSchedule_.due(nowSec)) return;
    exportSamples_.push_back(profiler_export::Capture(model_, nowSec - exportStart_));
    if (!exportSchedule_.record(nowSec)) {
      exportStatus_ = "Collecting Profiler data (" + std::to_string(exportSchedule_.sampleCount()) + "/5)";
      return;
    }
    saveExport();
  }

  // Moldura (X fecha, grip arrasta), botao Exportar e roda sobre a lista. Devolve true quando consumiu o evento.
  bool handleEvent(const SDL_Event& e, bool visible) {
    if (!visible) return false;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        if (frameDown(e.button.x, e.button.y)) return true;
        if (!exporting_ && insideRect(exportBox_, e.button.x, e.button.y)) {
          exportPressed_ = true;
          return true;
        }
        return false;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        frame_.pointerUp();
        if (e.button.button == SDL_BUTTON_LEFT && exportPressed_) {
          exportPressed_ = false;
          if (insideRect(exportBox_, e.button.x, e.button.y)) exportRequested_ = true;
        }
        return false;
      case SDL_EVENT_MOUSE_MOTION:
        if (frame_.dragging()) {
          frame_.pointerMove({e.motion.x, e.motion.y});
          return true;
        }
        return false;
      case SDL_EVENT_MOUSE_WHEEL:
        if (!contains(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
        scroll_ = std::clamp(scroll_ - e.wheel.y * Theme::kWheelStep, 0.f, maxScroll_);
        return true;
      default:
        return false;
    }
  }

  // Pedidos da moldura e do botao, lidos e zerados pelo host.
  bool consumeClose() {
    const bool v = closeRequested_;
    closeRequested_ = false;
    return v;
  }
  bool consumeExportRequest() {
    const bool v = exportRequested_;
    exportRequested_ = false;
    return v;
  }

  // Desenha a janela: moldura, barra do topo e a lista (rolagem). Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, SDL_Window* window) {
    if (!renderer || !window) return false;
    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
      hasPacket_ = false;
      return true;
    }
    // Primeira posicao: canto inferior esquerdo, com a largura de antes (420 px). O Console nasce no centro.
    frame_.place(static_cast<float>(winW), static_cast<float>(winH), Theme::kWidth, 16.f);
    const SU::Rect content = frame_.content();
    const SU::Rect area{{content.position.x, content.position.y + Theme::kToolbarH},
                        {content.size.x, std::max(0.f, content.size.y - Theme::kToolbarH)}};
    if (area.size.x <= 0.f || area.size.y <= 0.f) {
      hasPacket_ = false;
      return true;
    }

    const float panelW = area.size.x;
    const float panelH = area.size.y;
    const float panelX = area.position.x;
    const float panelY = area.position.y;
    if (std::abs(panelW - contentWidth_) > 0.5f) {
      contentWidth_ = panelW;
      rebuild();
    }
    viewportH_ = panelH;
    maxScroll_ = std::max(0.f, contentH_ - viewportH_);
    scroll_ = std::clamp(scroll_, 0.f, maxScroll_);
    panelBounds_ = {panelX, panelY, panelW, panelH};
    setSize(panel_, panelW, panelH);
    tree_.Layout({{panelX, panelY}, {panelW, panelH}});

    auto scene = bridge_.Extract(tree_);

    // Conteudo: retangulos e textos guardados, deslocados pela rolagem e recortados na lista.
    hasPacket_ = false;
    if (!scene.clipHierarchy.valid) return true;
    std::vector<SR::ClipRectGPU> clips = scene.clipHierarchy.effectiveRects;
    const SU::Rect list = nodeRect(list_);
    clips.push_back({list.position.x, list.position.y, list.position.x + list.size.x, list.position.y + list.size.y});
    const auto listClip = static_cast<std::uint32_t>(clips.size() - 1);
    std::vector<SR::UIRenderItem> extra;
    appendContent(extra, list, listClip);
    appendFrameDecorations(extra, listClip);
    appendWindow(extra, content);
    scene.renderItems.insert(scene.renderItems.end(), extra.begin(), extra.end());
    packet_ = extractor_.Build(++frameId_, scene.renderItems, nullptr, clips);
    hasPacket_ = SR::IsFramePacketValid(packet_);
    return sdl_.draw(renderer, packet_);
  }

  // Estado para teste.
  [[nodiscard]] float scroll() const { return scroll_; }
  [[nodiscard]] float contentHeight() const { return contentH_; }
  [[nodiscard]] bool hasPacket() const { return hasPacket_; }

 private:
  // ---------- Moldura, barra do topo e botao (por quadro) ----------
  bool frameDown(float x, float y) {
    const wf::Event ev = frame_.pointerDown({x, y});
    if (ev == wf::Event::Close) closeRequested_ = true;
    return ev != wf::Event::None;
  }

  static bool insideRect(SU::Rect r, float x, float y) {
    return x >= r.position.x && y >= r.position.y && x < r.position.x + r.size.x && y < r.position.y + r.size.y;
  }

  // Texto de uma linha, centrado em yMid, para a moldura e a barra (fora da lista: nao rola).
  void textItem(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint, float x, float yMid, const std::string& s, float size,
                SU::Vec4 color, float maxW) {
    if (s.empty() || maxW <= 0.f) return;
    const ST::TextLayoutResult lay = layoutOf(fit(s, size, maxW), size);
    SU::UILayoutRect box;
    box.resolvedPosition = {x, yMid - lay.lineHeight * 0.5f};
    box.resolvedSize = {lay.size.x, lay.size.y};
    auto items = ST::GenerateTextRenderItems(lay, box, color, kAtlasSlice, SR::NoClipIndex, 0.95f, paint, panel_, 0u);
    paint += items.size() + 1;
    out.insert(out.end(), items.begin(), items.end());
  }

  // Moldura SpriteUI (corpo, aba, X), titulo, grip e barra do topo com o botao Exportar e o status.
  void appendWindow(std::vector<SR::UIRenderItem>& out, const SU::Rect& content) {
    std::uint64_t paint = 100000;
    SW::WindowFrameState state;
    state.active = true;
    state.dragging = frame_.dragging();
    state.docked = false;
    auto frameItems = SW::BuildWindowFrameItems(panel_, frame_.rect(), frame_.metrics(), state, frameColors_, -2.f, paint);
    paint += frameItems.size() + 1;
    out.insert(out.end(), frameItems.begin(), frameItems.end());

    const SU::Rect title = frame_.titleRect();
    textItem(out, paint, title.position.x + wf::Metrics::kGripWidth, title.position.y + title.size.y * 0.5f, T("Profiler"),
             Theme::kHeadingFont + 1.f, colors_.text, title.size.x - wf::Metrics::kGripWidth);

    // Grip: 2 colunas x 3 pontos, no centro do icone.
    const SU::Rect grip = frame_.gripRect();
    const float cx = grip.position.x + grip.size.x * 0.5f, cy = grip.position.y + grip.size.y * 0.5f;
    for (int c = 0; c < 2; ++c) {
      for (int r = 0; r < 3; ++r) {
        out.push_back(solidItem(cx - 3.f + 4.f * static_cast<float>(c), cy - 4.f + 4.f * static_cast<float>(r), 2.f, 2.f,
                                colors_.textDim, 0.95f, SR::NoClipIndex, paint++));
      }
    }

    // Barra do topo: botao Exportar (apagado enquanto exporta) e o status ao lado.
    const float barY = content.position.y;
    const std::string label = T("Export data");
    const float labelW = layoutOf(label, Theme::kFont).size.x;
    const float btnW = labelW + 2.f * Theme::kBtnPad;
    exportBox_ = SU::Rect{{content.position.x + Theme::kPad * 0.5f, barY + (Theme::kToolbarH - Theme::kBtnH) * 0.5f},
                          {btnW, Theme::kBtnH}};
    const SU::Vec4 fill = exporting_ ? colors_.btnDisabled : (exportPressed_ ? colors_.btnPressed : colors_.btn);
    out.push_back(solidItem(exportBox_.position.x, exportBox_.position.y, btnW, Theme::kBtnH, fill, 0.2f, SR::NoClipIndex, paint++));
    textItem(out, paint, exportBox_.position.x + Theme::kBtnPad, barY + Theme::kToolbarH * 0.5f, label, Theme::kFont,
             exporting_ ? colors_.textDim : colors_.text, labelW);
    if (!exportStatus_.empty()) {
      const float sx = exportBox_.position.x + btnW + 12.f;
      const float right = content.position.x + content.size.x - Theme::kPad * 0.5f;
      textItem(out, paint, sx, barY + Theme::kToolbarH * 0.5f, exportStatus_, Theme::kFont, colors_.textDim, right - sx);
    }
  }

  // ---------- Arvore de layout: painel e lista ----------
  void buildTree() {
    SU::FlexItem panel;
    panel.width = {SU::SizeMode::Fixed, Theme::kWidth};
    panel.height = {SU::SizeMode::Fixed, 1.f};
    panel.direction = SU::LayoutDirection::Column;
    panel.alignItems = SU::AlignItems::Stretch;
    panel_ = tree_.Create({}, panel);

    SU::FlexItem list;
    list.width = {SU::SizeMode::Percent, 1.f};
    list.height = {SU::SizeMode::Flex, 1.f};
    list.grow = 1.f;
    list.shrink = 0.f;
    list_ = tree_.Create(panel_, list);

    SU::ElementVisual panelVisual;
    panelVisual.tintPremultipliedLinear = colors_.panel;
    panelVisual.interactable = false;
    panelVisual.clipOverflow = true;
    bridge_.SetVisual(panel_, panelVisual);
    builtWidth_ = Theme::kWidth;
  }

  void setSize(SU::UIHandle handle, float width, float height) {
    auto* element = tree_.Get(handle);
    if (!element) return;
    SU::FlexItem style = element->node.style;
    style.width.value = width;
    style.height.value = height;
    tree_.SetStyle(handle, style);
  }

  bool contains(float x, float y) const {
    return x >= panelBounds_.x && y >= panelBounds_.y && x < panelBounds_.x + panelBounds_.w && y < panelBounds_.y + panelBounds_.h;
  }

  SU::Rect nodeRect(SU::UIHandle handle) const {
    const auto* el = tree_.Get(handle);
    return el ? el->node.rect : SU::Rect{};
  }

  // ---------- Texto: medida e corte (so no refresh) ----------
  ST::TextLayoutResult layoutOf(const std::string& utf8, float size) const {
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::None;
    const auto glyphs = ST::ShapeTruetype(utf8, atlas_);
    return ST::LayoutText(utf8, glyphs, atlas_.metrics, cfg);
  }

  // Corta o texto com "..." para caber em maxW. Estima pela proporcao, entao mede poucas vezes.
  std::string fit(const std::string& s, float size, float maxW) const {
    const float full = layoutOf(s, size).size.x;
    if (full <= maxW) return s;
    std::size_t cut = static_cast<std::size_t>(std::floor(static_cast<float>(s.size()) * maxW / std::max(1.f, full)));
    cut = std::min(cut, s.size());
    while (cut > 0) {
      while (cut > 0 && cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // nao corta no meio de um UTF-8
      const std::string cand = s.substr(0, cut) + "...";
      if (layoutOf(cand, size).size.x <= maxW) return cand;
      --cut;
    }
    return "...";
  }

  // Negrito simulado (nao ha fonte em negrito nos assets): o mesmo texto desenhado de novo, 0,7 px ao lado.
  float addBold(float x, float y, const std::string& s, float size, SU::Vec4 color, float maxW, bool alignRight = false) {
    const float w = addText(x, y, s, size, color, maxW, alignRight);
    addText(x + 0.7f, y, s, size, color, maxW, alignRight);
    return w;
  }

  // Texto de largura limitada, medido e guardado. Retorna a largura desenhada (para alinhar o que vem depois).
  float addText(float x, float y, const std::string& s, float size, SU::Vec4 color, float maxW, bool alignRight = false) {
    if (s.empty() || maxW <= 0.f) return 0.f;
    const std::string shown = fit(s, size, maxW);
    ST::TextLayoutResult lay = layoutOf(shown, size);
    const float w = lay.size.x;
    Prim p;
    p.kind = Prim::Kind::Text;
    p.x = alignRight ? x - w : x;
    p.y = y;
    p.w = w;
    p.h = lay.lineHeight;
    p.color = color;
    p.z = 0.9f;
    p.text = static_cast<std::uint32_t>(texts_.size());
    texts_.push_back(std::move(lay));
    prims_.push_back(p);
    return w;
  }

  void addSolid(float x, float y, float w, float h, SU::Vec4 color, float z) {
    if (w <= 0.f || h <= 0.f) return;
    Prim p;
    p.kind = Prim::Kind::Solid;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.color = color;
    p.z = z;
    prims_.push_back(p);
  }

  float lineHeight(float size) const { return layoutOf("Ag", size).lineHeight; }

  static std::string trimmed(const char* s) {
    std::string out = s ? s : "";
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const auto last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
  }

  // ---------- Conteudo: uma passada pelas linhas do modelo ----------
  void rebuild() {
    prims_.clear();
    texts_.clear();
    const float x0 = Theme::kPad;
    const float inner = std::max(0.f, contentWidth_ - 2.f * Theme::kPad - Theme::kScrollLane);
    const float x1 = x0 + inner;
    const float lhFont = lineHeight(Theme::kFont);
    const float lhSmall = lineHeight(Theme::kSmallFont);
    const float rowH = lhFont + 4.f;
    float y = Theme::kPad * 0.5f;

    for (int i = 0; i < model_.rowCount(); ++i) {
      const pm::Row& r = model_.row(i);
      switch (r.kind) {
        case pm::RowKind::Gap:
          y += Theme::kGap;
          break;

        case pm::RowKind::Heading:
          y += Theme::kGap;
          if (r.emphasis) {
            addBold(x0, y, r.text, Theme::kEmphasisHeading, colors_.heading, inner);
            y += lineHeight(Theme::kEmphasisHeading) + 3.f;
          } else {
            addText(x0, y, r.text, Theme::kHeadingFont, colors_.heading, inner);
            y += lineHeight(Theme::kHeadingFont) + 3.f;
          }
          addSolid(x0, y, inner, 1.f, colors_.divider, 0.2f);
          y += 6.f;
          break;

        case pm::RowKind::Text:
          addText(x0, y, r.text, Theme::kFont, colors_.text, inner);
          y += rowH;
          break;

        case pm::RowKind::Graph: {
          // Legenda a esquerda; "max <valor>" a direita; depois o grafico de colunas (um por quadro).
          const std::string maxText = r.cellCount > 0 ? std::string(r.cells[0].label) + " " + trimmed(r.cells[0].value) : std::string();
          const float maxW = addText(x1, y, maxText, Theme::kSmallFont, colors_.textDim, inner * 0.6f, true);
          addText(x0, y, r.text, Theme::kSmallFont, colors_.textDim, inner - maxW - 10.f);
          y += lhSmall + 4.f;
          addSolid(x0, y, inner, Theme::kGraphH, colors_.graphBg, 0.1f);
          for (int line = 1; line < 4; ++line) {
            const float gridY = y + Theme::kGraphH * static_cast<float>(line) / 4.f;
            addSolid(x0, gridY, inner, 1.f, colors_.divider, 0.15f);
          }
          if (r.graph >= 0 && r.graph < pm::kGraphCount) {
            const float* samples = model_.graph(r.graph);
            const float scale = model_.graphScale(r.graph);
            const float colW = inner / static_cast<float>(model_.graphSamples());
            const SU::Vec4 colour = r.graph == 0 ? colors_.graphA : colors_.graphB;
            for (int s = 0; s < model_.graphSamples(); ++s) {
              const float sample = std::isfinite(samples[s]) ? samples[s] : 0.f;
              const float frac = scale > 0.f && std::isfinite(scale) ? std::clamp(sample / scale, 0.f, 1.f) : 0.f;
              const float h = frac * Theme::kGraphH;
              if (h >= 0.5f) addSolid(x0 + static_cast<float>(s) * colW, y + Theme::kGraphH - h, std::max(1.f, colW), h, colour, 0.2f);
            }
          }
          y += Theme::kGraphH + 6.f;
          break;
        }

        case pm::RowKind::Bar: {
          // Rotulo, barra e valor na mesma linha. A barra preenche `fraction` (0..1).
          const float ty = y + (rowH - lhFont) * 0.5f;
          const float labelW = std::min(Theme::kLabelW, inner * 0.48f);
          const float valueW = std::min(Theme::kBarValueW, inner * 0.24f);
          addText(x0, ty, r.text, Theme::kFont, colors_.text, labelW);
          const std::string value = r.cellCount > 0 ? trimmed(r.cells[0].value) : std::string();
          addText(x1, ty, value, Theme::kFont, colors_.text, valueW, true);
          const float trackX = x0 + labelW + 8.f;
          const float trackW = inner - labelW - 8.f - valueW - 8.f;
          const float trackY = y + (rowH - Theme::kBarH) * 0.5f;
          addSolid(trackX, trackY, trackW, Theme::kBarH, colors_.track, 0.2f);
          addSolid(trackX, trackY, trackW * std::clamp(r.fraction, 0.f, 1.f), Theme::kBarH, colors_.barFill, 0.3f);
          y += rowH;
          break;
        }

        case pm::RowKind::Cells: {
          const int n = r.cellCount;
          if (n == 1 && r.cells[0].valueWidth == 0) {
            // Uma linha de texto ("API: Vulkan"): rotulo a esquerda, valor a direita, numa linha.
            const float vw = addText(x1, y, r.cells[0].value, Theme::kFont, colors_.text, inner * 0.6f, true);
            addText(x0, y, r.cells[0].label, Theme::kFont, colors_.textDim, inner - vw - 10.f);
            y += rowH;
          } else if (n > 0) {
            // Varias celulas: rotulo pequeno em cima, valor embaixo, cada um na sua coluna.
            // Prioridade (r.emphasis): valor maior e em negrito; rotulo um degrau maior.
            const float labelSize = r.emphasis ? Theme::kFont : Theme::kSmallFont;
            const float valueSize = r.emphasis ? Theme::kEmphasisFont : Theme::kFont;
            const float lhLabel = lineHeight(labelSize);
            const float lhValue = lineHeight(valueSize);
            const float colW = inner / static_cast<float>(n);
            for (int c = 0; c < n; ++c) {
              const float cx = x0 + colW * static_cast<float>(c);
              addText(cx, y, r.cells[c].label, labelSize, colors_.textDim, colW - 6.f);
              if (r.emphasis) addBold(cx, y + lhLabel + 1.f, r.cells[c].value, valueSize, colors_.text, colW - 6.f);
              else addText(cx, y + lhLabel + 1.f, r.cells[c].value, valueSize, colors_.text, colW - 6.f);
            }
            y += lhLabel + 1.f + lhValue + 6.f;
          } else {
            y += rowH;
          }
          break;
        }
      }
    }
    contentH_ = y + Theme::kPad * 0.5f;
    builtWidth_ = contentWidth_;
  }

  // ---------- Quadro: retangulos e textos da lista (deslocados pela rolagem) ----------
  void appendContent(std::vector<SR::UIRenderItem>& out, const SU::Rect& list, std::uint32_t clip) {
    const float top = list.position.y;
    const float bottom = list.position.y + list.size.y;
    const float ox = list.position.x;
    const float oy = list.position.y - scroll_;
    std::uint64_t paint = 1000;
    for (const Prim& p : prims_) {
      const float sy = oy + p.y;
      if (sy + p.h < top || sy > bottom) continue;   // fora da janela: nem entra no pacote
      if (p.kind == Prim::Kind::Solid) {
        out.push_back(solidItem(ox + p.x, sy, p.w, p.h, p.color, p.z, clip, paint++));
      } else {
        SU::UILayoutRect box;
        box.resolvedPosition = {ox + p.x, sy};
        box.resolvedSize = {p.w, p.h};
        auto items = ST::GenerateTextRenderItems(texts_[p.text], box, p.color, kAtlasSlice, clip, p.z, paint, list_, 0u);
        paint += items.size() + 1;
        out.insert(out.end(), items.begin(), items.end());
      }
    }
  }

  // Barra de rolagem: sempre no lugar, por quadro (depende do viewport e da rolagem).
  void appendFrameDecorations(std::vector<SR::UIRenderItem>& out, std::uint32_t listClip) {
    if (contentH_ > viewportH_ + 0.5f && viewportH_ > 8.f) {
      const SU::Rect list = nodeRect(list_);
      const float trackH = std::max(1.f, viewportH_ - 8.f);
      const float thumbH = std::min(trackH, std::max(24.f, trackH * viewportH_ / contentH_));
      const float thumbY = list.position.y + 4.f + (maxScroll_ > 0.f ? (scroll_ / maxScroll_) * std::max(0.f, trackH - thumbH) : 0.f);
      out.push_back(solidItem(list.position.x + list.size.x - Theme::kScrollW - 4.f, thumbY, Theme::kScrollW, thumbH, colors_.scroll, 0.4f, listClip, 990));
    }
  }

  // Retangulo de cor solida. O dono (handle) tem que ser um no valido: o extrator recusa item sem dono.
  SR::UIRenderItem solidItem(float x, float y, float w, float h, SU::Vec4 color, float z, std::uint32_t clip, std::uint64_t paint) const {
    SR::UIRenderItem item;
    item.handle = list_;
    item.layout.resolvedPosition = {x, y};
    item.layout.resolvedSize = {w, h};
    item.tintPremultipliedLinear = color;
    item.textureSlice = SR::WhiteTextureSlice;
    item.clipIndex = clip;
    item.zOrder = z;
    item.paintOrder = paint;
    return item;
  }

  const ST::TruetypeAtlas& atlas_;
  Colors colors_;
  pm::ProfilerModel model_;
  sui::SpriteUiSdlRenderer& sdl_;

  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  SU::UIHandle panel_{}, list_{};

  std::vector<Prim> prims_;
  std::vector<ST::TextLayoutResult> texts_;

  struct Bounds { float x = 0.f, y = 0.f, w = 0.f, h = 0.f; };
  float contentH_ = 0.f, viewportH_ = 0.f, maxScroll_ = 0.f, scroll_ = 0.f;
  float contentWidth_ = Theme::kWidth, builtWidth_ = Theme::kWidth;
  Bounds panelBounds_{};
  std::uint64_t frameId_ = 0;
  bool hasPacket_ = false;
  bool exporting_ = false;

  // Moldura e pedidos da janela (X, grip e botao Exportar)
  wf::Frame frame_;
  SW::WindowFrameColors frameColors_{};
  SU::Rect exportBox_{};
  bool closeRequested_ = false;
  bool exportRequested_ = false;
  bool exportPressed_ = false;
  double exportStart_ = 0.0;
  profiler_export::CaptureSchedule exportSchedule_;
  std::vector<profiler_export::Snapshot> exportSamples_;
  std::string exportStatus_;

  void saveExport() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path directory = fs::current_path(ec);
    if (ec) {
      exporting_ = false;
      exportStatus_ = "Profiler export failed: " + ec.message();
      return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &time) != 0) {
#else
    if (!localtime_r(&time, &local)) {
#endif
      exporting_ = false;
      exportStatus_ = "Profiler export failed: could not format the current time";
      return;
    }
    char filename[64];
    std::snprintf(filename, sizeof filename, "ProfilerExport_%04d%02d%02d_%02d%02d%02d_%03lld.md",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<long long>(millis % 1000));
    const fs::path outputPath = directory / filename;
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) {
      exporting_ = false;
      exportStatus_ = "Profiler export failed: could not open " + outputPath.string();
      return;
    }
    file << profiler_export::ToMarkdown(exportSamples_);
    file.close();
    exporting_ = false;
    if (!file) {
      exportStatus_ = "Profiler export failed while writing " + outputPath.string();
      return;
    }
    exportStatus_ = "Saved: " + outputPath.string();
  }
};

}  // namespace editor::profiler_panel
