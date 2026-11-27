#pragma once

// Janela "Input Map" em SpriteUI, na moldura padrao do editor (framed::Chrome: titulo, "?", X, grip e dock).
// Dados e regras: input_map.hpp (o mapa salvo em Project/input_map.json). Aqui: desenho, cliques, digitacao e a
// captura "Listen" (proxima tecla, botao ou movimento do analogico vira a vinculacao).
//
// Regras de uso (decisoes de design):
//  - Combos de tecla e de botao viram "Listen": clique no campo e pressione a tecla/botao. Esc cancela; Backspace/Delete limpa.
//  - Combo de botao do gamepad: "Listen" + "x" (limpa). Eixo do gamepad: clique no nome cicla os eixos.
//  - Nomes de mapa e de acao sao editados com clique + digitacao; Enter ou clique fora termina.
//  - A janela rola com a roda do mouse.

#include "editor_state.hpp"
#include "framed_window.hpp"
#include "i18n.hpp"
#include "input_map.hpp"
#include "sui_panel.hpp"
#include "theme_colors.hpp"
#include "ui_scrollbar.hpp"
#include "usability_hints.hpp"   // confirmacao em dois cliques (Reset e Delete map)
#include "spriteui_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace editor::inputui {

namespace SU = ToolEngine::SpriteUi;
namespace SR = ToolEngine::SpriteUi::Rendering;

inline constexpr float kWidth = 980.f;
inline constexpr float kHeight = 620.f;
inline constexpr float kPad = 14.f;
inline constexpr float kFont = 13.f;
inline constexpr float kSmall = 12.f;
inline constexpr float kRowH = 26.f;
inline constexpr float kGap = 6.f;
inline constexpr float kScrollStep = 40.f;

// Colunas da tabela (largura fixa, em pixels logicos).
inline constexpr float kColAction = 150.f, kColType = 60.f, kColKey = 250.f, kColPad = 300.f, kColNow = 60.f, kColDel = 30.f;

// Teclas que nao viram vinculo quando capturadas: Esc cancela, Backspace/Delete limpam.
inline constexpr int kScanEscape = 41, kScanBackspace = 42, kScanDelete = 76;

// Da paleta em uso (theme_colors.hpp), refeito a cada quadro em draw(). ok (verde de "ligado") fica fixo.
struct Colors {
  SU::Vec4 field, fieldFocus, listen, button, danger, row, rowAlt, idle, accent;
  SU::Vec4 ok = sui::Srgb(0.30f, 1.00f, 0.40f);
  Colors() {
    const editor::theme_colors::Colors t = editor::theme_colors::Active();
    field = sui::PaletteColor(t.field);
    fieldFocus = sui::PaletteColor(t.fieldFocus);
    listen = sui::PaletteColor(t.btnOn);
    button = sui::PaletteColor(t.btnOn);
    danger = sui::PaletteColor(palette::mixRgb(t.surface, t.error, 0.6f));
    row = sui::PaletteColor(t.surface);
    rowAlt = sui::PaletteColor(t.field);
    idle = sui::PaletteColor(t.textDim);
    accent = sui::PaletteColor(t.heading);
  }
};

// Alvo da captura "Listen" (qual campo da linha recebe a proxima tecla, botao ou analogico).
enum class Target : std::uint8_t { None, Key, KeyNeg, PadButton, Stick };

// Campo com digitacao de texto.
enum class Text : std::uint8_t { None, SchemeName, RowAction };

// Elemento clicavel do quadro (coordenadas do corpo, sem rolagem).
enum class Hit : std::uint8_t {
  AddController, ResetDefaults, SchemeName, SchemeController, SchemeDelete, RowAction, RowKey, RowKeyNeg,
  RowPadButton, RowPadButtonClear, RowPadAxis, RowInvert, RowStick, RowDelete, AddButton, AddAxis
};

