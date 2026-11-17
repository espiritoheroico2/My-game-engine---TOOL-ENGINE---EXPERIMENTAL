#pragma once

// Politica de atualizacao por janela. Decide quando a janela deve ser reconstruida (layout, texto, draw data),
// separado do quadro do jogo. A janela tem uma taxa-alvo em Hz e o estado sujo de DirtyFlags.hpp (UIDirtyState).
//
// Regras:
//  - Reconstroi se a janela esta suja, ou se o intervalo da taxa passou desde a ultima reconstrucao.
//  - Nunca reconstroi duas vezes no mesmo instante.
//  - Taxa 0 (ou invalida): so reconstroi quando suja. Ex.: editor de texto parado.
//  - Sem alocacao no caminho de decisao. O tempo e recebido de fora (relogio injetado), nunca lido aqui.

#include "SpriteUi/Core/DirtyFlags.hpp"

namespace ToolEngine::SpriteUi {

class UIRefreshPolicy {
public:
    static constexpr float kDefaultRateHz = 60.0f;

    UIRefreshPolicy() noexcept = default;
    explicit UIRefreshPolicy(float rateHz) noexcept;

    // Valores nao finitos, zero ou negativos viram 0 (so quando suja).
    void SetRateHz(float rateHz) noexcept;
    [[nodiscard]] float RateHz() const noexcept { return rateHz_; }

    // Pura: nao muda o estado da politica nem o da janela.
    [[nodiscard]] bool ShouldRebuild(double nowSeconds, const UIDirtyState& dirty) const noexcept;

    // Registra uma reconstrucao feita no instante informado.
    void MarkRebuilt(double nowSeconds) noexcept;

    // Atalho: se deve reconstruir, registra a reconstrucao, limpa o estado sujo e devolve true.
    bool TryRebuild(double nowSeconds, UIDirtyState& dirty) noexcept;

private:
    float rateHz_ = kDefaultRateHz;
    double lastRebuild_ = 0.0;
    bool hasRebuilt_ = false;
};

} // namespace ToolEngine::SpriteUi
