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
#include "ui_scrollbar.hpp"
#include "usability_hints.hpp"   // motivo de botao bloqueado e dica da lista vazia
#include "config_preset.hpp"     // lista de configs (render + fisica) da pasta do projeto
#include "tooltip.hpp"           // balao da engrenagem (um Tooltip proprio da janela; o host o desenha)
#include "spriteui_host.hpp"
#include "window_frame.hpp"
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

#include <SDL3/SDL.h>   // SDL_ShowOpenFileDialog (botao "Escolher arquivo...")

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
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
// Medidas fixas da janela Scenes (margens, alturas de linha e botao, limites de linhas e assets).
struct Theme {
  static constexpr float kPanelWidth = 680.f;
  static constexpr float kPadding = 24.f;
  static constexpr float kGap = 6.f;
  static constexpr float kLabelHeight = 18.f;
  static constexpr float kFieldHeight = 30.f;
  static constexpr float kButtonHeight = 34.f;
  static constexpr float kButtonWidth = 92.f;     // largura minima dos botoes da barra (esticam para encher a linha)
  static constexpr float kButtonMaxWidth = 220.f;   // teto ao esticar um botao sozinho na linha
  static constexpr float kEditButtonWidth = 96.f;   // Confirmar e Cancelar, ao lado do campo de nome
  static constexpr float kAssignWidth = 120.f;      // Assign asset, na linha do titulo dos assets
  static constexpr float kRowHeight = 26.f;
  static constexpr std::size_t kMaxRows = 6;      // cenas visiveis (o resto rola depois)
  static constexpr std::size_t kMaxAssets = 4;    // assets visiveis
  static constexpr float kRemoveWidth = 28.f;
  static constexpr float kAssetNameWidth = 596.f;
  static constexpr float kFontSize = 12.f;
  static constexpr float kLabelSize = 12.f;
  static constexpr float kTitleSize = 12.f;   // titulo da moldura: mesmo tamanho das outras janelas
  static constexpr float kFieldInset = 8.f;
  static constexpr float kGripWidth = 14.f;   // mesmo valor de UIWindowComponent::gripWidth
  static constexpr float kHelpSize = 18.f;    // diametro do "?"
  static constexpr float kHelpGap = 4.f;      // entre grip, "?" e titulo
  static constexpr float kGearSize = 18.f;    // lado do icone de engrenagem ao lado do nome da cena
  static constexpr float kPopupWidth = 380.f; // largura da janelinha de config da cena
  static constexpr std::size_t kPopupMaxPresets = 5;  // configs visiveis na janelinha (alem de "Padrao do jogo")
  // Filhos do painel: rotulo, barra de botoes, linha do campo de nome (com Confirmar e Cancelar), status, lista de
  // cenas, linha do titulo dos assets (com Assign) e os assets.
  static constexpr std::size_t kChildren = 6 + kMaxAssets;
  static constexpr float kPanelHeight =
      2.f * kPadding + 2.f * kLabelHeight + 3.f * kButtonHeight +
      static_cast<float>(kMaxRows + kMaxAssets) * kRowHeight + static_cast<float>(kChildren - 1) * kGap;
};

using Colors = editor::file_panel_colors::Colors;
inline Colors PaletteColors() { return editor::file_panel_colors::FromPalette(); }

// Fundo "sem cor" dos grupos e rotulos: o visual padrao da SpriteUI e BRANCO opaco, entao todo no sem SetVisual
// (barras, linhas de agrupamento, rotulos) pintava branco. Transparente deixa ver o fundo do painel.
inline SU::Vec4 ClearTint() { return SU::Vec4{0.f, 0.f, 0.f, 0.f}; }

// Cor sRGB comum -> linear pre-multiplicado do renderizador SpriteUI.
inline SU::Vec4 Srgb(float r, float g, float b, float a = 1.f) {
  return SR::SrgbStraightToLinearPremultiplied({r, g, b, a});
}



// Entrada do quadro (ponteiro e foco da janela), ja lida do SDL pelo host.
struct FrameInput {
  float pointerX = 0.f, pointerY = 0.f;
  bool pointerDown = false;
  bool pointerPressed = false;
  bool pointerReleased = false;
  bool doubleClick = false;   // o Down deste quadro foi o segundo clique rapido (duplo clique)
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

// Janela Scenes (SpriteUI): lista de cenas, criar/renomear/apagar, assets da cena e status. So desenha e reporta Actions; o estado fica no Model.
class View {
 public:
  // O que o host deve fazer depois do quadro (salvar, abrir, apagar, ajuda, fechar...).
  // ScenePreset: o usuario escolheu a config de uma cena (engrenagem); o host le presetScene()/presetChoice() e chama
  // Editor::setScenePreset.
  enum class Action { None, Save, Reload, Open, Delete, Confirm, Assign, RemoveAsset, Help, Close, ScenePreset };

  View(Model& model, const sui::TextAtlasSet& text) : model_{model}, text_{text} { buildTree(); }

