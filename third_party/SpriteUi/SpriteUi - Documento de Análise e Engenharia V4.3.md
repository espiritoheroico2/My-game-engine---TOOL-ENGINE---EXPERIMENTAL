# **SpriteUi & Light Prism: Arquitetura, Engenharia e Guia Didático Consolidado (V4.3)**

Este documento descreve a arquitetura planejada para o subsistema **SpriteUi** e inclui explicações conceituais, diagramas e exemplos. Partes do plano original usam Flecs e acesso direto ao Diligent; isso não corresponde à arquitetura da ToolEngine, que possui ECS e RHI próprios. Para a integração com a ToolEngine, usar os adaptadores desse host e não adicionar Flecs, uma segunda janela SDL ou um backend Diligent paralelo. Consulte a seção 8 para o estado do código e das verificações locais; exemplos de arquitetura não devem ser interpretados como funcionalidades já entregues.

## **Visão Geral para Iniciantes: O que é a SpriteUi?**

Pense na SpriteUi como o painel de instrumentos de um carro de corrida de alta performance. Em motores de jogos tradicionais, desenhar elementos de interface (como botões, menus e barras de vida) pode gerar gargalos consideráveis na GPU devido a excessivas trocas de estado (os chamados *Draw Calls*). A SpriteUi foi projetada sob a filosofia de *Data-Oriented Design* (DOD) para organizar todos os dados de interface na memória RAM de forma contígua e perfeitamente alinhada. Dessa forma, a placa de vídeo (GPU) pode processar milhares de elementos visuais de uma só vez, em uma única instrução de desenho.

# **1\. Contratos Base e ABI de GPU (O Elo CPU-GPU)**

Para garantir o processamento num único *Draw Call* maciço sem corromper a memória, a estrutura de instância foi rigorosamente bloqueada em 80 bytes.

## **Conceito: O que é a ABI de GPU e por que 80 Bytes?**

Analogia: Pense na memória da GPU como uma esteira rolante automatizada que transporta caixas padronizadas de carga. Se cada caixa tiver um tamanho diferente, a esteira precisa parar constantemente para reajustar seus braços mecânicos. Se todas as caixas tiverem exatamente o mesmo tamanho (80 bytes) e estiverem alinhadas em intervalos regulares (16 bytes), a esteira funciona na velocidade máxima teórica sem interrupções.  
Abaixo está o diagrama representativo de como o layout de 80 bytes do SpriteInstanceGPU é distribuído no buffer de memória da GPU:

```
+---------------------------------------------------------------------------------+  Offset 0 (16 bytes)
| transformRow0 (float4: m00, m01, tx, depth)                                     |
+---------------------------------------------------------------------------------+  Offset 16 (16 bytes)
| transformRow1 (float4: m10, m11, ty, padding)                                   |
+---------------------------------------------------------------------------------+  Offset 32 (16 bytes)
| uvRect (float4: u, v, width, height)                                            |
+---------------------------------------------------------------------------------+  Offset 48 (16 bytes)
| tint (float4: R, G, B, A linear premultiplicado)                                |
+---------------------------------------------------------------------------------+  Offset 64 (16 bytes)
| resourceIds (uint4: textureSlice, materialIndex, clipIndex, flags)              |
+---------------------------------------------------------------------------------+  Total = 80 Bytes
```

## **1.1. Estrutura C++ (SpriteInstanceGPU)**

