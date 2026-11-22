arquitetura ultra draw calls
# TOOL ENGINE — Plano de Implementação da Arquitetura de Renderização
## Engenharia de software, pipeline gráfico e otimização de Draw Calls

**Projeto:** My-game-engine — TOOL ENGINE — EXPERIMENTAL  
**Base técnica consultada:** repositório GitHub, arquivos `gpu_renderer.hpp`, `instance_batch.hpp`, `lighting.hpp`, `rhi.hpp`, `diligent_device.cpp`, `frustum_cull.hpp` e `docs/ROADMAP.md`.  
**Objetivo:** evoluir o renderizador existente sem reescrita integral, reduzindo trabalho redundante de CPU/GPU e tornando o custo de cada passe mensurável.  
**Escopo:** renderização 3D, iluminação, sombras, SSAO, emissão/Bloom, transparência, RHI e integração com o Profiler.  
**Natureza do documento:** proposta de engenharia. Os trechos de código são contratos e exemplos de implementação; precisam ser adaptados aos tipos reais e compilados/testados no repositório antes de serem considerados código pronto.

---

# 1. Resumo executivo

A TOOL ENGINE já possui uma base funcional relevante: `GpuRenderer`, RHI próprio, integração com Diligent, Light Prism, agrupamento por instâncias no passe opaco, prepass de SSAO, mapas CSM, sombras de Point/Spot, emissão, transparência e Bloom.

A evolução proposta não consiste em substituir tudo por um novo renderizador. Consiste em separar responsabilidades que hoje estão concentradas em `GpuRenderer::end()` e tornar explícito o fluxo:

1. coletar os objetos da cena;
2. calcular limites espaciais e visibilidade;
3. produzir filas específicas para cada passe;
4. agrupar itens compatíveis;
5. executar os passes na ordem correta;
6. registrar estatísticas detalhadas;
7. validar visualmente e medir o resultado.

A arquitetura-alvo é:

```text
Scene / ECS
    |
    v
RenderSceneCollector
    |  DrawItem + bounds + material + mesh + texture
    v
VisibilitySystem
    |  camera frustum / CSM cascade / point face / spot frustum
    v
RenderQueueBuilder
    |  opaque / shadow / prepass / emission / transparent
    v
BatchBuilder
    |  batches by pass + pipeline + mesh + material/resources
    v
RenderPassExecutor
    |  shadow -> prepass/AO -> opaque -> emission -> transparent -> post FX
    v
RHI (IDevice / ICommandList)
    |
    v
Diligent backend
    |
    v
Vulkan / D3D12 / D3D11 / OpenGL(ES) / outros backends suportados
```

A principal prioridade é **instrumentar antes de otimizar**. A imagem fornecida pelo usuário mostra 109 Draw Calls, 11 objetos desenhados e 802 triângulos, mas esses números não demonstram por si só onde cada chamada é originada. O `DrawStats` atual é capturado pelo dispositivo e reiniciado em `present()`, enquanto o editor pode renderizar mais de uma vista. Portanto, o primeiro marco deve estabelecer escopo de quadro/vista e contagem por passe.

## 1.1 Resultados esperados

- Identificar quantos Draw Calls cada passe produz.
- Evitar enviar objetos irrelevantes para cada face de sombra.
- Reutilizar mapas de sombra apenas quando luzes, casters e parâmetros relevantes não mudaram.
- Reutilizar o modelo de batching para passes que podem ser agrupados sem violar a semântica visual.
- Reduzir `Flush()` redundante, depois de verificar dependências e regras do Diligent.
- Separar custo de CPU, custo de GPU, submissão, sincronização e readback.
- Manter o renderizador independente da API gráfica específica.
- Criar benchmarks reproduzíveis para impedir regressões.

## 1.2 O que não fazer inicialmente

Não iniciar com:
- reescrita integral do `GpuRenderer`;
- GPU-driven rendering completo;
- compute culling e indirect draw antes de estabilizar o caminho CPU;
- Hi-Z antes de haver culling conservador validado;
- mesh shaders como requisito da arquitetura;
- agrupamento indiscriminado de transparência;
- remoção de passes sem comparação de imagem.

O `docs/ROADMAP.md` já define a ordem P1–P6 e recomenda não avançar para compute/indirect, Hi-Z, LOD e impostores antes de P1/P2 medidos. Este documento preserva essa ordem.

---

# 2. Diagnóstico do estado atual do código

Esta seção separa fatos observados nos arquivos do repositório de recomendações de projeto.

## 2.1 Componentes existentes

| Componente | Estado observado | Consequência para o plano |
|---|---|---|
| `GpuRenderer` | Implementa preparação e execução de muitos passes em `end()` | Extrair etapas gradualmente, sem reescrever tudo |
| `instance_batch.hpp` | Define `BatchItem`, `BatchPlan`, `InstanceGPU` e `planInstanceBatches` | Reutilizar e evoluir o agrupamento |
| `lighting.hpp` | Faz a ponte entre cena, Light Prism e `LightFrame` | Manter a Light Prism como dona da construção de luzes/clusters/CSM |
| `rhi.hpp` | Define `IDevice`, `ICommandList`, `DrawStats` | Acrescentar capacidades de forma incremental e opcional |
| `diligent_device.cpp` | Implementa recursos e comandos usando Diligent | Manter detalhes de API confinados ao backend |
| `frustum_cull.hpp` | Contém frustum por esfera, mas o roadmap registra que ainda não está ligado ao renderizador | Integrar em P2 com testes de borda |
| `docs/ROADMAP.md` | Registra P1 integrado, mas ainda pendente de validação visual/benchmark; P2 ainda não integrado | Atualizar o roadmap somente após evidências |

### Observação de consistência documental

O comentário no cabeçalho de `instance_batch.hpp` ainda descreve o agrupamento como “ainda não ligado ao renderizador”, enquanto `GpuRenderer` já chama `planInstanceBatches` no passe opaco, prepass e sombras, e o `ROADMAP.md` registra P1 como implementado. Essa divergência deve ser corrigida como tarefa documental, sem confundir comentário antigo com estado real do código.

## 2.2 Passes identificados em `GpuRenderer`

A implementação atual contém, conforme os caminhos condicionais:

1. sombras CSM: uma execução por cascata;
2. sombras Point/Spot: uma execução por luz e uma submissão de geometria por face;
3. prepass de geometria para SSAO;
4. SSAO fullscreen;
5. blur de SSAO fullscreen;
6. passe PBR opaco instanciado;
7. passe de emissão, com desenho individual dos objetos emissivos;
8. passe transparente, com ordenação por distância e desenho individual;
9. Bloom: limiar/extração, blur horizontal, blur vertical e composição fullscreen.

Nem todo passe roda em todos os quadros. A contagem deve ser medida com os recursos realmente ativos.

## 2.3 O custo das sombras

No caminho CSM, o renderizador percorre `castPlan.batches` para cada cascata. No caminho Point/Spot, percorre os mesmos lotes em cada face da luz. O batching reduz chamadas dentro de cada face, mas não elimina a repetição entre faces.

Uma aproximação para o custo de submissões de geometria é:

