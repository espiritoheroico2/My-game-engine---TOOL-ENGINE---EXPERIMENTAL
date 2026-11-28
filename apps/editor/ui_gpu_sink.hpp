#pragma once

// Interface entre a ponte da SpriteUI (spriteui_host.hpp) e o desenho da interface pelo Diligent (ui_gpu_backend.hpp).
// So tipos da biblioteca padrao e do SDL: quem usa a interface (paineis, ViewTex, ImageCache) nao precisa conhecer o RHI.
//
// Modelo: a UI toda do quadro e gravada na CPU (vertices + lista de desenhos) e enviada de uma vez no fim do quadro
// (UiGpuSink::flush, chamado pelo main). Texturas:
//  - "slices" fixos da SpriteUI (glifos, icones, miniaturas de material, texto por tamanho): setSlice;
//  - texturas dinamicas (vistas Scene/Game e imagens do Canvas): createDynamic/updateDynamic/destroyDynamic, id 0 = nenhuma.
// Cada backend novo tem uma "geracao" nova: quem guarda ids dinamicos (ViewTex, ImageCache) compara a geracao e recria.
#include <SDL3/SDL.h>

#include <cstdint>

namespace ToolEngine::SpriteUi::Rendering {
struct FramePacket;
struct ClipRectGPU;
}  // namespace ToolEngine::SpriteUi::Rendering

namespace editor::sui {

// Imagem de uma vista (Scene/Game): ou um SDL_Texture (caminho SDL) ou um id dinamico do backend GPU. `linear` escolhe
// o filtro (a opcao "pixel filter" usa vizinho mais proximo).
struct ViewImage {
  SDL_Texture* sdl = nullptr;
  std::uint32_t gpu = 0;
  bool linear = true;
  explicit operator bool() const { return sdl != nullptr || gpu != 0; }
};

// Vertice de geometria avulsa (triangulos do gizmo e das particulas, imagens do Canvas). Como o SDL_Vertex: posicao em
// pixels logicos da janela, cor RETA em sRGB (0..1) com alfa, UV normalizado (0..1) da textura dinamica.
struct UiVertex {
  float x, y, u, v;
  float r, g, b, a;
};

class UiGpuSink {
 public:
  virtual ~UiGpuSink() = default;

  // Geracao deste backend (nunca repete dentro do processo).
  [[nodiscard]] virtual std::uint32_t generation() const = 0;

  // Cria ou substitui o slice `slice` (RGBA8, bytes crus). `linear` = filtro bilinear (glifos, icones, materiais).
  virtual bool setSlice(std::uint32_t slice, int w, int h, const std::uint8_t* rgba, bool linear) = 0;
  [[nodiscard]] virtual bool hasSlice(std::uint32_t slice) const = 0;

  // Textura dinamica (RGBA8). Devolve um id (> 0) ou 0 em falha. `update` troca o conteudo inteiro (w*h*4 bytes).
  virtual std::uint32_t createDynamic(int w, int h, const std::uint8_t* rgba) = 0;
  virtual void updateDynamic(std::uint32_t id, const std::uint8_t* rgba) = 0;
  virtual void destroyDynamic(std::uint32_t id) = 0;
  // Tamanho de uma textura dinamica (false se o id nao existe).
  virtual bool dynamicSize(std::uint32_t id, int& w, int& h) const = 0;

  // Liga um slot de vista (0..kViewSlots-1) a uma textura dinamica; id 0 solta o slot.
  virtual void bindView(std::uint32_t slot, std::uint32_t id, bool linear) = 0;

  // Grava um FramePacket da SpriteUI no quadro (ordem de pintura preservada).
  virtual bool drawPacket(const ToolEngine::SpriteUi::Rendering::FramePacket& packet) = 0;
  // Grava geometria avulsa (triangulos; indices 0 = lista sequencial de triangulos). `texture` = id dinamico (0 = branco).
  // `clip` nulo = sem recorte.
  virtual bool drawGeometry(const UiVertex* vertices, int vertexCount, const int* indices, int indexCount, std::uint32_t texture,
                            bool linear, const ToolEngine::SpriteUi::Rendering::ClipRectGPU* clip) = 0;
};

// Backend GPU ativo (nulo = caminho SDL). Quem anexa o backend (main.cpp) define; ViewTex e ImageCache consultam.
inline UiGpuSink*& ActiveUiGpu() {
  static UiGpuSink* sink = nullptr;
  return sink;
}

// Janela principal do editor, para os paineis pegarem tamanho e ligarem o texto de entrada nos dois modos (no modo GPU o
// SDL_Renderer das chamadas e um renderizador-fantasma sem janela).
inline SDL_Window*& UiMainWindow() {
  static SDL_Window* window = nullptr;
  return window;
}
inline SDL_Window* UiWindowOf(SDL_Renderer* renderer) {
  SDL_Window* w = renderer ? SDL_GetRenderWindow(renderer) : nullptr;
  return w ? w : UiMainWindow();
}

}  // namespace editor::sui
