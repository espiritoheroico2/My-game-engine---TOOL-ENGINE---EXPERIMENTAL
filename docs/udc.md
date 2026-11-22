
## Plano de implementação e evolução do sistema de renderização
### Engenharia de software, pipeline gráfico, otimização de Draw Calls e arquitetura multiplataforma

**Projeto:** TOOL ENGINE  
**Repositório de referência:** https://github.com/espiritoheroico2/toolgameengine  
**Base técnica:** arquitetura C++ da engine, RHI próprio, integração com Diligent Engine, Light Prism e renderizador multipasse.  
**Objetivo:** reduzir submissões redundantes e o custo total de CPU/GPU sem reescrever integralmente o renderizador nem comprometer a portabilidade.  
**Escopo:** renderização 3D, batching, instancing, Multi-Draw, iluminação, sombras, culling, SSAO, emissão/Bloom, transparência, RHI, sincronização, Profiler e backends gráficos.  
**Natureza:** documento de engenharia e plano de implementação. Os códigos são contratos/exemplos; precisam ser adaptados aos tipos reais, compilados e testados antes de serem considerados implementação pronta.

---

# 1. Resumo executivo

A TOOL ENGINE já possui uma base funcional importante: renderizador multipasse, RHI próprio, integração com Diligent, Light Prism, instancing no passe opaco, prepass para SSAO, CSM, sombras de Point/Spot, emissão, transparência e Bloom.

A meta não é substituir tudo por outro renderizador. É evoluir o caminho existente, separar preparação de execução e tornar cada custo mensurável.

A arquitetura proposta organiza o quadro em:

1. coleta dos objetos e recursos da cena;
2. atualização de transformações, bounds e revisões;
3. culling por câmera e por região de sombra;
4. construção de filas específicas por passe;
5. agrupamento por compatibilidade real de pipeline/recursos;
6. preparação de dados de instância e comandos;
7. execução dos passes na ordem correta;
8. coleta de métricas por quadro, vista, passe, luz e face;
9. comparação visual e benchmark reproduzível.

```text
Scene / ECS
    |
    v
RenderSceneCollector
    |  RenderItem + bounds + mesh/material/resource handles
    v
VisibilitySystem
    |  camera frustum / CSM cascade / point face / spot frustum
    v
RenderQueueBuilder
    |  opaque / depth-prepass / shadow / emission / transparent
    v
BatchBuilder + State Sorter
    |  pipeline + mesh + resource set + compatible state
    v
Instance Data / Multi-Draw Preparation
    |
    v
RenderPassExecutor
    |  shadow -> prepass/AO -> opaque -> emission -> transparent -> post FX
    v
RHI (IDevice / ICommandList)
    |
    v
Diligent backend
    |
    +-- Vulkan
    +-- Direct3D 12
    +-- Direct3D 11
    +-- OpenGL / OpenGL ES, conforme suporte efetivo da versão/configuração
```

## 1.1 Diagnóstico principal

A auditoria anterior identificou uma arquitetura multipasse na qual:

- o passe opaco já utiliza batching/instancing; portanto, não deve ser reimplementado do zero;
- CSM repete a submissão de geometria por cascata;
- Point Lights podem repetir a geometria pelas seis faces do cubemap;
- os caminhos de sombra não aproveitam ainda culling espacial suficientemente específico por cascata/face;
- emissão e transparência ainda possuem submissões individuais em seus caminhos identificados;
- o backend Diligent executa `Flush()` no caminho de `submit()`, o que merece medição antes de qualquer alteração;
- as estatísticas do dispositivo são reiniciadas em `present()`, o que pode prejudicar a separação de múltiplas vistas;
- faltam medições confiáveis de GPU por passe no Profiler.

A captura do editor apresentada anteriormente mostrou **109 Draw Calls, 11 objetos desenhados e 802 triângulos**. Isso é um sinal para investigação, não uma decomposição causal. Não é correto concluir que os 109 draws vieram somente dos 11 objetos: podem existir chamadas de sombras, fullscreen passes, grid, gizmos, overlays, Scene/Game e outros elementos do editor. A primeira entrega precisa identificar a origem de cada chamada.

## 1.2 Princípio central

**O objetivo não é simplesmente fazer o contador de Draw Calls ficar baixo. É reduzir o custo total de renderização.**

Uma solução pode diminuir chamadas de API e, ao mesmo tempo, aumentar:
- trabalho de vertex shader;
- fragmentos e overdraw;
- largura de banda;
- sincronização;
- cópias de buffers;
- memória de GPU;
- latência de quadro.

Por isso, toda otimização deve ser avaliada com Draw Calls, CPU time, GPU time, bytes enviados, mudanças de estado, instâncias, triângulos e equivalência visual.

## 1.3 Ordem de prioridade

1. P0 — telemetria correta por Frame/View/Pass.
2. P1 — bounds, culling da câmera e culling por região de sombra.
3. P2 — filas, batching e instancing por passe.
4. P3 — cache e atualização seletiva de sombras.
5. P4 — redução de state changes e análise de submissão/Flush.
6. P5 — MultiDraw/MDI e recursos avançados do RHI, com fallback.
7. P6 — GPU-driven rendering, compute culling e indirect draw, somente após medir o caminho CPU.

---

# 2. Diagnóstico técnico: onde podem nascer as Draw Calls

## 2.1 Draw Call não é sinônimo de objeto

Um objeto pode gerar:
- zero draws, se estiver invisível ou não participar de um passe;
- um draw, se for desenhado em um único passe;
- vários draws, se participar de sombras, prepass, opaco, emissão e outros passes;
- vários draws por face/cascata de sombra.

Da mesma forma, um draw pode representar muitas instâncias de uma mesma malha.

A contagem também depende do nível observado:
- **Render-level draw:** comando de desenho emitido pelo renderizador;
- **RHI draw:** chamada feita na interface abstrata;
- **backend draw:** operação encaminhada à Diligent;
- **native/API draw:** operação efetiva no backend/API;
- **GPU work:** trabalho realmente executado pela GPU.

Esses números não são necessariamente iguais. Uma operação Multi-Draw pode representar vários sub-draws dentro de uma chamada de nível superior. O Profiler deve deixar explícito qual nível está contando.

## 2.2 Passes identificados

