# **SpriteUi & Light Prism: Arquitetura, Engenharia e Integração (V4.2 — Consolidada)**

Este documento consolida contratos de arquitetura e material histórico de planejamento. Os exemplos e as fases descritos aqui não significam, por si só, que a integração correspondente esteja implementada. Para separar intenção de entrega, a seção 14 registra o estado conferido no código e na validação local em 09/11/2026; ela deve ser atualizada quando novas etapas forem concluídas.

## **1\. Contratos Base e ABI de GPU (O Elo CPU-GPU)**

Para garantir o processamento num único *Draw Call* maciço sem corromper a memória, a estrutura de instância foi rigorosamente bloqueada em 80 bytes.

### **1.1. Estrutura C++ (SpriteInstanceGPU)**

```c
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ToolEngine::SpriteUi::Rendering {

struct alignas(16) SpriteInstanceGPU final {
    float transformRow0[4]; // m00, m01, tx, depth
    float transformRow1[4]; // m10, m11, ty, padding
    float uvRect[4];         // u, v, width, height
    float tint[4];           // RGBA linear, premultiplicado
    std::uint32_t resourceIds[4]; // textureSlice, materialIndex, clipIndex, flags
};

static_assert(std::is_standard_layout_v<SpriteInstanceGPU>);
static_assert(std::is_trivially_copyable_v<SpriteInstanceGPU>);
static_assert(sizeof(SpriteInstanceGPU) == 80);
static_assert(alignof(SpriteInstanceGPU) == 16);

// Tabela auxiliar para evitar o inchaço da instância base
struct alignas(16) UIMaterialGPU {
    std::uint32_t textureIndicesAndFlags[4]; // albedoSlice, normalSlice, maskSlice, flags
    float emissiveColorIntensity[4];         // RGB emissivo, intensidade
    float lightingParams[4];                 // normalScale, roughness, metalness, reserved
};
}
```

### **1.2. Integração Modular de Iluminação**

A injeção global de iluminação foi abandonada. A engine diferencia as interfaces de HUD (espaço de ecrã) e os painéis inseridos no mundo (espaço diegético).

```c
enum class UICanvasSpace : std::uint8_t { Screen, World };
enum class UIShadingMode : std::uint8_t { Unlit, ClusterLit };

struct UICanvasRenderConfig {
    UICanvasSpace space = UICanvasSpace::Screen;
    UIShadingMode shading = UIShadingMode::Unlit;
    std::uint32_t lightLayerMask = 0xFFFFFFFFu;
    bool castsWorldLight = false; 
};
```

## **2\. Roteamento de Input, Foco e Captura**

O sistema de eventos abandona as iterações cegas e os botões virtuais, implementando uma verdadeira matriz de colisão inversa e captura de ponteiro.

### **2.1. O Roteador Central**

A validação de cliques utiliza as caixas transformadas (evitando *bugs* com rotação e escala) e garante que o estado de consumo é reiniciado a cada novo *frame*. A roda do rato (*wheel*) opera num vetor independente.

```c
struct UIInputSnapshot {
    vec2 pointerPositionLogical{};
    vec2 pointerDeltaLogical{};
    vec2 wheelDelta{};              
    std::uint32_t pointerId = 0;    
    bool primaryDown = false;
    bool primaryPressed = false;    
    bool primaryReleased = false;   
    bool windowFocused = true;
    std::uint64_t frameId = 0;
};

struct UIPointerRoutingState {
    flecs::entity capturedEntity = flecs::entity::null();
    flecs::entity focusedEntity = flecs::entity::null();
    flecs::entity hoveredEntity = flecs::entity::null();
    bool consumedThisFrame = false;
};
```

O roteamento gera eventos semânticos (Enter, Leave, Down, Move, Up, Click, Wheel, Cancel), processados posteriormente pelo subsistema de Widgets. A captura da janela avalia corretamente todas as 8 extremidades (*ResizeEdge*) para evitar deteções falsas.

## **3\. Layout Flexbox e Widgets**

### **3.1. Validação de Sliders e ScrollViews**

O UISliderComponent armazena o valor bruto real dentro do domínio numérico, normalizando apenas para efeitos de desenho, mitigando divisões por zero e valores infinitos.  
O sistema de ScrollView aplica a deslocação unicamente aos eixos configurados e desconta a altura/largura da área visível (*viewport*) para permitir chegar ao fim do conteúdo de forma correta:

```c
inline void ClampScroll(UIScrollViewComponent& scroll, vec2 viewportSize) noexcept {
    const float maxX = std::max(0.0f, scroll.contentSize.x - viewportSize.x);
    const float maxY = std::max(0.0f, scroll.contentSize.y - viewportSize.y);
    scroll.scrollOffset.x = scroll.allowScrollX ? std::clamp(scroll.scrollOffset.x, 0.0f, maxX) : 0.0f;
    scroll.scrollOffset.y = scroll.allowScrollY ? std::clamp(scroll.scrollOffset.y, 0.0f, maxY) : 0.0f;
}
```

### **3.2. Navegação Semântica (Gamepad/Teclado)**

Os bloqueios persistentes de teclado foram substituídos. A ação direcional do utilizador localiza o alvo e, caso o utilizador ative o elemento, dispara um UISemanticEvent com a ação Activate (substituindo a função vazia do antigo Submit) ou Cancel para menus de contexto.

## **4\. Tipografia Avançada: HarfBuzz e MSDF**

O processamento de texto reconhece os aglomerados (*clusters*) de caracteres para não mutilar a edição Unicode e suporta índices limpos.

```c
struct GlyphInstance {
    std::uint32_t glyphId = 0;
    std::uint32_t clusterByteOffset = 0; // Vital para o cursor interativo e ligação HarfBuzz
    float xAdvance = 0.0f;
    float yAdvance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    float uvRect[4]{};
    float planeBounds[4]{}; 
    std::uint16_t atlasPage = 0;
};
```

No *Pixel Shader*, a amostragem MSDF usa `fwidth` e `pxRange`; este último, medido em texels do atlas, deve ser normalizado pelas dimensões da textura para calcular a distância em pixels de tela. O shader de referência foi corrigido e compilado localmente; paridade em GPU real continua pendente.

## **5\. Motor de Animação: ToonMotion V2**

O motor de interpolação passou de *callbacks* baseados em funções alocadas dinamicamente (std::function) para componentes contíguos (POD), implementando chaves de propriedade que resolvem conflitos (Override, Queue, Ignore).  
O efeito de *Shake* agora calcula as oscilações num desvio (*visualOffset*) estritamente derivado do tempo da animação e limpo após o términus, eliminando permanentemente a acumulação indesejada de posições (o chamado *drift*):

```c
inline vec2 EvaluateShake(float elapsed, float duration, vec2 amplitude, float frequency = 38.0f) noexcept {
    if (duration <= 0.0f || frequency <= 0.0f) return {0.0f, 0.0f};
    const float t = std::clamp(elapsed / duration, 0.0f, 1.0f);
    const float envelope = (1.0f - t) * (1.0f - t);
    constexpr float phase = 1.61803398875f;
    return {
        amplitude.x * envelope * std::sin(elapsed * frequency),
        amplitude.y * envelope * std::sin(elapsed * frequency * phase)
    };
}
```

As animações baseadas no canal Alpha utilizam a conversão *Straight-to-Premultiplied*, mantendo as proporções RGB na GPU sem degradar cores emitidas.

## **6\. Otimização e Renderização On-Demand**

Em vez de assumir erradamente "zero custo de GPU", a biblioteca utiliza as UIDirtyBits para recalcular exclusivamente as secções alteradas.

```c
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
```

O *Acrylic Blur* amostra e desfoca a cena unicamente da captura do quadro atual (*SceneColor*), garantindo a eliminação de ecrãs fantasma e dessincronização. Os dados processados originam o corte de bordas arredondadas e sombras SDF através do cálculo com os 4 raios devidamente normalizados (RoundedBoxSDF4).

## **7\. Persistência Segura (Configurações JSON)**

As definições e propriedades do editor e dos painéis já não assumem injeções perigosas na leitura. Utiliza-se um sistema com validação atómica:

1. **Validação de Schema:** O cabeçalho deve possuir obrigatoriamente a "schemaVersion".  
2. **Filtragem de Atributos:** Dados injetados com falhas no motor (valores ausentes ou limites NaN) são limpos através da função ReadFiniteFloat.  
3. **Migração em Estágio:** As modificações só são enviadas para o Flecs após a passagem integral da árvore de configuração (escrita transacional).

---

# **Histórico de Versões (Legado)**

# **SpriteUi & Light Prism 2D: Arquitetura, Engenharia e Integração Luminosa (V3.0)**

# **1\. Estrutura do Projeto e Arquitetura DOD**

O sistema **SpriteUi** adota estritamente **Data-Oriented Design (DOD)** sobre o **Flecs (ECS)**, projetado para interoperar de forma nativa e sem colisões com a infraestrutura de iluminação e renderização da **Light Prism**. Toda a hierarquia de tipos, structs e sistemas reside sob namespaces estritos (ToolEngine::SpriteUi::) e utiliza o prefixo padronizado UI\_ em identificadores de componentes para garantir que os pools de memória e arquétipos de iluminação não conflitem com os componentes do grafo de cena 3D.  
A arquitetura desacopla a camada de retenção lógica (CPU) da extração de comandos e envio de instâncias (GPU RHI \- Diligent Engine). O framework de UI opera tanto em espaço de tela (Screen-Space Canvas) quanto projetado no mundo (In-World Billboard), compartilhando o pipeline de iluminação da Light Prism através de buffers estruturados dedicados e passes de renderização coordenados.  
---

# **2\. Cálculos Matemáticos Otimizados (Matemática 2D e Iluminação PBR 2D)**

## **2.1 Distribuição de Espaço Flexível e Nine-Slice Dinâmico**

A resolução de layout em duas etapas (Measure e Arrange) calcula o delta de expansão (grow) e compressão (shrink) no eixo principal do container de forma linear e vetorizável:  
freeSpace \= containerMainSize \- sum(baseSizes) \- sum(gaps);  
delta\_i \= (freeSpace \>= 0\) ? (freeSpace \* grow\_i / sum(grow)) : (freeSpace \* (shrink\_i \* baseSize\_i) / sum(shrink\_j \* baseSize\_j));  
No Nine-Slice dinâmico, se o elemento for menor que a soma das bordas, fatores de atenuação kx, ky \= min(1.0, target / borderSum) previnem inversão ou distorção visual dos cantos.

## **2.2 Iluminação PBR 2D e Atenuação em Espaço de Tela**

Elementos de UI estilizados ou integrados ao mundo suportam iluminação direta via Normal Maps 2D. O vetor normal tangencial N decodificado da textura é combinado com a direção da luz pontual 2D L \= normalize(LightPos.xy \- PixelPos.xy). A atenuação física quadrática com limite suave de raio segue a formulação da Light Prism:  
Atten\_2D(d, r) \= saturate(1.0 \- (d / r)^2)^2 / (1.0 \+ (d / r\_smooth)^2)  
---

# **3\. Referências Fundamentais**

* **Diligent Engine Architecture:** Abstração de RHI de baixo overhead, PSOs imutáveis e buffers uniformes/estruturados instanciados.  
* **Light Prism Clustered Shading:** Coordenação de grade de luzes 2D/3D e constantes globais uniformes (CBuffers de iluminação compartilhada).  
* **HarfBuzz & Chlumsky MSDF:** Text-shaping complexo Unicode e geração/amostragem de campos de distância assinados multicanal.  
* **Khronos KTX2 / Basis Universal:** Transcodificação direta em GPU para BC7/ASTC em tempo de inicialização e descarte por LRU.

---

# **4\. Gerenciamento de Cálculos e Fluxo de Dados (CPU para GPU)**

O subsistema UIRenderExtraction itera sobre as tabelas densas do Flecs na fase flecs::PostUpdate. Elementos visíveis geram instâncias compactas de 96 bytes organizadas em um StructuredBuffer\<UISpriteInstanceData\> mapeado via Upload Heap da Diligent Engine.  
Em paralelo ao Clustered Shading da Light Prism, a extração de UI projeta chaves de ordenação (Sort Keys de 64 bits) que codificam Layer, Material, ID de Clip SDF e Profundidade Z. Isso viabiliza batching agressivo sem quebrar a sobreposição de janelas nem interferir nas listas de luzes clusters do espaço de mundo.  
---

# **5\. Códigos Base: Integração ECS e Sombras 2D**

Os componentes são definidos como structs alinhadas (PODs) sem métodos virtuais para viabilizar cópia direta de memória e vetorização SIMD:

```c
namespace ToolEngine::SpriteUi {
    struct alignas(16) UIRectTransform {
        vec2 offsetMin{0.0f, 0.0f};
        vec2 offsetMax{100.0f, 40.0f};
        vec2 anchorMin{0.0f, 0.0f};
        vec2 anchorMax{0.0f, 0.0f};
        vec2 pivot{0.5f, 0.5f};
        vec2 scale{1.0f, 1.0f};
        float rotation = 0.0f;
        float padding = 0.0f;
    };

    struct alignas(16) UINormalMap {
        uint64_t normalTextureHandle = 0;
        float bumpIntensity = 1.0f;
        float roughness = 0.5f;
    };

    struct alignas(16) UIShadowCaster {
        vec2 shadowOffset{4.0f, -4.0f};
        vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.6f};
        float shadowSoftness = 8.0f;
        float elevation = 1.0f;
    };
}
```

---

# **6\. Diretrizes de Engenharia e Produção (Segurança e Anti-Exploit)**

Como a UI executa scripts dinâmicos (QuickJS) e ingere texto de rede, ela atua como barreira de segurança:

* **Sandboxing QuickJS:** Módulos de I/O de disco e rede são excluídos no build. O runtime define teto de 32 MB de RAM (JS\_SetMemoryLimit), 2 MB de callstack e um interrupt handler com verificação de orçamento de tempo de frame.  
* **Blindagem de FFI:** O script manipula apenas uint64\_t Handles validados contra flecs::entity::is\_alive() e máscara de 53 bits para compatibilidade segura com números em double precision do JS.  
* **Sanitização UTF-8 e Matemática:** Textos têm limite rígido de 2048 bytes com recuo em fronteiras de codepoint UTF-8 para evitar bytes truncados. Valores de escala e coordenadas sofrem sanitização estrita via std::isfinite e std::clamp antes da gravação no buffer de vértice.

