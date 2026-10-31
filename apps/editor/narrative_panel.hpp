// Painel do Controlador Narrativo: editor visual de grafo (imnodes) sobre o mesmo .json que o jogo carrega.
#pragma once
#include "i18n.hpp"
#include "imgui.h"
#include "imnodes.h"
#include "tool/narrative/narrative.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace editor {
inline void helpIcon(const char* topic); // definido em extra_ui.hpp


class NarrativePanel {
public:
  NarrativePanel() { newGraph(); }

  void newGraph() {
    graph_ = *tool::Json::parse(R"({"version":1,"variables":{"hp":{"type":"number","value":100}},"nodes":[],"links":[]})");
    path_.clear(); placed_.clear(); status_.clear();
  }
  bool open(const std::string& path) {
    std::ifstream f(path, std::ios::binary); if (!f) { status_ = "could not open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    auto j = tool::Json::parse(ss.str());
    if (!j) { status_ = "invalid JSON: " + path; return false; }
    graph_ = *j; path_ = path; std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path.c_str()); placed_.clear(); status_ = "opened: " + path; return true;
  }
  bool save() {
    if (path_.empty()) path_ = "narrative_graph.json";
    syncPositions();
    const std::string tmp = path_ + ".tmp"; // gravacao atomica
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << graph_.dump(); if (!f) { status_ = "failed to save"; return false; } }
    std::error_code ec; std::filesystem::rename(tmp, path_, ec);
    status_ = ec ? "failed to rename" : "saved to " + path_;
    return !ec;
  }
  const std::string& path() const { return path_; }
  bool canvasReady() const { return canvasReady_; }  // o contexto do imnodes existe (posicoes podem ser lidas)
  const tool::Json& graph() const { return graph_; }
  tool::Json& graph() { return graph_; }

  void draw(bool* visible) {
    if (!ImGui::Begin(editor::W("Narrative Controller", "narr").c_str(), visible)) { ImGui::End(); return; }
    editor::helpIcon("narrative");
    if (ImGui::Button(editor::T("New"))) newGraph();
    ImGui::SameLine(); if (ImGui::Button(editor::T("Save"))) save();
    ImGui::SameLine(); if (ImGui::Button(editor::T("Compile"))) compile();
    ImGui::SameLine(); ImGui::SetNextItemWidth(220); ImGui::InputText(editor::T("file"), pathBuf_, sizeof pathBuf_);
    ImGui::SameLine(); if (ImGui::Button(editor::T("Open"))) open(pathBuf_);
    ImGui::SameLine(); ImGui::TextDisabled("%s", status_.c_str());
    if (!compileMsg_.empty()) ImGui::TextColored(compileOk_ ? ImVec4(.4f, 1, .4f, 1) : ImVec4(1, .4f, .4f, 1), "%s", compileMsg_.c_str());
    drawVariables();
    ImGui::Separator();
    drawCanvas();
    ImGui::End();
  }

