#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tool::render {

// Flags de um objeto para classificar nas filas de render (opaco, transparente, emissivo, prepasso e sombra).
struct RenderQueueItem {
  bool opaque = false;
  bool transparent = false;
  bool emissive = false;
  bool prepass = false;
  bool castsShadow = false;
};

// Filas de indices por passe. Cada valor e a posicao do objeto no vetor de entrada.
struct RenderQueues {
  std::vector<std::uint32_t> opaque;
  std::vector<std::uint32_t> transparent;
  std::vector<std::uint32_t> emissive;
  std::vector<std::uint32_t> prepass;
  std::vector<std::uint32_t> shadowCasters;
};

// Preenche as filas a partir das flags. Transparente nao entra nas filas opacas; emissivo exige opaco e nao transparente.
inline void buildRenderQueues(const std::vector<RenderQueueItem>& items, RenderQueues& queues) {
  queues.opaque.clear();
  queues.transparent.clear();
  queues.emissive.clear();
  queues.prepass.clear();
  queues.shadowCasters.clear();

  for (std::size_t i = 0; i < items.size(); ++i) {
    const RenderQueueItem& item = items[i];
    const auto index = static_cast<std::uint32_t>(i);
    if (item.transparent) {
      queues.transparent.push_back(index);
    } else if (item.opaque) {
      queues.opaque.push_back(index);
      if (item.prepass) queues.prepass.push_back(index);
      if (item.castsShadow) queues.shadowCasters.push_back(index);
    }
    if (item.opaque && item.emissive && !item.transparent) queues.emissive.push_back(index);
  }
}

}  // namespace tool::render