---

# **7\. Componentes Essenciais Adicionais**

* **Tipografia Vetorial com HarfBuzz:** O motor integra HarfBuzz e FreeType para segmentação Unicode, suporte a scripts complexos (árabe, devanágari) e posicionamento subpixel com atlas dinâmico MSDF.  
* **Data Binding Reativo:** Comunicação desacoplada entre regras de negócio do gameplay e widgets visuais por UIValueBinding (OneWay, TwoWay) sem alocações dinâmicas por frame.

---

# **8\. Sistema de Renderização de Partículas e Transparência**

O sistema de partículas 2D (UIParticleEmitterComponent) aloca pools contíguos de memória e executa integração física simplificada (Euler). Para coordenar a transparência com os efeitos volumétricos e feixes de luz da Light Prism:

* **Passo Forward Dedicado:** As partículas e quads de UI translúcidos são injetados após o passe opaco de iluminação com Z-Write desativado e Z-Test ativado para respeitar a oclusão do cenário 3D.  
* **Alfa Pré-multiplicado Obrigatório:** Amostragem com fórmula out.rgb \= src.rgb \+ dst.rgb \* (1.0 \- src.a) eliminando artefatos escuros em bordas aditivas iluminadas.

---

# **9\. Efeitos em Espaço de Tela (Screen-Space: Acrylic Blur e UI Bloom)**

* **Acrylic Blur (Dual-Kawase Filtrado):** Para superfícies translúcidas com efeito de vidro fosco, uma cópia do Backbuffer de cor do quadro anterior ou do frame opaco sofre downsample sucessivo de 4 estágios com kernel Dual-Kawase e ruído analógico pontual para evitar banding visual.  
* **UI Bloom Pass:** Elementos decorativos com canal de emissão acima de 1.0 são extraídos para o buffer de Bloom compartilhado da Light Prism, unificando a pós-produção óptica da cena e da interface.

---

# **10\. Escalabilidade e Otimização**

* **Virtualização de Listas e Tabelas (DataGrid):** O cálculo \[first, last\] \= range(scrollOffset, viewportExtent, overscan) recicla entidades ativas do Flecs, mantendo custo constante de CPU/GPU para coleções com centenas de milhares de linhas.  
* **Garbage Collection de VRAM (KTX2 / Basis Universal):** Texturas decodificadas em BC7/ASTC possuem contadores de referência (RefCounts). Texturas desvinculadas por mais de 30 segundos são desalocadas da memória da GPU via LRU periódica a cada 5 segundos.

---

# **11\. Sombras Dinâmicas Avançadas: 2D Point Lights e UI Emissiva (Neon)**

A engine estende fontes pontuais 2D para a interface gráfica. Um painel ou botão com UIEmissiveComponent registra uma Point Light dinâmica de raio curto na grade de luzes da Light Prism. O Shader de UI calcula a projeção de penumbra dinâmica ocluindo cantos adjacentes e projetando iluminação nas bordas de janelas adjacentes.  
---

# **12\. "Fake Shadows": Upscaling Geométrico e Vetorial via SDF**

Para evitar o custo prohibitivo de mapas de sombras (Shadow Maps) em milhares de retângulos de UI, adota-se o cálculo analítico de **Fake Shadows por SDF**. A distância com sinal para uma caixa com cantos arredondados (Rounded Box SDF) é avaliada no Pixel Shader:  
float BoxSDF(float2 p, float2 b, float r) { float2 d \= abs(p) \- b \+ r; return min(max(d.x, d.y), 0.0) \+ length(max(d, 0.0)) \- r; }  
O cálculo de penumbra e dispersão gaussiana simplificada é obtido diretamente por smoothstep(softness, \-softness, dist), gerando bordas perfeitas e sombras de elevação com zero impacto na largura de banda da VRAM.

* # **13\. Core Gráfico e Substituição Definitiva do ImGui**

O objetivo central da **SpriteUi** não é apenas renderizar jogos, mas **substituir integralmente o ImGui** como a interface gráfica oficial da própria ToolEngine. O ImGui tradicional, embora excelente para debug, apresenta limitações de estilização e renderização de texto não-HD (bitmap básico). A SpriteUi funde a ergonomia de uma API Declarativa/Imediata com a performance extrema de um backend Retained (ECS).  

**Decisão de integração e estado:** SpriteUI não depende de ImGui. A substituição no editor da ToolEngine ainda não foi implementada. Para reduzir risco, o editor será migrado tela por tela, começando pelo Console; ImGui pode permanecer temporariamente apenas nas telas ainda não migradas. A integração deve reutilizar as abstrações de ECS, janela/input SDL3 e RHI da engine, sem adicionar uma segunda janela/event loop ou exigir Flecs. A dependência do ImGui só será removida do host após todas as telas migrarem e testes de paridade/regressão passarem. Isso é uma meta, não uma capacidade entregue.

**Limite técnico do host:** os exemplos desta especificação que usam Flecs ou submetem diretamente ao Diligent são históricos/ilustrativos e não são o plano de integração da ToolEngine. Ela mantém um ECS próprio e um `tool::rhi::IDevice`; a SpriteUI deve produzir `FramePacket` e um adaptador futuro deve consumi-lo pelo RHI da engine. O RHI consultado ainda não expõe todas as capacidades diretamente requeridas pelo shader atual (buffer de instâncias no vertex shader, `Texture2DArray`/sampler e base-instance). Resolver essa diferença no contrato do RHI ou escolher uma estratégia de shader suportada é um pré-requisito, não algo a esconder em um backend paralelo.

A arquitetura-alvo prevê widgets de produção; o código atual implementa componentes e lógica parcial, não o conjunto visual completo:

* **Janelas e Layout:** Janelas arrastáveis (Draggable Windows), painéis redimensionáveis via Nine-Slice, Grids responsivos, abas de navegação (Tabs) e listas virtualizadas de alta densidade (DataGrid).  
* **Interação e Controles:** Botões com transição de cor nativa, barras deslizantes (Sliders), menus suspensos (Dropdowns), ícones clicáveis e Toggle Switches.  
* **Entrada de Dados (Text Input):** Caixas de texto editáveis com suporte a cursor, seleção, atalhos de teclado e clipboard, processados nativamente pelo OS.  
* **Tipografia AAA:** Diferente do ImGui, a integração HarfBuzz \+ MSDF garante textos perfeitos e antialiasing analítico (outlines, glow, drop shadow) independente do fator de escala da interface (suporte nativo a telas 4K/8K).  
* **Renderização:** A extração do ECS compila os dados em blocos instanciados idênticos ao paradigma de ImDrawList, mas executados em um único Draw Call maciço por camada de material na GPU.

---

# **14\. Gerenciamento de Texturas, Canais de Cor e Alpha**

O tratamento de imagens cruas garante a fidelidade visual e a ausência de artefatos.

* **Conversão de PNG/JPG:** Imagens importadas (PNGs com canal Alpha) são processadas no build. A SpriteUI suporta a leitura direta, mas força o uso de **Alfa Pré-Multiplicado (Premultiplied Alpha)**. Isso significa que os canais RGB são multiplicados pelo canal Alpha no carregamento.  
* **Vantagem do Alfa Pré-multiplicado:** É a única forma matemática correta de renderizar texturas translúcidas com filtros e mipmaps sem gerar bordas escuras (halos pretos) ao redor dos sprites.  
* **Canais de Cores Independentes:** Sprites podem carregar texturas de dados nos canais não utilizados (ex: Canal R \= Albedo, Canal G \= Emissão, Canal B \= Normal Map X, Canal A \= Transparência) para economizar VRAM.

---

* # **15\. Animações Nativas: Motor "ToonMotion" e Timeline**

Interfaces modernas e jogos exigem resposta cinética (Motion Design). Para evitar lógica manual de transição no C++, a SpriteUi integra o **ToonMotion**, um motor de *Tweening* determinístico embutido no ECS. O coração deste sistema é a classe modular **Animatoone**.  
A Animatoone permite enfileirar e sobrepor interpolações usando o Sprite ID (Handle do ECS) e atua diretamente nos parâmetros de transformação, especialmente ancorada nos **Pivôs** (pivot) para garantir que escalas e rotações ocorram em relação ao ponto correto do sprite (ex: base do pé, centro do botão).  
**Efeitos Pré-Fabricados (Pre-built Tweens):**

* **Shake:** Tremor paramétrico (ruído) no offset, ideal para feedbacks de erro em botões ou impacto visual.  
* **Slide & Push:** Deslizamento suave com aceleração (Easing), empurrando outros elementos do Flexbox temporariamente.  
* **Fade In / Fade Out:** Modulação do canal alfa (A) no ColorTint, preservando o estado do alfa pré-multiplicado no shader.  
* **Squash & Stretch:** Esmagamento elástico (modificação vetorial em scale compensando X e Y a partir do pivô central) que obedece os princípios clássicos de animação.  
* **MoveTo:** Translação baseada em Splines ou Bezier no espaço 2D.

A Timeline do ToonMotion permite controle absoluto: pausar, reverter, alterar a velocidade temporal global (Time Scale) e encadear *callbacks* quando uma animação atinge 100%.  
---

# **16\. Upscaling, Pixel Art e Filtros de Imagem**

A engine atende tanto a jogos modernos de alta resolução quanto a jogos retrô estilizados.

* **Suporte a Pixel Art (Point Filtering):** Para jogos estilo retrô, o amostragem da textura é configurada para SamplerState PointClamp. Isso desativa a suavização (anti-aliasing) da placa de vídeo, mantendo os pixels duros (crisp) e perfeitos, independente do nível de zoom da câmera.  
* **Bilinear/Bicubic para UI Moderna:** Texturas HD e painéis de interface usam filtragem Bilinear com geração de Mipmaps, garantindo que botões e imagens reduzam de tamanho sem serrilhados na tela.

---

# **17\. Camadas (Layers) e Z-Order entre Sprites e GUI**

O motor resolve o clássico problema de sobreposição entre elementos do jogo e a interface.

* **Sistema de Z-Sorting:** Elementos possuem um Z-Index e uma Layer ID. A CPU (RenderExtraction) ordena todos os sprites baseados na Layer (ex: Fundo \= 0, Jogo \= 1, Partículas \= 2, UI \= 3\) usando Radix Sort super rápido.  
* **Intercalação:** Sprites do mundo e Sprites de UI compartilham o mesmo renderizador, permitindo que uma janela de UI fique *atrás* de uma explosão de partículas 2D do jogo, ou vice-versa, com suporte total a Z-Buffer.

---

# **18\. Shaders Customizados, UI Bloom e Emissão**

Nem todo sprite utiliza o shader PBR padrão. A arquitetura suporta Materiais Customizados 2D.

* **Efeitos Individuais ou por Camada:** Um desenvolvedor pode aplicar um Shader de Distorção (ex: efeito de água ou calor) apenas em um Sprite, ou aplicar um Shader de Tela (ex: Scanlines CRT) em uma Layer inteira.  
* **UI Bloom e Emissão:** O canal Emissivo (UIEmissiveComponent) dita quais pixels brilham no escuro. A engine extrai esses pixels brilhantes para um render pass separado (Bloom Pass) antes de combiná-los na tela final, criando efeitos de Neon espetaculares para lasers de jogos ou botões futuristas de interface.

---

# **19\. Otimização Brutal: Batching e Instancing**

Para suportar jogos tipo "Vampire Survivors" ou UIs hiper-complexas com 50.000 sprites ativos.

* **Instanced Rendering:** Desenhar sprites um por um (1 Draw Call por sprite) destrói a performance da CPU. A SpriteUI agrupa (Batches) todos os sprites que compartilham a mesma textura e material.  
* **Submissão em Bloco:** A GPU recebe o desenho de um único quadrado genérico e um buffer com 50.000 posições, cores e cortes UV. A tela inteira é desenhada em apenas 1 Draw Call, otimizando o consumo de energia e alcançando framerates massivos.

---

# **20\. Plano de Ação, MVP e Cronograma (Refatorado)**

A arquitetura e as fases foram rearranjadas para priorizar o MVP gráfico da interface, viabilizando a substituição do ImGui logo nas primeiras etapas, seguido da integração de luzes e partículas.

* **Fase 0 — Matemática e Core de Desenho (MVP Inicial):** Implementação de matrizes 2D, conversão e leitura de texturas (PNG para KTX2), canais de cores (Alpha pré-multiplicado) e submissão básica de Sprites via Instancing.  
* **Fase 1 — Substituição do ImGui (Widgets & Texto):** Criação da API de construção de janelas, sliders, inputs de texto, drop-downs, integração MSDF (HarfBuzz) e roteamento de eventos de input (Mouse/Teclado) para os botões.  
* **Fase 2 — Motor ToonMotion:** Implementação da classe Animatoone, manipulação de Timeline, Pivôs e a biblioteca de animações pré-fabricadas (Fade, Squash, Shake).  
* **Fase 3 — Integração Light Prism:** Vinculação dos buffers estruturados PBR, Normal Mapping em Sprites, UI Emissiva (Neon) e Bloom 2D.  
* **Fase 4 — Fake Shadows e Efeitos Especiais:** Sombras analíticas projetadas via SDF e desfoque Acrílico (Acrylic Blur) no Screen-Space.  
* **Fase 5 — Sistema de Partículas 2D:** Emissores baseados em ECS para poeira, fumaça e faíscas rodando no Forward Pass.  
* **Fase 6 — Otimização e Bindings JS:** Sandboxing do QuickJS, proteção FFI anti-exploit e GC de texturas.

---

# **21\. MVP — Arquitetura de Código (Fases 0 e 1\)**

## **21.1 Definições ECS C++ (SpriteUI\_MVP.hpp)**

4. 

```c
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

namespace ToolEngine::SpriteUi {
    // ABI Padronizada para GPU (80 bytes, alinhamento 16 bytes)
    struct alignas(16) SpriteInstanceGPU {
        std::array<float, 4> transformRow0; // m00, m01, tx, depth (16 bytes)
        std::array<float, 4> transformRow1; // m10, m11, ty, padding (16 bytes)
        std::array<float, 4> uvRect;        // u, v, w, h (16 bytes)
        std::array<float, 4> tint;          // r, g, b, a (Premultiplied Linear) (16 bytes)
        std::array<uint32_t, 4> resourceIds;// textureIndex, materialIndex, clipIndex, flags (16 bytes)
    };

    static_assert(sizeof(SpriteInstanceGPU) == 80, "SpriteInstanceGPU layout deve ter exatamente 80 bytes");
    static_assert(alignof(SpriteInstanceGPU) == 16, "SpriteInstanceGPU deve ter alinhamento de 16 bytes");
    static_assert(std::is_standard_layout_v<SpriteInstanceGPU>, "SpriteInstanceGPU deve possuir standard layout");
}
```