private:
  static constexpr int kIn = 0, kOut = 1, kThen = 2, kElse = 3;
  static int attr(size_t node, int kind) { return static_cast<int>(node) * 4 + kind; }

  void drawVariables() {
    if (!ImGui::CollapsingHeader(editor::T("Variables"))) return;
    auto* vars = graph_.find("variables");
    if (vars) {
      std::string del;
      for (auto& [name, v] : std::map<std::string, tool::Json>(vars->members())) {
        double val = v["value"].asNumber();
        ImGui::PushID(name.c_str()); ImGui::SetNextItemWidth(120);
        if (ImGui::InputDouble(name.c_str(), &val)) { tool::Json nv = tool::Json::object(); nv.set("type", "number"); nv.set("value", val); vars->set(name, nv); }
        ImGui::SameLine(); if (ImGui::SmallButton("x")) del = name;
        ImGui::PopID();
      }
      if (!del.empty()) vars->erase(del);
    }
    ImGui::SetNextItemWidth(120); ImGui::InputText(editor::T("new"), newVar_, sizeof newVar_);
    ImGui::SameLine();
    if (ImGui::SmallButton("+") && newVar_[0] && vars) { tool::Json nv = tool::Json::object(); nv.set("type", "number"); nv.set("value", 0); vars->set(newVar_, nv); newVar_[0] = 0; }
  }

  void drawCanvas() {
    auto* nodes = graph_.find("nodes"); auto* links = graph_.find("links");
    if (!nodes || !links) return;
    ImGui::BeginChild("canvas", ImVec2(ImGui::GetContentRegionAvail().x * 0.72f, 0));
    ImNodes::BeginNodeEditor(); canvasReady_ = true;
    const size_t n = nodes->items().size();
    for (size_t i = 0; i < n; ++i) {
      tool::Json& nd = nodes->at(i);
      const std::string type = nd["type"].asString(), id = nd["id"].asString();
      ImNodes::BeginNode(static_cast<int>(i));
      // Largura do no controlada: texto longo (id/argumento) e cortado, senao o no cresce a cada quadro (imnodes dimensiona pelo conteudo).
      auto clip = [](std::string t, size_t cap) { if (t.size() > cap) t = t.substr(0, cap - 1) + "~"; return t; };
      ImNodes::BeginNodeTitleBar(); ImGui::TextUnformatted(clip(type + " [" + id + "]", 30).c_str()); ImNodes::EndNodeTitleBar();
      ImGui::Dummy(ImVec2(150.f, 0.f)); // largura minima fixa
      const bool isTrigger = type == "trigger" || type == "onvar" || type == "start" || type == "update" || type == "create", isCond = type == "compare" || type == "and" || type == "or" || type == "not";
      if (!isTrigger && !isCond) { ImNodes::BeginInputAttribute(attr(i, kIn)); ImGui::TextUnformatted(editor::T("input")); ImNodes::EndInputAttribute(); }
      if (type == "branch") {
        ImNodes::BeginOutputAttribute(attr(i, kThen)); ImGui::TextUnformatted(editor::T("then")); ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(attr(i, kElse)); ImGui::TextUnformatted(editor::T("else")); ImNodes::EndOutputAttribute();
      } else if (!isCond) { ImNodes::BeginOutputAttribute(attr(i, kOut)); ImGui::TextUnformatted(editor::T("output")); ImNodes::EndOutputAttribute(); }
      if (nd.has("var") || nd.has("event") || nd.has("name") || nd.has("object") || nd.has("slot")) ImGui::TextDisabled("%s", clip(nd["var"].asString() + nd["event"].asString() + nd["name"].asString() + nd["object"].asString() + nd["slot"].asString(), 24).c_str());
      ImNodes::EndNode();
      if (!placed_.count(id)) { ImNodes::SetNodeGridSpacePos(static_cast<int>(i), ImVec2(std::clamp(static_cast<float>(nd["x"].asNumber(40 + 220.0 * static_cast<double>(i % 4))), -20000.f, 20000.f), std::clamp(static_cast<float>(nd["y"].asNumber(40 + 140.0 * static_cast<double>(i / 4))), -20000.f, 20000.f))); placed_.insert(id); }
    }
    const size_t nl = links->items().size();
    for (size_t l = 0; l < nl; ++l) {
      const tool::Json& lk = links->at(l);
      const int a = findNode(*nodes, lk["from"].asString()), b = findNode(*nodes, lk["to"].asString());
      if (a < 0 || b < 0) continue;
      const std::string port = lk["port"].asString();
      ImNodes::Link(static_cast<int>(l), attr(static_cast<size_t>(a), port == "then" ? kThen : port == "else" ? kElse : kOut), attr(static_cast<size_t>(b), kIn));
    }
    ImNodes::EndNodeEditor();
    // criar ligacao
    int s, e;
    if (ImNodes::IsLinkCreated(&s, &e)) {
      if (s % 4 == kIn) std::swap(s, e); // garante saida -> entrada
      if (s % 4 != kIn && e % 4 == kIn) {
        tool::Json lk = tool::Json::object();
        lk.set("from", nodes->at(static_cast<size_t>(s / 4))["id"]); lk.set("to", nodes->at(static_cast<size_t>(e / 4))["id"]);
        if (s % 4 == kThen) lk.set("port", "then"); else if (s % 4 == kElse) lk.set("port", "else");
        links->push(lk);
      }
    }
    int dl;
    if (ImNodes::IsLinkDestroyed(&dl)) links->removeAt(static_cast<size_t>(dl));
    // menu: adicionar no
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImNodes::IsNodeHovered(&dummy_)) ImGui::OpenPopup("addnode");
    if (ImGui::BeginPopup("addnode")) {
      for (const char* t : {"start", "update", "create", "trigger", "onvar", "compare", "and", "or", "not", "branch", "set", "add", "emit", "action", "delay", "savepoint", "loadpoint"})
        if (ImGui::MenuItem(t)) addNode(t);
      ImGui::EndPopup();
    }
    // apagar selecionados
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && ImNodes::NumSelectedNodes() > 0) {
      std::vector<int> sel(static_cast<size_t>(ImNodes::NumSelectedNodes())); ImNodes::GetSelectedNodes(sel.data());
      std::sort(sel.rbegin(), sel.rend());
      for (int idx : sel) removeNode(static_cast<size_t>(idx));
      ImNodes::ClearNodeSelection();
    }
    selected_ = ImNodes::NumSelectedNodes() == 1 ? (ImNodes::GetSelectedNodes(&selIdx_), selIdx_) : -1;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("props");
    drawProperties(*nodes);
    drawTest();
    ImGui::EndChild();
  }

  void drawProperties(tool::Json& nodes) {
    ImGui::SeparatorText(editor::T("Properties"));
    if (selected_ < 0 || static_cast<size_t>(selected_) >= nodes.items().size()) { ImGui::TextDisabled("%s", editor::T("select a node")); return; }
    tool::Json& nd = nodes.at(static_cast<size_t>(selected_));
    const std::string type = nd["type"].asString();
    auto text = [&](const char* label, const char* key) {
      char buf[128]; std::snprintf(buf, sizeof buf, "%s", nd[key].asString().c_str());
      if (ImGui::InputText(editor::T(label), buf, sizeof buf)) nd.set(key, std::string(buf));
    };
    auto number = [&](const char* label, const char* key) { double v = nd[key].asNumber(); if (ImGui::InputDouble(editor::T(label), &v)) nd.set(key, v); };
    auto op = [&] { static const char* ops[] = {"==", "!=", "<", "<=", ">", ">="}; int cur = 0; for (int i = 0; i < 6; ++i) if (nd["op"].asString() == ops[i]) cur = i; if (ImGui::Combo("op", &cur, ops, 6)) nd.set("op", ops[cur]); };
    auto mode = [&] { static const char* m[] = {"oneshot", "session", "reactivable"}; int cur = 0; for (int i = 0; i < 3; ++i) if (nd["mode"].asString() == m[i]) cur = i; if (ImGui::Combo("modo", &cur, m, 3)) nd.set("mode", m[cur]); };
    text("id", "id");
    if (type == "trigger") { text("event", "event"); mode(); number("group", "group"); }
    else if (type == "start") ImGui::TextDisabled("%s", editor::T("Runs once when the game starts (Play)."));
    else if (type == "update") ImGui::TextDisabled("%s", editor::T("Runs every frame while playing."));
    else if (type == "create") { text("object (name)", "object"); ImGui::TextDisabled("%s", editor::T("Runs when an object with this name is created while playing. Empty = any object.")); mode(); }
    else if (type == "savepoint" || type == "loadpoint") { text("slot", "slot"); ImGui::TextDisabled("%s", editor::T("Saves/loads the narrative variables and flows to Saves/<slot>.save.json")); }
    else if (type == "onvar") { text("variable", "var"); op(); number("value", "value"); number("hysteresis", "hysteresis"); mode(); number("group", "group"); }
    else if (type == "compare") { text("variable", "var"); op(); number("value", "value"); }
    else if (type == "branch") text("condition (id)", "cond");
    else if (type == "set" || type == "add") { text("variable", "var"); number("value", "value"); }
    else if (type == "emit") text("event", "event");
    else if (type == "action") { text("name", "name"); text("argument", "arg"); }
    else if (type == "delay") number("seconds", "seconds");
    else if (type == "and" || type == "or") {
      std::string joined; for (auto& v : nd["inputs"].items()) joined += (joined.empty() ? "" : ",") + v.asString();
      char buf[128]; std::snprintf(buf, sizeof buf, "%s", joined.c_str());
      if (ImGui::InputText(editor::T("inputs (ids)"), buf, sizeof buf)) { tool::Json arr = tool::Json::array(); std::stringstream ss(buf); std::string it; while (std::getline(ss, it, ',')) if (!it.empty()) arr.push(it); nd.set("inputs", arr); }
    } else if (type == "not") text("input (id)", "input");
  }

  void drawTest() {
    ImGui::SeparatorText(editor::T("Quick test"));
    ImGui::SetNextItemWidth(140); ImGui::InputText(editor::T("event"), testEvent_, sizeof testEvent_);
    ImGui::SameLine();
    if (ImGui::Button(editor::T("Fire"))) {
      tool::narr::Program p; std::string err;
      testLog_.clear();
      if (!tool::narr::Compiler().compile(graph_, p, err)) testLog_.push_back("ERROR: " + err);
      else {
        tool::narr::Runtime rt(p);
        rt.setActionHandler([&](const std::string& n, const std::string& a) { testLog_.push_back("action " + n + "(" + a + ")"); });
        rt.setEmitHandler([&](const std::string& e) { testLog_.push_back("emit " + e); });
        const int started = rt.fire(testEvent_); rt.update(1000);
        testLog_.insert(testLog_.begin(), std::to_string(started) + " fluxo(s) iniciado(s)");
      }
    }
    for (auto& l : testLog_) ImGui::TextUnformatted(l.c_str());
  }

  void compile() {
    tool::narr::Program p; std::string err;
    compileOk_ = tool::narr::Compiler().compile(graph_, p, err);
    compileMsg_ = compileOk_ ? "OK: " + std::to_string(p.code.size()) + " instructions, " + std::to_string(p.entries.size()) + " trigger(s)" +
                                   (p.warnings.empty() ? "" : "  | " + p.warnings[0])
                             : err;
  }

  void addNode(const std::string& type) {
    auto* nodes = graph_.find("nodes");
    int k = 1; std::string id;
    for (;; ++k) { id = type + std::to_string(k); bool used = false; for (auto& n : nodes->items()) used |= n["id"].asString() == id; if (!used) break; }
    tool::Json nd = tool::Json::object(); nd.set("id", id); nd.set("type", type);
    if (type == "trigger" || type == "onvar") nd.set("mode", "oneshot");
    if (type == "compare" || type == "onvar") nd.set("op", "==");
    if (type == "update") nd.set("mode", "reactivable");
    if (type == "savepoint" || type == "loadpoint") nd.set("slot", "slot1");
    nodes->push(nd);
  }
  void removeNode(size_t idx) {
    auto* nodes = graph_.find("nodes"); auto* links = graph_.find("links");
    if (idx >= nodes->items().size()) return;
    const std::string id = nodes->at(idx)["id"].asString();
    for (size_t l = links->items().size(); l-- > 0;) if (links->at(l)["from"].asString() == id || links->at(l)["to"].asString() == id) links->removeAt(l);
    nodes->removeAt(idx); placed_.erase(id);
    for (auto& n : nodes->items()) placed_.erase(n["id"].asString()); // reposiciona (indices mudaram)
  }
  static int findNode(const tool::Json& nodes, const std::string& id) { for (size_t i = 0; i < nodes.items().size(); ++i) if (nodes.items()[i]["id"].asString() == id) return static_cast<int>(i); return -1; }
  void syncPositions() {
    auto* nodes = graph_.find("nodes"); if (!nodes) return;
    for (size_t i = 0; i < nodes->items().size(); ++i) { const ImVec2 p = ImNodes::GetNodeGridSpacePos(static_cast<int>(i)); nodes->at(i).set("x", static_cast<double>(p.x)); nodes->at(i).set("y", static_cast<double>(p.y)); }
  }

  tool::Json graph_;
  bool canvasReady_ = false;
  std::string path_, status_, compileMsg_;
  bool compileOk_ = false;
  std::set<std::string> placed_;
  char pathBuf_[256] = "narrative_graph.json", newVar_[32] = "", testEvent_[64] = "event";
  int selected_ = -1, selIdx_ = 0, dummy_ = 0;
  std::vector<std::string> testLog_;
};

} // namespace editor
