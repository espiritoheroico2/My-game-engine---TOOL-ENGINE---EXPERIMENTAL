// ToolEngine Editor - temas de cores da UI.
// Um tema = 5 cores-base (fundo, superficie, texto, destaque, borda) + arredondamento + escala.
// Dessas cores sai a paleta completa do ImGui; qualquer cor pode ser ajustada depois na lista
// avancada. Temas do usuario ficam em assets/themes/*.json (copie/edite/compartilhe).
#pragma once
#include "i18n.hpp"
#include "imgui.h"
#include "tool/core/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace editor {

struct ThemeBase {
  ImVec4 bg, surface, text, accent, border;
  float rounding = 4.f;
};

struct ThemePreset {
  const char* key; // chave de traducao do nome
  ThemeBase base;
};

inline const std::vector<ThemePreset>& builtinThemes() {
  static const std::vector<ThemePreset> v = {
      {"Dark", {{0.09f, 0.09f, 0.10f, 1}, {0.14f, 0.14f, 0.16f, 1}, {0.90f, 0.91f, 0.93f, 1}, {0.26f, 0.55f, 0.96f, 1}, {0.24f, 0.24f, 0.28f, 1}, 4.f}},
      {"Light", {{0.94f, 0.94f, 0.95f, 1}, {0.99f, 0.99f, 1.00f, 1}, {0.10f, 0.11f, 0.13f, 1}, {0.16f, 0.45f, 0.90f, 1}, {0.76f, 0.77f, 0.80f, 1}, 4.f}},
      {"Midnight", {{0.04f, 0.05f, 0.10f, 1}, {0.08f, 0.10f, 0.18f, 1}, {0.85f, 0.89f, 1.00f, 1}, {0.45f, 0.40f, 0.95f, 1}, {0.16f, 0.19f, 0.32f, 1}, 6.f}},
      {"Forest", {{0.07f, 0.10f, 0.08f, 1}, {0.11f, 0.16f, 0.12f, 1}, {0.88f, 0.94f, 0.88f, 1}, {0.30f, 0.76f, 0.42f, 1}, {0.19f, 0.28f, 0.21f, 1}, 4.f}},
      {"Ember", {{0.10f, 0.08f, 0.08f, 1}, {0.17f, 0.12f, 0.11f, 1}, {0.95f, 0.90f, 0.86f, 1}, {0.96f, 0.45f, 0.20f, 1}, {0.30f, 0.20f, 0.18f, 1}, 3.f}},
      {"High contrast", {{0.00f, 0.00f, 0.00f, 1}, {0.06f, 0.06f, 0.06f, 1}, {1.00f, 1.00f, 1.00f, 1}, {1.00f, 0.90f, 0.10f, 1}, {0.80f, 0.80f, 0.80f, 1}, 0.f}},
  };
  return v;
}

struct ThemeFile {
  std::string name, path;
};

class Theme {
public:
  ThemeBase base = builtinThemes()[0].base;
  std::string current = "Dark"; // chave de preset, nome de arquivo ou "Custom"
  float scale = 1.f;
  std::vector<ThemeFile> files;

