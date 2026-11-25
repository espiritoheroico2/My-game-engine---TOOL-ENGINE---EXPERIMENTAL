#pragma once

#include "file_panel_colors.hpp"
#include "ui_style.hpp"

// Janela "Scenes" feita com SpriteUI. Estado e regras: scenes_model.hpp.
// Esta view so desenha e reporta cliques e digitacao. Quem salva, abre, apaga, cria, renomeia e atribui assets e o
// host (scenes_panel.hpp e main.cpp, via Action). Moldura: grip, "?" (abre a Help na secao Scenes), titulo em negrito,
// minimizar e X.
#include "window_palette.hpp"
#include "i18n.hpp"
#include "scenes_model.hpp"
#include "spriteui_host.hpp"
#include "window_help_icon.hpp"

#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::scenesui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace SM = editor::scenes_model;
using editor::scenes_model::Model;
using editor::scenes_model::NameMode;

// Medidas. A altura do painel e fixa: o layout nao mede o conteudo.
struct Theme {
  static constexpr float kPanelWidth = 680.f;
  static constexpr float kPadding = 24.f;
  static constexpr float kGap = 6.f;
  static constexpr float kLabelHeight = 18.f;
  static constexpr float kFieldHeight = 30.f;
  static constexpr float kButtonHeight = 34.f;
  static constexpr float kButtonWidth = 150.f;
  static constexpr float kButtonMaxWidth = 220.f;   // teto ao esticar um botao sozinho na linha
  static constexpr float kRowHeight = 26.f;
  static constexpr std::size_t kMaxRows = 6;      // cenas visiveis (o resto rola depois)
  static constexpr std::size_t kMaxAssets = 4;    // assets visiveis
  static constexpr float kRemoveWidth = 28.f;
  static constexpr float kAssetNameWidth = 596.f;
  static constexpr float kFontSize = 12.f;
  static constexpr float kLabelSize = 12.f;
  static constexpr float kTitleSize = 20.f;
  static constexpr float kFieldInset = 8.f;
  static constexpr float kGripWidth = 14.f;   // mesmo valor de UIWindowComponent::gripWidth
  static constexpr float kHelpSize = 18.f;    // diametro do "?"
  static constexpr float kHelpGap = 4.f;      // entre grip, "?" e titulo
  // Filhos do painel: rotulo, 3 linhas de botoes (topo, acoes, confirmar), campo de nome, lista de cenas,
  // rotulo de assets, assets e status. 17 filhos, 16 espacos.
  static constexpr std::size_t kChildren = 1 + 3 + 1 + kMaxRows + 1 + kMaxAssets + 1;
  static constexpr float kPanelHeight =
      2.f * kPadding + 3.f * kLabelHeight + 3.f * kButtonHeight + kFieldHeight +
      static_cast<float>(kMaxRows + kMaxAssets) * kRowHeight + static_cast<float>(kChildren - 1) * kGap;
};

using Colors = editor::file_panel_colors::Colors;
inline Colors PaletteColors() { return editor::file_panel_colors::FromPalette(); }

inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}



// Entrada do quadro (ponteiro e foco da janela), ja lida do SDL pelo host.
struct FrameInput {
  float pointerX = 0.f, pointerY = 0.f;
  bool pointerDown = false;
  bool pointerPressed = false;
  bool pointerReleased = false;
  bool windowFocused = true;
  std::uint64_t frameId = 0;
};

// Texto ja medido, com o layout para caret.
struct Shaped {
  std::string utf8;
  std::vector<ST::GlyphInstance> glyphs;
  ST::TextLayoutResult layout;
  std::uint32_t slice = 1u;   // slice do atlas do tamanho (TextAtlasSet)
};

class View {
 public:
  enum class Action { None, Save, Reload, Open, Delete, Confirm, Assign, RemoveAsset, Help, Close };

  View(Model& model, const sui::TextAtlasSet& text) : model_{model}, text_{text} { buildTree(); }