```
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ToolEngine::SpriteUi::Rendering {

// Estrutura contendo dados de instância por sprite enviados diretamente para o VBO da GPU.
// O alinhamento de 16 bytes garante compatibilidade imediata com registradores SIMD / Vector da GPU.
struct alignas(16) SpriteInstanceGPU final {
    float transformRow0[4]; // Linha 0 da matriz 2x3 de transformação 2D (m00, m01, translacao_x, profundidade_z)
    float transformRow1[4]; // Linha 1 da matriz 2x3 de transformação 2D (m10, m11, translacao_y, padding_alinhamento)
    float uvRect[4];        // Coordenadas de textura normalizadas na atlas: u_min, v_min, largura_uv, altura_uv
    float tint[4];          // Cor de tonalidade RGBA em espaço linear com alfa pré-multiplicado
    std::uint32_t resourceIds[4]; // Índices de recursos na GPU: [0]=fatia_textura, [1]=indice_material, [2]=indice_recorte, [3]=flags_estado
};

// Validações estáticas de compilação para assegurar conformidade rígida da ABI em todas as plataformas C++20
static_assert(std::is_standard_layout_v<SpriteInstanceGPU>, "SpriteInstanceGPU precisa ter layout padronizado C!");
static_assert(std::is_trivially_copyable_v<SpriteInstanceGPU>, "SpriteInstanceGPU deve permitir memcpy direto!");
static_assert(sizeof(SpriteInstanceGPU) == 80, "Tamanho da estrutura deve ser exatamente 80 bytes!");
static_assert(alignof(SpriteInstanceGPU) == 16, "Alinhamento em memória deve ser estritamente de 16 bytes!");

// Estrutura de dados de material para shading avançado de UI (PBR 2D / Iluminação por Clustered Shading)
struct alignas(16) UIMaterialGPU {
    std::uint32_t textureIndicesAndFlags[4]; // Índices de texturas no bindless array: albedo, normal, mascara, flags_material
    float emissiveColorIntensity[4];         // Cor de emissão de luz própria RGB e fator de intensidade em W
    float lightingParams[4];                 // Parâmetros de superfície: escala_normal, rugosidade, metalicidade, reservado
};
}
```

## **1.2. Integração Modular de Iluminação**

```
enum class UICanvasSpace : std::uint8_t { Screen, World };
enum class UIShadingMode : std::uint8_t { Unlit, ClusterLit };

struct UICanvasRenderConfig {
    UICanvasSpace space = UICanvasSpace::Screen;
    UIShadingMode shading = UIShadingMode::Unlit;
    std::uint32_t lightLayerMask = 0xFFFFFFFFu;
    bool castsWorldLight = false; 
};
```

## **Fluxograma Textual: O Pipeline de Render Extraction (CPU \-\> GPU)**

O diagrama a seguir descreve a jornada dos dados desde as entidades do ECS (Flecs) até a submissão de comandos na GPU via Diligent Engine:

```
[ Árvore ECS (Flecs) ]
         |
         v (Passo 1: Traversal & Invalidação)
[ Layout Node Systems ] --> Aplica regras Flexbox (Measure/Arrange)
         |
         v (Passo 2: Render Extraction Pass)
[ DrawList Builder ] --> Copia entidades visíveis para buffers contíguos POD
         |
         v (Passo 3: Radix Sorting por Z-Order e Material)
[ Instancing Buffer ] --> Mapeia SpriteInstanceGPU[80 bytes] direto para VBO
         |
         v (Passo Passo 4: Submissão de Command List)
[ Diligent RHI Engine ] --> Emite 1 Draw Call Instanciado na GPU
```

# **2\. Roteamento de Input, Foco e Captura**

## **2.1. O Roteador Central**

p  
struct UIInputSnapshot {  
vec2 pointerPositionLogical{};  
vec2 pointerDeltaLogical{};  
vec2 wheelDelta{};  
std::uint32\_t pointerId \= 0;  
bool primaryDown \= false;  
bool primaryPressed \= false;  
bool primaryReleased \= false;  
bool windowFocused \= true;  
std::uint64\_t frameId \= 0;  
};

## **Como funciona o Roteamento de Input sob o Capô**

O roteador de entrada lida com a captura e a propagação de eventos de ponteiro (mouse/touch) através da hierarquia visual. Quando o usuário clica na tela:

* **1\. Hit Testing AABB:** O sistema percorre a árvore visual em ordem inversa (da frente para trás) verificando se a posição do ponteiro está dentro da caixa delimitadora (AABB) de cada elemento.  
* **2\. Captura de Entidade:** Se uma entidade responder ao clique (ex: pressionar o botão do mouse), ela adquire a flag de `capturedEntity`. Todos os eventos de arraste subsequentes irão para ela até o botão ser solto.  
* **3\. Foco Semântico:** Elementos interativos adquirem a flag `focusedEntity` para receber atalhos de teclado e entrada de controle/gamepad.

struct UIPointerRoutingState {  
flecs::entity capturedEntity \= flecs::entity::null();  
flecs::entity focusedEntity \= flecs::entity::null();  
flecs::entity hoveredEntity \= flecs::entity::null();  
bool consumedThisFrame \= false;  
};

````

## 3. Layout Flexbox e Widgets

### 3.1. Validação de Sliders e ScrollViews

