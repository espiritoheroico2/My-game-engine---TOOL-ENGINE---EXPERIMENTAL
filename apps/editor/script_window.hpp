// script_window.hpp - Janela de edicao de script. "Edit script" no Inspector abre esta janela (um painel proprio,
// que nao fica preso ao Inspector). Tem: editor multilinha, autocomplete da API (Ctrl+Espaco ou ao digitar 2+ letras,
// clique para aceitar), menu de snippets (os exemplos do Help) e o verificador de scripts (pendencias e erros com linha).
#pragma once

#include "editor_state.hpp"
#include "script.hpp"
#include "script_check.hpp"
#include "script_complete.hpp"

#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace editor {

struct ScriptWindow {
  bool open = false;
  tool::Entity target{};
  std::string name, path, text, msg;
  std::vector<ScriptIssue> issues;
  bool dirty = false, checkPending = false, showAll = false, refocus = false;
  double dirtyAt = -10.0;
  // Texto e cursor sao aplicados pelo callback do campo (so ele pode mexer no buffer do ImGui).
  bool hasPending = false;
  std::string pendingText;
  size_t pendingCursor = 0, cursor = 0;
};

inline ScriptWindow& scriptWindow() { static ScriptWindow w; return w; }

// Abre a janela para o objeto selecionado (cria o arquivo do script se ainda nao existe).
inline void openScriptWindow(Editor& ed) {
  auto& W = scriptWindow();
  auto& w = ed.scene.world();
  auto* sc = w.get<ScriptComp>(ed.selected);
  if (!sc) return;
  if (!sc->path[0]) {
    const std::string rel = createScriptFile(ed.project, w.get<Name>(ed.selected)->text);
    if (!rel.empty()) ser::copyStr(sc->path, sizeof sc->path, rel);
  }
  W.target = ed.selected;
  W.name = w.get<Name>(ed.selected)->text;
  W.path = sc->path;
  W.msg.clear();
  W.text.clear();
  if (!readScript(ed.project, W.path, W.text)) W.msg = "Could not read the script file.";
  W.issues = checkScriptSource(W.text, W.path);
  W.dirty = W.checkPending = W.showAll = W.refocus = false;
  W.cursor = 0;
  W.open = true;
}

namespace script_window_detail {
// Callback do campo: guarda a posicao do cursor e aplica uma troca pendente (sugestao ou snippet).
inline int inputCallback(ImGuiInputTextCallbackData* d) {
  auto& W = scriptWindow();
  if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* t = static_cast<std::string*>(d->UserData);
    t->resize(static_cast<size_t>(d->BufTextLen));
    d->Buf = &(*t)[0];
    return 0;
  }
  if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
    W.cursor = static_cast<size_t>(d->CursorPos);
    if (W.hasPending) {
      d->DeleteChars(0, d->BufTextLen);
      d->InsertChars(0, W.pendingText.c_str());
      d->CursorPos = static_cast<int>(W.pendingCursor);
      d->SelectionStart = d->SelectionEnd = static_cast<int>(W.pendingCursor);
      W.hasPending = false;
      W.pendingText.clear();
    }
  }
  return 0;
}

// Agenda a troca: o texto novo entra no campo no proximo quadro, com o foco de volta nele.
inline void queueEdit(const TextEdit& e) {
  auto& W = scriptWindow();
  W.pendingText = e.text; W.pendingCursor = e.cursor; W.hasPending = true; W.refocus = true;
  W.text = e.text; W.dirty = true; W.checkPending = true; W.dirtyAt = ImGui::GetTime();
}
} // namespace script_window_detail