Conforme os caminhos condicionais já examinados no renderizador, os principais candidatos são:

1. CSM shadow: uma execução por cascata;
2. Point/Spot shadow: execução por luz e face;
3. depth/normal prepass para SSAO;
4. SSAO fullscreen;
5. blur de SSAO fullscreen;
6. PBR opaco instanciado;
7. emissão;
8. transparência ordenada;
9. Bloom: extração/threshold, blur horizontal, blur vertical e composição;
10. recursos de editor, como grid, gizmos, seleção, linhas e overlays, que devem ser medidos separadamente se passarem pelo mesmo dispositivo.

Nem todos rodam em todos os quadros. O Profiler deve registrar também passes ignorados e a razão: efeito desligado, fila vazia, cache válido, recurso indisponível etc.

## 2.3 Custo de sombras

A aproximação de submissões de geometria é:

```text
shadowDraws =
    sum(csmBatchesVisiblePerCascade)
  + sum(pointSpotBatchesVisiblePerFace)
```

No pior caso simplificado:

```text
shadowDraws ~= shadowBatchCount *
    (csmCascadeCount + sum(pointLightFaces))
```

Exemplo hipotético: 8 lotes, 4 cascatas e duas Point Lights com 6 faces:

```text
8 * (4 + 6 + 6) = 128 submissões de lotes
```

Isso não é uma medição da cena. É uma estimativa que demonstra por que o culling por região é importante.

**Melhoria prioritária:** construir uma fila de casters independente para cada cascata CSM e para cada face de Point Light. O conjunto visível pela câmera não substitui essas filas: um objeto fora da câmera pode projetar sombra dentro da imagem.

## 2.4 Emissão e transparência

Emissão é candidata a instancing porque os objetos compartilham frequentemente o mesmo pipeline e podem variar por dados de instância.

Transparência requer cuidado. A ordenação back-to-front não pode ser destruída por um sort global de material/mesh. A primeira otimização segura é agrupar somente elementos adjacentes na ordem já ordenada e com estado compatível.

## 2.5 Submissão e Flush

O caminho observado no backend Diligent chama `ctx->Flush()` em `submit()`. Isso pode introduzir overhead de CPU ou alterar o ritmo de submissão. Não se deve simplesmente remover o Flush: é necessário verificar o contrato do contexto, transições, recursos importados da Light Prism e comportamento em cada backend.

## 2.6 Limite da conclusão atual

Os achados acima identificam oportunidades arquiteturais, mas não provam qual passe responde pela maior parcela dos 109 Draw Calls. Essa resposta depende da instrumentação por passe e de uma captura reproduzível. O documento não deve registrar uma decomposição numérica que ainda não foi medida.

---

# 3. Arquitetura-alvo

A separação deve começar por funções e estruturas testáveis. Não é necessário criar todos os módulos/classes no primeiro commit.

## 3.1 RenderView

Uma execução de renderização para uma câmera e um alvo específicos. Scene e Game são vistas diferentes.

```cpp
struct RenderView {
    ViewId id;
    Camera camera;
    Mat4 view;
    Mat4 projection;
    Mat4 viewProjection;
    u32 width = 1;
    u32 height = 1;
    rhi::TextureId colorTarget;
    rhi::TextureId depthTarget;
    bool editorView = false;
};
```

A vista possui viewport, alvo, parâmetros de câmera e estatísticas próprias. Recursos dependentes de câmera devem ser indexados pela vista. Mapas Point/Spot podem ser compartilhados quando sua validade realmente independer da vista.

## 3.2 RenderItem

Representação preparada e estável de um item naquele quadro:

```cpp
struct RenderItem {
    MeshId mesh;
    MaterialId material;
    PipelineId pipeline;
    ResourceSetId resources;
    Mat4 world;
    Bounds worldBounds;
    u64 objectId;
    u32 transformRevision;
    u32 meshRevision;
    u32 materialRevision;
    RenderFlags flags;
};
```

Preferir IDs/handles estáveis a ponteiros como identidade de cache. Separar dados variáveis por objeto dos recursos compartilháveis.

## 3.3 RenderSceneCollector

Coleta entidades com malha, transformação e material. Obtém matriz mundial, bounds, recursos e flags. Não executa GPU, não faz `submit()` e não decide diretamente a ordem de todos os passes.

Reutilizar memória de vetores e evitar alocação por entidade a cada quadro.

## 3.4 VisibilitySystem

Executa testes contra:
- frustum da câmera;
- frustum de cada cascata CSM;
- frustum de cada face Point;
- frustum/cone de cada Spot;
- opcionalmente volumes de distância/LOD.

O resultado precisa ser conservador: falsos positivos custam desempenho; falsos negativos causam objetos ou sombras desaparecendo.

## 3.5 RenderQueueBuilder

Constrói filas usando índices para uma lista central de RenderItems:

```cpp
struct RenderQueues {
    Vector<RenderItemIndex> opaque;
    Vector<RenderItemIndex> transparent;
    Vector<RenderItemIndex> emissive;
    Vector<RenderItemIndex> prepass;
    Vector<ShadowQueue> csm;
    Vector<PointShadowQueue> pointShadowFaces;
    Vector<SpotShadowQueue> spotShadow;
};
```

Cada passe tem critérios próprios. Não usar a fila opaca como fila universal.

## 3.6 BatchBuilder

Agrupa somente itens compatíveis com o estado efetivamente configurado no pipeline.

```cpp
struct BatchKey {
    PipelineId pipeline;
    MeshId mesh;
    ResourceSetId resources;
    u32 rasterState;
    u32 depthStencilState;
    u32 blendState;
};
```

O `BatchKey` deve ser derivado dos recursos realmente bindados. Se parâmetros de material estão em dados por instância, não precisam fragmentar o lote. Se o shader precisa de uma textura diferente bindada por material, essa diferença precisa ser representada.

Não adicionar campos à chave sem necessidade: chaves excessivas fragmentam lotes. Não omitir estado que muda o resultado: isso cria batching incorreto.

## 3.7 RenderPassExecutor

Executa filas e lotes já preparados. Não consulta ECS nem recalcula bounds. Configura pipeline/recursos, executa comandos e registra estatísticas.

## 3.8 Render State Cache

Adicionar um cache de estado por command context para evitar binds repetidos de estados idênticos.