```cpp
inline void ClampScroll(UIScrollViewComponent& scroll, vec2 viewportSize) noexcept {
    const float maxX = std::max(0.0f, scroll.contentSize.x - viewportSize.x);
    const float maxY = std::max(0.0f, scroll.contentSize.y - viewportSize.y);
    scroll.scrollOffset.x = scroll.allowScrollX ? std::clamp(scroll.scrollOffset.x, 0.0f, maxX) : 0.0f;
    scroll.scrollOffset.y = scroll.allowScrollY ? std::clamp(scroll.scrollOffset.y, 0.0f, maxY) : 0.0f;
}
Explicação Didática: Entendendo o Layout Flexbox e Limites de Scroll
O sistema de layout Flexbox da SpriteUi calcula automaticamente o tamanho e a posição dos elementos filhos dentro de um container pai. A função ClampScroll impede que o conteúdo rolável (ScrollView) saia dos limites visíveis (Viewport), calculando a diferença entre a dimensão total do conteúdo e o tamanho da janela de visualização.
+-------------------------------------------+  (Viewport - Tela Visível)
| Conteúdo Visível (scrollOffset = 0,0)     |
| +---------------------------------------+ |
| | Elemento A                            | |
| +---------------------------------------+ |
+-------------------------------------------+
| Conteúdo Oculto (scrollOffset > max)      |  --> Limitado via ClampScroll()
| +---------------------------------------+ |
| | Elemento B                            | |
| +---------------------------------------+ |
+-------------------------------------------+
````

## **3.2. Navegação Semântica (Gamepad/Teclado)**

Substituição de bloqueios persistentes por navegação vetorial (cones de 90°) disparando eventos semânticos (Activate e Cancel).

# **4\. Tipografia Avançada: HarfBuzz e MSDF**

```
struct GlyphInstance {
    std::uint32_t glyphId = 0;
    std::uint32_t clusterByteOffset = 0; 
    float xAdvance = 0.0f;
    float yAdvance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    float uvRect[4]{};
    float planeBounds[4]{}; 
    std::uint16_t atlasPage = 0;
};
```

No Pixel Shader, a amostragem MSDF combina `fwidth` e `pxRange` dinâmico enviado via cbuffer (`FontMetrics::pxRange`). O shader consulta as dimensões do atlas e converte o intervalo de texels para pixels de tela antes de calcular a cobertura.

## **Conceito: O que é MSDF (Multi-channel Signed Distance Fields)?**

Fontes vetoriais tradicionais perdem nitidez ou exigem a regeneração contínua de atlas quando ampliadas em resoluções como 4K ou 8K. O MSDF resolve isso armazenando os contornos das letras como campos de distância vetoriais nos canais de cor RGB da textura. O shader inspeciona as distâncias e desenha bordas incrivelmente nítidas em qualquer nível de zoom sem consumir memória adicional.  
Visualização ASCII do Pixel Shader MSDF:

```
Textura Atlas MSDF (Canais R, G, B) --> [ Amostragem de Distância ]
                                                  |
                                                  v
                                  [ Cálculo de Median(R, G, B) ]
                                                  |
                                                  v
                                  [ fwidth() + pxRange Ajuste ]
                                                  |
                                                  v
                                [ Borda Antialiased Perfeita ]
```

## **Código HLSL: Pixel Shader de Renderização MSDF (SpriteUI.hlsl)**

```
// Trecho ilustrativo do cálculo usado em shaders/SpriteUI.hlsl
Texture2DArray<float4> g_FontAtlas : register(t0);
SamplerState           g_LinearSampler : register(s0);

cbuffer FontMetricsBuffer : register(b0) {
    float g_pxRange; // Intervalo de distância em pixels na atlas MSDF
};

