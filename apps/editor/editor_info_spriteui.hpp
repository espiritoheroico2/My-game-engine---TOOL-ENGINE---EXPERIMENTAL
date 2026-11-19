#pragma once

// Janela "Editor info" feita com SpriteUI (sem Dear ImGui). Estado e regras: editor_info_model.hpp.
// Ligacao com o SDL_Renderer: editor::sui::SpriteUiSdlRenderer (spriteui_host.hpp). Fonte: assets/fonts/Xmind-Regular.ttf.
//
// Esta view so desenha e reporta cliques. Quem grava o projeto e o laço do host (Action::Save), para o modelo continuar
// testavel sem janela. Ainda NAO esta ligada ao menu: a janela ImGui em export_ui.hpp continua a usada pelo editor
// ate o teste visual aprovar esta.
#include "editor_info_model.hpp"
#include "i18n.hpp"
#include "spriteui_host.hpp"

#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::infoui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
using editor::info::Field;
using editor::info::kFieldCount;
using editor::info::Key;
using Model = editor::info::Model;

// Medidas. A altura do painel e fixa (calculada abaixo): o layout nao mede o conteudo.
struct Theme {
  static constexpr float kPanelWidth = 680.f;
  static constexpr float kPadding = 24.f;
  static constexpr float kGap = 6.f;
  static constexpr float kTitleHeight = 30.f;
  static constexpr float kLabelHeight = 16.f;
  static constexpr float kFieldHeight = 30.f;
  static constexpr float kButtonHeight = 34.f;
  static constexpr float kButtonWidth = 160.f;
  static constexpr float kLineHeight = 18.f;   // status e lista de faltantes
  static constexpr float kFontSize = 15.f;
  static constexpr float kTitleSize = 20.f;
  static constexpr float kLabelSize = 13.f;
  static constexpr float kFieldInset = 8.f;
  // Titulo + 9 (rotulo, campo) + linha do botao + status + faltantes; 21 espacos entre 22 filhos.
  static constexpr float kPanelHeight =
      2.f * kPadding + kTitleHeight + static_cast<float>(kFieldCount) * (kLabelHeight + kFieldHeight) +
      kButtonHeight + 2.f * kLineHeight + 21.f * kGap;
};

// Rotulo de cada campo, na ordem de editor::info::Field (mesmas chaves do formulario ImGui).
inline const char* FieldLabel(Field f) {
  switch (f) {
    case Field::Product: return "Product name *";
    case Field::Version: return "Version (e.g. 1.0.0) *";
    case Field::Description: return "Description *";
    case Field::Creator: return "Creator *";
    case Field::Studio: return "Studio *";
    case Field::Country: return "Country *";
    case Field::City: return "City";
    case Field::Website: return "Website";
    case Field::Copyright: return "Copyright (empty = automatic)";
    case Field::Count: return "";
  }
  return "";
}

inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}

// Cores do tema escuro: mesmas da tela de inicio.
struct Colors {
  SU::Vec4 panel = Srgb(0.125f, 0.125f, 0.137f);
  SU::Vec4 field = Srgb(0.165f, 0.165f, 0.176f);
  SU::Vec4 fieldFocus = Srgb(0.196f, 0.196f, 0.216f);
  SU::Vec4 button = Srgb(0.200f, 0.345f, 0.588f);
  SU::Vec4 buttonHover = Srgb(0.263f, 0.420f, 0.690f);
  SU::Vec4 text = Srgb(0.910f, 0.918f, 0.933f);
  SU::Vec4 textDim = Srgb(0.560f, 0.565f, 0.600f);
  SU::Vec4 selection = Srgb(0.245f, 0.420f, 0.690f, 0.75f);
  SU::Vec4 caret = Srgb(0.930f, 0.930f, 0.950f);
  SU::Vec4 warning = Srgb(1.000f, 0.800f, 0.300f);
  SU::Vec4 ok = Srgb(0.400f, 0.900f, 0.500f);
};

// Entrada do quadro (ponteiro e foco da janela), ja lida do SDL pelo host.
struct FrameInput {
  float pointerX = 0.f, pointerY = 0.f;
  bool pointerDown = false;
  bool pointerPressed = false;
  bool pointerReleased = false;
  bool windowFocused = true;
  std::uint64_t frameId = 0;
};

// Texto ja medido, com o layout para caret e selecao.
struct Shaped {
  std::string utf8;
  std::vector<ST::GlyphInstance> glyphs;
  ST::TextLayoutResult layout;
};

class View {
 public:
  enum class Action { None, Save };

  View(Model& model, const ST::TruetypeAtlas& atlas) : model_{model}, atlas_{atlas} { buildTree(); }