Estados candidatos:
- pipeline;
- vertex/index buffers;
- descriptor/resource sets;
- viewport/scissor;
- blend/depth/raster state, se a abstração os separar;
- sampler e recursos associados.

Exemplo conceitual:

```cpp
struct CommandStateCache {
    PipelineId pipeline = InvalidPipeline;
    BufferId vertexBuffer = InvalidBuffer;
    BufferId indexBuffer = InvalidBuffer;
    ResourceSetId resources = InvalidResourceSet;
    Viewport viewport{};
    ScissorRect scissor{};
};

void bindPipelineIfChanged(
    ICommandList& cmd,
    CommandStateCache& cache,
    PipelineId next)
{
    if (cache.pipeline == next)
        return;

    cmd.setPipeline(next);
    cache.pipeline = next;
}
```

O cache deve ser invalidado ao:
- iniciar uma command list/contexto novo;
- trocar de contexto;
- executar operação externa que altera estado;
- mudar de render target quando isso afeta estado associado;
- ocorrer reset/perda/troca de dispositivo.

Não confiar em estado cacheado se a Diligent ou um subsistema externo puder alterar o mesmo contexto sem informar o renderizador. Recursos importados da Light Prism exigem contrato explícito de ownership/estado.

## 3.9 RenderStats

```cpp
enum class RenderPassId : u8 {
    CsmShadow, PointShadow, SpotShadow,
    DepthNormalPrepass, Ssao, SsaoBlur,
    Opaque, Emission, Transparent,
    BloomExtract, BloomBlurH, BloomBlurV, BloomComposite,
    EditorGrid, Gizmos, UIOverlay,
    Count
};

struct PassStats {
    u32 drawCalls = 0;          // chamadas de draw no nível definido
    u32 subDraws = 0;            // sub-draws quando Multi-Draw for usado
    u32 instances = 0;
    u32 visibleItems = 0;
    u32 culledItems = 0;
    u64 trianglesSubmitted = 0;
    u32 pipelineChanges = 0;
    u32 resourceBinds = 0;
    u64 bufferUploadBytes = 0;
    double cpuSubmitMs = 0.0;
    double gpuTimeMs = -1.0;
};
```

Manter métricas distintas para draw calls de nível RHI e sub-draws. Não somar as duas como se fossem a mesma grandeza.

---

# 4. Pipeline de quadro e dependências

```text
BeginFrame
  |
  +-- BeginView(Scene/Game)
        |
        +-- Collect items
        +-- Update world transforms / bounds / revisions
        +-- Camera visibility
        +-- Build shadow queues by cascade/face
        +-- Validate shadow cache
        +-- Build pass queues
        +-- Sort opaque by compatible state
        +-- Sort transparent back-to-front
        +-- Build batches and instance data
        |
        +-- Shadow passes (only invalid regions)
        +-- Depth/normal prepass
        +-- SSAO + blur
        +-- Opaque PBR
        +-- Emission
        +-- Transparent
        +-- Bloom/post FX
        +-- Editor overlays (where applicable)
        |
        +-- Export ViewStats
  |
  +-- Aggregate FrameStats
  +-- Present
```

A ordem final deve preservar as dependências reais do projeto. Cada passe deve declarar recursos lidos/escritos, alvo, clear/load policy e dependências. Um Render Graph genérico completo pode ser uma etapa futura; inicialmente, declarar dependências manualmente é suficiente.

| Passe | Lê | Escreve |
|---|---|---|
| Shadow | geometria, transforms, alpha mask se aplicável | shadow maps |
| Prepass | geometria, transforms | depth/normal |
| SSAO | depth/normal | AO bruto |
| SSAO Blur | AO bruto, depth/normal | AO filtrado |
| Opaque | luzes, sombras, AO, materiais | color + depth |
| Emission | itens emissivos, depth | glow |
| Transparent | color/depth/luzes | color |
| Bloom | glow | intermediários/final |
| Composite | color + bloom | saída final |

---

# 5. P0 — Instrumentação confiável

## 5.1 Escopo Frame/View/Pass

Adicionar `FrameId`, `ViewId` e `RenderPassId`. Cada execução Scene/Game precisa de estatísticas independentes. Agregar explicitamente por quadro.

```cpp
struct FrameRenderStats {
    FrameId frame;
    Vector<RenderViewStats> views;
    PassStats total;
};
```

Não usar `present()` como único delimitador de uma vista. Se Scene e Game forem renderizadas antes de uma apresentação, precisam continuar distinguíveis.

## 5.2 Contagem no ponto de emissão

Todos os caminhos de draw do renderizador devem passar por helpers instrumentados ou por um wrapper de command list.

```cpp
void drawIndexedTracked(
    ICommandList& cmd,
    PassStats& stats,
    u32 indexCount,
    u32 instanceCount)
{
    cmd.drawIndexed(indexCount, instanceCount);
    ++stats.drawCalls;
    stats.instances += instanceCount;
    stats.trianglesSubmitted +=
        static_cast<u64>(indexCount / 3) * instanceCount;
}
```

`indexCount / 3` só vale para triangle list. Se outras topologias forem suportadas, calcular conforme a topologia. A contagem de triângulos é estimativa de submissão, não de triângulos efetivamente visíveis após clipping.

## 5.3 Distinguir editor e jogo

Instrumentar separadamente:
- Scene View;
- Game View;
- grid;
- gizmos;
- seleção/outline;
- overlays/UI;
- passes de cena.

A captura de 109 draws não deve ser interpretada sem essa separação.

## 5.4 CPU e GPU

Medir CPU com relógio monotônico ao redor da preparação e gravação/submissão de cada passe. Nomear `cpuPassMs` ou `cpuSubmitMs`.

GPU time deve vir de timestamp queries. Resultados podem chegar alguns quadros depois; não bloquear a CPU esperando a GPU. Se indisponível, mostrar “não suportado/não medido”, não copiar CPU time para o campo GPU.

## 5.5 Timestamp queries opcionais no RHI

```cpp
struct DeviceFeatures {
    bool timestamps = false;
};

class ICommandList {
public:
    virtual bool writeTimestamp(QueryId) { return false; }
};

class IDevice {
public:
    virtual DeviceFeatures features() const = 0;
    virtual bool resolveTimestamps(/* range and output */) { return false; }
};
```

