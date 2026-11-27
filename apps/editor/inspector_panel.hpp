#pragma once

// Inspetor em SpriteUI (parte dos componentes). Cada componente do objeto selecionado vira um grupo (Panel) com
// cabecalho clicavel; o estado de aberto e fechado fica no modelo (inspector_model.hpp). Os controles sao os da
// biblioteca: Slider, Checkbox, Dropdown, ColorPicker, TextField e Button. Os valores vem dos componentes do objeto
// e so mudam em acao do usuario (clique, arrasto ou digitacao). Desenhar e ler nao escrevem nada.
//
// Secoes em SpriteUI: nome, ativo e travado; Transform; Mesh Renderer (com as partes de modelo importado); Light
// source; Global Light (sun); Camera (com o pos-processamento); Audio Source; Physics; Tools; Voxel; Canvas (game UI); Particles; ToolObject Script
// e Tags. Recursos (Add / Remove) entram neste painel.
//
// Exemplo: a Luz do sol tem cor, intensidade e sombra. Clicar na amostra da cor abre o seletor; arrastar a area
// de saturacao muda a cor do objeto na hora. Digitar "2,5" na intensidade grava 2.5 quando o numero e valido.
//
// Como funciona:
//   prepare:     monta a lista de controles do quadro (posicoes locais ao bloco) e devolve a altura. O main.cpp
//                chama antes de draw.
//   draw:        desenha grupos, controles, campo em edicao e listas abertas, recortados na parte visivel da janela.
//   handleEvent: roteia clique, arrasto, teclas e texto.

#include "theme_colors.hpp"
#include "SpriteUi/Elements/Button.hpp"
#include "SpriteUi/Elements/Checkbox.hpp"
#include "SpriteUi/Elements/Dropdown.hpp"
#include "SpriteUi/Elements/Panel.hpp"
#include "SpriteUi/Elements/Slider.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"
#include "SpriteUi/Widgets/ColorPicker.hpp"
#include "SpriteUi/Widgets/TextField.hpp"

#include "components.hpp"
#include "canvas_elems_model.hpp"
#include "framed_window.hpp"
#include "i18n.hpp"
#include "inspector_bridge.hpp"
#include "color_popup.hpp"
#include "inspector_model.hpp"
#include "project.hpp"
#include "text_edit_keys.hpp"
#include "slider_value_edit.hpp"
#include "sui_panel.hpp"
#include "ui_scrollbar.hpp"
#include "ui_style.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor::inspector_panel {

namespace im = editor::inspector_model;
namespace hm = editor::hierarchy_model;
namespace ib = editor::inspector_bridge;
namespace SU = ToolEngine::SpriteUi;
namespace Elm = SU::Elements;
namespace Wd = SU::Widgets;
namespace Edit = SU::Widgets;
namespace SR = SU::Rendering;
namespace ST = SU::Text;

// Dados do objeto que o painel le. Os ponteiros valem so no quadro; o main.cpp monta a cada quadro.
struct Context {
    bool hasObject = false;
    bool active = true;
    bool locked = false;
    unsigned entityIndex = 0;
    unsigned entityGeneration = 0;
    Name* name = nullptr;
    Transform* transform = nullptr;
    MeshRenderer* mesh = nullptr;
    PartOverrides* overrides = nullptr;
    std::vector<std::pair<int, std::string>> parts;   // (indice da parte, nome) do no importado do modelo
    LightComp* light = nullptr;
    GlobalLight* global = nullptr;
    CameraComp* camera = nullptr;
    bool principal = false;
    std::size_t principalCount = 0;
    bool splitVertical = true;
    AudioSource* audio = nullptr;
    std::vector<std::string> tagNames;   // todas as tags da cena (indice = id da tag)
    std::vector<bool> tagOn;             // quais o objeto tem
    std::vector<bool> tagBuiltin;        // tags embutidas: nao podem ser renomeadas
    ScriptComp* script = nullptr;        // script do objeto (nullptr: nao tem)
    std::string scriptPath;              // caminho relativo do script ("" se nao tem arquivo)
    bool playing = false;                // Play ligado: mostra o status do script
    std::string scriptStatus;            // status vindo do ScriptHost (so com Play)
    bool scriptRuntime = true;           // a build tem o runtime de script (QuickJS)
    PhysicsComp* physics = nullptr;      // fisica do objeto (nullptr: nao tem)
    float worldGravity[3] = {0.f, -9.81f, 0.f};   // gravidade da cena (mostrada quando o objeto nao tem a sua)
    std::string playPhysics;             // velocidade e estado no Play (vazio fora do Play)
    ToolsComp* tools = nullptr;          // ferramentas prontas do objeto (nullptr: nao tem)
    bool hasPhysics = false;             // o objeto tem Physics (algumas ferramentas precisam)
    VoxelChunk* voxel = nullptr;         // blocos do objeto (nullptr: nao tem)
    unsigned voxelSolid = 0;             // blocos preenchidos no chunk
    CanvasComp* cv = nullptr;            // Canvas (UI de jogo) do objeto (nullptr: nao tem)
    UiElem* uiElem = nullptr;            // elemento de UI selecionado na Hierarchy: o Inspector mostra so ele
    int uiIndex = -1;                    // posicao do elemento na lista do canvas
    std::string uiCanvasName;            // nome do Canvas dono do elemento
    std::vector<std::string> cameraNames;   // cameras da cena (para escolher onde a UI aparece)
    ParticleComp* particles = nullptr;   // particulas do objeto (nullptr: nao tem)
    std::vector<int> addable;            // recursos (im::Resource) que o objeto ainda pode receber
    std::vector<int> removable;          // recursos (im::Resource) que o objeto tem
};

// Acoes com efeito fora do objeto (o Editor, no main.cpp).
struct Hooks {
    std::function<std::vector<std::string>(editor::AssetKind)> assets;        // caminhos relativos ao projeto
    std::function<void(editor::AssetKind, const std::string&)> assetChosen;     // assignAsset + invalidate
    std::function<void(const std::string&)> previewSound;
    std::function<void()> stopSound;
    std::function<bool(const std::string&)> hasNamed;                      // existe objeto com esse nome
    std::function<void(bool)> setActive;
    std::function<void(bool)> setLocked;
    std::function<void(bool)> setPrincipal;
    std::function<void(int, bool)> setPartOverride;                        // parte, ligado
    std::function<void(int, const std::string&)> setPartTexture;           // textura de albedo, por parte
    std::function<void(bool)> setSplitVertical;
    std::function<bool(const std::string&)> addTagNamed;           // cria (se preciso) e atribui; false: limite de tags
    std::function<void(std::size_t)> addTagIndex;                  // atribui uma tag existente
    std::function<void(std::size_t)> removeTagIndex;               // tira uma tag do objeto
    std::function<bool(std::size_t, const std::string&)> renameTagIndex;   // renomeia a tag na cena; false: nome invalido
    std::function<void()> openScriptHelp;                          // "Help: script examples"
    std::function<void()> addScript;                               // cria o arquivo de script do objeto
    std::function<void(const std::string&)> openScriptExternal;    // abre o arquivo no editor de codigo
    std::function<void()> openScriptWindow;                        // abre a janela de edicao do script
    std::function<void()> reloadScripts;                           // reinicia os scripts da cena (durante o Play)
    std::function<void()> removePhysics;                           // tira a fisica do objeto
    std::function<std::vector<std::string>(int, int)> actionNames; // acoes do Input Map: (jogador 1..8, tipo 0 botao / 1 eixo)
    std::function<void()> openToolsHelp;                           // "Help: JS library"
    std::function<void(std::size_t)> removeTool;                   // tira a ferramenta da lista do objeto
    std::function<void(int, int, int, int)> voxelPaint;            // pinta um bloco: x, camada, z, tipo do pincel
    std::function<void()> voxelFill;                               // preenche o terreno
    std::function<void()> voxelClear;                              // limpa o chunk
    std::function<void()> removeUi;                                // tira a UI de jogo do objeto
    std::function<std::vector<std::string>()> uiImages;            // imagens do projeto para elementos de UI (so PNG e JPEG)
    std::function<void()> smokePreset;                             // "Smoke preset": valores de fumaca nas particulas
    std::function<void()> removeParticles;                         // tira as particulas do objeto
    std::function<void(int)> addResource;                          // adiciona um recurso (im::Resource)
    std::function<void(int)> removeResource;                       // tira um recurso (im::Resource)
};

// Metricas do painel, em pixels logicos.
struct Metrics {
    static constexpr float kPad = sui::kPanelPadding;
    static constexpr float kIndent = 12.f;
    static constexpr float kHeadH = 24.f;
    static constexpr float kRowH = 24.f;
    static constexpr float kGap = 4.f;
    static constexpr float kFont = 12.f;
    static constexpr float kSmallFont = 11.f;
    static constexpr float kLabelMax = 118.f;
    static constexpr float kSwatchW = 56.f;
    static constexpr float kValueW = 58.f;
    static constexpr float kCheckBox = 18.f;
    static constexpr float kCheckGap = 8.f;
    static constexpr float kButtonW = 110.f;
    static constexpr float kHelpW = 160.f;       // "Help: script examples"
    static constexpr float kEditW = 110.f;       // "Edit script", "Reload scripts"
    static constexpr float kExternalW = 120.f;   // "Edit external"
    static constexpr float kRemoveW = 140.f;     // "Remove Physics"
    static constexpr float kAxisW = 14.f;
    static constexpr float kPopupW = 232.f;
    static constexpr float kPopupH = 210.f;
    static constexpr float kZFrame = 0.10f;
    static constexpr float kZFill = 0.20f;
    static constexpr float kZField = 0.28f;
    static constexpr float kZControl = 0.30f;
    static constexpr float kZPopup = 0.95f;
    static constexpr float kZPopupText = 0.97f;
};

// Painel do Inspector: a cada quadro monta a lista de controles (prepare), desenha grupos/controles/popups (draw) e roteia mouse, teclas e texto (handleEvent). So muda os dados do objeto por acao do usuario.
class InspectorPanel {
  public:
    InspectorPanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : canvas_{text}, overlay_{text}, sdl_{sdl}, chrome_{text, sdl} {
        overlay_.setTextZ(Metrics::kZPopupText);
        chrome_.setBodyFill(false);   // o corpo e desenhado pelo painel, sem fundo da moldura
    }
    InspectorPanel(const InspectorPanel&) = delete;
    // Moldura SpriteUI (titulo, X, "?", grip e acoplamento). O corpo e o bloco do Inspector posicionado sobre ela.
    framed::Chrome& chrome() { return chrome_; }
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    // Monta os controles do quadro e devolve a altura do bloco (em pixels, sem a janela). Chamar antes de draw.
    // Ler o objeto aqui nao muda nada.
    float prepare(const Context& ctx, const Hooks& hooks, float width) {
        ctx_ = ctx;             // copia: os ponteiros dentro dele valem neste quadro, e o painel os usa em draw e eventos
        hooks_ = hooks;
        canvas_.setFontScale(editor::ui_style::textScale());
        overlay_.setFontScale(editor::ui_style::textScale());
        width_ = std::max(140.f, width);
        controls_.clear();
        clampScroll();   // limite calculado no quadro anterior (o corpo visivel e o conteudo ja medido)
        frames_.clear();
        canvas_.clear();
        overlay_.clear();
        labelW_ = std::min(Metrics::kLabelMax, (width_ - 2.f * Metrics::kPad - Metrics::kIndent) * 0.42f);
        innerX_ = Metrics::kPad + Metrics::kIndent;
        ctrlX_ = innerX_ + labelW_;
        ctrlW_ = std::max(60.f, width_ - Metrics::kPad - ctrlX_);
        keySeq_ = 1;
        disabled_ = ctx.locked;
        y_ = Metrics::kPad - scrollY_;   // a rolagem sobe o conteudo; o recorte do corpo esconde o que sai
        if (!ctx.hasObject) {
            closePopups();
            canvas_.addText(Metrics::kPad, y_, T("no object selected"), Metrics::kFont, theme_.textDim, width_ - 2.f * Metrics::kPad);
            y_ += Metrics::kRowH;
            contentH_ = y_ + scrollY_;
            addScrollbar();
            return y_ + Metrics::kPad;
        }
        const unsigned id = ((ctx.entityGeneration << 20) ^ ctx.entityIndex) ^ (static_cast<unsigned>(ctx.uiIndex + 1) * 0x9E3779B1u);
        if (id != lastObject_) {   // outro objeto: fecha listas e campo em edicao
            lastObject_ = id;
            closePopups();
            endEdit();
            tagMsg_.clear();
            renaming_ = -1;
        }
        if (ctx.uiElem) {   // elemento de UI selecionado: so as propriedades dele
            uiElemSection();
            contentH_ = y_ + scrollY_ + Metrics::kPad;
            addScrollbar();
            return y_ + Metrics::kPad;
        }
        header();
        if (ctx.transform) transform();
        if (ctx.mesh) mesh();
        if (ctx.light) light();
        if (ctx.global) global();
        if (ctx.camera) camera();
        if (ctx.audio) audio();
        if (ctx.physics) physics();
        if (ctx.tools) tools();
        if (ctx.voxel) voxel();
        if (ctx.cv) canvasSection();
        if (ctx.particles) particles();
        resources();
        script();
        tags();
        contentH_ = y_ + scrollY_ + Metrics::kPad;   // altura do conteudo sem a rolagem, com a folga de baixo
        addScrollbar();
        return y_ + Metrics::kPad;
    }

    // Abre ou fecha todas as secoes.
    void setAllSections(bool open) { expanders_.SetAll(open); }

