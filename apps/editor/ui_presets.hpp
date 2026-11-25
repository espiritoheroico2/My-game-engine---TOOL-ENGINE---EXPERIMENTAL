#pragma once

// Presets do visual do editor (campos de texto e caixas do SpriteUI), salvos em JSON.
// Segue o padrao de theme.hpp: um arquivo .json por preset, numa pasta, lido com tool::Json.
//
// Regras:
//  - Parte SEM SpriteUI: so dados e E/S de arquivo. Assim pode ser testada sem janela
//    (apps/editor/tests/test_ui_presets.cpp). A tela converte UiPreset em TextFieldStyle.
//  - Leitura tolerante: chave ausente usa o padrao; chave de tipo errado tambem; valores fora da faixa
//    sao limitados (clamp). Chaves desconhecidas sao ignoradas (arquivos de versoes novas continuam abrindo).
//  - Gravacao: o JSON tem "presetVersion" para futuras mudancas de formato.
//  - Cores: [r, g, b, a] em espaco linear, como o resto do editor.

#include "tool/core/json.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace editor::presets {

using Rgba = std::array<float, 4>;

struct UiPreset {
    std::string name = "Padrao";
    float fontSize = 16.0f;       // px. Faixa 8..48.
    float fieldHeight = 30.0f;    // px. Faixa 20..64.
    float paddingX = 8.0f;        // recuo horizontal do texto dentro do campo. Faixa 0..24.
    float caretWidth = 1.5f;      // px. Faixa 1..4.
    Rgba background{0.14f, 0.14f, 0.15f, 1.0f};
    Rgba focusBackground{0.18f, 0.18f, 0.20f, 1.0f};
    Rgba text{0.92f, 0.92f, 0.94f, 1.0f};
    Rgba caret{0.95f, 0.95f, 1.0f, 1.0f};
    Rgba selection{0.25f, 0.42f, 0.70f, 0.55f};

    friend bool operator==(const UiPreset& a, const UiPreset& b) {
        return a.name == b.name && a.fontSize == b.fontSize && a.fieldHeight == b.fieldHeight &&
               a.paddingX == b.paddingX && a.caretWidth == b.caretWidth && a.background == b.background &&
               a.focusBackground == b.focusBackground && a.text == b.text && a.caret == b.caret &&
               a.selection == b.selection;
    }
};

inline constexpr int kPresetVersion = 1;

// Limita um valor a uma faixa. NaN volta para o minimo (a faixa e a unica parte que nunca e invalida).
inline float Clamp(float v, float lo, float hi) {
    if (v != v) return lo;
    return std::clamp(v, lo, hi);
}

inline tool::Json ColorToJson(const Rgba& c) {
    tool::Json a = tool::Json::array();
    for (float v : c) a.push(static_cast<double>(v));
    return a;
}

// Le uma cor; se o formato estiver errado, devolve o padrao.
inline Rgba ColorFromJson(const tool::Json& a, const Rgba& fallback) {
    if (!a.isArray() || a.items().size() < 3) return fallback;
    Rgba out = fallback;
    for (std::size_t i = 0; i < 4 && i < a.items().size(); ++i) {
        out[i] = Clamp(static_cast<float>(a.items()[i].asNumber(fallback[i])), 0.0f, 1.0f);
    }
    return out;
}

inline tool::Json ToJson(const UiPreset& p) {
    tool::Json j = tool::Json::object();
    j.set("presetVersion", kPresetVersion);
    j.set("name", p.name);
    j.set("fontSize", static_cast<double>(p.fontSize));
    j.set("fieldHeight", static_cast<double>(p.fieldHeight));
    j.set("paddingX", static_cast<double>(p.paddingX));
    j.set("caretWidth", static_cast<double>(p.caretWidth));
    j.set("background", ColorToJson(p.background));
    j.set("focusBackground", ColorToJson(p.focusBackground));
    j.set("text", ColorToJson(p.text));
    j.set("caret", ColorToJson(p.caret));
    j.set("selection", ColorToJson(p.selection));
    return j;
}

// Converte um JSON em preset. Nao falha por campo ausente: devolve nullopt so se o documento nao for objeto.
inline std::optional<UiPreset> FromJson(const tool::Json& j) {
    if (!j.isObject()) return std::nullopt;
    const UiPreset d;
    UiPreset p;
    p.name = j["name"].isString() ? j["name"].asString() : d.name;
    if (p.name.empty()) p.name = d.name;
    p.fontSize = Clamp(static_cast<float>(j["fontSize"].asNumber(d.fontSize)), 8.0f, 48.0f);
    p.fieldHeight = Clamp(static_cast<float>(j["fieldHeight"].asNumber(d.fieldHeight)), 20.0f, 64.0f);
    p.paddingX = Clamp(static_cast<float>(j["paddingX"].asNumber(d.paddingX)), 0.0f, 24.0f);
    p.caretWidth = Clamp(static_cast<float>(j["caretWidth"].asNumber(d.caretWidth)), 1.0f, 4.0f);
    p.background = ColorFromJson(j["background"], d.background);
    p.focusBackground = ColorFromJson(j["focusBackground"], d.focusBackground);
    p.text = ColorFromJson(j["text"], d.text);
    p.caret = ColorFromJson(j["caret"], d.caret);
    p.selection = ColorFromJson(j["selection"], d.selection);
    return p;
}

// Nome de arquivo seguro: so letras, numeros, espaco, hifen e sublinhado. O resto vira "_".
inline std::string FileStemFor(const std::string& name) {
    std::string s;
    for (unsigned char c : name) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ' ' ||
                        c == '-' || c == '_';
        s += ok ? static_cast<char>(c) : '_';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s.empty() ? std::string("preset") : s;
}

// Grava o preset em <dir>/<nome>.json. Cria a pasta se preciso. false se nao conseguir gravar.
inline bool SavePreset(const std::filesystem::path& dir, const UiPreset& p) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path file = dir / (FileStemFor(p.name) + ".json");
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << ToJson(p).dump();
    return static_cast<bool>(f);
}

// Le um preset de um arquivo. nullopt se o arquivo nao existir ou o JSON estiver quebrado.
inline std::optional<UiPreset> LoadPreset(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    const auto j = tool::Json::parse(ss.str());
    if (!j) return std::nullopt;
    return FromJson(*j);
}

// Lista os presets de uma pasta (ordenados pelo nome do arquivo). Arquivos invalidos sao ignorados.
inline std::vector<UiPreset> ListPresets(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return {};
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".json") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<UiPreset> out;
    for (const auto& file : files) {
        if (auto p = LoadPreset(file)) out.push_back(std::move(*p));
    }
    return out;
}

} // namespace editor::presets
