#pragma once
// Janela do Controlador Narrativo em SpriteUI. Dados e regras: narrative_model.hpp.
//
// Layout do corpo (coordenadas locais, a partir do canto superior esquerdo do corpo):
//   - barra: New, Save, Compile, Open, campo de caminho e status;
//   - esquerda: variaveis (valor editavel, remover) e campo para criar uma;
//   - centro: grafo (nos arrastaveis, ligacoes saida -> entrada, pan arrastando o fundo, menu com clique direito);
//   - direita: propriedades do no selecionado e teste rapido (Fire).
//
// Mouse: a captura (pointer_capture.hpp) decide o arraste. Movimento sem botao so faz hover, nunca arrasta, e um
// Up fora do corpo ainda solta o que estava pressionado.
// Teclado: a digitacao so entra quando um campo esta focado; Delete apaga o no selecionado; Esc cancela.
// Os alvos de clique sao registrados a cada quadro e testados do ultimo desenhado para o primeiro (o de cima ganha).

#include "framed_window.hpp"
#include "i18n.hpp"
#include "narrative_model.hpp"
#include "pointer_capture.hpp"
#include "sui_panel.hpp"
#include "spriteui_host.hpp"
#include "text_atlas_set.hpp"
#include "ui_style.hpp"

#include "SpriteUi/Rendering/DrawList.hpp"
#include "SpriteUi/Rendering/RenderFlags.hpp"
#include "SpriteUi/Rendering/SpriteInstanceGPU.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;

class NarrativePanel {
public:
  NarrativePanel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl, NarrativeModel& model)
      : chrome_{text, sdl}, canvas_{text}, graphCanvas_{text}, sdl_{sdl}, model_{model} {}

  // Moldura (titulo, "?", minimizar, X, grip e acoplamento ao dock). O host usa para o dock e para o foco.
  framed::Chrome& chrome() { return chrome_; }

  // Eventos do SDL. True quando a janela usou o evento.
  bool handleEvent(const SDL_Event& e) {
    if (chrome_.handleEvent(e)) return true;
    if (!chrome_.visible()) return false;
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (e.button.button != SDL_BUTTON_LEFT && e.button.button != SDL_BUTTON_RIGHT) return false;
        const sui::Box body = body_;
        const float x = e.button.x - body.x, y = e.button.y - body.y;
        if (!body.contains(x, y)) {
          releaseKeyboard();
          return false;
        }
        keyboardOwned_ = true;   // clique dentro: teclas e digitacao vao para esta janela
        if (e.button.button == SDL_BUTTON_RIGHT) {
          rightClick(x, y);
          return true;
        }
        pointerDown(x, y);
        capture_.down();
        return true;
      }
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        if (!capture_.up()) return false;
        pointerUp(e.button.x - body_.x, e.button.y - body_.y);
        return true;
      }
      case SDL_EVENT_MOUSE_MOTION: {
        const float x = e.motion.x - body_.x, y = e.motion.y - body_.y;
        lastX_ = x;
        lastY_ = y;
        if (capture_.move((e.motion.state & SDL_BUTTON_LMASK) != 0)) {   // botao solto sem Up: solta o arraste
          pointerUp(x, y);
          return true;
        }
        if (!capture_.active()) return false;   // so hover: sem botao nao ha arraste
        pointerMove(x, y);
        return true;
      }
      case SDL_EVENT_TEXT_INPUT:
        if (!keyboardOwned_ || focus_ == Focus::None || !e.text.text) return false;
        buffer_ += e.text.text;
        return true;
      case SDL_EVENT_KEY_DOWN:
        return keyboardOwned_ && keyDown(e.key.key);
      default:
        return false;
    }
  }

  // Clique em outra janela (ou fora daqui): solta o teclado e aplica o campo em edicao.
  void releaseKeyboard() {
    commitFocus();
    closeMenu();
    keyboardOwned_ = false;
  }

  // Desenha a janela. Fica escondida (sem moldura) quando `visible` e falso.
  bool draw(SDL_Renderer* renderer, float winW, float winH, bool visible) {
    if (!visible) {
      chrome_.hide();
      return true;
    }
    const sui::Box body = chrome_.prepare(winW, winH, kWindowW, kWindowH);
    if (body.w <= 0.f || body.h <= 0.f) return true;   // minimizada
    body_ = body;
    if (SDL_Window* win = renderer ? SDL_GetRenderWindow(renderer) : nullptr; win && (focus_ != Focus::None) != textInputOn_) {
      if (focus_ != Focus::None) SDL_StartTextInput(win);
      else SDL_StopTextInput(win);
      textInputOn_ = focus_ != Focus::None;
    }
    if (!chrome_.draw(renderer, editor::T("Narrative Controller"))) return false;

    theme_ = sui::Theme{};
    hits_.clear();
    canvas_.clear();
    graphCanvas_.clear();
    const float scale = editor::ui_style::textScale();
    canvas_.setFontScale(scale);
    graphCanvas_.setFontScale(scale);
    layout(body);

    const SU::UIHandle owner = sui::handleFor(0);
    std::vector<SR::UIRenderItem> items;
    std::uint64_t paint = 1;
    const float bottom = body.y + body.h, top = body.y;
    items.push_back(sui::Canvas::solidItem(body.x, body.y, body.w, body.h, theme_.panel, 0.f, owner, kBodyClip, paint++));
    canvas_.appendItems(items, body.x, body.y, 0.f, top, bottom, owner, kBodyClip, paint);
    graphCanvas_.appendItems(items, body.x, body.y, 0.f, top, bottom, owner, kGraphClip, paint);

    packet_ = extractor_.Build(++frameId_, items, nullptr, clipsFor(body));
    if (!SR::IsFramePacketValid(packet_)) return true;
    return sdl_.draw(renderer, packet_);
  }

