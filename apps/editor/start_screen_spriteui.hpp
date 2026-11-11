#pragma once

// UI-CONTRATO: tela de inicio (criar ou abrir projeto) feita com SpriteUI, sem Dear ImGui.
// Estado e acoes: start_screen_model.hpp. Catalogo e regras: ui_contract.hpp (secoes 2, 3 e 4).
// Ligacao com o SDL_Renderer: SpriteUiSdlRenderer (no fim deste arquivo). Fonte: assets/fonts/Xmind-Regular.ttf.
//
// Layout: um painel centralizado com titulo, campos (nome, autor, pasta, abrir), botoes e uma area do logo.
// Campos usam TextEditState (start_screen_model.hpp): caret, selecao e texto sao desenhados aqui.
#include "i18n.hpp"
#include "start_screen_model.hpp"

#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace editor::start {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;

// Medidas e cores do painel. Cores em sRGB reto; convertidas para a forma que a SpriteUI usa (linear premultiplicada).
struct Theme {
  static constexpr float kPanelWidth = 680.f;
  static constexpr float kPadding = 24.f;
  static constexpr float kFieldHeight = 30.f;
  static constexpr float kLabelHeight = 18.f;
  static constexpr float kButtonHeight = 34.f;
  static constexpr float kFontSize = 15.f;
  static constexpr float kTitleSize = 20.f;
  static constexpr float kFieldInset = 8.f;
  static constexpr float kLogoSize = 120.f;
};

inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}

// Fundo e cores do tema escuro (mesmos valores aproximados do ImGui atual).
struct Colors {
  SU::Vec4 background = Srgb(0.094f, 0.094f, 0.106f);
  SU::Vec4 panel = Srgb(0.125f, 0.125f, 0.137f);
  SU::Vec4 field = Srgb(0.165f, 0.165f, 0.176f);
  SU::Vec4 fieldFocus = Srgb(0.196f, 0.196f, 0.216f);
  SU::Vec4 button = Srgb(0.200f, 0.345f, 0.588f);
  SU::Vec4 buttonHover = Srgb(0.263f, 0.420f, 0.690f);
  SU::Vec4 buttonDisabled = Srgb(0.200f, 0.200f, 0.220f);
  SU::Vec4 text = Srgb(0.910f, 0.918f, 0.933f);
  SU::Vec4 textDim = Srgb(0.560f, 0.565f, 0.600f);
  SU::Vec4 selection = Srgb(0.245f, 0.420f, 0.690f, 0.75f);
  SU::Vec4 caret = Srgb(0.930f, 0.930f, 0.950f);
  SU::Vec4 divider = Srgb(0.240f, 0.240f, 0.260f);
  SU::Vec4 border = Srgb(0.320f, 0.320f, 0.345f);
  SU::Vec4 error = Srgb(1.000f, 0.450f, 0.450f);
};

// Ponteiro e teclado do quadro, ja lidos do SDL pelo host.
struct FrameInput {
  float pointerX = 0.f, pointerY = 0.f;
  bool pointerDown = false;
  bool pointerPressed = false;
  bool pointerReleased = false;
  bool windowFocused = true;
  std::uint64_t frameId = 0;
};

// Texto ja medido. Guarda a camada de layout para posicionar caret e selecao.
struct Shaped {
  std::string utf8;
  std::vector<ST::GlyphInstance> glyphs;
  ST::TextLayoutResult layout;
};

// Pasta escolhida no dialogo nativo chega de outra thread (mesmo problema de project_dialog.hpp).
struct BrowseSlot {
  std::mutex mu;
  std::string path;
  bool ready = false;
};

inline void SDLCALL onFolderPicked(void* userdata, const char* const* files, int) {
  auto* slot = static_cast<BrowseSlot*>(userdata);
  std::lock_guard<std::mutex> lock(slot->mu);
  if (files && files[0]) {
    slot->path = files[0];
    slot->ready = true;
  }
}

class View {
 public:
  View(Model& model, const ST::TruetypeAtlas& atlas) : model_{model}, atlas_{atlas} {
    buildTree();
  }

  // Calcula o quadro: layout, roteamento, texto, caret e selecao. Retorna o pacote pronto para desenhar.
  // `actions` recebe o que foi clicado neste quadro (ver Action).
  enum class Action { None, Browse, Create, Open, OpenLast };