// Area clicavel desenhada no corpo (coordenadas do corpo, sem rolagem) e o que ela faz; scheme/row dizem qual controle e qual linha.
struct HitBox {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  Hit kind = Hit::AddController;
  int scheme = -1, row = -1;
};

// Janela Input Map: edita os esquemas de controle (teclado/gamepad) e as linhas acao -> tecla/botao/eixo. Captura a proxima tecla ou botao quando uma celula esta em modo "listen".
class Panel {
 public:
  Panel(const sui::TextAtlasSet& text, sui::SpriteUiSdlRenderer& sdl) : sdl_{sdl}, body_{text}, chrome_{text, sdl} {}
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // Moldura padrao (titulo, "?", X, grip, dock). O host registra no dock e le consumeClose()/consumeHelp().
  framed::Chrome& chrome() { return chrome_; }

  // Eventos do SDL (ver main.cpp). Devolve true quando a janela usou o evento.
  bool handleEvent(const SDL_Event& e, Editor& ed) {
    if (!ed.showInputMap || !chrome_.visible()) return false;
    if (chrome_.handleEvent(e)) {   // X, "?" e minimizar da moldura
      focus_ = {};
      listen_ = {};
      return true;
    }
    switch (e.type) {
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT || !contains(e.button.x, e.button.y)) return false;
        if (bar_.pointerDown(e.button.x, e.button.y, scroll_)) {   // barra de rolagem unica: arrasta ou pagina
          scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, viewH_);
          return true;
        }
        press(e.button.x, e.button.y, ed);
        return true;
      case SDL_EVENT_MOUSE_MOTION:
        if (bar_.pointerMove(e.motion.x, e.motion.y, scroll_)) scroll_ = ui_scrollbar::clampScroll(scroll_, contentH_, viewH_);
        return bar_.dragging();   // o arraste segue com o mouse fora da janela
      case SDL_EVENT_MOUSE_BUTTON_UP:
        return e.button.button == SDL_BUTTON_LEFT && bar_.pointerUp();
      case SDL_EVENT_MOUSE_WHEEL:
        if (!contains(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
        scroll_ = ui_scrollbar::clampScroll(scroll_ - e.wheel.y * kScrollStep, contentH_, viewH_);
        return true;
      case SDL_EVENT_TEXT_INPUT:
        if (focus_.kind == Text::None || !e.text.text) return false;
        texts_.emplace_back(e.text.text);
        return true;
      case SDL_EVENT_KEY_DOWN:
        return keyDown(e.key.scancode, ed);
      default:
        return false;
    }
  }

