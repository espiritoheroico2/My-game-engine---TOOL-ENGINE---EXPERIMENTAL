#pragma once

// Regras da janela Configuracoes que nao dependem de janela: abas, limites de cada controle e a conversao entre
// o indice de um combo e o valor que ele representa (resolucao do jogo, qualidade de sombra, divisao de tela).
// Nao usa SDL nem ImGui. A janela (ImGui hoje, SpriteUI depois) so chama estas funcoes, para a regra ficar em
// um lugar so. Roda no ctest como Editor.SettingsModel.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace editor::settings_model {

inline constexpr int kTabCount = 6;

// Nomes das abas, na ordem da barra (a chave e a mesma usada pela traducao, T(...)).
inline constexpr const char* kTabNames[kTabCount] = {
    "Appearance", "Settings", "Game", "Lighting", "Optimizing", "Language"};

// Limites da aba Optimizing. A escala da interface usa a mesma faixa do tema (70% a 200%);
// o cache de malhas evita crescer sem limite e o modo ocioso reduz o trabalho entre interacoes.
inline constexpr int kChunkCacheMin = 256;
inline constexpr int kChunkCacheMax = 32768;
inline constexpr float kInterfaceScaleMin = 0.7f;
inline constexpr float kInterfaceScaleMax = 2.0f;

[[nodiscard]] inline int ClampChunkCacheSize(int value) noexcept {
    return std::clamp(value, kChunkCacheMin, kChunkCacheMax);
}

[[nodiscard]] inline float ClampInterfaceScale(float value) noexcept {
    if (value != value) return 1.25f;
    return std::clamp(value, kInterfaceScaleMin, kInterfaceScaleMax);
}

// Indice de combo ou de radio, prendido em [0, count - 1]. Com count zero, devolve zero.
[[nodiscard]] inline int ClampChoice(int index, int count) noexcept {
    if (count <= 0) return 0;
    return std::clamp(index, 0, count - 1);
}

// Escala padrao quando o usuario nao mexeu: o renderizador por software usa metade da resolucao da janela.
[[nodiscard]] inline float DefaultRenderScale(bool softwareApi) noexcept {
    return softwareApi ? 0.5f : 1.0f;
}

// Modo "Game resolution": 0 acompanha a janela; 1 a 4 sao tamanhos fixos; 5 e personalizado.
inline constexpr int kResolutionCustom = 5;
inline constexpr int kFixedSizeCount = 4;
inline constexpr int kFixedSizes[kFixedSizeCount][2] = {{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440}};

// Tamanho personalizado prendido em 160 a 3840 (largura) e 90 a 2160 (altura).
inline constexpr int kCustomWidthMin = 160;
inline constexpr int kCustomWidthMax = 3840;
inline constexpr int kCustomHeightMin = 90;
inline constexpr int kCustomHeightMax = 2160;

// Tamanho fixo escolhido. Devolve false quando o modo acompanha a janela (nesse caso w e h nao mudam).
[[nodiscard]] inline bool FixedGameSize(int mode, int customWidth, int customHeight, int& width, int& height) noexcept {
    if (mode >= 1 && mode <= kFixedSizeCount) {
        width = kFixedSizes[mode - 1][0];
        height = kFixedSizes[mode - 1][1];
        return true;
    }
    if (mode == kResolutionCustom) {
        width = std::clamp(customWidth, kCustomWidthMin, kCustomWidthMax);
        height = std::clamp(customHeight, kCustomHeightMin, kCustomHeightMax);
        return true;
    }
    return false;
}

// Qualidade de sombra: 0 = 512, 1 = 1024, 2 = 2048, 3 = 4096. Resolucao fora dos degraus cai no degrau de cima.
inline constexpr int kShadowQualityCount = 4;
[[nodiscard]] inline int ShadowQualityIndex(std::uint32_t resolution) noexcept {
    if (resolution <= 512u) return 0;
    if (resolution <= 1024u) return 1;
    if (resolution <= 2048u) return 2;
    return 3;
}

// Resolucao do mapa de sombra do degrau: 512 vezes 2 elevado ao indice.
[[nodiscard]] inline std::uint32_t ShadowResolution(int index) noexcept {
    return 512u << ClampChoice(index, kShadowQualityCount);
}

// Divisao de tela: indice 0 = lado a lado (vertical), 1 = em cima e embaixo (horizontal).
[[nodiscard]] inline int SplitIndex(bool vertical) noexcept {
    return vertical ? 0 : 1;
}
[[nodiscard]] inline bool SplitIsVertical(int index) noexcept {
    return index == 0;
}

} // namespace editor::settings_model