Contrato ilustrativo. Adaptar à versão real da Diligent e aos mecanismos de query/fence dos backends.

## 5.6 Métricas necessárias

Por frame/view/pass:
- RHI draw calls;
- native draw calls, se mensurável;
- sub-draws;
- instâncias;
- itens visíveis/rejeitados;
- triângulos submetidos;
- pipeline changes;
- resource binds;
- bytes enviados a buffers;
- CPU preparation/submit time;
- GPU time;
- flush/submit count;
- shadow faces atualizadas/reutilizadas;
- motivo de passe ignorado.

## 5.7 Critérios de aceite P0

- Todos os draws do renderizador contabilizados.
- Draws do editor separados dos draws de cena.
- Scene/Game separados e total agregado corretamente.
- RHI draw e sub-draw não confundidos.
- CPU/GPU claramente separados.
- CSV exportável.
- Baseline capturado com configuração fixa.
- Nenhuma alteração visual.

---

# 6. P1 — Bounds e culling espacial

## 6.1 Bounds locais

Calcular AABB e esfera envolvente ao importar/criar a malha. Recalcular somente quando a geometria mudar.

```cpp
struct Bounds {
    Vec3 center;
    Vec3 extents;
    float radius;
};
```

## 6.2 Bounds mundiais

Para esfera, multiplicar o raio pela maior escala dos eixos da transformação. Isso é conservador para escala não uniforme.

```cpp
float maxAxisScale(const Mat4& m) {
    const float sx = length(axisX(m));
    const float sy = length(axisY(m));
    const float sz = length(axisZ(m));
    return max(sx, max(sy, sz));
}
```

A implementação deve respeitar a convenção de matriz da TOOL ENGINE. AABB mundial pode ser calculada transformando os oito cantos locais ou usando fórmula conservadora adequada.

## 6.3 Culling da câmera

Integrar `frustum_cull.hpp` ao caminho de construção das filas. Validar:
- convenção de profundidade zero-a-um vs menos-um-a-um;
- orientação dos planos;
- matriz row/column major;
- projeção perspective e ortográfica;
- near/far;
- viewport e correções específicas de API.

Começar com esfera; AABB-frustum pode vir depois se falsos positivos forem relevantes.

## 6.4 Culling de sombras

Não reutilizar camera-visible list para sombras.

- CSM: frustum por cascata.
- Point: frustum por face do cubemap.
- Spot: frustum/cone conforme projeção da luz.

Cada região produz fila e BatchPlan próprios. Reutilizar buffers/vetores e evitar alocações por face.

## 6.5 Culling hierárquico futuro

Quando a quantidade de objetos justificar, introduzir BVH, grid espacial ou árvore dinâmica. A escolha deve considerar o padrão de movimentação:
- cenas majoritariamente estáticas: BVH estático + lista dinâmica;
- muitas entidades móveis: estrutura dinâmica ou grid espacial;
- cenas pequenas: frustum linear pode ser mais barato e simples.

Não adicionar uma estrutura espacial complexa antes de medir o custo do loop linear.

## 6.6 Critérios P1

- testes de limites e casos tangentes;
- objetos fora da câmera não desaparecem indevidamente;
- objetos fora da câmera que projetam sombras continuam como casters;
- redução de itens/lotes por região demonstrada no Profiler;
- comparação visual automatizada/manual.

---

# 7. P2 — Filas, batching, instancing e dados de instância

## 7.1 Regras por passe

| Passe | Estratégia |
|---|---|
| Opaque PBR | agrupar por pipeline, mesh e recursos compatíveis; dados variáveis por instância |
| CSM | mesh + pipeline de sombra; material só se shader exigir |
| Point/Spot | fila por face/região e batching independente |
| Prepass | mesh + pipeline prepass |
| Emission | instancing por mesh/recursos do emissive |
| Transparent | ordenar primeiro; agrupar apenas adjacentes compatíveis |
| Fullscreen | evitar draws vazios/repetidos; geralmente um draw por operação |

## 7.2 Validar o plano de batch

Invariantes:
- todo índice está dentro da lista;
- nenhum item perdido ou duplicado;
- `sum(batch.count) == plan.order.size()`;
- `first + count` dentro do buffer;
- máximo de instâncias respeitado;
- chave representa todo estado relevante.

## 7.3 Buffer de instâncias

O passe opaco já usa instancing. Evoluir sem duplicar o sistema:
- manter layout C++/HLSL/GLSL sincronizado;
- `static_assert` de offsets/tamanho;
- capacidade cresce geometricamente;
- não recriar buffer por quadro;
- medir bytes enviados;
- considerar ring buffer por frame-in-flight;
- não sobrescrever memória ainda consumida pela GPU;
- destruir recursos em uso somente após mecanismo seguro de vida útil.

## 7.4 Emission instancing

Criar lote emissivo com dados de instância. Se o shader só precisa de transform e emissive color/intensity, considerar um layout compacto em vez de copiar toda a estrutura PBR. Medir o custo de upload antes de manter dois formatos.

## 7.5 Transparência

Preservar back-to-front. Agrupar apenas sequências adjacentes com mesh/pipeline/resources/blend state compatíveis e manter a ordem interna.

Alternativas futuras, com avaliação visual:
- Weighted Blended OIT;
- sorting por profundidade no GPU;
- categorias de transparência;
- depth prepass seletivo.

Não aplicar OIT como simples otimização invisível: altera a composição e precisa de validação.

## 7.6 Static batching

Avaliar para objetos estáticos repetidos ou geometria estática agregável:
- combinar malhas que compartilham material/pipeline;
- manter submeshes/material ranges quando necessário;
- armazenar bounds do agregado;
- invalidar/reconstruir quando membros mudarem;
- evitar combinar objetos que precisam de culling independente, animação ou seleção individual.

Static batching pode reduzir draws, mas aumenta memória/duplicação de vértices e pode piorar culling. Deve ser seletivo, não padrão para tudo.

## 7.7 Texture atlases e texture arrays

Quando muitos materiais diferem apenas pela textura:
- atlas reduz binds, mas exige UV transform e controle de mip/bleeding;
- texture array preserva coordenadas e permite selecionar camada por instância, mas requer dimensões/formato compatíveis e suporte adequado;
- bindless pode reduzir binds em APIs/capacidades compatíveis, mas não deve ser requisito de portabilidade.