## **21.2 Shader HLSL Compatível com Light Prism (SpriteShader.hlsli)**

5. 

```
cbuffer GlobalConstants : register(b0) {
    float4x4 ViewProjectionMatrix;
    float2 ScreenResolution;
    float Time;
    float Padding;
};

cbuffer LightPrismGlobalLights : register(b1) {
    float4 AmbientLightColor;
    float4 PointLightPosAndRadius[16];
    float4 PointLightColorAndIntensity[16];
    uint ActiveLightCount;
    float3 LightPadding;
};

Texture2D MainTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState MainSampler : register(s0);

struct VSInput {
    float3 BasePos : POSITION;
    float2 BaseUV : TEXCOORD0;
    float4x4 ModelMatrix : INSTANCE_MATRIX;
    float4 UVRect : INSTANCE_UVRECT;
    float4 ColorTint : INSTANCE_COLOR;
    float3 Pivot : INSTANCE_PIVOT;
    float Desaturation : INSTANCE_DESAT;
    float NormalIntensity : INSTANCE_NORMAL_INT;
    float Emissive : INSTANCE_EMISSIVE;
};

struct PSInput {
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 ColorTint : COLOR;
    float2 WorldPos : TEXCOORD1;
    float Desaturation : DESAT;
    float NormalIntensity : NORMAL_INT;
    float Emissive : EMISSIVE;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float3 localPos = input.BasePos - input.Pivot;
    float4 worldPos = mul(float4(localPos, 1.0f), input.ModelMatrix);
    output.Pos = mul(worldPos, ViewProjectionMatrix);
    output.WorldPos = worldPos.xy;
    output.UV = input.UVRect.xy + (input.BaseUV * input.UVRect.zw);
    output.ColorTint = input.ColorTint;
    output.Desaturation = input.Desaturation;
    output.NormalIntensity = input.NormalIntensity;
    output.Emissive = input.Emissive;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 texColor = MainTexture.Sample(MainSampler, input.UV);
    float3 normalSample = NormalTexture.Sample(MainSampler, input.UV).rgb * 2.0 - 1.0;
    float3 N = normalize(lerp(float3(0, 0, 1), normalSample, input.NormalIntensity));
    
    float3 totalDiffuse = AmbientLightColor.rgb;
    for (uint i = 0; i < ActiveLightCount; ++i) {
        float2 lightPos = PointLightPosAndRadius[i].xy;
        float radius = PointLightPosAndRadius[i].z;
        float dist = distance(lightPos, input.WorldPos);
        if (dist < radius) {
            float atten = saturate(1.0 - (dist / radius));
            atten *= atten;
            float3 L = normalize(float3(lightPos - input.WorldPos, 15.0));
            float NdotL = max(0.0, dot(N, L));
            totalDiffuse += PointLightColorAndIntensity[i].rgb * (NdotL * atten * PointLightColorAndIntensity[i].w);
        }
    }
    
    float lum = dot(texColor.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 desatColor = lerp(texColor.rgb, float3(lum, lum, lum), input.Desaturation);
    float3 finalRGB = (desatColor * totalDiffuse) + (desatColor * input.Emissive);
    return float4(finalRGB, texColor.a) * input.ColorTint;
}
```

6. 

---

# **22\. Implementação do Core Gráfico (Desenho, Cantos Arredondados e Fontes HD)**

Para que a SpriteUi desenhe qualquer coisa nativamente com altíssima qualidade (substituindo o ImGui), a base da biblioteca utiliza **SDF (Signed Distance Fields)** tanto para renderizar painéis com cantos arredondados perfeitos quanto para fontes MSDF em 4K/8K sem serrilhados.

### **22.1. Estruturas Base de Desenho (C++)**

Estes componentes definem o que será desenhado. O UIPanelComponent suporta cantos arredondados (vetoriais), e o UITextComponent aponta para um atlas MSDF gerado via HarfBuzz.

```c
namespace ToolEngine::SpriteUi {
    // Painéis, Janelas e Botões
    struct alignas(16) UIPanelComponent {
        vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        vec4 cornerRadius = {8.0f, 8.0f, 8.0f, 8.0f}; // TopLeft, TopRight, BotRight, BotLeft
        float borderThickness = 0.0f;
        vec4 borderColor = {0.0f, 0.0f, 0.0f, 1.0f};
        float softness = 1.0f; // Anti-aliasing da borda SDF
    };

    // Textos HD (MSDF)
    struct alignas(16) UITextComponent {
        uint64_t fontAtlasHandle; 
        char text[256]; // Buffer estático para evitar alocações
        vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        float fontSize = 24.0f;
        float outlineThickness = 0.0f;
        vec4 outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
    };
}
```

### **22.2. O Shader Unificado (HLSL: MSDF e Rounded Corners)**

No Pixel Shader, a engine não usa malhas complexas para cantos arredondados. Ela desenha um Quad (quadrado simples) e corta as pontas matematicamente (SDF), garantindo uma curva perfeita independente do zoom.

```
// shaders/SpriteUI_Core.hlsli

// 1. SDF para Cantos Arredondados (Rounded Box)
float RoundedBoxSDF(float2 p, float2 b, float r) {
    float2 d = abs(p) - b + float2(r, r);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

// 2. Amostragem de Fonte HD (MSDF - Multi-channel Signed Distance Field)
float Median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 finalColor = input.ColorTint;

    // Se for um Painel com Cantos Arredondados
    if (input.IsPanel) {
        float2 pixelPos = input.UV * input.Size;
        float2 halfSize = input.Size * 0.5f;
        
        // Distância SDF do centro do painel
        float dist = RoundedBoxSDF(pixelPos - halfSize, halfSize, input.CornerRadius.x);
        
        // Anti-aliasing suave na borda (smoothstep analítico)
        float alpha = 1.0f - smoothstep(0.0f, input.Softness, dist);
        finalColor.a *= alpha;
    }

    // Se for um Texto MSDF
    if (input.IsText) {
        float3 msd = FontTexture.Sample(MainSampler, input.UV).rgb;
        float sd = Median(msd.r, msd.g, msd.b) - 0.5f;
        
        // Fator de escala da tela para AA nítido
        float screenPxDistance = input.ScreenScale * (sd);
        float textAlpha = clamp(screenPxDistance + 0.5f, 0.0f, 1.0f);
        finalColor.a *= textAlpha;
    }

    return finalColor; // Retorna com Alfa Pré-Multiplicado
}
```

---

# **23\. Guia de Implementação: Usando a SpriteUi em um Software ou Jogo**

Para utilizar a SpriteUi no seu projeto, o fluxo de instanciação é simples e segue o modelo declarativo/DOD.

### **Passo 1: Inicialização do Motor**

Injete a SpriteUi no seu mundo Flecs logo após inicializar a RHI (Placa de Vídeo).

```c
#include "SpriteUi/SpriteUi.hpp"

int main() {
    flecs::world world;
    
    // Registra os sistemas e componentes da SpriteUi
    ToolEngine::SpriteUi::RegisterCore(world);
    ToolEngine::SpriteUi::ToonMotion::Animatoone::RegisterSystem(world);

    // ... Loop principal do SO/Jogo
}
```

### **Passo 2: Criando a Interface (Exemplo: Janela e Botão)**

Em vez de depender de loops ImGui::Begin(), você cria entidades persistentes no ECS. O Flexbox interno resolve o posicionamento recursivamente.

```c
void CreateMainMenu(flecs::world& world) {
    using namespace ToolEngine::SpriteUi;

    // 1. Cria o Canvas Root
    auto canvas = world.entity("MainCanvas")
        .set<UIRectTransform>({ .scale = {1.0f, 1.0f} });

    // 2. Cria um Painel Arredondado
    auto panel = world.entity("MenuPanel")
        .child_of(canvas)
        .set<UIRectTransform>({ .offsetMax = {400.0f, 600.0f}, .pivot = {0.5f, 0.5f} })
        .set<UIPanelComponent>({ 
            .color = {0.1f, 0.1f, 0.1f, 0.9f}, 
            .cornerRadius = {16.0f, 16.0f, 16.0f, 16.0f} // Bordas perfeitamente redondas
        });

    // 3. Cria um Botão com Texto HD
    auto btn = world.entity("StartButton")
        .child_of(panel)
        .set<UIRectTransform>({ .offsetMax = {200.0f, 50.0f} })
        .set<UIInteractionComponent>({}) // Torna clicável nativamente
        .set<UIPanelComponent>({ .color = {0.2f, 0.6f, 1.0f, 1.0f}, .cornerRadius = {8.0f, 8.0f, 8.0f, 8.0f} })
        .set<UITextComponent>({ .text = "INICIAR JOGO", .fontSize = 32.0f });

    // Aplica uma animação de entrada (ToonMotion)
    ToonMotion::Animatoone::Squash(btn, 0.5f);
}
```

### **Passo 3: O Loop de Atualização**

No loop do seu jogo ou editor, informe o tempo delta ao Flecs e despache o rendering.

```c
void GameLoop(flecs::world& world, float dt) {
    // 1. Processa Input (Mouse, Teclado)
    UpdateInputSystems(world);

    // 2. Atualiza ECS (Layout, ToonMotion, GC de Texturas)
    world.progress(dt);

    // 3. Extrai e desenha na GPU
    ToolEngine::SpriteUi::RenderExtractionSystem::ExtractAndDraw();
}
```

---

### **Apêndice E — Implementação do ToonMotion (Animações UI)**

A classe Animatoone injeta componentes dinâmicos no ECS. Quando um comando ToonMotion é disparado, ele anexa um ToonTweenComponent na entidade, que é processado fluidamente pelo sistema de animação.

```c
#pragma once
#include <flecs.h>
#include <functional>
#include "Math/Math.hpp"
#include "SpriteUI_Components.h"

namespace ToolEngine::SpriteUi::ToonMotion {

    enum class EasingType { Linear, EaseOutQuad, ElasticSquash, Bounce };

    // Estrutura interna que vive no ECS durante a animação
    struct ToonTweenComponent {
        float duration;
        float elapsed = 0.0f;
        EasingType ease;
        
        // Estado Inicial e Final
        vec2 start_scale, target_scale;
        vec2 start_pos, target_pos;
        vec4 start_color, target_color;
        
        bool modify_scale = false;
        bool modify_pos = false;
        bool modify_color = false;

        std::function<void()> on_complete = nullptr;
    };

    // Classe Modular do Usuário
    class Animatoone {
    public:
        // Inicializa o motor no mundo
        static void RegisterSystem(flecs::world& world) {
            world.system<UIRectTransform, SpriteComponent, ToonTweenComponent>("ToonMotionSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, UIRectTransform* t, SpriteComponent* s, ToonTweenComponent* tween) {
                    float dt = it.delta_time();
                    for (auto i : it) {
                        tween[i].elapsed += dt;
                        float progress = std::min(tween[i].elapsed / tween[i].duration, 1.0f);
                        
                        // Exemplo simplificado de Easing (EaseOutQuad)
                        float t_ease = (tween[i].ease == EasingType::EaseOutQuad) 
                                       ? progress * (2.0f - progress) : progress;

                        if (tween[i].modify_pos) {
                            t[i].offsetMin = lerp(tween[i].start_pos, tween[i].target_pos, t_ease);
                            // offsetMax acompanha o delta (preservando o tamanho)
                        }
                        if (tween[i].modify_scale) {
                            t[i].scale = lerp(tween[i].start_scale, tween[i].target_scale, t_ease);
                        }
                        if (tween[i].modify_color) {
                            s[i].color = lerp(tween[i].start_color, tween[i].target_color, t_ease);
                        }

                        // Finalização
                        if (progress >= 1.0f) {
                            if (tween[i].on_complete) tween[i].on_complete();
                            it.entity(i).remove<ToonTweenComponent>();
                        }
                    }
                });
        }

        // ======================================================
        // Efeitos Pré-Fabricados (API Declarativa)
        // ======================================================
        
        static void FadeIn(flecs::entity e, float duration) {
            if (!e.has<SpriteComponent>()) return;
            auto sprite = e.get<SpriteComponent>();
            vec4 start = sprite->color;
            start.w = 0.0f; // Alpha 0
            vec4 end = sprite->color;
            end.w = 1.0f;   // Alpha 1
            
            e.set<ToonTweenComponent>({
                .duration = duration, .ease = EasingType::EaseOutQuad,
                .start_color = start, .target_color = end, .modify_color = true
            });
        }

        static void Squash(flecs::entity e, float duration) {
            if (!e.has<UIRectTransform>()) return;
            auto transform = e.get_mut<UIRectTransform>();
            
            // Garantir que o pivô está na base inferior central para um Squash de pulo perfeito
            transform->pivot = {0.5f, 1.0f}; 
            
            e.set<ToonTweenComponent>({
                .duration = duration, .ease = EasingType::ElasticSquash,
                .start_scale = {1.5f, 0.5f}, // Esmagado
                .target_scale = {1.0f, 1.0f}, // Normal
                .modify_scale = true
            });
        }

        static void Shake(flecs::entity e, float intensity, float duration) {
            // Implementação de ruído na posição injetada no componente
        }
    };

} // namespace ToolEngine::SpriteUi::ToonMotion
```

7. 

---

# **24\. Gerenciamento Avançado de Janelas (Docking, Tabs e Resize)**

Para substituir completamente o ImGui sem regressão de funcionalidades no editor da ToolEngine, a SpriteUi implementa nativamente o comportamento de **Docking**, **Windowing** e **Layouting** interativo, processado inteiramente via ECS.

* **Redimensionamento (Resize):** Janelas possuem áreas de *hitbox* invisíveis nas bordas (Top, Bottom, Left, Right, Corners). Quando o UIInputSystem detecta o mouse sobre essas áreas com o botão pressionado, o componente UIRectTransform é ajustado diretamente, disparando um recalculo em cascata no motor de Flexbox.  
* **Arrastar e Soltar (Draggable):** Barras de título possuem hitboxes que habilitam a translação no espaço de tela.  
* **Agrupamento e Abas (Tabs):** Quando uma janela é solta sobre a região de conteúdo de outra, o sistema as agrupa adicionando um UITabGroupComponent. A UI desenha dinamicamente uma barra de *Tabs* no topo, e as entidades das janelas não ativas têm seu sistema de renderização desativado temporariamente (evitando custo de GPU).  
* **Slots e Docking:** A interface global da engine possui um UIDockSpace. Arrastar janelas para as bordas ativa as zonas de *Drop Slots*, subdividindo a tela em proporções ajustáveis (splitRatio) e construindo uma árvore de layout (Binary Tree) perfeitamente escalável.