    // Rolagem: a roda do mouse sobre o corpo move o conteudo. Limite = altura do conteudo menos o corpo visivel.
    void scrollBy(float dy) {
        scrollY_ += dy;
        clampScroll();
    }
    [[nodiscard]] float scrollY() const { return scrollY_; }

    // Barra de rolagem unica do editor (ui_scrollbar.hpp), no canto direito do corpo; some quando o conteudo cabe.
    // Coordenadas locais do canvas.
    void addScrollbar() {
        const float top = visible_.y - origin_.y;
        bar_.layout({width_ - ui_scrollbar::kWidth - 1.f, top, ui_scrollbar::kWidth, visible_.h}, contentH_, visible_.h);
        for (const ui_scrollbar::Quad& q : bar_.quads(palette::active(), scrollY_)) {
            if (q.rect.w > 0.f && q.rect.h > 0.f) canvas_.addSolid(q.rect.x, q.rect.y, q.rect.w, q.rect.h, sui::PaletteColor(q.color), Metrics::kZField);
        }
    }

    // Recalcula as cores a partir da paleta em uso (trocar o tema repinta no quadro seguinte).
    void refreshColors() {
      const editor::theme_colors::Colors t = editor::theme_colors::Active();
      headerColor_ = sui::PaletteColor(t.headerActive);
      headingColor_ = sui::PaletteColor(t.heading);
      fieldColor_ = sui::PaletteColor(t.field);
      fieldFocusColor_ = sui::PaletteColor(t.fieldFocus);
      fieldDisabled_ = sui::PaletteColor(palette::mixRgb(t.panel, t.field, 0.5f));
      selectionColor_ = sui::PaletteColor(t.selection);
      swatchBorder_ = sui::PaletteColor(t.border);
    }

    // Desenha o bloco. origin = canto superior esquerdo do bloco na tela; visible = area de recorte (corpo da moldura)
    // (o bloco e cortado por esse retangulo).
    bool draw(SDL_Renderer* renderer, sui::Box origin, sui::Box visible, bool shown) {
      theme_ = sui::Theme{};   // cores da paleta em uso (Configuracoes > Appearance)
      refreshColors();
        origin_ = origin;
        visible_ = visible;
        shown_ = shown && visible.w > 0.f && visible.h > 0.f;   // sem objeto, o proprio bloco mostra o aviso
        if (!shown_) return true;
        const float localTop = visible_.y - origin_.y;
        const SU::Rect window{{0.f, localTop}, {width_, visible_.h}};
        for (Control& c : controls_) {
            if (c.kind == Kind::Drop) drops_[c.key].Layout(window);
        }
        std::vector<SR::ClipRectGPU> clips{{visible_.x, visible_.y, visible_.x + visible_.w, visible_.y + visible_.h}};
        const SU::UIHandle owner = sui::handleFor(0);
        std::vector<SR::UIRenderItem> items;
        std::uint64_t paint = 1;

        for (const SU::Rect& f : frames_) {
            Elm::Panel frame;
            frame.border = f;
            append(items, paint, Elm::BuildPanelItems(frame, frameStyle_, Metrics::kZFrame, paint));
        }
        canvas_.appendItems(items, origin_.x, origin_.y, 0.f, visible_.y, visible_.y + visible_.h, owner, 0u, paint);
        for (const Control& c : controls_) appendControl(items, paint, c, clips);
        for (const Control& c : controls_) {
            if (c.kind == Kind::Drop && drops_[c.key].control.open) {
                append(items, paint, Elm::BuildDropdownPopupItems(drops_[c.key], dropdownStyle_, 0u, Metrics::kZPopup, paint));
                dropdownRowTexts(drops_[c.key]);
            }
        }
        if (popup_.isOpen()) appendPopup(items, paint);
        overlay_.appendItems(items, origin_.x, origin_.y, 0.f, visible_.y, visible_.y + visible_.h, owner, 0u, paint);

        packet_ = extractor_.Build(++frameId_, items, nullptr, clips);
        if (!SR::IsFramePacketValid(packet_)) return true;
        return sdl_.draw(renderer, packet_);
    }

    // true quando o ponteiro esta sobre um campo que pede cursor de texto.
    [[nodiscard]] bool wantsTextCursor(float x, float y) const {
        if (!shown_ || !sui::Box{visible_.x, visible_.y, visible_.w, visible_.h}.contains(x, y)) return false;
        const SU::Vec2 p{x - origin_.x, y - origin_.y};
        if (popup_.overHex(p)) return true;
        for (const Control& c : controls_) {
            if (!c.disabled && (c.kind == Kind::Num || c.kind == Kind::Text) && contains(c.box, p)) return true;
        }
        return false;
    }