Escolher por benchmark e qualidade visual. Não forçar materiais incompatíveis para o mesmo lote.

---

# 8. P3 — Shadow system: culling, cache, atlas e orçamento

## 8.1 Cache separado

- CSM: por vista/câmera e cascata.
- Point: por luz e face.
- Spot: por luz/região.

A câmera não deve invalidar Point/Spot se luz, projeção, casters e parâmetros permanecerem iguais.

## 8.2 Assinatura semântica

```cpp
struct ShadowCasterStamp {
    u64 objectId;
    u32 transformRevision;
    u32 meshRevision;
    u32 shadowMaterialRevision;
};

struct ShadowFaceSignature {
    u64 lightId;
    u64 projectionHash;
    u32 resolution;
    u64 casterHash;
    u32 pipelineRevision;
    u32 rasterParametersRevision;
};
```

Incluir somente dados que realmente alteram o mapa, mas não omitir nenhum. Hash determinístico; não usar ponteiros crus como identidade durável.

## 8.3 Invalidação por face

Invalidar quando:
- luz muda posição/direção/alcance/ângulo/tipo;
- matriz VP muda;
- resolução/formato muda;
- caster entra/sai da região;
- caster muda transformação;
- mesh ou shadow material muda;
- alpha clipping/bias/raster state muda;
- target/recurso é recriado;
- dispositivo/backend muda.

Um objeto que não participa de sombra não deve invalidar o mapa.

## 8.4 Atualização seletiva e orçamento

Configurar:
- máximo de luzes com sombra;
- máximo de faces atualizadas por quadro;
- resolução por tier;
- prioridade por importância visual, distância, tamanho projetado e movimento;
- política de aging para impedir que uma luz fique indefinidamente desatualizada.

A atualização em frames alternados pode ser usada para luzes secundárias, mas precisa de limite de atraso e política de movimento rápido.

## 8.5 Shadow Atlas e cubemap

Avaliar:
- atlas 2D para Spot/CSM ou regiões compatíveis;
- cubemap/cubemap array para Point;
- pooling de alocações por resolução;
- reutilização de targets;
- redução de recriação de recursos.

Um atlas pode reduzir custo de alocação/gestão e facilitar packing, mas **não elimina automaticamente os draws de geometria**. Point Light ainda precisa representar as direções/visibilidade das faces. O atlas exige:
- padding para evitar bleeding;
- viewport/scissor por tile;
- atualização parcial correta;
- controle de resolução e mip;
- limites de fragmentação.

Para Point Light, investigar cubemap arrays e rendering em camadas/multiview quando suportados. Geometry shader/layered rendering ou multiview podem reduzir submissões em alguns backends, mas não são soluções universais. Manter fallback de seis faces. Medir native draws, GPU time e compatibilidade.

## 8.6 Cache seguro

Só marcar válido após execução/submissão bem-sucedida e recurso pronto. Falha de criação, gravação, submissão, troca de dispositivo ou assinatura incompleta deve manter/inutilizar o cache corretamente.

## 8.7 Critérios P3

- cache ligado/desligado visualmente equivalente;
- mover câmera não refaz Point/Spot sem motivo;
- mover caster invalida apenas regiões afetadas;
- mover luz invalida as regiões daquela luz;
- faces atualizadas/reutilizadas e motivos aparecem no Profiler;
- limites de atualização não deixam sombras permanentemente obsoletas.

---

# 9. P4 — Redução de state changes, binds e custo de CPU

Reduzir Draw Calls não é a única forma de melhorar o renderizador. Muitas cenas são limitadas por mudanças de pipeline, resource binding, atualização de buffers ou custo de preparação.

## 9.1 Ordenação opaca

Ordenar opacos por uma chave que minimize mudanças:
1. render pass/pipeline;
2. material/resource set;
3. mesh;
4. parâmetros secundários.

A ordem exata deve ser escolhida com benchmark. Se a engine usar depth prepass, considerar front-to-back em uma etapa adequada. Não sacrificar batching ou correção de recursos por uma ordenação teórica.

## 9.2 State cache

Evitar binds redundantes com cache por command list. Contabilizar:
- chamadas solicitadas;
- chamadas realmente encaminhadas ao backend;
- cache hits;
- cache misses.

Não confundir “bind evitado” com “draw call evitada”.

## 9.3 Resource binding

Avaliar:
- agrupar parâmetros por material;
- resource sets persistentes;
- descriptor tables/sets conforme backend;
- texture arrays;
- atlases;
- bindless opcional;
- evitar recriar descriptor/resource bindings sem alteração.

A arquitetura de alto nível não deve depender de descriptor heap específico do D3D12 ou descriptor set específico do Vulkan.

## 9.4 Atualização de buffers

- agrupar uploads pequenos;
- evitar `updateBuffer` repetido por objeto quando dados podem ser empacotados;
- usar buffers persistentes/dinâmicos com estratégia adequada;
- alinhar e respeitar limites;
- evitar map/unmap excessivo;
- medir bytes e quantidade de uploads;
- evitar sincronização CPU/GPU por atualização.

## 9.5 CPU parallelism

Depois que as filas forem independentes e determinísticas, avaliar paralelizar:
- coleta por grupos de entidades;
- transformação/bounds;
- culling;
- construção de filas;
- geração de dados de instância.

Não paralelizar submissão ao mesmo contexto sem contrato explícito. Preferir workers produzindo command lists independentes quando a Diligent/backend permitir e a ordem de dependências estiver definida. O custo de sincronização pode superar o ganho em cenas pequenas.

---

# 10. P5 — Diligent, RHI e arquitetura multiplataforma

## 10.1 Diligent é a abstração, não uma API única

A TOOL ENGINE utiliza Diligent como camada de abstração. Vulkan é apenas um dos backends. A arquitetura de otimização deve ser comum, mas capacidades e custos podem variar por:
- Vulkan;
- Direct3D 12;
- Direct3D 11;
- OpenGL/OpenGL ES, conforme versão e suporte efetivamente configurado.

Não criar um renderizador separado para cada API. Manter os conceitos de RenderQueue/Batch/Pass no nível da engine e encapsular diferenças no RHI/Diligent.