---

# **25\. Código Fonte MVP: Fases 0 e 1 (Core, Janelas e Docking)**

Abaixo estão as implementações iniciais para a base matemática e a substituição das lógicas de UI do ImGui.

### **25.1 Fase 0: Matemática e Hitboxes (C++)**

Funções essenciais em linha (inline/SIMD friendly) para resolver colisões em UI (AABB 2D).

```c
namespace ToolEngine::SpriteUi::Core {
    // Verifica se o ponteiro/toque está dentro do Retângulo 2D
    inline bool RectContains(vec2 pos, vec2 size, vec2 mousePos) {
        return mousePos.x >= pos.x && mousePos.x <= pos.x + size.x &&
               mousePos.y >= pos.y && mousePos.y <= pos.y + size.y;
    }

    // Detecção da borda para redimensionamento de janela
    enum class ResizeEdge { None, Left, Right, Top, Bottom, BottomRight };
    
    inline ResizeEdge GetResizeEdge(vec2 pos, vec2 size, vec2 mousePos, float edgeThickness = 6.0f) {
        bool left = mousePos.x >= pos.x && mousePos.x <= pos.x + edgeThickness;
        bool right = mousePos.x >= pos.x + size.x - edgeThickness && mousePos.x <= pos.x + size.x;
        bool top = mousePos.y >= pos.y && mousePos.y <= pos.y + edgeThickness;
        bool bottom = mousePos.y >= pos.y + size.y - edgeThickness && mousePos.y <= pos.y + size.y;
        
        if (bottom && right) return ResizeEdge::BottomRight;
        if (left) return ResizeEdge::Left;
        if (right) return ResizeEdge::Right;
        if (top) return ResizeEdge::Top;
        if (bottom) return ResizeEdge::Bottom;
        return ResizeEdge::None;
    }
}
```

### **25.2 Fase 1: Componentes de Docking e Janela (Substituindo ImGui)**

Os dados da janela, do *Docking* e o sistema do Flecs que processa a movimentação a cada frame.

8. 

```c
namespace ToolEngine::SpriteUi {

    // Snapshot imutável de entrada por quadro
    struct alignas(16) UIInputSnapshot {
        vec2 mousePos;
        vec2 mouseDelta;
        bool leftPressed;
        bool leftReleased;
        bool leftDown;
    };

    // Retângulo resolvido pelo sistema de layout (separado de UIRectTransform)
    struct UILayoutRect {
        vec2 resolvedPos{0.0f, 0.0f};
        vec2 resolvedSize{0.0f, 0.0f};
        bool isDirty = false;
    };

    // Estado de roteamento mantido centralizadamente sem const_cast
    struct UIPointerRoutingState {
        flecs::entity capturedEntity = flecs::entity::null();
        flecs::entity focusedEntity = flecs::entity::null();
        bool eventHandled = false;
    };

    struct UIWindowComponent {
        char title[64];
        bool isMinimized = false;
        vec2 minSize = {150.0f, 100.0f};
        float titleBarHeight = 30.0f;
    };

    class UIWindowInteractionSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            world.system<UIRectTransform, UILayoutRect, UIWindowComponent>("UIWindowSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, UIRectTransform* t, UILayoutRect* layout, UIWindowComponent* win) {
                    const auto* input = it.world().get<UIInputSnapshot>();
                    auto* routing = it.world().get_mut<UIPointerRoutingState>();
                    if (!input || !routing || routing->eventHandled) return;

                    for (auto i : it) {
                        vec2 pos = layout[i].resolvedPos;
                        vec2 size = layout[i].resolvedSize;

                        Core::ResizeEdge edge = Core::GetResizeEdge(pos, size, input->mousePos);
                        if (edge == Core::ResizeEdge::BottomRight && input->leftDown) {
                            t[i].offsetMax += input->mouseDelta;
                            t[i].offsetMax.x = std::max(t[i].offsetMax.x, t[i].offsetMin.x + win[i].minSize.x);
                            t[i].offsetMax.y = std::max(t[i].offsetMax.y, t[i].offsetMin.y + win[i].minSize.y);
                            layout[i].isDirty = true;
                            routing->capturedEntity = it.entity(i);
                        }

                        vec2 titleBarSize = {size.x, win[i].titleBarHeight};
                        if (Core::RectContains(pos, titleBarSize, input->mousePos)) {
                            if (input->leftPressed) {
                                routing->focusedEntity = it.entity(i);
                            }
                            if (input->leftDown && routing->focusedEntity == it.entity(i)) {
                                t[i].offsetMin += input->mouseDelta;
                                t[i].offsetMax += input->mouseDelta;
                                layout[i].isDirty = true;
                            }
                        }

                        if (input->leftReleased && routing->capturedEntity == it.entity(i)) {
                            routing->capturedEntity = flecs::entity::null();
                        }
                    }
                });
        }
    };
} // namespace ToolEngine::SpriteUi
```

9. 

---

# **26\. O Motor de Eventos, Raycasting e Event Bubbling (Spatial Partitioning)**

Para evitar testar o mouse contra 50.000 hitboxes a cada frame, a SpriteUi utiliza um motor de eventos baseado em **Z-Index reverso e Consumo de Eventos (Event Bubbling)**.

* **Hit-Testing Reverso:** Como a UI é desenhada de trás para frente (Back-to-Front), o Raycast de tela testa as entidades de frente para trás (Front-to-Back). Assim que uma entidade com UIInteractionComponent intersecta o mouse, ela recebe o evento.  
* **Event Consumption:** Se a entidade for um botão e o usuário clicar, o evento é marcado como Handled \= true. Isso impede que o clique "atravesse" a interface e acabe disparando a arma do personagem no jogo 3D que está renderizado atrás da janela (Event Bubbling).  
* **Foco e Captura (Mouse Capture):** Se o usuário clica e segura um Slider, a entidade ganha o MouseCapture. Mesmo que o mouse saia da área do Slider, ele continua recebendo os deltas de movimento até o botão ser solto.

---

# **27\. O Sistema de Text Input (Teclado, Cursor e Clipboard)**

O rendering HD de textos (SDF) é complementado por um sistema de entrada que conversa diretamente com as APIs do Sistema Operacional (Windows/Linux/Mac via SDL/GLFW).

* **Entrada Unicode e IME:** A engine escuta eventos de caracteres (Char Input), não apenas de teclas físicas, garantindo suporte a acentuação (ex: "\~" \+ "a" \= "ã") e teclados asiáticos.  
* **Caret (Cursor Piscante) e Seleção:** O UITextInputComponent gerencia o índice do cursor na string. Renderizamos um Quad sobreposto para a Seleção de Texto (fundo azul claro) e usamos a função seno baseada no tempo global para piscar a barra vertical do Cursor.  
* **Integração OS:** Atalhos como Ctrl+C, Ctrl+V, Ctrl+X são interceptados e roteados para o Clipboard do sistema operacional, tornando o campo de texto indistinguível de um software nativo.

---

# **28\. Data Binding e Reatividade (MVVM no ECS)**

Para manter o princípio DOD limpo e evitar código espaguete, a UI e o Jogo conversam através de **Value Bindings (MVVM)** gerenciados pelo Flecs.

* **UIValueBinding:** Um componente genérico que vincula um dado do jogo (ex: HealthComponent do Player) a um dado visual da UI (ex: largura do UIRectTransform da barra de vida).  
* **Dirty Flag (Reatividade):** O ECS verifica o hash ou o valor em cache no início do frame. Se a vida do jogador não mudou, a UI não recalcula nada. Se mudou, o sistema atualiza exclusivamente o componente visual afetado.

---

### **Apêndice F — Código MVP: Eventos, Text Input e Data Binding**

### **F.1. O Sistema de Raycast e Eventos (C++)**

```c
namespace ToolEngine::SpriteUi::Events {

    enum class EventType { MouseEnter, MouseLeave, MouseDown, MouseUp, Click };

    struct UIEvent {
        EventType type;
        vec2 position;
        bool handled = false;
    };

    struct UIInteractionComponent {
        bool isHovered = false;
        bool isPressed = false;
        bool blockEvents = true;
    };

    class UIEventSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            world.system<UILayoutRect, UIInteractionComponent>("UIEventSystem")
                .kind(flecs::PreUpdate)
                .iter([](flecs::iter& it, UILayoutRect* layout, UIInteractionComponent* interact) {
                    const auto* input = it.world().get<UIInputSnapshot>();
                    auto* routing = it.world().get_mut<UIPointerRoutingState>();
                    if (!input || !routing || routing->eventHandled) return;

                    // Hit-test executado em ordem reversa de renderização (Front-to-Back)
                    for (int i = it.count() - 1; i >= 0; --i) {
                        bool hover = Core::RectContains(layout[i].resolvedPos, layout[i].resolvedSize, input->mousePos);
                        interact[i].isHovered = hover;

                        if (hover) {
                            if (input->leftPressed) interact[i].isPressed = true;
                            if (input->leftReleased && interact[i].isPressed) {
                                interact[i].isPressed = false;
                            }

                            if (interact[i].blockEvents) {
                                routing->eventHandled = true;
                                break;
                            }
                        } else {
                            if (input->leftReleased) interact[i].isPressed = false;
                        }
                    }
                });
        }
    };
}
```

### **F.2. Componente de Text Input Editável (C++)**

```c
namespace ToolEngine::SpriteUi::Input {

    // Componente POD de Text Input (sem alocações ou buffers inseguros no caminho quente)
    struct UITextInputComponent {
        bool hasFocus = false;
        uint32_t cursorGraphemeIndex = 0;
        int32_t selectionGraphemeStart = -1;
        float blinkTimer = 0.0f;
        uint32_t textLengthBytes = 0;
    };

    // Pipeline Canônico de Manipulação de Texto e Unicode:
    // 1. Entrada UTF-8 validada via SO (sem truncamento de codepoints multi-byte).
    // 2. Segmentação em Grapheme Clusters (evita quebrar diacríticos ou emojis compostos).
    // 3. HarfBuzz Text Shaping: Mapeamento de codepoints para Glyph IDs e offsets subpixel.
    // 4. Inserção no buffer de arena e rasterização via Atlas Dinâmico MSDF.
    void PushUtf8Text(flecs::world& world, const char* utf8String, size_t byteLength) {
        // Validar sequência UTF-8 completa antes da mutação
        // Despachar eventos de edição Unicode para sistemas externos de edição de texto
    }
}
```

### **F.3. Data Binding MVVM (Reatividade C++)**

```c
namespace ToolEngine::SpriteUi::Binding {

    // Componente genérico para vincular valores Flutuantes (ex: Vida do Player -> Width da Barra)
    struct UIFloatBinding {
        flecs::entity sourceEntity;     // Ex: Entidade do Jogador
        float* sourceValuePtr = nullptr;// Ponteiro para o dado bruto
        float lastKnownValue = -1.0f;   // Cache para checar alteração
        std::function<void(flecs::entity uiEntity, float newValue)> onValueChanged;
    };

    class UIBindingSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            world.system<UIFloatBinding>("UIFloatBindingSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, UIFloatBinding* bind) {
                    for (auto i : it) {
                        if (bind[i].sourceEntity.is_alive() && bind[i].sourceValuePtr != nullptr) {
                            
                            float currentValue = *(bind[i].sourceValuePtr);
                            
                            // Reatividade: Só executa se o valor mudou (Dirty Check)
                            if (abs(currentValue - bind[i].lastKnownValue) > 0.001f) {
                                bind[i].lastKnownValue = currentValue;
                                
                                // Chama o Callback para atualizar a UI (ex: mudar a largura de um UIRectTransform)
                                if (bind[i].onValueChanged) {
                                    bind[i].onValueChanged(it.entity(i), currentValue);
                                }
                            }
                        }
                    }
                });
        }
    };
}
```

10. 

---

# **29\. Renderização Analítica de Gráficos (Charts & Plots)**

A SpriteUi substitui a necessidade de bibliotecas externas de plotagem (como ImPlot) usando a matemática vetorial para desenhar gráficos em tempo real diretamente na GPU, ideais para telemetria de engine, estatísticas de RPG ou UI Sci-Fi.

* **Gráficos de Barra (Bar Charts):** Utilizam o sistema base de Flexbox e UIRectTransform. Cada barra é uma entidade UIPanelComponent ancorada na base, com sua altura vinculada aos dados do jogo via UIFloatBinding. O motor ToonMotion pode aplicar *Squash* ou interpolação no eixo Y para animar dinamicamente o crescimento ou redução das barras quando os dados mudam.  
* **Gráficos de Linha (Line Charts):** Em vez de instanciar milhares de *quads* individuais, a engine usa um UILineRendererComponent. Ele armazena um buffer circular (*ring buffer*) de pontos, desenhando a conexão vetorial via *Capsule SDF* (distância de cápsula) diretamente no Pixel Shader, garantindo espessura constante e bordas perfeitamente arredondadas em qualquer nível de zoom.  
* **Gráfico de "Batimentos" (Oscilloscope / Scrolling Line):** Usado para frequências (estilo eletrocardiograma, monitor de áudio ou debug de framerate). Os valores de frequência alimentam uma estrutura UIOscilloscopeGraph. Os dados vão para a GPU como uma textura 1D genérica ou buffer. O shader lê o histórico, aplica um deslocamento contínuo baseado no Tempo (Time) para criar o efeito de rolagem da tela, e renderiza a linha principal com um gradiente emissivo (rastro luminoso) que se apaga na cauda.

---

# **30\. Revisão e Código Fase 3: Integração Light Prism e Extração**

Revisando a arquitetura até a Fase 2, faltava documentar o **Elo de Ligação**: o código que extrai as janelas, textos e gráficos iterados no ECS e os empacota para a placa de vídeo, aplicando os preceitos da Fase 3 (Integração com a Light Prism).  
A Fase 3 foca em transformar a interface plana em elementos físicos reagentes à luz. Paineis ganham "relevo" e textos ou gráficos (como o de batimentos cardíacos) podem emitir luz própria (*Neon / Bloom*).