float Median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float4 mainPS(float3 atlasCoord : TEXCOORD1, float4 tint : COLOR0) : SV_Target {
    // Amostra os três canais de distância da textura de fontes
    float3 msd = g_FontAtlas.Sample(g_LinearSampler, atlasCoord).rgb;
    float sd = Median(msd.r, msd.g, msd.b) - 0.5f;
    uint atlasWidth, atlasHeight, arraySize, mipLevels;
    g_FontAtlas.GetDimensions(0, atlasWidth, atlasHeight, arraySize, mipLevels);
    float2 unitRange = g_pxRange / float2(atlasWidth, atlasHeight);
    float2 screenTexSize = rcp(max(fwidth(atlasCoord.xy), float2(1e-5f, 1e-5f)));
    float screenPxRange = max(0.5f * dot(unitRange, screenTexSize), 1.0f);
    float opacity = saturate(screenPxRange * sd + 0.5f);
    return tint * opacity; // tint is premultiplied linear RGBA
}
```

O exemplo resume a conversão usada pelo shader do projeto; `pxRange` está em texels do atlas, enquanto `fwidth` mede UV por pixel de tela. Portanto, o intervalo precisa ser normalizado pelas dimensões da textura antes de combiná-los.

# **5\. Motor de Animação: ToonMotion V2**

Substituição de std::function por componentes PODs contíguos com políticas Override, Queue e Ignore.p  
inline vec2 EvaluateShake(float elapsed, float duration, vec2 amplitude, float frequency \= 38.0f) noexcept {

```
if (duration <= 0.0f || frequency <= 0.0f) return {0.0f, 0.0f}
```

;  
\|---\|std::clamp(elapsed / duration, 0.0f, 1.0f);  
const float envelope \= (1.0f \- t) \* (1.0f \- t);  
constexpr float phase \= 1.61803398875f;  
return {  
amplitude.x \* envelope \* std::sin(elapsed \* frequency),  
amplitude.y \* envelope \* std::sin(elapsed \* frequency \* phase)  
};  
}

````

## 6. Otimização e Renderização On-Demand

```cpp
enum UIDirtyBits : std::uint32_t {
    DirtyNone       = 0,
    DirtyMeasure    = 1u << 0,
    DirtyArrange    = 1u << 1,
    DirtyTransform  = 1u << 2,
    DirtyHitTargets = 1u << 3,
    DirtyTextShape  = 1u << 4,
    DirtyTextAtlas  = 1u << 5,
    DirtyDrawData   = 1u << 6,
    DirtyBlur       = 1u << 7,
};
Explicação: Sistema de Invalidação On-Demand (Dirty Flags)
Para garantir zero uso desnecessário de CPU e GPU quando a interface está estática (ociosa), a SpriteUi adota o padrão de invalidação por bitmask. Se o usuário mover apenas um slider, apenas a flag de DrawData e Transform são marcadas como 'sujas' (dirty), evitando recalcular todo o layout Flexbox ou reconstruir a hierarquia de texto.
````

# **7\. Persistência Segura (JSON)**

* Validação rígida de `schemaVersion`.  
* Sanitização de valores via `ReadFiniteFloat`.  
* Escrita transacional pós-validação completa.

# **8\. Estado da Implementação (SpriteUi 0.6.0, verificado em 09/11/2026)**

O CMake declara a versão 0.6.0. O estado abaixo descreve o código presente; não significa que exista uma release publicada ou uma GUI completa com janela e GPU.

## **Módulos implementados no core**

* ABI `SpriteInstanceGPU` de 80 bytes, canvas e contratos de transformação.
* Layout com grow/shrink e limites, `justifyContent`, wrap, `alignContent` e baseline; árvore e bridge para cena.
* Render extraction, ordenação visual, clipping, batches adjacentes, validação de `FramePacket`, Nine-Slice e SDF arredondado.
* Roteamento de ponteiro primário, foco, captura, bubbling, navegação espacial e eventos semânticos.
* Lógica de widgets, virtual list, docking/abas, gráficos e popups.
* Layout geométrico de texto, quebra por palavra no subconjunto implementado, caret e mapeamento de clique para inserção.
* ToonMotion, bindings reativos, dirty flags e configuração JSON estrita/transacional.
* Shader HLSL de referência, compilado para DXIL nesta máquina.

## **Etapas pendentes ou parciais**

| Área | Estado |
|---|---|
| Backend de produção | `NullRenderBackend` existe; Diligent, GPU real, buffers/ring e fences pendentes. |
| ECS e plataforma | Adaptador Flecs pendente; target SDL3 opcional com wrapper RAII de janela, mouse/foco, navegação por teclado/gamepad, texto/composição IME e clipboard implementado e validado. GLFW e multi-pointer pendentes. |
| Widgets | Núcleo lógico implementado; camada visual integrada e drag-and-drop de docking pendentes. |
| Tipografia | Layout de glifos shaped implementado; HarfBuzz/FreeType, atlas MSDF e bidi/RTL não integrados. |
| Animação | ToonMotion básico implementado; timeline/keyframes e integração ECS pendentes. |
| Iluminação e efeitos | Light Prism/ClusterLit, normal mapping, Acrylic Blur, Bloom e sombras pendentes. |
| Recursos e scripting | KTX2/Basis/streaming, partículas, nodes de editor e QuickJS/FFI sandbox pendentes. |
| Produção e distribuição | CI multiplataforma e teste consumidor `find_package` configurados; job remoto ainda não confirmado. Cobertura, fuzz contínuo e validação GPU pendentes. |