## 10.2 MultiDrawIndexed e MDI

Investigar os recursos de Multi-Draw disponíveis na versão de Diligent usada pelo projeto. Não presumir que uma chamada `MultiDrawIndexed` equivale a um único draw nativo em todos os backends.

É necessário distinguir:
- chamada de alto nível feita pela TOOL ENGINE;
- chamada abstrata da Diligent;
- número de sub-draws;
- operações nativas emitidas pelo backend;
- custo real de CPU e GPU.

Em alguns caminhos, a implementação pode iterar e emitir draws individuais; em outros, pode usar uma operação nativa/indireta. Isso deve ser verificado no código/versão da Diligent e medido em cada backend.

### Multi-Draw tradicional

Agrupa múltiplas operações de desenho compatíveis em uma chamada de nível superior. Pode reduzir overhead de CPU/driver, mas não necessariamente reduz a quantidade de trabalho de geometria ou o número de sub-draws.

### Draw Indexed Indirect / Multi-Draw Indirect

A GPU lê argumentos de desenho de um buffer. É útil quando esses argumentos são produzidos por compute ou preparados em lote. Exige:
- layout/alinhamento correto;
- limites de draw count;
- buffer de argumentos;
- barreiras entre compute e graphics;
- sincronização;
- fallback quando indisponível.

### Critérios para adotar

Só integrar após:
1. baseline P0;
2. batching CPU correto;
3. perfil indicando overhead de submissão;
4. confirmação de suporte na Diligent e backend;
5. fallback testado;
6. comparação de CPU/GPU/frame latency.

## 10.3 RHI com capabilities

```cpp
struct DeviceFeatures {
    bool timestamps = false;
    bool computeShaders = false;
    bool indirectDraw = false;
    bool multiDraw = false;
    bool storageBuffers = false;
    bool textureArrays = false;
    bool bindlessResources = false;
    bool layeredRendering = false;
};
```

A lista é ilustrativa. Cada feature precisa de definição precisa, detecção real e testes. Não reportar suporte apenas porque a API gráfica em tese oferece o recurso; a combinação Diligent + versão + dispositivo pode não expô-lo.

## 10.4 Separar gravação, submit, flush e present

Conceitos:
- gravação de comandos;
- finalização de command list;
- submissão;
- flush;
- apresentação;
- sincronização/frame-in-flight.

O contrato atual pode continuar enquanto é medido. A mudança deve ocorrer por experimento A/B:
- A: comportamento atual;
- B: flush agrupado onde for seguro;
- C: submissão agrupada respeitando dependências.

Validar recursos importados da Light Prism, transições e vida útil. Não remover flush por suposição.

## 10.5 Matriz de validação

| Backend | Verificações |
|---|---|
| Vulkan | timestamps, transições, instancing, multi-draw/indirect quando suportado |
| Direct3D 12 | bindings, heaps, DXIL, transições, fences e timestamps |
| Direct3D 11 | fallback de recursos, bindings e limites |
| OpenGL/OpenGL ES | GLSL, clip/depth, limites de recursos e fallback |
| Null/Software | lógica, contadores e testes sem alegar GPU time |

Registrar versão da Diligent, driver, GPU, API e capabilities no CSV/relatório de benchmark.

---

# 11. P6 — GPU-driven rendering e compute culling

Somente após P0–P5.

```text
Instance Bounds Buffer
    |
Compute: frustum/occlusion test
    |
Visible Index Buffer + Counters
    |
Indirect Argument Generation
    |
Draw Indexed Indirect / Multi-Draw Indirect
```

Possíveis etapas:
1. enviar bounds e dados de instância;
2. compute culling;
3. compactar índices visíveis;
4. gerar argumentos indiretos;
5. executar draws;
6. comparar com CPU culling.

Manter modo de validação CPU/GPU. Evitar overflow de counters, race conditions, barreiras incorretas e leitura síncrona de resultados. O caminho CPU continua sendo referência e fallback.

## 11.1 Occlusion culling e Hi-Z

Hi-Z pode remover objetos ocultos, mas requer depth válido e uma política conservadora. Só implementar após frustum culling estável. Atenção a:
- objetos pequenos/rápidos;
- câmera em movimento;
- depth jitter;
- projeção e reversed-Z, se usado;
- falsos negativos;
- custo de construir pirâmide e executar compute.

## 11.2 LOD e impostors

LOD reduz custo de vértices/pixels, não necessariamente Draw Calls. Pode aumentar lotes se cada nível usar mesh/material diferente. Agrupar LOD por compatibilidade e distância/tamanho projetado. Impostors são opção para cenas com muitos objetos distantes, mas exigem pipeline e transição visual.

---

# 12. Organização de arquivos sugerida

Implementar gradualmente:

```text
engine/render/include/tool/render/
    render_view.hpp
    render_item.hpp
    render_bounds.hpp
    render_scene_collector.hpp
    visibility_system.hpp
    render_queue.hpp
    render_queue_builder.hpp
    batch_builder.hpp
    render_state_cache.hpp
    shadow_cache.hpp
    render_stats.hpp
    render_pass_executor.hpp
    gpu_renderer.hpp
    instance_batch.hpp
    frustum_cull.hpp
```

Dependências:
```text
render_item       -> math + resource handles
visibility        -> bounds + frustum
queue_builder     -> render_item + visibility
batch_builder     -> render_item + batch types
state_cache       -> RHI handles
shadow_cache      -> IDs/revisions/signatures
pass_executor     -> queues + batches + RHI
gpu_renderer      -> orquestra módulos
RHI               -> não depende de ECS, Scene, Material ou LightPrism
```

Não criar uma classe para cada função trivial. O objetivo é modularidade testável, não excesso de abstrações.

---

# 13. Plano de implementação incremental

Cada etapa deve ser pequena, compilável, revisável e reversível.

## Etapa 0 — Baseline e telemetria

1. FrameId/ViewId/PassId.
2. Contagem em todos os draws.
3. Separação Scene/Game/editor.
4. CPU por passe.
5. GPU timestamp opcional.
6. CSV.
7. baseline com captura e configuração fixa.

**Entrega:** relatório que explique de onde vêm os 109 draws (ou a nova contagem medida).

## Etapa 1 — Bounds