### **30.1. Componentes PBR, Neon e Gráficos (Fase 3\)**

```c
namespace ToolEngine::SpriteUi::Materials {
    
    // Reação à luz dinâmica 2D (Relevo simulado)
    struct UINormalMap {
        uint64_t textureHandle = 0;
        float bumpIntensity = 1.0f;
    };

    // Efeito Neon / Bloom Pass
    struct UIEmissiveComponent {
        float intensity = 2.0f; // Valores > 1.0 ativam o brilho (Bloom)
        vec3 glowColor = {0.0f, 1.0f, 0.8f};
    };
    
    // Gráfico de Frequência (Eletrocardiograma / Osciloscópio)
    struct UIOscilloscopeGraph {
        float valueHistory[256]; // Ring buffer de dados
        int headIndex = 0;
        vec4 lineColor = {0.0f, 1.0f, 0.0f, 1.0f};
        float thickness = 2.0f;
        float scrollSpeed = 50.0f; // Velocidade em pixels por segundo
    };
}
```

### **30.2. O Sistema de Extração (O Elo CPU-GPU)**

Este sistema recolhe as matrizes e propriedades de materiais e despacha o *Draw Call* instanciado.

11. 

```c
namespace ToolEngine::SpriteUi::Rendering {

    class UIRenderExtractionSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            // Query segura com componentes opcionais via ?
            world.system<UILayoutRect, SpriteComponent, const Materials::UINormalMap*, const Materials::UIEmissiveComponent*>("UIExtractionSystem")
                .kind(flecs::PostUpdate)
                .iter([](flecs::iter& it, UILayoutRect* layouts, SpriteComponent* sprites, const Materials::UINormalMap** normals, const Materials::UIEmissiveComponent** emissives) {
                    std::vector<SpriteInstanceGPU> drawBuffer;
                    drawBuffer.reserve(it.count());

                    for (auto i : it) {
                        SpriteInstanceGPU instance = {};

                        // Matriz 2x3 compactada em float4 (Row0, Row1)
                        instance.transformRow0 = {1.0f, 0.0f, layouts[i].resolvedPos.x, 0.0f};
                        instance.transformRow1 = {0.0f, 1.0f, layouts[i].resolvedPos.y, 0.0f};
                        instance.uvRect = {sprites[i].uv.x, sprites[i].uv.y, sprites[i].uv.z, sprites[i].uv.w};

                        // Cor linear pré-multiplicada
                        instance.tint = {sprites[i].color.x, sprites[i].color.y, sprites[i].color.z, sprites[i].color.w};

                        // Índices de recursos e flags
                        uint32_t flags = 0;
                        if (normals && normals[i]) flags |= 0x1;
                        if (emissives && emissives[i]) flags |= 0x2;
                        instance.resourceIds = {sprites[i].textureId, sprites[i].materialId, 0, flags};

                        drawBuffer.push_back(instance);
                    }

                    // Submissão direta ao Upload Heap da Diligent Engine
                    // GraphicsBackend::UpdateStructuredBuffer("UISpriteInstances", drawBuffer.data(), drawBuffer.size() * sizeof(SpriteInstanceGPU));
                });
        }
    };
}
```

12. 

### **25.3 Fase 2: Implementação de Widgets Avançados (Botões, Sliders e Scroll)**

Com o roteamento seguro de eventos resolvido na Fase 1, a Fase 2 introduz os componentes lógicos (PODs) para os widgets de interface. Nenhum destes componentes armazena callbacks (std::function); eles representam puramente o estado visual e semântico. A comunicação com o jogo deve ser feita por sistemas reativos observando as mudanças de valor ou filas de eventos separadas.

```c
namespace ToolEngine::SpriteUi::Widgets {

    // 1. Botões e Toggles
    struct UIButtonComponent {
        vec4 normalColor = {0.8f, 0.8f, 0.8f, 1.0f};
        vec4 hoverColor = {0.9f, 0.9f, 0.9f, 1.0f};
        vec4 pressedColor = {0.6f, 0.6f, 0.6f, 1.0f};
        bool isDisabled = false;
    };

    struct UIToggleComponent {
        bool isOn = false;
        vec4 onColor = {0.2f, 0.8f, 0.2f, 1.0f};
        vec4 offColor = {0.8f, 0.2f, 0.2f, 1.0f};
    };

    // 2. Sliders
    struct UISliderComponent {
        float value = 0.5f;     // Normalizado [0, 1]
        float minValue = 0.0f;
        float maxValue = 1.0f;
        bool isDragging = false;
    };

    // 3. Scroll View (Virtualização e Clipping)
    struct UIScrollViewComponent {
        vec2 scrollOffset = {0.0f, 0.0f};
        vec2 contentSize = {0.0f, 0.0f};
        bool allowScrollX = false;
        bool allowScrollY = true;
    };

    // Sistema unificado de interação de Widgets (Processado após o UIEventSystem)
    class UIWidgetSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            // Atualização visual de Botões baseada no estado de interação
            world.system<const Events::UIInteractionComponent, UIButtonComponent, UIPanelComponent>("UIButtonVisualSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, const Events::UIInteractionComponent* interact, UIButtonComponent* btn, UIPanelComponent* panel) {
                    for (auto i : it) {
                        if (btn[i].isDisabled) {
                            panel[i].color = btn[i].normalColor; // Pode aplicar máscara de desaturação
                            continue;
                        }
                        
                        if (interact[i].isPressed) {
                            panel[i].color = btn[i].pressedColor;
                        } else if (interact[i].isHovered) {
                            panel[i].color = btn[i].hoverColor;
                        } else {
                            panel[i].color = btn[i].normalColor;
                        }
                    }
                });

            // Atualização lógica de Sliders (arraste e captura)
            world.system<const UILayoutRect, const Events::UIInteractionComponent, UISliderComponent>("UISliderSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, const UILayoutRect* layout, const Events::UIInteractionComponent* interact, UISliderComponent* slider) {
                    const auto* input = it.world().get<UIInputSnapshot>();
                    auto* routing = it.world().get_mut<UIPointerRoutingState>();
                    if (!input || !routing) return;

                    for (auto i : it) {
                        flecs::entity entity = it.entity(i);

                        // Inicia o arrasto se foi pressionado
                        if (interact[i].isPressed && input->leftPressed) {
                            routing->capturedEntity = entity;
                            slider[i].isDragging = true;
                        }
                        
                        // Solta o arrasto
                        if (slider[i].isDragging && input->leftReleased) {
                            if (routing->capturedEntity == entity) {
                                routing->capturedEntity = flecs::entity::null();
                            }
                            slider[i].isDragging = false;
                        }

                        // Atualiza o valor baseado na posição do mouse durante o arraste
                        if (slider[i].isDragging) {
                            float localMouseX = input->mousePos.x - layout[i].resolvedPos.x;
                            float normalized = std::clamp(localMouseX / layout[i].resolvedSize.x, 0.0f, 1.0f);
                            slider[i].value = slider[i].minValue + normalized * (slider[i].maxValue - slider[i].minValue);
                        }
                    }
                });
                
            // ScrollView e processamento da roda do rato (Mouse Wheel)
            world.system<const Events::UIInteractionComponent, UIScrollViewComponent>("UIScrollSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, const Events::UIInteractionComponent* interact, UIScrollViewComponent* scroll) {
                    const auto* input = it.world().get<UIInputSnapshot>();
                    if (!input) return;

                    for (auto i : it) {
                        // Se o rato estiver em cima da view, aplicamos o delta do scroll
                        if (interact[i].isHovered && (input->mouseDelta.y != 0.0f)) { // Assumindo que mouseDelta carrega a roda ou usando input->wheelDelta
                            if (scroll[i].allowScrollY) {
                                scroll[i].scrollOffset.y -= input->mouseDelta.y * 20.0f; // Multiplicador de velocidade
                                // Clamp do scroll baseado no contentSize vs layout resolvida
                                scroll[i].scrollOffset.y = std::max(0.0f, scroll[i].scrollOffset.y); 
                            }
                        }
                    }
                });
        }
    };
}
```

Nesta fase, a interface já dispõe de interatividade sólida para controlos de jogo e ferramentas de edição. A lógica respeita as regras de *captura de foco*, impedindo que o rato interaja com elementos de fundo enquanto um UISlider está a ser arrastado. O scroll respeita limites de renderização através do *Scissor Rect* emitido em conjunto pela *Render Extraction*.

### **25.4 Fase 3: Texto e Tipografia HD (HarfBuzz e MSDF)**

A Fase 3 estabelece o pipeline profissional de tipografia, abandonando a conversão direta (e ineficiente) de strings para *quads* frame a frame. Em arquitetura orientada a dados (DOD), separamos a intenção (a string UTF-8) do resultado processado (os glifos prontos a desenhar).  
O UITextShapingSystem deteta mudanças na string utilizando uma *dirty flag*, processa o texto através do HarfBuzz para resolver ligaduras e kerning (espaçamento sub-pixel), e armazena o resultado no UITextCacheComponent.

```c
namespace ToolEngine::SpriteUi::Text {

    // Representação de um glifo processado e pronto para a GPU
    struct GlyphInstance {
        uint32_t codepoint;
        vec2 offset;     // Deslocamento local (Kerning + Shaping)
        vec2 advance;    // Avanço horizontal/vertical para o próximo caractere
        vec4 uvRect;     // Mapeamento no Atlas MSDF
        vec2 size;       // Dimensões do caractere
    };

    // Cache de texto: Evita invocar o HarfBuzz em todos os frames
    struct UITextCacheComponent {
        std::vector<GlyphInstance> shapedGlyphs;
        bool isDirty = true;
        vec2 textBounds = {0.0f, 0.0f};
    };

    // Sistema de Text Shaping (Integração HarfBuzz)
    class UITextShapingSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            // Reage apenas a entidades que possuam um texto e um cache
            world.system<const UITextComponent, UITextCacheComponent>("UITextShapingSystem")
                .kind(flecs::PreUpdate)
                .iter([](flecs::iter& it, const UITextComponent* text, UITextCacheComponent* cache) {
                    for (auto i : it) {
                        if (cache[i].isDirty) {
                            // 1. Limpar cache antigo
                            cache[i].shapedGlyphs.clear();

                            // 2. Invocar HarfBuzz com text[i].text (UTF-8 validado)
                            // HB_Buffer_Add_UTF8(...)
                            // HB_Shape(...)
                            
                            // 3. Obter Glyph IDs, ligaduras e offsets subpixel
                            // 4. Mapear UVs consultando o FontAtlas (MSDF)
                            // 5. Preencher cache[i].shapedGlyphs e calcular textBounds

                            cache[i].isDirty = false; // Marcar como limpo
                        }
                    }
                });
        }
    };
}
```

O sistema de extração (UIRenderExtractionSystem) itera sobre o UITextCacheComponent. Para cada GlyphInstance válido, gera uma SpriteInstanceGPU de 80 bytes diretamente no buffer da GPU. A seleção de texto e o piscar do cursor (UITextInputComponent) desenham entidades geométricas separadas por trás do texto e não forçam o recalculo do *shaping* de HarfBuzz.

### **25.5 Fase 4: Animação e Bindings (ToonMotion V2 Expansivo)**

A Fase 4 restrutura o motor de animações para cumprir os requisitos de segurança e concorrência do ECS (Data-Oriented Design). A V2 remove a dependência de *callbacks* (std::function) e introduz um pipeline robusto e rico em recursos, evitando implementações "secas" e puramente teóricas. Agora suportamos IDs de rastreio, controlo de reprodução (Play, Pause, Break) e uma biblioteca completa de pré-animações (Shake, Squash, Spin).