  // Calcula um quadro e retorna o pacote para desenhar. `actions` recebe o que foi clicado neste quadro.
  SR::FramePacket Frame(float windowWidth, float windowHeight, const FrameInput& in, std::vector<Action>& actions) {
    actions.clear();
    if (windowWidth <= 0.f || windowHeight <= 0.f) return {};

    // Largura: cabe na janela com 16 px de margem de cada lado.
    const float panelWidth = std::min(Theme::kPanelWidth, windowWidth - 32.f);
    setPanelWidth(panelWidth);
    const float x = std::floor((windowWidth - panelWidth) * 0.5f);
    const float y = std::max(0.f, std::floor((windowHeight - Theme::kPanelHeight) * 0.5f));
    tree_.Layout({{x, y}, {panelWidth, Theme::kPanelHeight}});

    // Texto medido de cada campo e status/faltantes (muda com o modelo, por isso mede a cada quadro).
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      fieldText_[i] = shape(model_.field(static_cast<Field>(i)).Text(), Theme::kFontSize);
      followCaret(i);
    }

    // Visuais antes da extracao: o extrator so cria item para no que ja tem visual (senao campos e botao somem).
    applyVisuals();
    auto scene = bridge_.Extract(tree_);
    SU::Input::UIInputSnapshot input;
    input.pointerPositionLogical = {in.pointerX, in.pointerY};
    input.pointerDeltaLogical = {in.pointerX - lastX_, in.pointerY - lastY_};
    input.primaryDown = in.pointerDown;
    input.primaryPressed = in.pointerPressed;
    input.primaryReleased = in.pointerReleased;
    input.windowFocused = in.windowFocused;
    input.frameId = in.frameId;
    lastX_ = in.pointerX;
    lastY_ = in.pointerY;

    const auto events = router_.Process(input, scene.hitTargets);
    for (const auto& event : events) {
      if (event.type == SU::Input::UIEventType::Click) handleClick(event, actions);
    }
    hovered_ = router_.GetState().hoveredEntity;

    for (auto& item : scene.renderItems) {
      if (const auto* visual = bridge_.GetVisual(item.handle)) {
        item.tintPremultipliedLinear = visual->tintPremultipliedLinear;
        item.zOrder = visual->zOrder;
      }
    }

