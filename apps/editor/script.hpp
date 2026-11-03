// ToolEngine Editor - scripts dos objetos (arquivos .tool.js em Project/Assets/Scripts).
//
// Todo objeto criado no editor ja nasce com um script ToolObject contendo start(), update(dt), OnDelete() e
// DeleteAndReuse(). O arquivo e texto puro dentro do projeto (entra no controle de versao, pode ser editado em
// qualquer editor de codigo ou na caixa de texto do Inspector).
//
// ESTADO ATUAL (v0.4): o editor cria, guarda, edita e salva os scripts; a EXECUCAO deles (maquina virtual embutida,
// planejada com QuickJS) ainda nao existe - ver docs/NEXT_STEPS.md. Ate la, as funcoes nativas equivalentes
// (tool::ToolObject::DeleteAndReuse etc.) continuam disponiveis em C++.
//
// SEGURANCA: o caminho vem do arquivo de cena (nao confiavel). Todo acesso passa por scriptPathOk():
//   - relativo, sem "..", dentro de "Assets/", terminando em ".js";  - tamanho maximo de 256 KB ao ler.
#pragma once
#include "project.hpp"

#include <cctype>
#include <string>

namespace editor {

inline constexpr size_t kMaxScriptBytes = 256u << 10;

// true se `rel` e um caminho de script aceitavel (ver SEGURANCA acima).
inline bool scriptPathOk(const Project& proj, const std::string& rel) {
  if (rel.size() < 11 || rel.size() >= 160 || rel.rfind("Assets/", 0) != 0) return false;
  if (rel.size() < 3 || rel.compare(rel.size() - 3, 3, ".js") != 0) return false;
  return !proj.resolve(rel).empty();
}

// "Cube (2)" -> "Cube2": nome de classe valido (letras/numeros, comeca com letra).
inline std::string scriptClassName(const std::string& objectName) {
  std::string n; bool up = true;
  for (unsigned char c : objectName) {
    if (std::isalnum(c)) { n += static_cast<char>(up ? std::toupper(c) : c); up = false; }
    else up = true;
  }
  if (n.empty() || std::isdigit(static_cast<unsigned char>(n[0]))) n = "Object" + n;
  return n.substr(0, 40);
}

// Texto inicial de um script. Usa a mesma forma que o runtime QuickJS executa: funcoes (sem classes).
// A lista completa da API e os exemplos testados ficam na janela Help > "Scripts: exemplos (QuickJS)".
inline std::string scriptTemplate(const std::string& objectName) {
  std::string n; for (char c : objectName) if (c != '"' && c != '\\' && c != '\n' && c != '\r') n += c;
  return "// Script of the object \"" + n + "\" (QuickJS). The engine calls these functions during Play.\n"
         "//   start(obj)              once, when the object starts\n"
         "//   update(obj, dt)         every frame (dt = seconds since the last frame)\n"
         "//   OnEventTrigger(obj, name) every event sent to this object (send, or the Narrative)\n"
         "// Example: obj.transform.rot[1] += 45 * dt;   // spins 45 degrees per second\n"
         "// Help > Scripts: examples has tested examples (player, shooting, cameras, colors, events).\n"
         "\n"
         "export function start(obj) {\n"
         "}\n"
         "\n"
         "export function update(obj, dt) {\n"
         "}\n"
         "\n"
         "export function OnEventTrigger(obj, name) {\n"
         "}\n";
}

// Cria Assets/Scripts/<nome>.tool.js (acrescenta " (2)", " (3)"... se ja existir). Retorna o caminho relativo ou "".
inline std::string createScriptFile(const Project& proj, const std::string& objectName) {
  std::string base;
  for (char c : objectName) base += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
  if (base.empty()) base = "Object";
  if (base.size() > 48) base.resize(48);
  std::error_code ec;
  for (int i = 1; i < 1000; ++i) {
    const std::string rel = "Assets/Scripts/" + base + (i == 1 ? "" : " (" + std::to_string(i) + ")") + ".tool.js";
    if (!scriptPathOk(proj, rel)) return {};
    const fs::path p = proj.resolve(rel);
    if (fs::exists(p, ec)) continue;
    return writeTextFile(p, scriptTemplate(objectName)) ? rel : std::string();
  }
  return {};
}

inline bool readScript(const Project& proj, const std::string& rel, std::string& out) {
  return scriptPathOk(proj, rel) && readTextFile(proj.resolve(rel), out, kMaxScriptBytes);
}
inline bool writeScript(const Project& proj, const std::string& rel, const std::string& text) {
  return scriptPathOk(proj, rel) && text.size() <= kMaxScriptBytes && writeTextFile(proj.resolve(rel), text);
}

} // namespace editor
