#pragma once

// Pedidos da janela Configuracoes em SpriteUI e a regra que grava cada um no Editor. A janela so devolve a lista de
// pedidos; ApplyChanges chama um metodo do alvo para cada um (o Editor, no main.cpp; um alvo falso, no teste).
// Sem SDL e sem SpriteUI, assim o teste roda no ctest como Editor.SettingsChanges.
//
// Exemplo: o usuario escolhe a API 1 (Vulkan) num jogo que hoje usa a API 0 (CPU). A janela devolve Change(Api, 1).
// ApplyChanges chama target.applyApi(1). Se o jogo ja usasse a API 1, nada seria chamado.

#include <cstdint>
#include <vector>

namespace editor::settings_changes {

// Campo que o usuario mexeu.
enum class Field : std::uint8_t {
    Tab, Api, ResMode, CustomW, CustomH, RenderScale, Upscale, Aa, PixelFilter, Fps,
    Shadows, ShadowQuality, ChunkCacheMax, InterfaceScale, IdleRendering, Palette,
    ButtonRadius, WindowRadius, TextScale
};

struct Change {
    Field field;
    int index = 0;      // combos, radio, aba e tamanhos personalizados
    float value = 0.f;  // escala
    bool on = false;    // caixas
};
using Requests = std::vector<Change>;

// Grava os pedidos no alvo, na ordem. O alvo tem apiRequested() e um metodo set para cada campo (ver main.cpp).
// Pedido de API igual a atual nao chama applyApi (trocar de API e caro).
template <class Target>
void ApplyChanges(Target& target, const Requests& requests) {
    for (const Change& c : requests) {
        switch (c.field) {
            case Field::Tab: target.setTab(c.index); break;
            case Field::Palette: target.setPalette(c.index); break;
            case Field::Api:
                if (c.index != target.apiRequested()) target.applyApi(c.index);
                break;
            case Field::ResMode: target.setResMode(c.index); break;
            case Field::CustomW: target.setCustomW(c.index); break;
            case Field::CustomH: target.setCustomH(c.index); break;
            case Field::RenderScale: target.setRenderScale(c.value); break;
            case Field::Upscale: target.setUpscale(c.index); break;
            case Field::Aa: target.setAa(c.index); break;
            case Field::PixelFilter: target.setPixelFilter(c.on); break;
            case Field::Fps: target.setFps(c.index); break;
            case Field::Shadows: target.setShadows(c.on); break;
            case Field::ShadowQuality: target.setShadowQuality(c.index); break;
            case Field::ChunkCacheMax: target.setChunkCacheMax(c.index); break;
            case Field::InterfaceScale: target.setInterfaceScale(c.value); break;
            case Field::IdleRendering: target.setIdleRendering(c.on); break;
            case Field::ButtonRadius: target.setButtonRadius(c.value); break;
            case Field::WindowRadius: target.setWindowRadius(c.value); break;
            case Field::TextScale: target.setTextScale(c.value); break;
        }
    }
}

} // namespace editor::settings_changes