  // Calcula um quadro e devolve o pacote para desenhar. `actions` recebe o que foi clicado neste quadro.
  SR::FramePacket Frame(float windowWidth, float windowHeight, const FrameInput& in, std::vector<Action>& actions) {
    metrics_.cornerRadius = editor::ui_style::windowRadius();
    actions.clear();
    removed_ = kNone;
    if (windowWidth <= 0.f || windowHeight <= 0.f) return {};

    const float panelWidth = dock_ ? dock_->size.x : std::min(Theme::kPanelWidth, windowWidth - 32.f);
    const float frameHeight = frameHeightNow();
    if (dock_) {   // acoplada: a area vem do dock; o arraste nao a move
      windowPos_ = dock_->position;
      dragging_ = false;
      placed_ = true;
    } else if (docked_) {   // saiu do dock: continua no lugar em que estava
      windowPos_ = frame_.position;
      placed_ = true;
    }
    docked_ = dock_.has_value();
    if (!placed_) {   // primeira vez: centralizada
      windowPos_ = {std::floor((windowWidth - panelWidth) * 0.5f),
                    std::max(0.f, std::floor((windowHeight - Theme::kPanelHeight - metrics_.headerHeight) * 0.5f))};
      placed_ = true;
    }
    if (!docked_) {
      windowPos_.x = std::clamp(windowPos_.x, 0.f, std::max(0.f, windowWidth - panelWidth));
      windowPos_.y = std::clamp(windowPos_.y, 0.f, std::max(0.f, windowHeight - frameHeight));
    }
    frame_ = SU::Rect{windowPos_, {panelWidth, docked_ ? dock_->size.y : frameHeight}};

    handleFrameInput(in, actions);
    frame_ = SU::Rect{windowPos_, {panelWidth, docked_ ? (minimized_ ? metrics_.headerHeight : dock_->size.y) : frameHeightNow()}};

    // Minimizada: so a moldura (cabecalho). O conteudo nao aparece nem recebe cliques.
    if (minimized_) {
      std::vector<SR::UIRenderItem> items;
      std::uint64_t paint = 1000;
      addFrame(items, paint);
      return extractor_.Build(in.frameId, items, nullptr, {});
    }

    const SU::Rect content = SW::WindowFrameContent(frame_, metrics_);
    content_ = content;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, naturalH_ - content.size.y));
    setPanelSize(content.size.x);   // altura natural: as linhas de botoes quebram conforme a largura; o excesso rola
    tree_.Layout({{content.position.x, content.position.y - scroll_}, content.size});
    naturalH_ = rectOf(panelHandle_).size.y;

    rows_ = SM::Rows(model_.scenes, model_.current);
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
        item.flags = visual->flags;
        item.cornerRadius = visual->cornerRadius;
      }
    }

    if (!scene.clipHierarchy.valid) return {};
    std::vector<SR::UIRenderItem> extra;
    std::vector<SR::ClipRectGPU> clips = scene.clipHierarchy.effectiveRects;
    // Recorte pela moldura: conteudo mais largo que a janela (acoplada em folha estreita) nao vaza para fora dela.
    const SU::Rect framed = content_;
    clips.push_back({framed.position.x, framed.position.y, framed.position.x + framed.size.x, framed.position.y + framed.size.y});
    const auto frameClip = static_cast<std::uint32_t>(clips.size() - 1);
    for (auto& item : scene.renderItems) {
      if (item.clipIndex == SR::NoClipIndex) item.clipIndex = frameClip;
    }
    std::uint64_t paint = 1000;
    addFrame(extra, paint);
    addContent(extra, paint);
    scene.renderItems.insert(scene.renderItems.end(), extra.begin(), extra.end());
    return extractor_.Build(in.frameId, scene.renderItems, nullptr, clips);
  }

  // Digitacao: so entra quando o campo de nome esta focado (criar ou renomear).
  void TypeText(const std::string& utf8) {
    if (!model_.nameFocus || model_.mode == NameMode::None) return;
    model_.name += utf8;
  }
  void Backspace() {
    if (!model_.nameFocus || model_.mode == NameMode::None) return;
    SM::PopUtf8(model_.name);
  }

  // Estado para testes e para o host.
  [[nodiscard]] std::size_t removedIndex() const { return removed_; }
  [[nodiscard]] SU::Rect frameRect() const { return frame_; }
  [[nodiscard]] SU::Vec2 windowPos() const { return windowPos_; }
  // Area dada pelo dock (folha, sem a faixa de abas). nullopt: a janela flutua.
  void setDock(std::optional<SU::Rect> dock) { dock_ = dock; }
  [[nodiscard]] bool docked() const { return docked_; }
  [[nodiscard]] SU::Rect gripRect() const { return SW::WindowGripRect(SW::WindowFrameHeader(frame_, metrics_, docked_), Theme::kGripWidth); }
  // Sai do dock na posicao `at`: a janela segue o ponteiro (o arraste da moldura volta a valer).
  void floatAt(SU::Vec2 at) {
    dock_.reset();
    windowPos_ = at;
    placed_ = true;
    docked_ = false;
  }
  [[nodiscard]] bool dragging() const { return dragging_; }
  [[nodiscard]] bool minimized() const { return minimized_; }
  [[nodiscard]] const SW::WindowFrameMetrics& frameMetrics() const { return metrics_; }
  [[nodiscard]] SU::UIHandle rowHandle(std::size_t i) const { return rows_h_[i]; }
  [[nodiscard]] SU::UIHandle assetRemoveHandle(std::size_t i) const { return assetRemove_[i]; }
  [[nodiscard]] SU::UIHandle saveHandle() const { return save_; }
  [[nodiscard]] SU::UIHandle newHandle() const { return new_; }
  [[nodiscard]] SU::UIHandle reloadHandle() const { return reload_; }
  [[nodiscard]] SU::UIHandle openHandle() const { return open_; }
  [[nodiscard]] SU::UIHandle renameHandle() const { return rename_; }
  [[nodiscard]] SU::UIHandle deleteHandle() const { return delete_; }
  [[nodiscard]] SU::UIHandle assignHandle() const { return assign_; }
  [[nodiscard]] SU::UIHandle nameHandle() const { return nameField_; }
  [[nodiscard]] SU::UIHandle confirmHandle() const { return confirm_; }
  [[nodiscard]] SU::UIHandle cancelHandle() const { return cancel_; }
  [[nodiscard]] SU::Rect helpRect() const { return helpRectNow(); }
  [[nodiscard]] SU::Rect rectOf(SU::UIHandle handle) const { return nodeRect(handle); }

 private:
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  // Roda do mouse: rola o painel quando o conteudo passa da janela.
 public:
  void scrollBy(float dy) { scroll_ = std::clamp(scroll_ + dy, 0.f, std::max(0.f, naturalH_ - content_.size.y)); }

 private:
  static bool inside(SU::Rect r, SU::Vec2 p) {
    return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y &&
           p.y < r.position.y + r.size.y;
  }

  float frameHeightNow() const {
    return minimized_ ? metrics_.headerHeight + 2.f * metrics_.borderWidth
                      : Theme::kPanelHeight + metrics_.headerHeight + 2.f * metrics_.borderWidth;
  }

  // "?" logo depois do grip; o titulo comeca depois dele.
  SU::Rect helpRectNow() const {
    const SU::Rect bar = SW::WindowFrameHeader(frame_, metrics_, docked_);
    const SU::Rect grip = SW::WindowGripRect(bar, Theme::kGripWidth);
    return SU::Rect{{grip.position.x + grip.size.x + Theme::kHelpGap, bar.position.y + (bar.size.y - Theme::kHelpSize) * 0.5f},
                    {Theme::kHelpSize, Theme::kHelpSize}};
  }

  // Moldura: X fecha; minimizar recolhe ao cabecalho; "?" pede a Help; so o grip arrasta a janela.
  void handleFrameInput(const FrameInput& in, std::vector<Action>& actions) {
    const SU::Vec2 p{in.pointerX, in.pointerY};
    if (dragging_) {
      if (in.pointerDown) {
        windowPos_ = {p.x - grab_.x, p.y - grab_.y};
      } else {
        dragging_ = false;
      }
      return;
    }
    if (!in.pointerPressed) return;
    const SW::WindowFramePart part = SW::WindowFramePartAt(frame_, metrics_, false, p);
    if (part == SW::WindowFramePart::Close) {
      actions.push_back(Action::Close);
      return;
    }
    if (part == SW::WindowFramePart::Minimize) {
      minimized_ = !minimized_;
      return;
    }
    if (inside(helpRectNow(), p)) {
      actions.push_back(Action::Help);
      return;
    }
    const SU::Rect bar = SW::WindowFrameHeader(frame_, metrics_, docked_);
    if (!docked_ && inside(SW::WindowGripRect(bar, Theme::kGripWidth), p)) {
      dragging_ = true;
      grab_ = {p.x - windowPos_.x, p.y - windowPos_.y};
    }
  }

  // Cores do tema para a moldura, o conteudo e os botoes. Chamada na montagem e em cada desenho: a paleta
  // escolhida em Configuracoes > Appearance vale no quadro seguinte.
  void applyPalette() {
    frameColors_ = editor::window_palette::Standard();
    panelColor_ = frameColors_.body;
    colors_ = PaletteColors();
  }

  // ---------- Arvore de layout ----------
  void buildTree() {
    metrics_.headerHeight = 30.f;
    metrics_.cornerRadius = editor::ui_style::windowRadius();
    applyPalette();   // a arvore usa as cores do tema ja aqui (antes, o conteudo saia com a cor padrao: cinza)

    SU::FlexItem panel;
    panel.width = {SU::SizeMode::Fixed, Theme::kPanelWidth};
    panel.height = {SU::SizeMode::Fixed, Theme::kPanelHeight};
    panel.direction = SU::LayoutDirection::Column;
    panel.alignItems = SU::AlignItems::Stretch;
    panel.padding = {Theme::kPadding, Theme::kPadding, Theme::kPadding, Theme::kPadding};
    panel.gap = Theme::kGap;
    panelHandle_ = tree_.Create({}, panel);

    label_ = tree_.Create(panelHandle_, labelStyle(Theme::kLabelHeight));
    const auto topRow = tree_.Create(panelHandle_, wrapRowStyle());
    save_ = tree_.Create(topRow, buttonStyle(Theme::kButtonWidth));
    new_ = tree_.Create(topRow, buttonStyle(Theme::kButtonWidth));
    reload_ = tree_.Create(topRow, buttonStyle(Theme::kButtonWidth));
    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) rows_h_[i] = tree_.Create(panelHandle_, labelStyle(Theme::kRowHeight));

    const auto actionRow = tree_.Create(panelHandle_, wrapRowStyle());
    open_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    rename_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    delete_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    assign_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));

    nameField_ = tree_.Create(panelHandle_, labelStyle(Theme::kFieldHeight));
    const auto confirmRow = tree_.Create(panelHandle_, wrapRowStyle());
    confirm_ = tree_.Create(confirmRow, buttonStyle(Theme::kButtonWidth));
    cancel_ = tree_.Create(confirmRow, buttonStyle(Theme::kButtonWidth));

    assetsLabel_ = tree_.Create(panelHandle_, labelStyle(Theme::kLabelHeight));
    for (std::size_t i = 0; i < Theme::kMaxAssets; ++i) {
      assetRow_[i] = tree_.Create(panelHandle_, rowStyle(Theme::kRowHeight));
      assetName_[i] = tree_.Create(assetRow_[i], assetNameStyle());
      assetRemove_[i] = tree_.Create(assetRow_[i], assetRemoveStyle());
    }
    status_ = tree_.Create(panelHandle_, labelStyle(Theme::kLabelHeight));

    SU::ElementVisual panelVisual;
    panelVisual.tintPremultipliedLinear = panelColor_;
    panelVisual.interactable = false;
    panelVisual.clipOverflow = false;
    panelVisual.zOrder = -1.f;   // fundo do painel atras dos elementos (zOrder 0)
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
  static SU::FlexItem buttonStyle(float width) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, width};
    s.height = {SU::SizeMode::Fixed, Theme::kButtonHeight};
    s.grow = 1.f;                                   // enche a linha quando sobra espaco (uma linha, um botao)
    s.maxSize = {Theme::kButtonMaxWidth, std::numeric_limits<float>::infinity()};
    s.shrink = 0.f;
    return s;
  }
  static SU::FlexItem assetNameStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, 0.f};           // ocupa o espaco que sobra ao lado do botao de remover
    s.grow = 1.f;
    s.height = {SU::SizeMode::Fixed, Theme::kRowHeight};
    s.shrink = 0.f;
    return s;
  }
  static SU::FlexItem assetRemoveStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, Theme::kRemoveWidth};
    s.height = {SU::SizeMode::Fixed, Theme::kRowHeight};
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

  // Linha de botoes que quebra linha quando a janela estreita (cada botao vai para a linha de baixo).
  static SU::FlexItem wrapRowStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Auto, 0.f};
    s.shrink = 0.f;
    s.direction = SU::LayoutDirection::Row;
    s.alignItems = SU::AlignItems::Start;
    s.wrap = SU::FlexWrap::Wrap;
    s.gap = Theme::kGap;
    return s;
  }

  void setPanelSize(float width) {
    auto* el = tree_.Get(panelHandle_);
    if (!el) return;
    SU::FlexItem style = el->node.style;
    style.width.value = width;
    style.height = {SU::SizeMode::Auto, 0.f};
    tree_.SetStyle(panelHandle_, style);
  }

  // ---------- Texto ----------
  Shaped shape(const std::string& utf8, float size) const {
    Shaped s;
    s.utf8 = utf8;
    size = editor::ui_style::uiFont(size);
    const ST::TruetypeAtlas& atlas = text_.atlasFor(size);
    s.slice = text_.sliceFor(size);
    s.glyphs = ST::ShapeTruetype(utf8, atlas);
    ST::TextLayoutConfig cfg;
    cfg.fontSize = size;
    cfg.alignment = ST::TextAlignment::Left;
    cfg.wrap = ST::TextWrap::None;
    s.layout = ST::LayoutText(utf8, s.glyphs, atlas.metrics, cfg);
    return s;
  }

  // ---------- Visuais, clique e foco ----------
  // Botao: cor normal, de hover ou apagada; so clica quando habilitado (interactable).
  void paintButton(SU::UIHandle h, bool enabled, SU::Vec4 base) {
    SU::ElementVisual v;
    v.interactable = enabled;
    v.clipOverflow = false;
    v.flags = SR::FlagRoundedPanel;
    v.cornerRadius = editor::ui_style::buttonRadius();
    v.zOrder = 0.f;
    v.tintPremultipliedLinear = !enabled ? colors_.disabled : (hovered_ == h ? colors_.buttonHover : base);
    bridge_.SetVisual(h, v);
    enabled_.push_back({h, enabled});
  }

  bool isEnabled(SU::UIHandle h) const {
    for (const auto& [handle, on] : enabled_) {
      if (handle == h) return on;
    }
    return false;
  }

  void applyVisuals() {
    enabled_.clear();
    const SM::Actions acts = SM::ActionsFor(model_.selected, model_.current);
    const bool confirming = model_.mode != NameMode::None;
    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
      const bool has = i < rows_.size();
      SU::ElementVisual v;
      v.interactable = has;
      v.clipOverflow = false;
      v.zOrder = 0.f;
      v.tintPremultipliedLinear = has && rows_[i].name == model_.selected ? colors_.rowSelected : colors_.field;
      bridge_.SetVisual(rows_h_[i], v);
    }
    paintButton(save_, true, colors_.button);
    paintButton(new_, !confirming, colors_.button);
    paintButton(reload_, true, colors_.button);
    paintButton(open_, acts.open && !confirming, colors_.button);
    paintButton(rename_, acts.rename && !confirming, colors_.button);
    paintButton(delete_, acts.remove && !confirming, colors_.buttonDanger);
    paintButton(assign_, !confirming, colors_.button);
    paintButton(confirm_, confirming, model_.mode == NameMode::Delete ? colors_.buttonDanger : colors_.button);
    paintButton(cancel_, confirming, colors_.button);

    SU::ElementVisual field;
    field.tintPremultipliedLinear = model_.nameFocus ? colors_.fieldFocus : colors_.field;
    field.clipOverflow = false;
    field.zOrder = 0.f;
    bridge_.SetVisual(nameField_, field);

    for (std::size_t i = 0; i < Theme::kMaxAssets; ++i) {
      SU::ElementVisual name;   // nome do asset: so texto, nao clica
      name.interactable = false;
      name.clipOverflow = false;
      name.zOrder = 0.f;
      name.tintPremultipliedLinear = panelColor_;
      bridge_.SetVisual(assetName_[i], name);
      paintButton(assetRemove_[i], i < model_.assets.size(), colors_.buttonDanger);
    }
  }

  void handleClick(const SU::Input::UIEvent& event, std::vector<Action>& actions) {
    if (event.target == nameField_) {   // so foca quando ha criar ou renomear em curso
      model_.nameFocus = model_.mode == NameMode::New || model_.mode == NameMode::Rename;
      return;
    }
    model_.nameFocus = false;   // qualquer outro clique tira o foco do campo

    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
      if (event.target != rows_h_[i]) continue;
      if (i < rows_.size()) model_.selected = rows_[i].name;
      return;
    }
    if (event.target == save_) {
      actions.push_back(Action::Save);
      return;
    }
    if (event.target == reload_) {
      actions.push_back(Action::Reload);
      return;
    }
    if (event.target == assign_) {
      if (model_.assignCandidate.empty()) {
        model_.status = "Select a model or sound in Assets to assign it here.";
      } else {
        actions.push_back(Action::Assign);
      }
      return;
    }
    if (event.target == new_) {
      SM::BeginName(model_, NameMode::New);
      return;
    }
    if (event.target == cancel_) {
      SM::FinishName(model_);
      return;
    }
    if (event.target == confirm_) {
      confirm(actions);
      return;
    }

    if (model_.mode != NameMode::None) return;   // com uma confirmacao em curso, so Confirmar e Cancelar agem
    const SM::Actions acts = SM::ActionsFor(model_.selected, model_.current);
    if (event.target == open_ && acts.open) {
      actions.push_back(Action::Open);
      return;
    }
    if (event.target == rename_ && acts.rename) {
      SM::BeginName(model_, NameMode::Rename);
      return;
    }
    if (event.target == delete_ && acts.remove) {   // apagar pede confirmacao antes
      SM::BeginDelete(model_);
      return;
    }

    for (std::size_t i = 0; i < Theme::kMaxAssets; ++i) {
      if (event.target != assetRemove_[i]) continue;
      if (i < model_.assets.size()) {
        removed_ = i;
        actions.push_back(Action::RemoveAsset);
      }
      return;
    }
  }

  // Confirmar: apagar vai direto ao host; criar ou renomear so se o nome passa nas regras (renomear ignora o
  // proprio nome da selecao).
  void confirm(std::vector<Action>& actions) {
    if (model_.mode == NameMode::None) return;
    if (model_.mode == NameMode::Delete) {
      actions.push_back(Action::Delete);
      return;
    }
    std::vector<std::string> taken;
    for (const std::string& s : model_.scenes) {
      if (model_.mode == NameMode::Rename && s == model_.selected) continue;
      taken.push_back(s);
    }
    const std::string problem = SM::NameProblem(model_.name, taken);
    if (!problem.empty()) {
      model_.status = problem;
      return;
    }
    model_.status.clear();
    actions.push_back(Action::Confirm);
  }

  SU::Rect nodeRect(SU::UIHandle handle) const {
    const auto* el = tree_.Get(handle);
    return el ? el->node.rect : SU::Rect{};
  }

  // Moldura da SpriteUI (corpo, aba, minimizar e X), grip, "?" e titulo. Por tras dos elementos (zOrder -2).
  void addFrame(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint) {
    auto solid = [&](SU::UIHandle owner, SU::Rect r, SU::Vec4 color, float z) {
      SR::UIRenderItem item;
      item.handle = owner;
      item.layout.resolvedPosition = r.position;
      item.layout.resolvedSize = r.size;
      item.tintPremultipliedLinear = color;
      item.textureSlice = SR::WhiteTextureSlice;
      item.clipIndex = SR::NoClipIndex;
      item.zOrder = z;
      item.paintOrder = paint++;
      out.push_back(item);
    };
    auto text = [&](SU::UIHandle owner, const Shaped& shaped, SU::Vec2 origin, SU::Vec4 color, float z) {
      if (shaped.utf8.empty()) return;
      SU::UILayoutRect box;
      box.resolvedPosition = origin;
      box.resolvedSize = {shaped.layout.size.x, shaped.layout.size.y};
      auto items = ST::GenerateTextRenderItems(shaped.layout, box, color, shaped.slice, SR::NoClipIndex, z, paint, owner, 0u);
      paint += items.size() + 1;
      out.insert(out.end(), items.begin(), items.end());
    };

    SW::WindowFrameState state;
    state.active = true;
    state.dragging = dragging_;
    state.docked = docked_;
    applyPalette();
    auto frameItems = SW::BuildWindowFrameItems(panelHandle_, frame_, metrics_, state, frameColors_, -2.f, paint);
    paint += frameItems.size() + 1;
    out.insert(out.end(), frameItems.begin(), frameItems.end());

    // Grip (pontos).
    const SU::Rect bar = SW::WindowFrameHeader(frame_, metrics_, docked_);
    const SU::Rect grip = SW::WindowGripRect(bar, Theme::kGripWidth);
    const float cx = grip.position.x + grip.size.x * 0.5f, cy = grip.position.y + grip.size.y * 0.5f;
    for (int c = 0; c < 2; ++c) {
      for (int r = 0; r < 3; ++r) {
        solid(panelHandle_, {{cx - 3.f + 4.f * static_cast<float>(c), cy - 4.f + 4.f * static_cast<float>(r)}, {2.f, 2.f}},
              colors_.textDim, 0.95f);
      }
    }

    // "?": circulo com a interrogacao, depois do grip.
    const SU::Rect help = helpRectNow();
    const auto circle = editor::window_help_icon::BuildCircleItems(panelHandle_, help, colors_.help, -1.9f, paint);
    out.insert(out.end(), circle.begin(), circle.end());
    const Shaped q = shape("?", Theme::kLabelSize);
    text(panelHandle_, q, {help.position.x + (help.size.x - q.layout.size.x) * 0.5f,
                           help.position.y + (help.size.y - q.layout.lineHeight) * 0.5f}, colors_.text, 0.95f);

    // Titulo em negrito (duas passadas de 0,7 px, como o Profiler). Comeca depois do "?".
    const Shaped title = shape(T("Scenes"), Theme::kTitleSize);
    const float tx = help.position.x + help.size.x + Theme::kHelpGap;
    const float ty = bar.position.y + (bar.size.y - title.layout.lineHeight) * 0.5f;
    text(panelHandle_, title, {tx, ty}, colors_.text, 0.95f);
    text(panelHandle_, title, {tx + 0.7f, ty}, colors_.text, 0.95f);
  }

  // Conteudo: textos, botoes, campo de nome e assets. Usa o no de cada elemento como dono.
  void addContent(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint) {
    auto solid = [&](SU::UIHandle owner, SU::Rect r, SU::Vec4 color, float z) {
      SR::UIRenderItem item;
      item.handle = owner;
      item.layout.resolvedPosition = r.position;
      item.layout.resolvedSize = r.size;
      item.tintPremultipliedLinear = color;
      item.textureSlice = SR::WhiteTextureSlice;
      item.clipIndex = SR::NoClipIndex;
      item.zOrder = z;
      item.paintOrder = paint++;
      out.push_back(item);
    };
    auto text = [&](SU::UIHandle owner, const Shaped& shaped, SU::Vec2 origin, SU::Vec4 color, float z) {
      if (shaped.utf8.empty()) return;
      SU::UILayoutRect box;
      box.resolvedPosition = origin;
      box.resolvedSize = {shaped.layout.size.x, shaped.layout.size.y};
      auto items = ST::GenerateTextRenderItems(shaped.layout, box, color, shaped.slice, SR::NoClipIndex, z, paint, owner, 0u);
      paint += items.size() + 1;
      out.insert(out.end(), items.begin(), items.end());
    };
    // Texto centralizado no botao; apagado quando o botao esta desabilitado.
    auto button = [&](SU::UIHandle h, const std::string& label) {
      const Shaped s = shape(label, Theme::kFontSize);
      const SU::Rect r = nodeRect(h);
      text(h, s, {r.position.x + (r.size.x - s.layout.size.x) * 0.5f, r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f},
           isEnabled(h) ? colors_.text : colors_.textDim, 0.9f);
    };
    // Texto alinhado a esquerda, centrado na altura da linha.
    auto line = [&](SU::UIHandle h, const std::string& label, SU::Vec4 color, float size = Theme::kLabelSize) {
      const Shaped s = shape(label, size);
      const SU::Rect r = nodeRect(h);
      text(h, s, {r.position.x + Theme::kFieldInset, r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f}, color, 0.9f);
    };

    line(label_, T("Current scene: ") + (model_.current.empty() ? T("(none)") : model_.current), colors_.textDim);

    button(save_, T("Save scene"));
    button(new_, T("New scene..."));
    button(reload_, T("Reload list"));
    button(open_, T("Open"));
    button(rename_, T("Rename..."));
    button(delete_, T("Delete"));
    button(assign_, T("Assign asset"));
    button(confirm_, model_.mode == NameMode::New      ? T("Create scene")
                     : model_.mode == NameMode::Rename ? T("Rename scene")
                     : model_.mode == NameMode::Delete ? T("Delete scene")
                                                       : T("Confirm"));
    button(cancel_, T("Cancel"));

    // Lista de cenas: nome e "(open)" na cena atual; a selecionada fica destacada.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
      line(rows_h_[i], rows_[i].current ? rows_[i].name + T(" (open)") : rows_[i].name, colors_.text, Theme::kLabelSize + 1.f);
    }

    // Campo de nome: placeholder quando vazio, caret quando focado. Em apagar, mostra a pergunta (sem edicao).
    const SU::Rect fr = nodeRect(nameField_);
    if (model_.mode == NameMode::Delete) {
      const Shaped q = shape(T("Delete '") + model_.selected + T("'? This cannot be undone."), Theme::kFontSize);
      text(nameField_, q, {fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - q.layout.lineHeight) * 0.5f},
           colors_.warning, 0.9f);
    } else {
      const bool placeholder = model_.name.empty();
      const std::string shown = placeholder ? (model_.mode == NameMode::None ? T("Click New scene... or Rename...") : T("Scene name"))
                                            : model_.name;
      const Shaped fs = shape(shown, Theme::kFontSize);
      const SU::Vec2 origin{fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - fs.layout.lineHeight) * 0.5f};
      if (model_.nameFocus) {
        const std::uint32_t offset = placeholder ? 0u : static_cast<std::uint32_t>(model_.name.size());
        const SU::Rect caret = ST::GetCaretRect(fs.layout, offset);
        solid(nameField_, {{origin.x + caret.position.x, origin.y}, {1.5f, fs.layout.lineHeight}}, colors_.caret, 0.6f);
      }
      text(nameField_, fs, origin, placeholder ? colors_.textDim : colors_.text, 0.9f);
    }

    // Assets: nome, "(missing)" se o arquivo nao existe, e o "x" para tirar.
    line(assetsLabel_, T("Assets in this scene") + (model_.assets.empty() ? T(" (none)") : std::string{}), colors_.textDim);
    for (std::size_t i = 0; i < model_.assets.size(); ++i) {
      const SU::Rect r = nodeRect(assetName_[i]);
      const Shaped name = shape(model_.assets[i], Theme::kLabelSize);
      text(assetName_[i], name, {r.position.x + Theme::kFieldInset, r.position.y + (r.size.y - name.layout.lineHeight) * 0.5f},
           colors_.text, 0.9f);
      if (i < model_.assetMissing.size() && model_.assetMissing[i]) {
        const Shaped miss = shape(T("(missing)"), Theme::kLabelSize);
        text(assetName_[i], miss,
             {r.position.x + Theme::kFieldInset + name.layout.size.x + 10.f, r.position.y + (r.size.y - miss.layout.lineHeight) * 0.5f},
             colors_.warning, 0.9f);
      }
      button(assetRemove_[i], "x");
    }

    if (!model_.status.empty()) line(status_, model_.status, colors_.warning);
  }

  Model& model_;
  const sui::TextAtlasSet& text_;
  Colors colors_;
  SU::Vec4 panelColor_{};

  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  SU::Input::UIEventRouter router_;
  SR::RenderExtractor extractor_;

  SU::UIHandle panelHandle_{}, label_{}, save_{}, new_{}, reload_{}, open_{}, rename_{}, delete_{}, assign_{};
  SU::Rect content_{};        // area visivel do conteudo (abaixo do cabecalho)
  float scroll_ = 0.f;        // deslocamento vertical do painel (roda do mouse)
  float naturalH_ = 0.f;      // altura natural do painel no ultimo quadro
  SU::UIHandle nameField_{}, confirm_{}, cancel_{}, assetsLabel_{}, status_{};
  SU::UIHandle rows_h_[Theme::kMaxRows]{};
  SU::UIHandle assetRow_[Theme::kMaxAssets]{}, assetName_[Theme::kMaxAssets]{}, assetRemove_[Theme::kMaxAssets]{};
  SU::UIHandle hovered_{};
  std::vector<SM::Row> rows_;
  std::vector<std::pair<SU::UIHandle, bool>> enabled_;
  std::size_t removed_ = kNone;
  float lastX_ = 0.f, lastY_ = 0.f;

  // Moldura: geometria e cores (SpriteUI). Definidas em buildTree.
  SW::WindowFrameMetrics metrics_{};
  SW::WindowFrameColors frameColors_{};
  SU::Rect frame_{};
  SU::Vec2 windowPos_{};
  std::optional<SU::Rect> dock_;
  bool docked_ = false;
  SU::Vec2 grab_{};
  bool placed_ = false;
  bool dragging_ = false;
  bool minimized_ = false;
};

}  // namespace editor::scenesui