private:
  enum class Focus : std::uint8_t { None, Path, NewVar, VarValue, Prop, Event };
  enum class Hit : std::uint8_t { Button, Field, VarValue, VarRemove, Dropdown, Grip, Node, Port, Link, Canvas, MenuItem };

  struct HitItem {
    sui::Box box;
    Hit kind = Hit::Canvas;
    std::string key;   // botao, campo ou variavel
    std::size_t index = 0;   // indice do no ou da ligacao
    std::string port;   // "out", "then", "else" ou "in"
  };

  static constexpr float kWindowW = 880.f, kWindowH = 560.f;
  static constexpr float kToolbarH = 40.f, kLeftW = 200.f, kRightW = 270.f;
  static constexpr float kNodeW = 180.f, kNodeH = 62.f, kTitleH = 22.f, kPort = 9.f;
  static constexpr float kFontSize = 12.f, kLabelSize = 13.f;
  // Altura dos pontos de conexao dentro do no (saida padrao, then e else do branch) e largura da alca de arraste.
  static constexpr float kPortY = 40.f, kThenY = 30.f, kElseY = 50.f, kGripW = 18.f;
  static constexpr float kGridStep = 24.f, kLinkW = 2.f, kLinkHit = 7.f;
  static constexpr std::size_t kBodyClip = 0, kGraphClip = 1;

  // Layout do corpo. Desenha os controles nos canvas e registra os alvos de clique.
  void layout(const sui::Box& body) {
    const float pad = 8.f;
    const float W = body.w, H = body.h;
    const SU::Vec4 text = theme_.text, dim = theme_.textDim;

    // Barra: botoes e campo de caminho.
    const char* buttons[] = {"New", "Save", "Compile", "Open"};
    const char* keys[] = {"new", "save", "compile", "open"};
    float bx = pad;
    for (std::size_t i = 0; i < 4; ++i) {
      const float bw = 70.f;
      button(bx, pad, bw, 24.f, editor::T(buttons[i]), keys[i]);
      bx += bw + 6.f;
    }
    fieldRow(bx + 8.f, pad, W - bx - 16.f - 150.f, 24.f, "", focus_ == Focus::Path ? buffer_ : model_.path(), Focus::Path, "path");
    canvas_.addText(W - 150.f - pad + 4.f, pad + 6.f, model_.status(), kFontSize, dim, 142.f);
    canvas_.addText(pad, pad + 26.f, model_.compileMessage(), kFontSize, model_.compileOk() ? theme_.accent : theme_.error, W - 2 * pad);

    // Variaveis (esquerda).
    const float leftX = pad, topY = kToolbarH + 22.f;
    canvas_.addText(leftX, topY, editor::T("Variables"), kLabelSize, text, kLeftW - pad);
    float y = topY + 24.f;
    if (const tool::Json* vars = const_cast<NarrativeModel&>(model_).varList()) {
      for (const auto& [name, v] : vars->members()) {
        canvas_.addText(leftX, y + 4.f, name, kFontSize, dim, 76.f);
        const std::string value = numberText(v["value"].asNumber());
        const bool editing = focus_ == Focus::VarValue && focusKey_ == name;
        fieldRow(leftX + 80.f, y, 70.f, 22.f, "", editing ? buffer_ : value, Focus::VarValue, "var:" + name);
        hits_.push_back({{leftX + 156.f, y, 22.f, 22.f}, Hit::VarRemove, name, 0, ""});
        smallButton(leftX + 156.f, y, 22.f, 22.f, "x");
        y += 26.f;
      }
    }
    fieldRow(leftX, y + 6.f, 116.f, 22.f, "", focus_ == Focus::NewVar ? buffer_ : std::string(), Focus::NewVar, "newvar");
    button(leftX + 122.f, y + 6.f, 46.f, 22.f, "+", "add-var");

    // Propriedades (direita).
    const float rx = W - kRightW + pad;
    const float rw = kRightW - 2 * pad;
    canvas_.addText(rx, topY, editor::T("Properties"), kLabelSize, text, rw);
    float py = topY + 24.f;
    py = propertyRows(rx, py, rw);
    py += 14.f;
    canvas_.addText(rx, py, editor::T("Quick test"), kLabelSize, text, rw);
    py += 26.f;
    fieldRow(rx, py, rw - 62.f, 22.f, "", focus_ == Focus::Event ? buffer_ : testEvent_, Focus::Event, "event");
    button(rx + rw - 56.f, py, 56.f, 22.f, editor::T("Fire"), "fire");
    py += 34.f;
    const auto& log = model_.testLog();
    for (std::size_t i = 0; i < log.size() && i < 8; ++i) {
      canvas_.addText(rx, py, log[i], kFontSize, dim, rw);
      py += 16.f;
    }

    // Grafo (centro).
    graphBox_ = {kLeftW, kToolbarH + 22.f, std::max(10.f, W - kLeftW - kRightW), std::max(10.f, H - kToolbarH - 30.f)};
    graphCanvas_.addSolid(graphBox_.x, graphBox_.y, graphBox_.w, graphBox_.h, theme_.surface, 0.1f, 0.f);
    drawGrid();
    hits_.push_back({graphBox_, Hit::Canvas, "", 0, ""});
    drawNodes();
    drawLinks();
    drawLinkPreview();
    if (menu_) drawMenu();
  }

  // Campo focado? Variaveis e propriedades guardam o nome da chave em focusKey_.
  bool isFocused(Focus f, const std::string& key) const {
    if (focus_ != f) return false;
    if (f == Focus::VarValue) return key == "var:" + focusKey_;
    if (f == Focus::Prop) return key == focusKey_;
    return true;
  }

  // Botao com rotulo centralizado; registra o alvo.
  void button(float x, float y, float w, float h, const std::string& label, const std::string& key) {
    const bool hot = mouseIn({x, y, w, h});
    canvas_.addSolid(x, y, w, h, hot ? theme_.accent : theme_.surface, 0.3f, 4.f);
    canvas_.addText(x + 6.f, y + (h - 14.f) * 0.5f, label, kFontSize, theme_.text, w - 12.f);
    hits_.push_back({{x, y, w, h}, Hit::Button, key, 0, ""});
  }
  void smallButton(float x, float y, float w, float h, const std::string& label) {
    canvas_.addSolid(x, y, w, h, theme_.surface, 0.3f, 4.f);
    canvas_.addText(x + 7.f, y + 4.f, label, kFontSize, theme_.textDim, w - 8.f);
  }

  // Campo de texto: caixa + valor (com cursor quando focado). Registra o alvo de foco.
  void fieldRow(float x, float y, float w, float h, const std::string& label, const std::string& value, Focus focus, const std::string& key) {
    if (!label.empty()) canvas_.addText(x, y - 14.f, label, kFontSize, theme_.textDim, w);
    const bool focused = isFocused(focus, key);
    canvas_.addSolid(x, y, w, h, focused ? theme_.accent : theme_.surface, 0.3f, 4.f);
    std::string shown = value;
    if (focused) shown += "|";
    canvas_.addText(x + 6.f, y + (h - 14.f) * 0.5f, shown, kFontSize, theme_.text, w - 12.f);
    hits_.push_back({{x, y, w, h}, key.rfind("var:", 0) == 0 ? Hit::VarValue : Hit::Field, key, 0, ""});
  }

  // Linhas de propriedades do no selecionado. Devolve a nova altura.
  float propertyRows(float x, float y, float w) {
    const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList();
    const int idx = selectedIndex();
    if (!nodes || idx < 0) {
      canvas_.addText(x, y, editor::T("select a node"), kFontSize, theme_.textDim, w);
      return y + 20.f;
    }
    const std::size_t i = static_cast<std::size_t>(idx);
    const tool::Json& nd = nodes->items()[i];
    const std::string type = nd["type"].asString();
    const bool pinned = nd.has("pinned");
    canvas_.addText(x, y, type, kFontSize, theme_.accent, w);
    y += 22.f;
    if (pinned) {
      canvas_.addText(x, y, editor::T("Scene block: it can't be deleted. Its name follows the scene."), kFontSize, theme_.textDim, w);
      y += 30.f;
    } else {
      y = textProp(x, y, w, "id", "id");
    }
    auto text = [&](const char* label, const char* key) { y = textProp(x, y, w, label, key); };
    auto number = [&](const char* label, const char* key) { y = numberProp(x, y, w, label, key); };
    auto dropdown = [&](const char* label, const char* key, const std::vector<std::string>& options) {
      const std::string current = nd[key].asString();
      y = fieldProp(x, y, w, label, key, current.empty() ? options.front() : current, true);
    };
    static const std::vector<std::string> ops{"==", "!=", "<", "<=", ">", ">="};
    static const std::vector<std::string> modes{"oneshot", "session", "reactivable"};
    auto note = [&](const char* s) { canvas_.addParagraph(x, y, editor::T(s), kFontSize, theme_.textDim, w); y += 30.f; };
    if (type == "trigger") { text("event", "event"); dropdown("mode", "mode", modes); number("group", "group"); }
    else if (type == "start") note("Runs once when the game starts (Play).");
    else if (type == "update") note("Runs every frame while playing.");
    else if (type == "create") { text("object (name)", "object"); note("Runs when an object with this name is created while playing. Empty = any object."); dropdown("mode", "mode", modes); }
    else if (type == "savepoint" || type == "loadpoint") { text("slot", "slot"); note("Saves/loads the narrative variables and flows to Saves/<slot>.save.json"); }
    else if (type == "onvar") { text("variable", "var"); dropdown("op", "op", ops); number("value", "value"); number("hysteresis", "hysteresis"); dropdown("mode", "mode", modes); number("group", "group"); }
    else if (type == "compare") { text("variable", "var"); dropdown("op", "op", ops); number("value", "value"); }
    else if (type == "branch") text("condition (id)", "cond");
    else if (type == "set" || type == "add") { text("variable", "var"); number("value", "value"); }
    else if (type == "emit") text("event", "event");
    else if (type == "action") { text("name", "name"); text("argument", "arg"); }
    else if (type == "delay") number("seconds", "seconds");
    else if (type == "and" || type == "or") {
      std::string joined;
      for (const auto& v : nd["inputs"].items()) joined += (joined.empty() ? "" : ",") + v.asString();
      y = fieldProp(x, y, w, "inputs (ids)", "inputs", joined, false);
    } else if (type == "not") text("input (id)", "input");
    return y;
  }

  float textProp(float x, float y, float w, const char* label, const char* key) {
    return fieldProp(x, y, w, label, key, nodeString(key), false);
  }
  float numberProp(float x, float y, float w, const char* label, const char* key) {
    return fieldProp(x, y, w, label, key, numberText(nodeNumber(key)), false);
  }
  // Campo (ou seletor, que cicla as opcoes ao clicar) da propriedade `key` do no selecionado.
  float fieldProp(float x, float y, float w, const char* label, const char* key, const std::string& value, bool dropdownField) {
    const std::string k = std::string("prop:") + key;
    const bool editing = !dropdownField && focus_ == Focus::Prop && focusKey_ == k;
    canvas_.addText(x, y, editor::T(label), kFontSize, theme_.textDim, w);
    const float fy = y + 16.f;
    fieldRow(x, fy, w, 22.f, "", editing ? buffer_ : value, dropdownField ? Focus::None : Focus::Prop, k);
    if (dropdownField) {
      hits_.back().kind = Hit::Dropdown;
      canvas_.addText(x + w - 14.f, fy + 4.f, "v", kFontSize, theme_.textDim, 12.f);
    }
    return fy + 34.f;
  }

  // Grade de fundo: linhas finas a cada kGridStep, deslocadas pelo pan para acompanhar o grafo.
  void drawGrid() {
    const SU::Vec4 c = theme_.divider;
    const SU::Vec4 line{c.x * 0.45f, c.y * 0.45f, c.z * 0.45f, c.w * 0.45f};
    const float x0 = graphBox_.x, y0 = graphBox_.y, x1 = x0 + graphBox_.w, y1 = y0 + graphBox_.h;
    const float ox = std::fmod(pan_.x, kGridStep), oy = std::fmod(pan_.y, kGridStep);
    for (float x = x0 + (ox < 0.f ? ox + kGridStep : ox); x < x1; x += kGridStep) graphCanvas_.addLine(x, y0, x, y1, 1.f, line, 0.12f);
    for (float y = y0 + (oy < 0.f ? oy + kGridStep : oy); y < y1; y += kGridStep) graphCanvas_.addLine(x0, y, x1, y, 1.f, line, 0.12f);
  }

  // Ponto de conexao de uma porta. O mesmo centro serve para o circulo, a area de clique e o inicio/fim do cabo.
  // "in" fica na borda esquerda; "out", "then" e "else" ficam na borda direita.
  SU::Vec2 portCenter(const sui::Box& b, const std::string& port) const {
    if (port == "in") return {b.x, b.y + kPortY};
    if (port == "then") return {b.x + b.w, b.y + kThenY};
    if (port == "else") return {b.x + b.w, b.y + kElseY};
    return {b.x + b.w, b.y + kPortY};
  }
  void portSquare(SU::Vec2 c, SU::Vec4 color, float z) {
    graphCanvas_.addSolid(c.x - kPort * 0.5f, c.y - kPort * 0.5f, kPort, kPort, color, z, kPort * 0.5f);
  }
  void portHit(SU::Vec2 c, const std::string& id, std::size_t i, const char* port) {
    hits_.push_back({{c.x - kPort, c.y - kPort, kPort * 2.f, kPort * 2.f}, Hit::Port, id, i, port});
  }

  // Grafo: nos, com alca (grip) na barra de titulo, detalhe e portas.
  // Ordem dos alvos: no (corpo: so seleciona), alca (arrasta), portas (por cima de tudo).
  void drawNodes() {
    const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList();
    if (!nodes) return;
    for (std::size_t i = 0; i < nodes->items().size(); ++i) {
      const tool::Json& nd = nodes->items()[i];
      const sui::Box b = nodeBox(nd);
      const std::string type = nd["type"].asString(), id = nd["id"].asString();
      const bool selected = id == selectedId_;
      const float z = 0.2f + 0.002f * static_cast<float>(i % 200);
      graphCanvas_.addSolid(b.x - 1.f, b.y - 1.f, b.w + 2.f, b.h + 2.f, selected ? theme_.accent : theme_.textDim, z, 6.f);
      graphCanvas_.addSolid(b.x, b.y, b.w, b.h, theme_.panel, z + 0.001f, 6.f);
      graphCanvas_.addSolid(b.x, b.y, b.w, kTitleH, theme_.surface, z + 0.001f, 6.f);
      // Alca: seis pontinhos (3 linhas de 2) no inicio da barra de titulo.
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c)
          graphCanvas_.addSolid(b.x + 6.f + static_cast<float>(c) * 4.f, b.y + 6.f + static_cast<float>(r) * 4.f, 2.f, 2.f,
                                theme_.textDim, z + 0.0015f, 0.f);
      graphCanvas_.setTextZ(z + 0.0015f);
      const bool pinned = nd.has("pinned");
      graphCanvas_.addText(b.x + kGripW, b.y + 4.f, pinned ? "Scene: " + nd["label"].asString() : type + " [" + id + "]", kFontSize,
                           theme_.text, kNodeW - kGripW - 6.f);
      graphCanvas_.addText(b.x + 6.f, b.y + kTitleH + 4.f, detailOf(nd), kFontSize, theme_.textDim, kNodeW - 12.f);

      hits_.push_back({b, Hit::Node, id, i, ""});
      hits_.push_back({{b.x, b.y, kGripW, kTitleH}, Hit::Grip, id, i, ""});

      const bool isTrigger = type == "trigger" || type == "onvar" || type == "start" || type == "update" || type == "create";
      const bool isCond = type == "compare" || type == "and" || type == "or" || type == "not";
      if (!isTrigger && !isCond) {
        const SU::Vec2 in = portCenter(b, "in");
        portSquare(in, theme_.textDim, z + 0.002f);
        portHit(in, id, i, "in");
      }
      if (type == "branch") {
        const SU::Vec2 th = portCenter(b, "then"), el = portCenter(b, "else");
        portSquare(th, theme_.accent, z + 0.002f);
        portSquare(el, theme_.error, z + 0.002f);
        portHit(th, id, i, "then");
        portHit(el, id, i, "else");
      } else if (!isCond) {
        const SU::Vec2 out = portCenter(b, "out");
        portSquare(out, theme_.accent, z + 0.002f);
        portHit(out, id, i, "out");
      }
      graphCanvas_.setTextZ(0.9f);
    }
  }

  // Cabo em curva cubica (bezier) de `a` ate `b`, desenhado em trechos retos. Devolve os pontos (para o clique).
  // As tangentes saem para a direita de `a` e chegam pela esquerda de `b`.
  std::vector<SU::Vec2> drawCurve(SU::Vec2 a, SU::Vec2 b, SU::Vec4 color, float z) {
    const float d = std::max(40.f, std::abs(b.x - a.x) * 0.5f);
    const SU::Vec2 c1{a.x + d, a.y}, c2{b.x - d, b.y};
    constexpr int kSteps = 24;
    std::vector<SU::Vec2> pts;
    pts.reserve(kSteps + 1);
    for (int s = 0; s <= kSteps; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(kSteps), mt = 1.f - t;
      const float w0 = mt * mt * mt, w1 = 3.f * mt * mt * t, w2 = 3.f * mt * t * t, w3 = t * t * t;
      pts.push_back(SU::Vec2{w0 * a.x + w1 * c1.x + w2 * c2.x + w3 * b.x, w0 * a.y + w1 * c1.y + w2 * c2.y + w3 * b.y});
    }
    for (std::size_t s = 1; s < pts.size(); ++s)
      graphCanvas_.addLine(pts[s - 1].x, pts[s - 1].y, pts[s].x, pts[s].y, kLinkW, color, z);
    return pts;
  }

  // Ligacoes: cada uma sai do ponto da porta de saida e chega ao ponto de entrada do no destino.
  void drawLinks() {
    curves_.clear();
    const tool::Json* links = const_cast<NarrativeModel&>(model_).linkList();
    const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList();
    if (!links || !nodes) return;
    for (std::size_t l = 0; l < links->items().size(); ++l) {
      const tool::Json& lk = links->items()[l];
      const int a = NarrativeModel::findNode(*nodes, lk["from"].asString());
      const int b = NarrativeModel::findNode(*nodes, lk["to"].asString());
      if (a < 0 || b < 0) continue;
      const std::string port = lk["port"].asString();
      const sui::Box ba = nodeBox(nodes->items()[static_cast<std::size_t>(a)]);
      const sui::Box bb = nodeBox(nodes->items()[static_cast<std::size_t>(b)]);
      const SU::Vec4 color = port == "else" ? theme_.error : port == "then" ? theme_.accent : theme_.textDim;
      curves_.push_back({l, drawCurve(portCenter(ba, port), portCenter(bb, "in"), color, 0.15f)});
    }
  }
  void drawLinkPreview() {
    if (drag_ != Drag::Link) return;
    const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList();
    if (!nodes) return;
    const int a = NarrativeModel::findNode(*nodes, linkFrom_);
    if (a < 0) return;
    const sui::Box ba = nodeBox(nodes->items()[static_cast<std::size_t>(a)]);
    drawCurve(portCenter(ba, linkPort_), cursor_, theme_.accent, 0.15f);
  }

  // Distancia do ponto ate a poligonal do cabo: clique direito perto do cabo o apaga.
  static bool nearPolyline(const std::vector<SU::Vec2>& pts, float x, float y, float tol) {
    for (std::size_t s = 1; s < pts.size(); ++s) {
      const SU::Vec2 a = pts[s - 1], b = pts[s];
      const float dx = b.x - a.x, dy = b.y - a.y;
      const float len2 = dx * dx + dy * dy;
      const float t = len2 > 0.f ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / len2, 0.f, 1.f) : 0.f;
      const float px = a.x + t * dx - x, py = a.y + t * dy - y;
      if (px * px + py * py <= tol * tol) return true;
    }
    return false;
  }

  // Menu de criar no (clique direito no fundo do grafo).
  void drawMenu() {
    const auto& types = NarrativeModel::nodeTypes();
    const float mw = 150.f, rowH = 20.f;
    const float mh = rowH * static_cast<float>(types.size()) + 8.f;
    const sui::Box m{menuAt_.x, menuAt_.y, mw, mh};
    graphCanvas_.addSolid(m.x, m.y, m.w, m.h, theme_.surface, 0.6f, 4.f);
    for (std::size_t i = 0; i < types.size(); ++i) {
      const float ry = m.y + 4.f + rowH * static_cast<float>(i);
      const sui::Box row{m.x + 2.f, ry, mw - 4.f, rowH};
      if (mouseIn(row)) graphCanvas_.addSolid(row.x, row.y, row.w, row.h, theme_.accent, 0.62f, 3.f);
      graphCanvas_.addText(row.x + 6.f, row.y + 4.f, types[i], kFontSize, theme_.text, mw - 12.f);
      hits_.push_back({row, Hit::MenuItem, types[i], i, ""});
    }
    hits_.push_back({m, Hit::Canvas, "menu", 0, ""});   // dentro do menu nao clica no grafo
  }

  // ---- Eventos -----------------------------------------------------------------------------------------
  void pointerDown(float x, float y) {
    if (menu_) {
      for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->kind == Hit::MenuItem && it->box.contains(x, y)) {
          const SU::Vec2 g = graphPoint(menuAt_.x, menuAt_.y);
          const int idx = model_.addNode(it->key, g.x, g.y);
          if (idx >= 0) selectNode(static_cast<std::size_t>(idx));
          closeMenu();
          return;
        }
      }
      closeMenu();
      return;
    }
    const HitItem* hit = topHit(x, y, {Hit::Port, Hit::Button, Hit::VarRemove, Hit::Field, Hit::VarValue, Hit::Dropdown, Hit::Grip, Hit::Node, Hit::Canvas});
    if (!hit) {
      commitFocus();
      return;
    }
    switch (hit->kind) {
      case Hit::Button: {
        commitFocus();
        buttonAction(hit->key);
        break;
      }
      case Hit::VarRemove:
        commitFocus();
        model_.removeVariable(hit->key);
        break;
      case Hit::Field:
        commitFocus();
        beginField(hit->key);
        break;
      case Hit::VarValue: {
        commitFocus();
        focus_ = Focus::VarValue;
        focusKey_ = hit->key.substr(4);
        buffer_ = numberText(variableValue(focusKey_));
        break;
      }
      case Hit::Dropdown:
        commitFocus();
        cycleDropdown(hit->key.substr(5));
        break;
      case Hit::Port:
        commitFocus();
        if (hit->port != "in") {
          drag_ = Drag::Link;
          linkFrom_ = hit->key;
          linkPort_ = hit->port;
          cursor_ = {x, y};
        }
        break;
      case Hit::Grip:   // so a alca arrasta o no
        commitFocus();
        selectNode(hit->index);
        drag_ = Drag::Node;
        dragIdx_ = hit->index;
        grabX_ = x;
        grabY_ = y;
        break;
      case Hit::Node:   // corpo do no: so seleciona
        commitFocus();
        selectNode(hit->index);
        break;
      case Hit::Canvas:
        commitFocus();
        selectedId_.clear();
        drag_ = Drag::Pan;
        grabX_ = x;
        grabY_ = y;
        break;
      default:
        break;
    }
  }

  void pointerMove(float x, float y) {
    if (drag_ == Drag::Node) {
      const float dx = x - grabX_, dy = y - grabY_;
      grabX_ = x;
      grabY_ = y;
      if (const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList(); nodes && dragIdx_ < nodes->items().size()) {
        const tool::Json& nd = nodes->items()[dragIdx_];
        model_.setNodeNumber(dragIdx_, "x", nd["x"].asNumber() + dx);
        model_.setNodeNumber(dragIdx_, "y", nd["y"].asNumber() + dy);
      }
    } else if (drag_ == Drag::Pan) {
      pan_.x += x - grabX_;
      pan_.y += y - grabY_;
      grabX_ = x;
      grabY_ = y;
    } else if (drag_ == Drag::Link) {
      cursor_ = {x, y};
    }
  }

  void pointerUp(float x, float y) {
    if (drag_ == Drag::Link) {
      for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->kind == Hit::Port && it->port == "in" && it->box.contains(x, y)) {
          model_.addLink(linkFrom_, linkPort_, it->key);
          break;
        }
      }
    }
    drag_ = Drag::None;
  }

  void rightClick(float x, float y) {
    commitFocus();
    for (const Curve& c : curves_)
      if (nearPolyline(c.pts, x, y, kLinkHit)) {
        model_.removeLink(c.index);
        return;
      }
    const HitItem* hit = topHit(x, y, {Hit::Node, Hit::Canvas});
    if (!hit) return;
    if (hit->kind == Hit::Node) {
      selectNode(hit->index);
    } else if (hit->kind == Hit::Canvas) {
      menu_ = true;
      menuAt_ = {x, y};
    }
  }

  bool keyDown(SDL_Keycode key) {
    if (focus_ != Focus::None) {
      if (key == SDLK_BACKSPACE) {
        popUtf8(buffer_);
        return true;
      }
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        commitFocus();
        return true;
      }
      if (key == SDLK_ESCAPE) {
        focus_ = Focus::None;
        buffer_.clear();
        return true;
      }
      return true;   // com campo em edicao, a janela consome as teclas (setas etc.)
    }
    if (key == SDLK_ESCAPE && menu_) {
      closeMenu();
      return true;
    }
    if (key == SDLK_DELETE && selectedIndex() >= 0) {
      model_.removeNode(static_cast<std::size_t>(selectedIndex()));
      selectedId_.clear();
      return true;
    }
    return false;
  }

  void buttonAction(const std::string& key) {
    if (key == "new") model_.newGraph();
    else if (key == "save") model_.save();
    else if (key == "compile") model_.compile();
    else if (key == "open") model_.open(model_.path().empty() ? "narrative_graph.json" : model_.path());
    else if (key == "add-var") {
      // O nome digitado ja foi aplicado por commitFocus() no clique.
    } else if (key == "fire") {
      model_.fireTest(testEvent_.empty() ? "event" : testEvent_);
    }
  }

  void beginField(const std::string& key) {
    if (key == "path") { focus_ = Focus::Path; buffer_ = model_.path(); }
    else if (key == "newvar") { focus_ = Focus::NewVar; buffer_.clear(); }
    else if (key == "event") { focus_ = Focus::Event; buffer_ = testEvent_; }
    else if (key.rfind("prop:", 0) == 0) {
      focus_ = Focus::Prop;
      focusKey_ = key;
      buffer_ = nodePropText(key.substr(5));
    }
  }

  // Aplica o texto digitado no campo focado e sai do foco.
  void commitFocus() {
    if (focus_ == Focus::None) return;
    switch (focus_) {
      case Focus::Path:
        if (!buffer_.empty() && model_.open(buffer_)) {}
        break;
      case Focus::NewVar:
        if (model_.addVariable(buffer_)) {}
        break;
      case Focus::VarValue:
        model_.setVariable(focusKey_, std::strtod(buffer_.c_str(), nullptr));
        break;
      case Focus::Event:
        testEvent_ = buffer_;
        break;
      case Focus::Prop:
        commitProp(focusKey_.substr(5), buffer_);
        break;
      case Focus::None:
        break;
    }
    focus_ = Focus::None;
    focusKey_.clear();
    buffer_.clear();
  }

  void commitProp(const std::string& key, const std::string& text) {
    const int idx = selectedIndex();
    if (idx < 0) return;
    const std::size_t i = static_cast<std::size_t>(idx);
    if (key == "id") {
      if (!text.empty() && model_.renameNode(i, text)) selectedId_ = text;
      return;
    }
    if (key == "inputs") {
      std::vector<std::string> items;
      std::string cur;
      for (char c : text) {
        if (c == ',') { if (!cur.empty()) items.push_back(cur); cur.clear(); }
        else cur += c;
      }
      if (!cur.empty()) items.push_back(cur);
      model_.setNodeList(i, key, items);
      return;
    }
    if (isNumericKey(key)) model_.setNodeNumber(i, key, std::strtod(text.c_str(), nullptr));
    else model_.setNodeString(i, key, text);
  }

  void cycleDropdown(const std::string& key) {
    const int idx = selectedIndex();
    if (idx < 0) return;
    static const std::vector<std::string> ops{"==", "!=", "<", "<=", ">", ">="};
    static const std::vector<std::string> modes{"oneshot", "session", "reactivable"};
    const auto& options = key == "op" ? ops : modes;
    const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList();
    const std::string cur = nodes->items()[static_cast<std::size_t>(idx)][key.c_str()].asString();
    std::size_t next = 0;
    for (std::size_t k = 0; k < options.size(); ++k) if (options[k] == cur) next = (k + 1) % options.size();
    model_.setNodeString(static_cast<std::size_t>(idx), key, options[next]);
  }

  // ---- Auxiliares --------------------------------------------------------------------------------------
  void selectNode(std::size_t index) {
    if (const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList(); nodes && index < nodes->items().size())
      selectedId_ = nodes->items()[index]["id"].asString();
  }
  void closeMenu() { menu_ = false; }

  int selectedIndex() const {
    const tool::Json* nodes = const_cast<NarrativeModel&>(model_).nodeList();
    if (!nodes || selectedId_.empty()) return -1;
    return NarrativeModel::findNode(*nodes, selectedId_);
  }

  sui::Box nodeBox(const tool::Json& nd) const {
    const float nx = nd["x"].asNumber(40.0), ny = nd["y"].asNumber(40.0);
    return {graphBox_.x + pan_.x + static_cast<float>(nx), graphBox_.y + pan_.y + static_cast<float>(ny), kNodeW, kNodeH};
  }
  SU::Vec2 graphPoint(float x, float y) const { return {x - graphBox_.x - pan_.x, y - graphBox_.y - pan_.y}; }

  // Alvo de cima que contem o ponto, entre os tipos pedidos (ordem de prioridade: o primeiro tipo da lista vence).
  const HitItem* topHit(float x, float y, std::initializer_list<Hit> kinds) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
      if (!it->box.contains(x, y)) continue;
      for (Hit k : kinds)
        if (it->kind == k) return &*it;
    }
    return nullptr;
  }
  bool mouseIn(const sui::Box& b) const { return b.contains(lastX_, lastY_); }

  double variableValue(const std::string& name) const {
    const tool::Json* vars = const_cast<NarrativeModel&>(model_).varList();
    if (!vars || !vars->has(name)) return 0.0;
    return vars->members().at(name)["value"].asNumber();
  }
  std::string nodeString(const char* key) const {
    const int idx = selectedIndex();
    if (idx < 0) return "";
    return const_cast<NarrativeModel&>(model_).nodeList()->items()[static_cast<std::size_t>(idx)][key].asString();
  }
  double nodeNumber(const char* key) const {
    const int idx = selectedIndex();
    if (idx < 0) return 0.0;
    return const_cast<NarrativeModel&>(model_).nodeList()->items()[static_cast<std::size_t>(idx)][key].asNumber();
  }
  std::string nodePropText(const std::string& key) const {
    return isNumericKey(key) ? numberText(nodeNumber(key.c_str())) : nodeString(key.c_str());
  }
  static bool isNumericKey(const std::string& key) {
    return key == "value" || key == "hysteresis" || key == "group" || key == "seconds" || key == "x" || key == "y";
  }
  static std::string numberText(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
  }
  static std::string detailOf(const tool::Json& nd) {
    const std::string joined = nd["var"].asString() + nd["event"].asString() + nd["name"].asString() + nd["object"].asString() + nd["slot"].asString();
    return joined.size() > 26 ? joined.substr(0, 25) + "~" : joined;
  }
  static void popUtf8(std::string& s) {
    if (s.empty()) return;
    std::size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.erase(i);
  }

  std::vector<SR::ClipRectGPU> clipsFor(const sui::Box& body) const {
    return {{body.x, body.y, body.x + body.w, body.y + body.h},
            {graphBox_.x + body.x, graphBox_.y + body.y, graphBox_.x + body.x + graphBox_.w, graphBox_.y + body.y + graphBox_.h}};
  }

  framed::Chrome chrome_;
  sui::Canvas canvas_;        // painel (esquerda, direita, barra): recorte do corpo
  sui::Canvas graphCanvas_;   // grafo: recorte da area do grafo
  sui::Theme theme_{};
  sui::SpriteUiSdlRenderer& sdl_;
  NarrativeModel& model_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  sui::Box body_{};
  sui::Box graphBox_{};
  SU::Vec2 pan_{0.f, 0.f};
  std::vector<HitItem> hits_;
  struct Curve {
    std::size_t index = 0;
    std::vector<SU::Vec2> pts;
  };
  std::vector<Curve> curves_;   // cabos desenhados no quadro (para o clique direito)

  enum class Drag : std::uint8_t { None, Node, Link, Pan };
  Drag drag_ = Drag::None;
  pointer_capture::PointerCapture capture_;
  std::size_t dragIdx_ = 0;
  std::string linkFrom_, linkPort_;
  SU::Vec2 cursor_{0.f, 0.f}, menuAt_{0.f, 0.f};
  float grabX_ = 0.f, grabY_ = 0.f, lastX_ = -1.f, lastY_ = -1.f;
  bool menu_ = false;
  bool textInputOn_ = false;
  bool keyboardOwned_ = false;   // o ultimo clique foi nesta janela: teclado e digitacao vem para ela

  Focus focus_ = Focus::None;
  std::string focusKey_, buffer_, selectedId_, testEvent_ = "event";
};

} // namespace editor