    if (!scene.clipHierarchy.valid) return {};
    std::vector<SR::UIRenderItem> extra;
    std::vector<SR::ClipRectGPU> clips = scene.clipHierarchy.effectiveRects;
    addDecorations(extra, clips);
    scene.renderItems.insert(scene.renderItems.end(), extra.begin(), extra.end());
    return extractor_.Build(in.frameId, scene.renderItems, nullptr, clips);
  }

  [[nodiscard]] SU::UIHandle fieldHandle(Field f) const { return fields_[static_cast<std::size_t>(f)]; }
  [[nodiscard]] SU::UIHandle saveHandle() const { return save_; }
  [[nodiscard]] SU::Rect rectOf(SU::UIHandle handle) const { return nodeRect(handle); }

 private:
  // ---------- Arvore de layout ----------
  void buildTree() {
    SU::FlexItem panel;
    panel.width = {SU::SizeMode::Fixed, Theme::kPanelWidth};
    panel.height = {SU::SizeMode::Fixed, Theme::kPanelHeight};
    panel.direction = SU::LayoutDirection::Column;
    panel.alignItems = SU::AlignItems::Stretch;
    panel.padding = {Theme::kPadding, Theme::kPadding, Theme::kPadding, Theme::kPadding};
    panel.gap = Theme::kGap;
    panelHandle_ = tree_.Create({}, panel);

    title_ = tree_.Create(panelHandle_, labelStyle(Theme::kTitleHeight));
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      labels_[i] = tree_.Create(panelHandle_, labelStyle(Theme::kLabelHeight));
      fields_[i] = tree_.Create(panelHandle_, fieldStyle());
    }
    const auto buttonRow = tree_.Create(panelHandle_, rowStyle(Theme::kButtonHeight));
    save_ = tree_.Create(buttonRow, buttonStyle(Theme::kButtonWidth));
    status_ = tree_.Create(panelHandle_, labelStyle(Theme::kLineHeight));
    missing_ = tree_.Create(panelHandle_, labelStyle(Theme::kLineHeight));

    SU::ElementVisual panelVisual;
    panelVisual.tintPremultipliedLinear = colors_.panel;
    panelVisual.interactable = false;
    panelVisual.clipOverflow = true;
    panelVisual.zOrder = -1.f;   // fundo do painel atras dos campos e do botao (zOrder 0)
    bridge_.SetVisual(panelHandle_, panelVisual);
  }

  // Filhos de altura fixa nao encolhem (shrink = 0): sem isso o flex os reduz a zero dentro da coluna.
  static SU::FlexItem labelStyle(float height) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    return s;
  }
  static SU::FlexItem fieldStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, Theme::kFieldHeight};
    s.shrink = 0.f;
    return s;
  }
  static SU::FlexItem buttonStyle(float width) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, width};
    s.height = {SU::SizeMode::Fixed, Theme::kButtonHeight};
    s.shrink = 0.f;
    return s;
  }
  static SU::FlexItem rowStyle(float height) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    s.direction = SU::LayoutDirection::Row;
    s.alignItems = SU::AlignItems::Stretch;
    return s;
  }

  void setPanelWidth(float width) {
    auto* el = tree_.Get(panelHandle_);
    if (!el) return;
    SU::FlexItem style = el->node.style;
    style.width.value = width;
    tree_.SetStyle(panelHandle_, style);
  }

  // ---------- Texto ----------
  Shaped shape(const std::string& utf8, float size) const {
    Shaped s;
    s.utf8 = utf8;
    s.glyphs = ST::ShapeTruetype(utf8, atlas_);
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::None;
    s.layout = ST::LayoutText(utf8, s.glyphs, atlas_.metrics, cfg);
    return s;
  }

  // ---------- Visuais, clique e foco ----------
  void applyVisuals() {
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      SU::ElementVisual v;
      v.tintPremultipliedLinear = model_.hasFocus(static_cast<Field>(i)) ? colors_.fieldFocus : colors_.field;
      v.clipOverflow = true;
      v.zOrder = 0.f;
      bridge_.SetVisual(fields_[i], v);
    }
    SU::ElementVisual button;
    button.tintPremultipliedLinear = hovered_ == save_ ? colors_.buttonHover : colors_.button;
    button.zOrder = 0.f;
    bridge_.SetVisual(save_, button);
  }

  void handleClick(const SU::Input::UIEvent& event, std::vector<Action>& actions) {
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      if (event.target != fields_[i]) continue;
      const auto field = static_cast<Field>(i);
      model_.Focus(field);
      // Posicao do clique dentro do campo -> byte do caret. O texto e centralizado na altura do campo.
      const float textY = (nodeRect(fields_[i]).size.y - fieldText_[i].layout.lineHeight) * 0.5f;
      const SU::Vec2 local{event.localPosition.x - Theme::kFieldInset + scroll_[i], event.localPosition.y - textY};
      model_.SetCaret(ST::FindInsertionOffset(fieldText_[i].layout, local), false);
      return;
    }
    if (event.target == save_) actions.push_back(Action::Save);
  }

  // Rolagem horizontal: mantem o caret visivel na area util do campo. Sem foco, mostra o inicio do texto.
  void followCaret(std::size_t i) {
    const SU::Rect r = nodeRect(fields_[i]);
    const float visible = std::max(0.f, r.size.x - 2.f * Theme::kFieldInset);
    const float maxScroll = std::max(0.f, fieldText_[i].layout.size.x - visible);
    float& scroll = scroll_[i];
    if (!model_.hasFocus(static_cast<Field>(i))) {
      scroll = std::min(scroll, maxScroll);
      return;
    }
    const float caretX = ST::GetCaretRect(fieldText_[i].layout,
                                          static_cast<std::uint32_t>(model_.field(static_cast<Field>(i)).Caret())).position.x;
    if (caretX - scroll < 0.f) scroll = caretX;
    if (caretX - scroll > visible) scroll = caretX - visible;
    scroll = std::clamp(scroll, 0.f, maxScroll);
  }

  SU::Rect nodeRect(SU::UIHandle handle) const {
    const auto* el = tree_.Get(handle);
    return el ? el->node.rect : SU::Rect{};
  }

  // Desenho que a SpriteUI nao cria sozinha: texto, selecao, caret, status e faltantes. Usa o no como dono.
  void addDecorations(std::vector<SR::UIRenderItem>& out, std::vector<SR::ClipRectGPU>& clips) {
    std::uint64_t paint = 1000;
    std::uint32_t clipNow = SR::NoClipIndex;
    auto solid = [&](SU::UIHandle owner, SU::Rect r, SU::Vec4 color, float z) {
      SR::UIRenderItem item;
      item.handle = owner;
      item.layout.resolvedPosition = r.position;
      item.layout.resolvedSize = r.size;
      item.tintPremultipliedLinear = color;
      item.textureSlice = SR::WhiteTextureSlice;
      item.clipIndex = clipNow;
      item.zOrder = z;
      item.paintOrder = paint++;
      out.push_back(item);
    };
    auto text = [&](SU::UIHandle owner, const Shaped& shaped, SU::Vec2 origin, SU::Vec4 color, float z) {
      if (shaped.utf8.empty()) return;
      SU::UILayoutRect box;
      box.resolvedPosition = origin;
      box.resolvedSize = {shaped.layout.size.x, shaped.layout.size.y};
      auto items = ST::GenerateTextRenderItems(shaped.layout, box, color, atlasSlice_, clipNow, z, paint, owner, 0u);
      paint += items.size() + 1;
      out.insert(out.end(), items.begin(), items.end());
    };

    text(title_, shape(T("Editor info"), Theme::kTitleSize), nodeRect(title_).position, colors_.text, 0.9f);

    for (std::size_t i = 0; i < kFieldCount; ++i) {
      const auto field = static_cast<Field>(i);
      text(labels_[i], shape(T(FieldLabel(field)), Theme::kLabelSize), nodeRect(labels_[i]).position, colors_.textDim, 0.9f);

      // Campo: selecao, caret e texto, recortados dentro da caixa.
      const SU::UIHandle handle = fields_[i];
      const SU::Rect r = nodeRect(handle);
      const Shaped& s = fieldText_[i];
      const float textY = r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f;
      const SU::Vec2 origin{r.position.x + Theme::kFieldInset - scroll_[i], textY};
      clips.push_back({r.position.x, r.position.y, r.position.x + r.size.x, r.position.y + r.size.y});
      clipNow = static_cast<std::uint32_t>(clips.size() - 1);
      const auto& state = model_.field(field);
      if (model_.hasFocus(field) && state.HasSelection()) {
        const auto [begin, end] = state.SelectionRange();
        const SU::Rect a = ST::GetCaretRect(s.layout, static_cast<std::uint32_t>(begin));
        const SU::Rect c = ST::GetCaretRect(s.layout, static_cast<std::uint32_t>(end));
        solid(handle, {{origin.x + a.position.x, origin.y}, {c.position.x - a.position.x, s.layout.lineHeight}},
              colors_.selection, 0.5f);
      }
      if (model_.hasFocus(field)) {
        const SU::Rect caret = ST::GetCaretRect(s.layout, static_cast<std::uint32_t>(state.Caret()));
        solid(handle, {{origin.x + caret.position.x, origin.y}, {1.5f, s.layout.lineHeight}}, colors_.caret, 0.6f);
      }
      text(handle, s, origin, colors_.text, 0.9f);
      clipNow = SR::NoClipIndex;
    }

    // Botao Save info: texto centralizado.
    const Shaped save = shape(T("Save info"), Theme::kFontSize);
    const SU::Rect sr = nodeRect(save_);
    text(save_, save, {sr.position.x + (sr.size.x - save.layout.size.x) * 0.5f,
                       sr.position.y + (sr.size.y - save.layout.lineHeight) * 0.5f}, colors_.text, 0.9f);

    // Status depois de salvar (cor verde quando gravou, neutra quando nao).
    if (!model_.status().empty()) {
      text(status_, shape(T(model_.status().c_str()), Theme::kLabelSize), nodeRect(status_).position,
           model_.saved() ? colors_.ok : colors_.textDim, 0.9f);
    }

    // Campos obrigatorios que faltam (amarelo, como no ImGui).
    const std::vector<std::string> miss = model_.missing();
    if (!miss.empty()) {
      std::string list;
      for (const auto& m : miss) {
        if (!list.empty()) list += ", ";
        list += T(m.c_str());
      }
      text(missing_, shape(T("Missing:") + std::string(" ") + list, Theme::kLabelSize), nodeRect(missing_).position,
           colors_.warning, 0.9f);
    } else {
      text(missing_, shape(T("All required fields are filled."), Theme::kLabelSize), nodeRect(missing_).position,
           colors_.ok, 0.9f);
    }
  }

  Model& model_;
  const ST::TruetypeAtlas& atlas_;
  Colors colors_;
  static constexpr std::uint32_t atlasSlice_ = 1u;   // textureSlice do atlas de glifos

  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  SU::Input::UIEventRouter router_;
  SR::RenderExtractor extractor_;

  SU::UIHandle panelHandle_{}, title_{}, save_{}, status_{}, missing_{};
  SU::UIHandle labels_[kFieldCount]{}, fields_[kFieldCount]{};
  SU::UIHandle hovered_{};
  Shaped fieldText_[kFieldCount]{};
  float scroll_[kFieldCount]{};
  float lastX_ = 0.f, lastY_ = 0.f;
};

}  // namespace editor::infoui