1. bounds local na criação/importação;
2. revisão de mesh;
3. atualização após edição;
4. teste de escala não uniforme/negativa;
5. bounds de primitivas e modelos.

## Etapa 2 — Culling da câmera

1. validar convenção de clip por backend;
2. integrar frustum;
3. filtrar opaco/prepass/emissão;
4. preservar casters fora da câmera;
5. comparar imagens.

## Etapa 3 — Culling de sombra

1. frustum por cascata;
2. frustum por face Point;
3. frustum Spot;
4. fila e batch por região;
5. medir draws e rejeições.

## Etapa 4 — Cache por face

1. IDs/revisões estáveis;
2. assinatura completa;
3. validade por face;
4. invalidar por caster/luz/recurso;
5. testar câmera/luz/caster/backend.

## Etapa 5 — Filas e batching

1. extrair RenderQueueBuilder;
2. separar plano e dados de instância;
3. preservar opaco;
4. instanciar emissão;
5. transparência adjacente segura;
6. state sorting/cache.

## Etapa 6 — Shadow resource management

1. pooling de shadow targets;
2. avaliar atlas/cubemap array;
3. orçamento de luzes/faces;
4. atualização seletiva;
5. medir VRAM e GPU time.

## Etapa 7 — Diligent submit/Flush

1. benchmark A/B;
2. verificar dependências e recursos importados;
3. agrupar flush apenas onde seguro;
4. validar backends.

## Etapa 8 — Multi-Draw/Indirect

1. verificar API/versão Diligent;
2. detectar capability real;
3. implementar caminho opcional;
4. medir draws lógicos e nativos;
5. fallback CPU.

## Etapa 9 — Compute culling/Hi-Z/LOD

Somente quando as etapas anteriores estiverem validadas e o Profiler demonstrar necessidade.

---

# 14. Estratégia de testes

## 14.1 Unit tests

**Frustum:** dentro, fora de cada plano, tangente, interseção, near/far, perspectiva, ortográfica, convenções de profundidade.

**Bounds:** malha vazia, vértice extremo, escala uniforme/não uniforme/negativa, rotação, hierarquia.

**Batching:** iguais, mesh igual/textura diferente, textura igual/mesh diferente, lote vazio, limite máximo, ordem/índices válidos.

**State cache:** mesmo estado não gera bind repetido; mudança gera bind; reset/contexto novo invalida; operação externa invalida.

**Shadow cache:** assinatura igual reutiliza; luz/caster/mesh/material/resolução/backend alterado invalida; falha não marca válido.

**Indirect:** contadores, offsets, alinhamento, overflow, fallback e barreiras.

## 14.2 Image tests

Cenas determinísticas:
1. muitos cubos com mesh/material compartilhados;
2. objetos dentro e fora da câmera;
3. caster fora da câmera projetando sombra visível;
4. Point Light com casters em todas as direções;
5. Spot com casters dentro/fora do cone;
6. múltiplas Point Lights;
7. emissivos com depth;
8. camadas transparentes;
9. Scene e Game views simultâneas;
10. editor grid/gizmos ligados/desligados.

Comparar imagem de referência e otimizada por diferença por canal, percentual de pixels acima do limiar e regiões críticas. Não depender somente de hash, pois drivers/precisão podem variar.

## 14.3 Matriz de APIs

Testar em GPU real sempre que possível. Compilar não equivale a validar. Registrar backends pendentes explicitamente.

---

# 15. Benchmark padrão

## 15.1 Cena

- cubo compartilhado;
- esfera;
- plano;
- materiais repetidos e distintos;
- Point/Spot/directional light;
- casters estáticos/dinâmicos;
- emissivos e transparentes;
- editor overlays opcionais;
- câmera fixa e trajetória reproduzível.

## 15.2 Escalas

10, 100, 500, 1.000 e 5.000 objetos.

Cenários:
- todos visíveis;
- 80% fora do frustum;
- casters fora da câmera, mas dentro da região de sombra;
- muitas meshes distintas;
- mesh/material compartilhados;
- muitos objetos estáticos;
- muitos objetos dinâmicos.

## 15.3 Configurações

- sem sombras;
- CSM 1/2/3/4 cascatas;
- Point sem sombra;
- Point com sombra;
- duas Point Lights;
- limite máximo de luzes;
- SSAO on/off;
- Bloom on/off;
- transparência on/off;
- Scene/Game/editor separados.

## 15.4 CSV

```csv
frame,view,pass,api_backend,rhi_draw_calls,native_draw_calls,sub_draws,
instances,visible_items,culled_items,triangles,pipeline_changes,
resource_binds,buffer_upload_bytes,submit_count,flush_count,
cpu_prepare_ms,cpu_submit_ms,gpu_ms,shadow_faces_rendered,
shadow_faces_reused,cache_invalid_reason
```

Campos não suportados devem ficar vazios/indisponíveis, nunca preenchidos com estimativas sem identificação.

## 15.5 Métricas de aprovação

- imagem equivalente;
- sem novos erros de validação;
- redução no custo-alvo;
- CPU/GPU/frame latency não pioram significativamente;
- memória e upload medidos;
- backend e hardware identificados;
- fallback testado.

---

# 16. Profiler e debug visual

Adicionar visualizações:
- bounds;
- frustum da câmera;
- frustum por cascata;
- frustum por face Point/Spot;
- casters aceitos/rejeitados;
- cor por batch;
- ID de cascata/face;
- shadow map individual;
- cache atualizado/reutilizado/inválido;
- motivo de invalidação;
- pipeline/resource binds;
- estado de capabilities.

Tabela sugerida:

| Pass | RHI Draws | Sub-draws | Instances | Visible | Culled | CPU ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| CSM 0 | ... | ... | ... | ... | ... | ... | ... |
| Point 0 Face 0 | ... | ... | ... | ... | ... | ... | ... |
| Prepass | ... | ... | ... | ... | ... | ... | ... |
| Opaque | ... | ... | ... | ... | ... | ... | ... |
| Emission | ... | ... | ... | ... | ... | ... | ... |
| Transparent | ... | ... | ... | ... | ... | ... | ... |
| Editor Grid | ... | ... | ... | ... | ... | ... | ... |

Exibir separadamente:
- draws solicitados pelo renderer;
- draws efetivamente encaminhados;
- sub-draws em Multi-Draw;
- cache hits de estado;
- faces de sombra atualizadas/reutilizadas;
- custo CPU e GPU.