```c
namespace ToolEngine::SpriteUi::ToonMotion {

    enum class TweenProperty { Position, Scale, Rotation, ColorAlpha, ShakeOffset };
    enum class TweenConflict { Override, Queue, Ignore };
    enum class Easing { Linear, EaseOutQuad, ElasticSquash, Bounce };
    enum class PlayState { Playing, Paused, Stopped };

    // Estrutura POD expansiva, totalmente segura para iteração no ECS
    struct ToonTween {
        uint32_t tweenId = 0;                 // ID único da animação para rastreio
        PlayState state = PlayState::Playing; // Controlo de Play/Pause/Break
        
        TweenProperty property;
        TweenConflict conflictPolicy;
        Easing easing;
        
        float duration;
        float elapsed = 0.0f;
        
        vec4 startValue;
        vec4 targetValue;
        
        uint32_t completionEventId = 0; // ID do evento para notificar o sistema global ao terminar
    };

    // Biblioteca Rica de Pré-Animações (ToonFX)
    class ToonFX {
    public:
        static uint32_t s_NextTweenId;

        // Pré-animação: Esticar e Esmagar (Feedback visual)
        static void SquashAndStretch(flecs::entity e, float duration) {
            e.set<ToonTween>({
                .tweenId = ++s_NextTweenId, .state = PlayState::Playing,
                .property = TweenProperty::Scale, .conflictPolicy = TweenConflict::Override,
                .easing = Easing::ElasticSquash, .duration = duration,
                .startValue = {1.5f, 0.5f, 1.0f, 1.0f}, .targetValue = {1.0f, 1.0f, 1.0f, 1.0f}
            });
        }

        // Pré-animação: Girar/Rotação
        static void Spin(flecs::entity e, float targetAngle, float duration) {
            e.set<ToonTween>({
                .tweenId = ++s_NextTweenId, .state = PlayState::Playing,
                .property = TweenProperty::Rotation, .conflictPolicy = TweenConflict::Override,
                .easing = Easing::EaseOutQuad, .duration = duration,
                .startValue = {0.0f, 0.0f, 0.0f, 0.0f}, .targetValue = {targetAngle, 0.0f, 0.0f, 0.0f}
            });
        }

        // Pré-animação: Tremer (Erro/Impacto)
        static void Shake(flecs::entity e, float intensity, float duration) {
            e.set<ToonTween>({
                .tweenId = ++s_NextTweenId, .state = PlayState::Playing,
                .property = TweenProperty::ShakeOffset, .conflictPolicy = TweenConflict::Override,
                .easing = Easing::Bounce, .duration = duration,
                .startValue = {intensity, intensity, 0.0f, 0.0f}, .targetValue = {0.0f, 0.0f, 0.0f, 0.0f}
            });
        }

        // API de Controlo de Tempo e Playback
        static void Pause(flecs::entity e) { if(auto* t = e.get_mut<ToonTween>()) t->state = PlayState::Paused; }
        static void Play(flecs::entity e)  { if(auto* t = e.get_mut<ToonTween>()) t->state = PlayState::Playing; }
        static void Break(flecs::entity e) { if(auto* t = e.get_mut<ToonTween>()) t->state = PlayState::Stopped; }
    };

    uint32_t ToonFX::s_NextTweenId = 1000;

    class ToonMotionSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            world.system<UIRectTransform, SpriteComponent, ToonTween>("ToonMotionSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, UIRectTransform* t, SpriteComponent* s, ToonTween* tween) {
                    float dt = it.delta_time();
                    for (auto i : it) {
                        // Respeito ao estado de Playback
                        if (tween[i].state == PlayState::Paused) continue;
                        if (tween[i].state == PlayState::Stopped) { // Break
                            it.entity(i).remove<ToonTween>();
                            continue;
                        }

                        if (tween[i].duration <= 0.001f) tween[i].elapsed = 1.0f;
                        else tween[i].elapsed += dt;

                        float progress = std::clamp(tween[i].elapsed / tween[i].duration, 0.0f, 1.0f);
                        
                        // Cálculo de Easing
                        float t_ease = (tween[i].easing == Easing::EaseOutQuad) 
                                     ? progress * (2.0f - progress) : progress;

                        // Aplicação expandida de propriedades
                        if (tween[i].property == TweenProperty::Position) {
                            t[i].offsetMin = lerp(vec2(tween[i].startValue), vec2(tween[i].targetValue), t_ease);
                        } else if (tween[i].property == TweenProperty::Scale) {
                            t[i].scale = lerp(vec2(tween[i].startValue), vec2(tween[i].targetValue), t_ease);
                        } else if (tween[i].property == TweenProperty::ColorAlpha) {
                            s[i].color.w = lerp(tween[i].startValue.w, tween[i].targetValue.w, t_ease);
                        } else if (tween[i].property == TweenProperty::Rotation) {
                            t[i].rotation = lerp(tween[i].startValue.x, tween[i].targetValue.x, t_ease);
                        } else if (tween[i].property == TweenProperty::ShakeOffset) {
                            // Ruído determinístico baseado no tempo global para o Shake
                            float shakeX = sin(tween[i].elapsed * 50.0f) * lerp(tween[i].startValue.x, 0.0f, progress);
                            float shakeY = cos(tween[i].elapsed * 45.0f) * lerp(tween[i].startValue.y, 0.0f, progress);
                            t[i].offsetMin += vec2(shakeX, shakeY); // Aplicação de offset visual dinâmico
                        }

                        // Resolução de Conclusão e Event Bubbling
                        if (progress >= 1.0f) {
                            if (tween[i].completionEventId != 0) {
                                // DispatchEvent(tween[i].completionEventId, it.entity(i));
                            }
                            it.entity(i).remove<ToonTween>();
                        }
                    }
                });
        }
    };
}
```

A adição da API ToonFX e do controlo de PlayState reflete uma arquitetura pronta para produção, garantindo que o motor de UI tem as primitivas necessárias para construir interfaces com *Game Feel* nativo. O princípio de "evitar código seco" torna-se agora uma diretiva clara em todo o motor, expandindo as APIs para lá do mínimo viável (MVP) e oferecendo ferramentas ricas diretamente acopladas ao Flecs ECS.  
---

### **25.6 Fase 5: Integração Light Prism e Efeitos Especiais (Blur, Bloom e PBR)**

A Fase 5 estabelece a integração profunda da interface com o renderizador global (Light Prism). Em vez de a UI ser apenas uma camada plana colada no topo do ecrã, ela interage opticamente com a cena. Painéis de vidro embaciado (Acrylic Blur) distorcem o cenário atrás de si, os ecrãs emitem luz própria que afeta o mundo e sombras suaves (geradas analiticamente por SDF) dão profundidade vetorial.  
Para garantir performance e manter a coerência arquitetural (sem injetar código na RHI diretamente a partir do ECS), separamos os componentes de *design* das chamadas de extração gráfica:

```c
namespace ToolEngine::SpriteUi::Effects {

    // Efeito de Vidro Fosco (Acrylic Blur) - Inspirado no Fluent Design
    struct UIAcrylicBlurComponent {
        float blurRadius = 16.0f; // Controla a dispersão no kernel Dual-Kawase
        float tintOpacity = 0.6f;
        vec4 tintColor = {0.05f, 0.05f, 0.05f, 1.0f};
        bool useDithering = true; // Essencial para evitar banding em gradientes suaves
    };

    // Sombra Vetorial Fake (Zero custo de VRAM, gerada analiticamente no Shader)
    struct UIShadowSDFComponent {
        vec2 shadowOffset = {6.0f, -6.0f};
        float softness = 12.0f; // Espalhamento de borda (Smoothstep margin)
        vec4 shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    };

    class UIEffectsExtractionSystem {
    public:
        static void RegisterSystem(flecs::world& world) {
            // Extração de painéis de vidro (Acrylic Blur)
            world.system<UILayoutRect, UIAcrylicBlurComponent>("UIAcrylicExtractionSystem")
                .kind(flecs::PostUpdate)
                .iter([](flecs::iter& it, UILayoutRect* layouts, UIAcrylicBlurComponent* blur) {
                    for (auto i : it) {
                        // O sistema recolhe a bounding box (UILayoutRect) e as 
                        // propriedades de Blur. Na submissão à RHI (Diligent Engine),
                        // estes painéis instruem o renderizador a realizar uma cópia do 
                        // Backbuffer e aplicar downsample/upsample com o kernel Dual-Kawase.
                        // Como a estrutura é POD, ela é simplesmente empacotada num 
                        // buffer estruturado e tratada num passe de Pós-Processamento da UI.
                    }
                });
        }
    };
}
```

#### **Pipeline de Emissão (Neon / Bloom) e Iluminação PBR**

Conforme planeado no motor, quando uma entidade UI possui o UIEmissiveComponent (com intensidade \> 1.0), a SpriteInstanceGPU sinaliza a flag no resourceIds. O *Pixel Shader* bifurca essa informação, gerando saída não só para o alvo principal de cor (*Main Render Target*), mas também injetando os pixéis super-brilhantes diretamente na *Bloom Chain* da Light Prism. Isto cria automaticamente um brilho ótico (Neon) à volta de textos de aviso ou barras de vida críticas.  
Para painéis de interface no mundo 3D (*In-World UI*), os Normal Maps (UINormalMap) reagem às *Point Lights* da grade de iluminação (Clustered Shading). A SpriteUi lê o LightPrismGlobalLights (Constant Buffer) partilhado com o resto do motor, garantindo que botões 2D em paredes escuras de uma masmorra fiquem sombreados de forma fisicamente correta.

### **25.7 Fase 6: Otimização On-Demand, Menus Avançados e QuickJS Bindings**

A Fase 6 conclui o pipeline arquitetural abordando três pilares essenciais para produção AAA: eficiência energética (resolvendo a "queima" de GPU clássica do ImGui), sistemas complexos de navegação (menus suspensos perfeitos) e blindagem de *scripting*.

#### **1\. Fim da Queima de GPU: Renderização On-Demand (Idle-Sleep)**

Um dos maiores defeitos estruturais do ImGui é o seu paradigma *Immediate Mode*, que força a CPU a reconstruir e a GPU a re-renderizar todos os vértices a 60, 120 ou 144 frames por segundo, mesmo que o utilizador não esteja a tocar no rato ou teclado. Como a **SpriteUi** foi projetada em **Retained Mode** sob o ECS, resolvemos isto implementando uma arquitetura de **On-Demand Rendering**.  
O motor ECS mantém um recurso global genérico chamado UIDirtyState. O sistema de Extração (UIRenderExtractionSystem) só recolhe as entidades e despacha novos dados para o *Upload Heap* da GPU se a *flag* de estado sujo for verdadeira.  
**O que ativa o *Dirty State*?**

13. **Inputs:** Movimento do rato, cliques, toques no ecrã ou digitação de carateres.  
14. **Animação Cinética:** Existência de pelo menos um componente ToonTween ativo com o PlayState::Playing.  
15. **Reatividade (Data Binding):** O UIBindingSystem detetou uma mudança na variável observada (ex: vida do jogador) e alterou uma propriedade visual.  
16. **Efeitos Temporais:** Piscar do cursor (caret) em caixas de texto.

**Resultado:** Se o editor ou o jogo principal estiver em pausa (ou sem nada a mexer na interface), a carga no sistema UI (CPU e GPU) cai para praticamente **0%**, pois a RHI apenas mantém ou reutiliza o último *framebuffer* compilado.

#### **2\. Layouts Avançados: Flexbox, Ícones e Menus em Cascata**

Para evitar o esforço manual de alinhamento e garantir janelas redondas e bonitas para a produção, consolidamos a camada matemática de Flexbox, Ícones vetoriais e Popups.

```c
namespace ToolEngine::SpriteUi::Layout {
    
    enum class FlexDirection { Row, Column };
    enum class FlexAlign { Start, Center, End, SpaceBetween, SpaceEvenly, Stretch };

    // Componente definitivo para alinhamento CSS-Like sem ambiguidades
    struct UIFlexLayout {
        FlexDirection direction = FlexDirection::Column;
        FlexAlign justifyContent = FlexAlign::Start;
        FlexAlign alignItems = FlexAlign::Stretch;
        vec4 padding = {0.0f, 0.0f, 0.0f, 0.0f}; // Top, Right, Bottom, Left
        float gap = 4.0f; // Espaço exato entre os filhos
    };

    // Componente de Ícones HD (Usa o mesmo pipeline avançado MSDF e HarfBuzz da tipografia)
    struct UIIconComponent {
        uint32_t unicodePoint; // Ex: FontAwesome codepoint
        vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        float size = 16.0f;
    };

    // Gestor Global de Menus Contextuais e Dropdowns
    struct UIPopupManager {
        flecs::entity activePopupTreeRoot;
        bool blockBackgroundInteractions = true;
    };
}
```

* **Ícones HD:** Os ícones (UIIconComponent) não usam mapas de bits (PNGs rasgados). Usam o motor MSDF (SDF multicanal), permitindo que um ícone de lupa ou de pasta escale para monitores 4K sem qualquer perda de qualidade.  
* **Menus e Submenus:** Estão fixos numa camada Z-Index máxima. O UIEventSystem ganha uma regra vital: sempre que existir um *Popup* ativo, se o rato clicar fora das barreiras (AABB) desse submenu, o sistema não propaga o evento (Event Bubbling impedido) e despacha automaticamente o comando para colapsar toda a árvore de menus, imitando o comportamento canónico de sistemas operativos como Windows e macOS.

#### **3\. Integração QuickJS e Sandboxing (Segurança FFI)**

Para que Designers e Modders possam criar lógica de UI interativa sem compilar o C++, acoplamos o **QuickJS**. A SpriteUi atua como cofre:

* **Sandboxing:** Os scripts JS da UI não dispõem dos módulos de sistema (I/O, disco, rede).  
* **Proteção de Memória:** O motor chama JS\_SetMemoryLimit (teto estrito de 32 MB) limitando os danos de *memory leaks* criados nos scripts de interface.  
* **Segurança de Componentes (FFI):** O Javascript manipula a UI apenas trocando Handles opacos (uint64\_t). A engine garante que o componente está vivo via flecs::entity::is\_alive() e impõe limites numéricos finitos antes de deixar o Javascript mover ou mudar a cor de um elemento nativo.

### **25.8 Fase 2.5: Spatial Navigation (Gamepad e Teclado) e Feedback Visual**

Para suportar nativamente comandos de Joysticks (Gamepads) e setas do teclado, a SpriteUi implementa um sistema de **Navegação Espacial (Spatial Navigation)**. O sistema abstrai a origem do input: qualquer API externa (XInput, SDL\_GameController) apenas injeta o comando direcional, e o motor calcula matematicamente qual é o próximo elemento da UI a ser selecionado usando testes de AABB (Bounding Boxes).  
Adicionalmente, introduzimos o UIStateFeedbackComponent, um subcomponente declarativo para reações visuais automáticas (Hover, Select, Cancel), dispensando a escrita de scripts complexos para cada botão.

#### **1\. Abstração de Input e Navegação Espacial**

