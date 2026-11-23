#pragma once
// Modelo do Controlador Narrativo (sem interface): grafo JSON, arquivo, bloco fixo da cena, nos, ligacoes,
// variaveis, compilacao e teste rapido. A janela (narrative_panel.hpp, SpriteUI) so le e altera este modelo.
// O mesmo .json e carregado pelo jogo. Posicoes dos nos ficam nos campos "x" e "y" de cada no (espaco do grafo).

#include "tool/narrative/narrative.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace editor {

class NarrativeModel {
public:
  // Tipos de no que o menu "novo no" oferece, na ordem do menu.
  static const std::vector<std::string>& nodeTypes() {
    static const std::vector<std::string> t{"start", "update", "create", "trigger", "onvar", "compare", "and", "or",
                                            "not", "branch", "set", "add", "emit", "action", "delay", "savepoint", "loadpoint"};
    return t;
  }

  NarrativeModel() { newGraph(); }

  // Grafo vazio com a variavel "hp" e o bloco padrao da cena (se a cena ja foi informada).
  void newGraph() {
    graph_ = *tool::Json::parse(R"({"version":1,"variables":{"hp":{"type":"number","value":100}},"nodes":[],"links":[]})");
    path_.clear();
    status_.clear();
    ensureSceneBlock();
  }

  // Cena ativa: o grafo ganha o bloco padrao da cena (indestrutivel, com o nome da cena). Bloco novo: grava no arquivo.
  void setScene(const std::string& name) {
    sceneName_ = name;
    if (ensureSceneBlock() && !path_.empty()) save();
  }

