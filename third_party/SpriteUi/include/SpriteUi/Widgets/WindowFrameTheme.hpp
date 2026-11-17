#pragma once

// Tema unico das janelas: medidas e cores da moldura (WindowFrame) para um estilo. E o unico lugar que escolhe
// cores e tamanhos de janela. O raio dos cantos e a largura da borda vem do StylePreset (Classic, Modern ou
// Effects), entao trocar o estilo muda a forma de todas as janelas ao mesmo tempo.
//
// Para mudar a aparencia de todas as janelas, edite WindowFrameTheme.cpp. Nenhuma janela deve escolher cor ou
// medida por conta propria.
//
// Exemplo: com StyleKind::Modern, a janela "Console" e a "Caixa azul" do Inspector tem raio 8 e borda 1.

#include "SpriteUi/Widgets/StylePresets.hpp"
#include "SpriteUi/Widgets/WindowFrame.hpp"
#include "SpriteUi/Widgets/WindowFramePaint.hpp"

namespace ToolEngine::SpriteUi::Widgets {

struct WindowFrameLook {
    WindowFrameMetrics metrics{};
    WindowFrameColors colors{};
};

// Aparencia das janelas para o estilo dado. Valor invalido de StyleKind usa o estilo Modern (ver GetPreset).
[[nodiscard]] WindowFrameLook MakeWindowFrameLook(StyleKind kind) noexcept;

} // namespace ToolEngine::SpriteUi::Widgets