```text
shadowDraws =
    csmCascadeCount * visibleShadowBatchesPerCascade
  + sum(pointLightFaces[i] * visibleShadowBatchesPerFace[i])
```

Se, apenas para estimativa, todos os lotes fossem visíveis em todas as regiões:

```text
shadowDraws ~= shadowBatchCount * (
    csmCascadeCount + sum(pointLightFaces)
)
```

Exemplo hipotético: 8 lotes de sombra, 4 cascatas, duas Point Lights com 6 faces cada:

```text
8 * (4 + 6 + 6) = 128 comandos de desenho
```

Esse número é uma estimativa de pior caso simplificada, não uma medição da cena do usuário. Culling por cascata/face deve reduzir os lotes efetivamente submetidos.

## 2.4 O cache de sombras precisa de uma assinatura semântica

`GpuRenderer::setLighting()` recebe `reuse` e `pointReuse` do chamador. O renderizador confia nesses booleanos para pular o passe. Para Point/Spot, o próprio método também verifica validade, resolução e quantidade de luzes; isso não substitui uma assinatura completa.

A assinatura que autoriza reutilização precisa refletir todos os dados que alteram o mapa:

- identidade/versão da luz;
- tipo da luz;
- posição, direção, alcance, ângulos do spot;
- matrizes VP das faces;
- resolução e formato do mapa;
- lista de casters que intersectam a região;
- transformação mundial dos casters;
- revisão da malha/geomatria;
- estado `castShadow`;
- parâmetros de rasterização que alteram o resultado (bias, culling, alpha clipping, se aplicável).

A câmera principal não deve invalidar automaticamente mapas de Point/Spot, pois as projeções dessas luzes são independentes da câmera. A câmera pode alterar o conjunto de luzes escolhidas pelo orçamento ou a seleção de casters se o algoritmo usar proximidade da câmera; nesse caso a seleção deve fazer parte da assinatura.

## 2.5 Emissão e transparência

O passe de emissão percorre `draws_` e emite um `drawIndexed(..., 1)` por objeto emissivo, atualizando `ObjectBuffer` individualmente. Esse passe é candidato a instancing.

O passe transparente ordena `tdraws_` do mais distante para o mais próximo e desenha os itens individualmente. Não deve ser agrupado de forma ingênua: a ordem de mistura pode alterar o resultado. O plano é preservar a ordenação e só agrupar sequências adjacentes que tenham estado compatível, após provar equivalência visual.

## 2.6 RHI e submissão

O RHI atual possui `beginCommands()`, `submit()`, `present()` e `DrawStats`. A interface de comando já possui `drawIndexed`, `drawIndexedRange`, `setViewport`, `setScissor`, `setStorage`, etc. Ainda não há no contrato apresentado um conjunto explícito de `dispatch`, `drawIndexedIndirect`, timestamp query ou fence de GPU por passe.

No backend Diligent, `submit()` chama `ctx->Flush()`. Como o renderizador submete listas após diversos passes, a frequência de flush deve ser medida. Não remover flush por suposição: primeiro verificar a semântica do contexto imediato, transições e recursos importados da Light Prism em cada backend.

## 2.7 Estatísticas por vista

O backend copia `stats_` para `last_` e zera `stats_` em `present()`. Se Scene e Game forem renderizadas antes de uma única apresentação, a métrica do dispositivo pode não representar a soma desejada; dependendo do fluxo real de chamadas, pode representar apenas a janela de contagem entre apresentações. A solução é definir explicitamente estatísticas por `RenderView` e por `Frame`, e não depender implicitamente de `present()` como delimitador universal de uma vista.

---

# 3. Arquitetura-alvo e responsabilidades

A separação deve ser lógica primeiro. Não é obrigatório criar imediatamente uma classe para cada caixa do diagrama. O objetivo inicial é extrair funções/estruturas testáveis do `GpuRenderer::end()`.

## 3.1 `RenderView`

Representa uma execução de renderização para uma câmera e um alvo específicos. Scene e Game são vistas distintas, mesmo quando compartilham a mesma cena.

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

Responsabilidades:
- guardar parâmetros da câmera;
- definir viewport e alvo;
- possuir estatísticas próprias;
- identificar caches dependentes da vista;
- evitar compartilhar por acidente dados temporários entre Scene e Game.

Não colocar estado global de câmera dentro de cache de sombra de luz pontual, pois essas sombras não dependem diretamente da câmera.

## 3.2 `RenderItem` / `DrawItem`

É a representação imutável de um objeto preparado para renderização naquele quadro.

```cpp
struct RenderItem {
    const GpuMesh* mesh = nullptr;
    MaterialId material;
    rhi::TextureId albedo;
    Mat4 world;
    Bounds worldBounds;
    u64 objectId = 0;
    u32 meshRevision = 0;
    u32 materialRevision = 0;
    RenderFlags flags;
};
```

`Bounds` deve conter pelo menos esfera e, idealmente, AABB local. O objeto deve carregar identidade estável; não usar somente endereço de ponteiro como identidade persistente de cache.

Os dados que podem mudar por quadro (matriz, cor, parâmetros) ficam no item ou em dados de instância. Os recursos imutáveis (malha, textura, pipeline) são referenciados por handle/recurso.

## 3.3 `RenderSceneCollector`

Recebe a cena/ECS e produz uma lista linear de `RenderItem`.

Responsabilidades:
- consultar entidades que possuem malha e transformação;
- calcular/obter matriz mundial;
- obter material e textura;
- obter bounds locais da malha e transformá-los;
- separar opacos, transparentes, emissivos e casters;
- não executar comandos GPU;
- não decidir qual passe vai desenhar diretamente.

A coleta deve reutilizar vetores e evitar alocação por objeto. A etapa de coleta não deve chamar `Flush`, `submit` nem `present`.

## 3.4 `VisibilitySystem`

Recebe bounds e volumes de visibilidade e retorna conjuntos de itens visíveis.

Volumes:
- frustum da câmera;
- frustum de cada cascata CSM;
- frustum de cada face de Point Light;
- cone/frustum de cada Spot Light.

Importante: o conjunto visível pela câmera **não é** o conjunto correto de casters de sombra. Um objeto fora da câmera pode projetar sombra dentro da região iluminada que aparece na tela.

## 3.5 `RenderQueueBuilder`

Cria filas específicas. Não executa GPU.

```cpp
struct RenderQueues {
    std::vector<RenderItemIndex> opaque;
    std::vector<RenderItemIndex> transparent;
    std::vector<RenderItemIndex> emissive;
    std::vector<RenderItemIndex> prepass;
    std::vector<ShadowQueue> csm;
    std::vector<PointShadowQueue> pointShadow;
};
```

As filas são índices para uma lista central de itens, reduzindo cópias de `RenderItem`.

Cada fila possui regras de seleção:
- opaco: geometria opaca válida para PBR;
- transparente: opacidade abaixo do limite definido;
- emissão: somente itens emissivos e visíveis no passe;
- prepass: somente itens que participam do SSAO/G-buffer;
- sombra: somente casters que intersectam a região da luz e passam pelas regras de sombra.