  SR::FramePacket Frame(float windowWidth, float windowHeight, const FrameInput& in, std::vector<Action>& actions) {
    actions.clear();
    if (windowWidth <= 0.f || windowHeight <= 0.f) return {};

    // 1. Medidas: a altura do painel depende do conteudo; primeiro layout mede, o segundo centraliza.
    // O no raiz ocupa o viewport inteiro: o viewport tem que ter o tamanho do painel, senao o painel fica alto.
    const float panelWidth = std::min(Theme::kPanelWidth, windowWidth - 32.f);
    setPanelWidth(panelWidth);
    const SU::Vec2 panelSize{panelWidth, panelHeight()};
    tree_.Layout({{0.f, 0.f}, {panelSize.x, panelSize.y}});
    const float x = std::floor((windowWidth - panelSize.x) * 0.5f);
    const float y = std::max(0.f, std::floor((windowHeight - panelSize.y) * 0.5f));
    tree_.Layout({{x, y}, {panelSize.x, panelSize.y}});

    // 2. Texto medido para cada campo (com o layout de caret) e para os rotulos.
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      fieldText_[i] = shape(model_.field(static_cast<Field>(i)).Text(), Theme::kFontSize);
    }

    // 3. Cena da SpriteUI (fundos, botoes e campos com visual) e roteamento.
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
      if (event.type != SU::Input::UIEventType::Click) continue;
      handleClick(event, actions);
    }
    hovered_ = router_.GetState().hoveredEntity;

    // 4. Visuais dos nos (fundo do painel, campos, botoes) com o estado de foco e hover.
    applyVisuals();
    for (auto& item : scene.renderItems) {
      if (const auto* visual = bridge_.GetVisual(item.handle)) {
        item.tintPremultipliedLinear = visual->tintPremultipliedLinear;
        item.zOrder = visual->zOrder;
      }
    }

    // 5. Itens que a SpriteUI nao cria sozinha: texto, selecao, caret, divisores e borda do logo.
    std::vector<SR::UIRenderItem> extra;
    addDecorations(extra);
    scene.renderItems.insert(scene.renderItems.end(), extra.begin(), extra.end());

    if (!scene.clipHierarchy.valid) return {};
    return extractor_.Build(in.frameId, scene.renderItems, nullptr, scene.clipHierarchy.effectiveRects);
  }

  // Centro e tamanho de um botao em coordenadas da janela (para teste).
  [[nodiscard]] bool rectOf(SU::UIHandle handle, SU::Rect& out) const {
    const auto* el = tree_.Get(handle);
    if (!el) return false;
    out = el->node.rect;
    return true;
  }
  [[nodiscard]] SU::UIHandle fieldHandle(Field f) const { return fields_[static_cast<std::size_t>(f)]; }
  [[nodiscard]] SU::UIHandle browseHandle() const { return browse_; }
  [[nodiscard]] SU::UIHandle createHandle() const { return create_; }
  [[nodiscard]] SU::UIHandle openHandle() const { return open_; }
  [[nodiscard]] SU::UIHandle openLastHandle() const { return openLast_; }

 private:
  // ---------- Arvore de layout ----------
  void buildTree() {
    hasLast_ = !model_.lastProject().empty();   // antes do painel: a altura depende dele
    SU::FlexItem panel;
    panel.width = {SU::SizeMode::Fixed, Theme::kPanelWidth};
    // Altura fixa calculada (panelHeight): Auto nao mede o conteudo aqui.
    panel.height = {SU::SizeMode::Fixed, panelHeight()};
    panel.direction = SU::LayoutDirection::Column;
    panel.alignItems = SU::AlignItems::Stretch;
    panel.padding = {Theme::kPadding, Theme::kPadding, Theme::kPadding, Theme::kPadding};
    panel.gap = 8.f;
    panelHandle_ = tree_.Create({}, panel);

    const SU::FlexItem fullRow = row(SU::Length{SU::SizeMode::Percent, 1.f}, Theme::kFieldHeight, 8.f);

    // Titulo e secao "criar".
    title_ = tree_.Create(panelHandle_, labelStyle(Theme::kTitleSize + 10.f));
    sectionNew_ = tree_.Create(panelHandle_, labelStyle(22.f));

    // Corpo: campos a esquerda, logo a direita.
    const auto body = tree_.Create(panelHandle_, row(SU::Length{SU::SizeMode::Percent, 1.f}, kBodyHeight, 16.f));
    SU::FlexItem leftStyle;
    leftStyle.width = {SU::SizeMode::Flex, 1.f};
    leftStyle.grow = 1.f;
    leftStyle.height = {SU::SizeMode::Fixed, kBodyHeight};
    leftStyle.direction = SU::LayoutDirection::Column;
    leftStyle.alignItems = SU::AlignItems::Stretch;
    leftStyle.gap = 6.f;
    const auto left = tree_.Create(body, leftStyle);

    labels_[0] = tree_.Create(left, labelStyle(Theme::kLabelHeight));
    fields_[0] = tree_.Create(left, fieldStyle());
    labels_[1] = tree_.Create(left, labelStyle(Theme::kLabelHeight));
    fields_[1] = tree_.Create(left, fieldStyle());
    labels_[2] = tree_.Create(left, labelStyle(Theme::kLabelHeight));
    const auto locRow = tree_.Create(left, fullRow);
    SU::FlexItem locField;
    locField.width = {SU::SizeMode::Flex, 1.f};
    locField.grow = 1.f;
    locField.height = {SU::SizeMode::Fixed, Theme::kFieldHeight};
    fields_[2] = tree_.Create(locRow, locField);
    browse_ = tree_.Create(locRow, buttonStyle(110.f));
    hint_ = tree_.Create(left, labelStyle(16.f));
    const auto createRow = tree_.Create(left, fullRow);
    create_ = tree_.Create(createRow, buttonStyle(160.f));

    SU::FlexItem logoColumn;
    logoColumn.width = {SU::SizeMode::Fixed, 136.f};
    logoColumn.height = {SU::SizeMode::Fixed, kBodyHeight};
    logoColumn.direction = SU::LayoutDirection::Column;
    logoColumn.alignItems = SU::AlignItems::Stretch;
    const auto logoCol = tree_.Create(body, logoColumn);
    logo_ = tree_.Create(logoCol, SU::FlexItem{{SU::SizeMode::Fixed, Theme::kLogoSize},
                                              {SU::SizeMode::Fixed, Theme::kLogoSize}});

    divider1_ = tree_.Create(panelHandle_, dividerStyle());

    // Secao "abrir".
    sectionOpen_ = tree_.Create(panelHandle_, labelStyle(22.f));
    const auto openRow = tree_.Create(panelHandle_, fullRow);
    SU::FlexItem openField;
    openField.width = {SU::SizeMode::Flex, 1.f};
    openField.grow = 1.f;
    openField.height = {SU::SizeMode::Fixed, Theme::kFieldHeight};
    fields_[3] = tree_.Create(openRow, openField);
    open_ = tree_.Create(openRow, buttonStyle(110.f));

    if (!model_.lastProject().empty()) {
      const auto lastRow = tree_.Create(panelHandle_, fullRow);
      openLast_ = tree_.Create(lastRow, buttonStyle(180.f));
      lastLabel_ = tree_.Create(lastRow, labelStyle(Theme::kFieldHeight));
    }

    error_ = tree_.Create(panelHandle_, labelStyle(18.f));

    // Visuais e interacao: campos e botoes sao alvos de clique. Painel e logo sao so desenho.
    SU::ElementVisual panelVisual;
    panelVisual.tintPremultipliedLinear = colors_.panel;
    panelVisual.interactable = false;
    panelVisual.clipOverflow = true;
    bridge_.SetVisual(panelHandle_, panelVisual);
  }

  // Altura do corpo (campos a esquerda e logo a direita): 8 linhas com 7 espacos de 6 px.
  static constexpr float kBodyHeight = 236.f;
  static constexpr float kGap = 8.f;

  // Altura total: padding, filhos (titulo, secao criar, corpo, divisor, secao abrir, linha abrir,
  // [linha do ultimo projeto], erro) e espacos entre eles.
  float panelHeight() const {
    const float fixed = 30.f + 22.f + kBodyHeight + 1.f + 22.f + Theme::kFieldHeight + 18.f;
    const int children = hasLast_ ? 8 : 7;
    const float lastRow = hasLast_ ? Theme::kFieldHeight : 0.f;
    return 2.f * Theme::kPadding + fixed + lastRow + static_cast<float>(children - 1) * kGap;
  }

  // Filhos de altura fixa nao encolhem (shrink = 0): sem isso o flex os reduz a zero dentro de colunas.
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
  static SU::FlexItem row(SU::Length width, float height, float gap) {
    SU::FlexItem s;
    s.width = width;
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    s.direction = SU::LayoutDirection::Row;
    s.alignItems = SU::AlignItems::Stretch;
    s.gap = gap;
    return s;
  }
  static SU::FlexItem dividerStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, 1.f};
    s.shrink = 0.f;
    return s;
  }

  void setPanelWidth(float width) {
    auto* el = tree_.Get(panelHandle_);
    if (!el) return;
    SU::FlexItem style = el->node.style;
    style.width.value = width;
    tree_.SetStyle(panelHandle_, style);
  }

  SU::Vec2 panelSizeOf() const {
    const auto* el = tree_.Get(panelHandle_);
    return el ? el->node.rect.size : SU::Vec2{};
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

  // ---------- Visuais, foco e clique ----------
  void applyVisuals() {
    const bool hoverCreate = hovered_ == create_;
    const bool hoverOpen = hovered_ == open_;
    const bool hoverBrowse = hovered_ == browse_;
    const bool hoverLast = hasLast_ && hovered_ == openLast_;
    setButton(browse_, hoverBrowse);
    setButton(create_, hoverCreate);
    setButton(open_, hoverOpen);
    if (hasLast_) setButton(openLast_, hoverLast);

    for (std::size_t i = 0; i < kFieldCount; ++i) {
      SU::ElementVisual v;
      v.tintPremultipliedLinear = model_.hasFocus(static_cast<Field>(i)) ? colors_.fieldFocus : colors_.field;
      v.clipOverflow = true;
      v.zOrder = 0.f;
      bridge_.SetVisual(fields_[i], v);
    }
    SU::ElementVisual logoVisual;
    logoVisual.tintPremultipliedLinear = colors_.panel;
    logoVisual.interactable = false;
    bridge_.SetVisual(logo_, logoVisual);
  }

  void setButton(SU::UIHandle handle, bool hover) {
    SU::ElementVisual v;
    v.tintPremultipliedLinear = hover ? colors_.buttonHover : colors_.button;
    v.zOrder = 0.f;
    bridge_.SetVisual(handle, v);
  }

  void handleClick(const SU::Input::UIEvent& event, std::vector<Action>& actions) {
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      if (event.target != fields_[i]) continue;
      const auto field = static_cast<Field>(i);
      model_.Focus(field);
      // event.localPosition vem do canto do campo; o texto comeca apos o recuo e fica centralizado na altura.
      const float lineHeight = fieldText_[i].layout.lineHeight;
      const SU::Rect rect = nodeRect(fields_[i]);
      const float textY = (rect.size.y - lineHeight) * 0.5f;
      const SU::Vec2 local{event.localPosition.x - Theme::kFieldInset, event.localPosition.y - textY};
      const std::uint32_t offset = ST::FindInsertionOffset(fieldText_[i].layout, local);
      model_.SetCaret(offset, false);
      return;
    }
    if (event.target == browse_) actions.push_back(Action::Browse);
    else if (event.target == create_) actions.push_back(Action::Create);
    else if (event.target == open_) actions.push_back(Action::Open);
    else if (hasLast_ && event.target == openLast_) actions.push_back(Action::OpenLast);
  }

  SU::Rect nodeRect(SU::UIHandle handle) const {
    const auto* el = tree_.Get(handle);
    return el ? el->node.rect : SU::Rect{};
  }

  // Itens de desenho extras: texto, selecao, caret, divisores e borda do logo. Usam o no como dono (handle)
  // para o extrator aceitar os itens; zOrder define a ordem sobre os fundos (0) da SpriteUI.
  void addDecorations(std::vector<SR::UIRenderItem>& out) {
    std::uint64_t paint = 1000;
    auto solid = [&](SU::UIHandle owner, SU::Rect r, SU::Vec4 color, float z) {
      SR::UIRenderItem item;
      item.handle = owner;
      item.layout.resolvedPosition = r.position;
      item.layout.resolvedSize = r.size;
      item.tintPremultipliedLinear = color;
      item.textureSlice = SR::WhiteTextureSlice;
      item.zOrder = z;
      item.paintOrder = paint++;
      out.push_back(item);
    };
    auto text = [&](SU::UIHandle owner, const Shaped& shaped, SU::Vec2 origin, SU::Vec4 color, float z) {
      if (shaped.utf8.empty()) return;
      SU::UILayoutRect box;
      box.resolvedPosition = origin;
      box.resolvedSize = {shaped.layout.size.x, shaped.layout.size.y};
      auto items = ST::GenerateTextRenderItems(shaped.layout, box, color, atlasSlice_, SR::NoClipIndex, z,
                                              paint, owner, /*renderFlags=*/0u);
      paint += items.size() + 1;
      out.insert(out.end(), items.begin(), items.end());
    };

    // Divisores e borda do logo.
    solid(divider1_, nodeRect(divider1_), colors_.divider, 0.1f);
    const SU::Rect logoRect = nodeRect(logo_);
    const float b = 1.5f;
    solid(logo_, {logoRect.position, {logoRect.size.x, b}}, colors_.border, 0.1f);
    solid(logo_, {{logoRect.position.x, logoRect.position.y + logoRect.size.y - b}, {logoRect.size.x, b}}, colors_.border, 0.1f);
    solid(logo_, {logoRect.position, {b, logoRect.size.y}}, colors_.border, 0.1f);
    solid(logo_, {{logoRect.position.x + logoRect.size.x - b, logoRect.position.y}, {b, logoRect.size.y}}, colors_.border, 0.1f);

    // Titulos e rotulos.
    text(title_, shape(T("ToolEngine"), Theme::kTitleSize), nodeRect(title_).position, colors_.text, 0.9f);
    text(sectionNew_, shape(T("Create a new project"), Theme::kFontSize), nodeRect(sectionNew_).position, colors_.text, 0.9f);
    text(sectionOpen_, shape(T("Or open an existing project folder"), Theme::kFontSize), nodeRect(sectionOpen_).position, colors_.text, 0.9f);
    text(labels_[0], shape(T("Project name"), Theme::kFontSize), nodeRect(labels_[0]).position, colors_.textDim, 0.9f);
    text(labels_[1], shape(T("Author"), Theme::kFontSize), nodeRect(labels_[1]).position, colors_.textDim, 0.9f);
    text(labels_[2], shape(T("Save in folder"), Theme::kFontSize), nodeRect(labels_[2]).position, colors_.textDim, 0.9f);
    text(hint_, shape(T("The project folder will be created inside this folder."), 13.f), nodeRect(hint_).position, colors_.textDim, 0.9f);

    // Campos: selecao, caret e texto. O texto fica dentro da caixa, com recuo horizontal e centralizado na altura.
    for (std::size_t i = 0; i < kFieldCount; ++i) {
      const auto field = static_cast<Field>(i);
      const SU::UIHandle handle = fields_[i];
      if (!handle.IsValid()) continue;
      const SU::Rect r = nodeRect(handle);
      const Shaped& s = fieldText_[i];
      const float textY = r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f;
      const SU::Vec2 origin{r.position.x + Theme::kFieldInset, textY};
      if (model_.hasFocus(field) && model_.field(field).HasSelection()) {
        const auto [begin, end] = model_.field(field).SelectionRange();
        const SU::Rect a = ST::GetCaretRect(s.layout, static_cast<std::uint32_t>(begin));
        const SU::Rect c = ST::GetCaretRect(s.layout, static_cast<std::uint32_t>(end));
        solid(handle, {{origin.x + a.position.x, origin.y}, {c.position.x - a.position.x, s.layout.lineHeight}},
              colors_.selection, 0.5f);
      }
      if (model_.hasFocus(field)) {
        const SU::Rect caret = ST::GetCaretRect(s.layout, static_cast<std::uint32_t>(model_.field(field).Caret()));
        solid(handle, {{origin.x + caret.position.x, origin.y}, {1.5f, s.layout.lineHeight}}, colors_.caret, 0.6f);
      }
      text(handle, s, origin, colors_.text, 0.9f);
    }

    // Botoes: texto centralizado.
    centerText(browse_, shape(T("Browse..."), Theme::kFontSize), colors_.text, 0.9f, text);
    centerText(create_, shape(T("Create project"), Theme::kFontSize), colors_.text, 0.9f, text);
    centerText(open_, shape(T("Open"), Theme::kFontSize), colors_.text, 0.9f, text);
    if (hasLast_) {
      centerText(openLast_, shape(T("Open last project"), Theme::kFontSize), colors_.text, 0.9f, text);
      text(lastLabel_, shape(model_.lastProject(), 13.f), nodeRect(lastLabel_).position, colors_.textDim, 0.9f);
    }

    if (!model_.error().empty()) {
      text(error_, shape(T(model_.error().c_str()), Theme::kFontSize), nodeRect(error_).position, colors_.error, 0.9f);
    }
  }

  template <typename TextFn>
  void centerText(SU::UIHandle handle, const Shaped& s, SU::Vec4 color, float z, TextFn& text) {
    const SU::Rect r = nodeRect(handle);
    const SU::Vec2 origin{r.position.x + (r.size.x - s.layout.size.x) * 0.5f,
                          r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f};
    text(handle, s, origin, color, z);
  }

  Model& model_;
  const ST::TruetypeAtlas& atlas_;
  Colors colors_;
  static constexpr std::uint32_t atlasSlice_ = 1u;   // textureSlice do atlas de glifos (0 = pixel branco)

  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  SU::Input::UIEventRouter router_;
  SR::RenderExtractor extractor_;

  SU::UIHandle panelHandle_{}, title_{}, sectionNew_{}, sectionOpen_{}, hint_{}, logo_{}, divider1_{};
  SU::UIHandle labels_[3]{}, fields_[kFieldCount]{};
  SU::UIHandle browse_{}, create_{}, open_{}, openLast_{}, lastLabel_{}, error_{};
  bool hasLast_ = false;
  SU::UIHandle hovered_{};
  Shaped fieldText_[kFieldCount]{};
  float lastX_ = 0.f, lastY_ = 0.f;
};

