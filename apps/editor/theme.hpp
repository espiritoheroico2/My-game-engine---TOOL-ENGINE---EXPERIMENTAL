// ToolEngine Editor - temas de cores e escala da interface (sem ImGui).
// Um tema = 5 cores-base (fundo, superficie, texto, destaque, borda) + arredondamento + escala. Quem desenha as cores e
// a SpriteUI, pela paleta de sui_palette.hpp (Configuracoes > Appearance). Este arquivo guarda a escolha, a escala e o
// arquivo de tema (assets/themes/*.json e editor_theme.json). Temas do usuario ficam em assets/themes/*.json.
#pragma once
#include "i18n.hpp"
#include "tool/core/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace editor {

struct Rgba { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };

struct ThemeBase {
  Rgba bg, surface, text, accent, border;
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
  // Escala da interface: 125% por padrao (como o Windows em Full HD). Vale para fontes e para as medidas do layout.
  static constexpr float kDefaultScale = 1.25f;
  float scale = kDefaultScale;
  std::vector<ThemeFile> files;

  void setPreset(size_t i) {
    base = builtinThemes()[i].base;
    current = builtinThemes()[i].key;
  }

  // --- arquivos de tema ---------------------------------------------------------------------------------
  static tool::Json col(Rgba c) {
    tool::Json a = tool::Json::array();
    a.push(c.x); a.push(c.y); a.push(c.z); a.push(c.w);
    return a;
  }
  static Rgba readCol(const tool::Json& a, Rgba d) {
    if (!a.isArray() || a.items().size() < 3) return d;
    return {static_cast<float>(a.items()[0].asNumber(d.x)), static_cast<float>(a.items()[1].asNumber(d.y)), static_cast<float>(a.items()[2].asNumber(d.z)),
            a.items().size() > 3 ? static_cast<float>(a.items()[3].asNumber(1)) : 1.f};
  }

  // Salva as 5 cores-base, o arredondamento, a escala e o nome.
  bool save(const std::string& path, const std::string& name) const {
    tool::Json j = tool::Json::object();
    j.set("name", name); j.set("uiVersion", 3.0); j.set("rounding", base.rounding); j.set("scale", scale);
    j.set("bg", col(base.bg)); j.set("surface", col(base.surface)); j.set("text", col(base.text)); j.set("accent", col(base.accent)); j.set("border", col(base.border));
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
    // Arquivos salvos antes da versao 2 traziam escala 1.0 por padrao: nesses, usamos a nova escala padrao (125%).
    const bool v2 = (*j)["uiVersion"].asNumber(0.0) >= 2.0;
    scale = v2 ? std::clamp(static_cast<float>((*j)["scale"].asNumber(kDefaultScale)), 0.7f, 2.5f) : kDefaultScale;
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