  static ImVec4 mix(ImVec4 a, ImVec4 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}; }
  static ImVec4 alpha(ImVec4 c, float a) { c.w = a; return c; }
  static float luma(ImVec4 c) { return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z; }

  // Gera a paleta completa a partir das 5 cores-base e aplica no estilo do ImGui.
  void apply() const {
    ImGuiStyle& s = ImGui::GetStyle();
    const ThemeBase& b = base;
    const bool light = luma(b.bg) > 0.5f;
    const ImVec4 dim = mix(b.bg, b.text, 0.5f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = b.text;
    c[ImGuiCol_TextDisabled] = dim;
    c[ImGuiCol_WindowBg] = b.bg;
    c[ImGuiCol_ChildBg] = alpha(b.bg, 0.f);
    c[ImGuiCol_PopupBg] = mix(b.surface, b.bg, 0.2f);
    c[ImGuiCol_Border] = b.border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = mix(b.surface, b.bg, light ? 0.f : 0.35f);
    c[ImGuiCol_FrameBgHovered] = mix(b.surface, b.accent, 0.25f);
    c[ImGuiCol_FrameBgActive] = mix(b.surface, b.accent, 0.40f);
    c[ImGuiCol_TitleBg] = mix(b.bg, b.surface, 0.6f);
    c[ImGuiCol_TitleBgActive] = b.surface;
    c[ImGuiCol_TitleBgCollapsed] = b.bg;
    c[ImGuiCol_MenuBarBg] = b.surface;
    c[ImGuiCol_ScrollbarBg] = alpha(b.bg, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = mix(b.surface, b.text, 0.25f);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(b.surface, b.text, 0.4f);
    c[ImGuiCol_ScrollbarGrabActive] = b.accent;
    c[ImGuiCol_CheckMark] = b.accent;
    c[ImGuiCol_SliderGrab] = b.accent;
    c[ImGuiCol_SliderGrabActive] = mix(b.accent, b.text, 0.3f);
    c[ImGuiCol_Button] = mix(b.surface, b.accent, 0.45f);
    c[ImGuiCol_ButtonHovered] = mix(b.surface, b.accent, 0.75f);
    c[ImGuiCol_ButtonActive] = b.accent;
    c[ImGuiCol_Header] = mix(b.surface, b.accent, 0.40f);
    c[ImGuiCol_HeaderHovered] = mix(b.surface, b.accent, 0.65f);
    c[ImGuiCol_HeaderActive] = mix(b.surface, b.accent, 0.85f);
    c[ImGuiCol_Separator] = b.border;
    c[ImGuiCol_SeparatorHovered] = mix(b.border, b.accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = b.accent;
    c[ImGuiCol_ResizeGrip] = alpha(b.accent, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = alpha(b.accent, 0.65f);
    c[ImGuiCol_ResizeGripActive] = alpha(b.accent, 0.95f);
    c[ImGuiCol_Tab] = mix(b.bg, b.surface, 0.8f);
    c[ImGuiCol_TabHovered] = mix(b.surface, b.accent, 0.7f);
    c[ImGuiCol_TabSelected] = mix(b.surface, b.accent, 0.5f);
    c[ImGuiCol_TabSelectedOverline] = b.accent;
    c[ImGuiCol_TabDimmed] = mix(b.bg, b.surface, 0.5f);
    c[ImGuiCol_TabDimmedSelected] = mix(b.surface, b.accent, 0.25f);
    c[ImGuiCol_TabDimmedSelectedOverline] = alpha(b.accent, 0.5f);
    c[ImGuiCol_DockingPreview] = alpha(b.accent, 0.7f);
    c[ImGuiCol_DockingEmptyBg] = mix(b.bg, {0, 0, 0, 1}, light ? 0.f : 0.3f);
    c[ImGuiCol_PlotLines] = b.accent;
    c[ImGuiCol_PlotLinesHovered] = mix(b.accent, b.text, 0.4f);
    c[ImGuiCol_PlotHistogram] = b.accent;
    c[ImGuiCol_PlotHistogramHovered] = mix(b.accent, b.text, 0.4f);
    c[ImGuiCol_TableHeaderBg] = b.surface;
    c[ImGuiCol_TableBorderStrong] = b.border;
    c[ImGuiCol_TableBorderLight] = alpha(b.border, 0.5f);
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = alpha(b.text, 0.04f);
    c[ImGuiCol_TextSelectedBg] = alpha(b.accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = b.accent;
    c[ImGuiCol_NavCursor] = b.accent;
    c[ImGuiCol_NavWindowingHighlight] = alpha(b.text, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = alpha(b.bg, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = alpha(b.bg, 0.6f);

    // Formas: mais "ar" que o padrao do ImGui.
    s.WindowRounding = s.PopupRounding = b.rounding;
    s.FrameRounding = s.GrabRounding = s.TabRounding = s.ScrollbarRounding = b.rounding;
    s.ChildRounding = b.rounding;
    s.WindowPadding = {10, 8};
    s.FramePadding = {8, 4};
    s.ItemSpacing = {8, 6};
    s.ItemInnerSpacing = {6, 4};
    s.WindowBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.TabBarOverlineSize = 2.f;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    ImGui::GetIO().FontGlobalScale = scale;
  }

  void setPreset(size_t i) {
    base = builtinThemes()[i].base;
    current = builtinThemes()[i].key;
    apply();
  }

  // --- arquivos de tema ---------------------------------------------------------------------------------
  static tool::Json col(ImVec4 c) {
    tool::Json a = tool::Json::array();
    a.push(c.x); a.push(c.y); a.push(c.z); a.push(c.w);
    return a;
  }
  static ImVec4 readCol(const tool::Json& a, ImVec4 d) {
    if (!a.isArray() || a.items().size() < 3) return d;
    return {static_cast<float>(a.items()[0].asNumber(d.x)), static_cast<float>(a.items()[1].asNumber(d.y)), static_cast<float>(a.items()[2].asNumber(d.z)),
            a.items().size() > 3 ? static_cast<float>(a.items()[3].asNumber(1)) : 1.f};
  }

  // Salva base + todas as cores finais (inclusive ajustes avancados).
  bool save(const std::string& path, const std::string& name) const {
    tool::Json j = tool::Json::object(), colors = tool::Json::object();
    j.set("name", name); j.set("rounding", base.rounding); j.set("scale", scale);
    j.set("bg", col(base.bg)); j.set("surface", col(base.surface)); j.set("text", col(base.text)); j.set("accent", col(base.accent)); j.set("border", col(base.border));
    for (int i = 0; i < ImGuiCol_COUNT; ++i) colors.set(ImGui::GetStyleColorName(i), col(ImGui::GetStyle().Colors[i]));
    j.set("colors", colors);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << j.dump();
    return static_cast<bool>(f);
  }

  bool load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    auto j = tool::Json::parse(ss.str());
    if (!j || !j->isObject()) return false;
    base.bg = readCol((*j)["bg"], base.bg); base.surface = readCol((*j)["surface"], base.surface); base.text = readCol((*j)["text"], base.text);
    base.accent = readCol((*j)["accent"], base.accent); base.border = readCol((*j)["border"], base.border);
    base.rounding = static_cast<float>((*j)["rounding"].asNumber(base.rounding));
    scale = std::clamp(static_cast<float>((*j)["scale"].asNumber(scale)), 0.7f, 2.5f);
    apply();
    for (auto& [name, v] : (*j)["colors"].members())
      for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (name == ImGui::GetStyleColorName(i)) ImGui::GetStyle().Colors[i] = readCol(v, ImGui::GetStyle().Colors[i]);
    current = (*j)["name"].asString().empty() ? std::filesystem::path(path).stem().string() : (*j)["name"].asString();
    return true;
  }

  void scan(const std::string& dir) {
    files.clear();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
      if (e.path().extension() == ".json") files.push_back({e.path().stem().string(), e.path().generic_string()});
    std::sort(files.begin(), files.end(), [](const ThemeFile& a, const ThemeFile& b) { return a.name < b.name; });
  }
};

} // namespace editor