## 3.6 `BatchBuilder`

Agrupa itens por compatibilidade de estado. A chave não deve ser sempre apenas `(mesh, texture)`. A chave correta depende do passe.

Exemplo de chave:

```cpp
struct BatchKey {
    PipelineId pipeline;
    MeshId mesh;
    MaterialId material;   // ou material class + descriptors compatíveis
    TextureSetId textures;
    u32 rasterState;
    u32 depthBlendState;
};
```

No passe opaco atual, o shader armazena parâmetros variáveis por instância e o lote é agrupado por malha/textura. Isso permite que materiais com parâmetros diferentes sejam instanciados se todos os parâmetros necessários estiverem no buffer por instância e o pipeline/recursos forem compatíveis. A chave deve refletir exatamente o que é bind global e o que é dado por instância.

Para sombra, material pode não ser necessário se o shader de sombra não lê textura/material. Se alpha-tested shadows forem adicionadas no futuro, textura/material passam a fazer parte da chave.

## 3.7 `RenderPassExecutor`

Executa filas já preparadas. Não consulta ECS nem calcula bounds.

Responsabilidades:
- iniciar alvo e limpar conforme contrato;
- configurar pipeline e recursos;
- submeter lotes;
- registrar estatísticas por passe;
- garantir ordem e dependências entre passes.

No primeiro estágio, os métodos podem continuar dentro de `GpuRenderer`, mas devem receber `RenderQueues`/`BatchPlan` em vez de reconstruir tudo dentro de `end()`.

## 3.8 `RenderStats`

Estrutura independente do contador legado do dispositivo:

```cpp
enum class RenderPassId : u8 {
    CsmShadow, PointShadow, SpotShadow,
    DepthNormalPrepass, Ssao, SsaoBlur,
    Opaque, Emission, Transparent,
    BloomExtract, BloomBlurH, BloomBlurV, BloomComposite,
    Count
};

struct PassStats {
    u32 drawCalls = 0;
    u32 instances = 0;
    u32 visibleItems = 0;
    u32 culledItems = 0;
    u64 trianglesSubmitted = 0;
    u32 pipelineChanges = 0;
    u32 resourceBinds = 0;
    u64 bufferUploadBytes = 0;
    double cpuSubmitMs = 0.0;
    double gpuTimeMs = -1.0; // -1 = indisponível/não medido
};

struct RenderViewStats {
    ViewId view;
    std::array<PassStats, static_cast<size_t>(RenderPassId::Count)> passes;
};
```

`drawCalls` deve ser incrementado no ponto em que o comando `drawIndexed` é efetivamente registrado, não inferido pelo número de objetos. O backend também mantém suas estatísticas próprias para conferência.

---

# 4. Pipeline de quadro proposto

## 4.1 Ordem lógica

A ordem abaixo é uma referência; preservar os requisitos de recursos e o comportamento atual do projeto.

```text
BeginFrame
  |
  +-- BeginView(Scene ou Game)
        |
        +-- Collect scene items
        +-- Update bounds / revisions
        +-- Build camera frustum
        +-- Camera visibility
        +-- Build CSM queues por cascata
        +-- Build Point/Spot queues por face
        +-- Validate shadow cache
        +-- Build opaque / prepass / emission / transparent queues
        +-- Build batches
        |
        +-- CSM shadow passes (somente cascatas invalidadas)
        +-- Point/Spot shadow passes (somente faces invalidadas)
        +-- Depth/normal prepass (se SSAO exigir)
        +-- SSAO + blur (se habilitado)
        +-- Opaque PBR
        +-- Emission (se houver emissivos e Bloom estiver ativo)
        +-- Transparent (se houver transparentes)
        +-- Bloom fullscreen chain (se ativo)
        |
        +-- EndView / export stats
  |
  +-- EndFrame
  +-- Present
```

O executor pode agrupar submissões em uma ou mais listas de comandos, desde que o backend preserve a ordem de dependências e transições de recursos.

## 4.2 Regra de dependências

Não tratar o grafo como uma lista arbitrária de comandos. Cada passe declara:
- recursos lidos;
- recursos escritos;
- alvo de cor/profundidade;
- dependência de passe anterior;
- se precisa limpar o alvo;
- se preserva conteúdo existente.

Exemplo:

| Passe | Lê | Escreve | Dependência |
|---|---|---|---|
| CSM | geometria, transformações | mapas CSM | coleta e filas CSM |
| Point Shadow | geometria, transformações | mapas Point/Spot | filas por face |
| Prepass | geometria, transformações | normal/depth | coleta e câmera |
| SSAO | normal/depth | AO bruto | prepass |
| SSAO Blur | AO bruto, normal/depth | AO filtrado | SSAO |
| Opaque | mapas de sombra, AO, luzes, materiais | color + depth | sombras e AO |
| Emission | geometria emissiva, depth | glow | opaco/depth |
| Transparent | color/depth, luzes | color | opaco |
| Bloom | glow | bloom intermediário/final | emissão |
| Composite | color, bloom | saída final | bloom |

O primeiro incremento pode codificar essas dependências manualmente. Um Render Graph genérico completo só é necessário quando a quantidade de passes/recursos justificar.

---

# 5. Implementação P0 — instrumentação e métricas confiáveis

**Prioridade máxima.** Nenhuma otimização deve ser declarada vencedora sem contagem consistente e imagem equivalente.

## 5.1 Delimitar Frame, View e Pass

Adicionar identificadores explícitos:

```cpp
using FrameId = u64;
using ViewId  = u32;

struct RenderContext {
    FrameId frame;
    ViewId view;
    RenderPassId pass;
};
```

O contador por passe fica no renderizador. O RHI pode receber marcadores de debug opcionais, mas não precisa conhecer a semântica da cena.

## 5.2 Contagem local de Draw Calls

Criar um helper para evitar esquecer contadores:

```cpp
void drawIndexed(
    rhi::ICommandList& cmd,
    RenderViewStats& stats,
    RenderPassId pass,
    u32 indexCount,
    u32 instanceCount)
{
    cmd.drawIndexed(indexCount, instanceCount);

    auto& p = stats.passes[static_cast<size_t>(pass)];
    ++p.drawCalls;
    p.instances += instanceCount;
    p.trianglesSubmitted +=
        static_cast<u64>(indexCount / 3) * instanceCount;
}
```

A fórmula `indexCount / 3` só é válida para triangle list sem primitivas degeneradas e sem outros topologies. O RHI atual fixa `TRIANGLE_LIST` em `createPipeline`, então é adequada para esse caminho; se outras topologias forem introduzidas, calcular conforme topologia.

Separar:
- `visibleItems`: itens aceitos pelo culling;
- `culledItems`: itens rejeitados;
- `instances`: instâncias enviadas;
- `drawCalls`: comandos registrados;
- `trianglesSubmitted`: triângulos estimados enviados.

## 5.3 Medir CPU por passe

Usar relógio monotônico de CPU em torno de preparação e gravação de comandos:

```cpp
const auto t0 = Clock::now();
executeOpaquePass(...);
stats.cpuSubmitMs = milliseconds(Clock::now() - t0);
```