## **Validação executada**

Em Windows com MSVC: builds Debug/Release, alvo `spriteui_check_headers`, CTest e ambos os exemplos CLI passaram. `spriteui_tests.exe` reportou **727 checks**. O adaptador opcional também compilou com SDL 3.2.30 e passou pelo CTest (core + pacote consumidor). DXC compilou os shaders VS e PS para DXIL. Não houve teste em GPU real, nem validação Clang/GCC, sanitizers ou CI remoto; não declarar esses itens como verificados por esta rodada.

# **9\. Registro de Revisão (V4.2 → V4.3)**

## **Bugs Corrigidos**

* **Layout:** Resolução de percentuais contra a área interna do pai.  
* **Layout:** Stretch restrito a Auto/Flex e min/max preservados no grow/shrink.  
* **Texto:** Inclusão do parâmetro `owner` obrigatório em `GenerateTextRenderItems`.  
* **LayoutBridge:** Roteamento de `HitTarget::parent` para o ancestral interagível mais próximo.  
* **Docking:** Proteção da subárvore em `Split` e preservação da aba ativa.  
* **Bindings:** Flag `initialized` eliminando falso disparo na 1ª leitura.  
* **VirtualList:** `clamp` antes do cast de índice flutuante para `size_t`.  
* **Navegação:** `Activate` adquire foco antes da ativação.  
* **UIConfig:** Parser estrito com escapes, suporte UTF-8 e escrita transacional.  
* **Sanitização:** Verificação `IsFinite` e checagem de NaN.

# **10\. Histórico de Versões e Integração ImGui**

Substituição total do ImGui pela SpriteUi Retained (ECS \+ DOD), unificando renderização vetorial SDF, fontes MSDF 4K/8K, pipeline PBR 2D e suporte a Clustered Shading com zero queima de GPU em estado ocioso.

## **Roadmap de Migração: Integração gradual da SpriteUi**

A substituição do ImGui é um objetivo de produto, não uma capacidade já entregue. O core SpriteUI não depende de ImGui. Na ToolEngine, a migração deve preservar o editor funcional durante o trabalho: usar o ECS, a janela/input SDL3 e o RHI já existentes, migrar uma tela por vez e manter o ImGui somente como fallback das telas ainda não substituídas. A primeira tela planejada é o Console. A dependência do ImGui só deve ser removida do host quando nenhuma tela a utilizar e os testes de paridade/regressão estiverem aprovados.

* **Fase 1 — Core retido e ABI (implementada no core):** tipos, layout, árvore, scene bridge, extração e ABI verificada; não há conector Flecs.
* **Fase 2 — Input e widgets lógicos (implementada no core):** roteador, foco/captura, navegação e lógica de widgets; falta camada de janela/OS e kit visual completo.
* **Fase 3 — Texto e MSDF (parcial):** layout, quebra, caret e shader compilado; faltam HarfBuzz/FreeType, atlas dinâmico e validação de fontes reais/GPU.
* **Fase 4 — Backend e plataforma (parcial):** target SDL3 opcional para hosts independentes; na ToolEngine, reutilizar `tool::IWindow` e o event loop existente. O adaptador para o ECS/RHI próprios da ToolEngine, GLFW e input multi-pointer permanecem pendentes. O RHI atual precisa expor caminhos compatíveis para instâncias em vertex shader, texturas-array/samplers e offsets de instância (ou deve ser acordada outra estratégia de backend).
* **Fase 5 — Iluminação e efeitos (pendente):** integração Light Prism, ClusterLit, normal mapping, blur, bloom e sombras.
* **Fase 6 — Produção e migração de aplicação (pendente):** CI, benchmarks, validação multiplataforma/GPU e pacote de consumidor; integrar primeiro o backend ao RHI/input da ToolEngine, migrar o Console e depois as demais telas individualmente, validar paridade e só então remover código, targets e dependências de ImGui do editor.