// Desenha a janela (se aberta). Chamada a cada quadro, fora do Inspector.
inline void scriptWindowUi(Editor& ed) {
  auto& W = scriptWindow();
  if (!W.open) return;
  auto& w = ed.scene.world();
  const bool alive = w.alive(W.target) && w.has<ScriptComp>(W.target);
  ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_FirstUseEver);
  const std::string title = std::string("Script: ") + (W.name.empty() ? "?" : W.name) + (W.dirty ? " *" : "") + "###scriptwin";
  bool keep = true;
  if (!ImGui::Begin(title.c_str(), &keep)) { ImGui::End(); if (!keep) W.open = false; return; }
  if (!keep) W.open = false;
  if (!alive) {
    ImGui::TextColored({1.f, .6f, .4f, 1.f}, "%s", "The object of this script was removed or has no script any more.");
    ImGui::End();
    return;
  }
  const ImGuiIO& io = ImGui::GetIO();
  // Atalhos: Ctrl+S salva, Ctrl+Espaco abre a lista completa de sugestoes.
  const bool ctrl = io.KeyCtrl;
  const bool saveKey = ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false);
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) W.showAll = true;

  // Barra de ferramentas.
  if (ImGui::Button("Save") || saveKey) {
    W.msg = writeScript(ed.project, W.path, W.text) ? "Script saved." : "Could not save the script.";
    W.dirty = false;
    W.issues = checkScriptSource(W.text, W.path); W.checkPending = false;
    const std::string who = w.get<Name>(W.target)->text;
    for (const ScriptIssue& is : W.issues) tool::debug().report(is.code.c_str(), is.sev, "[" + who + "] line " + std::to_string(is.line) + ": " + is.msg, "", is.line);
    tool::logInfo("[" + who + "] saved: " + issueSummary(W.issues));
  }
  ImGui::SameLine();
  if (ImGui::Button("Complete (Ctrl+Space)")) W.showAll = !W.showAll;
  ImGui::SameLine();
  if (ImGui::BeginCombo("Snippets", "Insert example...")) {
    for (const Snippet& s : scriptSnippets()) {
      const std::string label = std::to_string(s.n) + ". " + s.title;
      if (ImGui::Selectable(label.c_str())) script_window_detail::queueEdit(insertAt(W.text, W.cursor, s.code + "\n"));
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Close")) W.open = false;
  if (W.dirty) { ImGui::SameLine(); ImGui::TextDisabled("%s", "(unsaved)"); }
  if (!W.msg.empty()) ImGui::TextDisabled("%s", W.msg.c_str());

  // Campo de texto. A altura deixa espaco para as sugestoes e o verificador.
  const float bottom = 210.f;
  if (W.refocus) { ImGui::SetKeyboardFocusHere(); W.refocus = false; }
  const float h = std::max(120.f, ImGui::GetContentRegionAvail().y - bottom);
  if (ImGui::InputTextMultiline("##scripttext", &W.text[0], W.text.capacity() + 1, ImVec2(-1, h),
        ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_AllowTabInput,
        script_window_detail::inputCallback, &W.text)) {
    W.dirty = true; W.checkPending = true; W.dirtyAt = ImGui::GetTime();
  }
  if (W.checkPending && ImGui::GetTime() - W.dirtyAt >= 0.4) { W.issues = checkScriptSource(W.text, W.path); W.checkPending = false; }

  // Autocomplete: aparece ao digitar 2 letras ou com Ctrl+Espaco. Clique aceita.
  const std::string word = completionWord(W.text, W.cursor);
  if (word.size() >= 2 || W.showAll) {
    const std::vector<ApiItem> sugg = completionsFor(word, W.showAll);
    ImGui::BeginChild("##complete", ImVec2(-1, 96), true);
    if (sugg.empty()) ImGui::TextDisabled("%s", "No suggestions.");
    for (const ApiItem& it : sugg) {
      const std::string label = it.name + "   (" + it.kind + ")   " + it.sig;
      if (ImGui::Selectable(label.c_str())) {
        script_window_detail::queueEdit(applyCompletion(W.text, W.cursor, it.ins));
        W.showAll = false;
      }
      if (ImGui::IsItemHovered() && !it.doc.empty()) ImGui::SetTooltip("%s", it.doc.c_str());
    }
    ImGui::EndChild();
  } else if (W.showAll) {
    W.showAll = false;
  }

  // Verificador: OK ou a lista de erros e avisos com a linha de cada um.
  bool anyError = false;
  for (const ScriptIssue& is : W.issues) if (is.sev == tool::Severity::Error) anyError = true;
  if (W.issues.empty()) ImGui::TextColored({.5f, 1.f, .5f, 1.f}, "%s", "Check: OK (no problems found)");
  else ImGui::TextColored(anyError ? ImVec4(1, .4f, .4f, 1) : ImVec4(1, .85f, .3f, 1), "%s", ("Check: " + issueSummary(W.issues)).c_str());
  ImGui::BeginChild("##scriptissues", ImVec2(-1, 0), true);
  std::vector<std::string> src;
  for (size_t p = 0; p <= W.text.size();) { size_t e = W.text.find('\n', p); if (e == std::string::npos) e = W.text.size(); src.push_back(W.text.substr(p, e - p)); p = e + 1; }
  for (size_t i = 0; i < W.issues.size() && i < 60; ++i) {
    const ScriptIssue& is = W.issues[i];
    const ImVec4 c = is.sev == tool::Severity::Error ? ImVec4(1, .4f, .4f, 1) : ImVec4(1, .85f, .3f, 1);
    ImGui::TextColored(c, "%s", ("line " + std::to_string(is.line) + "  " + is.code + "  " + is.msg).c_str());
    if (is.line >= 1 && static_cast<size_t>(is.line) <= src.size()) ImGui::TextDisabled("    %s", src[static_cast<size_t>(is.line) - 1].c_str());
  }
  if (W.issues.size() > 60) ImGui::TextDisabled("%s", ("... " + std::to_string(W.issues.size() - 60) + " more (see Console: Check scripts)").c_str());
  ImGui::EndChild();
  ImGui::End();
}

} // namespace editor