```c
namespace ToolEngine::SpriteUi::Navigation {

    enum class NavDirection { Up, Down, Left, Right, Submit, Cancel };

    // Componente para indicar que o elemento pode receber foco via Gamepad/Teclado
    struct UIFocusableComponent {
        bool isSelected = false;
        bool isHovered = false; // Pode ser sincronizado com o rato
        
        // Overrides manuais opcionais (se nulo, a engine calcula o mais próximo)
        flecs::entity overrideUp;
        flecs::entity overrideDown;
        flecs::entity overrideLeft;
        flecs::entity overrideRight;
    };

    // Subcomponente de Feedback Visual Automático
    struct UIStateFeedbackComponent {
        vec4 normalColor = {1.0f, 1.0f, 1.0f, 1.0f};
        vec4 hoverColor  = {1.2f, 1.2f, 1.2f, 1.0f}; // Clareia
        vec4 selectColor = {0.5f, 0.8f, 1.0f, 1.0f}; // Cor de Destaque
        
        bool animateOnSelect = true; // Roda o ToonFX::Squash automaticamente
        bool animateOnCancel = false;
    };

    // Estado global de Foco do Gamepad
    struct UINavRoutingState {
        flecs::entity currentSelectedEntity = flecs::entity::null();
        bool inputConsumedThisFrame = false;
    };

    class UINavigationSystem {
    public:
        // API exposta para a Engine injetar inputs do Joystick ou Teclado
        static void InjectNavCommand(flecs::world& world, NavDirection dir) {
            auto* navState = world.get_mut<UINavRoutingState>();
            if (!navState || navState->inputConsumedThisFrame) return;

            flecs::entity current = navState->currentSelectedEntity;
            
            // Lógica de Submit / Cancel
            if (dir == NavDirection::Submit && current.is_alive()) {
                // Despacha evento de clique
                navState->inputConsumedThisFrame = true;
                return;
            }

            // Descobrir o próximo elemento baseado na direção
            flecs::entity bestTarget = flecs::entity::null();
            float bestDistance = FLT_MAX;

            if (current.is_alive()) {
                const auto* currentLayout = current.get<UILayoutRect>();
                vec2 centerA = currentLayout->resolvedPos + (currentLayout->resolvedSize * 0.5f);

                // Pesquisa todos os Focusable
                auto filter = world.filter<const UILayoutRect, UIFocusableComponent>();
                filter.iter([&](flecs::iter& it, const UILayoutRect* layout, UIFocusableComponent* focusable) {
                    for (auto i : it) {
                        flecs::entity candidate = it.entity(i);
                        if (candidate == current) continue;

                        vec2 centerB = layout[i].resolvedPos + (layout[i].resolvedSize * 0.5f);
                        vec2 delta = centerB - centerA;
                        
                        // Validação vetorial de direção (Dot Product simples / Cones de 90 graus)
                        bool validDirection = false;
                        if (dir == NavDirection::Right && delta.x > 0 && abs(delta.x) > abs(delta.y)) validDirection = true;
                        if (dir == NavDirection::Left  && delta.x < 0 && abs(delta.x) > abs(delta.y)) validDirection = true;
                        if (dir == NavDirection::Down  && delta.y > 0 && abs(delta.y) > abs(delta.x)) validDirection = true;
                        if (dir == NavDirection::Up    && delta.y < 0 && abs(delta.y) > abs(delta.x)) validDirection = true;

                        if (validDirection) {
                            float dist = length(delta);
                            if (dist < bestDistance) {
                                bestDistance = dist;
                                bestTarget = candidate;
                            }
                        }
                    }
                });
            } else {
                // Se nada está selecionado, seleciona o primeiro disponível
                auto filter = world.filter<UIFocusableComponent>();
                filter.iter([&](flecs::iter& it, UIFocusableComponent* f) {
                    if (it.count() > 0) bestTarget = it.entity(0);
                });
            }

            // Aplica a nova seleção
            if (bestTarget.is_alive()) {
                if (current.is_alive()) current.get_mut<UIFocusableComponent>()->isSelected = false;
                bestTarget.get_mut<UIFocusableComponent>()->isSelected = true;
                navState->currentSelectedEntity = bestTarget;
                
                // Dispara animação automática sem callbacks verbosos
                if (auto* feedback = bestTarget.get<UIStateFeedbackComponent>()) {
                    if (feedback->animateOnSelect) {
                        ToonMotion::ToonFX::SquashAndStretch(bestTarget, 0.3f);
                    }
                }
            }
            
            navState->inputConsumedThisFrame = true;
        }

        static void RegisterSystem(flecs::world& world) {
            // Sistema que aplica a cor baseada no estado (Normal, Hover, Selected)
            world.system<UIFocusableComponent, const UIStateFeedbackComponent, UIPanelComponent>("UIFeedbackVisualSystem")
                .kind(flecs::OnUpdate)
                .iter([](flecs::iter& it, UIFocusableComponent* focus, const UIStateFeedbackComponent* feedback, UIPanelComponent* panel) {
                    for (auto i : it) {
                        // Ler estado e aplicar clareamento/escurecimento
                        if (focus[i].isSelected) {
                            panel[i].color = feedback[i].selectColor;
                        } else if (focus[i].isHovered) {
                            panel[i].color = feedback[i].hoverColor;
                        } else {
                            panel[i].color = feedback[i].normalColor;
                        }
                    }
                });
        }
    };
}
```

#### **2\. Adaptação no Motor Principal (Joystick e Teclado)**

O motor de jogo apenas necessita de mapear os botões físicos para a injeção abstrata de comandos. O código a seguir deve residir no subsistema de Input externo (fora da UI) e comunicar com a SpriteUi:

```c
void ProcessGameInputs(flecs::world& world) {
    // A SpriteUi não importa APIs como XInput ou SDL. Ela apenas recebe comandos abstratos.
    
    if (Gamepad::IsButtonPressed(GAMEPAD_DPAD_RIGHT) || Keyboard::IsKeyPressed(KEY_RIGHT)) {
        ToolEngine::SpriteUi::Navigation::UINavigationSystem::InjectNavCommand(world, NavDirection::Right);
    }
    else if (Gamepad::IsButtonPressed(GAMEPAD_DPAD_LEFT) || Keyboard::IsKeyPressed(KEY_LEFT)) {
        ToolEngine::SpriteUi::Navigation::UINavigationSystem::InjectNavCommand(world, NavDirection::Left);
    }
    else if (Gamepad::IsButtonPressed(GAMEPAD_DPAD_UP) || Keyboard::IsKeyPressed(KEY_UP)) {
        ToolEngine::SpriteUi::Navigation::UINavigationSystem::InjectNavCommand(world, NavDirection::Up);
    }
    else if (Gamepad::IsButtonPressed(GAMEPAD_DPAD_DOWN) || Keyboard::IsKeyPressed(KEY_DOWN)) {
        ToolEngine::SpriteUi::Navigation::UINavigationSystem::InjectNavCommand(world, NavDirection::Down);
    }
    
    // Botões de Ação
    if (Gamepad::IsButtonPressed(GAMEPAD_BUTTON_A) || Keyboard::IsKeyPressed(KEY_ENTER)) {
        ToolEngine::SpriteUi::Navigation::UINavigationSystem::InjectNavCommand(world, NavDirection::Submit);
    }
}
```

Deste modo, qualquer elemento de UI torna-se automaticamente percorrível matematicamente. O sistema de *cones de 90 graus* (vetores de direção em InjectNavCommand) garante que, independentemente da bagunça do *layout*, a engine encontra sempre o botão físico mais próximo na direção que o jogador escolheu, suportando arcades, comandos e teclados de forma nativa e sem *hardcoding* de navegação em cada tela.  
---

### **25.9 Fase 2.6: Transição ImGui e SDL para SpriteUi (Substituição Modular AAA)**

Para que a **SpriteUi** possa substituir integralmente o Dear ImGui 1.91.9b e o SDL3, sem perda de funcionalidades e mantendo a arquitetura de **Data-Oriented Design (DOD)**, mapeámos todos os recursos do antigo sistema para os novos componentes ECS. O objetivo é garantir que não existam retrocessos e que recursos como o layout de painéis e o editor de grafos continuem a funcionar de forma nativa e otimizada. A SpriteUi é **100% modular**, permitindo desativar recursos desnecessários na compilação. As partículas do jogo continuarão a ser processadas no mundo espacial (Light Prism), mantendo o UIParticleEmitterComponent estritamente opcional.

#### **1\. Mapeamento Direto de Recursos (ImGui \-\> SpriteUi)**

A tabela abaixo define os contratos de substituição de todos os widgets e subsistemas para garantir que as regras do editor (R1 a R8) sejam cumpridas integralmente:

| Recurso ImGui / SDL | Componente / Sistema SpriteUi | Estratégia de Implementação (ECS) |
| :---- | :---- | :---- |
| **Janelas e Docking** (Begin/End, Layout .ini) | UIDockSpace, UIWindowComponent | Layout gerido por UIFlexLayout. Janelas têm IDs fixos no ECS para preservar o estado. O layout é serializado/desserializado para .json usando o Flecs JSON API, garantindo a Regra R1 (Salvar layout) e R2 (Travar interface). |
| **Widgets Básicos** (Button, Checkbox, Sliders) | UIButtonComponent, UIToggleComponent, UISliderComponent | A SpriteUi substitui os *returns* no clique imediato pelo motor de Eventos (Event Bubbling) ou pelo *Data Binding* (UIFloatBinding), onde a interface altera diretamente o estado e vice-versa. |
| **Inputs de Texto** (InputText, InputFloat) | UITextInputComponent, UITextCacheComponent | Substitui os *buffers* estáticos do ImGui. Usa o HarfBuzz para fontes HD (Regra R3). O UIPointerRoutingState gerencia a captura de foco, garantindo que atalhos (Delete, F2) sejam bloqueados quando o utilizador escreve (Regra R4). |
| **Tabelas, Listas e Clippings** (ImGuiListClipper, BeginTable) | UIScrollViewComponent, UIDataGrid | Essencial para as Regras R8 (Performance com \>300 entidades). A SpriteUi virtualiza listas: apenas as entidades visíveis no UILayoutRect recebem instâncias GPU. As células usam o UIFlexLayout. |
| **Textos e Localização** (T(), W(), .json) | UITextComponent integrado ao I18nSystem | IDs fixos no ECS. Quando o idioma muda (Regra R5), apenas a *string* do UITextComponent muda, o ID da entidade e o seu layout permanecem intocados. |
| **Overlays e Viewport** (ImDrawList, ImGui::Image) | UIPanelComponent, Sistema de Renderização ECS | Retângulos (Gizmos) são desenhados como UIPanelComponent. A cena 3D (Render Target) é renderizada num UIPanelComponent usando o *textureId* da RHI, substituindo o ImGui::Image. |
| **Grafos e Nodes** (ImNodes) | UINodeGraphComponent, UILineRendererComponent | A SpriteUi suporta nativamente renderização vetorial SDF de linhas de conexão e janelas agrupáveis. Não é necessário manter o ImNodes como biblioteca separada. |
| **Arrastar e Soltar** (DragDrop) | UIDragDropState, UIDragDropTarget | Sistema de *Drag and Drop* integrado ao roteamento espacial (UINavigationSystem), usando chaves de string ("HIER\_ENTITY", "ASSET\_PATH") para identificar os *payloads*. |

#### **2\. Modularidade e Desativação de Recursos**

Como requerido, a SpriteUi não impõe *overhead* de recursos não utilizados. Sistemas como o ToonMotionSystem, UIAcrylicExtractionSystem ou UIParticleEmitterComponent podem ser desligados durante a fase de inicialização do Flecs, bastando não chamar o seu método RegisterSystem(). Isto garante que a UI 2D permaneça estritamente focada na renderização da interface e que as partículas (fx.hpp) continuem exclusivas ao motor 3D.

#### **3\. Etapas de Migração Suportadas Nativamente**

A SpriteUi foi construída para respeitar o **Plano de troca (Seção 6\)** do documento técnico atual:

17. **Desenho de sprites (fx.hpp):** Substituição direta do ImDrawList pela instanciação UIPanelComponent no UiCanvas do jogo.  
18. **UI do Jogo:** Renderizada separadamente sem interferir nos painéis do editor (Regra R7).  
19. **Viewport 3D:** Substituição do ImGui::Image.  
20. **Painéis (Console, Assets, Hierarquia):** Construídos com UIFlexLayout e virtualização (UIScrollViewComponent), substituindo as árvores do ImGui.  
21. **Layout e Docking:** Substituição completa usando UIWindowInteractionSystem e UIDockSpace.  
22. **Grafos:** A renderização por *SDF Lines* substitui o ImNodes.

### **25.10 Fase 2.7: Integração Nativa JSON para Configurações (Editor e Jogo)**

Para garantir que configurações do editor (editor\_layout.ini, editor\_theme.json) e do jogo sejam persistidas de forma declarativa e compatível com a arquitetura DOD, a SpriteUi integra a serialização de dados via **JSON**. Aproveitamos a biblioteca nlohmann/json (ou o próprio addon JSON do Flecs) para transformar o estado das entidades (ex: posição de janelas, propriedades de temas) em arquivos legíveis.  
Esta arquitetura remove a necessidade de parses manuais e esparguete de configuração, garantindo que o layout e as preferências de navegação sejam salvos com segurança.

#### **1\. Mapeamento de Configurações via ECS e JSON**

```c
namespace ToolEngine::SpriteUi::Config {

    // Estrutura global de configurações da UI
    struct UISettingsComponent {
        float globalUIScale = 1.0f;
        int activeThemeIndex = 0;
        bool enableVSync = true;
    };

    class UIConfigManager {
    public:
        // Exemplo utilizando nlohmann/json para salvar o layout de janelas
        static void SaveEditorLayout(flecs::world& world, const std::string& filepath) {
            nlohmann::json layoutData;
            
            // Busca todas as janelas do editor ativas no ECS
            auto query = world.query<const UIWindowComponent, const UIRectTransform>();
            query.iter([&layoutData](flecs::iter& it, const UIWindowComponent* win, const UIRectTransform* transform) {
                for (auto i : it) {
                    std::string entityName = it.entity(i).name().c_str();
                    layoutData["Windows"][entityName] = {
                        {"offsetMinX", transform[i].offsetMin.x},
                        {"offsetMinY", transform[i].offsetMin.y},
                        {"offsetMaxX", transform[i].offsetMax.x},
                        {"offsetMaxY", transform[i].offsetMax.y},
                        {"isMinimized", win[i].isMinimized}
                    };
                }
            });

            // Persiste em disco (I/O assíncrono recomendado em produção)
            std::ofstream file(filepath);
            if (file.is_open()) {
                file << layoutData.dump(4); // Indentação de 4 espaços
            }
        }

        static void LoadEditorLayout(flecs::world& world, const std::string& filepath) {
            std::ifstream file(filepath);
            if (!file.is_open()) return;

            nlohmann::json layoutData;
            file >> layoutData;

            if (layoutData.contains("Windows")) {
                for (auto& [windowName, windowConfig] : layoutData["Windows"].items()) {
                    flecs::entity e = world.lookup(windowName.c_str());
                    if (e.is_alive() && e.has<UIRectTransform>()) {
                        auto* t = e.get_mut<UIRectTransform>();
                        t->offsetMin.x = windowConfig["offsetMinX"];
                        t->offsetMin.y = windowConfig["offsetMinY"];
                        t->offsetMax.x = windowConfig["offsetMaxX"];
                        t->offsetMax.y = windowConfig["offsetMaxY"];
                        
                        if (e.has<UIWindowComponent>()) {
                            e.get_mut<UIWindowComponent>()->isMinimized = windowConfig["isMinimized"];
                        }
                    }
                }
            }
        }
    };
}
```

### **25.11 Fase 3.5: Modularidade de Iluminação (Bypass do Light Prism 3D)**