Nomear como `cpuSubmitMs` ou `cpuPassMs`, não como tempo de GPU.

## 5.4 Timestamp queries na GPU

O roadmap marca timestamps reais de GPU como pendentes (T10). Acrescentar ao RHI uma capacidade opcional, sem exigir que todo backend suporte imediatamente:

```cpp
struct TimestampQueryId { u32 value = kInvalidIndex; };

class ICommandList {
public:
    virtual bool beginTimestamp(TimestampQueryId) { return false; }
    virtual bool endTimestamp(TimestampQueryId) { return false; }
};

class IDevice {
public:
    virtual bool supportsTimestamps() const { return false; }
    virtual bool resolveTimestamps(/* query range, result buffer */) { return false; }
};
```

O contrato final precisa ser desenhado conforme os tipos de query/fence oferecidos pela versão de Diligent vendorizada. Não copiar uma API de outro backend diretamente para a interface comum.

Regras:
- resultados podem chegar alguns quadros depois;
- não esperar a GPU terminar só para atualizar o Profiler;
- `gpuTimeMs = -1` quando indisponível;
- evitar reutilizar query ainda em voo;
- consultar `supportsTimestamps()` e apresentar estado “não medido”.

## 5.5 Corrigir agregação entre vistas

Criar estatísticas por vista e agregadas por quadro:

```cpp
struct FrameRenderStats {
    FrameId frame;
    std::vector<RenderViewStats> views;
    PassStats total;
};
```

Scene e Game devem ter `ViewId` diferentes. O total do quadro é a soma explícita das vistas. `present()` não deve ser a única fronteira de agregação.

## 5.6 Critérios de aceite P0

- 100% dos `drawIndexed` do `GpuRenderer` contabilizados por passe.
- `drawCalls` local confere com `DrawStats` do backend dentro do escopo definido.
- Scene e Game aparecem separadamente e no total.
- CPU e GPU não são confundidos.
- Exportação CSV contém frame, view, pass e os contadores.
- Sem mudança visual.

---

# 6. Implementação P1 — culling espacial correto

O repositório já possui `frustum_cull.hpp` com teste de esfera contra seis planos. O roadmap registra que a implementação ainda não está ligada ao renderizador. A integração deve começar pela câmera, depois sombras.

## 6.1 Bounds da malha

Ao importar/criar uma malha, calcular uma vez:
- AABB local: `min`, `max`;
- centro local;
- raio local da esfera envolvente.

Não recalcular percorrendo todos os vértices a cada quadro.

```cpp
struct Bounds {
    Vec3 center;
    Vec3 extents; // meia extensão AABB
    float radius;
};

struct Mesh {
    // dados existentes...
    Bounds localBounds;
    u32 revision = 1;
};
```

Se a malha for editada, incrementar `revision` e recalcular bounds.

## 6.2 Transformar esfera para mundo

Para transformações com rotação e escala não uniforme, usar um fator conservador. A escala máxima das três bases da matriz evita descartar objetos visíveis:

```cpp
float maxAxisScale(const Mat4& m) {
    const float sx = length(Vec3{m.m[0][0], m.m[0][1], m.m[0][2]});
    const float sy = length(Vec3{m.m[1][0], m.m[1][1], m.m[1][2]});
    const float sz = length(Vec3{m.m[2][0], m.m[2][1], m.m[2][2]});
    return std::max({sx, sy, sz});
}

Bounds worldBounds(const Bounds& local, const Mat4& world) {
    Bounds out;
    out.center = world.transformPoint(local.center);
    out.radius = local.radius * maxAxisScale(world);
    // AABB mundial pode ser calculada transformando os 8 cantos locais.
    return out;
}
```

A convenção `m[col][row]` deve permanecer consistente com `tool::Mat4`. Confirmar a implementação de `transformPoint` e `length` no código real antes de integrar.

## 6.3 Culling da câmera

Usar `frustumFromViewProj()` com a convenção de profundidade correta:
- Vulkan/D3D: profundidade zero a um, conforme o comentário atual do helper;
- OpenGL: profundidade menos um a um, conforme o helper atual;
- validar também a convenção real do backend e as correções de Y/clip usadas pelos shaders.

Não assumir que “OpenGL” e “GLES” são caminhos idênticos em todas as versões. O `GpuRenderer` atual identifica `Backend::GLES` no caminho GLSL 430; documentar claramente a nomenclatura real e a API suportada.

O culling deve ser conservador: aceitar objetos extras é preferível a descartar geometria visível.

## 6.4 Culling de CSM

Construir um frustum por cascata a partir de cada matriz `cascadeVp_[k]`.

Para cada cascata:
1. obter o frustum;
2. consultar bounds dos objetos com `castShadow`;
3. opcionalmente aplicar extensão de caster já prevista na configuração CSM;
4. criar uma fila própria;
5. construir batches apenas com os itens aprovados.

Não reutilizar o frustum da câmera.

## 6.5 Culling de Point Light

Para Point Light, cada face corresponde a uma projeção. Usar a matriz VP daquela face e culling por frustum.

Para cada luz:
- validar `faces` (Point: até 6; Spot: normalmente 1);
- gerar/usar o frustum da face;
- testar apenas casters;
- montar `ShadowQueue` por face;
- não enviar o mesmo lote quando nenhum objeto intersectar aquela face.

Para geometrias grandes, a esfera envolvente pode causar falsos positivos. Isso não compromete a imagem, apenas reduz menos Draw Calls. AABB-frustum pode ser uma melhoria posterior.

## 6.6 Culling de Spot

Usar o frustum/cone de projeção do spot, incluindo near/far e ângulo externo. A matriz VP já preparada no `ShadowLight` pode ser usada como base para extrair planos.

## 6.7 Dados e pseudocódigo

```cpp
for (u32 cascade = 0; cascade < csmCount; ++cascade) {
    const Frustum f = frustumFromViewProj(
        cascadeViewProjection[cascade], depthZeroToOne);

    auto& queue = queues.csm[cascade];
    queue.clear();

    for (u32 i : shadowCasterIndices) {
        const Bounds& b = items[i].worldBounds;
        if (sphereInFrustum(f, b.center.x, b.center.y,
                            b.center.z, b.radius)) {
            queue.push_back(i);
        } else {
            stats.csm[cascade].culledItems++;
        }
    }
}
```

No código real, não alocar/limpar de forma que provoque realocação recorrente. Reutilizar a capacidade dos vetores.

## 6.8 Critérios de aceite P1

- Testes unitários de esfera dentro, fora, tangente e parcialmente intersectando.
- Testes de profundidade Vulkan/D3D e OpenGL.
- Cena de referência com objetos fora da câmera que ainda projetam sombra.
- Nenhum objeto visível desaparece.
- Contagem por cascata/face mostra redução.
- Comparação de imagem antes/depois dentro de tolerância definida.

---

# 7. Implementação P2 — filas de renderização e batching por passe

## 7.1 Não agrupar todos os objetos com a mesma regra

O batching depende da semântica do passe:

| Passe | Agrupamento sugerido | Restrição |
|---|---|---|
| Opaque PBR | mesh + textura/recursos realmente bindados + pipeline | parâmetros por objeto devem estar no buffer de instâncias |
| CSM shadow | mesh + pipeline de sombra | textura/material só se alpha clipping for usado |
| Point/Spot shadow | mesh + pipeline + face queue | fila diferente por face |
| Prepass | mesh + pipeline prepass | excluir itens sem participação no prepass |
| Emission | mesh + recursos exigidos pelo shader de emissão | dados emissivos por instância |
| Transparent | ordem de profundidade primeiro; agrupar apenas itens adjacentes compatíveis | não reordenar arbitrariamente |
| Fullscreen | um draw por operação fullscreen, em geral | não há malhas de cena para agrupar |

## 7.2 Corrigir o contrato do BatchKey

O `BatchItem` atual agrupa por `mesh` e `texture`. Essa regra funciona quando os demais dados variáveis estão no buffer por instância e o pipeline é igual.

Evolução recomendada:

```cpp
struct BatchKey {
    u64 mesh;
    u64 pipeline;
    u64 textureSet;
    u64 materialClass;
    u32 rasterState;
    u32 blendDepthState;
};

struct BatchItem {
    BatchKey key;
    u32 renderItemIndex;
};
```

Não adicionar campos à chave sem necessidade. Cada campo extra pode fragmentar lotes. Por outro lado, omitir um estado que altera o resultado pode criar batching incorreto.

## 7.3 Separar plano e dados de instância

O plano deve armazenar a ordem e os lotes; os dados de instância são empacotados na mesma ordem.

```cpp
BatchPlan plan = buildBatches(queue, items, maxInstancesPerBatch);

for (u32 orderedIndex : plan.order) {
    const RenderItem& item = items[orderedIndex];
    instanceData.push_back(makeInstanceGPU(item));
}
```

Validar invariantes:
- `order` é uma permutação válida;
- nenhum item é perdido/duplicado;
- `sum(batch.count) == plan.order.size()`;
- `first + count` nunca ultrapassa o buffer;
- limite de instâncias respeitado.

## 7.4 Buffer de instâncias

O caminho atual usa `StructuredBuffer` e `SV_InstanceID`. Manter o layout HLSL/GLSL sincronizado e os `static_assert` do C++.

Boas práticas:
- capacidade cresce geometricamente;
- não recriar buffer a cada quadro;
- registrar bytes enviados;
- considerar ring buffer por frame-in-flight quando a sincronização for implementada;
- evitar sobrescrever dados ainda consumidos pela GPU;
- não destruir buffer em uso sem mecanismo seguro de vida útil.

O `updateBuffer` atual do backend usa `MAP_FLAG_DISCARD` para buffers dinâmicos. A estratégia de múltiplos frames deve respeitar a semântica de descarte e a vida útil dos recursos.

## 7.5 Emissão instanciada

O passe atual atualiza `ObjectBuffer` e desenha um objeto por chamada. Criar `EmissionInstanceGPU` ou reutilizar `InstanceGPU`, se o shader consumir o mesmo layout.

Passos:
1. selecionar itens emissivos;
2. filtrar por visibilidade e depth policy;
3. agrupar por mesh e recursos necessários;
4. preencher dados emissivos por instância;
5. enviar buffer;
6. executar um `drawIndexed(indexCount, instanceCount)` por lote.

Se o passe de emissão usa somente matriz e emissive, pode criar um layout menor para reduzir upload. Medir antes de duplicar formatos.

## 7.6 Transparência: otimização segura

O caminho atual ordena do fundo para frente e desenha individualmente. Não remover essa ordenação.

Primeira melhoria segura:
- ordenar por profundidade;
- percorrer na ordem;
- agrupar somente sequências adjacentes com mesma malha, pipeline, textura e estado;
- se agrupar itens adjacentes, preservar a ordem de instâncias dentro do lote;
- não mover um item para perto de outro lote se isso alterar a ordem de composição.

Mais tarde, avaliar:
- weighted blended OIT;
- depth pre-pass para transparência seletiva;
- materiais transparentes por categoria;
- GPU sorting, se comprovadamente necessário.

Essas opções mudam a semântica visual e não fazem parte da primeira entrega.

## 7.7 Critérios de aceite P2

- Opaque mantém imagem equivalente.
- Draw Calls do passe opaco aproximam-se da quantidade de lotes, não da quantidade de entidades.
- Emissão deixa de produzir um draw por objeto quando os itens são compatíveis.
- Transparência mantém a ordem e a aparência.
- Buffer uploads e pipeline switches são registrados.
- Testes de instancing com 1, 2, 4096 e mais de 4096 instâncias.

---

# 8. Implementação P3 — cache de sombras correto e por região

## 8.1 Cache separado por tipo

Manter caches independentes:
- `CsmShadowCache` por vista/câmera e cascata;
- `PointShadowCache` por luz e face;
- `SpotShadowCache` por luz.

CSM depende da câmera e da luz direcional. Point/Spot dependem da transformação/parâmetros da luz e dos casters relevantes, não da câmera diretamente.