    // Roteia o evento: moldura, barra de rolagem, ponteiro nos controles, teclas e texto do campo em edicao. true = consumiu.
    bool handleEvent(const SDL_Event& e) {
        if (chrome_.handleEvent(e)) return true;
        if (!shown_) return false;
        // Barra de rolagem: o arraste do polegar segue com o mouse fora do corpo.
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT &&
            bar_.pointerDown(e.button.x - origin_.x, e.button.y - origin_.y, scrollY_)) {
            clampScroll();
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (bar_.pointerMove(e.motion.x - origin_.x, e.motion.y - origin_.y, scrollY_)) clampScroll();
            if (bar_.dragging()) return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT && bar_.pointerUp()) return true;
        switch (e.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button != SDL_BUTTON_LEFT) return false;
                return pointer(Elm::PointerPhase::Down, e.button.x, e.button.y, e.button.windowID);
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button != SDL_BUTTON_LEFT) return false;
                return pointer(Elm::PointerPhase::Up, e.button.x, e.button.y, e.button.windowID);
            case SDL_EVENT_MOUSE_WHEEL:
                if (!sui::Box{visible_.x, visible_.y, visible_.w, visible_.h}.contains(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
                scrollBy(-e.wheel.y * kWheelStep);
                return true;
            case SDL_EVENT_MOUSE_MOTION:
                if (!captured_) return false;
                return pointer(Elm::PointerPhase::Move, e.motion.x, e.motion.y, e.motion.windowID);
            case SDL_EVENT_TEXT_INPUT:
                if (popup_.editingHex()) return popup_.text(e.text.text, SDL_GetWindowFromID(e.text.windowID));
                if (field_kind_ == FieldKind::None) return false;
                if (e.text.text) {
                    field_.Edit().InsertText(e.text.text);
                    commitField();
                }
                return true;
            case SDL_EVENT_KEY_DOWN:
                if (popup_.editingHex()) return popup_.keyDown(e.key.key, static_cast<SDL_Keymod>(e.key.mod), SDL_GetWindowFromID(e.key.windowID));
                if (field_kind_ == FieldKind::None) return false;
                keyInField(e.key.key, static_cast<SDL_Keymod>(e.key.mod), e.key.windowID);
                return true;
            default:
                return false;
        }
    }

  private:
    // ------------------------------------------------------------------ tipos
    // Tipo de controle desenhado numa linha do inspetor.
    enum class Kind : std::uint8_t { Header, Slider, Check, Drop, Swatch, Num, Text, Button, Cell };
    // Tipo do campo de texto em edicao (nenhum, numero ou texto livre).
    enum class FieldKind : std::uint8_t { None, Number, Text };

    // Controle do quadro: tipo, chave unica, caixa (coordenadas locais) e estado; a lista e refeita a cada prepare.
    struct Control {
        Kind kind = Kind::Button;
        std::uint32_t key = 0;
        SU::Rect box{};
        bool disabled = false;
        im::Section section = im::Section::Transform;   // Header
        float value = 0.f;                                // Slider, Num
        int decimals = 2;                                 // Num
        bool sliderValue = false;                         // Num: campo do valor de um deslizante (confirma ao sair)
        im::Range range{-std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        bool checked = false;                             // Check
        std::size_t index = 0;                            // Drop
        std::vector<std::string> options;                 // Drop
        std::string text;                                 // Text, Num (exibicao)
        float* rgb = nullptr;                             // Swatch
        std::function<void(float)> setF;                  // Slider, Num
        std::function<void(bool)> setB;                   // Check
        std::function<void(std::size_t)> setIndex;        // Drop
        std::function<void(const std::string&)> setText;  // Text
        std::function<void()> onClick;                    // Header, Swatch, Button
        std::function<void()> onEnter, onCancel;          // Text: Enter confirma, Esc cancela (renomear tag)
        bool flat = false;                                // Button: sem fundo (so a area de clique, como um rotulo)
    };

    // ------------------------------------------------------------------ chaves e helpers de layout
    std::uint32_t nextKey() { return keySeq_++; }

    static SU::Rect rect(float x, float y, float w, float h) { return {{x, y}, {w, h}}; }

    // Ponto dentro do retangulo.
    static bool contains(const SU::Rect& r, SU::Vec2 p) {
        return p.x >= r.position.x && p.x < r.position.x + r.size.x && p.y >= r.position.y && p.y < r.position.y + r.size.y;
    }

    // Registra um controle do quadro e devolve a referencia para o chamador preencher.
    Control& add(Kind kind, const SU::Rect& box, std::uint32_t key) {
        Control c;
        c.kind = kind;
        c.key = key;
        c.box = box;
        c.disabled = disabled_;
        controls_.push_back(std::move(c));
        return controls_.back();
    }

    // Rotulo da linha, a esquerda dos controles.
    void label(float y, const char* text) {
        canvas_.addText(innerX_, y + 5.f, text, Metrics::kFont, theme_.text, labelW_ - 6.f);
    }

    // Grupo com cabecalho clicavel. Retorna true quando a secao esta aberta (o conteudo entra).
    bool beginSection(im::Section s, const char* title) {
        const float top = y_;
        const float w = width_ - 2.f * Metrics::kPad;
        Control& c = add(Kind::Header, rect(Metrics::kPad, y_, w, Metrics::kHeadH), nextKey());
        c.section = s;
        c.disabled = false;   // abrir e fechar vale mesmo com o objeto travado
        c.onClick = [this, s] { expanders_.Toggle(s); };
        canvas_.addSolid(Metrics::kPad, y_, w, Metrics::kHeadH, headerColor_, Metrics::kZFill);
        canvas_.addText(Metrics::kPad + 8.f, y_ + 5.f, title, Metrics::kFont + 1.f, headingColor_, w - 32.f);
        canvas_.addText(width_ - Metrics::kPad - 14.f, y_ + 4.f, expanders_.IsOpen(s) ? "-" : "+", Metrics::kFont + 1.f, theme_.textDim, 12.f);
        y_ += Metrics::kHeadH;
        if (!expanders_.IsOpen(s)) {
            frames_.push_back(rect(Metrics::kPad, top, w, Metrics::kHeadH));
            y_ += Metrics::kGap;
            return false;
        }
        y_ += Metrics::kGap;
        groupTop_ = top;
        return true;
    }

    // Fecha o grupo: guarda a moldura de fundo da secao e afasta a proxima.
    void endSection() {
        frames_.push_back(rect(Metrics::kPad, groupTop_, width_ - 2.f * Metrics::kPad, y_ - groupTop_ + Metrics::kGap));
        y_ += Metrics::kGap * 2.f;
    }

    void nextRow() { y_ += Metrics::kRowH + Metrics::kGap; }

    // Deslizante: limites do modelo; o valor vem do objeto. Arrastar grava na hora; o campo ao lado aceita um numero
    // digitado (Enter ou sair do campo confirma, com o mesmo limite do deslizante; Esc cancela).
    void slider(const char* text, float* target, im::Range range, bool enabled = true) {
        label(y_, text);
        const float sw = ctrlW_ - Metrics::kValueW;
        Control& c = add(Kind::Slider, rect(ctrlX_, y_, sw, Metrics::kRowH), nextKey());
        c.disabled = disabled_ || !enabled;
        c.value = *target;
        c.range = range;
        c.setF = [target](float v) { *target = v; };
        const float vx = ctrlX_ + sw + 4.f;
        const float vw = Metrics::kValueW - 4.f;
        Control& v = add(Kind::Num, rect(vx, y_, vw, Metrics::kRowH), nextKey());
        v.disabled = c.disabled;
        v.sliderValue = true;
        v.value = *target;
        v.decimals = 2;
        v.range = range;
        v.text = im::FormatNumber(*target, 2);
        v.setF = [target](float x) { *target = x; };
        if (!editing(v.key)) {
            canvas_.addSolid(vx, y_, vw, Metrics::kRowH, v.disabled ? fieldDisabled_ : fieldColor_, Metrics::kZField);
            canvas_.addText(vx + 4.f, y_ + 5.f, v.text, Metrics::kFont, v.disabled ? theme_.textDim : theme_.text, vw - 8.f);
        }
        nextRow();
    }

    // Campo numerico: digitado. Escreve no objeto so quando o texto e um numero valido dentro do intervalo.
    void numberCell(float x, float w, char axis, float* target, int decimals, im::Range range, std::function<void()> changed = {}) {
        const float bx = x + (axis != 0 ? Metrics::kAxisW : 0.f);
        if (axis != 0) {
            const char letter[2] = {axis, 0};
            canvas_.addText(x + 2.f, y_ + 5.f, letter, Metrics::kFont, axisColor(axis), Metrics::kAxisW);
        }
        Control& c = add(Kind::Num, rect(bx, y_, w - (bx - x), Metrics::kRowH), nextKey());
        c.value = *target;
        c.decimals = decimals;
        c.range = range;
        c.text = im::FormatNumber(*target, decimals);
        c.setF = [target, changed](float v) {
            *target = v;
            if (changed) changed();   // quem depende do valor (ex.: tamanho do bloco) reage aqui
        };
        if (!editing(c.key)) canvas_.addSolid(bx, y_, w - (bx - x), Metrics::kRowH, c.disabled ? fieldDisabled_ : fieldColor_, Metrics::kZField);
        if (!editing(c.key)) canvas_.addText(bx + 6.f, y_ + 5.f, c.text, Metrics::kFont, c.disabled ? theme_.textDim : theme_.text, w - (bx - x) - 10.f);
    }

    // Linha X/Y/Z: rotulo e tres campos numericos.
    void vecRow(const char* text, float* v, int decimals, im::Range range = {-1e9f, 1e9f}) {
        label(y_, text);
        const float cell = (ctrlW_ - 2.f * Metrics::kGap) / 3.f;
        static const char axes[3] = {'X', 'Y', 'Z'};
        for (int i = 0; i < 3; ++i) {
            numberCell(ctrlX_ + static_cast<float>(i) * (cell + Metrics::kGap), cell, axes[i], &v[i], decimals, range);
        }
        nextRow();
    }

    // Numero sozinho na linha (near, far, ortho size).
    void numberRow(const char* text, float* target, int decimals, im::Range range = {-1e9f, 1e9f}, std::function<void()> changed = {}) {
        label(y_, text);
        numberCell(ctrlX_, ctrlW_, 0, target, decimals, range, std::move(changed));
        nextRow();
    }

    // Caixa de selecao com rotulo a direita. A escrita passa por "set" (campo do objeto ou hook).
    void check(const char* text, bool value, std::function<void(bool)> set, bool enabled = true) {
        Control& c = add(Kind::Check, rect(ctrlX_, y_, ctrlW_, Metrics::kRowH), nextKey());
        c.disabled = disabled_ || !enabled;
        c.checked = value;
        c.setB = std::move(set);
        canvas_.addText(ctrlX_ + Metrics::kCheckBox + Metrics::kCheckGap, y_ + 5.f, text, Metrics::kFont, theme_.text,
                        ctrlW_ - Metrics::kCheckBox - Metrics::kCheckGap);
        nextRow();
    }

    // Caixa de marcar ligada direto a um bool (grava ao clicar).
    void checkField(const char* text, bool* target, bool enabled = true) {
        check(text, *target, [target](bool on) { *target = on; }, enabled);
    }

    // Lista de escolha com rotulo a esquerda.
    void drop(const char* text, std::vector<std::string> options, std::size_t index, std::function<void(std::size_t)> set, bool enabled = true) {
        label(y_, text);
        Control& c = add(Kind::Drop, rect(ctrlX_, y_, ctrlW_, Metrics::kRowH), nextKey());
        c.disabled = disabled_ || !enabled;
        c.options = std::move(options);
        c.index = index;
        c.setIndex = std::move(set);
        if (index < c.options.size()) {   // texto do valor escolhido: entra no canvas do quadro (antes do desenho)
            canvas_.addText(ctrlX_ + 8.f, y_ + 5.f, c.options[index], Metrics::kFont, theme_.text, ctrlW_ - 24.f);
        }
        nextRow();
    }

    // Amostra de cor (abre o seletor) com o HEX ao lado.
    void swatch(const char* text, float* rgb, bool enabled = true) {
        label(y_, text);
        Control& c = add(Kind::Swatch, rect(ctrlX_, y_, Metrics::kSwatchW, Metrics::kRowH), nextKey());
        c.disabled = disabled_ || !enabled;
        c.rgb = rgb;
        c.onClick = [this, rgb, key = c.key] { openPicker(key, rgb); };
        canvas_.addSolid(ctrlX_, y_, Metrics::kSwatchW, Metrics::kRowH, swatchBorder_, Metrics::kZFill);
        canvas_.addSolid(ctrlX_ + 1.f, y_ + 1.f, Metrics::kSwatchW - 2.f, Metrics::kRowH - 2.f, sui::Srgb(rgb[0], rgb[1], rgb[2]), Metrics::kZFill + 0.01f);
        canvas_.addText(ctrlX_ + Metrics::kSwatchW + 6.f, y_ + 5.f, im::HexText(rgb), Metrics::kSmallFont + 1.f, theme_.textDim, 80.f);
        nextRow();
    }

    // Campo de texto (nome, alvo da luz). Escreve a cada tecla no buffer do componente.
    void textRow(const char* text, char* buf, std::size_t cap, bool enabled = true) {
        label(y_, text);
        Control& c = add(Kind::Text, rect(ctrlX_, y_, ctrlW_, Metrics::kRowH), nextKey());
        c.disabled = disabled_ || !enabled;
        c.text = buf;
        c.setText = [buf, cap](const std::string& s) { std::snprintf(buf, cap, "%s", s.c_str()); };
        if (!editing(c.key)) {
            canvas_.addSolid(ctrlX_, y_, ctrlW_, Metrics::kRowH, c.disabled ? fieldDisabled_ : fieldColor_, Metrics::kZField);
            canvas_.addText(ctrlX_ + 6.f, y_ + 5.f, c.text, Metrics::kFont, c.disabled ? theme_.textDim : theme_.text, ctrlW_ - 10.f);
        }
        nextRow();
    }

    // Botao com rotulo. Fica na area de controles, a partir de x.
    void button(float x, const char* text, std::function<void()> onClick, float w = Metrics::kButtonW) {
        Control& c = add(Kind::Button, rect(x, y_, w, Metrics::kRowH), nextKey());
        c.onClick = std::move(onClick);
        canvas_.addText(x + 8.f, y_ + 5.f, text, Metrics::kFont, theme_.text, w - 12.f);   // rotulo: canvas do quadro
    }

    // Lista de assets do tipo dado. "(none)" limpa o campo; escolher grava o nome no buffer e passa pelo hook.
    void assetRow(const char* text, editor::AssetKind kind, char* buf, std::size_t cap) {
        std::vector<std::string> options{T("(none)")};
        std::size_t selected = 0;
        for (const std::string& rel : hooks_.assets(kind)) {
            if (rel == buf) selected = options.size();
            options.push_back(rel);
        }
        if (buf[0] && selected == 0) {   // arquivo fora da pasta de Assets: mostra o nome mesmo assim
            options.push_back(buf);
            selected = options.size() - 1;
        }
        const std::vector<std::string> list = options;
        drop(text, std::move(options), selected, [this, kind, buf, cap, list](std::size_t i) {
            const std::string chosen = i == 0 || i >= list.size() ? std::string() : list[i];
            std::snprintf(buf, cap, "%s", chosen.c_str());
            hooks_.assetChosen(kind, chosen);
        });
    }

    // Cor do eixo: X vermelho, Y verde, Z azul.
    [[nodiscard]] SU::Vec4 axisColor(char axis) const {
        if (axis == 'X') return sui::Srgb(0.95f, 0.38f, 0.38f);
        if (axis == 'Y') return sui::Srgb(0.50f, 0.85f, 0.45f);
        return sui::Srgb(0.42f, 0.62f, 1.f);
    }

    [[nodiscard]] bool editing(std::uint32_t key) const { return field_kind_ != FieldKind::None && field_key_ == key; }

    // Textos traduzidos de uma lista (T devolve o texto do idioma atual).
    static std::vector<std::string> tr(std::initializer_list<const char*> keys) {
        std::vector<std::string> out;
        for (const char* k : keys) out.emplace_back(T(k));
        return out;
    }

    // ------------------------------------------------------------------ secoes
    void header() {
        canvas_.addText(Metrics::kPad, y_, F("Entity %u (generation %u)", ctx_.entityIndex, ctx_.entityGeneration), Metrics::kSmallFont + 1.f,
                        theme_.textDim, width_ - 2.f * Metrics::kPad);
        y_ += Metrics::kRowH - 4.f;
        if (ctx_.name) textRow(T("Name"), ctx_.name->text, sizeof ctx_.name->text);
        check(T("Active"), ctx_.active, [this](bool on) { hooks_.setActive(on); });
        // Travado: o checkbox funciona mesmo com o resto do bloco so de leitura.
        Control& lk = add(Kind::Check, rect(ctrlX_, y_, ctrlW_, Metrics::kRowH), nextKey());
        lk.disabled = false;
        lk.checked = ctx_.locked;
        lk.setB = [this](bool on) { hooks_.setLocked(on); };
        canvas_.addText(ctrlX_ + Metrics::kCheckBox + Metrics::kCheckGap, y_ + 5.f, T("Locked (editor only)"), Metrics::kFont, theme_.text,
                        ctrlW_ - Metrics::kCheckBox - Metrics::kCheckGap);
        nextRow();
    }

    // Secao Transform (posicao, rotacao, escala).
    void transform() {
        Transform& t = *ctx_.transform;
        if (!beginSection(im::Section::Transform, T("Transform"))) return;
        vecRow(T("Position"), t.pos, 2);
        vecRow(T("Rotation"), t.rot, 1);
        vecRow(T("Scale"), t.scale, 2);
        endSection();
    }

    // Secao Mesh Renderer: forma/modelo, material, textura, emissao, neblina, sombras e materiais por parte.
    void mesh() {
        MeshRenderer& m = *ctx_.mesh;
        if (!beginSection(im::Section::MeshRenderer, T("Mesh Renderer"))) return;
        drop(T("Shape"), tr({"Cube", "Sphere", "Plane", "Model", "Cylinder", "Cone", "Capsule", "Wheel", "Triangle prism", "Hexagon prism"}),
             static_cast<std::size_t>(std::clamp(m.shape, 0, 9)), [&m](std::size_t i) { m.shape = static_cast<int>(i); });
        if (m.shape == 3) {
            assetRow(T("Model file"), editor::AssetKind::Model, m.model, sizeof m.model);
            if (!m.model[0]) {
                canvas_.addText(ctrlX_, y_ + 2.f, T("Import a .glb/.gltf/.fbx in Assets and pick it here."), Metrics::kSmallFont, theme_.textDim, ctrlW_);
                nextRow();
            }
        }
        assetRow(T("Texture"), editor::AssetKind::Image, m.texture, sizeof m.texture);
        if (m.shape == 3 && m.node >= 0) {
            canvas_.addText(innerX_, y_, T("Imported node. Each part keeps its file material unless you override it below."), Metrics::kSmallFont,
                            theme_.textDim, width_ - innerX_ - Metrics::kPad);
            nextRow();
            partsRows();
        } else {
            swatch(T("Color"), m.color);
            slider(T("Roughness"), &m.roughness, {0.05f, 1.f});
            slider(T("Metallic"), &m.metallic, {0.f, 1.f});
        }
        slider(T("Opacity"), &m.opacity, {0.f, 1.f});
        checkField(T("Enable emission"), &m.emissionOn);
        swatch(T("Emission color"), m.emissive, m.emissionOn);
        slider(T("Emission intensity"), &m.emissiveIntensity, {0.f, 50.f}, m.emissionOn);
        checkField(T("Enable fog"), &m.fogOn);
        swatch(T("Fog color"), m.fogColor, m.fogOn);
        slider(T("Fog amount"), &m.fogAmount, {0.f, 1.f}, m.fogOn);
        slider(T("Fog start"), &m.fogStart, {0.f, 1000.f}, m.fogOn);
        slider(T("Fog end"), &m.fogEnd, {0.f, 1000.f}, m.fogOn);
        checkField(T("Cast shadow"), &m.castShadow);
        checkField(T("Receive shadow"), &m.receiveShadow);
        endSection();
    }

    // Partes de um modelo importado: cada parte pode ter o seu material (sobreposto ao do arquivo).
    void partsRows() {
        for (const auto& [part, name] : ctx_.parts) {
            canvas_.addText(innerX_, y_ + 5.f, name.empty() ? T("Part") : name.c_str(), Metrics::kFont, headingColor_, width_ - innerX_ - Metrics::kPad);
            nextRow();
            PartOverride* ov = findOverride(part);
            check(T("Override material"), ov != nullptr, [this, part](bool on) { hooks_.setPartOverride(part, on); });
            if (ov) {
                swatch(T("Color"), ov->color);
                slider(T("Roughness"), &ov->roughness, {0.05f, 1.f});
                slider(T("Metallic"), &ov->metallic, {0.f, 1.f});
                partTextureRow(part, ov->texture);
            } else {
                canvas_.addText(innerX_, y_ + 5.f, T("File material"), Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
                nextRow();
            }
        }
    }

    // Material trocado da parte `part`, ou nullptr se nao ha.
    [[nodiscard]] PartOverride* findOverride(int part) const {
        if (!ctx_.overrides) return nullptr;
        for (PartOverride& it : ctx_.overrides->items) {
            if (it.part == part) return &it;
        }
        return nullptr;
    }

    // Linha de textura de uma parte do modelo (lista das imagens do projeto).
    void partTextureRow(int part, char* texture) {
        std::vector<std::string> options{T("(none)")};
        std::size_t selected = 0;
        if (hooks_.assets) for (const std::string& rel : hooks_.assets(editor::AssetKind::Image)) {
            if (rel == texture) selected = options.size();
            options.push_back(rel);
        }
        if (texture[0] && selected == 0) { options.push_back(texture); selected = options.size() - 1; }
        const std::vector<std::string> list = options;
        drop(T("Texture"), std::move(options), selected, [this, part, texture, list](std::size_t i) {
            const std::string chosen = i == 0 || i >= list.size() ? std::string() : list[i];
            std::snprintf(texture, 160, "%s", chosen.c_str());
            if (hooks_.setPartTexture) hooks_.setPartTexture(part, chosen);
        });
    }

    // Secao Light source (tipo, cor, intensidade, alcance, sombra).
    void light() {
        LightComp& lc = *ctx_.light;
        if (!beginSection(im::Section::Light, T("Light source"))) return;
        drop(T("Type"), tr({"Directional", "Point", "Spot"}), static_cast<std::size_t>(std::clamp(static_cast<int>(lc.light.type), 0, 2)),
             [&lc](std::size_t i) { lc.light.type = static_cast<tool::LightType>(i); });
        swatch(T("Light color"), &lc.light.color.x);
        slider(T("Intensity"), &lc.light.intensity, {0.f, 200.f});
        slider(T("Range"), &lc.light.range, {0.1f, 200.f});
        checkField(T("Sync rotation with Transform"), &lc.syncRot);
        textRow(T("Object name to follow"), lc.target, sizeof lc.target);
        if (lc.target[0] && !hooks_.hasNamed(lc.target)) {
            canvas_.addText(innerX_, y_, T("No object with this name: uses this object."), Metrics::kSmallFont, warnColor_, width_ - innerX_ - Metrics::kPad);
            nextRow();
        }
        if (!lc.syncRot) vecRow(T("Direction"), &lc.light.direction.x, 3);
        if (lc.light.type != tool::LightType::Directional) checkField(T("Cast shadow"), &lc.light.castShadow);
        endSection();
    }

    // Secao Global Light (sol): cor, intensidade, direcao, ambiente e ciclo dinamico.
    void global() {
        GlobalLight& g = *ctx_.global;
        if (!beginSection(im::Section::GlobalLight, T("Global Light (sun)"))) return;
        swatch(T("Light color"), g.color);
        slider(T("Intensity"), &g.intensity, {0.f, 20.f});
        slider(T("Direction (azimuth)"), &g.azimuth, {0.f, 360.f});
        slider(T("Height (elevation)"), &g.elevation, {-10.f, 90.f});
        swatch(T("Ambient light"), g.ambient);
        checkField(T("Sun casts shadows"), &g.castShadow);
        checkField(T("Dynamic (day/night cycle while playing)"), &g.dynamic);
        if (g.dynamic) slider(T("Speed (deg/s)"), &g.speed, {0.f, 360.f});
        canvas_.addText(innerX_, y_, T("Only the first Global Light in the scene is used."), Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
        nextRow();
        endSection();
    }

    // Secao Camera: campo de visao, planos, 2D, principal/split screen, audio e pos-processamento.
    void camera() {
        CameraComp& c = *ctx_.camera;
        if (!beginSection(im::Section::Camera, T("Camera"))) return;
        checkField(T("2D camera (orthographic)"), &c.ortho2D);
        if (c.ortho2D) numberRow(T("Visible height (world units)"), &c.orthoSize, 1, {0.5f, 500.f});
        else slider(T("Field of view"), &c.fovDeg, {10.f, 120.f});
        numberRow(T("Near"), &c.nearZ, 2, {0.01f, 10.f});
        numberRow(T("Far"), &c.farZ, 1, {1.f, 5000.f});
        check(T("Principal (active game camera)"), ctx_.principal, [this](bool on) { hooks_.setPrincipal(on); });
        if (ctx_.principal) {
            if (ctx_.principalCount >= 2) {
                canvas_.addText(innerX_, y_, F("%zu Principal cameras: split screen is on.", ctx_.principalCount), Metrics::kSmallFont, okColor_,
                                width_ - innerX_ - Metrics::kPad);
                nextRow();
            }
            drop(T("Split screen"), tr({"Vertical split (side by side)", "Horizontal split (top / bottom)"}), ctx_.splitVertical ? 0 : 1,
                 [this](std::size_t i) { hooks_.setSplitVertical(i == 0); });
        }
        checkField(T("Sound ear (audio listener)"), &c.soundEar);
        if (c.soundEar) slider(T("Master volume"), &c.earVolume, {0.f, 2.f});
        postFx(c.post);
        endSection();
    }

    // Pos-processamento da camera: um subgrupo com o proprio cabecalho (estado no modelo, fechado no inicio).
    void postFx(tool::PostFx& p) {
        const float top = y_;
        const float w = width_ - innerX_ - Metrics::kPad;
        Control& h = add(Kind::Header, rect(innerX_, y_, w, Metrics::kRowH), nextKey());
        h.section = im::Section::PostFx;
        h.disabled = false;
        h.onClick = [this] { expanders_.Toggle(im::Section::PostFx); };
        canvas_.addSolid(innerX_, y_, w, Metrics::kRowH, headerColor_, Metrics::kZFill);
        canvas_.addText(innerX_ + 8.f, y_ + 5.f, T("Post-processing (this camera)"), Metrics::kFont, headingColor_, w - 30.f);
        canvas_.addText(innerX_ + w - 12.f, y_ + 4.f, expanders_.IsOpen(im::Section::PostFx) ? "-" : "+", Metrics::kFont + 1.f, theme_.textDim, 12.f);
        y_ += Metrics::kRowH + Metrics::kGap;
        if (expanders_.IsOpen(im::Section::PostFx)) {
            checkField(T("Enable post-processing"), &p.enabled);
            if (p.enabled) {
                drop(T("Mode"), tr({"Add to the scene's global effect", "Replace the global effect"}), p.additive ? 0 : 1,
                     [&p](std::size_t i) { p.additive = i == 0; });
                slider(T("Exposure (EV)"), &p.exposure, {-3.f, 3.f});
                slider(T("Contrast"), &p.contrast, {0.5f, 1.8f});
                slider(T("Saturation"), &p.saturation, {0.f, 2.f});
                swatch(T("Tint color"), p.tint);
                slider(T("Tint strength"), &p.tintStrength, {0.f, 1.f});
                slider(T("Vignette"), &p.vignette, {0.f, 1.f});
                if (p.vignette > 0.f) slider(T("Vignette size"), &p.vignetteSize, {0.2f, 1.4f});
                slider(T("Sharpen"), &p.sharpen, {0.f, 1.f});
                button(ctrlX_, T("Reset effect"), [&p] {
                    const bool add = p.additive;
                    p = tool::PostFx{};
                    p.enabled = true;
                    p.additive = add;
                });
                nextRow();
            }
        }
        frames_.push_back(rect(innerX_ - 4.f, top, w + 8.f, y_ - top));
    }

    // Fisica (Newtoon): corpo, colisor, massa, atrito e quique. Com corpo dinamico, gravidade e amortecimento. A
    // gravidade propria (Vector physics) tem atalhos de direcao. Durante o Play mostra a velocidade e o estado.
    void physics() {
        PhysicsComp& p = *ctx_.physics;
        if (!beginSection(im::Section::Physics, T("Physics (Newtoon)"))) return;
        drop(T("Body"), tr({"Static", "Dynamic", "Kinematic"}), static_cast<std::size_t>(std::clamp(p.body, 0, 2)),
             [&p](std::size_t i) { p.body = static_cast<int>(i); });
        drop(T("Collider"), tr({"Auto (from mesh)", "Box", "Sphere"}), static_cast<std::size_t>(std::clamp(p.collider, 0, 2)),
             [&p](std::size_t i) { p.collider = static_cast<int>(i); });
        const bool dynamic = p.body == 1;
        if (dynamic) {
            numberRow(T("Mass (kg)"), &p.mass, 3, {0.001f, 100000.f});
            slider(T("Gravity scale"), &p.gravityScale, {-3.f, 3.f});
            slider(T("Linear drag"), &p.linearDamping, {0.f, 5.f});
            slider(T("Angular drag"), &p.angularDamping, {0.f, 5.f});
            checkField(T("Use gravity"), &p.useGravity);
            checkField(T("Freeze rotation"), &p.freezeRotation);
            checkField(T("CCD (fast small objects cannot pass through walls)"), &p.ccd);
        }
        slider(T("Friction"), &p.friction, {0.f, 2.f});
        slider(T("Bounciness"), &p.bounciness, {0.f, 1.f});
        checkField(T("Trigger (detects, does not push)"), &p.trigger);
        if (dynamic) {
            checkField(T("Vector physics (own gravity)"), &p.overrideGravity);
            if (p.overrideGravity) {
                vecRow(T("Gravity vector (m/s2)"), p.gravity, 2, {-1000.f, 1000.f});
                static const struct Preset { const char* name; float v[3]; } presets[] = {
                    {"Down", {0.f, -9.81f, 0.f}}, {"Up", {0.f, 9.81f, 0.f}}, {"Left", {-9.81f, 0.f, 0.f}},
                    {"Right", {9.81f, 0.f, 0.f}}, {"Forward", {0.f, 0.f, -9.81f}}, {"Back", {0.f, 0.f, 9.81f}}, {"Zero", {0.f, 0.f, 0.f}}};
                const float cell = (ctrlW_ - 3.f * Metrics::kGap) / 4.f;
                for (std::size_t i = 0; i < std::size(presets); ++i) {
                    if (i != 0 && i % 4 == 0) nextRow();
                    const Preset pr = presets[i];
                    button(ctrlX_ + static_cast<float>(i % 4) * (cell + Metrics::kGap), T(pr.name),
                           [&p, pr] { for (int k = 0; k < 3; ++k) p.gravity[k] = pr.v[k]; }, cell);
                }
                nextRow();
            } else {
                canvas_.addText(innerX_, y_ + 5.f, F("World gravity: %.2f, %.2f, %.2f", static_cast<double>(ctx_.worldGravity[0]),
                                                     static_cast<double>(ctx_.worldGravity[1]), static_cast<double>(ctx_.worldGravity[2])),
                                Metrics::kSmallFont + 1.f, theme_.textDim, width_ - innerX_ - Metrics::kPad);
                nextRow();
            }
        }
        if (!ctx_.playPhysics.empty()) {
            canvas_.addText(innerX_, y_ + 5.f, ctx_.playPhysics, Metrics::kSmallFont + 1.f, theme_.textDim, width_ - innerX_ - Metrics::kPad);
            nextRow();
        }
        button(ctrlX_, T("Remove Physics"), [this] { hooks_.removePhysics(); }, Metrics::kRemoveW);
        nextRow();
        endSection();
    }

    // Voxel (blocos do objeto): tamanho do bloco, pincel com cor, Fill terrain e Clear, a camada (y) e a grade 16x16
    // da camada. Cada celula da grade pinta um bloco na camada escolhida. Fill, Clear e pintar ja mudam a revisao.
    void voxel() {
        VoxelChunk& vc = *ctx_.voxel;
        if (!beginSection(im::Section::Voxel, T("Voxel chunk (blocks)"))) return;
        label(y_, T("Solid blocks"));
        canvas_.addText(ctrlX_, y_ + 5.f, F("%u / %d", ctx_.voxelSolid, kChunkBlocks), Metrics::kFont, theme_.text, ctrlW_);
        nextRow();
        numberRow(T("Block size (m)"), &vc.cell, 2, {0.05f, 10.f}, [&vc] { ++vc.revision; });
        static const char* brushNames[kBlockTypes] = {"Air (erase)", "Grass", "Dirt", "Stone", "Sand", "Water", "Brick", "Snow"};
        std::vector<std::string> brushes;
        for (const char* n : brushNames) brushes.push_back(T(n));
        drop(T("Brush"), brushes, static_cast<std::size_t>(std::clamp(voxelBrush_, 0, kBlockTypes - 1)),
             [this](std::size_t i) { voxelBrush_ = static_cast<int>(i); });
        if (voxelBrush_ > 0) swatch(T("Brush color"), vc.palette[voxelBrush_]);
        button(innerX_, T("Fill terrain"), [this] { hooks_.voxelFill(); }, Metrics::kButtonW);
        button(innerX_ + Metrics::kButtonW + Metrics::kGap, T("Clear"), [this] { hooks_.voxelClear(); }, Metrics::kButtonW);
        nextRow();
        std::vector<std::string> layers;
        for (int i = 0; i < kChunkSize; ++i) layers.push_back(std::to_string(i));
        drop(T("Layer (y)"), layers, static_cast<std::size_t>(std::clamp(voxelLayer_, 0, kChunkSize - 1)),
             [this](std::size_t i) { voxelLayer_ = static_cast<int>(i); });
        // Grade da camada: cada celula e um botao; a cor vem do bloco (ou escuro para o ar).
        constexpr float kCell = 12.f;
        constexpr float kCellGap = 2.f;
        for (int z = 0; z < kChunkSize; ++z) {
            for (int x = 0; x < kChunkSize; ++x) {
                const unsigned char t = vc.at(x, voxelLayer_, z);
                Control& c = add(Kind::Cell, rect(innerX_ + static_cast<float>(x) * (kCell + kCellGap), y_, kCell, kCell), nextKey());
                const SU::Vec4 col = t ? sui::Srgb(vc.palette[t][0], vc.palette[t][1], vc.palette[t][2]) : sui::Srgb(0.18f, 0.18f, 0.2f);
                canvas_.addSolid(c.box.position.x, c.box.position.y, kCell, kCell, col, Metrics::kZField);
                c.onClick = [this, x, z, layer = voxelLayer_, brush = voxelBrush_] { hooks_.voxelPaint(x, layer, z, brush); };
            }
            y_ += kCell + kCellGap;   // proxima linha da grade
        }
        y_ += Metrics::kGap;
        endSection();
    }

    // Subgrupo recolhivel dentro de uma secao (estado no modelo). Use sub() para abrir e endSub() para fechar a
    // moldura, mesmo quando estiver fechado (so o cabecalho).
    bool sub(im::Section s, const char* title, float& top) {
        top = y_;
        const float w = width_ - innerX_ - Metrics::kPad;
        Control& h = add(Kind::Header, rect(innerX_, y_, w, Metrics::kRowH), nextKey());
        h.section = s;
        h.disabled = false;
        h.onClick = [this, s] { expanders_.Toggle(s); };
        canvas_.addSolid(innerX_, y_, w, Metrics::kRowH, headerColor_, Metrics::kZFill);
        canvas_.addText(innerX_ + 8.f, y_ + 5.f, title, Metrics::kFont, headingColor_, w - 30.f);
        canvas_.addText(innerX_ + w - 12.f, y_ + 4.f, expanders_.IsOpen(s) ? "-" : "+", Metrics::kFont + 1.f, theme_.textDim, 12.f);
        y_ += Metrics::kRowH + Metrics::kGap;
        return expanders_.IsOpen(s);
    }

    // Fecha um subgrupo: guarda a moldura dele (da posicao `top` ate o Y atual).
    void endSub(float top) {
        frames_.push_back(rect(innerX_ - 4.f, top, width_ - innerX_ - Metrics::kPad + 8.f, y_ - top));
    }

    // Numero inteiro na linha (mesma aparencia do campo numerico; grava arredondado).
    void intRow(const char* text, int* target, int lo, int hi) {
        label(y_, text);
        Control& c = add(Kind::Num, rect(ctrlX_, y_, ctrlW_, Metrics::kRowH), nextKey());
        c.value = static_cast<float>(*target);
        c.decimals = 0;
        c.range = {static_cast<float>(lo), static_cast<float>(hi)};
        c.text = std::to_string(*target);
        c.setF = [target](float v) { *target = static_cast<int>(std::lround(v)); };
        if (!editing(c.key)) {
            canvas_.addSolid(ctrlX_, y_, ctrlW_, Metrics::kRowH, c.disabled ? fieldDisabled_ : fieldColor_, Metrics::kZField);
            canvas_.addText(ctrlX_ + 6.f, y_ + 5.f, c.text, Metrics::kFont, c.disabled ? theme_.textDim : theme_.text, ctrlW_ - 10.f);
        }
        nextRow();
    }

    // Particulas: emissao, forma, area de nascimento, cores, opacidade, trajetoria, tornado e movimento. Os
    // subgrupos recolhem como o pos-processamento (estado no modelo).
    void particles() {
        ParticleComp& p = *ctx_.particles;
        if (!beginSection(im::Section::Particles, T("Particles"))) return;
        checkField(T("Emitting"), &p.emitting);
        intRow(T("Max particles"), &p.maxParticles, 1, 5000);
        numberRow(T("Rate (per second)"), &p.rate, 1, {0.f, 5000.f});
        numberRow(T("Lifetime (s)"), &p.lifetime, 2, {0.05f, 60.f});
        numberRow(T("Speed"), &p.speed, 2, {0.f, 500.f});
        slider(T("Spread (deg)"), &p.spread, {0.f, 180.f});

        float top = 0.f;
        if (sub(im::Section::ParticleShape, T("Shape and spawn area"), top)) {
            button(ctrlX_, T("Smoke preset"), [this] { hooks_.smokePreset(); }, Metrics::kButtonW);
            nextRow();
            drop(T("Shape"), tr({"Dot", "Square", "Triangle", "Star", "Ring", "Streak (motion)", "Smoke"}),
                 static_cast<std::size_t>(std::clamp(p.form, 0, 6)), [&p](std::size_t i) { p.form = static_cast<int>(i); });
            drop(T("Spawn area"), tr({"Point", "Random box", "Random sphere"}), static_cast<std::size_t>(std::clamp(p.area, 0, 2)),
                 [&p](std::size_t i) { p.area = static_cast<int>(i); });
            if (p.area) vecRow(T("Area size"), p.areaSize, 2, {0.f, 100.f});
            numberRow(T("Size start"), &p.size, 3, {0.001f, 50.f});
            numberRow(T("Size end"), &p.sizeEnd, 3, {0.f, 50.f});
        }
        endSub(top);

        if (sub(im::Section::ParticleColors, T("Colors and opacity"), top)) {
            canvas_.addText(innerX_, y_, T("Four colors over the lifetime: start, 1/3, 2/3 and end. Colors keep their brightness (no darkening)."),
                            Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
            nextRow();
            swatch(T("Color 1 (start)"), p.color);
            swatch(T("Color 2"), p.color2);
            swatch(T("Color 3"), p.color3);
            swatch(T("Color 4 (end)"), p.colorEnd);
            slider(T("Opacity"), &p.alpha, {0.f, 1.f});
            checkField(T("Opacity over time"), &p.fadeOpacity);
            if (p.fadeOpacity) numberRow(T("Opacity delay (s)"), &p.fadeDelay, 2, {0.f, p.lifetime});
        }
        endSub(top);

        if (sub(im::Section::ParticleRoute, T("Trace route"), top)) {
            checkField(T("Follow the route"), &p.route);
            canvas_.addText(innerX_, y_, T("Points are local to this object. Each particle travels the smooth curve through them over its lifetime. The Scene view draws the route while this object is selected."),
                            Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
            nextRow();
            for (int i = 0; i < p.routeCount; ++i) {
                const std::string name = F("Point %d", i + 1);
                vecRow(name.c_str(), p.routePts[i], 2);
                button(ctrlX_, T("Remove point"), [&p, i] {
                    for (int k = i; k + 1 < p.routeCount; ++k) for (int c = 0; c < 3; ++c) p.routePts[k][c] = p.routePts[k + 1][c];
                    --p.routeCount;
                }, Metrics::kButtonW);
                nextRow();
            }
            if (p.routeCount < 8) {
                button(innerX_, T("Add point"), [&p] {
                    float* last = p.routeCount > 0 ? p.routePts[p.routeCount - 1] : nullptr;
                    float* dst = p.routePts[p.routeCount];
                    dst[0] = last ? last[0] + 1.f : 0.f;
                    dst[1] = last ? last[1] : 0.f;
                    dst[2] = last ? last[2] : 0.f;
                    ++p.routeCount;
                }, Metrics::kButtonW);
            }
            if (p.routeCount > 0) button(innerX_ + Metrics::kButtonW + Metrics::kGap, T("Clear route"), [&p] { p.routeCount = 0; }, Metrics::kButtonW);
            nextRow();
        }
        endSub(top);

        if (sub(im::Section::ParticleTornado, T("Tornado"), top)) {
            checkField(T("Spin (tornado)"), &p.tornado);
            if (p.tornado) {
                slider(T("Turns per second"), &p.tornadoSpeed, {0.f, 6.f});
                numberRow(T("Base radius"), &p.tornadoRadius, 2, {0.f, 50.f});
                numberRow(T("Height"), &p.tornadoHeight, 2, {0.f, 100.f});
                canvas_.addText(innerX_, y_, T("The particles rise and spread out like a funnel. If Trace route is on, the route wins."),
                                Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
                nextRow();
            }
        }
        endSub(top);

        canvas_.addText(innerX_, y_ + 5.f, T("Motion"), Metrics::kFont + 1.f, headingColor_, width_ - innerX_ - Metrics::kPad);
        nextRow();
        slider(T("Gravity scale (world physics)"), &p.gravityScale, {-3.f, 3.f});
        slider(T("Bounce on floor"), &p.bounce, {0.f, 1.f});
        if (p.bounce > 0.f) numberRow(T("Floor Y"), &p.floorY, 2);
        canvas_.addText(innerX_, y_, T("Emits along the object's +Y axis. Drawn over the view with real colour and opacity (no depth test)."),
                        Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
        nextRow();
        button(ctrlX_, T("Remove Particles"), [this] { hooks_.removeParticles(); }, Metrics::kRemoveW);
        nextRow();
        endSection();
    }

    // Nomes dos recursos (ordem de im::Resource), como aparecem na lista.
    static const char* resourceLabel(int id) {
        static const char* names[] = {
            "Physics body (Newtoon)", "Tool: Jump", "Tool: Move", "Tool: Follow Target", "Tool: Spin", "Tool: On Collision",
            "Tool: Script library", "Mesh (cube)", "Point Light", "Camera", "Audio Source", "Particle System", "ToolObject Script",
            "Canvas (game UI)", "Voxel chunk (blocks)", "Tools (all)", "Global Light"};
        const std::size_t i = static_cast<std::size_t>(id);
        return i < std::size(names) ? names[i] : "";
    }

    // Adicionar e remover recursos: duas listas. Cada lista mostra so o que cabe
    // no objeto. Os recursos planejados aparecem em texto, sem opcao.
    void resources() {
        if (!beginSection(im::Section::Resources, T("Resources"))) return;
        std::vector<std::string> add{T("(choose)")};
        std::vector<int> addIds;
        for (int id : ctx_.addable) {
            add.emplace_back(T(resourceLabel(id)));
            addIds.push_back(id);
        }
        drop(T("+ Add Resource"), std::move(add), 0, [this, addIds](std::size_t i) {
            if (i > 0 && i - 1 < addIds.size()) hooks_.addResource(addIds[i - 1]);
        }, !ctx_.addable.empty());
        nextRow();
        std::vector<std::string> remove{T("(choose)")};
        std::vector<int> removeIds;
        for (int id : ctx_.removable) {
            remove.emplace_back(T(resourceLabel(id)));
            removeIds.push_back(id);
        }
        drop(T("- Remove Resource"), std::move(remove), 0, [this, removeIds](std::size_t i) {
            if (i > 0 && i - 1 < removeIds.size()) hooks_.removeResource(removeIds[i - 1]);
        }, !ctx_.removable.empty());
        nextRow();
        canvas_.addText(innerX_, y_, T("Coming in a future update (locked): Textures / Normal map, Material (.tmat) and custom shader, Point light shadows, Compress as NTC, Ray tracing (DXR)."),
                        Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
        nextRow();
        endSection();
    }

    // Canvas (UI de jogo): visivel, camera onde aparece (vazio = todas as Principais), ancora e tamanho do canvas e
    // aparencia. Os elementos nao se editam aqui: sao linhas filhas do Canvas na Hierarchy (ver uiElemSection).
    void canvasSection() {
        CanvasComp& ui = *ctx_.cv;
        if (!beginSection(im::Section::Canvas, T("Canvas (game UI)"))) return;
        checkField(T("Visible in game"), &ui.visible);
        checkField(T("Show in Scene"), &ui.showInScene);
        std::vector<std::string> options{T("(all Principal cameras)")};
        std::size_t selected = 0;
        for (const std::string& name : ctx_.cameraNames) {
            if (name == ui.camera) selected = options.size();
            options.push_back(name);
        }
        if (ui.camera[0] && selected == 0) {
            options.push_back(ui.camera);
            selected = options.size() - 1;
        }
        const std::vector<std::string> list = options;
        drop(T("Camera"), std::move(options), selected, [&ui, list](std::size_t i) {
            if (i == 0 || i >= list.size()) ui.camera[0] = 0;
            else std::snprintf(ui.camera, sizeof ui.camera, "%s", list[i].c_str());
        });
        // Posicao e tamanho do canvas: em pixels de projeto (referencia 1920x1080), e no modo Scale acompanham a camera.
        slider(T("Anchor X"), &ui.layout.anchorX, {-2.f, 3.f});
        slider(T("Anchor Y"), &ui.layout.anchorY, {-2.f, 3.f});
        numberRow(T("Offset X"), &ui.layout.offsetX, 0, {-1e5f, 1e5f});
        numberRow(T("Offset Y"), &ui.layout.offsetY, 0, {-1e5f, 1e5f});
        numberRow(T("Canvas width"), &ui.layout.width, 0, {1.f, 1e5f});
        numberRow(T("Canvas height"), &ui.layout.height, 0, {1.f, 1e5f});
        drop(T("Resize"), tr({"Follow anchor (fixed size)", "Scale with camera"}),
             ui.layout.resize == tool::canvas::Resize::Scale ? 1u : 0u, [&ui](std::size_t i) {
                 ui.layout.resize = i == 1 ? tool::canvas::Resize::Scale : tool::canvas::Resize::Follow;
             });
        // Aparencia do canvas: fundo e borda (cor e opacidade), canto redondo e espessura da borda.
        swatch(T("Background"), ui.bg);
        slider(T("Background opacity"), &ui.bg[3], {0.f, 1.f});
        swatch(T("Border"), ui.edge);
        slider(T("Border opacity"), &ui.edge[3], {0.f, 1.f});
        slider(T("Corner radius"), &ui.style.radius, {0.f, 200.f});
        slider(T("Border width"), &ui.style.borderWidth, {0.f, 40.f});
        canvas_.addText(innerX_, y_, F("%s: %d / %d  (%s)", T("Elements"), ui.count, kMaxUi,
                                       T("edit them in the Hierarchy: click a child row; right-click the Canvas > Add element")),
                        Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
        nextRow();
        canvas_.addText(innerX_, y_, T("Appears over the Game view of the chosen camera. Button clicks (while playing) send their event name."),
                        Metrics::kSmallFont, theme_.textDim, width_ - innerX_ - Metrics::kPad);
        nextRow();
        button(ctrlX_, T("Remove Canvas"), [this] { hooks_.removeUi(); }, Metrics::kButtonW);
        nextRow();
        endSection();
    }

    // Imagem do elemento de UI: lista so de PNG e JPEG do projeto. "(none)" limpa. Nao passa por assignAsset (so grava o caminho).
    void uiImageRow(const char* text, char* buf, std::size_t cap) {
        std::vector<std::string> options{T("(none)")};
        std::size_t selected = 0;
        if (hooks_.uiImages) {
            for (const std::string& rel : hooks_.uiImages()) {
                if (rel == buf) selected = options.size();
                options.push_back(rel);
            }
        }
        if (buf[0] && selected == 0) {   // arquivo fora da lista: mostra o caminho gravado mesmo assim
            options.push_back(buf);
            selected = options.size() - 1;
        }
        const std::vector<std::string> list = options;
        drop(text, std::move(options), selected, [buf, cap, list](std::size_t i) {
            const std::string chosen = i == 0 || i >= list.size() ? std::string() : list[i];
            std::snprintf(buf, cap, "%s", chosen.c_str());
        });
    }

    // Linha de botoes pequenos (cada um com seu texto), a partir da coluna dos controles.
    void smallButtons(std::initializer_list<std::pair<const char*, std::function<void()>>> items) {
        float x = ctrlX_;
        for (const auto& it : items) {
            button(x, it.first, it.second, 60.f);
            x += 60.f + Metrics::kGap;
        }
        nextRow();
    }

    // Elemento de UI selecionado na Hierarchy: todas as propriedades dele (nome, tipo, posicao, tamanho, cor, texto,
    // imagem, sombra, lista de entradas e linhas/colunas). O que nao vale para o tipo nao aparece.
    void uiElemSection() {
        UiElem& it = *ctx_.uiElem;
        const int kind = std::clamp(it.kind, 0, static_cast<int>(UiKind::Count) - 1);
        const bool isText = kind == static_cast<int>(UiKind::Text);
        const bool isButton = kind == static_cast<int>(UiKind::Button);
        const bool isBar = kind == static_cast<int>(UiKind::Bar);
        const bool isImage = kind == static_cast<int>(UiKind::Image);
        const bool isInventory = kind == static_cast<int>(UiKind::Inventory);
        const bool hasEntries = kind == static_cast<int>(UiKind::Menu) || kind == static_cast<int>(UiKind::List) ||
                                kind == static_cast<int>(UiKind::ListContainer) || isInventory;
        canvas_.addText(Metrics::kPad, y_, F("%s: %s  [%d]", T("UI element of"), ctx_.uiCanvasName.c_str(), ctx_.uiIndex),
                        Metrics::kSmallFont + 1.f, theme_.textDim, width_ - 2.f * Metrics::kPad);
        y_ += Metrics::kRowH - 4.f;
        if (!beginSection(im::Section::Canvas, T("UI element"))) return;
        textRow(T("Name"), it.name, sizeof it.name);   // nome do elemento (Hierarchy); vazio = nome do tipo
        textRow(T("Text"), it.text, sizeof it.text);
        std::vector<std::string> kinds;
        for (int k = 0; k < static_cast<int>(UiKind::Count); ++k) kinds.emplace_back(T(hm::kElemKindNames[k]));
        drop(T("Kind"), std::move(kinds), static_cast<std::size_t>(kind), [&it](std::size_t t) { it.kind = static_cast<int>(t); });
        canvas_.addText(innerX_, y_ + 5.f, F("%s: %s  (%s %d)", T("Id (unique, automatic)"), it.id, T("Region"), it.region),
                        Metrics::kFont, headingColor_, width_ - innerX_ - Metrics::kPad);
        nextRow();
        intRow(T("Order Z (higher = in front)"), &it.z, -999, 999);
        if (isButton) textRow(T("Event name (on click)"), it.action, sizeof it.action);
        if (isBar) {
            textRow(T("Variable"), it.action, sizeof it.action);
            numberRow(T("Full at"), &it.maxValue, 2, {0.0001f, 1e9f});
        }
        // Posicao (ponto do elemento na area do canvas: 0 = esquerda/topo, 1 = direita/base) e tamanho, em fracao do canvas.
        slider(T("Position X (anchor)"), &it.x, {0.f, 1.f});
        slider(T("Position Y (anchor)"), &it.y, {0.f, 1.f});
        slider(T("Width"), &it.w, {0.f, 1.f});
        slider(T("Height"), &it.h, {0.f, 1.f});
        slider(T("Corner radius"), &it.radius, {0.f, 200.f});
        swatch(T("Color"), it.color);
        slider(T("Color opacity"), &it.color[3], {0.f, 1.f});
        if (isText || isButton || kind == static_cast<int>(UiKind::Window)) slider(T("Font size"), &it.fontScale, {0.3f, 8.f});
        if (isText) numberRow(T("Wrap width (px, 0 = none)"), &it.wrapWidth, 0, {0.f, 1e5f});
        if (isImage) {
            uiImageRow(T("Image (png/jpg)"), it.image, sizeof it.image);
            checkField(T("Preserve image"), &it.preserveAspect);   // mantem a proporcao da imagem ao redimensionar
        }
        if (isButton) uiImageRow(T("Background image"), it.image, sizeof it.image);
        // Sombra projetada: liga e desliga; deslocamento, suavizacao e cor.
        checkField(T("Shadow"), &it.shadow);
        if (it.shadow) {
            numberRow(T("Shadow offset X"), &it.shadowX, 1, {-200.f, 200.f});
            numberRow(T("Shadow offset Y"), &it.shadowY, 1, {-200.f, 200.f});
            slider(T("Shadow blur"), &it.shadowBlur, {0.f, 100.f});
            swatch(T("Shadow color"), it.shadowColor);
            slider(T("Shadow opacity"), &it.shadowColor[3], {0.f, 1.f});
        }
        if (isInventory) {
            intRow(T("Rows"), &it.rows, 1, 16);
            intRow(T("Columns"), &it.cols, 1, 16);
            uiImageRow(T("Slot background image"), it.slotBackImage, sizeof it.slotBackImage);   // vazio = quadrado
            uiImageRow(T("Slot front image"), it.slotFrontImage, sizeof it.slotFrontImage);
            checkField(T("Preserve image"), &it.preserveAspect);
        }
        if (hasEntries) {   // lista editavel: um campo por entrada, com subir, descer e remover; "Add" no fim
            canvas_.addText(innerX_, y_ + 5.f, F("%s: %d / %d", isInventory ? T("Slot labels") : T("Entries"), it.entryCount, kMaxUiEntries),
                            Metrics::kFont, headingColor_, width_ - innerX_ - Metrics::kPad);
            nextRow();
            UiElem* target = &it;
            for (int i = 0; i < it.entryCount && i < kMaxUiEntries; ++i) {
                const std::string entryLabel = F("%s %d", T("Entry"), i + 1);
                textRow(entryLabel.c_str(), it.entries[i], kUiEntryLen);
                smallButtons({{T("Up"), [target, i] { editor::canvas_elems::MoveEntry(*target, i, -1); }},
                              {T("Down"), [target, i] { editor::canvas_elems::MoveEntry(*target, i, 1); }},
                              {T("Remove"), [target, i] { editor::canvas_elems::RemoveEntry(*target, i); }}});
            }
            if (it.entryCount < kMaxUiEntries) {
                button(ctrlX_, T("+ Add entry"), [target] { editor::canvas_elems::AddEntry(*target); }, Metrics::kButtonW);
                nextRow();
            }
        }
        endSection();
    }

    // Escolha de acao do Input Map: "(none)" limpa; uma acao fora da lista aparece
    // com o nome que ja estava gravado.
    void actionRow(const char* text, char* buf, std::size_t cap, int player, int kind) {
        std::vector<std::string> options{T("(none)")};
        std::size_t selected = 0;
        for (const std::string& name : hooks_.actionNames(player, kind)) {
            if (name == buf) selected = options.size();
            options.push_back(name);
        }
        if (buf[0] && selected == 0) {
            options.push_back(buf);
            selected = options.size() - 1;
        }
        const std::vector<std::string> list = options;
        drop(text, std::move(options), selected, [buf, cap, list](std::size_t i) {
            const std::string chosen = i == 0 || i >= list.size() ? std::string() : list[i];
            std::snprintf(buf, cap, "%s", chosen.c_str());
        });
    }

    // Ferramentas prontas do objeto (Jump, Move, Follow Target, Spin, On Collision, Script library). Cada uma tem um
    // titulo com Remove, o Enabled e os campos do tipo. A lista aberta e fixa (sem recolher por ferramenta).
    void tools() {
        ToolsComp& tl = *ctx_.tools;
        if (!beginSection(im::Section::Tools, T("Tools"))) return;
        button(innerX_, T("Help: JS library"), [this] { hooks_.openToolsHelp(); }, Metrics::kHelpW);
        nextRow();
        static const char* names[] = {"Jump", "Move", "Follow Target", "Spin", "On Collision", "Script library"};
        for (int i = 0; i < tl.count && i < kMaxTools; ++i) {
            ToolItem& it = tl.items[i];
            const std::size_t idx = static_cast<std::size_t>(i);
            const int k = std::clamp(it.kind, 0, static_cast<int>(ToolKind::Count) - 1);
            canvas_.addText(innerX_, y_ + 5.f, T(names[k]), Metrics::kFont, headingColor_, labelW_ - 6.f);
            button(ctrlX_, T("Remove"), [this, idx] { hooks_.removeTool(idx); }, Metrics::kButtonW);
            nextRow();
            check(T("Enabled"), it.enabled, [&it](bool on) { it.enabled = on; });
            drop(T("Player (controller map)"), {"1", "2", "3", "4", "5", "6", "7", "8"},
                 static_cast<std::size_t>(std::clamp(it.player, 1, 8) - 1), [&it](std::size_t p) { it.player = static_cast<int>(p) + 1; });
            switch (static_cast<ToolKind>(k)) {
                case ToolKind::Jump:
                    actionRow(T("Action"), it.action, sizeof it.action, it.player, 0);
                    numberRow(T("Height (m)"), &it.a, 2, {0.1f, 50.f});
                    check(T("Allow air jump"), it.b != 0.f, [&it](bool on) { it.b = on ? 1.f : 0.f; });
                    break;
                case ToolKind::Move:
                    actionRow(T("X axis"), it.action, sizeof it.action, it.player, 1);
                    actionRow(T("Y axis (forward)"), it.target, sizeof it.target, it.player, 1);
                    numberRow(T("Speed (m/s)"), &it.a, 2, {0.1f, 100.f});
                    check(T("Face direction"), it.b != 0.f, [&it](bool on) { it.b = on ? 1.f : 0.f; });
                    check(T("Camera relative"), it.mode == 1, [&it](bool on) { it.mode = on ? 1 : 0; });
                    break;
                case ToolKind::FollowTarget:
                    textRow(T("Target object name"), it.target, sizeof it.target);
                    vecRow(T("Offset"), &it.a, 2);
                    slider(T("Smoothing"), &it.d, {0.f, 20.f});
                    check(T("Look at target"), it.mode == 1, [&it](bool on) { it.mode = on ? 1 : 0; });
                    break;
                case ToolKind::Spin:
                    vecRow(T("Degrees / second (X Y Z)"), &it.a, 1);
                    break;
                case ToolKind::OnCollision:
                    textRow(T("Only objects with tag"), it.target, sizeof it.target);
                    drop(T("Action"), tr({"Log to Console", "Destroy the other object", "Destroy this object", "Bounce this object"}),
                         static_cast<std::size_t>(std::clamp(it.mode, 0, 3)), [&it](std::size_t m) { it.mode = static_cast<int>(m); });
                    if (it.mode == 3) numberRow(T("Bounce speed (m/s)"), &it.a, 1, {0.1f, 100.f});
                    if (!ctx_.hasPhysics) {
                        canvas_.addText(innerX_, y_, T("This tool needs a Physics component on the object."), Metrics::kSmallFont + 1.f,
                                        warnColor_, width_ - innerX_ - Metrics::kPad);
                        nextRow();
                    }
                    break;
                case ToolKind::ScriptLib:
                    textRow(T("Library file"), it.action, sizeof it.action);
                    if (it.action[0]) {
                        button(ctrlX_, T("Edit external"), [this, path = std::string(it.action)] { hooks_.openScriptExternal(path); },
                               Metrics::kExternalW);
                        nextRow();
                    }
                    canvas_.addText(innerX_, y_, T("Runs once the script runtime exists."), Metrics::kSmallFont, theme_.textDim,
                                    width_ - innerX_ - Metrics::kPad);
                    nextRow();
                    break;
                default:
                    break;
            }
            y_ += Metrics::kGap;   // respiro entre ferramentas
        }
        endSection();
    }

    // Script do objeto (ToolObject Script). Sem script: so o botao de criar o arquivo (opcional). Com script: liga e
    // desliga, mostra o arquivo, abre a janela de edicao ou o editor externo e, durante o Play, o status e o
    // reinicio dos scripts. Fora do Play o script so e guardado e editado.
    void script() {
        if (!beginSection(im::Section::Script, T("ToolObject Script"))) return;
        button(innerX_, T("Help: script examples"), [this] { hooks_.openScriptHelp(); }, Metrics::kHelpW);
        nextRow();
        if (!ctx_.script) {
            button(ctrlX_, T("Add Script"), [this] { hooks_.addScript(); });
            nextRow();
            canvas_.addText(innerX_, y_, T("Creates a script file for this object (optional)."), Metrics::kSmallFont, theme_.textDim,
                            width_ - innerX_ - Metrics::kPad);
            nextRow();
            endSection();
            return;
        }
        checkField(T("Script enabled"), &ctx_.script->enabled);
        label(y_, T("File"));
        canvas_.addText(ctrlX_, y_ + 5.f, ctx_.scriptPath.empty() ? std::string(T("(no file)")) : ctx_.scriptPath, Metrics::kSmallFont + 1.f,
                        theme_.textDim, ctrlW_);
        nextRow();
        button(ctrlX_, T("Edit script"), [this] { hooks_.openScriptWindow(); }, Metrics::kEditW);
        if (!ctx_.scriptPath.empty()) {
            button(ctrlX_ + Metrics::kEditW + Metrics::kGap, T("Edit external"), [this] { hooks_.openScriptExternal(ctx_.scriptPath); },
                   Metrics::kExternalW);
        }
        nextRow();
        if (ctx_.playing) {
            canvas_.addText(innerX_, y_ + 5.f, std::string(T("Script status")) + ": " + ctx_.scriptStatus, Metrics::kSmallFont + 1.f,
                            theme_.textDim, width_ - innerX_ - Metrics::kPad);
            nextRow();
            button(ctrlX_, T("Reload scripts"), [this] { hooks_.reloadScripts(); }, Metrics::kEditW);
            nextRow();
        } else {
            canvas_.addText(innerX_, y_ + 5.f, T("Scripts run while the game is playing (Play)."), Metrics::kSmallFont + 1.f, theme_.textDim,
                            width_ - innerX_ - Metrics::kPad);
            nextRow();
        }
        if (!ctx_.scriptRuntime) {
            canvas_.addText(innerX_, y_ + 5.f, T("This build has no script runtime (QuickJS is off)."), Metrics::kSmallFont + 1.f, warnColor_,
                            width_ - innerX_ - Metrics::kPad);
            nextRow();
        }
        endSection();
    }

    // Tags do objeto: uma linha por tag atribuida (Remove). Duplo clique no nome abre a renomeacao (Enter confirma,
    // Esc cancela); tags embutidas nao renomeiam. Depois, campo para criar e Add tag, e a lista das tags existentes.
    void tags() {
        if (!beginSection(im::Section::Tags, T("Tags"))) return;
        for (std::size_t i = 0; i < ctx_.tagNames.size() && i < ctx_.tagOn.size(); ++i) {
            if (!ctx_.tagOn[i]) continue;
            if (renaming_ == static_cast<int>(i)) {
                tagRenameRow(i);
                continue;
            }
            const bool builtin = i < ctx_.tagBuiltin.size() && ctx_.tagBuiltin[i];
            label(y_, ctx_.tagNames[i].c_str());
            Control& hit = add(Kind::Button, rect(innerX_, y_, labelW_, Metrics::kRowH), nextKey());
            hit.flat = true;
            hit.disabled = disabled_;
            hit.onClick = [this, i, builtin] { onTagNameClick(i, builtin); };
            button(ctrlX_, T("Remove"), [this, i] { hooks_.removeTagIndex(i); }, Metrics::kButtonW);
            nextRow();
        }
        textRow(T("New tag"), newTag_, sizeof newTag_);
        button(ctrlX_, T("Add tag"), [this] {
            if (disabled_ || newTag_[0] == 0) return;
            tagMsg_ = hooks_.addTagNamed(newTag_) ? std::string() : std::string(T("Too many tags (limit 128)."));
            newTag_[0] = 0;
        }, Metrics::kButtonW);
        nextRow();
        std::vector<std::string> options;
        std::vector<std::size_t> ids;
        for (std::size_t i = 0; i < ctx_.tagNames.size() && i < ctx_.tagOn.size(); ++i) {
            if (ctx_.tagOn[i]) continue;
            options.push_back(ctx_.tagNames[i]);
            ids.push_back(i);
        }
        drop(T("Existing tag"), options, options.size(), [this, ids](std::size_t k) {
            if (k < ids.size()) hooks_.addTagIndex(ids[k]);
        });
        if (!tagMsg_.empty()) {
            canvas_.addText(innerX_, y_, tagMsg_, Metrics::kFont, theme_.error, width_ - innerX_ - Metrics::kPad);
            nextRow();
        }
        endSection();
    }

    // Clique no nome da tag: dois cliques seguidos (ate 400 ms) comecam a renomear.
    void onTagNameClick(std::size_t i, bool builtin) {
        if (disabled_ || builtin) return;
        const std::uint32_t now = SDL_GetTicks();
        const bool twice = lastTagClick_ == static_cast<int>(i) && now - lastTagClickMs_ < 400u;
        lastTagClick_ = static_cast<int>(i);
        lastTagClickMs_ = now;
        if (!twice) return;
        renaming_ = static_cast<int>(i);
        std::snprintf(renameBuf_, sizeof renameBuf_, "%s", ctx_.tagNames[i].c_str());
        renameFocus_ = true;
        tagMsg_ = T("Enter confirms, Esc cancels.");
        lastTagClick_ = -1;
    }

    // Linha da tag em renomeacao: campo com o nome. Enter grava (hook renameTagIndex); Esc desfaz.
    void tagRenameRow(std::size_t i) {
        label(y_, ctx_.tagNames[i].c_str());
        Control& c = add(Kind::Text, rect(ctrlX_, y_, ctrlW_, Metrics::kRowH), nextKey());
        c.text = renameBuf_;
        c.setText = [this](const std::string& t) { std::snprintf(renameBuf_, sizeof renameBuf_, "%s", t.c_str()); };
        c.onEnter = [this, i] {
            const std::string to = renameBuf_;
            renaming_ = -1;
            if (hooks_.renameTagIndex && hooks_.renameTagIndex(i, to)) tagMsg_.clear();
            else tagMsg_ = std::string(T("Could not rename: the name is empty, too long, already used, or a built-in tag."));
        };
        c.onCancel = [this] { renaming_ = -1; tagMsg_.clear(); };
        if (renameFocus_) {
            renameFocus_ = false;
            startEdit(c, SDL_GetKeyboardFocus());
        }
        nextRow();
    }

    // Secao Audio Source: arquivo, volume, loop, 3D, distancias e botoes de ouvir/parar.
    void audio() {
        AudioSource& a = *ctx_.audio;
        if (!beginSection(im::Section::Audio, T("Audio Source"))) return;
        assetRow(T("Sound file"), editor::AssetKind::Audio, a.clip, sizeof a.clip);
        slider(T("Volume"), &a.volume, {0.f, 2.f});
        checkField(T("Loop"), &a.loop);
        checkField(T("Play on start"), &a.playOnStart);
        checkField(T("3D sound (needs a Sound Ear)"), &a.spatial);
        if (a.spatial) {
            slider(T("Min distance"), &a.minDistance, {0.1f, 100.f});
            slider(T("Max distance"), &a.maxDistance, {1.f, 1000.f});
        }
        button(ctrlX_, T("Preview"), [this, &a] {
            if (a.clip[0]) hooks_.previewSound(a.clip);
        });
        button(ctrlX_ + Metrics::kButtonW + Metrics::kGap, T("Stop"), [this] { hooks_.stopSound(); });
        nextRow();
        endSection();
    }

    // ------------------------------------------------------------------ controles: desenho
    void appendControl(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint, const Control& c, std::vector<SR::ClipRectGPU>& clips) {
        switch (c.kind) {
            case Kind::Header:
            case Kind::Swatch:
            case Kind::Num:
                if (c.kind == Kind::Num && editing(c.key)) appendField(items, paint, c.box, clips);
                break;
            case Kind::Slider: {
                Elm::Slider& s = sliders_[c.key];
                s.border = c.box;
                s.states.disabled = c.disabled;
                ib::SyncSlider(s, c.range, c.value);
                append(items, paint, Elm::BuildSliderItems(s, sliderStyle_, Metrics::kZControl, paint));
                break;
            }
            case Kind::Check: {
                Elm::Checkbox& cb = checks_[c.key];
                cb.border = c.box;
                cb.states.disabled = c.disabled;
                ib::SyncCheckbox(cb, c.checked);
                append(items, paint, Elm::BuildCheckboxItems(cb, checkStyle_, Metrics::kZControl, paint));
                break;
            }
            case Kind::Drop: {
                Elm::Dropdown& dd = drops_[c.key];
                dd.border = c.box;
                dd.states.disabled = c.disabled;
                dd.control.disabled = c.disabled;
                ib::SyncDropdown(dd, c.options, c.index);
                append(items, paint, Elm::BuildDropdownButtonItems(dd, dropdownStyle_, Metrics::kZControl, paint));
                break;
            }
            case Kind::Text:
                if (editing(c.key)) appendField(items, paint, c.box, clips);
                break;
            case Kind::Button: {
                Elm::Button& b = buttons_[c.key];
                b.border = c.box;
                b.states.disabled = c.disabled;
                if (!c.flat) append(items, paint, Elm::BuildButtonBackground(b, buttonStyle_, Metrics::kZControl, paint));
                break;
            }
            case Kind::Cell:   // celula da grade do voxel: a cor ja foi para o canvas; so recebe clique
                break;
        }
    }

    // Texto de um campo em edicao: caixa, selecao e caret do TextField da biblioteca.
    void appendField(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint, const SU::Rect& box, std::vector<SR::ClipRectGPU>& clips) {
        Edit::TextFieldRenderTarget target;
        target.owner = sui::handleFor(0);
        target.textureSlice = canvas_.sliceFor(Metrics::kFont);
        target.zOrder = Metrics::kZField;
        target.paintOrder = paint;
        Edit::TextFieldStyle style;
        style.background = fieldColor_;
        style.focusBackground = fieldFocusColor_;
        style.text = theme_.text;
        style.selection = selectionColor_;
        style.caret = theme_.text;
        style.paddingX = 6.f;
        style.height = box.size.y;
        style.fontSize = editor::ui_style::uiFont(Metrics::kFont);
        const ST::TextLayoutResult layout = canvas_.layout(field_.Edit().Text(), Metrics::kFont);
        const SU::Rect screen = toScreen(box);   // Build trabalha em coordenadas da tela (e o recorte tambem)
        std::vector<SR::UIRenderItem> built = field_.Build(screen, layout, style, target, clips);
        paint += built.size() + 1;
        items.insert(items.end(), built.begin(), built.end());
    }

    // Retangulo do bloco (local) -> coordenadas da tela.
    [[nodiscard]] SU::Rect toScreen(const SU::Rect& r) const {
        return rect(r.position.x + origin_.x, r.position.y + origin_.y, r.size.x, r.size.y);
    }

    // Texto de cada opcao de uma lista aberta (por cima dos controles).
    void dropdownRowTexts(const Elm::Dropdown& dd) {
        for (std::size_t i = 0; i < dd.control.options.size(); ++i) {
            const SU::Rect r = Elm::DropdownRowRect(dd, i);
            overlay_.addText(r.position.x + 8.f, r.position.y + 6.f, dd.control.options[i], Metrics::kFont, theme_.text, r.size.x - 16.f);
        }
    }

    // Copia os itens de um builder para a lista: posicao local do bloco somada a origem na tela.
    void append(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint, std::vector<SR::UIRenderItem> built) const {
        for (SR::UIRenderItem& item : built) {
            item.handle = sui::handleFor(0);
            item.clipIndex = 0u;
            item.layout.resolvedPosition.x += origin_.x;
            item.layout.resolvedPosition.y += origin_.y;
        }
        paint += built.size() + 1;
        items.insert(items.end(), built.begin(), built.end());
    }

    // ------------------------------------------------------------------ seletor de cor
    // O seletor e o popup compartilhado (color_popup.hpp): aqui so abre na amostra e entrega o desenho e o ponteiro.
    void openPicker(std::uint32_t key, float* rgb) {
        closePopups();
        // Fica abaixo da amostra, sem sair da largura do bloco.
        const SU::Rect sw = swatchBox(key);
        const float x = std::min(sw.position.x, std::max(0.f, width_ - Metrics::kPopupW - 2.f));
        popup_.open(key, rgb, rect(x, sw.position.y + sw.size.y + 4.f, Metrics::kPopupW, Metrics::kPopupH));
    }

    // Caixa da amostra de cor com essa chave (para ancorar o popup de cor).
    [[nodiscard]] SU::Rect swatchBox(std::uint32_t key) const {
        for (const Control& c : controls_) {
            if (c.key == key) return c.box;
        }
        return rect(ctrlX_, Metrics::kPad, Metrics::kSwatchW, Metrics::kRowH);
    }

    // Fecha o popup de cor e todas as listas abertas.
    void closePopups() {
        popup_.close();
        for (auto& [k, dd] : drops_) (void)dd.control.Close();
    }

    // Visual do popup de cor a partir das cores do painel.
    [[nodiscard]] color_popup::Look popupLook() const {
        color_popup::Look look;
        look.frame = frameStyle_;
        look.field = fieldColor_;
        look.fieldFocus = fieldFocusColor_;
        look.text = theme_.text;
        look.selection = selectionColor_;
        look.font = Metrics::kFont;
        look.fieldFont = editor::ui_style::uiFont(Metrics::kFont);
        look.zPanel = Metrics::kZPopup;
        return look;
    }

    // Lista aberta do seletor: fundo, area de cor, campo HEX e canais, no quadro do painel.
    void appendPopup(std::vector<SR::UIRenderItem>& items, std::uint64_t& paint) {
        popup_.setLook(popupLook());
        popup_.append(items, paint, sui::handleFor(0), SU::Vec2{origin_.x, origin_.y}, 0u,
                      rect(visible_.x, visible_.y, visible_.w, visible_.h));
    }

    // ------------------------------------------------------------------ eventos
    // Ponteiro: Down e Up/Move com captura. Fora da janela visivel, um Down fecha listas e campo.
    bool pointer(Elm::PointerPhase phase, float x, float y, SDL_WindowID windowId) {
        SDL_Window* window = SDL_GetWindowFromID(windowId);
        const SU::Vec2 p{x - origin_.x, y - origin_.y};
        const bool inWindow = sui::Box{visible_.x, visible_.y, visible_.w, visible_.h}.contains(x, y);
        if (phase == Elm::PointerPhase::Down) {
            if (!inWindow) {   // clique fora do bloco: fecha listas e o campo em edicao
                closePopups();
                endEdit(window);
                return false;
            }
            captured_ = true;
        } else if (phase == Elm::PointerPhase::Up) {
            const bool wasCaptured = captured_;
            captured_ = false;
            if (!wasCaptured) return false;
        }
        return routePointer(phase, p, window);
    }

    // Roteia o ponteiro (mover/apertar/soltar): fecha campo em edicao se clicou fora, trata popup de cor, listas abertas e depois cada tipo de controle.
    bool routePointer(Elm::PointerPhase phase, SU::Vec2 p, SDL_Window* window) {
        // Clique fora do campo em edicao (que nao seja o HEX, tratado no seletor) encerra a edicao.
        if (phase == Elm::PointerPhase::Down && field_kind_ != FieldKind::None && !contains(field_box_, p)) {
            endEdit(window);
        }
        // Lista aberta de escolha: so ela recebe o ponteiro.
        for (Control& c : controls_) {
            if (c.kind != Kind::Drop || !drops_[c.key].control.open) continue;
            applyDrop(c, phase, p);
            return true;
        }
        if (field_kind_ != FieldKind::None && field_.DraggingSelection()) {
            if (phase == Elm::PointerPhase::Move) {
                const ST::TextLayoutResult lay = canvas_.layout(field_.Edit().Text(), Metrics::kFont);
                field_.PointerMove(SU::Vec2{p.x - field_box_.position.x, p.y - field_box_.position.y},
                                  SU::Vec2{field_box_.size.x, field_box_.size.y}, lay, fieldStyle(field_box_));
                return true;
            }
            if (phase == Elm::PointerPhase::Up) {
                field_.PointerUp();
                return true;
            }
        }
        // Seletor de cor aberto: arrastes na area e nos campos.
        if (popup_.isOpen()) {
            if (popup_.contains(p)) {
                popup_.pointer(phase, p, window);
                return true;
            }
            if (phase == Elm::PointerPhase::Down) closePopups();
        }
        // Clique fora do campo de valor de um deslizante: confirma o digitado e solta o campo antes do clique.
        if (phase == Elm::PointerPhase::Down && field_slider_ && !contains(field_box_, p)) {
            commitSliderField();
            endEdit(window);
        }
        bool any = false;
        for (Control& c : controls_) {
            if (c.disabled && c.kind != Kind::Header) continue;
            switch (c.kind) {
                case Kind::Header:
                case Kind::Swatch:
                case Kind::Button:
                case Kind::Cell:
                    if (buttonClick(c, phase, p)) any = true;
                    break;
                case Kind::Slider: {
                    Elm::Slider& s = sliders_[c.key];
                    const Elm::SliderEvents ev = s.OnPointer(event(phase, p, c.box));
                    if (ev.valueChanged) {
                        float out = c.value;
                        if (im::AcceptInRange(s.Value(), c.range, out) && c.setF) c.setF(out);
                    }
                    if (ev.pressStarted || ev.valueChanged || ev.pressEnded) any = true;
                    break;
                }
                case Kind::Check: {
                    Elm::Checkbox& cb = checks_[c.key];
                    const Elm::CheckboxEvents ev = cb.OnPointer(event(phase, p, c.box));
                    if (ev.valueChanged && c.setB) c.setB(cb.checked);
                    if (ev.pressStarted || ev.clicked) any = true;
                    break;
                }
                case Kind::Drop:
                    if (contains(c.box, p) || phase == Elm::PointerPhase::Up) {
                        applyDrop(c, phase, p);
                        any = true;
                    }
                    break;
                case Kind::Num:
                case Kind::Text:
                    if (phase == Elm::PointerPhase::Down && contains(c.box, p)) {
                        if (!editing(c.key)) startEdit(c, window);   // clicar de novo no campo so move o caret
                        const ST::TextLayoutResult lay = canvas_.layout(field_.Edit().Text(), Metrics::kFont);
                        field_.PointerDown(SU::Vec2{p.x - c.box.position.x, p.y - c.box.position.y}, c.box.size, lay, fieldStyle(c.box));
                        any = true;
                    }
                    break;
            }
        }
        if (phase == Elm::PointerPhase::Down && !any) endEdit(window);
        return any;
    }

    // Botao, cabecalho e amostra de cor: clique (Up dentro com Down dentro).
    bool buttonClick(Control& c, Elm::PointerPhase phase, SU::Vec2 p) {
        Elm::Button& b = buttons_[c.key];
        const Elm::ButtonEvents ev = b.OnPointer(event(phase, p, c.box));
        if (ev.clicked && c.onClick) c.onClick();
        return ev.pressStarted || ev.clicked;
    }

    // Aplica o ponteiro a uma lista (dropdown): abrir, escolher uma opcao ou fechar.
    void applyDrop(Control& c, Elm::PointerPhase phase, SU::Vec2 p) {
        Elm::Dropdown& dd = drops_[c.key];
        const SU::Rect window{{0.f, visible_.y - origin_.y}, {width_, visible_.h}};
        const bool onList = dd.control.open && contains(dd.Popup(), p);
        Elm::DropdownEvents ev;
        if (onList) {
            if (phase == Elm::PointerPhase::Up) ev = dd.ChooseAt(p, window);
        } else {
            ev = dd.OnPointer(event(phase, p, dd.border), window);
        }
        if (ev.opened) {
            for (auto& [k, other] : drops_) {
                if (&other != &dd) (void)other.control.Close();
            }
        }
        if (ev.valueChanged && c.setIndex) c.setIndex(dd.control.selectedIndex);
    }

    // Monta o evento de ponteiro relativo a caixa do controle.
    [[nodiscard]] Elm::PointerEvent event(Elm::PointerPhase phase, SU::Vec2 p, const SU::Rect& box) const {
        return Elm::PointerEvent{phase, p, 0u, contains(box, p)};
    }

    // ------------------------------------------------------------------ campo de texto
    [[nodiscard]] Edit::TextFieldStyle fieldStyle(const SU::Rect& box) const {
        Edit::TextFieldStyle style;
        style.background = fieldColor_;
        style.focusBackground = fieldFocusColor_;
        style.text = theme_.text;
        style.selection = selectionColor_;
        style.caret = theme_.text;
        style.paddingX = 6.f;
        style.height = box.size.y;
        style.fontSize = editor::ui_style::uiFont(Metrics::kFont);
        return style;
    }

    // Foco num campo: copia o texto atual para o editor da biblioteca. Numero ou texto dependem do controle.
    void startEdit(Control& c, SDL_Window* window) {
        field_key_ = c.key;
        field_ = Edit::TextField(c.text);
        field_.SetFocus(true);
        field_kind_ = c.kind == Kind::Num ? FieldKind::Number : FieldKind::Text;
        field_slider_ = c.sliderValue;
        edit_value_ = c.value;
        field_box_ = c.box;
        edit_range_ = c.range;
        edit_set_f_ = c.setF;
        edit_set_text_ = c.setText;
        edit_on_enter_ = c.onEnter;
        edit_on_cancel_ = c.onCancel;
        if (window) SDL_StartTextInput(window);
    }

    // Encerra a edicao do campo de texto (sem aplicar) e desliga a entrada de texto do SDL.
    void endEdit(SDL_Window* window = nullptr) {
        if (field_kind_ == FieldKind::None) return;
        field_.SetFocus(false);
        field_kind_ = FieldKind::None;
        field_slider_ = false;
        field_key_ = 0;
        edit_set_f_ = nullptr;
        edit_set_text_ = nullptr;
        edit_on_enter_ = nullptr;
        edit_on_cancel_ = nullptr;
        if (window) SDL_StopTextInput(window);
    }

    // Escreve o texto digitado no objeto. Numero invalido ou fora do texto nao escreve nada.
    // Valor de um deslizante digitado: so grava ao confirmar (Enter ou clique fora), com o mesmo limite do arraste.
    void commitSliderField() {
        const float out = slider_edit::CommitSliderText(field_.Edit().Text(), edit_value_, edit_range_.min, edit_range_.max,
                                                        0.f, false);
        if (out != edit_value_ && edit_set_f_) edit_set_f_(out);
    }

    // Aplica o texto digitado: numero valido vira o valor do controle; texto livre grava no buffer.
    void commitField() {
        if (field_slider_) return;   // digitar so muda o texto; o valor entra em commitSliderField
        const std::string text = field_.Edit().Text();
        if (field_kind_ == FieldKind::Number) {
            float out = 0.f;
            if (im::ParseNumberInRange(text, edit_range_, out) && edit_set_f_) edit_set_f_(out);
        } else if (field_kind_ == FieldKind::Text) {
            if (edit_set_text_) edit_set_text_(text);
        }
    }

    // Teclas no campo em edicao: Esc cancela, Enter confirma; as demais edicoes (setas, apagar, atalhos) vao para text_keys::ApplyEditKey.
    void keyInField(SDL_Keycode key, SDL_Keymod mod, SDL_WindowID windowId) {
        SDL_Window* window = SDL_GetWindowFromID(windowId);
        if (key == SDLK_ESCAPE) {
            const std::function<void()> cancel = edit_on_cancel_;
            endEdit(window);
            if (cancel) cancel();
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (field_slider_) commitSliderField();
            const std::function<void()> enter = edit_on_enter_;
            endEdit(window);
            if (enter) enter();
            return;
        }
        if (!text_keys::ApplyEditKey(field_, key, mod)) return;
        commitField();
    }

    // ------------------------------------------------------------------ estado
    Context ctx_;
    Hooks hooks_;
    float width_ = 0.f, y_ = 0.f, labelW_ = 0.f, innerX_ = 0.f, ctrlX_ = 0.f, ctrlW_ = 0.f, groupTop_ = 0.f;
    static constexpr float kWheelStep = 40.f;   // pixels por giro da roda
    ui_scrollbar::Scrollbar bar_;
    float scrollY_ = 0.f;     // quanto o conteudo subiu (>= 0)
    float contentH_ = 0.f;    // altura total do conteudo medida no ultimo layout (sem a rolagem)
    // Prende a rolagem entre 0 e o que sobra do conteudo alem da area visivel.
    void clampScroll() {
        const float maxScroll = std::max(0.f, contentH_ - visible_.h);
        scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
    }
    std::uint32_t keySeq_ = 1;
    bool disabled_ = false;
    unsigned lastObject_ = 0;
    char newTag_[48] = "";        // campo "New tag" (buffer do componente; sobrevive entre quadros)
    std::string tagMsg_;          // aviso da secao Tags (limite de tags)
    int voxelBrush_ = 1;          // pincel do voxel (0 = apagar); guardado entre quadros
    int voxelLayer_ = 0;          // camada (y) mostrada na grade

    sui::Canvas canvas_;
    sui::Canvas overlay_;
    sui::SpriteUiSdlRenderer& sdl_;
    framed::Chrome chrome_;
    sui::Theme theme_;
    SR::RenderExtractor extractor_;
    SR::FramePacket packet_;
    std::uint64_t frameId_ = 0;

    sui::Box origin_;
    sui::Box visible_;
    bool shown_ = false;
    bool captured_ = false;

    im::Expanders expanders_;
    std::vector<Control> controls_;
    std::vector<SU::Rect> frames_;

    std::unordered_map<std::uint32_t, Elm::Slider> sliders_;
    std::unordered_map<std::uint32_t, Elm::Checkbox> checks_;
    std::unordered_map<std::uint32_t, Elm::Dropdown> drops_;
    std::unordered_map<std::uint32_t, Elm::Button> buttons_;

    Edit::TextField field_;
    FieldKind field_kind_ = FieldKind::None;
    std::uint32_t field_key_ = 0;
    SU::Rect field_box_{};   // caixa local do campo em edicao (Number, Text)
    im::Range edit_range_{};
    bool field_slider_ = false;   // o campo em edicao e o valor de um deslizante (ver commitSliderField)
    float edit_value_ = 0.f;      // valor do objeto quando a edicao comecou (base do Esc e do "sem mudanca")
    std::function<void(float)> edit_set_f_;
    std::function<void(const std::string&)> edit_set_text_;
    std::function<void()> edit_on_enter_, edit_on_cancel_;
    // Renomear tag: a tag em edicao, o texto novo e o pedido para o campo ganhar foco no quadro seguinte.
    int renaming_ = -1;
    char renameBuf_[48] = "";
    bool renameFocus_ = false;
    int lastTagClick_ = -1;
    std::uint32_t lastTagClickMs_ = 0;

    color_popup::ColorPopup popup_{canvas_, overlay_};

    // Cores derivadas da paleta em uso, refeitas a cada quadro em draw(). warnColor_ e okColor_ ficam fixos (semantica).
    SU::Vec4 headerColor_, headingColor_, fieldColor_, fieldFocusColor_, fieldDisabled_, selectionColor_, swatchBorder_;
    SU::Vec4 warnColor_ = sui::Srgb(1.f, 0.6f, 0.3f, 1.f);
    SU::Vec4 okColor_ = sui::Srgb(0.6f, 0.9f, 0.6f, 1.f);

    Elm::PanelStyle frameStyle_{};
    Elm::SliderStyle sliderStyle_{};
    Elm::CheckboxStyle checkStyle_{};
    Elm::DropdownStyle dropdownStyle_{};
    Elm::ButtonStyle buttonStyle_{};
};

}  // namespace editor::inspector_panel
