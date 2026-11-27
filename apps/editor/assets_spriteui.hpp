#pragma once

#include "ui_scrollbar.hpp"
#include "file_panel_colors.hpp"
#include "ui_style.hpp"

// Janela "Assets" feita com SpriteUI. Estado e regras: assets_model.hpp.
// Esta view so desenha e reporta cliques, digitacao e rolagem. Quem lista a pasta, cria, renomeia, apaga, importa,
// abre e salva e o host (assets_sprite_panel.hpp e main.cpp, via Action). Moldura: grip, "?" (abre a Help na secao
// Assets), titulo em negrito, minimizar e X.
//
// Limites desta primeira versao: o editor de texto so digita no fim do arquivo (sem mover o caret); os menus de
// contexto sao botoes na barra de acoes.
#include "window_palette.hpp"
#include "assets_model.hpp"
#include "assets_tips.hpp"
#include "i18n.hpp"
#include "spriteui_host.hpp"
#include "window_frame.hpp"
#include "window_help_icon.hpp"

#include "SpriteUi/Input/InputRouter.hpp"
#include "SpriteUi/Layout/LayoutBridge.hpp"
#include "SpriteUi/Layout/LayoutTree.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Widgets/DockInteraction.hpp"
#include "SpriteUi/Text/TextLayout.hpp"
#include "SpriteUi/Text/TruetypeAtlas.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace editor::assetsui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;
namespace ST = ToolEngine::SpriteUi::Text;
namespace SW = ToolEngine::SpriteUi::Widgets;
namespace AM = editor::assets_model;

// Medidas. A altura do painel e fixa: o layout nao mede o conteudo.
// Medidas fixas da janela Assets (margens, alturas, tamanhos de icone e de fonte, em pixels).
struct Theme {
  static constexpr float kPanelWidth = 760.f;
  static constexpr float kPadding = 16.f;
  static constexpr float kGap = 6.f;
  static constexpr float kRowHeight = 30.f;        // barras de botoes (migalhas, ferramentas, acoes)
  static constexpr float kFieldHeight = 28.f;      // filtro e campo de nome
  static constexpr float kStatusHeight = 20.f;
  static constexpr float kButtonWidth = 140.f;     // botoes da barra de acoes (5 cabem em 728 px)
  static constexpr float kToolWidth = 118.f;
  static constexpr float kViewIconWidth = 30.f;    // botoes quadrados de lista e icones (so desenho)
  static constexpr float kUpWidth = 40.f;
  static constexpr float kCrumbWidth = 150.f;
  static constexpr std::size_t kMaxCrumbs = 4;     // migalhas visiveis (as ultimas do caminho)
  static constexpr float kListRowHeight = 26.f;
  static constexpr float kIconRowHeight = 96.f;
  static constexpr float kGridHeight = static_cast<float>(AM::kIconRows) * kIconRowHeight;   // 384
  static constexpr float kIconSize = 56.f;
  static constexpr float kTextLineHeight = 20.f;
  static constexpr int kTextLines = static_cast<int>(kGridHeight / kTextLineHeight);
  static constexpr float kFontSize = 12.f;
  static constexpr float kLabelSize = 12.f;
  static constexpr float kTitleSize = 12.f;   // titulo da moldura: mesmo tamanho das outras janelas
  static constexpr float kFieldInset = 8.f;
  static constexpr float kGripWidth = 14.f;   // mesmo valor de UIWindowComponent::gripWidth
  static constexpr float kHelpSize = 18.f;    // diametro do "?"
  static constexpr float kHelpGap = 4.f;      // entre grip, "?" e titulo
  // Barras: crumbs, ferramentas e acoes (3 linhas de botoes); filtro e nome (2 campos); grade; status.
  static constexpr float kPanelHeight = 2.f * kPadding + 3.f * kRowHeight + 2.f * kFieldHeight + kGridHeight +
                                        kStatusHeight + 6.f * kGap;
  static constexpr double kDoubleClickSeconds = 0.4;
};

using Colors = editor::file_panel_colors::Colors;
inline Colors PaletteColors() { return editor::file_panel_colors::FromPalette(); }

// Cor sRGB comum -> linear pre-multiplicado do renderizador SpriteUI.
inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}


// Entrada do quadro (ponteiro, foco e relogio), ja lida do SDL pelo host.
// Entrada do quadro (ponteiro, duplo clique e foco da janela), ja lida do SDL pelo host.
struct FrameInput {
  float pointerX = 0.f, pointerY = 0.f;
  bool pointerDown = false;
  bool pointerPressed = false;
  bool pointerReleased = false;
  bool windowFocused = true;
  double timeSeconds = 0.0;   // relogio para o duplo clique
  std::uint64_t frameId = 0;
};

// Texto ja medido, com o layout para caret.
// Texto ja medido (glifos e layout) com a fatia do atlas do tamanho usado.
struct Shaped {
  std::string utf8;
  std::vector<ST::GlyphInstance> glyphs;
  ST::TextLayoutResult layout;
  std::uint32_t slice = 1u;   // slice do atlas do tamanho (TextAtlasSet)
};

// Janela Assets (SpriteUI): navegador de pastas com migalhas, filtro, lista/grade de icones, criar/renomear/apagar, importar e editor de texto simples. So desenha e reporta Actions; o estado fica no AM::State.
class View {
 public:
  // O que o host deve fazer depois do quadro (subir pasta, importar, abrir, instanciar, apagar, salvar texto...).
  enum class Action {
    None, Up, Crumb, Import, Open, Activate, Instantiate, Assign, Confirm, Delete, SaveText, CloseText, Help, Close
  };

  View(AM::State& state, const sui::TextAtlasSet& text) : state_{state}, text_{text} { buildTree(); }

  // Provedor de miniatura (opcional): dado o item, devolve true e preenche o retangulo UV (u, v, w, h) dentro do
  // atlas kMaterialSlice. Usado para os materiais (.json em Materials/); false = icone generico do tipo.
  using ThumbProvider = std::function<bool(const AM::Item&, float uv[4])>;
  void setThumbProvider(ThumbProvider p) { thumb_ = std::move(p); }