  // Calcula um quadro e devolve o pacote para desenhar. `actions` recebe o que foi clicado neste quadro.
  // Calcula um quadro (layout, cliques, hover, rolagem) e devolve o pacote de desenho. `actions` recebe o que foi clicado.
  SR::FramePacket Frame(float windowWidth, float windowHeight, const FrameInput& in, std::vector<Action>& actions) {
    frame_.refreshStyle();
    applyPalette();   // troca de tema vale ja neste quadro
    actions.clear();
    removed_ = kNone;
    if (windowWidth <= 0.f || windowHeight <= 0.f) return {};

    const float panelWidth = dock_ ? dock_->size.x : std::min(Theme::kPanelWidth, windowWidth - 32.f);
    const float frameHeight = Theme::kPanelHeight + frame_.metrics().headerHeight + 2.f * frame_.metrics().borderWidth;
    frame_.placeIn(windowWidth, windowHeight, dock_, panelWidth, frameHeight);   // posicao, dock e minimizar: moldura padrao
    handleFrameInput(in, actions);
    if (in.pointerPressed) doubleClick_ = in.doubleClick;   // vale ate a soltura (o clique sai no quadro da soltura)

    // Minimizada: so a moldura (cabecalho). O conteudo nao aparece nem recebe cliques.
    if (frame_.minimized()) {
      std::vector<SR::UIRenderItem> items;
      std::uint64_t paint = 1000;
      addFrame(items, paint);
      return extractor_.Build(in.frameId, items, nullptr, {});
    }

    const SU::Rect content = frame_.content();
    content_ = content;
    rows_ = SM::Rows(model_.scenes, model_.current);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, naturalH_ - content.size.y));
    listScroll_ = ui_scrollbar::clampScroll(listScroll_, listContentH(), listViewH());
    if (enterPending_) {   // Enter no campo de nome: mesmas regras do botao Confirmar
      enterPending_ = false;
      confirm(actions);
    }
    // Barras de rolagem (ui_scrollbar.hpp): a do painel e a da lista de cenas. Usam o trilho do quadro anterior; o clique
    // no polegar ou no trilho nao chega aos botoes de baixo, e o arraste segue com o mouse fora da janela.
    bool barUse = bar_.dragging() || listBar_.dragging();
    if (in.pointerPressed && listBar_.pointerDown(in.pointerX, in.pointerY, listScroll_)) barUse = true;
    else if (in.pointerPressed && bar_.pointerDown(in.pointerX, in.pointerY, scroll_)) barUse = true;
    if (bar_.dragging() && !in.pointerDown && !in.pointerPressed) bar_.pointerUp();
    else bar_.pointerMove(in.pointerX, in.pointerY, scroll_);   // arrasta, ou so atualiza o hover
    if (listBar_.dragging() && !in.pointerDown && !in.pointerPressed) listBar_.pointerUp();
    else listBar_.pointerMove(in.pointerX, in.pointerY, listScroll_);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, naturalH_ - content.size.y));
    listScroll_ = ui_scrollbar::clampScroll(listScroll_, listContentH(), listViewH());
    setPanelSize(content.size.x);   // altura natural: a barra de botoes quebra conforme a largura; o excesso rola
    tree_.Layout({{content.position.x, content.position.y - scroll_}, content.size});
    naturalH_ = rectOf(panelHandle_).size.y;
    bar_.layout({content.position.x + content.size.x - ui_scrollbar::kWidth, content.position.y, ui_scrollbar::kWidth, content.size.y},
                naturalH_, content.size.y);
    const SU::Rect lr = nodeRect(listBox_);
    listBar_.layout({lr.position.x + lr.size.x - ui_scrollbar::kWidth, lr.position.y, ui_scrollbar::kWidth, lr.size.y},
                    listContentH(), listViewH());
    listFirst_ = static_cast<std::size_t>(std::lround(listScroll_ / Theme::kRowHeight));

    // Engrenagem de cada cena + janelinha de config: tratados a mao (fora da arvore de layout). Se o clique foi deles,
    // o roteador da SpriteUI nao o recebe (nao seleciona a linha por baixo).
    if (gearAndPopup(in, actions)) barUse = true;

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
    // O Click sobe pelos pais (bubbling): cada clique chega aqui uma vez para o elemento tocado e de novo para a linha, a
    // lista e o painel. So o primeiro vale; os de cima tirariam o foco do campo de nome logo apos o clique que o abriu.
    for (const auto& event : events) {
      if (event.type != SU::Input::UIEventType::Click) continue;
      handleClick(event, actions);
      break;
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
    addScrollbar(extra, paint, bar_, scroll_);
    addScrollbar(extra, paint, listBar_, listScroll_);
    scene.renderItems.insert(scene.renderItems.end(), extra.begin(), extra.end());
    return extractor_.Build(in.frameId, scene.renderItems, nullptr, clips);
  }

  // Digitacao: so entra quando o campo de nome esta focado (criar ou renomear).
  void TypeText(const std::string& utf8) {
    if (!model_.nameFocus || model_.mode == NameMode::None) return;
    model_.name += utf8;
  }
  // Apaga o ultimo caractere (UTF-8) do nome, se o campo esta focado.
  void Backspace() {
    if (!model_.nameFocus || model_.mode == NameMode::None) return;
    SM::PopUtf8(model_.name);
  }

  // Enter no campo de nome: confirma criar ou renomear no proximo quadro (a acao Confirm sai em Frame).
  void PressEnter() {
    if (model_.mode == NameMode::New || model_.mode == NameMode::Rename) enterPending_ = true;
  }
  // Esc: cancela criar, renomear ou apagar. Devolve true se havia algo a cancelar.
  bool PressEscape() {
    if (model_.mode == NameMode::None) return false;
    SM::FinishName(model_);
    model_.status.clear();
    return true;
  }
  // Roda do mouse em (x, y): rola a lista de cenas quando o ponteiro esta sobre ela, senao o painel.
  void wheelAt(float x, float y, float dy) {
    if (listBar_.visible() && inside(nodeRect(listBox_), {x, y})) {
      listScroll_ = ui_scrollbar::clampScroll(listScroll_ + dy * (Theme::kRowHeight / 40.f), listContentH(), listViewH());
      return;
    }
    scrollBy(dy);
  }

  // Estado para testes e para o host.
  [[nodiscard]] std::size_t removedIndex() const { return removed_; }
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
  [[nodiscard]] const SW::WindowFrameMetrics& frameMetrics() const { return frame_.metrics(); }
  [[nodiscard]] SU::UIHandle rowHandle(std::size_t i) const { return rows_h_[i]; }
  [[nodiscard]] SU::UIHandle listHandle() const { return listBox_; }
  [[nodiscard]] std::size_t firstVisibleRow() const { return listFirst_; }   // indice (na lista ordenada) da primeira linha visivel
  [[nodiscard]] float listScroll() const { return listScroll_; }
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

  // ---- config por cena (engrenagem) ----
  // Pasta do projeto, para listar <projeto>/Configs. O host chama quando abre/troca de projeto.
  void setProjectDir(std::string dir) { projectDir_ = std::move(dir); }
  // Config atual de uma cena (vazio = padrao do jogo), so para marcar a escolha na janelinha. O host chama ao sincronizar.
  void setScenePreset(const std::string& scene, const std::string& path) { presets_[scene] = path; }
  // Cena e arquivo escolhidos, validos quando o quadro devolveu Action::ScenePreset (arquivo vazio = padrao do jogo).
  [[nodiscard]] const std::string& presetScene() const { return presetScene_; }
  [[nodiscard]] const std::string& presetChoice() const { return presetChoice_; }
  // Tooltip da janela: o host chama tooltip().draw(canvas, w, h) no fim do quadro (o View registra o alvo no Frame).
  editor::tooltip::Tooltip& tooltip() { return tip_; }
  // Janelinha de config aberta? (o host pode usar para esconder outros baloes)
  [[nodiscard]] bool presetPopupOpen() const { return popup_; }

 private:
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  // Roda do mouse: rola o painel quando o conteudo passa da janela.
 public:
  void scrollBy(float dy) { scroll_ = std::clamp(scroll_ + dy, 0.f, std::max(0.f, naturalH_ - content_.size.y)); }

 private:
  // Ponto dentro do retangulo (borda direita/inferior exclusiva).
  static bool inside(SU::Rect r, SU::Vec2 p) {
    return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y &&
           p.y < r.position.y + r.size.y;
  }


  // "?" logo depois do grip; o titulo comeca depois dele.
  SU::Rect helpRectNow() const { return frame_.helpRect(); }

  // ---------- Engrenagem e janelinha de config da cena ----------
  // Item da janelinha: kind 0 = "Padrao do jogo", 1 = config da pasta Configs, 2 = "Escolher arquivo...", 3 = "Fechar".
  struct PopItem {
    int kind = 0;
    SU::Rect r{};
    std::string label;   // texto mostrado
    std::string path;    // arquivo da config (so kind 1)
  };

  // Resultado do seletor de arquivo do SDL (a callback roda em outra thread: guarda o caminho sob mutex e o Frame le).
  struct PickSlot {
    std::mutex m;
    std::string path;
    bool ready = false;
  };
  static PickSlot& pickSlot() { static PickSlot s; return s; }
  static void SDLCALL onFilePicked(void*, const char* const* files, int) {
    if (!files || !*files) return;   // cancelado ou erro: nada a fazer
    PickSlot& s = pickSlot();
    std::lock_guard<std::mutex> lock(s.m);
    s.path = *files;
    s.ready = true;
  }

  // Dois caminhos apontam para o mesmo arquivo? (compara pelo sistema de arquivos; se falhar, compara o texto)
  static bool samePath(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return a == b;
    std::error_code ec;
    const bool eq = std::filesystem::equivalent(a, b, ec);
    return ec ? a == b : eq;
  }

  // Retangulo da engrenagem da linha visivel `i`: encostada a direita da linha, antes da barra de rolagem da lista.
  SU::Rect gearRect(std::size_t i) const {
    const SU::Rect rr = nodeRect(rows_h_[i]);
    const float barReserve = listBar_.visible() ? ui_scrollbar::kWidth + 4.f : 0.f;
    return {{rr.position.x + rr.size.x - barReserve - Theme::kGearSize - 6.f,
             rr.position.y + (rr.size.y - Theme::kGearSize) * 0.5f},
            {Theme::kGearSize, Theme::kGearSize}};
  }

  // Calcula a janelinha de config: retangulo, titulo e a lista de itens clicaveis (recalculada a cada quadro).
  void layoutPopup() {
    popItems_.clear();
    const float pad = 8.f, titleH = 24.f, rowH = Theme::kRowHeight, btnH = 30.f;
    const std::size_t n = std::min(presetList_.size(), Theme::kPopupMaxPresets);
    const float w = std::max(120.f, std::min(Theme::kPopupWidth, content_.size.x - 16.f));
    const float h = pad + titleH + static_cast<float>(1 + n) * rowH + pad + btnH + pad;
    const float x = content_.position.x + (content_.size.x - w) * 0.5f;
    float y = content_.position.y + 40.f;
    y = std::max(content_.position.y, std::min(y, content_.position.y + content_.size.y - h));
    popRect_ = {{x, y}, {w, h}};
    float cy = y + pad + titleH;
    popItems_.push_back({0, {{x + pad, cy}, {w - 2.f * pad, rowH}}, T("Game default"), {}});
    cy += rowH;
    for (std::size_t i = 0; i < n; ++i, cy += rowH)
      popItems_.push_back({1, {{x + pad, cy}, {w - 2.f * pad, rowH}}, presetList_[i].name, presetList_[i].path});
    cy += pad;
    const float bw = (w - 3.f * pad) * 0.5f;
    popItems_.push_back({2, {{x + pad, cy}, {bw, btnH}}, T("Choose file..."), {}});
    popItems_.push_back({3, {{x + 2.f * pad + bw, cy}, {bw, btnH}}, T("Close"), {}});
  }

  // Grava a escolha: lembra na janela e pede ao host (Action::ScenePreset) para gravar na cena.
  void choosePreset(const std::string& scene, const std::string& path, std::vector<Action>& actions) {
    presets_[scene] = path;
    presetScene_ = scene;
    presetChoice_ = path;
    actions.push_back(Action::ScenePreset);
  }

  // Engrenagens (tooltip, clique que abre a janelinha) e a janelinha (clique nos itens). Devolve true se consumiu o
  // ponteiro neste quadro (o roteador da SpriteUI nao deve ver esse clique).
  bool gearAndPopup(const FrameInput& in, std::vector<Action>& actions) {
    const SU::Vec2 p{in.pointerX, in.pointerY};
    // Tooltip: o tempo vem do relogio (a janela redesenha por eventos, entao limita o salto entre quadros).
    const auto now = std::chrono::steady_clock::now();
    const float dt = lastTipTime_.time_since_epoch().count() == 0
                         ? 0.f : std::min(0.25f, std::chrono::duration<float>(now - lastTipTime_).count());
    lastTipTime_ = now;
    tip_.beginFrame(in.pointerX, in.pointerY, dt);
    tipText_ = T("Add custom settings to this scene");   // vive ate o draw() do tooltip (membro)

    // Escolha feita no seletor de arquivo (outra thread): vale para a cena que abriu o seletor.
    {
      PickSlot& s = pickSlot();
      std::lock_guard<std::mutex> lock(s.m);
      if (s.ready) {
        s.ready = false;
        if (!pickScene_.empty()) choosePreset(pickScene_, s.path, actions);
        pickScene_.clear();
        popup_ = false;
      }
    }

    gearHover_ = kNone;
    if (!frame_.minimized() && !popup_) {
      for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
        if (listFirst_ + i >= rows_.size()) break;
        const SU::Rect g = gearRect(i);
        if (!inside(content_, {g.position.x + g.size.x * 0.5f, g.position.y + g.size.y * 0.5f})) continue;   // linha rolada para fora
        if (!inside(g, p)) continue;
        gearHover_ = i;
        tip_.target(g.position.x, g.position.y, g.size.x, g.size.y, tipText_.c_str());
        if (in.pointerPressed && model_.mode == NameMode::None) {   // abre a janelinha para esta cena
          popup_ = true;
          popupScene_ = rows_[listFirst_ + i].name;
          presetList_ = editor::config_preset::list(projectDir_);   // le a pasta Configs na hora de abrir
          return true;
        }
      }
    }
    if (!popup_) return false;

    tip_.suppress(true);   // com a janelinha aberta nao mostra balao por cima
    layoutPopup();
    popHover_ = kNone;
    for (std::size_t i = 0; i < popItems_.size(); ++i)
      if (inside(popItems_[i].r, p)) popHover_ = i;
    if (!in.pointerPressed) return inside(popRect_, p);   // o ponteiro sobre a janelinha nao age nas linhas de baixo
    if (!inside(popRect_, p)) { popup_ = false; return true; }   // clique fora fecha
    if (popHover_ == kNone) return true;
    const PopItem item = popItems_[popHover_];
    switch (item.kind) {
      case 0: choosePreset(popupScene_, {}, actions); popup_ = false; break;          // padrao do jogo
      case 1: choosePreset(popupScene_, item.path, actions); popup_ = false; break;   // config da pasta
      case 2: {   // seletor de arquivo (JSON); a resposta chega pela callback e e lida no proximo quadro
        pickScene_ = popupScene_;
        pickDefault_ = editor::config_preset::folder(projectDir_);
        static const SDL_DialogFileFilter kFilters[] = {{"Config (*.json)", "json"}};
        SDL_ShowOpenFileDialog(&onFilePicked, nullptr, nullptr, kFilters, 1, pickDefault_.c_str(), false);
        break;
      }
      default: popup_ = false; break;   // Fechar
    }
    return true;
  }

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

  // Cores do tema para a moldura, o conteudo e os botoes. Chamada na montagem e em cada desenho: a paleta
  // escolhida em Configuracoes > Appearance vale no quadro seguinte.
  void applyPalette() {
    frameColors_ = editor::window_palette::Standard();
    panelColor_ = frameColors_.body;
    colors_ = PaletteColors();
  }

  // ---------- Arvore de layout ----------
  // Cria a arvore de layout uma vez: rotulos, botoes, campo de nome, linhas da lista e assets.
  void buildTree() {
    SU::UIHandle& toolbar = toolbarH_;
    SU::UIHandle& editRow = editRowH_;
    SU::UIHandle& assetsHead = assetsHeadH_;
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
    // Barra de botoes: uma linha, todos com a mesma altura (quebra de linha so quando a janela estreita).
    toolbar = tree_.Create(panelHandle_, wrapRowStyle());
    new_ = tree_.Create(toolbar, buttonStyle(Theme::kButtonWidth));
    open_ = tree_.Create(toolbar, buttonStyle(Theme::kButtonWidth));
    rename_ = tree_.Create(toolbar, buttonStyle(Theme::kButtonWidth));
    delete_ = tree_.Create(toolbar, buttonStyle(Theme::kButtonWidth));
    save_ = tree_.Create(toolbar, buttonStyle(Theme::kButtonWidth));
    reload_ = tree_.Create(toolbar, buttonStyle(Theme::kButtonWidth));

    // Campo de nome com Confirmar e Cancelar na mesma linha.
    editRow = tree_.Create(panelHandle_, rowStyle(Theme::kButtonHeight));
    nameField_ = tree_.Create(editRow, assetNameStyle(Theme::kButtonHeight));
    confirm_ = tree_.Create(editRow, fixedButtonStyle(Theme::kEditButtonWidth));
    cancel_ = tree_.Create(editRow, fixedButtonStyle(Theme::kEditButtonWidth));
    status_ = tree_.Create(panelHandle_, labelStyle(Theme::kLabelHeight));

    // Lista de cenas: altura fixa (kMaxRows linhas); o excesso rola pela barra da lista.
    SU::FlexItem listStyle = rowStyle(static_cast<float>(Theme::kMaxRows) * Theme::kRowHeight);
    listStyle.direction = SU::LayoutDirection::Column;
    listStyle.gap = 0.f;   // linhas coladas: o espaco entre elas e a propria altura fixa
    listBox_ = tree_.Create(panelHandle_, listStyle);
    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) rows_h_[i] = tree_.Create(listBox_, labelStyle(Theme::kRowHeight));

    assetsHead = tree_.Create(panelHandle_, rowStyle(Theme::kButtonHeight));
    assetsLabel_ = tree_.Create(assetsHead, assetNameStyle(Theme::kButtonHeight));
    assign_ = tree_.Create(assetsHead, fixedButtonStyle(Theme::kAssignWidth));
    for (std::size_t i = 0; i < Theme::kMaxAssets; ++i) {
      assetRow_[i] = tree_.Create(panelHandle_, rowStyle(Theme::kRowHeight));
      assetName_[i] = tree_.Create(assetRow_[i], assetNameStyle(Theme::kRowHeight));
      assetRemove_[i] = tree_.Create(assetRow_[i], assetRemoveStyle());
    }

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
  // Estilo de botao com largura fixa e altura padrao.
  static SU::FlexItem buttonStyle(float width) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, width};
    s.height = {SU::SizeMode::Fixed, Theme::kButtonHeight};
    s.grow = 1.f;                                   // enche a linha quando sobra espaco (uma linha, um botao)
    s.maxSize = {Theme::kButtonMaxWidth, std::numeric_limits<float>::infinity()};
    s.shrink = 0.f;
    return s;
  }
  // Botao de largura fixa (nao estica), mesma altura dos outros botoes.
  static SU::FlexItem fixedButtonStyle(float width) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, width};
    s.height = {SU::SizeMode::Fixed, Theme::kButtonHeight};
    s.shrink = 0.f;
    return s;
  }
  // Campo que ocupa o espaco que sobra ao lado dos botoes da mesma linha (nome do asset, campo de nome, titulo).
  static SU::FlexItem assetNameStyle(float height) {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, 0.f};
    s.grow = 1.f;
    s.height = {SU::SizeMode::Fixed, height};
    s.shrink = 0.f;
    return s;
  }
  // Botao de remover asset (largura fixa).
  static SU::FlexItem assetRemoveStyle() {
    SU::FlexItem s;
    s.width = {SU::SizeMode::Fixed, Theme::kRemoveWidth};
    s.height = {SU::SizeMode::Fixed, Theme::kRowHeight};
    s.shrink = 0.f;
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
    s.gap = Theme::kGap;
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

  // Ajusta a largura do painel (altura do conteudo vem do layout).
  void setPanelSize(float width) {
    auto* el = tree_.Get(panelHandle_);
    if (!el) return;
    SU::FlexItem style = el->node.style;
    style.width.value = width;
    style.height = {SU::SizeMode::Auto, 0.f};
    tree_.SetStyle(panelHandle_, style);
  }

  // ---------- Texto ----------
  // Mede e posiciona o texto (glifos, caret) no tamanho de fonte da interface.
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

  // Corta o texto com "..." no fim para caber em `maxWidth` (nunca invade o que vem depois).
  Shaped shapeFit(const std::string& utf8, float size, float maxWidth) const {
    Shaped s = shape(utf8, size);
    if (maxWidth <= 0.f || s.layout.size.x <= maxWidth) return s;
    std::string cut = utf8;
    while (!cut.empty()) {
      SM::PopUtf8(cut);
      s = shape(cut + "...", size);
      if (s.layout.size.x <= maxWidth) return s;
    }
    return shape("...", size);
  }
  // Altura do conteudo da lista (todas as cenas) e da area visivel (kMaxRows linhas), para a barra da lista.
  float listContentH() const { return static_cast<float>(rows_.size()) * Theme::kRowHeight; }
  static float listViewH() { return static_cast<float>(Theme::kMaxRows) * Theme::kRowHeight; }

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

  // Habilita/desabilita botoes conforme selecao, cena atual e modo (criar/renomear/apagar) e pinta cada elemento.
  void applyVisuals() {
    enabled_.clear();
    // Painel, grupos (barra de botoes, linhas, caixa da lista, linhas de assets) e rotulos: nunca o branco padrao.
    {
      SU::ElementVisual pv;
      pv.tintPremultipliedLinear = panelColor_;
      pv.interactable = false;
      pv.clipOverflow = false;
      pv.zOrder = -1.f;
      bridge_.SetVisual(panelHandle_, pv);
      SU::ElementVisual gv;
      gv.tintPremultipliedLinear = ClearTint();
      gv.interactable = false;
      gv.clipOverflow = false;
      gv.zOrder = -0.5f;
      for (const SU::UIHandle g : {toolbarH_, editRowH_, listBox_, assetsHeadH_, assetRow_[0], assetRow_[1], assetRow_[2], assetRow_[3]})
        bridge_.SetVisual(g, gv);
      SU::ElementVisual lv = gv;   // rotulos so de texto
      lv.zOrder = 0.f;
      for (const SU::UIHandle l : {label_, status_, assetsLabel_}) bridge_.SetVisual(l, lv);
    }
    const SM::Actions acts = SM::ActionsFor(model_.selected, model_.current);
    const bool confirming = model_.mode != NameMode::None;
    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
      const std::size_t ri = listFirst_ + i;
      const bool has = ri < rows_.size();
      SU::ElementVisual v;
      v.interactable = has;
      v.clipOverflow = false;
      v.zOrder = 0.f;
      // Selecionada: destaque da selecao; passar o mouse: tom de campo em foco; resto: campo.
      v.tintPremultipliedLinear = has && rows_[ri].name == model_.selected ? colors_.rowSelected
                                  : has && hovered_ == rows_h_[i]          ? colors_.fieldFocus
                                                                           : colors_.field;
      bridge_.SetVisual(rows_h_[i], v);
    }
    paintButton(new_, !confirming, colors_.button);
    paintButton(open_, acts.open && !confirming, colors_.button);
    paintButton(rename_, acts.rename && !confirming, colors_.button);
    paintButton(delete_, acts.remove && !confirming, colors_.buttonDanger);
    paintButton(save_, true, colors_.button);
    paintButton(reload_, true, colors_.button);
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
      name.tintPremultipliedLinear = i < model_.assets.size() ? colors_.field : ClearTint();   // mesma cor das linhas da lista
      bridge_.SetVisual(assetName_[i], name);
      paintButton(assetRemove_[i], i < model_.assets.size(), colors_.buttonDanger);
    }
  }

  // Clique em um elemento: foca o nome, escolhe linha da lista, remove asset ou dispara o botao correspondente (Action).
  void handleClick(const SU::Input::UIEvent& event, std::vector<Action>& actions) {
    if (event.target == nameField_) {   // so foca quando ha criar ou renomear em curso
      model_.nameFocus = model_.mode == NameMode::New || model_.mode == NameMode::Rename;
      return;
    }
    // Outro clique tira o foco do campo, mas com criar/renomear em curso o campo continua aberto: o foco volta ao clicar nele.
    model_.nameFocus = false;

    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
      if (event.target != rows_h_[i]) continue;
      const std::size_t ri = listFirst_ + i;
      if (ri < rows_.size() && model_.mode == NameMode::None) {   // com criar/renomear/apagar em curso a selecao fica
        model_.selected = rows_[ri].name;
        model_.status.clear();   // o aviso anterior (ex.: "ja esta aberta") era sobre a selecao antiga
      }
      // Duplo clique abre a cena (mesmas regras do botao Open: nunca a atual, nunca com confirmacao em curso).
      if (ri < rows_.size() && doubleClick_ && model_.mode == NameMode::None && SM::ActionsFor(model_.selected, model_.current).open) {
        actions.push_back(Action::Open);
      }
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
    namespace UH = editor::usability;
    // Botao bloqueado: em vez de nao fazer nada, mostra o motivo na linha de aviso da janela.
    const auto blocked = [&](UH::SceneAct act) {
      if (const char* why = UH::SceneActionBlockedReason(act, model_.selected, model_.current)) model_.status = why;
    };
    if (event.target == open_) {
      if (acts.open) actions.push_back(Action::Open);
      else blocked(UH::SceneAct::Open);
      return;
    }
    if (event.target == rename_) {
      if (acts.rename) SM::BeginName(model_, NameMode::Rename);
      else blocked(UH::SceneAct::Rename);
      return;
    }
    if (event.target == delete_) {   // apagar pede confirmacao antes
      if (acts.remove) SM::BeginDelete(model_);
      else blocked(UH::SceneAct::Delete);
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
      model_.nameFocus = true;   // o campo continua em edicao: corrige o nome sem clicar de novo
      return;
    }
    model_.status.clear();
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
    const Shaped title = shape(T("Scenes"), Theme::kTitleSize);
    const float tx = help.position.x + help.size.x + Theme::kHelpGap;
    const SU::Rect bar = frame_.header();
    const float ty = bar.position.y + (bar.size.y - title.layout.lineHeight) * 0.5f;
    text(panelHandle_, title, {tx, ty}, colors_.text, 0.95f);
    text(panelHandle_, title, {tx + 0.7f, ty}, colors_.text, 0.95f);
  }

  // Conteudo: textos, botoes, campo de nome e assets. Usa o no de cada elemento como dono.
  // Barra de rolagem unica do editor: so aparece quando o conteudo passa da janela.
  // Desenha uma barra de rolagem (a do painel ou a da lista) com o deslocamento dela.
  void addScrollbar(std::vector<SR::UIRenderItem>& out, std::uint64_t& paint, const ui_scrollbar::Scrollbar& bar, float scroll) {
    for (const ui_scrollbar::Quad& q : bar.quads(palette::active(), scroll)) {
      if (q.rect.w <= 0.f || q.rect.h <= 0.f) continue;
      SR::UIRenderItem item;
      item.handle = panelHandle_;
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

  // Gera os itens de desenho do conteudo: textos, botoes, campo de nome com cursor e lista de assets.
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
    // Texto centralizado no botao (cortado com "..." se nao cabe); apagado quando o botao esta desabilitado.
    auto button = [&](SU::UIHandle h, const std::string& label) {
      const SU::Rect r = nodeRect(h);
      const Shaped s = shapeFit(label, Theme::kFontSize, r.size.x - 2.f * Theme::kGap);
      text(h, s, {r.position.x + (r.size.x - s.layout.size.x) * 0.5f, r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f},
           isEnabled(h) ? colors_.text : colors_.textDim, 0.9f);
    };
    // Texto alinhado a esquerda, centrado na altura da linha, cortado com "..." antes de `reserve` px do fim.
    auto line = [&](SU::UIHandle h, const std::string& label, SU::Vec4 color, float size = Theme::kLabelSize, float reserve = 0.f) {
      const SU::Rect r = nodeRect(h);
      const Shaped s = shapeFit(label, size, r.size.x - 2.f * Theme::kFieldInset - reserve);
      text(h, s, {r.position.x + Theme::kFieldInset, r.position.y + (r.size.y - s.layout.lineHeight) * 0.5f}, color, 0.9f);
    };

    line(label_, T("Current scene: ") + (model_.current.empty() ? T("(none)") : model_.current), colors_.textDim);

    button(new_, T("New scene..."));
    button(open_, T("Open"));
    button(rename_, T("Rename..."));
    button(delete_, T("Delete"));
    button(save_, T("Save scene"));
    button(reload_, T("Reload list"));
    button(assign_, T("Assign asset"));
    button(confirm_, model_.mode == NameMode::New      ? T("Create scene")
                     : model_.mode == NameMode::Rename ? T("Rename scene")
                     : model_.mode == NameMode::Delete ? T("Delete scene")
                                                       : T("Confirm"));
    button(cancel_, T("Cancel"));

    // Lista de cenas: nome e "(open)" na cena atual (em cor de destaque); o texto nunca passa da barra da lista.
    const float barReserve = listBar_.visible() ? ui_scrollbar::kWidth + 4.f : 0.f;
    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
      const std::size_t ri = listFirst_ + i;
      if (ri >= rows_.size()) break;
      // Reserva tambem o espaco da engrenagem (a direita do nome) para o texto nao passar por baixo dela.
      line(rows_h_[i], rows_[ri].current ? rows_[ri].name + T(" (open)") : rows_[ri].name,
           rows_[ri].current ? colors_.help : colors_.text, Theme::kLabelSize + 1.f, barReserve + Theme::kGearSize + 10.f);
    }

    // Engrenagem: dentes (barras e quadrados nas diagonais) + disco + furo na cor da linha. Mais clara no hover.
    auto gear = [&](SU::Rect g, SU::Vec4 col, SU::Vec4 bg) {
      const float s = g.size.x, cx = g.position.x + s * 0.5f, cy = g.position.y + s * 0.5f;
      const float t = s * 0.26f;   // espessura dos dentes
      solid(panelHandle_, {{g.position.x, cy - t * 0.5f}, {s, t}}, col, 0.8f);   // dentes horizontais
      solid(panelHandle_, {{cx - t * 0.5f, g.position.y}, {t, s}}, col, 0.8f);   // dentes verticais
      const float d = s * 0.30f;   // distancia dos dentes diagonais ao centro
      for (const float sx : {-1.f, 1.f})
        for (const float sy : {-1.f, 1.f})
          solid(panelHandle_, {{cx + sx * d - t * 0.5f, cy + sy * d - t * 0.5f}, {t, t}}, col, 0.8f);
      const float bd = s * 0.72f, hd = s * 0.30f;   // disco do corpo e furo central
      auto body = editor::window_help_icon::BuildCircleItems(panelHandle_, {{cx - bd * 0.5f, cy - bd * 0.5f}, {bd, bd}}, col, 0.85f, paint);
      out.insert(out.end(), body.begin(), body.end());
      auto hole = editor::window_help_icon::BuildCircleItems(panelHandle_, {{cx - hd * 0.5f, cy - hd * 0.5f}, {hd, hd}}, bg, 0.9f, paint);
      out.insert(out.end(), hole.begin(), hole.end());
    };
    for (std::size_t i = 0; i < Theme::kMaxRows; ++i) {
      const std::size_t ri = listFirst_ + i;
      if (ri >= rows_.size()) break;
      const SU::Rect g = gearRect(i);
      if (!inside(content_, {g.position.x + g.size.x * 0.5f, g.position.y + g.size.y * 0.5f})) continue;   // linha rolada para fora
      const SU::Vec4 bg = rows_[ri].name == model_.selected ? colors_.rowSelected : hovered_ == rows_h_[i] ? colors_.fieldFocus : colors_.field;
      gear(g, gearHover_ == i ? colors_.text : colors_.textDim, bg);
    }

    // Nenhuma cena: orienta a criar (a lista ficava em branco).
    if (rows_.empty()) {
      if (const char* hint = editor::usability::ScenesEmptyHint(0)) line(rows_h_[0], T(hint), colors_.textDim, Theme::kLabelSize);
    }

    // Campo de nome: placeholder quando vazio, caret quando focado. Em apagar, mostra a pergunta (sem edicao).
    const SU::Rect fr = nodeRect(nameField_);
    if (model_.mode == NameMode::Delete) {
      const Shaped q = shapeFit(T("Delete '") + model_.selected + T("'? This cannot be undone."), Theme::kFontSize,
                                fr.size.x - 2.f * Theme::kFieldInset);
      text(nameField_, q, {fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - q.layout.lineHeight) * 0.5f},
           colors_.warning, 0.9f);
    } else {
      const bool placeholder = model_.name.empty();
      const std::string shown = placeholder ? (model_.mode == NameMode::None ? T("Click New scene... or Rename...") : T("Scene name"))
                                            : model_.name;
      const Shaped fs = shapeFit(shown, Theme::kFontSize, fr.size.x - 2.f * Theme::kFieldInset - 4.f);
      const SU::Vec2 origin{fr.position.x + Theme::kFieldInset, fr.position.y + (fr.size.y - fs.layout.lineHeight) * 0.5f};
      if (model_.nameFocus && fs.utf8 == shown) {   // sem caret quando o texto foi cortado com "..."
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
      const Shaped name = shapeFit(model_.assets[i], Theme::kLabelSize, r.size.x - 2.f * Theme::kFieldInset - 70.f);
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

    if (!model_.status.empty()) line(status_, T(model_.status.c_str()), colors_.warning);   // T: as mensagens do modelo vem em ingles (chave)

    // Janelinha de config da cena (por cima de tudo): titulo, "Padrao do jogo", configs da pasta, Escolher arquivo, Fechar.
    if (popup_) {
      const float pad = 8.f;
      solid(panelHandle_, {{popRect_.position.x - 1.f, popRect_.position.y - 1.f}, {popRect_.size.x + 2.f, popRect_.size.y + 2.f}},
            colors_.textDim, 1.1f);   // contorno
      solid(panelHandle_, popRect_, panelColor_, 1.2f);   // fundo
      const Shaped title = shapeFit(std::string(T("Scene config")) + ": " + popupScene_, Theme::kFontSize, popRect_.size.x - 2.f * pad);
      text(panelHandle_, title, {popRect_.position.x + pad, popRect_.position.y + pad + (24.f - title.layout.lineHeight) * 0.5f},
           colors_.text, 1.5f);
      const auto cur = presets_.find(popupScene_);
      // Config atual da cena (vazio = padrao). O valor guardado pode ser relativo ao projeto: vira absoluto para comparar.
      const std::string current = cur == presets_.end() ? std::string{}
                                                        : editor::config_preset::absoluteFromStored(projectDir_, cur->second);
      for (std::size_t i = 0; i < popItems_.size(); ++i) {
        const PopItem& it = popItems_[i];
        const bool isButton = it.kind >= 2;
        const bool active = it.kind == 0 ? current.empty() : it.kind == 1 && samePath(current, it.path);
        const bool hot = popHover_ == i;
        const SU::Vec4 bg = isButton ? (hot ? colors_.buttonHover : colors_.button)
                                     : active ? colors_.rowSelected : (hot ? colors_.fieldFocus : colors_.field);
        solid(panelHandle_, it.r, bg, 1.3f);
        const std::string label = active ? "> " + it.label : it.label;   // "> " marca a config em uso
        const Shaped s = shapeFit(label, Theme::kFontSize, it.r.size.x - 2.f * Theme::kGap);
        const float tx = isButton ? it.r.position.x + (it.r.size.x - s.layout.size.x) * 0.5f : it.r.position.x + Theme::kFieldInset;
        text(panelHandle_, s, {tx, it.r.position.y + (it.r.size.y - s.layout.lineHeight) * 0.5f},
             active ? colors_.help : colors_.text, 1.5f);
      }
    }
  }

  Model& model_;
  const sui::TextAtlasSet& text_;
  Colors colors_;
  SU::Vec4 panelColor_{};

  SU::LayoutTree tree_;
  SU::LayoutSceneBridge bridge_;
  SU::Input::UIEventRouter router_;
  SR::RenderExtractor extractor_;

  SU::UIHandle toolbarH_{}, editRowH_{}, assetsHeadH_{};
  SU::UIHandle panelHandle_{}, label_{}, save_{}, new_{}, reload_{}, open_{}, rename_{}, delete_{}, assign_{};
  SU::Rect content_{};        // area visivel do conteudo (abaixo do cabecalho)
  ui_scrollbar::Scrollbar bar_;
  ui_scrollbar::Scrollbar listBar_;   // barra da lista de cenas (mais de kMaxRows cenas)
  SU::UIHandle listBox_{};
  float listScroll_ = 0.f;            // deslocamento da lista em pixels (linhas inteiras no desenho)
  std::size_t listFirst_ = 0;         // primeira cena visivel
  bool enterPending_ = false;         // Enter apertado: confirma o nome no proximo quadro
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
  bool doubleClick_ = false;

  // Config por cena (engrenagem + janelinha).
  std::string projectDir_;                      // pasta do projeto (lista <projeto>/Configs)
  std::map<std::string, std::string> presets_;  // cena -> arquivo de config escolhido (vazio = padrao do jogo)
  editor::tooltip::Tooltip tip_;                // balao da engrenagem (o host o desenha)
  std::string tipText_;                         // texto do balao: precisa viver ate o draw()
  std::chrono::steady_clock::time_point lastTipTime_{};
  std::size_t gearHover_ = kNone;               // linha visivel cuja engrenagem esta sob o mouse
  bool popup_ = false;                          // janelinha de config aberta
  std::string popupScene_;                      // cena da janelinha
  SU::Rect popRect_{};
  std::vector<PopItem> popItems_;
  std::size_t popHover_ = kNone;
  std::vector<editor::config_preset::Entry> presetList_;   // configs da pasta, lidas ao abrir a janelinha
  std::string pickScene_;                       // cena que abriu o seletor de arquivo (aguardando a resposta)
  std::string pickDefault_;                     // pasta inicial do seletor (precisa viver durante a chamada)
  std::string presetScene_, presetChoice_;      // ultima escolha (lida pelo host com Action::ScenePreset)

  // Moldura: cores (SpriteUI, definidas em buildTree) e geometria padrao (window_frame::Frame).
  SW::WindowFrameColors frameColors_{};
  window_frame::Frame frame_;
  std::optional<SU::Rect> dock_;
};

}  // namespace editor::scenesui
