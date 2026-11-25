#pragma once

// Campo numerico ao lado de cada deslizante: o valor digitado vira o valor do deslizante. Regra pura, sem SDL, para
// o teste cobrir. Usada pelo Inspector (inspector_panel.hpp) e pelas Configuracoes (settings_panel.hpp).
// Enter (ou sair do campo) confirma; Esc cancela. O valor confirmado e prendido no intervalo e arredondado como o
// deslizante faria ao arrastar. Texto invalido nao muda nada.

#include "settings_form.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

namespace editor::slider_edit {

// Le um numero digitado: aceita ponto ou virgula como separador decimal, sinal de mais e espacos nas pontas.
// Devolve false para texto vazio, texto que nao e so um numero, ou valor que nao e finito.
[[nodiscard]] inline bool ParseTypedNumber(std::string_view text, float& out) {
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return false;

    std::string clean(text);
    if (clean.find('.') == std::string::npos) std::replace(clean.begin(), clean.end(), ',', '.');

    float value = 0.f;
    const char* first = clean.data();
    const char* last = clean.data() + clean.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last || !std::isfinite(value)) return false;
    out = value;
    return true;
}

// Valor que o deslizante passa a ter depois de confirmar `text`. Texto invalido devolve `current` (nada muda).
// Com isInt, arredonda para inteiro. Senao, com step maior que zero, prende nos degraus a partir de `lo`. Depois
// prende em [lo, hi]; hi menor que lo devolve lo (mesma regra de settings_form::ClampSliderValue).
[[nodiscard]] inline float CommitSliderText(std::string_view text, float current, float lo, float hi, float step,
                                            bool isInt) {
    float typed = 0.f;
    if (!ParseTypedNumber(text, typed)) return current;
    if (isInt) return settings_form::ClampSliderValue(typed, lo, hi, true);
    if (hi >= lo && std::isfinite(step) && step > 0.f) typed = lo + std::round((typed - lo) / step) * step;
    return settings_form::ClampSliderValue(typed, lo, hi, false);
}

} // namespace editor::slider_edit