  // Calcula um quadro e devolve o pacote para desenhar. `actions` recebe o que foi pedido neste quadro.
  SR::FramePacket Frame(float windowWidth, float windowHeight, const FrameInput& in, std::vector<Action>& actions) {
    frame_.refreshStyle();
    actions.clear();
    tips_.clear();   // as dicas sao refeitas a cada quadro (retangulos mudam com rolagem e layout)
    now_ = in.timeSeconds;
    if (windowWidth <= 0.f || windowHeight <= 0.f) return {};

    const float panelWidth = dock_ ? dock_->size.x : std::min(Theme::kPanelWidth, windowWidth - 32.f);
    const float frameHeight = Theme::kPanelHeight + frame_.metrics().headerHeight + 2.f * frame_.metrics().borderWidth;
    frame_.placeIn(windowWidth, windowHeight, dock_, panelWidth, frameHeight);   // posicao, dock e minimizar: moldura padrao
    handleFrameInput(in, actions);

    // Minimizada: so a moldura (cabecalho). O conteudo nao aparece nem recebe cliques.
    if (frame_.minimized()) {
      std::vector<SR::UIRenderItem> items;
      std::uint64_t paint = 1000;
      addFrame(items, paint);
      return extractor_.Build(in.frameId, items, nullptr, {});
    }

    // Barra de rolagem unica (ui_scrollbar.hpp), em linhas. Usa o trilho do quadro anterior; o clique nela nao chega aos
    // itens de baixo e o arraste segue com o mouse fora da janela (o painel guarda a captura).
    bool barUse = bar_.dragging();
    {
      float rows = static_cast<float>(state_.scroll);
      if (in.pointerPressed && bar_.pointerDown(in.pointerX, in.pointerY, rows)) barUse = true;
      if (bar_.dragging() && !in.pointerDown && !in.pointerPressed) bar_.pointerUp();
      else bar_.pointerMove(in.pointerX, in.pointerY, rows);
      state_.scroll = static_cast<int>(std::lround(rows));
    }
    prepareLists();
    applyGridStyles();
    const SU::Rect content = frame_.content();
    setPanelWidth(content.size.x);
    tree_.Layout({content.position, {content.size.x, Theme::kPanelHeight}});
    applyVisuals();

    auto scene = bridge_.Extract(tree_);
    SU::Input::UIInputSnapshot input;
    input.pointerPositionLogical = {in.pointerX, in.pointerY};
    input.pointerDeltaLogical = {in.pointerX - lastX_, in.pointerY - lastY_};
    input.primaryDown = in.pointerDown && !barUse;
    input.primaryPressed = in.pointerPressed && !barUse;
    input.primaryReleased = in.pointerReleased && !barUse;
    input.windowFocused = in.windowFocused;
    input.frameId = in.frameId;
    lastX_ = in.pointerX;
    lastY_ = in.pointerY;

    const auto events = router_.Process(input, scene.hitTargets);
    for (const auto& event : events) {
      if (event.type == SU::Input::UIEventType::Click) handleClick(event, actions);
    }
    if (pendingConfirm_) {   // Enter no campo de nome
      pendingConfirm_ = false;
      tryConfirm(actions);
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
    const SU::Rect framed = frame_.rect();
    clips.push_back({framed.position.x, framed.position.y, framed.position.x + framed.size.x, framed.position.y + framed.size.y});
    const auto frameClip = static_cast<std::uint32_t>(clips.size() - 1);
    for (auto& item : scene.renderItems) {
      if (item.clipIndex == SR::NoClipIndex) item.clipIndex = frameClip;
    }
    std::uint64_t paint = 1000;
    addFrame(extra, paint);
    addContent(extra, paint);
    addScrollbar(extra, paint);
    scene.renderItems.insert(scene.renderItems.end(), extra.begin(), extra.end());
    collectTips();   // depois do layout e da barra de rolagem: todos os retangulos ja estao prontos
    return extractor_.Build(in.frameId, scene.renderItems, nullptr, clips);
  }

  // Digitacao e tecla Enter / Backspace (o host repassa o que o SDL mandou).
  void TypeText(const std::string& utf8) {
    AM::Type(state_, utf8);
    if (state_.textOpen) follow_ = true;
  }
  // Apaga o ultimo caractere (UTF-8) do campo focado (filtro, nome ou texto aberto).
  void Backspace() {
    AM::Backspace(state_);
    if (state_.textOpen) follow_ = true;
  }
  // Enter: confirma criar/renomear/apagar ou, no texto aberto, quebra a linha.
  void EnterKey() {
    if (state_.textOpen) {
      AM::Type(state_, "\n");
      follow_ = true;
    } else if (state_.mode != AM::State::Mode::None) {
      pendingConfirm_ = true;
    }
  }
  // Roda do mouse: move a lista (linhas) ou o texto (linhas).
  void Scroll(int rows) { state_.scroll -= rows; }

  // Resultado do ultimo clique: valido quando a acao correspondente (Activate, Crumb) aparece no quadro.
  [[nodiscard]] std::size_t pickedItem() const { return pickedItem_; }          // indice em State::items
  [[nodiscard]] std::size_t activatedItem() const { return activatedItem_; }    // duplo clique
  [[nodiscard]] const std::string& pickedCrumb() const { return pickedCrumb_; } // pasta da migalha clicada
  [[nodiscard]] static constexpr std::size_t none() { return kNone; }

  // Estado para testes e para o host.
  [[nodiscard]] SU::Rect frameRect() const { return frame_.rect(); }
  [[nodiscard]] SU::Vec2 windowPos() const { return frame_.position(); }
  // Area dada pelo dock (folha, sem a faixa de abas). nullopt: a janela flutua.
  void setDock(std::optional<SU::Rect> dock) { dock_ = dock; }
  [[nodiscard]] bool docked() const { return frame_.docked(); }
  [[nodiscard]] SU::Rect gripRect() const { return frame_.gripRect(); }
  // Redimensionar pela borda e arrastar pelo grip sao do host do dock (unico dono do ponteiro): ele usa esta moldura.
  window_frame::Frame* frameState() { return &frame_; }
  // Sai do dock na posicao `at`: a janela segue flutuante a partir dali.
  void floatAt(SU::Vec2 at) {
    dock_.reset();
    frame_.undock(at);
  }
  [[nodiscard]] bool dragging() const { return frame_.dragging(); }
  [[nodiscard]] bool minimized() const { return frame_.minimized(); }
  [[nodiscard]] const std::vector<std::size_t>& visible() const { return visible_; }
  [[nodiscard]] SU::UIHandle cellHandle(int row, int col) const { return cell_[row][col]; }
  [[nodiscard]] SU::UIHandle openHandle() const { return open_; }
  [[nodiscard]] SU::UIHandle addHandle() const { return add_; }
  [[nodiscard]] SU::UIHandle assignHandle() const { return assign_; }
  [[nodiscard]] SU::UIHandle renameHandle() const { return rename_; }
  [[nodiscard]] SU::UIHandle deleteHandle() const { return delete_; }
  [[nodiscard]] SU::UIHandle importHandle() const { return import_; }
  [[nodiscard]] SU::UIHandle newFolderHandle() const { return newFolder_; }
  [[nodiscard]] SU::UIHandle newFileHandle() const { return newFile_; }
  [[nodiscard]] SU::UIHandle listHandle() const { return list_; }
  [[nodiscard]] SU::UIHandle iconsHandle() const { return icons_; }
  [[nodiscard]] SU::UIHandle upHandle() const { return up_; }
  [[nodiscard]] SU::UIHandle crumbHandle(std::size_t i) const { return crumb_[i]; }
  [[nodiscard]] SU::UIHandle filterHandle() const { return filter_; }
  [[nodiscard]] SU::UIHandle nameHandle() const { return nameField_; }
  [[nodiscard]] SU::UIHandle confirmHandle() const { return confirm_; }
  [[nodiscard]] SU::UIHandle cancelHandle() const { return cancel_; }
  [[nodiscard]] SU::Rect helpRect() const { return helpRectNow(); }
  [[nodiscard]] SU::Rect rectOf(SU::UIHandle handle) const { return nodeRect(handle); }

  // Dica de um controle: retangulo em coordenadas de tela e texto ja traduzido.
  struct Tip {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    std::string text;
  };
  // Dicas do ultimo quadro, na ordem de registro (o host usa a ultima que contem o mouse: a mais "por cima").
  [[nodiscard]] const std::vector<Tip>& tips() const { return tips_; }

 private:
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
  static constexpr int kMaxRows = AM::kListRows;   // linhas montadas (a grade usa as 4 primeiras)
  static constexpr int kMaxCols = AM::kIconCols;

  // Ponto dentro do retangulo (borda direita/inferior exclusiva).
  static bool inside(SU::Rect r, SU::Vec2 p) {
    return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y &&
           p.y < r.position.y + r.size.y;
  }


  // "?" logo depois do grip; o titulo comeca depois dele.
  SU::Rect helpRectNow() const { return frame_.helpRect(); }

  // Botoes da moldura: X fecha, "?" pede a Help, minimizar recolhe (a propria moldura). Arraste e redimensionamento
  // nao passam por aqui: o host do dock decide o gesto e move a moldura.
  void handleFrameInput(const FrameInput& in, std::vector<Action>& actions) {
    if (!in.pointerPressed) return;
    switch (frame_.buttonAt({in.pointerX, in.pointerY})) {
      case window_frame::Event::Close: actions.push_back(Action::Close); break;
      case window_frame::Event::Help: actions.push_back(Action::Help); break;
      default: break;
    }
  }

  // ---------- Dicas (tooltips) ----------
  // Registra uma dica para o retangulo `r` pelo id da tabela assets_tips (traduzida pelo pacote de idioma).
  void addTip(const SU::Rect& r, const char* id) {
    if (r.size.x <= 0.f || r.size.y <= 0.f) return;
    const char* english = editor::assets_tips::Lookup(id);
    if (!english) return;
    tips_.push_back({r.position.x, r.position.y, r.size.x, r.size.y, editor::T(english)});
  }

  // Id da dica de um item da grade conforme o tipo (material = .json dentro de Materials/).
  static const char* itemTipId(const AM::Item& it) {
    if (it.dir || it.kind == AssetKind::Folder) return "itemFolder";
    switch (it.kind) {
      case AssetKind::Model: return "itemModel";
      case AssetKind::Audio: return "itemAudio";
      case AssetKind::Scene: return "itemScene";
      case AssetKind::Image: return "itemImage";
      case AssetKind::Text: return it.rel.rfind("Materials/", 0) == 0 ? "itemMaterial" : "itemText";
      default: return "itemFile";
    }
  }

  // Monta as dicas do quadro para todos os controles. Ordem: do fundo para o topo (o ultimo que contem o mouse vence).
  void collectTips() {
    addTip(nodeRect(status_), "status");
    if (state_.textOpen) addTip(nodeRect(grid_), "textArea");   // fora do editor de texto a grade nao tem dica propria
    addTip(helpRectNow(), "help");
    addTip(nodeRect(up_), "up");
    for (std::size_t i = 0; i < Theme::kMaxCrumbs; ++i) {
      if (crumbFirst_ + i < crumbs_.size()) addTip(nodeRect(crumb_[i]), "crumb");
    }
    addTip(nodeRect(import_), "import");
    addTip(nodeRect(newFolder_), "newFolder");
    addTip(nodeRect(newFile_), "newFile");
    addTip(nodeRect(list_), "viewList");
    addTip(nodeRect(icons_), "viewIcons");
    addTip(nodeRect(filter_), "search");
    // Itens da grade (cada celula com item); com o texto aberto nenhuma celula age.
    for (int r = 0; r < kMaxRows; ++r) {
      for (int c = 0; c < kMaxCols; ++c) {
        const std::size_t v = cellVisible(r, c);
        if (v == kNone) continue;
        addTip(nodeRect(cell_[r][c]), itemTipId(state_.items[visible_[v]]));
      }
    }
    if (bar_.visible()) {
      const ui_scrollbar::Rect& t = bar_.track();
      addTip(SU::Rect{{t.x, t.y}, {t.w, t.h}}, "scrollbar");
    }
    // Barra de acoes: com o texto aberto, Open vira Save e Rename vira Close.
    addTip(nodeRect(open_), state_.textOpen ? "save" : "open");
    addTip(nodeRect(add_), "add");
    addTip(nodeRect(assign_), "assign");
    addTip(nodeRect(rename_), state_.textOpen ? "closeText" : "rename");
    addTip(nodeRect(delete_), "delete");
    // Campo de nome e botoes de confirmacao (so aparecem durante criar/renomear/apagar).
    addTip(nodeRect(nameField_), "nameField");
    if (!state_.textOpen && state_.mode != AM::State::Mode::None) {
      addTip(nodeRect(confirm_), "confirm");
      addTip(nodeRect(cancel_), "cancel");
    }
  }

  // ---------- Listas e rolagem (antes do layout) ----------
  void prepareLists() {
    visible_ = AM::Visible(state_);
    crumbs_ = AM::Crumbs(state_.folder);
    crumbFirst_ = crumbs_.size() > Theme::kMaxCrumbs ? crumbs_.size() - Theme::kMaxCrumbs : 0;
    if (state_.textOpen) {
      const int lines = AM::TextLines(state_.text);
      const int maxScroll = std::max(0, lines - Theme::kTextLines);
      if (follow_) state_.scroll = maxScroll;
      state_.scroll = std::clamp(state_.scroll, 0, maxScroll);
    } else {
      state_.scroll = std::clamp(state_.scroll, 0, AM::MaxScroll(visible_.size(), state_.iconView));
    }
    follow_ = false;
  }

  // Item visivel na celula (linha, coluna) da grade, ou kNone. Com o texto aberto, nenhuma celula age.
  std::size_t cellVisible(int row, int col) const {
    if (state_.textOpen) return kNone;
    const int cols = AM::Cols(state_.iconView);
    std::size_t v = kNone;
    if (state_.iconView) {
      if (row >= AM::kIconRows) return kNone;
      v = static_cast<std::size_t>((state_.scroll + row) * cols + col);
    } else {
      if (col != 0 || row >= AM::kListRows) return kNone;
      v = static_cast<std::size_t>(state_.scroll + row);
    }
    return v < visible_.size() ? v : kNone;
  }

  // Item selecionado da lista visivel, ou nullptr.
  const AM::Item* selectedItem() const {
    for (const AM::Item& it : state_.items) {
      if (it.rel == state_.selected) return &it;
    }
    return nullptr;
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
    applyPalette();   // a arvore usa as cores do tema ja aqui (antes, o conteudo saia com a cor padrao: cinza)

    SU::FlexItem panel;
    panel.width = {SU::SizeMode::Fixed, Theme::kPanelWidth};
    panel.height = {SU::SizeMode::Fixed, Theme::kPanelHeight};
    panel.direction = SU::LayoutDirection::Column;
    panel.alignItems = SU::AlignItems::Stretch;
    panel.padding = {Theme::kPadding, Theme::kPadding, Theme::kPadding, Theme::kPadding};
    panel.gap = Theme::kGap;
    panelHandle_ = tree_.Create({}, panel);

    // Barra de migalhas: "<", as ultimas pastas do caminho.
    const auto crumbRow = tree_.Create(panelHandle_, rowStyle(Theme::kRowHeight));
    up_ = tree_.Create(crumbRow, buttonStyle(Theme::kUpWidth));
    for (std::size_t i = 0; i < Theme::kMaxCrumbs; ++i) crumb_[i] = tree_.Create(crumbRow, buttonStyle(Theme::kCrumbWidth));

    // Ferramentas: importar, pastas e textos novos, lista ou icones.
    const auto toolRow = tree_.Create(panelHandle_, rowStyle(Theme::kRowHeight));
    import_ = tree_.Create(toolRow, buttonStyle(Theme::kToolWidth));
    newFolder_ = tree_.Create(toolRow, buttonStyle(Theme::kToolWidth));
    newFile_ = tree_.Create(toolRow, buttonStyle(Theme::kToolWidth));
    list_ = tree_.Create(toolRow, buttonStyle(Theme::kViewIconWidth));
    icons_ = tree_.Create(toolRow, buttonStyle(Theme::kViewIconWidth));

    filter_ = tree_.Create(panelHandle_, fieldStyle(Theme::kFieldHeight));

    // Grade: linhas (kMaxRows) com celulas (kMaxCols). Na lista so a primeira coluna aparece.
    grid_ = tree_.Create(panelHandle_, gridStyle());
    for (int r = 0; r < kMaxRows; ++r) {
      row_[r] = tree_.Create(grid_, rowStyle(Theme::kListRowHeight));
      for (int c = 0; c < kMaxCols; ++c) cell_[r][c] = tree_.Create(row_[r], cellStyle(1.f / kMaxCols, false));
    }

    const auto actionRow = tree_.Create(panelHandle_, rowStyle(Theme::kRowHeight));
    open_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    add_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    assign_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    rename_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));
    delete_ = tree_.Create(actionRow, buttonStyle(Theme::kButtonWidth));