// --------------------------------------------------------------------------------------------------------------
// Ligacao com o SDL_Renderer: converte o FramePacket em SDL_RenderGeometry, um lote por vez, com recorte e atlas.
// --------------------------------------------------------------------------------------------------------------
class SpriteUiSdlRenderer {
 public:
  ~SpriteUiSdlRenderer() { destroy(); }

  // Sobe o atlas de glifos como textura RGBA (cor branca, alfa = cobertura).
  bool uploadAtlas(SDL_Renderer* renderer, const ST::TruetypeAtlas& atlas) {
    destroy();
    if (!renderer || !atlas.IsValid()) return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(atlas.width) * atlas.height * 4u, 255u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(atlas.width) * atlas.height; ++i) {
      rgba[i * 4 + 3] = atlas.pixels[i];
    }
    atlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                               static_cast<int>(atlas.width), static_cast<int>(atlas.height));
    if (!atlas_) return false;
    SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_LINEAR);
    return SDL_UpdateTexture(atlas_, nullptr, rgba.data(), static_cast<int>(atlas.width * 4u));
  }

  bool draw(SDL_Renderer* renderer, const SR::FramePacket& packet) {
    if (!renderer) return false;
    for (const auto& batch : packet.batches) {
      if (!setClip(renderer, packet, batch.clipIndex)) return false;
      vertices_.clear();
      indices_.clear();
      for (std::uint32_t i = 0; i < batch.instanceCount; ++i) {
        const auto& inst = packet.instances[batch.firstInstance + i];
        const SDL_FColor color = sdlColor(inst);
        const bool textured = batch.textureSlice != SR::WhiteTextureSlice;
        const SDL_FPoint p[4] = {transform(inst, 0.f, 0.f), transform(inst, 1.f, 0.f),
                                 transform(inst, 1.f, 1.f), transform(inst, 0.f, 1.f)};
        const SDL_FPoint uv[4] = {uvAt(inst, 0.f, 0.f, textured), uvAt(inst, 1.f, 0.f, textured),
                                  uvAt(inst, 1.f, 1.f, textured), uvAt(inst, 0.f, 1.f, textured)};
        const int base = static_cast<int>(vertices_.size());
        for (int k = 0; k < 4; ++k) vertices_.push_back({p[k], color, uv[k]});
        indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
      }
      if (vertices_.empty()) continue;
      SDL_Texture* texture = batch.textureSlice == SR::WhiteTextureSlice ? nullptr : atlas_;
      if (!SDL_RenderGeometry(renderer, texture, vertices_.data(), static_cast<int>(vertices_.size()),
                              indices_.data(), static_cast<int>(indices_.size()))) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
      }
    }
    return SDL_SetRenderClipRect(renderer, nullptr);
  }

  void destroy() {
    if (atlas_) SDL_DestroyTexture(atlas_);
    atlas_ = nullptr;
  }

 private:
  static bool setClip(SDL_Renderer* renderer, const SR::FramePacket& packet, std::uint32_t clipIndex) {
    if (clipIndex == SR::NoClipIndex || clipIndex >= packet.clipRects.size()) return SDL_SetRenderClipRect(renderer, nullptr);
    const auto& clip = packet.clipRects[clipIndex];
    const int left = static_cast<int>(std::floor(clip.minX));
    const int top = static_cast<int>(std::floor(clip.minY));
    const int right = static_cast<int>(std::ceil(clip.maxX));
    const int bottom = static_cast<int>(std::ceil(clip.maxY));
    const SDL_Rect rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    return SDL_SetRenderClipRect(renderer, &rect);
  }

  static SDL_FPoint transform(const SR::SpriteInstanceGPU& inst, float u, float v) {
    // Retangulo local [0,1]^2 -> tela: linha 0 = (m00, m01, tx), linha 1 = (m10, m11, ty).
    const float lx = u, ly = v;
    return {inst.transformRow0[0] * lx + inst.transformRow0[1] * ly + inst.transformRow0[2],
            inst.transformRow1[0] * lx + inst.transformRow1[1] * ly + inst.transformRow1[2]};
  }

  static SDL_FPoint uvAt(const SR::SpriteInstanceGPU& inst, float u, float v, bool textured) {
    if (!textured) return {0.f, 0.f};
    return {inst.uvRect[0] + u * inst.uvRect[2], inst.uvRect[1] + v * inst.uvRect[3]};
  }

  // Cor do item: tint linear premultiplicada -> sRGB reto (SDL mistura com alfa reto).
  static SDL_FColor sdlColor(const SR::SpriteInstanceGPU& inst) {
    const float a = inst.tint[3];
    if (a <= 0.f) return {0.f, 0.f, 0.f, 0.f};
    return {encode(inst.tint[0] / a), encode(inst.tint[1] / a), encode(inst.tint[2] / a), a};
  }
  static float encode(float linear) {
    linear = std::clamp(linear, 0.f, 1.f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
  }

  SDL_Texture* atlas_ = nullptr;
  std::vector<SDL_Vertex> vertices_;
  std::vector<int> indices_;
};