  // Desenha a moldura e o corpo quando aberta e visivel (`shown`: a aba ativa do dock, ou flutuante). Retorna false so em falha do SDL.
  bool draw(SDL_Renderer* renderer, SDL_Window* window, Editor& ed, bool shown) {
    if (!ed.showInputMap || !shown) {
      chrome_.hide();
      if (textOn_) stopText(window);
      return true;
    }
    if (!renderer || !window) return false;
    theme_ = sui::Theme{};   // paleta em uso neste quadro (trocar o tema repinta a janela)
    colors_ = Colors{};
    int ww = 0, wh = 0;
    if (!SDL_GetWindowSize(window, &ww, &wh)) return false;
    if (ww <= 0 || wh <= 0) return true;   // janela minimizada
    syncTextInput(window);
    applyTyping(ed);

    // Leitura ao vivo (indicadores ON/valor) e captura "Listen".
    if (!ed.playing) {
      ed.sampler.sample(ed.rawInput, focus_.kind == Text::None);
      ed.input.update(ed.rawInput);
    }
    capture(ed);

    const sui::Box body = chrome_.prepare(static_cast<float>(ww), static_cast<float>(wh), kWidth, kHeight);
    if (!chrome_.draw(renderer, T("Input Map"))) return false;
    if (body.w <= 0.f || body.h <= 0.f) {   // minimizada: so o cabecalho
      panel_ = {};
      return true;
    }
    panel_ = body;
    bodyW_ = panel_.w - 2.f * kPad;
    viewH_ = panel_.h;

    rebuild(ed);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH_ - viewH_));
    if (dirty_ && focus_.kind == Text::None && listen_.kind == Target::None) {   // grava quando a edicao termina
      ed.applyInputMap();
      ed.inputMap.save(ed.project);
      dirty_ = false;
    }
    return drawPacket(renderer);
  }

 private:
  // Campo de texto em edicao (qual tipo, esquema e linha).
  struct Focus {
    Text kind = Text::None;
    int scheme = -1, row = -1;
  };
  // Celula esperando a proxima tecla/botao/eixo para virar vinculo.
  struct Listen {
    Target kind = Target::None;
    int scheme = -1, row = -1;
  };

  [[nodiscard]] bool contains(float x, float y) const {
    return x >= panel_.x && y >= panel_.y && x < panel_.x + panel_.w && y < panel_.y + panel_.h;
  }
  [[nodiscard]] static bool inside(float x, float y, float left, float top, float w, float h) {
    return x >= left && y >= top && x < left + w && y < top + h;
  }

  // ---------- Eventos ----------
  void press(float x, float y, Editor& ed) {
    const float lx = x - panel_.x, ly = y - panel_.y;
    listen_ = {};   // qualquer clique cancela a captura
    const float bx = lx, by = ly + scroll_;
    const HitBox* hit = nullptr;
    for (const HitBox& h : hits_) if (inside(bx, by, h.x, h.y, h.w, h.h)) hit = &h;   // o ultimo desenhado vence
    if (!hit) {
      focus_ = {};   // clique fora de campo termina a digitacao
      arm_.Cancel();   // e desarma a confirmacao pendente
      return;
    }
    dispatch(*hit, ed);
  }

  // Executa a acao do HitBox clicado: adicionar/apagar esquema ou linha, ciclar controle e eixo, ou entrar em foco/captura.
  void dispatch(const HitBox& h, Editor& ed) {
    auto& schemes = ed.inputMap.schemes;
    const int si = h.scheme, ri = h.row;
    const bool validS = si >= 0 && static_cast<size_t>(si) < schemes.size();
    const bool validR = validS && ri >= 0 && static_cast<size_t>(ri) < schemes[static_cast<size_t>(si)].rows.size();
    // Reset e Delete map apagam configuracao sem desfazer: o primeiro clique so "arma" o botao (o texto pede confirmacao),
    // o segundo, no mesmo botao, executa. Qualquer outro clique desarma.
    if (h.kind != Hit::ResetDefaults && h.kind != Hit::SchemeDelete) arm_.Cancel();
    switch (h.kind) {
      case Hit::AddController:
        if (schemes.size() < kMaxSchemes) {
          Scheme s = schemes.empty() ? InputMap::defaults().schemes[0] : schemes[0];
          std::snprintf(s.name, sizeof s.name, "Player %zu", schemes.size() + 1);
          s.pad = std::min(3, static_cast<int>(schemes.size()));
          schemes.push_back(s);
          dirty_ = true;
        }
        break;
      case Hit::ResetDefaults:
        if (!arm_.Click(kArmReset)) break;
        ed.inputMap = InputMap::defaults();
        focus_ = {};
        dirty_ = true;
        break;
      case Hit::SchemeName:
        if (validS) focus_ = {Text::SchemeName, si, -1};
        break;
      case Hit::SchemeController:
        if (validS) {   // ciclo: Teclado -> Gamepad 1 -> ... -> Gamepad 4 -> Teclado
          Scheme& s = schemes[static_cast<size_t>(si)];
          s.pad = (s.pad + 2) % 5 - 1;
          dirty_ = true;
        }
        break;
      case Hit::SchemeDelete:
        if (validS && schemes.size() > 1) {
          if (!arm_.Click(kArmDeleteBase + si)) break;
          schemes.erase(schemes.begin() + si);
          focus_ = {};
          dirty_ = true;
        }
        break;
      case Hit::RowAction:
        if (validR) focus_ = {Text::RowAction, si, ri};
        break;
      case Hit::RowKey:
        if (validR) listen_ = {Target::Key, si, ri};
        break;
      case Hit::RowKeyNeg:
        if (validR) listen_ = {Target::KeyNeg, si, ri};
        break;
      case Hit::RowPadButton:
        if (validR) listen_ = {Target::PadButton, si, ri};
        break;
      case Hit::RowPadButtonClear:
        if (validR) { schemes[static_cast<size_t>(si)].rows[static_cast<size_t>(ri)].padButton = -1; dirty_ = true; }
        break;
      case Hit::RowPadAxis:
        if (validR) {
          MapRow& r = schemes[static_cast<size_t>(si)].rows[static_cast<size_t>(ri)];
          r.padAxis = r.padAxis >= 5 ? -1 : r.padAxis + 1;   // ciclo: (nenhum), eixo 0 .. 5
          dirty_ = true;
        }
        break;
      case Hit::RowInvert:
        if (validR) { MapRow& r = schemes[static_cast<size_t>(si)].rows[static_cast<size_t>(ri)]; r.padInvert = !r.padInvert; dirty_ = true; }
        break;
      case Hit::RowStick:
        if (validR) listen_ = {Target::Stick, si, ri};
        break;
      case Hit::RowDelete:
        if (validS && validR) {
          auto& rows = schemes[static_cast<size_t>(si)].rows;
          rows.erase(rows.begin() + ri);
          focus_ = {};
          dirty_ = true;
        }
        break;
      case Hit::AddButton:
      case Hit::AddAxis:
        if (validS && schemes[static_cast<size_t>(si)].rows.size() < kMaxRows) {
          const bool axis = h.kind == Hit::AddAxis;
          auto& rows = schemes[static_cast<size_t>(si)].rows;
          MapRow r;
          std::snprintf(r.action, sizeof r.action, axis ? "axis%zu" : "button%zu", rows.size() + 1);
          r.kind = axis ? 1 : 0;
          rows.push_back(r);
          dirty_ = true;
        }
        break;
    }
  }

  // Tecla com a janela aberta: em captura, Esc cancela e Backspace/Delete limpam o vinculo; em texto, edita/encerra o campo.
  bool keyDown(SDL_Scancode sc, Editor& ed) {
    const int code = static_cast<int>(sc);
    if (listen_.kind != Target::None) {
      if (code == kScanEscape) { listen_ = {}; return true; }
      if (code == kScanBackspace || code == kScanDelete) { setListened(ed, 0); return true; }
      return true;   // enquanto captura, a tecla vai para o vinculo (capture) e nao para os atalhos
    }
    if (focus_.kind == Text::None) return false;
    if (code == kScanBackspace) { ++backspaces_; return true; }
    if (code == kScanEscape || code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) { focus_ = {}; return true; }
    return true;
  }

  // Digitacao pendente (texto e apagar) aplicada no campo focado.
  void applyTyping(Editor& ed) {
    if (focus_.kind == Text::None) { texts_.clear(); backspaces_ = 0; return; }
    char* buf = nullptr;
    size_t cap = 0;
    if (!fieldBuffer(ed, buf, cap)) { texts_.clear(); backspaces_ = 0; focus_ = {}; return; }
    for (const std::string& t : texts_) {
      const size_t len = std::strlen(buf);
      if (len + t.size() < cap) { std::memcpy(buf + len, t.data(), t.size()); buf[len + t.size()] = '\0'; dirty_ = true; }
    }
    for (int i = 0; i < backspaces_; ++i) {   // apaga um caractere UTF-8 (sem os bytes de continuacao)
      size_t len = std::strlen(buf);
      if (len == 0) break;
      do { --len; } while (len > 0 && (static_cast<unsigned char>(buf[len]) & 0xC0) == 0x80);
      buf[len] = '\0';
      dirty_ = true;
    }
    texts_.clear();
    backspaces_ = 0;
  }

  // Buffer do campo de texto focado (nome do mapa ou da acao).
  bool fieldBuffer(Editor& ed, char*& buf, size_t& cap) {
    auto& schemes = ed.inputMap.schemes;
    if (focus_.scheme < 0 || static_cast<size_t>(focus_.scheme) >= schemes.size()) return false;
    Scheme& s = schemes[static_cast<size_t>(focus_.scheme)];
    if (focus_.kind == Text::SchemeName) { buf = s.name; cap = sizeof s.name; return true; }
    if (focus_.row < 0 || static_cast<size_t>(focus_.row) >= s.rows.size()) return false;
    MapRow& r = s.rows[static_cast<size_t>(focus_.row)];
    buf = r.action;
    cap = sizeof r.action;
    return true;
  }

  // Grava o valor capturado (ou limpa) no alvo do Listen.
  void setListened(Editor& ed, int value) {
    auto& schemes = ed.inputMap.schemes;
    if (listen_.kind == Target::None || static_cast<size_t>(listen_.scheme) >= schemes.size()) { listen_ = {}; return; }
    auto& rows = schemes[static_cast<size_t>(listen_.scheme)].rows;
    if (static_cast<size_t>(listen_.row) >= rows.size()) { listen_ = {}; return; }
    MapRow& r = rows[static_cast<size_t>(listen_.row)];
    switch (listen_.kind) {
      case Target::Key: r.key = value; break;
      case Target::KeyNeg: r.keyNeg = value; break;
      case Target::PadButton: r.padButton = value; break;
      case Target::Stick: r.padAxis = value; break;
      case Target::None: break;
    }
    listen_ = {};
    dirty_ = true;
  }

  // Captura do Listen: tecla nova (edge), botao do gamepad ou analogico acima de 0.7.
  void capture(Editor& ed) {
    tool::RawInput& raw = ed.rawInput;
    int newKey = -1, newBtn = -1, newAxis = -1;
    for (int k = 4; k < static_cast<int>(tool::kMaxKeys); ++k) {
      if (raw.keys[k] && !prevKeys_[k] && newKey < 0 && k != kScanEscape && k != kScanBackspace && k != kScanDelete) newKey = k;
      prevKeys_[k] = raw.keys[k];
    }
    for (size_t p = 0; p < tool::kMaxPads; ++p) {
      for (int b = 0; b < 15; ++b) {
        if (raw.pads[p].buttons[b] && !prevBtn_[p][b] && newBtn < 0) newBtn = b;
        prevBtn_[p][b] = raw.pads[p].buttons[b];
      }
      for (int a = 0; a < 6; ++a) if (std::fabs(raw.pads[p].axes[a]) > 0.7f && newAxis < 0) newAxis = a;
    }
    if (listen_.kind == Target::None) return;
    if (listen_.kind == Target::Key || listen_.kind == Target::KeyNeg) { if (newKey >= 0) setListened(ed, newKey); }
    else if (listen_.kind == Target::PadButton) { if (newBtn >= 0) setListened(ed, newBtn); }
    else if (listen_.kind == Target::Stick) { if (newAxis >= 0) setListened(ed, newAxis); }
  }

  // ---------- Texto e foco de teclado ----------
  void syncTextInput(SDL_Window* win) {
    const bool want = focus_.kind != Text::None;
    if (want == textOn_) return;
    if (want) SDL_StartTextInput(win);
    else SDL_StopTextInput(win);
    textOn_ = want;
  }
  // Encerra a edicao de texto e desliga a entrada de texto do SDL.
  void stopText(SDL_Window* win) {
    focus_ = {};
    syncTextInput(win);
  }

  // ---------- Conteudo ----------
  // Monta o conteudo do corpo (coordenadas do corpo, sem rolagem) e a lista de cliques.
  void rebuild(Editor& ed) {
    body_.clear();
    hits_.clear();
    const float x0 = kPad;
    float y = 0.f;

    const float lh = body_.lineHeight(kFont);
    y += kPad;
    body_.addText(x0, y, F("Gamepads detected: %d", ed.sampler.connected()), kFont, theme_.text, bodyW_);
    float gx = x0 + 230.f;
    for (int i = 0; i < ed.sampler.connected(); ++i) {
      const std::string label = "[" + std::to_string(i + 1) + "] " + ed.sampler.padName(static_cast<size_t>(i));
      const float w = body_.addText(gx, y, label, kSmall, theme_.textDim, bodyW_ - (gx - x0));
      gx += w + 16.f;
    }
    y += lh + kGap;

    toolButton(x0, y, 200.f, T("+ Add controller map"), Hit::AddController, -1, -1, colors_.button);
    const bool resetArmed = arm_.Armed(kArmReset);
    toolButton(x0 + 210.f, y, 150.f, resetArmed ? T("Confirm reset?") : T("Reset to defaults"), Hit::ResetDefaults, -1, -1,
               resetArmed ? colors_.danger : colors_.button);
    body_.addText(x0 + 380.f, y + (kRowH - lh) * 0.5f, T("Saved in Project/input_map.json"), kSmall, theme_.textDim, bodyW_ - 380.f);
    y += kRowH + kPad;

    auto& schemes = ed.inputMap.schemes;
    for (size_t si = 0; si < schemes.size(); ++si) {
      Scheme& s = schemes[si];
      const int S = static_cast<int>(si);
      // Cabecalho do mapa: nome (editavel), controle, apagar mapa.
      body_.addText(x0, y + (kRowH - lh) * 0.5f, T("Name"), kSmall, theme_.textDim, 60.f);
      fieldBox(x0 + 50.f, y, 180.f, s.name, focus_.kind == Text::SchemeName && focus_.scheme == S, Hit::SchemeName, S, -1);
      body_.addText(x0 + 250.f, y + (kRowH - lh) * 0.5f, T("Controller"), kSmall, theme_.textDim, 90.f);
      const std::string pad = s.pad < 0 ? std::string(T("Keyboard only")) : "Gamepad " + std::to_string(s.pad + 1);
      toolButton(x0 + 330.f, y, 180.f, pad, Hit::SchemeController, S, -1, colors_.field);
      if (schemes.size() > 1) toolButton(x0 + 520.f, y, 110.f, arm_.Armed(kArmDeleteBase + S) ? T("Confirm delete?") : T("Delete map"), Hit::SchemeDelete, S, -1, colors_.danger);
      y += kRowH + kGap;

      // Cabecalhos da tabela.
      float cx = x0;
      const float head[6] = {kColAction, kColType, kColKey, kColPad, kColNow, kColDel};
      const std::string names[6] = {T("Action"), T("Type"), T("Keyboard"), T("Gamepad"), T("Now"), ""};
      for (int c = 0; c < 6; ++c) { body_.addText(cx + 4.f, y, names[c], kSmall, theme_.textDim, head[c] - 8.f); cx += head[c]; }
      y += lh + 4.f;
      body_.addSolid(x0, y, bodyW_, 1.f, theme_.divider, 0.2f);
      y += 4.f;

      // Linhas.
      for (size_t ri = 0; ri < s.rows.size(); ++ri) {
        MapRow& r = s.rows[ri];
        const int R = static_cast<int>(ri);
        body_.addSolid(x0, y, bodyW_, kRowH, ri % 2 ? colors_.rowAlt : colors_.row, 0.05f);
        cx = x0;
        fieldBox(cx + 2.f, y + 1.f, kColAction - 6.f, r.action, focus_.kind == Text::RowAction && focus_.scheme == S && focus_.row == R, Hit::RowAction, S, R);
        cx += kColAction;
        body_.addText(cx + 4.f, y + (kRowH - lh) * 0.5f, r.kind == 0 ? T("Button") : T("Axis"), kSmall, theme_.text, kColType - 8.f);
        cx += kColType;

        // Teclado: botao = uma tecla; eixo = tecla negativa e positiva.
        if (r.kind == 0) {
          keyBox(cx + 2.f, y + 1.f, kColKey - 6.f, r.key, Target::Key, S, R, Hit::RowKey);
        } else {
          keyBox(cx + 2.f, y + 1.f, 118.f, r.keyNeg, Target::KeyNeg, S, R, Hit::RowKeyNeg, "- ");
          keyBox(cx + 124.f, y + 1.f, 118.f, r.key, Target::Key, S, R, Hit::RowKey, "+ ");
        }
        cx += kColKey;

        // Gamepad: botao (Listen + limpar) ou eixo (ciclo do eixo, inverter, Listen do analogico).
        if (r.kind == 0) {
          const bool listening = listen_.kind == Target::PadButton && listen_scheme(S) && listen_row(R);
          const std::string cur = listening ? std::string(T("Press a button...")) : (r.padButton >= 0 ? kPadButtonNames[static_cast<size_t>(r.padButton)] : std::string(T("(none)")));
          toolButton(cx + 2.f, y + 1.f, 230.f, cur, Hit::RowPadButton, S, R, listening ? colors_.listen : colors_.field);
          toolButton(cx + 236.f, y + 1.f, 26.f, "x", Hit::RowPadButtonClear, S, R, colors_.field);
        } else {
          const std::string cur = r.padAxis >= 0 ? kPadAxisNames[static_cast<size_t>(r.padAxis)] : std::string(T("(none)"));
          toolButton(cx + 2.f, y + 1.f, 150.f, cur, Hit::RowPadAxis, S, R, colors_.field);
          toolButton(cx + 156.f, y + 1.f, 66.f, T("Invert"), Hit::RowInvert, S, R, r.padInvert ? colors_.listen : colors_.field);
          const bool listening = listen_.kind == Target::Stick && listen_scheme(S) && listen_row(R);
          toolButton(cx + 226.f, y + 1.f, 66.f, listening ? T("Move stick...") : T("Listen"), Hit::RowStick, S, R, listening ? colors_.listen : colors_.field);
        }
        cx += kColPad;

        // Valor ao vivo (ON ou o valor do eixo).
        if (r.kind == 0) {
          const bool d = ed.inputMap.down(ed.input, si, r.action);
          body_.addText(cx + 4.f, y + (kRowH - lh) * 0.5f, d ? "ON" : "-", kSmall, d ? colors_.ok : colors_.idle, kColNow - 8.f);
        } else {
          char buf[24];
          std::snprintf(buf, sizeof buf, "%+.2f", static_cast<double>(ed.inputMap.axis(ed.input, si, r.action)));
          body_.addText(cx + 4.f, y + (kRowH - lh) * 0.5f, buf, kSmall, theme_.text, kColNow - 8.f);
        }
        cx += kColNow;
        toolButton(cx + 2.f, y + 1.f, kColDel - 4.f, "x", Hit::RowDelete, S, R, colors_.field);
        y += kRowH + 2.f;
      }
      // Adicionar linhas.
      if (s.rows.size() < kMaxRows) {
        toolButton(x0, y + 2.f, 120.f, T("+ Add button"), Hit::AddButton, S, -1, colors_.field);
        toolButton(x0 + 130.f, y + 2.f, 120.f, T("+ Add axis"), Hit::AddAxis, S, -1, colors_.field);
        y += kRowH + 4.f;
      }
      y += kPad;
    }
    contentH_ = y + kPad;
  }

  bool listen_scheme(int s) const { return listen_.kind != Target::None && listen_.scheme == s; }
  bool listen_row(int r) const { return listen_.kind != Target::None && listen_.row == r; }

  // Campo de texto (nome): fundo, destaque quando focado e texto. Clique registra o cique.
  void fieldBox(float x, float y, float w, const char* value, bool focused, Hit kind, int s, int r) {
    body_.addSolid(x, y, w, kRowH - 2.f, focused ? colors_.fieldFocus : colors_.field, 0.2f);
    const float lh = body_.lineHeight(kFont);
    std::string shown = value;
    if (focused) shown += "|";
    body_.addText(x + 6.f, y + (kRowH - 2.f - lh) * 0.5f, shown, kFont, theme_.text, w - 12.f);
    hits_.push_back({x, y, w, kRowH - 2.f, kind, s, r});
  }

  // Caixa de tecla: mostra o nome da tecla, "Press a key..." quando captura.
  void keyBox(float x, float y, float w, int code, Target target, int s, int r, Hit kind, const char* prefix = "") {
    const bool listening = listen_.kind == target && listen_scheme(s) && listen_row(r);
    std::string cur = listening ? std::string(T("Press a key...")) : (code > 0 ? std::string(SDL_GetScancodeName(static_cast<SDL_Scancode>(code))) : std::string(T("(none)")));
    toolButton(x, y, w, std::string(prefix) + cur, kind, s, r, listening ? colors_.listen : colors_.field);
  }

  // Botao com texto centralizado. `fill` escolhe a cor (campo, destaque ou perigo).
  void toolButton(float x, float y, float w, const std::string& label, Hit kind, int s, int r, SU::Vec4 fill) {
    body_.addSolid(x, y, w, kRowH - 2.f, fill, 0.2f);
    const float lh = body_.lineHeight(kSmall);
    const auto lay = body_.layout(label, kSmall);
    body_.addText(x + std::max(4.f, (w - lay.size.x) * 0.5f), y + (kRowH - 2.f - lh) * 0.5f, label, kSmall, theme_.text, w - 8.f);
    hits_.push_back({x, y, w, kRowH - 2.f, kind, s, r});
  }

  // Monta o pacote (corpo com rolagem e barra) e desenha no SDL. A moldura ja foi desenhada por chrome_.
  bool drawPacket(SDL_Renderer* renderer) {
    std::vector<SR::UIRenderItem> items;
    const std::vector<SR::ClipRectGPU> clips{{panel_.x, panel_.y, panel_.x + panel_.w, panel_.y + panel_.h}};
    const SU::UIHandle owner = sui::handleFor(0);
    std::uint64_t paint = 1;
    body_.appendItems(items, panel_.x, panel_.y, scroll_, panel_.y, panel_.y + panel_.h, owner, 0u, paint);
    // Barra de rolagem unica do editor (ui_scrollbar.hpp).
    bar_.layout({panel_.x + panel_.w - ui_scrollbar::kWidth - 2.f, panel_.y, ui_scrollbar::kWidth, viewH_}, contentH_, viewH_);
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
  sui::Canvas body_;
  framed::Chrome chrome_;
  SR::RenderExtractor extractor_;
  SR::FramePacket packet_;
  std::uint64_t frameId_ = 0;

  sui::Box panel_;
  float bodyW_ = 0.f, viewH_ = 0.f, contentH_ = 0.f;
  float scroll_ = 0.f;
  ui_scrollbar::Scrollbar bar_;
  std::vector<HitBox> hits_;

  Focus focus_;
  Listen listen_;
  bool textOn_ = false;
  std::vector<std::string> texts_;
  int backspaces_ = 0;
  bool dirty_ = false;
  // Confirmacao em dois cliques: chaves do botao armado (Reset = 1; Delete map = 100 + indice do mapa).
  static constexpr int kArmReset = 1, kArmDeleteBase = 100;
  editor::usability::ConfirmArm arm_;

  bool prevKeys_[tool::kMaxKeys] = {};
  bool prevBtn_[tool::kMaxPads][16] = {};
};

}  // namespace editor::inputui