    const auto nameRow = tree_.Create(panelHandle_, rowStyle(Theme::kFieldHeight));
    nameField_ = tree_.Create(nameRow, fieldWidthStyle(500.f));
    confirm_ = tree_.Create(nameRow, buttonStyle(110.f));
    cancel_ = tree_.Create(nameRow, buttonStyle(110.f));

    status_ = tree_.Create(panelHandle_, labelStyle(Theme::kStatusHeight));

    // Containers e rotulos sem acao: visual transparente e nao interativo. Sem visual, o roteador os trata como alvos
    // bloqueantes, e a linha da barra de acoes roubava o clique do campo de nome.
    makePassive(crumbRow);
    makePassive(toolRow);
    makePassive(actionRow);
    makePassive(nameRow);
    makePassive(status_);
    for (int r = 0; r < kMaxRows; ++r) makePassive(row_[r]);

    SU::ElementVisual panelVisual;
    panelVisual.tintPremultipliedLinear = panelColor_;
    panelVisual.interactable = false;
    panelVisual.clipOverflow = false;
    panelVisual.zOrder = -1.f;   // fundo do painel atras dos elementos (zOrder 0)
    bridge_.SetVisual(panelHandle_, panelVisual);
  }

  // Deixa o elemento sem clique e sem cor (so ocupa espaco no layout; o desenho e feito a mao).
  void makePassive(SU::UIHandle h) {
    SU::ElementVisual v;
    v.interactable = false;
    v.clipOverflow = false;
    v.zOrder = 0.f;
    v.tintPremultipliedLinear = SU::Vec4{};
    bridge_.SetVisual(h, v);
  }

  // Filhos de altura fixa nao encolhem (shrink = 0): sem isso o flex os reduz a zero dentro da coluna.
  static SU::FlexItem labelStyle(float height) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    return s;
  }
  // Botao com largura fixa e altura padrao.
  static SU::FlexItem buttonStyle(float width) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, width};
    s.height = {SU::SizeMode::Fixed, Theme::kRowHeight};
    s.shrink = 0.f;
    return s;
  }
  // Campo de altura dada que ocupa toda a largura.
  static SU::FlexItem fieldStyle(float height) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    return s;
  }
  // Campo de altura padrao com largura fixa.
  static SU::FlexItem fieldWidthStyle(float width) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, width};
    s.height = {SU::SizeMode::Fixed, Theme::kFieldHeight};
    s.shrink = 0.f;
    return s;
  }
  // Area da lista/grade: ocupa a largura e a altura fixa da grade.
  static SU::FlexItem gridStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, Theme::kGridHeight};
    s.shrink = 0.f;
    s.direction = SU::LayoutDirection::Column;
    s.alignItems = SU::AlignItems::Stretch;
    return s;
  }
  // Linha de altura fixa que ocupa toda a largura.
  static SU::FlexItem rowStyle(float height) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Percent, 1.f};
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    s.direction = SU::LayoutDirection::Row;
    s.alignItems = SU::AlignItems::Stretch;
    return s;
  }
  // Celula: fracao da linha; com `hidden`, some (largura zero).
  static SU::FlexItem cellStyle(float fraction, bool hidden) {
    SU::FlexItem s;
    if (hidden) {
      s.width = {SU::SizeMode::Fixed, 0.f};
    } else {
      s.width = {SU::SizeMode::Percent, fraction};
    }
    s.height = {SU::SizeMode::Percent, 1.f};
    s.shrink = 0.f;
    return s;
  }

  // Lista (1 coluna de linhas de altura fixa) ou grade de icones (linhas altas com 6 celulas).
  void applyGridStyles() {
    const bool icon = state_.iconView;
    for (int r = 0; r < kMaxRows; ++r) {
      float height = 0.f;
      if (icon) {
        height = r < AM::kIconRows ? Theme::kIconRowHeight : 0.f;
      } else {
        height = r < AM::kListRows ? Theme::kListRowHeight : 0.f;
      }
      tree_.SetStyle(row_[r], rowStyle(height));
      for (int c = 0; c < kMaxCols; ++c) {
        if (icon) {
          tree_.SetStyle(cell_[r][c], cellStyle(1.f / kMaxCols, false));
        } else {
          tree_.SetStyle(cell_[r][c], cellStyle(1.f, c != 0));
        }
      }
    }
  }

  // Ajusta a largura do painel (a altura e fixa, ver Theme::kPanelHeight).
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

  // Texto cortado com "..." para caber em maxW.
  Shaped fitShape(const std::string& utf8, float size, float maxW) const {
    Shaped s = shape(utf8, size);
    if (s.layout.size.x <= maxW) return s;
    std::string cut = utf8;
    while (!cut.empty()) {
      AM::PopUtf8Back(cut);
      Shaped t = shape(cut + "...", size);
      if (t.layout.size.x <= maxW) return t;
    }
    return shape("...", size);
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

  // Botao habilitado neste quadro? (calculado em applyVisuals)
  bool isEnabled(SU::UIHandle h) const {
    for (const auto& [handle, on] : enabled_) {
      if (handle == h) return on;
    }
    return false;
  }

  // Habilita/desabilita botoes conforme selecao, modo e texto aberto, e pinta cada elemento.
  void applyVisuals() {
    enabled_.clear();
    const bool text = state_.textOpen;
    const bool idle = !text && state_.mode == AM::State::Mode::None;
    const AM::Item* sel = selectedItem();
    const bool hasSel = sel != nullptr;
    const bool selFolderOrScene = hasSel && (sel->dir || sel->kind == AssetKind::Scene || sel->kind == AssetKind::Text);
    const bool selAddable = hasSel && (sel->kind == AssetKind::Model || sel->kind == AssetKind::Audio);

    paintButton(up_, idle && state_.folder != AM::kRoot, colors_.button);
    for (std::size_t i = 0; i < Theme::kMaxCrumbs; ++i) {
      const bool shown = crumbFirst_ + i < crumbs_.size();
      paintButton(crumb_[i], shown && idle, colors_.button);
    }
    paintButton(import_, idle, colors_.button);
    paintButton(newFolder_, idle, colors_.button);
    paintButton(newFile_, idle, colors_.button);
    paintButton(list_, idle, state_.iconView ? colors_.button : colors_.buttonActive);
    paintButton(icons_, idle, state_.iconView ? colors_.buttonActive : colors_.button);

    paintButton(open_, text || (idle && selFolderOrScene), colors_.button);
    paintButton(add_, idle && selAddable, colors_.button);
    paintButton(assign_, idle && selAddable, colors_.button);
    paintButton(rename_, text || (idle && hasSel), colors_.button);
    paintButton(delete_, idle && hasSel, colors_.buttonDanger);
    paintButton(confirm_, !text && state_.mode != AM::State::Mode::None,
                state_.mode == AM::State::Mode::Delete ? colors_.buttonDanger : colors_.button);
    paintButton(cancel_, !text && state_.mode != AM::State::Mode::None, colors_.button);

    SU::ElementVisual filter;
    filter.interactable = !text;
    filter.clipOverflow = false;
    filter.zOrder = 0.f;
    filter.tintPremultipliedLinear = state_.filterFocus ? colors_.fieldFocus : colors_.field;
    bridge_.SetVisual(filter_, filter);

    SU::ElementVisual field;
    field.interactable = !text;
    field.clipOverflow = false;
    field.zOrder = 0.f;
    field.tintPremultipliedLinear = state_.nameFocus ? colors_.fieldFocus : colors_.field;
    bridge_.SetVisual(nameField_, field);

    // Grade: cada celula com item e clicavel; a selecionada fica destacada. Com o texto aberto, a area vira um campo.
    for (int r = 0; r < kMaxRows; ++r) {
      for (int c = 0; c < kMaxCols; ++c) {
        const std::size_t v = cellVisible(r, c);
        SU::ElementVisual cv;
        cv.clipOverflow = false;
        cv.zOrder = 0.f;
        cv.interactable = v != kNone;
        if (v == kNone) {
          cv.tintPremultipliedLinear = panelColor_;
        } else {
          const AM::Item& it = state_.items[visible_[v]];
          cv.tintPremultipliedLinear = it.rel == state_.selected ? colors_.rowSelected : colors_.field;
        }
        bridge_.SetVisual(cell_[r][c], cv);
      }
    }
    SU::ElementVisual gridVisual;   // fundo da grade (area do texto aberto)
    gridVisual.interactable = false;
    gridVisual.clipOverflow = false;
    gridVisual.zOrder = 0.f;
    gridVisual.tintPremultipliedLinear = text ? colors_.field : panelColor_;
    bridge_.SetVisual(grid_, gridVisual);
  }

  // Clique em um elemento: migalha, botao de acao, celula da grade (selecionar/duplo clique), foco dos campos etc.; vira Action para o host.
  void handleClick(const SU::Input::UIEvent& event, std::vector<Action>& actions) {
    if (event.target == filter_) {
      if (!state_.textOpen) {
        state_.filterFocus = true;
        state_.nameFocus = false;
      }
      return;
    }
    if (event.target == nameField_) {
      if (AM::NeedsName(state_.mode)) {
        state_.nameFocus = true;
        state_.filterFocus = false;
      }
      return;
    }
    state_.nameFocus = false;    // qualquer outro clique tira o foco dos campos
    state_.filterFocus = false;

    for (int r = 0; r < kMaxRows; ++r) {
      for (int c = 0; c < kMaxCols; ++c) {
        if (event.target != cell_[r][c]) continue;
        const std::size_t v = cellVisible(r, c);
        if (v == kNone) return;
        const std::size_t idx = visible_[v];
        pickedItem_ = idx;
        state_.selected = state_.items[idx].rel;
        if (event.target == lastCell_ && now_ - lastClickTime_ < Theme::kDoubleClickSeconds) {
          activatedItem_ = idx;   // duplo clique: o host decide (entra na pasta, abre, edita, instancia)
          actions.push_back(Action::Activate);
          lastCell_ = {};
        } else {
          lastCell_ = event.target;
          lastClickTime_ = now_;
        }
        return;
      }
    }

    if (!isEnabled(event.target)) return;
    if (event.target == up_) {
      actions.push_back(Action::Up);
      return;
    }
    for (std::size_t i = 0; i < Theme::kMaxCrumbs; ++i) {
      if (event.target != crumb_[i]) continue;
      pickedCrumb_ = crumbs_[crumbFirst_ + i].folder;
      actions.push_back(Action::Crumb);
      return;
    }
    if (event.target == import_) {
      actions.push_back(Action::Import);
      return;
    }
    if (event.target == newFolder_) {
      AM::Begin(state_, AM::State::Mode::NewFolder, std::string(AM::kDefaultFolderName));
      return;
    }
    if (event.target == newFile_) {
      AM::Begin(state_, AM::State::Mode::NewFile, std::string(AM::kDefaultFileName));
      return;
    }
    if (event.target == list_) {
      state_.iconView = false;
      state_.scroll = 0;
      return;
    }
    if (event.target == icons_) {
      state_.iconView = true;
      state_.scroll = 0;
      return;
    }
    if (event.target == open_) {
      actions.push_back(state_.textOpen ? Action::SaveText : Action::Open);
      return;
    }
    if (event.target == add_) {
      actions.push_back(Action::Instantiate);
      return;
    }
    if (event.target == assign_) {
      actions.push_back(Action::Assign);
      return;
    }
    if (event.target == rename_) {
      if (state_.textOpen) {
        actions.push_back(Action::CloseText);
      } else {
        AM::Begin(state_, AM::State::Mode::Rename, AM::BaseName(state_.selected));
      }
      return;
    }
    if (event.target == delete_) {   // apagar pede confirmacao antes
      AM::Begin(state_, AM::State::Mode::Delete);
      return;
    }
    if (event.target == confirm_) {
      tryConfirm(actions);
      return;
    }
    if (event.target == cancel_) {
      AM::Finish(state_);
      return;
    }
  }

  // Confirmar: apagar vai ao host; criar ou renomear so se o nome passa nas regras.
  void tryConfirm(std::vector<Action>& actions) {
    if (state_.mode == AM::State::Mode::None) return;
    if (state_.mode == AM::State::Mode::Delete) {
      actions.push_back(Action::Delete);
      return;
    }
    const std::string problem = AM::NameProblemFor(state_, state_.name);
    if (!problem.empty()) {
      state_.status = problem;
      return;
    }
    state_.status.clear();
    actions.push_back(Action::Confirm);
  }

  // Retangulo calculado de um no do layout (vazio se nao existe).
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
    state.dragging = frame_.dragging();
    state.docked = frame_.docked();
    applyPalette();
    auto frameItems = SW::BuildWindowFrameItems(panelHandle_, frame_.rect(), frame_.metrics(), state, frameColors_, -2.f, paint);
    paint += frameItems.size() + 1;
    out.insert(out.end(), frameItems.begin(), frameItems.end());

    // Grip (pontos).
    const SU::Rect grip = frame_.gripRect();
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
    const Shaped title = shape(T("Assets"), Theme::kTitleSize);
    const float tx = help.position.x + help.size.x + Theme::kHelpGap;
    const SU::Rect bar = frame_.header();
    const float ty = bar.position.y + (bar.size.y - title.layout.lineHeight) * 0.5f;
    text(panelHandle_, title, {tx, ty}, colors_.text, 0.95f);
    text(panelHandle_, title, {tx + 0.7f, ty}, colors_.text, 0.95f);
  }

  // Conteudo: migalhas, barras, grade (icones ou lista ou texto aberto), campo de nome e status.
  // Barra de rolagem unica do editor, na direita da grade; as unidades sao linhas (a roda tambem rola em linhas).
  void addScrollbar(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint) {
    const SU::Rect grid = nodeRect(grid_);
    float totalRows = 0.f, viewRows = 0.f;
    if (state_.textOpen) {
      totalRows = static_cast<float>(AM::TextLines(state_.text));
      viewRows = static_cast<float>(Theme::kTextLines);
    } else {
      const int cols = AM::Cols(state_.iconView);
      totalRows = static_cast<float>((visible_.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols));
      viewRows = static_cast<float>(state_.iconView ? AM::kIconRows : AM::kListRows);
    }
    bar_.layout({grid.position.x + grid.size.x - ui_scrollbar::kWidth, grid.position.y, ui_scrollbar::kWidth, grid.size.y}, totalRows, viewRows);
    for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), static_cast<float>(state_.scroll))) {
      if (q.rect.w <= 0.f || q.rect.h <= 0.f) continue;
      SR::UIRenderItem item;
      item.handle = grid_;
      item.layout.resolvedPosition = {q.rect.x, q.rect.y};
      item.layout.resolvedSize = {q.rect.w, q.rect.h};
      item.tintPremultipliedLinear = Srgb(q.color.r, q.color.g, q.color.b, q.color.a);
      item.textureSlice = SR::WhiteTextureSlice;
      item.clipIndex = SR::NoClipIndex;
      item.zOrder = 0.7f;
      item.paintOrder = paint++;
      out.push_back(item);
    }
  }

  // Gera os itens de desenho do conteudo: botoes, filtro, grade de itens, campo de nome e status.
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
    // Texto alinhado a esquerda na celula, centrado na altura.
    auto line = [&](SU::UIHandle h, const std::string& label, SU::Vec4 color, float size = Theme::kLabelSize) {
      const Shaped s = shape(label, size);
      const SU::Rect r = nodeRect(h);
      text(h, s, {r.position.x + Theme::kFieldInset, r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f}, color, 0.9f);
    };

    // Migalhas e barra de ferramentas.
    button(up_, "<");
    for (std::size_t i = 0; i < Theme::kMaxCrumbs; ++i) {
      if (crumbFirst_ + i >= crumbs_.size()) break;
      button(crumb_[i], crumbs_[crumbFirst_ + i].label);
    }
    button(import_, T("Import..."));
    button(newFolder_, T("New folder"));
    button(newFile_, T("New text file"));
    // Icones de modo de exibicao: quadrado com 3 linhas (lista) e quadrado com uma pasta (icones).
    {
      const SU::Vec4 ink = colors_.text;
      const SU::Rect lr = nodeRect(list_);
      const SU::Rect ir = nodeRect(icons_);
      auto frameOf = [](const SU::Rect& r) { return SU::Rect{{r.position.x + 6.f, r.position.y + 6.f}, {18.f, 18.f}}; };
      const SU::Rect lf = frameOf(lr);
      const float t = 1.5f;
      solid(list_, {lf.position, {lf.size.x, t}}, ink, 0.6f);
      solid(list_, {{lf.position.x, lf.position.y + lf.size.y - t}, {lf.size.x, t}}, ink, 0.6f);
      solid(list_, {lf.position, {t, lf.size.y}}, ink, 0.6f);
      solid(list_, {{lf.position.x + lf.size.x - t, lf.position.y}, {t, lf.size.y}}, ink, 0.6f);
      for (int i = 0; i < 3; ++i) {
        solid(list_, {{lf.position.x + 4.5f, lf.position.y + 4.5f + 4.5f * static_cast<float>(i)}, {lf.size.x - 9.f, t}}, ink, 0.6f);
      }
      const SU::Rect gf = frameOf(ir);
      solid(icons_, {gf.position, {gf.size.x, t}}, ink, 0.6f);
      solid(icons_, {{gf.position.x, gf.position.y + gf.size.y - t}, {gf.size.x, t}}, ink, 0.6f);
      solid(icons_, {gf.position, {t, gf.size.y}}, ink, 0.6f);
      solid(icons_, {{gf.position.x + gf.size.x - t, gf.position.y}, {t, gf.size.y}}, ink, 0.6f);
      solid(icons_, {{gf.position.x + 3.f, gf.position.y + 4.f}, {7.f, 3.f}}, ink, 0.6f);   // aba da pasta
      solid(icons_, {{gf.position.x + 3.f, gf.position.y + 6.f}, {12.f, 8.f}}, ink, 0.6f);  // corpo da pasta
    }

    // Filtro: placeholder quando vazio.
    {
      const SU::Rect fr = nodeRect(filter_);
      const bool placeholder = state_.filter.empty();
      const std::string shown = placeholder ? T("Search") : state_.filter;
      const Shaped fs = shape(shown, Theme::kFontSize);
      const SU::Vec2 origin{fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - fs.layout.lineHeight) * 0.5f};
      if (state_.filterFocus) {
        const std::uint32_t offset = placeholder ? 0u : static_cast<std::uint32_t>(state_.filter.size());
        const SU::Rect caret = ST::GetCaretRect(fs.layout, offset);
        solid(filter_, {{origin.x + caret.position.x, origin.y}, {1.5f, fs.layout.lineHeight}}, colors_.caret, 0.6f);
      }
      text(filter_, fs, origin, placeholder ? colors_.textDim : colors_.text, 0.9f);
    }

    // Miniatura de material: quadrado texturizado (atlas kMaterialSlice) no lugar do icone; uv vem do provedor.
    auto thumb = [&](SU::UIHandle owner, SU::Rect r, const float* uv, float z) {
      SR::UIRenderItem item;
      item.handle = owner;
      item.layout.resolvedPosition = r.position;
      item.layout.resolvedSize = r.size;
      item.tintPremultipliedLinear = {1.f, 1.f, 1.f, 1.f};
      item.uvRect[0] = uv[0]; item.uvRect[1] = uv[1]; item.uvRect[2] = uv[2]; item.uvRect[3] = uv[3];
      item.textureSlice = sui::kMaterialSlice;
      item.clipIndex = SR::NoClipIndex;
      item.zOrder = z;
      item.paintOrder = paint++;
      out.push_back(item);
    };
    drawGrid(solid, text, thumb);

    // Barra de acoes. Com o texto aberto, "Save" e "Close" tomam o lugar de Open e Rename.
    if (state_.textOpen) {
      button(open_, T("Save"));
      button(rename_, T("Close"));
    } else {
      button(open_, T("Open"));
      button(rename_, T("Rename..."));
    }
    button(add_, T("Add to scene"));
    button(assign_, T("Assign to scene"));
    button(delete_, T("Delete"));

    // Campo de nome: ou a pergunta de apagar, ou o nome em edicao, ou a dica.
    const SU::Rect fr = nodeRect(nameField_);
    const AM::State::Mode mode = state_.mode;
    if (mode == AM::State::Mode::Delete) {
      const Shaped q = fitShape(T("Delete '") + state_.selected + T("'? This cannot be undone."), Theme::kFontSize, fr.size.x - 16.f);
      text(nameField_, q, {fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - q.layout.lineHeight) * 0.5f},
           colors_.warning, 0.9f);
    } else if (state_.textOpen) {
      const Shaped q = fitShape(T("Typing adds text at the end of the file. Save writes it to disk."), Theme::kFontSize, fr.size.x - 16.f);
      text(nameField_, q, {fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - q.layout.lineHeight) * 0.5f},
           colors_.textDim, 0.9f);
    } else {
      const bool placeholder = state_.name.empty();
      std::string shown;
      if (placeholder) {
        shown = mode == AM::State::Mode::NewFolder ? T("Folder name") : mode == AM::State::Mode::NewFile ? T("File name")
              : mode == AM::State::Mode::Rename    ? T("New name")  : T("Select a file or folder, or create one.");
      } else {
        shown = state_.name;
      }
      const Shaped fs = shape(shown, Theme::kFontSize);
      const SU::Vec2 origin{fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - fs.layout.lineHeight) * 0.5f};
      if (state_.nameFocus) {
        const std::uint32_t offset = placeholder ? 0u : static_cast<std::uint32_t>(state_.name.size());
        const SU::Rect caret = ST::GetCaretRect(fs.layout, offset);
        solid(nameField_, {{origin.x + caret.position.x, origin.y}, {1.5f, fs.layout.lineHeight}}, colors_.caret, 0.6f);
      }
      text(nameField_, fs, origin, placeholder ? colors_.textDim : colors_.text, 0.9f);
    }
    if (AM::NeedsName(mode) || mode == AM::State::Mode::Delete) {
      button(confirm_, mode == AM::State::Mode::NewFolder ? T("Create folder")
                       : mode == AM::State::Mode::NewFile ? T("Create file")
                       : mode == AM::State::Mode::Rename  ? T("Rename")
                                                          : T("Delete"));
      button(cancel_, T("Cancel"));
    }

    // Status: aviso ou dica de uso.
    const std::string hint = state_.textOpen ? std::string{} : T("Drop files from your computer onto the editor window to import them.");
    if (!state_.status.empty()) {
      line(status_, state_.status, colors_.warning);
    } else if (!hint.empty()) {
      line(status_, hint, colors_.textDim);
    }
  }

  // Desenha o conteudo da grade: texto aberto, ou itens (icones ou lista), ou "pasta vazia".
  // Desenha a area principal: texto aberto (com linhas e cursor), mensagem de vazio ou os itens em lista/grade de icones.
  template <typename SolidFn, typename TextFn, typename ThumbFn>
  void drawGrid(SolidFn& solid, TextFn& text, ThumbFn& thumb) {
    const SU::Rect grid = nodeRect(grid_);
    if (state_.textOpen) {
      // Linhas do texto a partir da rolagem; caret no fim.
      std::vector<std::string> lines;
      std::size_t start = 0;
      for (;;) {
        const std::size_t nl = state_.text.find('\n', start);
        lines.push_back(state_.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
      const std::size_t first = static_cast<std::size_t>(std::max(0, state_.scroll));
      for (std::size_t i = 0; i < static_cast<std::size_t>(Theme::kTextLines) && first + i < lines.size(); ++i) {
        const Shaped s = fitShape(lines[first + i], Theme::kFontSize, grid.size.x - 2.f * Theme::kFieldInset);
        const float y = grid.position.y + 4.f + static_cast<float>(i) * Theme::kTextLineHeight;
        text(grid_, s, {grid.position.x + Theme::kFieldInset, y}, colors_.text, 0.5f);
        if (first + i + 1 == lines.size()) {   // caret logo depois da ultima linha (o texto so cresce no fim)
          const float cx = grid.position.x + Theme::kFieldInset + s.layout.size.x + 1.f;
          solid(grid_, {{cx, y}, {1.5f, Theme::kTextLineHeight - 4.f}}, colors_.caret, 0.6f);
        }
      }
      return;
    }

    if (visible_.empty() && state_.filter.empty()) {
      const Shaped s = shape(T("This folder is empty."), Theme::kLabelSize);
      text(grid_, s, {grid.position.x + Theme::kFieldInset, grid.position.y + 4.f}, colors_.textDim, 0.5f);
      return;
    }
    if (visible_.empty()) {
      const Shaped s = shape(T("No items match the filter."), Theme::kLabelSize);
      text(grid_, s, {grid.position.x + Theme::kFieldInset, grid.position.y + 4.f}, colors_.textDim, 0.5f);
      return;
    }

    for (int r = 0; r < kMaxRows; ++r) {
      for (int c = 0; c < kMaxCols; ++c) {
        const std::size_t v = cellVisible(r, c);
        if (v == kNone) continue;
        const AM::Item& it = state_.items[visible_[v]];
        const SU::UIHandle cell = cell_[r][c];
        const SU::Rect cr = nodeRect(cell);
        if (state_.iconView) {
          // Icone centralizado; nome embaixo, cortado.
          const float ix = cr.position.x + (cr.size.x - Theme::kIconSize) * 0.5f;
          const float iy = cr.position.y + 6.f;
          drawItemIcon(solid, text, thumb, cell, {{ix, iy}, {Theme::kIconSize, Theme::kIconSize}}, it, true);
          const Shaped name = fitShape(it.name, Theme::kLabelSize, cr.size.x - 8.f);
          text(cell, name, {cr.position.x + (cr.size.x - name.layout.size.x) * 0.5f, iy + Theme::kIconSize + 4.f},
               colors_.text, 0.5f);
        } else {
          // Lista: icone pequeno, nome, tipo e tamanho alinhados.
          drawItemIcon(solid, text, thumb, cell, {{cr.position.x + 8.f, cr.position.y + 5.f}, {16.f, 16.f}}, it, false);
          const Shaped name = fitShape(it.name, Theme::kLabelSize + 1.f, cr.size.x * 0.55f - 40.f);
          const float cy = cr.position.y + (cr.size.y - name.layout.lineHeight) * 0.5f;
          text(cell, name, {cr.position.x + 32.f, cy}, colors_.text, 0.5f);
          const Shaped kind = shape(T(kindLabel(it.kind)), Theme::kLabelSize);
          text(cell, kind, {cr.position.x + cr.size.x * 0.6f, cy}, colors_.textDim, 0.5f);
          if (!it.dir) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(it.size) / 1024.0);
            const Shaped size = shape(buf, Theme::kLabelSize);
            text(cell, size, {cr.position.x + cr.size.x * 0.85f, cy}, colors_.textDim, 0.5f);
          }
        }
      }
    }
  }

  // Icone de um item: miniatura do material se o provedor tem uma; senao o icone generico do tipo.
  template <typename SolidFn, typename TextFn, typename ThumbFn>
  void drawItemIcon(SolidFn& solid, TextFn& text, ThumbFn& thumb, SU::UIHandle owner, SU::Rect r, const AM::Item& it, bool withTag) {
    float uv[4];
    if (!it.dir && thumb_ && thumb_(it, uv)) {
      thumb(owner, r, uv, 0.45f);
      return;
    }
    drawIcon(solid, text, owner, r, it.kind, it.dir, withTag);
  }

  // Icone do tipo: pasta (aba + corpo) ou bloco colorido com a sigla do tipo (3D, SND, SCN, IMG, TXT).
  // Desenha o icone de um item: pasta amarela ou ficha do tipo de arquivo (com a etiqueta do tipo se withTag).
  template <typename SolidFn, typename TextFn>
  void drawIcon(SolidFn& solid, TextFn& text, SU::UIHandle owner, SU::Rect r, AssetKind kind, bool dir, bool withTag) {
    if (dir || kind == AssetKind::Folder) {
      const SU::Vec4 fill = Srgb(0.871f, 0.667f, 0.235f);
      solid(owner, {r.position, {r.size.x * 0.4f, r.size.y * 0.22f}}, fill, 0.45f);
      solid(owner, {{r.position.x, r.position.y + r.size.y * 0.18f}, {r.size.x, r.size.y * 0.82f}}, fill, 0.45f);
      return;
    }
    SU::Vec4 fill = Srgb(0.430f, 0.430f, 0.470f);
    const char* tag = "";
    switch (kind) {
      case AssetKind::Model: fill = Srgb(0.353f, 0.588f, 0.902f); tag = "3D"; break;
      case AssetKind::Audio: fill = Srgb(0.431f, 0.784f, 0.510f); tag = "SND"; break;
      case AssetKind::Scene: fill = Srgb(0.745f, 0.431f, 0.863f); tag = "SCN"; break;
      case AssetKind::Image: fill = Srgb(0.902f, 0.471f, 0.471f); tag = "IMG"; break;
      case AssetKind::Text: fill = Srgb(0.667f, 0.667f, 0.706f); tag = "TXT"; break;
      default: break;
    }
    solid(owner, r, fill, 0.45f);
    if (withTag && tag[0] != '\0') {
      const Shaped s = shape(tag, Theme::kLabelSize);
      text(owner, s, {r.position.x + (r.size.x - s.layout.size.x) * 0.5f, r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f},
           colors_.ink, 0.46f);
    }
  }

  // Etiqueta curta do tipo de asset (mostrada no icone e na coluna de tipo).
  static const char* kindLabel(AssetKind k) {
    switch (k) {
      case AssetKind::Folder: return "Folder";
      case AssetKind::Model: return "Model";
      case AssetKind::Audio: return "Audio";
      case AssetKind::Scene: return "Scene";
      case AssetKind::Image: return "Image";
      case AssetKind::Text: return "Text";
      default: return "File";
    }
  }

  AM::State& state_;
  const sui::TextAtlasSet& text_;
  ThumbProvider thumb_;   // miniaturas de material (opcional; ver setThumbProvider)
  Colors colors_;
  SU::Vec4 panelColor_{};

  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  SU::Input::UIEventRouter router_;
  SR::RenderExtractor extractor_;

  SU::UIHandle panelHandle_{}, up_{}, crumb_[Theme::kMaxCrumbs]{}, import_{}, newFolder_{}, newFile_{}, list_{}, icons_{};
  ui_scrollbar::Scrollbar bar_;
  SU::UIHandle filter_{}, grid_{}, row_[kMaxRows]{}, cell_[kMaxRows][kMaxCols]{};
  SU::UIHandle open_{}, add_{}, assign_{}, rename_{}, delete_{}, nameField_{}, confirm_{}, cancel_{}, status_{};
  SU::UIHandle hovered_{}, lastCell_{};
  std::vector<std::size_t> visible_;
  std::vector<AM::Crumb> crumbs_;
  std::size_t crumbFirst_ = 0;
  std::vector<std::pair<SU::UIHandle, bool>> enabled_;
  std::vector<Tip> tips_;   // dicas do quadro atual (ver collectTips)
  std::size_t pickedItem_ = kNone;
  std::size_t activatedItem_ = kNone;
  std::string pickedCrumb_;
  bool pendingConfirm_ = false;
  bool follow_ = false;
  double now_ = 0.0;
  double lastClickTime_ = -1e9;
  float lastX_ = 0.f, lastY_ = 0.f;

  // Moldura: cores (SpriteUI, definidas em buildTree) e geometria padrao (window_frame::Frame).
  SW::WindowFrameColors frameColors_{};
  window_frame::Frame frame_;
  std::optional<SU::Rect> dock_;
};

}  // namespace editor::assetsui