// Carrega a fonte e monta o atlas de glifos (ASCII, Latin-1 e Latin Extended-A, para o portugues).
inline bool LoadTextAtlas(const std::string& fontPath, ST::TruetypeAtlas& out) {
  std::ifstream file(fontPath, std::ios::binary);
  if (!file) return false;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  ST::TruetypeAtlasConfig cfg;
  cfg.rasterPixelSize = 32.f;
  cfg.atlasWidth = 1024;
  cfg.atlasHeight = 1024;
  cfg.firstCodepoint = 0x20;
  cfg.lastCodepoint = 0x17F;
  return ST::BuildTruetypeAtlas(bytes, cfg, out);
}

// Laco da tela de inicio com SpriteUI. Retorna true quando o projeto foi criado ou aberto (model.done()).
// closed = true quando a janela foi fechada antes disso (nada foi criado).
inline bool RunStartScreen(SDL_Window* window, SDL_Renderer* renderer, Model& model,
                           const ST::TruetypeAtlas& atlas, const std::string& defaultLocation, bool& closed) {
  // Texto digitado chega como SDL_EVENT_TEXT_INPUT so com a entrada de texto ligada. O ImGui liga isso sozinho;
  // este laco nao usa o ImGui, entao liga aqui e desliga ao sair para o editor decidir depois.
  struct TextInputScope {
    SDL_Window* w;
    explicit TextInputScope(SDL_Window* win) : w{win} { SDL_StartTextInput(w); }
    ~TextInputScope() { SDL_StopTextInput(w); }
  } textInput{window};
  SpriteUiSdlRenderer sdl;
  if (!sdl.uploadAtlas(renderer, atlas)) return false;
  model.SetDefaultLocation(defaultLocation);
  View view(model, atlas);
  BrowseSlot browse;
  FrameInput in;
  std::uint64_t frame = 0;

  while (!model.done() && !closed) {
    in.pointerPressed = false;
    in.pointerReleased = false;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      // Modificadores do proprio evento (ev.key.mod): SDL_GetModState() le o estado no momento do poll, e
      // um Ctrl+A rapido ja foi solto nessa hora.
      const SDL_Keymod mod = ev.type == SDL_EVENT_KEY_DOWN ? static_cast<SDL_Keymod>(ev.key.mod) : SDL_KMOD_NONE;
      const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
      const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
      switch (ev.type) {
        case SDL_EVENT_QUIT: closed = true; break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
          if (ev.window.windowID == SDL_GetWindowID(window)) closed = true;
          break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: in.windowFocused = true; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: in.windowFocused = false; break;
        case SDL_EVENT_MOUSE_MOTION:
          in.pointerX = ev.motion.x;
          in.pointerY = ev.motion.y;
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (ev.button.button != SDL_BUTTON_LEFT) break;
          in.pointerX = ev.button.x;
          in.pointerY = ev.button.y;
          in.pointerDown = true;
          in.pointerPressed = true;
          break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (ev.button.button != SDL_BUTTON_LEFT) break;
          in.pointerX = ev.button.x;
          in.pointerY = ev.button.y;
          in.pointerDown = false;
          in.pointerReleased = true;
          break;
        case SDL_EVENT_TEXT_INPUT: model.InsertText(ev.text.text); break;
        case SDL_EVENT_KEY_DOWN: {
          const SDL_Keycode k = ev.key.key;
          if (ctrl && k == SDLK_A) { model.SelectAll(); break; }
          if (ctrl && (k == SDLK_C || k == SDLK_X)) {
            if (k == SDLK_C) model.Copy(); else model.Cut();
            if (!model.clipboardOut().empty()) SDL_SetClipboardText(model.clipboardOut().c_str());
            break;
          }
          if (ctrl && k == SDLK_V) {
            if (char* clip = SDL_GetClipboardText()) { model.Paste(clip); SDL_free(clip); }
            break;
          }
          if (ctrl && k == SDLK_Z) { model.Undo(); break; }
          switch (k) {
            case SDLK_LEFT: model.Press(Key::Left, shift); break;
            case SDLK_RIGHT: model.Press(Key::Right, shift); break;
            case SDLK_HOME: model.Press(Key::Home, shift); break;
            case SDLK_END: model.Press(Key::End, shift); break;
            case SDLK_BACKSPACE: model.Press(Key::Backspace, shift); break;
            case SDLK_DELETE: model.Press(Key::Delete, shift); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: model.Press(Key::Enter, shift); break;
            case SDLK_TAB: model.Press(Key::Tab, shift); break;
            case SDLK_ESCAPE: model.Press(Key::Escape, shift); break;
            default: break;
          }
          break;
        }
        default: break;
      }
    }

    {
      std::lock_guard<std::mutex> lock(browse.mu);
      if (browse.ready) {
        model.SetText(Field::Location, browse.path);
        browse.ready = false;
      }
    }

    int width = 0, height = 0;
    if (!SDL_GetWindowSize(window, &width, &height)) return false;
    std::vector<View::Action> actions;
    in.frameId = ++frame;
    const SR::FramePacket packet = view.Frame(static_cast<float>(width), static_cast<float>(height), in, actions);

    for (const auto action : actions) {
      switch (action) {
        case View::Action::Browse: {
          const std::string loc = model.field(Field::Location).Text();
          SDL_ShowOpenFolderDialog(onFolderPicked, &browse, window, loc.empty() ? nullptr : loc.c_str(), false);
          break;
        }
        case View::Action::Create: model.CreateProject(defaultLocation); break;
        case View::Action::Open: model.OpenPath(); break;
        case View::Action::OpenLast: model.OpenLast(); break;
        case View::Action::None: break;
      }
    }
    if (model.done()) break;

    SDL_SetRenderDrawColor(renderer, 24, 24, 27, 255);
    SDL_RenderClear(renderer);
    if (!sdl.draw(renderer, packet)) return false;
    SDL_RenderPresent(renderer);
  }
  return model.done();
}

} // namespace editor::start