---

# 17. Riscos e decisões de engenharia

| Risco | Efeito | Mitigação |
|---|---|---|
| Culling agressivo | objetos/sombras desaparecem | bounds conservadores e testes |
| Cache incompleto | sombras congeladas | assinatura semântica |
| BatchKey insuficiente | estado/material incorreto | chave baseada no estado real |
| BatchKey excessiva | lotes fragmentados | dados por instância |
| Transparência reordenada | composição errada | agrupar somente adjacentes |
| Flush removido cedo | sincronização incorreta | A/B e validação por backend |
| MDI presumido como draw único | métricas enganosas | contar RHI/native/sub-draw separadamente |
| Atlas mal organizado | bleeding/fragmentação | padding, tiles e testes |
| State cache desatualizado | estado gráfico incorreto | invalidação explícita |
| Buffer sobrescrito em voo | corrupção/flicker | ring buffer/fences/vida útil |
| Multi-view compartilhando recurso | contaminação entre vistas | chave por escopo |
| GPU-driven prematuro | complexidade sem ganho | priorizar caminho CPU medido |
| Static batching excessivo | VRAM e culling piores | uso seletivo |

## 17.1 Light Prism

Manter Light Prism como dona dos cálculos de iluminação que já oferece: clusters, BRDF e construção de dados de luz/CSM conforme o contrato atual.

A TOOL ENGINE deve concentrar-se em:
- seleção/culling de casters;
- filas e execução dos passes;
- cache de sombras sob responsabilidade do renderer;
- integração de recursos;
- métricas e submissão.

Não duplicar algoritmos de iluminação sem necessidade.

## 17.2 Independência da câmera

CSM depende da vista/câmera e da luz direcional. Point/Spot dependem da luz, projeção e casters relevantes. Compartilhar cache somente quando a assinatura comprovar que o conteúdo é idêntico.

## 17.3 Portabilidade

Nenhuma feature avançada pode ser requisito para abrir um projeto ou renderizar a cena. Cada capacidade deve ter fallback funcional e estado de suporte visível.

---

# 18. Critérios finais de conclusão

A arquitetura estará implementada quando:

1. coleta, visibilidade, filas, batches e execução estiverem separados;
2. `GpuRenderer` atuar principalmente como orquestrador;
3. todos os passes possuírem estatísticas confiáveis;
4. draws do editor e da cena forem separados;
5. culling da câmera estiver integrado e validado;
6. CSM/Point/Spot possuírem culling por região;
7. cache de sombra tiver assinatura completa e invalidação por face;
8. opaco e emissão utilizarem batching adequado;
9. transparência mantiver a ordem visual;
10. state cache reduzir binds redundantes sem erro;
11. submissão/Flush tiver sido medida;
12. Multi-Draw/MDI for opcional e corretamente contabilizado;
13. Profiler separar CPU/GPU e RHI/native draws;
14. benchmark reproduzível cobrir cenários e backends;
15. capacidades e fallbacks estiverem documentados;
16. nenhuma melhoria depender exclusivamente de uma API gráfica.

## 18.1 Metas de desempenho

Não definir um número universal de Draw Calls. Metas adequadas:
- draws opacos próximos ao número de batches compatíveis;
- draws de sombra próximos aos batches visíveis por região;
- nenhum lote vazio submetido;
- emissão/transparência ignoradas quando as filas estiverem vazias;
- mapas de sombra não refeitos quando a assinatura for válida;
- nenhum bind redundante quando o state cache puder provar igualdade;
- uploads e flushes medidos e reduzidos quando forem gargalo;
- frame time e latência melhorarem ou permanecerem estáveis.

---

# 19. Referências técnicas internas

Arquivos que devem continuar sendo revisados durante a implementação:

- `engine/render/include/tool/render/gpu_renderer.hpp`
- `engine/render/include/tool/render/instance_batch.hpp`
- `engine/render/include/tool/render/lighting.hpp`
- `engine/render/include/tool/render/frustum_cull.hpp`
- `engine/rhi/include/tool/rhi/rhi.hpp`
- `engine/rhi_diligent/src/diligent_device.cpp`
- `apps/editor/editor_state.hpp`
- `docs/ROADMAP.md`
- `docs/CHANGELOG.md`

Repositório de referência:
https://github.com/espiritoheroico2/toolgameengine

A versão da Diligent, os backends realmente habilitados e as APIs efetivamente compiladas devem ser confirmados no estado atual do repositório antes de implementar capacidades específicas.

---

# 20. Parecer final

A TOOL ENGINE não precisa abandonar o renderizador atual. O caminho mais seguro é transformar gradualmente o `GpuRenderer::end()` em uma sequência explícita, mensurável e testável.

As oportunidades imediatas mais importantes são:

1. descobrir exatamente de onde vêm as Draw Calls;
2. reduzir geometria repetida nas cascatas e faces de sombra com culling específico;
3. reutilizar mapas de sombra por face quando nada relevante mudou;
4. aproveitar instancing também na emissão;
5. manter transparência correta e agrupá-la somente quando seguro;
6. reduzir binds e mudanças de estado com ordenação e State Cache;
7. medir o custo de `Flush()` e da submissão Diligent;
8. avaliar Multi-Draw/MDI conforme o backend real, sem presumir equivalência entre APIs;
9. só então avançar para compute culling e GPU-driven rendering.

A arquitetura deve continuar baseada em Diligent e no RHI da TOOL ENGINE. Vulkan, Direct3D 12, Direct3D 11 e OpenGL/OpenGL ES devem compartilhar o mesmo pipeline de alto nível, com capabilities detectadas, fallbacks e validação explícita.

**Ordem recomendada:** P0 telemetria → P1 bounds/culling → P2 filas/batching/instancing → P3 cache e orçamento de sombras → P4 state cache e submissão → P5 Multi-Draw/MDI opcional → P6 GPU-driven/Hi-Z/LOD.

A regra de engenharia é simples: nenhuma otimização deve ser considerada concluída apenas porque reduziu o contador de Draw Calls. Ela precisa demonstrar menor custo real, preservar a imagem, manter a estabilidade dos recursos e funcionar nos backends suportados.
"""

