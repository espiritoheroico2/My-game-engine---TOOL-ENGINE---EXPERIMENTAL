#pragma once

// Inspetor: regras que nao dependem de janela. Guarda quais secoes de componente estao abertas, prende valores de
// slider nos limites, recusa numeros invalidos (NaN, infinito, texto) e le cores HEX. A janela SpriteUI
// (inspector_panel.hpp) so chama estas funcoes; assim a regra fica num lugar so e roda no ctest sem janela, como
// Editor.InspectorModel.
//
// Exemplo: a Luz do sol tem cor, intensidade e sombra. A secao "Global Light (sun)" pode estar fechada: o modelo
// guarda isso entre um quadro e outro. Se o usuario digitar "2,5" na intensidade, ParseNumber devolve 2.5. Se
// digitar "nan", a intensidade continua como estava.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace editor::inspector_model {

// Secoes de componente com expansor (cabecalho clicavel). O cabecalho do objeto (nome, ativo, travado) nao tem.
// PostFx e o subgrupo "Post-processing (this camera)" dentro da Camera.
enum class Section : std::uint8_t { Transform, MeshRenderer, Light, GlobalLight, Camera, Audio, PostFx, Tags };
inline constexpr std::size_t kSectionCount = 8;

// Estado de aberto e fechado de cada secao (como o DefaultOpen do ImGui).
struct Expanders {
    std::array<bool, kSectionCount> open{};

    // Tudo aberto, menos o pos-processamento (fechado no inicio, como o TreeNode do ImGui).
    Expanders() {
        open.fill(true);
        open[static_cast<std::size_t>(Section::PostFx)] = false;
    }

    // Ler o estado nao muda nada. So Toggle e SetAll mudam.
    [[nodiscard]] bool IsOpen(Section s) const noexcept { return open[static_cast<std::size_t>(s)]; }
    void Toggle(Section s) noexcept { open[static_cast<std::size_t>(s)] = !open[static_cast<std::size_t>(s)]; }
    void SetAll(bool isOpen) noexcept { open.fill(isOpen); }
};

// Limites de um slider (ou de um numero prendido). Os valores vem da janela ImGui que existia antes.
struct Range {
    float min;
    float max;
};

// Valor vindo de um slider ou de um campo numerico. NaN e infinito sao recusados: devolve false e out fica como
// estava. Valor finito e prendido em [min, max].
[[nodiscard]] inline bool AcceptInRange(float candidate, Range range, float& out) noexcept {
    if (!std::isfinite(candidate)) return false;
    out = std::clamp(candidate, range.min, range.max);
    return true;
}

// Texto digitado num campo numerico. Aceita ponto ou virgula como decimal e espacos nas pontas. Recusa vazio,
// letras, notacao hexadecimal, "nan" e "inf". Sem sucesso, out nao muda.
[[nodiscard]] inline bool ParseNumber(std::string_view text, float& out) {
    std::string s;
    for (char c : text) {
        if (c == ' ' || c == '\t') continue;
        s.push_back(c == ',' ? '.' : c);
    }
    if (s.empty()) return false;
    for (char c : s) {
        if (c == 'x' || c == 'X') return false;   // 0x10 seria aceito pelo strtod; aqui e so decimal
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

// Numero digitado e prendido no intervalo. Mesmas recusas de ParseNumber.
[[nodiscard]] inline bool ParseNumberInRange(std::string_view text, Range range, float& out) {
    float v = 0.f;
    if (!ParseNumber(text, v)) return false;
    return AcceptInRange(v, range, out);
}

// Cor HEX "#RRGGBB" ou "RRGGBB" (maiusculas ou minusculas, espacos nas pontas). Cor invalida nao muda rgb: a cor
// atual fica. Retorna true quando rgb foi escrito.
[[nodiscard]] inline bool ApplyHexColor(std::string_view text, float rgb[3]) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6) return false;
    unsigned value = 0;
    for (char c : text) {
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        value = value * 16u + digit;
    }
    rgb[0] = static_cast<float>((value >> 16) & 0xFFu) / 255.f;
    rgb[1] = static_cast<float>((value >> 8) & 0xFFu) / 255.f;
    rgb[2] = static_cast<float>(value & 0xFFu) / 255.f;
    return true;
}

// Texto HEX de uma cor de 0 a 1: "#RRGGBB" em maiusculas. Valores fora da faixa sao prendidos.
[[nodiscard]] inline std::string HexText(const float rgb[3]) {
    char buf[12];
    const auto channel = [](float c) {
        return static_cast<unsigned>(std::lround(std::clamp(c, 0.f, 1.f) * 255.f));
    };
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
    return buf;
}

// Numero para exibir, com casas fixas (o campo nunca mostra mais casas do que a regra pede).
[[nodiscard]] inline std::string FormatNumber(float value, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", std::clamp(decimals, 0, 6), static_cast<double>(value));
    return buf;
}

// Objeto travado: o resto do Inspector so mostra. O checkbox "Locked" continua funcionando (regra do ImGui).
[[nodiscard]] inline bool EditAllowed(bool locked) noexcept {
    return !locked;
}

} // namespace editor::inspector_model