## 8.2 Assinatura por face

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
    u32 casterCount;
    u64 casterHash;
    u32 pipelineRevision;
};
```

Hash deve ser determinístico. Não usar somente ponteiros crus como identidade durável. Se o sistema não possui revisão de transformação, adicionar um contador de versão atualizado quando a transformação mundial mudar.

A assinatura de face inclui somente casters que intersectam a face. Isso permite atualizar uma face sem invalidar as outras.

## 8.3 Invalidação

Invalidar face quando:
- luz se move/rotaciona;
- alcance/ângulo/tipo muda;
- matriz de projeção muda;
- resolução muda;
- caster entra/sai da face;
- caster muda transformação;
- mesh muda;
- material de sombra muda;
- parâmetros do passe mudam.

Não invalidar quando:
- apenas a câmera se move e o conjunto de luz/casters permanece idêntico;
- objeto que não é caster se move;
- objeto fora da região de influência se move;
- parâmetro visual que não participa do shader de sombra muda.

## 8.4 Atualização parcial

Para Point Light, não tratar seis faces como uma unidade indivisível. Guardar validade por face.

```cpp
for (u32 face = 0; face < light.faces; ++face) {
    if (cache.face[face].signature == newSignature[face])
        continue;

    renderPointShadowFace(light, face, queues.point[lightIndex][face]);
    cache.face[face].signature = newSignature[face];
}
```

A implementação atual usa uma faixa `R x 6R` por luz e viewport por face. Essa organização pode ser preservada inicialmente; a fila e o cache passam a ser por face.

## 8.5 Orçamento de sombras

O limite atual de Point/Spot shadows é configurável (`pointShadowLights`, com limite máximo de quatro no renderer) e seleciona luzes próximas conforme a lógica de iluminação. Evoluir para um orçamento mais explícito:

- máximo de luzes com sombra;
- máximo de faces atualizadas por quadro;
- resolução por tier;
- prioridade por distância, importância visual, tamanho projetado e movimento;
- atualização em frames alternados para luzes de baixa prioridade.

Não deixar uma luz dinâmica sem atualizar indefinidamente. Usar política de aging e limite máximo de atraso.

## 8.6 Segurança do cache

O cache não pode ser marcado válido se:
- criação do target falhar;
- gravação de comandos falhar;
- submissão falhar;
- a assinatura for incompleta;
- houve troca de dispositivo/backend;
- o recurso foi recriado.

Ao trocar API, invalidar recursos e assinaturas, como o roadmap já exige para as sombras.

## 8.7 Critérios de aceite P3

- Imagem idêntica entre cache ligado e desligado, dentro da tolerância.
- Mover somente a câmera não refaz Point Shadows quando a seleção de luz/casters não muda.
- Mover caster invalida apenas as faces que ele intersecta.
- Mover luz invalida as faces daquela luz.
- Trocar backend invalida tudo.
- Profiler mostra faces reutilizadas, refeitas e motivo da invalidação.

---

# 9. Implementação P4 — submissão de comandos e Diligent

## 9.1 Problema a medir

O backend Diligent implementa `submit(ICommandList&)` com `ctx->Flush()`. O renderizador chama `submit` ao fim de vários passes. Isso pode aumentar custo de CPU/submissão, mas a consequência real depende da implementação de `DiligentCommands`, do contexto e das transições.

## 9.2 Experimento controlado

Criar três modos temporários de benchmark:
- A: comportamento atual, flush por submit;
- B: gravação de listas e flush agrupado no final do conjunto seguro;
- C: submissão agrupada por dependências (por exemplo, sombras antes do passe que as lê).

Medir CPU submit, GPU time, frame latency e estabilidade em Vulkan, D3D12 e OpenGL.

Não agrupar comandos que dependem de resultados intermediários sem garantir a ordem e as transições.

## 9.3 Evolução de interface

Separar conceitos:
- `finishCommandList`: encerra gravação;
- `submit`: envia trabalho ao contexto;
- `flush`: força progresso/submissão conforme backend;
- `present`: apresenta e fecha frame/sincroniza recursos de frame.

Evitar adicionar `flush()` público se o contrato não precisar dele. A primeira etapa pode manter a API e alterar somente o backend após teste.

## 9.4 Recursos importados da Light Prism

O renderizador importa buffers nativos da Light Prism para o RHI. Ao alterar submissão e vida útil, garantir:
- mesmo dispositivo/contexto;
- recursos válidos até o último comando que os utiliza;
- transições de estado corretas;
- não liberar/importar handles antes de os comandos consumirem o recurso;
- sincronização correta entre trabalho compute da Light Prism e o passe PBR.

## 9.5 Critérios de aceite P4

- Sem mudança de imagem.
- Sem validação de recursos/transições no Diligent.
- Sem regressão de sincronização.
- CPU submit menor ou igual no benchmark representativo.
- GPU time e frame latency não pioram de forma relevante.
- Testes em todos os backends suportados disponíveis; backends não testados ficam explicitamente marcados como pendentes.

---

# 10. Implementação P5 — RHI preparado para compute e indirect draw

Essa etapa é posterior a P0–P4. O `rhi.hpp` atual não expõe um contrato completo de compute dispatch nem de indirect draw; não introduzir essas funções como se já existissem.

## 10.1 Extensões possíveis

Adicionar capacidades opcionais:

```cpp
struct DeviceFeatures {
    bool computeShaders = false;
    bool indirectDraw = false;
    bool timestamps = false;
    bool storageBuffers = false;
};

class IDevice {
public:
    virtual DeviceFeatures features() const = 0;
};
```

Possíveis operações futuras:

```cpp
class ICommandList {
public:
    virtual void dispatch(u32 groupsX, u32 groupsY, u32 groupsZ) {}
    virtual void drawIndexedIndirect(
        BufferId argumentBuffer,
        u32 byteOffset,
        u32 drawCount,
        u32 stride) {}
};
```

Essas assinaturas são ilustrativas. O contrato definitivo deve considerar:
- alinhamento e formato do argumento indireto;
- limite de draw count;
- barreiras entre compute e graphics;
- suporte real do Diligent e de cada backend;
- fallback CPU quando indisponível.

## 10.2 GPU culling

Fluxo futuro:

```text
Instance bounds buffer
    |
Compute shader: frustum test
    |
Visible instance index buffer + counters
    |
Batch/indirect argument generation
    |
drawIndexedIndirect
```

Não mover toda a lógica para GPU de uma vez. Manter o resultado comparável ao culling CPU e criar modo de validação que desenhe a diferença entre os dois.

## 10.3 Critérios de aceite P5

- Resultado visual equivalente ao CPU culling.
- Fallback funcional.
- Sem overflow de buffers/counters.
- Sem dependência de uma API específica.
- Ganho medido em GPU real, não somente redução teórica de draw calls.

---

# 11. Organização de arquivos sugerida

Não criar todos os arquivos de uma vez. Esta é a organização final desejada, implementada em commits pequenos.

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
    shadow_cache.hpp
    render_stats.hpp
    render_pass_executor.hpp
    gpu_renderer.hpp              # fachada/orquestrador
    instance_batch.hpp            # algoritmo de batching
    frustum_cull.hpp              # helper geométrico
```

Implementações `.cpp` podem ser adicionadas se o projeto preferir separar templates/helpers de implementação. Como a base atual usa bastante implementação em headers, seguir o padrão existente e evitar compilar tudo em múltiplas unidades sem necessidade.

## 11.1 Dependências permitidas

```text
render_item       -> math, mesh/material handles
visibility        -> render_bounds, frustum_cull
queue_builder     -> render_item, visibility
batch_builder     -> render_item, batch types
shadow_cache      -> ids/revisions/signatures
pass_executor     -> queues, batches, RHI
gpu_renderer      -> coordena os módulos
RHI               -> não depende de ECS, Scene, Material ou LightPrism
```

O RHI não deve conhecer entidades ECS nem materiais de alto nível.

---

# 12. Plano de implementação em etapas e commits

Cada etapa deve ser pequena, compilável e reversível.

## Etapa 0 — Baseline e telemetria

**Arquivos prováveis:** `rhi.hpp`, `gpu_renderer.hpp`, `diligent_device.cpp`, Profiler/exportação.

Tarefas:
1. definir FrameId/ViewId;
2. adicionar estatísticas por passe;
3. instrumentar todos os pontos de draw;
4. separar Scene/Game;
5. adicionar exportação CSV;
6. adicionar timestamps opcionais ou preparar o contrato;
7. salvar screenshot e baseline.

Entrega: relatório antes da otimização.

## Etapa 1 — Bounds de malha

**Arquivos prováveis:** estrutura `Mesh`, importadores e testes de render.

Tarefas:
1. calcular bounds na criação/importação;
2. guardar revisão da malha;
3. recalcular após edição;
4. testar escala negativa/não uniforme e rotação;
5. validar bounds de modelos importados e primitivas.

Entrega: bounds estáveis sem culling ativo.

## Etapa 2 — Camera frustum culling

**Arquivos prováveis:** `frustum_cull.hpp`, novo `visibility_system.hpp`, `gpu_renderer.hpp`.