  bool open(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { status_ = "could not open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    auto j = tool::Json::parse(ss.str());
    if (!j) { status_ = "invalid JSON: " + path; return false; }
    graph_ = *j;
    path_ = path;
    status_ = "opened: " + path;
    ensureSceneBlock();
    return true;
  }

  // Gravacao atomica: escreve num .tmp e troca pelo arquivo.
  bool save() {
    if (path_.empty()) path_ = "narrative_graph.json";
    const std::string tmp = path_ + ".tmp";
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      f << graph_.dump();
      if (!f) { status_ = "failed to save"; return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    status_ = ec ? "failed to rename" : "saved to " + path_;
    return !ec;
  }

  [[nodiscard]] const std::string& path() const { return path_; }
  [[nodiscard]] const std::string& status() const { return status_; }
  void setStatus(std::string s) { status_ = std::move(s); }
  [[nodiscard]] const tool::Json& graph() const { return graph_; }
  tool::Json& graph() { return graph_; }
  [[nodiscard]] const std::string& compileMessage() const { return compileMsg_; }
  [[nodiscard]] bool compileOk() const { return compileOk_; }
  [[nodiscard]] const std::vector<std::string>& testLog() const { return testLog_; }

  // Nos e ligacoes (atalhos seguros: vazio quando o grafo nao tem a lista).
  tool::Json* nodeList() { return graph_.find("nodes"); }
  tool::Json* linkList() { return graph_.find("links"); }
  tool::Json* varList() { return graph_.find("variables"); }

  [[nodiscard]] static int findNode(const tool::Json& nodes, const std::string& id) {
    for (std::size_t i = 0; i < nodes.items().size(); ++i)
      if (nodes.items()[i]["id"].asString() == id) return static_cast<int>(i);
    return -1;
  }

  // Cria um no do tipo `type` na posicao (x, y) do grafo. Devolve o indice do novo no, ou -1.
  int addNode(const std::string& type, double x, double y) {
    auto* nodes = nodeList();
    if (!nodes) return -1;
    int k = 1;
    std::string id;
    for (;; ++k) {
      id = type + std::to_string(k);
      bool used = false;
      for (auto& n : nodes->items()) used |= n["id"].asString() == id;
      if (!used) break;
    }
    tool::Json nd = tool::Json::object();
    nd.set("id", id);
    nd.set("type", type);
    if (type == "trigger" || type == "onvar") nd.set("mode", "oneshot");
    if (type == "compare" || type == "onvar") nd.set("op", "==");
    if (type == "update") nd.set("mode", "reactivable");
    if (type == "savepoint" || type == "loadpoint") nd.set("slot", "slot1");
    nd.set("x", x);
    nd.set("y", y);
    nodes->push(nd);
    return static_cast<int>(nodes->items().size()) - 1;
  }

  // Apaga o no (e as ligacoes dele). O bloco fixo da cena nao pode ser apagado. True se apagou.
  bool removeNode(std::size_t idx) {
    auto* nodes = nodeList();
    auto* links = linkList();
    if (!nodes || !links || idx >= nodes->items().size()) return false;
    if (nodes->at(idx).has("pinned")) { status_ = "the scene block can't be deleted"; return false; }
    const std::string id = nodes->at(idx)["id"].asString();
    for (std::size_t l = links->items().size(); l-- > 0;)
      if (links->at(l)["from"].asString() == id || links->at(l)["to"].asString() == id) links->removeAt(l);
    nodes->removeAt(idx);
    return true;
  }

  // Liga a saida `port` ("out", "then" ou "else") do no `from` a entrada do no `to`. True se criou.
  bool addLink(const std::string& fromId, const std::string& port, const std::string& toId) {
    auto* lks = linkList();
    auto* nds = nodeList();
    if (!lks || !nds || fromId == toId) return false;
    if (findNode(*nds, fromId) < 0 || findNode(*nds, toId) < 0) return false;
    // "out" nao e gravado (saida padrao): a comparacao usa a porta normalizada.
    const std::string stored = port == "out" ? std::string() : port;
    for (auto& l : lks->items())   // a mesma ligacao nao entra duas vezes
      if (l["from"].asString() == fromId && l["to"].asString() == toId && l["port"].asString() == stored) return false;
    tool::Json lk = tool::Json::object();
    lk.set("from", fromId);
    lk.set("to", toId);
    if (port == "then" || port == "else") lk.set("port", port);
    lks->push(lk);
    return true;
  }
  void removeLink(std::size_t idx) {
    if (auto* links = linkList(); links && idx < links->items().size()) links->removeAt(idx);
  }

  // Variaveis numericas.
  bool addVariable(const std::string& name) {
    auto* vars = varList();
    if (!vars || name.empty() || vars->has(name)) return false;
    tool::Json nv = tool::Json::object();
    nv.set("type", "number");
    nv.set("value", 0.0);
    vars->set(name, nv);
    return true;
  }
  void setVariable(const std::string& name, double value) {
    auto* vars = varList();
    if (!vars || !vars->has(name)) return;
    tool::Json nv = tool::Json::object();
    nv.set("type", "number");
    nv.set("value", value);
    vars->set(name, nv);
  }
  void removeVariable(const std::string& name) {
    if (auto* vars = varList()) vars->erase(name);
  }

  // Compila o grafo e guarda a mensagem (sucesso com contagem, ou o erro).
  bool compile() {
    tool::narr::Program p;
    std::string err;
    compileOk_ = tool::narr::Compiler().compile(graph_, p, err);
    compileMsg_ = compileOk_ ? "OK: " + std::to_string(p.code.size()) + " instructions, " + std::to_string(p.entries.size()) + " trigger(s)" +
                                   (p.warnings.empty() ? "" : "  | " + p.warnings[0])
                             : err;
    return compileOk_;
  }

  // Teste rapido: dispara o evento no grafo compilado e roda um passo de 1 s. Guarda o log.
  void fireTest(const std::string& event) {
    tool::narr::Program p;
    std::string err;
    testLog_.clear();
    if (!tool::narr::Compiler().compile(graph_, p, err)) {
      testLog_.push_back("ERROR: " + err);
      return;
    }
    tool::narr::Runtime rt(p);
    rt.setActionHandler([&](const std::string& n, const std::string& a) { testLog_.push_back("action " + n + "(" + a + ")"); });
    rt.setEmitHandler([&](const std::string& e) { testLog_.push_back("emit " + e); });
    const int started = rt.fire(event);
    rt.update(1000);
    testLog_.insert(testLog_.begin(), std::to_string(started) + " fluxo(s) iniciado(s)");
  }

  // Edicao de um no pelo indice. Renomear troca o id nas ligacoes; o bloco fixo da cena mantem o id.
  bool renameNode(std::size_t idx, const std::string& newId) {
    auto* nds = nodeList();
    auto* lks = linkList();
    if (!nds || !lks || idx >= nds->items().size() || newId.empty()) return false;
    if (nds->at(idx).has("pinned")) return false;
    const std::string oldId = nds->at(idx)["id"].asString();
    if (newId == oldId) return true;
    if (findNode(*nds, newId) >= 0) return false;   // id em uso
    nds->at(idx).set("id", newId);
    for (std::size_t i = 0; i < lks->items().size(); ++i) {
      if (lks->at(i)["from"].asString() == oldId) lks->at(i).set("from", newId);
      if (lks->at(i)["to"].asString() == oldId) lks->at(i).set("to", newId);
    }
    return true;
  }
  void setNodeString(std::size_t idx, const std::string& key, const std::string& value) {
    if (auto* nds = nodeList(); nds && idx < nds->items().size()) nds->at(idx).set(key, value);
  }
  void setNodeNumber(std::size_t idx, const std::string& key, double value) {
    if (auto* nds = nodeList(); nds && idx < nds->items().size()) nds->at(idx).set(key, value);
  }
  void setNodeList(std::size_t idx, const std::string& key, const std::vector<std::string>& items) {
    if (auto* nds = nodeList(); nds && idx < nds->items().size()) {
      tool::Json arr = tool::Json::array();
      for (const std::string& it : items) arr.push(it);
      nds->at(idx).set(key, arr);
    }
  }

  // Garante um unico bloco fixo da cena (no start marcado com "pinned"); atualiza o nome se a cena foi renomeada.
  bool ensureSceneBlock() {
    auto* nodes = nodeList();
    if (!nodes || sceneName_.empty()) return false;
    for (std::size_t i = 0; i < nodes->items().size(); ++i)
      if (nodes->at(i).has("pinned")) { nodes->at(i).set("label", sceneName_); return false; }
    std::string id = "scene";
    for (int k = 2; findNode(*nodes, id) >= 0; ++k) id = "scene" + std::to_string(k);
    tool::Json nd = tool::Json::object();
    nd.set("id", id);
    nd.set("type", "start");
    nd.set("pinned", true);
    nd.set("label", sceneName_);
    nd.set("x", 40.0);
    nd.set("y", 40.0);
    nodes->push(nd);
    return true;
  }

private:
  tool::Json graph_;
  std::string path_, status_, compileMsg_, sceneName_;
  bool compileOk_ = false;
  std::vector<std::string> testLog_;
};

} // namespace editor