Um dos requisitos fundamentais da arquitetura é evitar que a UI 2D crie problemas de acoplamento com o renderizador 3D, especificamente a sobrecarga do **Light Prism 3D**. O desenvolvedor deve ser capaz de compilar a SpriteUi estritamente para o HUD do jogo ou ferramentas (Totalmente **Unlit**).  
Para resolver isto, garantimos que o código do *Shader* e a extração do ECS obedeçam a uma diretiva de pré-processador (\#ifdef). Se o módulo de iluminação não for injetado, todo o processamento de PBR, Normal Maps e *Clustered Shading* na interface é ignorado, custando literalmente zero recursos de GPU e CPU.

#### **1\. HLSL: Diretivas do Pré-Processador para Modularidade**

No arquivo SpriteShader.hlsli, usamos a diretiva \#ifdef ENABLE\_LIGHT\_PRISM para isolar os cálculos pesados de sombra e penumbra 2D:

```
// SpriteShader.hlsli - Estrutura Dinâmica e Modular

float4 PSMain(PSInput input) : SV_TARGET {
    float4 texColor = MainTexture.Sample(MainSampler, input.UV);
    float3 finalRGB = texColor.rgb;
    
    // Se a compilação do shader habilitar o Light Prism, inclui-se os cálculos
#ifdef ENABLE_LIGHT_PRISM
    float3 normalSample = NormalTexture.Sample(MainSampler, input.UV).rgb * 2.0 - 1.0;
    float3 N = normalize(lerp(float3(0, 0, 1), normalSample, input.NormalIntensity));
    
    float3 totalDiffuse = AmbientLightColor.rgb;
    for (uint i = 0; i < ActiveLightCount; ++i) {
        float2 lightPos = PointLightPosAndRadius[i].xy;
        float radius = PointLightPosAndRadius[i].z;
        float dist = distance(lightPos, input.WorldPos);
        
        if (dist < radius) {
            float atten = saturate(1.0 - (dist / radius));
            atten *= atten;
            float3 L = normalize(float3(lightPos - input.WorldPos, 15.0));
            float NdotL = max(0.0, dot(N, L));
            totalDiffuse += PointLightColorAndIntensity[i].rgb * (NdotL * atten * PointLightColorAndIntensity[i].w);
        }
    }
    
    // Se for Unlit, o emissivo apenas brilha, se tiver Light Prism, afeta a cena
    finalRGB = (finalRGB * totalDiffuse) + (finalRGB * input.Emissive);
#else
    // Fallback Ultra-Rápido (Unlit mode para editor / HUD simples)
    finalRGB += (finalRGB * input.Emissive);
#endif

    float lum = dot(finalRGB, float3(0.2126, 0.7152, 0.0722));
    float3 desatColor = lerp(finalRGB, float3(lum, lum, lum), input.Desaturation);

    return float4(desatColor, texColor.a) * input.ColorTint;
}
```

#### **2\. C++: Configuração de Módulo Opcional (Opt-in)**

No momento de injetar a SpriteUi no motor do jogo, as luzes só são registadas se o utilizador chamar explicitamente a função correspondente.

```c
namespace ToolEngine::SpriteUi {
    void Initialize(flecs::world& world, bool enable3DLighting = false) {
        // 1. Registar Core (Sempre ativo: Matemática, Layout, Input)
        RegisterCore(world);
        Widgets::UIWidgetSystem::RegisterSystem(world);
        Navigation::UINavigationSystem::RegisterSystem(world);
        
        // 2. Registar Iluminação (Opcional - Protege o motor contra dependências cruzadas)
        if (enable3DLighting) {
            // Este sistema só vai iterar se for ativado. Ele carrega os Constant Buffers do Light Prism
            Materials::UILightingSystem::RegisterSystem(world);
            
            // Opcionalmente compilaria o PSO (Pipeline State Object) com a Macro ENABLE_LIGHT_PRISM
            }
    }
}
```

Deste modo, a funcionalidade central de construção de UI fica separada do complexo motor gráfico de *Clustered Shading*, cumprindo o requisito estrutural de que não existem ferramentas ligadas sem necessidade.  
---

# **SpriteUI — Arquitetura Corrigida e Especificação de Implementação (V4)**

**Status:** especificação de referência para implementação  
**Data:** 03/11/2026  
**Objetivo:** converter as ideias úteis da V3 em contratos testáveis, sem presumir que os exemplos originais já são código funcional.

## **1\. Princípios e decisões**

23. **Core independente:** matemática, layout, roteamento de input, texto e comandos de desenho não dependem diretamente de uma API gráfica.  
24. **Flecs como adaptador ECS:** a integração com Flecs organiza estado persistente, mas o core pode ser testado sem criar um mundo Flecs.  
25. **Backend isolado:** Diligent Engine implementa o backend de GPU; nenhum componente ECS armazena ponteiro cru para recurso da RHI.  
26. **Screen-Space e In-World separados:** HUD/editor é unlit por padrão; UI diegética pode consumir iluminação Light Prism.  
27. **POD no caminho quente:** dados extraídos para GPU são triviais, contíguos e com ABI verificada. Callbacks, strings e serviços pertencem a sistemas/arenas externos.  
28. **Ordem visual determinística:** a ordem de pintura precede a otimização por material; batches nunca podem mudar a composição de elementos translúcidos.  
29. **Otimização medida:** dirty flags, cache e batching são validados com perfil em hardware real.

## **2\. Módulos do projeto**

(Estrutura modular recomendada via CMake, separando Core, Layout, Input, Text, Animation, Rendering, Resources, ECS e Integrations). Dependências estritas incluem Flecs, Diligent Engine, HarfBuzz, FreeType, msdfgen, KTX-Software e SDL/GLFW.

## **3\. Contrato de coordenadas e transformações**

Separação estrita de configuração (UIRectTransform), retângulo resolvido (UILayoutRect) e matriz final.

* Anchors pertencem ao intervalo \[0,1\].  
* O hit-test transforma o ponteiro para o espaço local antes de testar a AABB, nunca comparando posições absolutas do mouse com offsets relativos.

## **4\. Flex layout com limites**

* A distribuição de espaço desconta margins, gaps e padding antes do cálculo de crescimento.  
* Distinção explícita entre base size, min size e max size.  
* O algoritmo aplica clamp nos limites, congela os itens que atingiram seu máximo/mínimo e redistribui o espaço remanescente iterativamente.

## **5\. Input, foco, captura e eventos**

Remoção de callbacks std::function dos componentes POD.  
Introdução de UIInputSnapshot para o frame bruto e estruturas UIEvent e UIPointerRoutingState para roteamento centralizado. O hit-test usa a ordem visual reversa (estável). Captura e Foco são entidades fixadas no roteador para garantir estabilidade no drag e digitação.

## **6\. Texto, shaping e edição Unicode**

Pipeline robusto:  
UTF-8 input \-\> HarfBuzz shaping \-\> Glyph IDs \-\> msdfgen atlas \-\> Text Layout.  
A seleção e o cursor (caret) respeitam os limites de grapheme, enquanto a cobertura MSDF no shader escala com as derivadas UV e faixa de distância exata.

## **7\. ToonMotion revisado**

Uso de propriedades explícitas (TweenProperty) e políticas de conflito (TweenConflict), eliminando interpolações manuais sem controle de relógio. Duração zero aplica estado final, animações notificam filas de eventos sem causar falhas no iterador do Flecs.

## **8\. ABI de instância GPU (O Elo CPU-GPU corrigido)**

A estrutura C++ (SpriteInstanceGPU) agora reflete exatamente o layout em HLSL:  
80 bytes alinhados contendo:

* transformRow0 (float4: m00, m01, tx, depth)  
* transformRow1 (float4: m10, m11, ty, padding)  
* uvRect (float4: u, v, w, h)  
* tint (float4: R, G, B, A)  
* resourceIds (uint4: textureIndex, materialIndex, clipIndex, flags)

## **9\. Alpha e composição**

Definição padronizada de *Source-Over* Premultiplicado. O pipeline assume RGB em espaço linear pré-multiplicado. Blend state obrigatório na submissão: Source ONE, Destination INV\_SRC\_ALPHA.

## **10\. SDF e Batching**

SDF shadow com offset, softness parametrizada e smoothstep correto (min, max). A ordem de extração prioriza Canvas/Layer visual. Batching apenas mescla comandos consecutivos, evitando o risco de quebrar a profundidade visual por classificar cegamente apenas por material.

## **11\. Integrações (Light Prism e Newtoon)**

* UI 2D é *unlit* por padrão. Integração iluminada apenas em *In-World UI*, limitando-se ao indexador clusterizado.  
* Integração física (Newtoon) será um adaptador unidirecional (snapshot) no ECS da engine principal.

## **12\. Ordem de Implementação (Roadmap Final)**

O roadmap abaixo foi atualizado para refletir o estado real do repositório, não apenas os exemplos históricos das seções anteriores:

1. **Contratos e build — concluído para o core:** ABI e contratos de CPU validados; build C++20, headers públicos autocontidos, testes e compilação offline HLSL verificados em MSVC/DXC nesta máquina.
2. **Core visual CPU — concluído:** transformação, árvore e motor de layout, clipping, render extraction, batching adjacente, Nine-Slice e referência SDF. Ainda não há submissão a uma GPU real.
3. **Input, navegação e widgets lógicos — implementado no core:** roteamento de ponteiro primário, foco/captura, bubbling, navegação semântica, janelas, docking, popup e virtualização. Integração de janela/OS, multi-pointer e desenho de widgets prontos continuam pendentes.
4. **Texto — layout implementado; pipeline de fontes pendente:** layout de glifos, quebra de linha, caret e conversão para itens de renderização estão no core. Shaping real HarfBuzz, carregamento de fontes FreeType, geração/gestão de atlas e RTL/bidi não estão integrados.
5. **Animação e bindings — núcleo implementado:** ToonMotion e bindings reativos estão disponíveis; timeline/keyframes e integração com ECS permanecem pendentes.
6. **Integrações de engine — pendente:** adaptadores Flecs e Diligent, buffers/fences, janela nativa e integração de iluminação Light Prism.
7. **Produção — pendente:** scheduler de atualização on-demand orientado por dirty flags, efeitos e recursos adicionais (blur, bloom, sombras, KTX2/Basis, partículas e QuickJS/FFI), CI multiplataforma, benchmarks e validação em GPU real. A substituição do ImGui depende da definição de escopo e de integração em uma aplicação hospedeira.
---

## **13\. Sumário de Auditoria e Próximos Passos**

O core deixou de estar somente nas Fases 0 e 1: as capacidades de layout, input, widgets lógicos, texto geométrico, animação, configuração, extração e testes avançaram. O próximo marco arquitetural de runtime é integrar um backend gráfico e um adaptador ECS sem acoplar essas dependências ao core. Antes de declarar uma release de produto, também é necessário fechar CI e validação em consumidores e hardware-alvo.

## **14\. Estado conferido do projeto (09/11/2026)**

Versão declarada pelo CMake: **0.6.0**. Este estado descreve o que foi encontrado no repositório e verificado localmente; não representa uma release publicada.

| Etapa / módulo | Estado conferido |
|---|---|
| Core C++20, matemática, handles e ABI `SpriteInstanceGPU` de 80 bytes | Implementado; contrato protegido por `static_assert`. |
| Layout | `Measure`/`Arrange`, Row/Column, Fixed/Percent/Auto/Flex, grow/shrink com min/max, `justifyContent`, wrap, `alignContent` e baseline implementados. Não equivale a todo o CSS Flexbox. |
| Cena e render extraction | Árvore, bridge, clipping hierárquico, ordem de pintura, validação de pacote e batches adjacentes implementados. `NullRenderBackend` não submete à GPU. |
| Renderização 2D auxiliar | Nine-Slice e painéis SDF arredondados implementados no core; Nine-Slice ainda não está integrado ao `LayoutSceneBridge`/`ElementVisual`. |
| Input, navegação e widgets | Roteamento e lógica de widgets disponíveis; target SDL3 opcional com wrapper RAII de janela, mouse/foco, navegação por teclado/gamepad, texto/composição IME e clipboard implementado. Input multi-pointer e kit completo de widgets visuais continuam pendentes. |
| Shader MSDF | `pxRange` do atlas convertido para pixels de tela usando dimensões da textura e derivadas de UV; compilação DXIL verificada. Paridade visual em GPU real pendente. |
| Texto | Layout de glifos shaped, quebra por palavra/cluster no subconjunto implementado, linhas, caret e hit/insertion offset disponíveis. HarfBuzz/FreeType e bidi/RTL não estão integrados. |
| Animação, bindings, persistência | ToonMotion, bindings e parser/serializador JSON estrito implementados; timeline/ECS não. |
| Atualização on-demand | Dirty flags existem; orquestração incremental de layout/renderização e orçamento de desempenho ainda não foram validados numa engine hospedeira. |
| Shader | Compilação offline DXIL verificada com DXC nesta máquina. Isso não valida a execução nem a paridade em GPU real. |
| Build e testes locais | Build Debug MSVC, `spriteui_check_headers`, CTest e exemplos CLI passaram; executável reportou 727 checks. O adaptador SDL3 também compilou com SDL 3.2.30 e passou os testes de input, clipboard e pacote consumidor. |

### Etapas ainda necessárias para o escopo de produção planejado

1. Definir se a primeira entrega é o core sem backend ou uma GUI integrada; documentar o limite e a política de versão.
2. Implementar e testar backend Diligent com gestão de recursos, buffers/ring, fences, blending e clipping; validar em GPU e comparar com as referências visuais.
3. Criar adaptador Flecs e completar input multi-pointer/gestão avançada de plataforma; SDL3 já oferece wrapper básico de janela, mouse/foco, navegação, texto/IME e clipboard.
4. Integrar fontes reais (FreeType + HarfBuzz), atlas MSDF, cache e ciclo de vida; testar Unicode fora do escopo atual, incluindo bidi/RTL, no nível definido pelo produto.
5. Ligar a lógica dos widgets ao desenho/estilo e completar o fluxo de docking drag-and-drop, se fizerem parte do produto.
6. Implementar as integrações opt-in de Light Prism (ClusterLit/normal maps) e efeitos planejados (Acrylic Blur, Bloom e sombras).
7. Decidir e, se necessário, implementar KTX2/Basis/streaming, partículas, nodes de editor e QuickJS/FFI sandbox.
8. Adicionar CI para MSVC, Clang e GCC; sanitizers/fuzz contínuos; benchmarks; cobertura; teste de instalação com `find_package`; e instruções de contribuição/segurança.

### Evidência e limites da validação

Validação local em Windows/MSVC: `cmake --build build --parallel`, `cmake --build build --target spriteui_check_headers --parallel`, `ctest --test-dir build --output-on-failure`, execução de `spriteui_tests.exe` (**727 checks**) e dos dois exemplos; DXC gerou os shaders VS/PS DXIL. Não foram verificados nesta rodada Clang/GCC, sanitizers, CI, backend de GPU real ou integração com Flecs/SDL/Diligent.