Tarefas:
1. extrair frustum da view-projection;
2. testar convenção de clip por backend;
3. filtrar filas opacas/prepass/emissão;
4. manter casters fora da câmera para os passes de sombra;
5. comparar imagem.

Entrega: P2 de câmera integrado e medido.

## Etapa 3 — Shadow culling por região

Tarefas:
1. construir frustum por cascata;
2. construir frustum por face de Point Light;
3. construir frustum de Spot;
4. criar filas independentes;
5. construir batches por fila;
6. medir draw calls e objetos rejeitados.

Entrega: redução de chamadas em cenas com casters fora das regiões.

## Etapa 4 — Cache de sombra por face

Tarefas:
1. identificar IDs/revisões estáveis;
2. gerar assinatura completa;
3. manter validade por face;
4. invalidar por transformação, luz, mesh, material e target;
5. testar câmera móvel, luz móvel, caster móvel e backend trocado.

Entrega: mapas reaproveitados sem artefatos.

## Etapa 5 — Extrair filas e batches

Tarefas:
1. mover seleção de itens para `RenderQueueBuilder`;
2. separar plano de batch e dados de instância;
3. manter PBR opaco igual;
4. criar emissão instanciada;
5. agrupar transparência somente por adjacência segura.

Entrega: `GpuRenderer::end()` menor, com comportamento equivalente.

## Etapa 6 — Submissão Diligent

Tarefas:
1. medir flush atual;
2. criar benchmark A/B;
3. revisar transições e recursos importados;
4. reduzir flush somente onde comprovadamente seguro;
5. executar matriz de backends.

Entrega: menor overhead sem regressão.

## Etapa 7 — Compute/indirect

Somente após P0–P6 anteriores:
1. definir DeviceFeatures;
2. implementar API opcional;
3. implementar compute culling;
4. gerar argumentos indiretos;
5. comparar com CPU;
6. manter fallback.

Entrega: caminho GPU-driven opcional, nunca requisito para rodar a engine.

---

# 13. Estratégia de testes

## 13.1 Testes unitários

**Frustum**
- esfera completamente dentro;
- esfera fora de cada plano;
- esfera tangente;
- esfera atravessando plano;
- near/far;
- projeção perspective e ortográfica;
- convenções de profundidade.

**Bounds**
- mesh sem vértices;
- mesh com vértice extremo;
- escala uniforme;
- escala não uniforme;
- escala negativa;
- rotação;
- transformação hierárquica.

**Batching**
- itens iguais;
- mesh igual e textura diferente;
- textura igual e mesh diferente;
- mais que `maxPerBatch`;
- lote vazio;
- ordem e índices válidos.

**Shadow cache**
- assinatura idêntica reutiliza;
- luz move invalida;
- caster move invalida somente faces intersectadas;
- mesh revision invalida;
- resolução invalida;
- troca de backend invalida;
- falha de render não marca cache válido.

## 13.2 Testes de imagem

Criar cenas determinísticas:
1. cubos repetidos com uma única malha;
2. objetos espalhados dentro/fora da câmera;
3. caster fora da câmera projetando sombra visível;
4. Point Light com casters em cada direção;
5. Spot Light com caster dentro e fora do cone;
6. duas luzes pontuais com sombras;
7. emissivos atrás e na frente de obstáculos;
8. múltiplas camadas transparentes.

Comparar:
- imagem de referência;
- imagem otimizada;
- diferença absoluta por canal;
- porcentagem de pixels acima do limiar;
- regiões críticas definidas manualmente.

Não usar somente hash de imagem, pois pequenas diferenças de driver e precisão podem gerar falsos negativos. Manter também uma métrica de erro/tolerância.

## 13.3 Matriz de APIs

O repositório utiliza Diligent como abstração e tem caminhos para Vulkan, D3D12, D3D11 e GLES/OpenGL, mas o roadmap registra pendências de validação visual/execução em GPU real para alguns backends. Assim:

| Backend | Validar |
|---|---|
| Vulkan | caminho principal, timestamps, sombras, instancing, transições |
| D3D12 | compilação HLSL/DXIL, bindings, recursos e timestamps |
| D3D11 | suporte de recursos e fallback de capacidades |
| OpenGL/GLES | GLSL gerado, clip/depth, sombras, Bloom e paridade visual |
| Software/Null | testes de lógica e fallback, não comparar GPU time |

Não declarar todos os backends validados apenas porque compartilham a interface RHI.

---

# 14. Benchmark padrão

## 14.1 Cena fixa

Construir um projeto de benchmark versionado com:
- 1 malha de cubo compartilhada;
- 1 esfera;
- 1 plano;
- materiais repetidos e materiais distintos;
- Point/Spot e directional light;
- casters estáticos e dinâmicos;
- emissivos;
- transparentes;
- câmera fixa e trajetória de câmera reproduzível.

## 14.2 Escalas

Executar com:
- 10 objetos;
- 100 objetos;
- 500 objetos;
- 1.000 objetos;
- 5.000 objetos.

Separar testes com:
- todos visíveis;
- 80% fora do frustum;
- objetos fora da câmera, mas dentro da região de sombra;
- muitas malhas diferentes;
- uma malha/material compartilhado.

## 14.3 Modos de iluminação

Executar:
- sem sombra;
- CSM 1/2/3/4 cascatas;
- uma Point Light sem sombra;
- uma Point Light com sombra;
- duas Point Lights com sombra;
- limite máximo configurado;
- SSAO ligado/desligado;
- Bloom ligado/desligado;
- transparência ligada/desligada.

## 14.4 CSV

Campos mínimos:

```csv
frame,view,pass,draw_calls,instances,visible_items,culled_items,
triangles,pipeline_switches,buffer_upload_bytes,cpu_ms,gpu_ms,
shadow_faces_rendered,shadow_faces_reused
```

O valor `gpu_ms` deve ficar vazio ou `-1` quando não houver timestamp disponível. Não preencher com CPU time.

## 14.5 Métricas de aprovação

Uma alteração só é aprovada quando:
- imagem equivalente;
- não há erros/warnings novos de validação;
- draw calls do passe-alvo diminuem ou permanecem justificadamente iguais;
- CPU/GPU não apresentam regressão relevante;
- memória e buffer uploads são observados;
- pelo menos um backend principal foi executado em GPU real;
- demais backends recebem status de validação explícito.

---

# 15. Riscos e decisões de engenharia

| Risco | Efeito | Mitigação |
|---|---|---|
| Culling agressivo | objetos/sombras desaparecem | bounds conservadores, teste de borda e debug draw |
| Cache incompleto | sombras congeladas ou incorretas | assinatura por revisão e invalidação explícita |
| BatchKey insuficiente | materiais/estados incorretos | chave derivada do estado realmente bindado |
| BatchKey excessiva | fragmentação de lotes | dados por instância e medição |
| Transparência reordenada | composição visual errada | agrupar somente adjacentes |
| Flush removido cedo | sincronização/recursos incorretos | benchmark e validação Diligent |
| API RHI muito específica | quebra portabilidade | capacidades opcionais e fallback |
| Estatística agregada errada | otimização baseada em dado falso | FrameId/ViewId/PassId |
| Cache de recurso sem vida útil | uso-after-free na GPU | frame-in-flight e destruição adiada |
| Multi-view compartilhando cache | Scene/Game contaminam dados | cache por escopo e chave correta |

