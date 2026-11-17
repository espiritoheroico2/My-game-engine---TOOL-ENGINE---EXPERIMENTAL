#pragma once

// Sprite: uma imagem recortada de um atlas, com tamanho em pixels logicos e uma lista de ancoras.
// A ancora principal (anchors[0]) e o ponto em volta do qual o sprite gira e onde ele e posicionado.
// Exemplo: uma moeda de 16x16 com ancora no centro gira em volta do centro; com ancora no canto
// superior esquerdo, o canto fica no lugar ao girar 90 graus.

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Rendering/DrawList.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ToolEngine::SpriteUi::Rendering {

struct UISprite {
    std::uint32_t atlasId = WhiteTextureSlice;
    // u, v, largura, altura em coordenadas de textura (0 a 1).
    std::array<float, 4> uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    // Tamanho em pixels logicos, sem escala de DPI.
    Vec2 sizePixels{};
    // Pontos em pixels locais do sprite (origem no canto superior esquerdo). anchors[0] e a principal.
    std::vector<Vec2> anchors;
    // Alfa do sprite; valores fora de [0, 1] sao prendidos na hora de desenhar.
    float alpha = 1.0f;

    [[nodiscard]] bool IsValid() const noexcept {
        if (!IsFinite(sizePixels) || sizePixels.x <= 0.0f || sizePixels.y <= 0.0f) return false;
        if (!IsFinite(alpha)) return false;
        for (const float value : uvRect) {
            if (!IsFinite(value)) return false;
        }
        if (uvRect[2] <= 0.0f || uvRect[3] <= 0.0f) return false;
        for (const Vec2& anchor : anchors) {
            if (!IsFinite(anchor)) return false;
        }
        return true;
    }

    // Ancora principal em pixels locais. Sem ancoras, vale o centro do sprite.
    [[nodiscard]] Vec2 MainAnchor() const noexcept {
        return anchors.empty() ? sizePixels * 0.5f : anchors.front();
    }
};

// Cria o item de desenho de um sprite. A posicao e onde fica a ancora principal, e a rotacao gira em volta
// dela. Sprite invalido (tamanho, UV ou ancora nao finitos) devolve nada.
[[nodiscard]] inline std::optional<UIRenderItem> MakeSpriteItem(
    UIHandle owner,
    const UISprite& sprite,
    Vec2 anchorPosition,
    float rotationRadians = 0.0f,
    std::uint32_t clipIndex = NoClipIndex,
    float zOrder = 0.0f,
    std::uint64_t paintOrder = 0) noexcept
{
    if (!sprite.IsValid() || !IsFinite(anchorPosition) || !IsFinite(rotationRadians)) return std::nullopt;

    const Vec2 anchor = sprite.MainAnchor();
    // Com pivo normalizado, o pivo cai em resolvedPosition + pivot * tamanho. Queremos que ele caia em
    // anchorPosition, entao a posicao do retangulo e anchorPosition menos a ancora.
    const Vec2 topLeft = anchorPosition - anchor;

    UIRenderItem item;
    item.handle = owner;
    item.layout.resolvedPosition = topLeft;
    item.layout.resolvedSize = sprite.sizePixels;
    item.transform.pivot = {anchor.x / sprite.sizePixels.x, anchor.y / sprite.sizePixels.y};
    item.transform.rotationRadians = rotationRadians;
    item.uvRect[0] = sprite.uvRect[0];
    item.uvRect[1] = sprite.uvRect[1];
    item.uvRect[2] = sprite.uvRect[2];
    item.uvRect[3] = sprite.uvRect[3];
    item.textureSlice = sprite.atlasId;
    item.opacity = std::clamp(sprite.alpha, 0.0f, 1.0f);
    item.clipIndex = clipIndex;
    item.zOrder = zOrder;
    item.paintOrder = paintOrder;
    return item;
}

} // namespace ToolEngine::SpriteUi::Rendering
