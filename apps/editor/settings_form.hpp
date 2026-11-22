#pragma once

// Linhas genericas das secoes de Configuracoes em SpriteUI (Lighting, Physics, Game, Optimizing, Language, Tags e
// Project). O main.cpp monta as linhas a partir do Editor (rotulo, valor, faixa e opcoes); o painel as desenha e,
// quando o usuario mexe, devolve um pedido Field::Form com o id da linha. O main.cpp grava o valor no Editor.
// Sem SDL e sem ImGui: os tipos ficam fora do painel para poderem ser usados nos testes.

#include <cstdint>
#include <string>
#include <vector>

namespace editor::settings_form {

// Tipo de linha. Heading e Gap so organizam; Note e so texto; os demais recebem interacao.
enum class RowKind : std::uint8_t {
    Heading,   // subtitulo da secao (texto em negrito)
    Note,      // texto explicativo (quebra de linha automatica)
    Slider,    // valor entre lo e hi (isInt: passo de 1). Mostra `note` ao lado, se houver
    Check,     // caixa marcada/desmarcada (on)
    Dropdown,  // combo: options, escolhida = index
    Radio,     // lista de opcoes em radio: options, escolhida = index
    Button,    // botao com `label`; cada clique gera um pedido
    Text,      // campo de texto (text); o pedido sai ao confirmar (Enter ou perder o foco)
    Gap        // espaco vertical
};

struct Row {
    RowKind kind = RowKind::Note;
    int id = 0;                        // identifica o campo para o main.cpp (ver FormId em main.cpp)
    std::string label;                 // rotulo a esquerda (Heading, Note e Button usam como texto)
    std::string note;                  // texto ao lado do controle (ex.: "3 px"), ou ajuda abaixo
    float value = 0.f;                 // Slider
    float lo = 0.f;                    // Slider: minimo
    float hi = 1.f;                    // Slider: maximo
    bool isInt = false;                // Slider: valores inteiros
    bool on = false;                   // Check
    int index = 0;                     // Dropdown, Radio: opcao escolhida
    std::vector<std::string> options;  // Dropdown, Radio
    std::string text;                  // Text: conteudo atual
    bool enabled = true;               // false = mostrado apagado e sem interacao
};

using Rows = std::vector<Row>;

} // namespace editor::settings_form