## 15.1 Decisão: manter Light Prism como dona do sistema de iluminação

`lighting.hpp` declara que a Light Prism calcula luzes, clusters, CSM e BRDF, enquanto a engine faz a ponte e os renderizadores consomem `LightFrame`. Essa divisão deve continuar.

A nova arquitetura não deve duplicar:
- cluster assignment;
- construção da BRDF;
- cálculo CSM já oferecido pela Light Prism.

A engine deve concentrar seus esforços em:
- seleção de casters;
- culling por face/região;
- fila e cache de sombras Point/Spot que são responsabilidade do renderer da engine;
- integração de passes;
- estatísticas e submissão.

## 15.2 Decisão: cache por vista quando o resultado depende da vista

O roadmap registra que CPU já separou caches/assinaturas Scene e Game, mas GPU ainda foi apontada como compartilhada em uma etapa anterior. Confirmar o estado atual antes de alterar. O princípio é: todo recurso cujo conteúdo dependa da câmera ou do tamanho da vista deve ter chave por vista. Mapas Point/Spot não precisam ser duplicados por vista se a luz/casters forem idênticos e o cache for compartilhável com vida útil correta.

## 15.3 Decisão: não exigir compute para a engine funcionar

O RHI deve poder informar capacidades e oferecer fallback. O caminho CPU deve continuar sendo referência de correção e compatibilidade.

---

# 16. Debug visual e ferramentas do editor

Adicionar modos de visualização temporários:

- bounds das malhas;
- frustum da câmera;
- frustum de cada cascata;
- frustum das faces Point/Spot;
- casters aceitos/rejeitados;
- cor diferente por lote;
- ID da face/cascata;
- mapa de sombra individual;
- estado de cache: atualizado/reutilizado/inválido;
- motivo de invalidação.

No Profiler, adicionar uma tabela:

| Pass | Draws | Instances | Visible | Culled | CPU ms | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| CSM 0 | ... | ... | ... | ... | ... | ... |
| CSM 1 | ... | ... | ... | ... | ... | ... |
| Point 0 Face 0 | ... | ... | ... | ... | ... | ... |
| Prepass | ... | ... | ... | ... | ... | ... |
| Opaque | ... | ... | ... | ... | ... | ... |
| Emission | ... | ... | ... | ... | ... | ... |
| Transparent | ... | ... | ... | ... | ... | ... |

Permitir expandir Point Light por face. Mostrar separadamente:
- draws emitidos;
- faces refeitas;
- faces reutilizadas;
- itens rejeitados por culling;
- tempo CPU;
- tempo GPU quando disponível.

---

# 17. Critérios finais de conclusão

A arquitetura pode ser considerada implementada quando:

1. coleta de cena, visibilidade, filas, batches e execução estiverem separados;
2. o `GpuRenderer` atuar principalmente como orquestrador;
3. cada passe possuir estatísticas confiáveis;
4. culling da câmera estiver integrado e validado;
5. CSM/Point/Spot tiverem culling espacial próprio;
6. cache de sombra possuir assinatura completa e invalidação por face;
7. opaco e emissão usarem batching correto;
8. transparência mantiver a ordem visual;
9. submissão Diligent tiver sido medida e otimizada com segurança;
10. Profiler separar CPU e GPU;
11. benchmarks reproduzíveis cobrirem escalas e cenários;
12. os backends tiverem estado de validação documentado.

## 17.1 Metas de desempenho

Não fixar um número universal de Draw Calls para qualquer cena. O resultado depende de malhas, materiais, passes ativos, luzes com sombra e visibilidade.

Metas melhores:
- Draw Calls do opaco próximos ao número de batches compatíveis;
- Draw Calls de sombras próximos ao número de batches visíveis por região;
- nenhuma submissão de lote vazio;
- zero passe de emissão quando não houver emissivos;
- zero passe de transparência quando não houver transparentes;
- zero reconstrução de mapa de sombra quando a assinatura permanecer válida;
- nenhuma chamada fullscreen desnecessária quando o efeito estiver desligado;
- CPU/GPU time estáveis em benchmark.

---

# 18. Referências internas do repositório

Arquivos consultados para fundamentar este plano:

- `engine/render/include/tool/render/gpu_renderer.hpp`
- `engine/render/include/tool/render/instance_batch.hpp`
- `engine/render/include/tool/render/lighting.hpp`
- `engine/render/include/tool/render/frustum_cull.hpp`
- `engine/rhi/include/tool/rhi/rhi.hpp`
- `engine/rhi_diligent/src/diligent_device.cpp`
- `apps/editor/editor_state.hpp`
- `docs/ROADMAP.md`
- `docs/CHANGELOG.md`

Links:
- Repositório: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL
- `gpu_renderer.hpp`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/engine/render/include/tool/render/gpu_renderer.hpp
- `instance_batch.hpp`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/engine/render/include/tool/render/instance_batch.hpp
- `lighting.hpp`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/engine/render/include/tool/render/lighting.hpp
- `frustum_cull.hpp`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/engine/render/include/tool/render/frustum_cull.hpp
- `rhi.hpp`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/engine/rhi/include/tool/rhi/rhi.hpp
- `diligent_device.cpp`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/engine/rhi_diligent/src/diligent_device.cpp
- `ROADMAP.md`: https://github.com/espiritoheroico2/My-game-engine---TOOL-ENGINE---EXPERIMENTAL/blob/main/docs/ROADMAP.md

---

# 19. Parecer final

A TOOL ENGINE não precisa abandonar o renderizador atual. O caminho de menor risco é transformar gradualmente o `GpuRenderer::end()` em uma sequência de etapas explícitas, testáveis e mensuráveis.

A maior oportunidade imediata está em reduzir a geometria repetida nos passes de sombra: cada cascata e cada face de Point Light deve receber apenas os casters relevantes. Em seguida, o cache por face evita recalcular mapas que não mudaram. A instrumentação por passe permitirá provar o resultado.

O instancing do passe opaco já está presente e deve ser tratado como uma base existente, não como uma funcionalidade a ser reimplementada do zero. Emissão é um candidato claro para aproveitar o mesmo princípio. Transparência exige uma estratégia própria por causa da ordem de composição.

Por fim, a portabilidade deve continuar sendo responsabilidade do RHI/Diligent. Vulkan, DirectX e OpenGL não devem ganhar renderizadores paralelos: devem executar os mesmos conceitos de alto nível, com capacidades específicas detectadas e fallbacks claros.

**Ordem recomendada:** P0 telemetria → bounds/culling → filas e batches por passe → cache de sombras → submissão Diligent → compute/indirect opcional.

Esse encadeamento reduz o risco de otimizar o subsistema errado e cria uma base de engenharia que pode escalar de cenas pequenas para milhares de objetos sem depender de suposições sobre o número bruto de Draw Calls.
"""