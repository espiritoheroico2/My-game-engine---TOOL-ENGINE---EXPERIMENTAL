# TOOLGAMEENGINE — Auditoria Técnica e Plano de Evolução do SpriteUI

**Tipo:** relatório de auditoria estática, arquitetura, desempenho, qualidade visual e plano de implementação  
**Projeto analisado:** [espiritoheroico2/TOOLGAMEENGINE](https://github.com/espiritoheroico2/TOOLGAMEENGINE)  
**Escopo principal:** `third_party/SpriteUi` e sua integração com a engine/editor  
**Data da consolidação:** 23/11/2026  
**Status:** diagnóstico estático; recomendações ainda precisam ser implementadas e validadas em execução real.

---

## 1. Objetivo e escopo

Este documento consolida a pesquisa realizada no repositório, organiza os problemas encontrados e transforma os achados em um plano de trabalho executável por agentes de programação.

O objetivo não é simplesmente aumentar a taxa de quadros. O SpriteUI precisa evoluir como uma biblioteca de interface confiável, previsível e visualmente refinada, com:

- menor custo de CPU por frame;
- menor quantidade de alocações e cópias;
- atualizações incrementais de layout e renderização;
- batching correto, sem misturar recursos incompatíveis;
- tipografia nítida em diferentes escalas e densidades de pixels;
- cantos, bordas, curvas, linhas e gradientes visualmente consistentes;
- comportamento equivalente entre backends, dentro das capacidades de cada API;
- testes que validem tanto a estrutura quanto os pixels realmente produzidos;
- instrumentação para identificar gargalos reais, em vez de otimizar por suposição.

A análise considera a biblioteca SpriteUI, os adapters, shaders, o caminho SDL usado pelo editor e os sistemas auxiliares de atlas, layout e extração de comandos.

### 1.1 Limitação importante da auditoria

A inspeção foi feita estaticamente sobre o código e a documentação disponíveis no repositório. O ambiente desta auditoria não conseguiu concluir um `git clone` nem executar a compilação do projeto. Portanto:

- nenhum benchmark foi executado nesta auditoria;
- nenhum resultado de GPU foi medido;
- não foi possível comparar imagens renderizadas antes/depois;
- os custos descritos como prováveis são hipóteses fundamentadas na estrutura do código, não medições;
- as recomendações precisam ser confirmadas com testes, profiling e capturas de imagem no ambiente de desenvolvimento.

Os achados são classificados por confiança. Um problema estrutural visível no código não significa necessariamente que ele seja o maior gargalo em uma cena real.

---

## 2. Resumo executivo

O SpriteUI já possui uma base arquitetural relevante: árvore de layout, widgets, primitivas, extração de comandos, cache de renderização, atlas de texto, instâncias de GPU, shaders e um adapter para a engine. Não há justificativa técnica, com as evidências disponíveis, para descartar tudo e reescrever a biblioteca.

O problema central é a distância entre a arquitetura pretendida e o comportamento efetivo de cada caminho de renderização. Algumas funcionalidades existem em uma camada, mas não necessariamente são usadas por outra. Um shader com suporte a determinada técnica, por exemplo, não melhora a tela do editor se a tela estiver usando um caminho SDL que expande geometria na CPU.

### 2.1 Principais achados

| ID | Área | Achado | Severidade sugerida | Confiança |
|---|---|---|---|---|
| SUI-01 | Render cache | Substituição de segmentos com `erase`/`insert` pode movimentar muitos elementos | Alta | Alta |
| SUI-02 | Layout | Reconstrução recursiva de árvore temporária e trabalho repetido | Alta | Alta |
| SUI-03 | Tipografia | Atlas de cobertura 8-bit não equivale a MSDF; faltam recursos de shaping/kerning robustos | Alta | Alta |
| SUI-04 | Geometria | Cantos arredondados aproximados por poucos segmentos podem apresentar facetas | Média | Alta |
| SUI-05 | RHI | Adapter não respeita integralmente batches e estados de recursos do packet | Crítica | Alta |
| SUI-06 | Amostragem | Caminho de textura usa leitura pontual em vez de sampler filtrado em determinadas rotas | Média | Alta |
| SUI-07 | Atlas do editor | Conjunto de atlas pode crescer consideravelmente; não foi identificada política clara de expulsão no conjunto | Alta | Média |
| SUI-08 | Extração | Ordenação e estruturas temporárias podem gerar trabalho/alocações recorrentes | Média | Média |
| SUI-09 | Testes | Cobertura estrutural não substitui testes de imagem e validação em backends reais | Alta | Alta |
| SUI-10 | Integração | Caminhos SDL e RHI têm capacidades e resultados diferentes; é necessário definir qual é o caminho oficial | Alta | Alta |

### 2.2 Direção arquitetural recomendada

Evoluir para uma arquitetura híbrida, com responsabilidades bem separadas:

1. **UI/Layout:** árvore persistente, medidas em cache, dirty propagation e atualização incremental.
2. **Render extraction:** geração de um packet reutilizável, com memória reaproveitada e ordenação previsível.
3. **Render cache:** armazenamento por blocos/segmentos estáveis, com dirty ranges em vez de movimentar grandes vetores.
4. **Renderer:** batches explícitos, agrupados somente quando textura, material, clip, blend e demais estados forem compatíveis.
5. **Primitivas:** SDF/analítica no backend de shader para formas simples; geometria CPU quando necessária para backends limitados.
6. **Texto:** shaping e posicionamento de glifos independentes do backend, atlas apropriado para a escala e técnica de renderização.
7. **Backends:** SDL, Diligent/RHI e demais rotas com contratos explícitos e testes de conformidade.
8. **Observabilidade:** contadores, timers e benchmarks por etapa, além de testes de imagem.

---

## 3. Arquitetura observada e implicações

### 3.1 A existência de um shader não garante que a tela o esteja usando

O repositório possui um caminho SDL utilizado pela tela inicial/editor e também um adapter RHI com buffers e shaders. Esses caminhos são distintos.

No caminho SDL, a geometria pode ser expandida e enviada por `SDL_RenderGeometry`. Nesse cenário, uma melhoria em um shader do backend RHI não altera automaticamente o resultado visual da tela que continua usando SDL.

**Consequência:** antes de implementar uma melhoria gráfica, o agente deve identificar o caminho de execução usado pelo elemento que está sendo corrigido. Toda tarefa de renderização deve registrar:

- qual backend foi usado;
- qual shader ou rotina CPU gerou a primitiva;
- formato dos vértices/instâncias;
- formato e origem da textura;
- transformação aplicada;
- escala/DPI;
- blend e clip ativos.

### 3.2 Contrato recomendado entre as camadas

O fluxo lógico deve ser:

```text
Widget Tree
    |
    v
Layout System (medidas cacheadas + dirty propagation)
    |
    v
Draw List / Render Extraction
    |
    v
Render Cache (segmentos estáveis + dirty ranges)
    |
    v
Frame Packet (ordem explícita + recursos + clips + batches)
    |
    v
Backend Adapter
    |
    +--> SDL
    +--> Diligent / RHI
    +--> outros backends
```

Cada camada deve ter um contrato documentado. O backend não deve inferir silenciosamente o significado de campos que o packet já fornece.

---

## 4. Achados técnicos detalhados

## SUI-01 — Atualização do RenderListCache pode movimentar grandes intervalos

**Arquivo principal:**  
[`RenderListCache.hpp`](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/include/SpriteUi/Rendering/RenderListCache.hpp)

### Evidência estrutural

A atualização do cache percorre segmentos e, para substituir trechos sujos, utiliza operações equivalentes a `vector::erase` e `vector::insert`. Essas operações podem deslocar os elementos posteriores ao intervalo. O custo pode crescer com o tamanho do vetor, mesmo que apenas um segmento pequeno tenha mudado.

Também há trabalho de visitação dos segmentos durante `Update`, inclusive quando muitos deles estão limpos.

### Impacto potencial

- cópia/movimentação de instâncias em CPU;
- invalidação de referências/iteradores;
- custo proporcional ao tamanho total do vetor em alterações localizadas;
- picos de tempo quando uma pequena alteração ocorre perto do início do vetor;
- custo pouco previsível em interfaces grandes.

### Solução recomendada

Substituir o modelo de compactação imediata por armazenamento em blocos estáveis. Cada segmento deve manter:

- offset/capacidade;
- quantidade válida;
- estado dirty;
- versão;
- identificador estável;
- intervalo de dados afetado.

A atualização de um segmento deve escrever no espaço reservado para ele quando couber. Se não couber, alocar outro bloco e marcar o antigo para recolhimento posterior. A compactação deve ocorrer em momento controlado, não a cada alteração.

### Pseudocódigo de referência

```cpp
struct Segment
{
    uint32_t id = 0;
    uint32_t offset = 0;
    uint32_t count = 0;
    uint32_t capacity = 0;
    uint64_t version = 0;
    bool dirty = false;
};

class StableRenderCache
{
public:
    void updateSegment(Segment& segment,
                       std::span<const Instance> source)
    {
        if (source.size() <= segment.capacity)
        {
            std::copy(source.begin(), source.end(),
                      storage.begin() + segment.offset);

            segment.count = static_cast<uint32_t>(source.size());
            segment.dirty = true;
            ++segment.version;
            return;
        }

        // A real implementation should allocate from a free-list
        // or arena and defer reclamation until no frame references it.
        const uint32_t newOffset =
            allocateBlock(static_cast<uint32_t>(source.size()));

        std::copy(source.begin(), source.end(),
                  storage.begin() + newOffset);

        retireBlock(segment.offset, segment.capacity);

        segment.offset = newOffset;
        segment.count = static_cast<uint32_t>(source.size());
        segment.capacity = static_cast<uint32_t>(source.size());
        segment.dirty = true;
        ++segment.version;
    }

private:
    std::vector<Instance> storage;
};
```

**Atenção:** o pseudocódigo ilustra a política, não é uma implementação pronta para substituir o código atual. Em um renderer assíncrono, blocos antigos não podem ser reutilizados enquanto um frame da GPU ainda os referencia. Use fence/frame index ou uma política de frames-in-flight.

### Critérios de aceitação

- Atualizar um segmento pequeno não deve mover os dados de todos os segmentos seguintes.
- Segmentos limpos não devem ser reconstruídos.
- O cache deve manter a ordem visual original.
- Testar crescimento, redução, remoção e reordenação de segmentos.
- Testar vários frames em voo, caso o backend use buffers assíncronos.
- Benchmark com 100, 1.000, 10.000 e 100.000 instâncias, registrando tempo e bytes copiados.

---

## SUI-02 — LayoutTree reconstrói estruturas temporárias em cada Layout

**Arquivos principais:**  
[`LayoutTree.cpp`](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/src/Layout/LayoutTree.cpp)  
[`LayoutTree.hpp`](https://github.com/espiritoheroico2/TOOLGAMEENGINE/tree/main/third_party/SpriteUi/include/SpriteUi/Layout)

### Evidência estrutural

O fluxo de `LayoutTree::Layout()` constrói uma representação temporária da árvore, executa o layout e depois faz commit dos resultados. A reconstrução recursiva pode ocorrer mesmo quando a maior parte da árvore não sofreu alterações. O layout também usa vetores temporários em operações de medição e distribuição.

### Impacto potencial

- alocações recorrentes;
- recursão e travessia repetidas;
- medição redundante de widgets;
- custo elevado em interfaces com muitos nós;
- picos de CPU quando apenas um controle muda de tamanho.

### Solução recomendada

Adotar layout incremental com flags de invalidação por nó:

```cpp
enum LayoutDirty : uint8_t
{
    DirtyNone       = 0,
    DirtyMeasure    = 1 << 0,
    DirtyArrange    = 1 << 1,
    DirtySubtree    = 1 << 2
};
```

A invalidação deve propagar apenas o necessário:

- mudança de texto: invalida medida do texto, o nó e ancestrais cujo tamanho depende do filho;
- mudança de cor: não invalida layout;
- mudança de posição absoluta: invalida arrange do nó e possivelmente descendentes;
- mudança de tamanho do pai: invalida arrange dos filhos;
- mudança de fonte: invalida medida dos textos afetados.

Reutilizar buffers de scratch por instância de layout. Evitar armazenar temporários em variáveis locais que alocam repetidamente.

### Código-base ilustrativo

```cpp
struct LayoutNode
{
    uint8_t dirty = DirtyMeasure | DirtyArrange | DirtySubtree;
    Vec2 measuredSize{};
    Rect arrangedRect{};
    std::vector<LayoutNode*> children;
};

void markMeasureDirty(LayoutNode& node)
{
    node.dirty |= DirtyMeasure | DirtyArrange;

    // Propagate only while the parent's measurement depends on this child.
    // The actual dependency policy must be defined by the layout model.
}

void markArrangeDirty(LayoutNode& node)
{
    node.dirty |= DirtyArrange | DirtySubtree;
}

void updateLayout(LayoutNode& node, LayoutScratch& scratch)
{
    if (node.dirty & DirtyMeasure)
        measureNode(node, scratch);

    if (node.dirty & DirtyArrange)
        arrangeNode(node, scratch);

    if (node.dirty & DirtySubtree)
    {
        for (LayoutNode* child : node.children)
            updateLayout(*child, scratch);
    }

    node.dirty = DirtyNone;
}
```

### Cuidados

Não implementar dirty flags sem definir dependências de layout. Em um layout flexível, por exemplo, a alteração da medida de um filho pode alterar a distribuição dos irmãos. A invalidação precisa respeitar o algoritmo (row/column, flex, wrap, constraints, anchors etc.).

### Critérios de aceitação

- Alterar apenas cor não deve executar medição nem arrange.
- Alterar texto deve recalcular somente o texto e os ancestrais dependentes.
- Redimensionar a janela deve invalidar os nós dependentes do viewport.
- Comparar resultados geométricos do layout antigo e novo em uma bateria de casos.
- Adicionar teste que verifica contagem de nós medidos/arranjados.

---

## SUI-03 — Pipeline tipográfico não entrega qualidade e recursos de texto modernos

**Áreas:** geração de atlas, cache de fontes, layout de texto e shaders.

### Evidência estrutural

O gerador `BuildTruetypeAtlas` utiliza bitmap de cobertura de 8 bits. Isso é uma técnica válida para determinados usos, mas não equivale a MSDF. A presença de um caminho de leitura MSDF no shader não comprova que o gerador esteja produzindo dados MSDF.

Também não foi identificado, no caminho examinado, um pipeline completo de shaping e posicionamento avançado que resolva adequadamente kerning, ligaduras e scripts complexos.

### Sintomas que podem ocorrer

- letras que perdem definição quando ampliadas;
- bordas com aparência irregular em escalas não previstas;
- espaçamento visual incorreto em pares específicos;
- acentuação e combinações Unicode incompletas;
- texto em idiomas com shaping complexo renderizado incorretamente;
- diferença de aparência entre tamanhos próximos.

### Solução recomendada: separar shaping, atlas e rasterização

Não misturar as três responsabilidades:

1. **Shaping:** converte texto Unicode em sequência de glifos posicionados. Recomendação: HarfBuzz.
2. **Rasterização/geração de contornos:** obtém métricas, bitmaps ou contornos. Recomendação: FreeType.
3. **Armazenamento e renderização:** atlas com formato e técnica escolhidos de acordo com o uso.

Opções de renderização:

- bitmap coverage: excelente para tamanhos próximos ao tamanho rasterizado;
- SDF: útil para escalas moderadas e formas simples;
- MSDF: melhor preservação de cantos e contornos em uma faixa maior de escalas;
- vetor/tesselação: útil para casos especiais, mas pode aumentar complexidade e custo.

### Estrutura recomendada

```cpp
struct PositionedGlyph
{
    uint32_t glyphId = 0;
    uint32_t cluster = 0;
    Vec2 offset{};
    Vec2 advance{};
};

struct GlyphAtlasEntry
{
    uint32_t page = 0;
    Rect uv{};
    Vec2 bearing{};
    Vec2 size{};
    float distanceRange = 0.0f;
};

struct ShapedText
{
    std::vector<PositionedGlyph> glyphs;
    Vec2 totalAdvance{};
};
```

### Fluxo recomendado

```text
UTF-8
  |
  v
Unicode decoding
  |
  v
HarfBuzz shaping (font + script + language + direction)
  |
  v
Glyph IDs + advances + offsets + clusters
  |
  v
Glyph cache / atlas lookup
  |
  +--> cached glyph: reuse
  |
  +--> missing glyph: FreeType raster/outline
                    |
                    +--> bitmap coverage
                    +--> SDF/MSDF generation
  |
  v
Glyph instances
  |
  v
Renderer
```

### Shader: cobertura bitmap

Para um atlas de cobertura, o fragment shader deve interpretar o canal de cobertura e multiplicá-lo pela cor do texto. O canal não deve ser tratado como cor RGB comum.

```hlsl
float coverage = Atlas.Sample(LinearClamp, input.uv).r;
float alpha = coverage * input.color.a;
return float4(input.color.rgb, alpha);
```

### Shader: MSDF conceitual

```hlsl
float median3(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

float3 sampleValue = Atlas.Sample(LinearClamp, input.uv).rgb;
float signedDistance = median3(
    sampleValue.r, sampleValue.g, sampleValue.b) - 0.5;

float screenPxRange = ComputeScreenPxRange(
    input.uv, input.distanceRange);

float opacity = saturate(
    signedDistance * screenPxRange + 0.5);

return float4(input.color.rgb,
              input.color.a * opacity);
```

O cálculo real de `screenPxRange` precisa considerar derivadas (`fwidth`) e a convenção usada pelo gerador de MSDF. Não copiar esse exemplo sem alinhar o formato do atlas, o range gravado e o shader.

### Critérios de aceitação

- testes com tamanhos pequenos, médios e grandes;
- escala fracionária (ex.: 0,75x, 1,0x, 1,25x, 1,5x, 2x);
- DPI 100%, 125%, 150%, 200%;
- acentos, cedilha, caracteres combinados e Unicode;
- pares com kerning;
- ligaduras e textos em pelo menos um script que exija shaping;
- comparação de screenshots com tolerância de pixels documentada;
- validar fallback para glifos ausentes.

---

## SUI-04 — Cantos arredondados aproximados com poucos segmentos

**Arquivo relacionado:** implementação de primitivas no caminho SDL, incluindo `appendRoundedFan()`.

### Evidência estrutural

A aproximação observada usa seis segmentos por canto. É barata, mas pode gerar facetas em cantos grandes, especialmente com alta densidade de pixels ou escala ampliada. A geometria também não oferece, por si só, antialiasing analítico nas bordas.

### Solução recomendada

Usar duas estratégias:

**Backend com shader:** desenhar um retângulo e calcular a distância ao contorno arredondado no fragment shader. O shader produz cobertura suave com derivadas.

**Backend CPU/SDL:** aumentar a tesselação de forma adaptativa e criar uma faixa de cobertura (fringe) para antialiasing. O número de segmentos deve depender do raio, escala e erro máximo de tela.

### Fórmula de SDF para rounded box

Para ponto `p` relativo ao centro, meia-extensão `b` e raio `r`:

\[
q = |p| - b + r
\]

\[
d = \min(\max(q_x,q_y),0) + \| \max(q,0) \| - r
\]

`d` é a distância assinada aproximada: negativa no interior e positiva no exterior.

### HLSL conceitual

```hlsl
float roundedBoxSdf(float2 p, float2 halfSize, float radius)
{
    float2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0))
         + min(max(q.x, q.y), 0.0)
         - radius;
}

float roundedBoxCoverage(float distance)
{
    float aa = max(fwidth(distance), 1e-4);
    return 1.0 - smoothstep(-aa, aa, distance);
}
```

O shader precisa receber dimensões e raio no mesmo espaço de coordenadas em que o SDF é calculado. Para cantos diferentes, usar quatro raios ou um conjunto de parâmetros específico.

### Tesselação adaptativa no backend CPU

Para arco de raio `r`, erro máximo `e` e ângulo total `theta`, uma estimativa do passo angular pode ser derivada de:

\[
e = r(1-\cos(\Delta\theta/2))
\]

Logo:

\[
\Delta\theta \leq 2\arccos(1-e/r)
\]

Limitar o resultado a um mínimo e máximo de segmentos. O erro `e` deve ser definido em pixels de tela, convertido pela escala atual.

### Critérios de aceitação

- cantos com raios 2, 4, 8, 16, 24, 32 e 64 px;
- escala 0,75x a 3x;
- bordas claras e escuras sobre fundos contrastantes;
- comparar backend CPU e shader;
- testar raio maior que metade da menor dimensão;
- garantir que o raio seja clampado de forma consistente.

---

## SUI-05 — Adapter RHI não respeita integralmente os batches/estados do packet

**Arquivo:**  
[`ToolEngineRhi.hpp`](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/adapters/toolengine/include/SpriteUi/Adapters/ToolEngineRhi.hpp)

<badge color="danger">Severidade crítica</badge>

### Evidência estrutural

No caminho examinado, `SpriteUiRhiBackend::Submit()` percorre instâncias e agrupa por sequência de `clipIndex`, em vez de consumir integralmente `packet.batches`. O adapter também opera com um atlas único e não resolve de forma geral `textureSlice`, `materialIndex` e demais estados de recursos.

### Risco

Um packet que contenha mais de uma página de atlas, textura, material ou estado de blend pode ser renderizado incorretamente se o adapter assumir que todos os elementos usam o mesmo recurso. O erro pode ser silencioso: o desenho aparece, mas com textura/material incorretos.

### Solução recomendada

Definir compatibilidade de batch explicitamente. Dois comandos só podem ser combinados se todos os estados que afetam o resultado forem compatíveis.

Exemplo de chave:

```cpp
struct BatchKey
{
    uint32_t textureId = 0;
    uint32_t textureSlice = 0;
    uint32_t materialId = 0;
    uint32_t clipId = 0;
    uint32_t blendMode = 0;
    uint32_t samplerId = 0;

    friend bool operator==(const BatchKey&, const BatchKey&) = default;
};
```

O batching deve preservar a ordem de desenho quando a transparência ou o contrato de UI exigir. Não ordenar globalmente por textura sem provar que a reordenação é visualmente segura.

### Validação obrigatória

Se o adapter ainda não suportar múltiplas texturas ou materiais, deve falhar explicitamente:

```cpp
if (!resources.supports(instance.textureId,
                        instance.textureSlice,
                        instance.materialId))
{
    return RenderResult::UnsupportedResource;
}
```

Evitar fallback silencioso para atlas zero.

### Critérios de aceitação

- duas texturas diferentes na mesma frame;
- duas páginas do mesmo atlas;
- dois materiais;
- diferentes clips;
- diferentes modos de blend;
- sequência A-B-A que exija preservar ordem;
- verificar que o número de batches corresponde às chaves compatíveis;
- erro explícito para recursos não suportados.

---

## SUI-06 — Amostragem pontual pode prejudicar sprites e texto escalados

**Área:** shaders HLSL/GLSL do backend RHI.

### Evidência estrutural

Foi observado uso de leitura pontual (`Load` em HLSL e `texelFetch` em GLSL) em um caminho de amostragem. Isso é apropriado em casos em que se deseja acesso exato a um texel, mas não substitui uma amostragem filtrada para imagens/sprites escalados.

### Solução recomendada

Separar as políticas de amostragem:

- pixel art: nearest/point;
- sprites comuns: linear com mipmaps quando apropriado;
- atlas de cobertura de texto: linear com padding adequado;
- MSDF: linear, sem tratar o atlas como cor sRGB;
- render targets: política definida por finalidade.

Não usar um único sampler para todos os tipos de recurso.

### Cuidados com atlas

- padding entre regiões;
- extrusão de bordas para sprites;
- UVs consistentes com half-texel quando exigido pela API/estilo de amostragem;
- evitar mipmaps que misturem glifos ou sprites vizinhos;
- para MSDF, respeitar o padding necessário ao range de distância.

### Critérios de aceitação

Testar o mesmo sprite em escalas 0,5x, 1x, 1,5x, 2x e 3x, com nearest e linear. Testar texto pequeno e ampliado. Verificar bleeding entre regiões vizinhas do atlas.

---

## SUI-07 — Crescimento de memória dos atlas do editor

**Arquivo relacionado:** `apps/editor/text_atlas_set.hpp`.

### Evidência e estimativa

O conjunto examinado mantém múltiplos tamanhos de atlas (8–64 px) sob demanda. As páginas observadas incluem dimensões de 512², 1024² e 2048². Considerando as quantidades máximas descritas no código/documentação examinados e assumindo RGBA8 na GPU, a soma teórica pode chegar a aproximadamente:

\[
13 \times 512^2 \times 4 +
20 \times 1024^2 \times 4 +
24 \times 2048^2 \times 4
\]

\[
= 500\,170\,752 \text{ bytes} \approx 477 \text{ MiB}
\]

Isso é um limite teórico estimado para todas as páginas consideradas residentes, não uma medição do consumo atual. O armazenamento CPU em R8, se todas as páginas equivalentes estiverem residentes, seria aproximadamente um quarto desse total, antes de overhead e estruturas auxiliares.

### Solução recomendada

- política explícita de residência e expulsão;
- orçamento de memória por tipo de atlas;
- métricas de bytes CPU/GPU;
- cache LRU ou clock com pinning de fontes atualmente visíveis;
- evitar RGBA quando apenas cobertura R8 é necessária;
- upload incremental de regiões;
- liberar páginas sem referências depois de um período ou pressão de memória;
- considerar atlas por família de fonte e tamanho, ou MSDF para reduzir a quantidade de tamanhos rasterizados.

### Código-base de orçamento

```cpp
struct AtlasMemoryStats
{
    uint64_t cpuBytes = 0;
    uint64_t gpuBytes = 0;
    uint32_t pageCount = 0;
};

struct AtlasBudget
{
    uint64_t maxCpuBytes = 64ull * 1024 * 1024;
    uint64_t maxGpuBytes = 128ull * 1024 * 1024;
};

bool overBudget(const AtlasMemoryStats& stats,
                const AtlasBudget& budget)
{
    return stats.cpuBytes > budget.maxCpuBytes ||
           stats.gpuBytes > budget.maxGpuBytes;
}
```

O valor de orçamento deve ser configurável e medido em máquinas-alvo; os números do exemplo são apenas defaults ilustrativos.

### Critérios de aceitação

- teste de abertura/fechamento de documentos e painéis;
- carregar muitos tamanhos de fonte;
- observar memória antes/depois da limpeza;
- garantir que glifos visíveis ou em uso não sejam expulsos;
- verificar que um glifo removido possa ser recriado corretamente;
- exibir estatísticas de páginas, bytes e cache hit rate.

---

## SUI-08 — Render extraction pode fazer trabalho e alocações recorrentes

**Arquivo relacionado:**  
[`DrawList.cpp`](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/src/Rendering/DrawList.cpp)

### Evidência e confiança

O fluxo de extração examinado ordena elementos e constrói estruturas locais/packet por chamada. Isso pode gerar custo recorrente, especialmente se a capacidade dos vetores não for reaproveitada. O impacto precisa ser confirmado por profiling.

### Solução recomendada

Criar uma API de escrita em destino reutilizável:

```cpp
void BuildInto(const UiTree& tree,
               FramePacket& out,
               RenderScratch& scratch);
```

Antes de limpar os vetores, manter capacidade. Evitar criar vários vetores temporários independentes se um scratch buffer por thread/frame resolver. Se os elementos já estiverem em ordem, detectar a condição monotônica e pular o sort.

### Critérios de aceitação

- contagem de alocações por frame;
- bytes alocados por frame;
- tempo de extração com 1k/10k/100k comandos;
- preservar ordenação e estabilidade;
- nenhuma mudança de resultado visual.

---

## SUI-09 — Testes estruturais não bastam para validar qualidade visual

### Situação

O repositório possui testes de unidade, teste RHI e ferramentas de benchmark/teste GPU. Um backend Null pode validar chamadas e estrutura, mas não comprova que pixels corretos foram produzidos. O teste GPU observado cobre alguns aspectos, mas a suíte precisa ser ampliada para tipografia, curvas, escalas, clipping, blending e diferenças entre backends.

### Suíte recomendada

1. **Golden images:** capturas determinísticas de cenas de referência.
2. **Pixel diff:** comparação com tolerância, além de métricas agregadas.
3. **Testes de geometria:** cantos, bordas, linhas, polígonos e clipping.
4. **Testes de texto:** acentos, shaping, kerning, DPI e escalas fracionárias.
5. **Testes de recursos:** múltiplos atlas, texturas e materiais.
6. **Testes de regressão de memória:** crescimento e expulsão de atlas.
7. **Testes de performance:** CPU, GPU, alocações, bytes copiados e batches.
8. **Matriz de backends:** SDL e RHI, e APIs realmente suportadas pelo build.

### Métricas de imagem

- MAE (erro absoluto médio);
- RMSE;
- porcentagem de pixels acima de uma tolerância;
- comparação de bordas para detectar serrilhado;
- comparação por regiões (texto, bordas, fundos).

Não depender apenas de uma métrica global: um pequeno erro em texto pode ser visualmente importante e diluído por uma grande área de fundo.

---

## SUI-10 — Falta definir o caminho de renderização oficial e sua paridade

### Problema

O projeto possui mais de um caminho de renderização. Isso é saudável se os contratos e as diferenças forem explícitos, mas perigoso se o editor usar um backend enquanto a engine usa outro e as melhorias forem implementadas apenas em um deles.

### Recomendação

Criar uma matriz de capacidades:

| Recurso | SDL | RHI |
|---|---|---|
| Retângulo sólido | suportado | suportado |
| Cantos arredondados SDF | fallback CPU ou não suportado | shader |
| Texto bitmap | suportado conforme atlas | suportado conforme atlas |
| MSDF | definir | definir |
| Múltiplas texturas | validar | implementar/validar |
| Múltiplos materiais | definir | implementar/validar |
| Clip retangular | validar | validar |
| Clip arredondado | fallback ou limitação | shader |
| Mipmaps | conforme suporte SDL | conforme RHI |

A tabela deve ser atualizada com base em testes reais, não apenas na presença de classes ou shaders.

---

## 5. Plano de arquitetura alvo

### 5.1 Princípios

- **Correção antes de batching:** nunca combinar comandos incompatíveis.
- **Incrementalidade:** mudanças locais devem produzir trabalho local sempre que possível.
- **Memória previsível:** evitar alocações e cópias sem orçamento/telemetria.
- **Backend explícito:** cada adapter declara capacidades e limitações.
- **Qualidade mensurável:** screenshots e testes visuais fazem parte da definição de pronto.
- **Compatibilidade:** manter o caminho antigo até que o novo seja validado.
- **Pequenas mudanças:** cada etapa deve gerar um commit revisável.

### 5.2 Modelo de dados sugerido para renderização

```cpp
enum class TextureKind : uint8_t
{
    Color,
    AlphaCoverage,
    Sdf,
    Msdf
};

struct ResourceBinding
{
    uint32_t textureId = 0;
    uint32_t textureSlice = 0;
    uint32_t materialId = 0;
    uint32_t samplerId = 0;
    TextureKind kind = TextureKind::Color;
};

struct DrawState
{
    ResourceBinding resource{};
    uint32_t clipId = 0;
    uint32_t blendMode = 0;
    uint32_t pipelineId = 0;
};

struct DrawCommand
{
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    DrawState state{};
    uint64_t stableOrder = 0;
};
```

O desenho acima é um modelo conceitual. Antes de adotá-lo, o agente deve mapear os tipos já existentes no SpriteUI e reaproveitar as estruturas existentes quando forem equivalentes. Não criar tipos duplicados apenas para seguir este documento.

### 5.3 Ordem de desenho e batching

Para UI com transparência, a ordem de desenho geralmente faz parte do resultado visual. Portanto:

- manter uma chave de estado;
- criar batches de comandos consecutivos compatíveis;
- não fazer sort global por textura como otimização padrão;
- só reordenar elementos explicitamente declarados como independentes/opacos;
- medir draw calls e mudanças de estado separadamente.

---

## 6. Plano de implementação por etapas

Cada etapa inclui objetivo, entregáveis, testes e um prompt que pode ser entregue diretamente a um agente de programação.

**Regra geral para todos os agentes:** primeiro inspecionar os arquivos reais e mapear a implementação existente. Não assumir que o pseudocódigo deste documento coincide com a API atual. Preservar compatibilidade, fazer mudanças pequenas, compilar/testar quando possível e relatar claramente o que não foi executado.

---

## ETAPA 0 — Baseline, inventário e instrumentação

**Prioridade:** máxima  
**Risco:** baixo  
**Objetivo:** medir o estado atual antes de otimizar.

### Tarefas

1. Identificar os caminhos de renderização realmente usados pelo editor e pela engine.
2. Mapear o fluxo Widget → Layout → DrawList → FramePacket → Backend.
3. Registrar alocações por frame, tempo por etapa, bytes copiados, quantidade de instâncias, batches, draw calls e memória dos atlas.
4. Criar cenas determinísticas de benchmark:
   - UI pequena;
   - painel com muitos widgets;
   - lista longa;
   - texto abundante;
   - muitas imagens;
   - muitos clips;
   - atualizações localizadas;
   - atualização de toda a árvore.
5. Salvar imagens baseline para testes de regressão.

### Definição de pronto

- documento de arquitetura atual;
- tabela dos caminhos de backend;
- benchmark reproduzível;
- métricas coletadas sem alterar o resultado visual;
- screenshots baseline.

### Prompt para o agente

```text
Você é um engenheiro sênior de C++20, renderização em tempo real e profiling.

Tarefa: fazer o inventário técnico do SpriteUI no repositório TOOLGAMEENGINE e criar um baseline mensurável antes de qualquer refatoração.

Instruções:
1. Inspecione os arquivos reais. Não deduza a arquitetura apenas pelos nomes das classes.
2. Mapeie o fluxo completo: árvore de widgets, layout, draw list, extração, cache, frame packet e backend.
3. Identifique qual backend é usado pelo editor e quais caminhos são usados pela engine.
4. Localize benchmarks e testes existentes; não duplique ferramentas sem necessidade.
5. Adicione instrumentação de baixo impacto para medir:
   - tempo de layout;
   - tempo de extração;
   - tempo de atualização do cache;
   - tempo de submit;
   - quantidade de instâncias e batches;
   - draw calls e mudanças de estado;
   - alocações/bytes alocados quando viável;
   - memória CPU/GPU dos atlas.
6. Crie cenas de benchmark determinísticas e screenshots baseline.
7. Não faça otimizações nesta etapa, exceto a instrumentação indispensável.

Restrições:
- C++20 e padrões de estilo existentes.
- Não quebrar APIs públicas.
- Não adicionar dependências grandes sem justificativa.
- Não inventar resultados de GPU caso o ambiente não permita executá-los.
- Se não puder compilar, registre o comando tentado e o erro.

Entregáveis:
- inventário da arquitetura atual;
- lista de caminhos de renderização;
- métricas baseline;
- comandos exatos para reproduzir;
- lista de problemas confirmados e hipóteses separadas;
- alterações pequenas e testáveis.

Antes de concluir, faça uma segunda revisão crítica procurando instrumentação que altere o timing, métricas enganosas e benchmarks não determinísticos.
```

---

## ETAPA 1 — Corrigir contrato de recursos e batching do RHI

**Prioridade:** crítica  
**Risco:** médio  
**Dependências:** Etapa 0

### Tarefas

1. Inspecionar `FramePacket`, batches, instâncias, materiais, atlas, clips e blend states.
2. Definir exatamente quais campos determinam compatibilidade de batch.
3. Fazer o adapter consumir os batches existentes ou construir batches consecutivos com a mesma chave.
4. Preservar a ordem de desenho.
5. Suportar múltiplas texturas/páginas quando o RHI permitir.
6. Se ainda não suportar determinado recurso, retornar erro explícito.
7. Criar testes com duas texturas, duas páginas, dois materiais, clips e sequência A-B-A.

### Definição de pronto

- nenhuma textura/material incorreto em casos multi-recurso;
- ordem de desenho preservada;
- recursos não suportados geram diagnóstico;
- batches contabilizados corretamente;
- teste visual no backend RHI.

### Prompt para o agente

```text
Atue como engenheiro sênior de renderização C++ e APIs gráficas.

Tarefa: auditar e corrigir o adapter SpriteUI RHI, especialmente SpriteUiRhiBackend::Submit(), para que ele respeite integralmente o contrato de recursos e batches do FramePacket.

Arquivo inicial:
third_party/SpriteUi/adapters/toolengine/include/SpriteUi/Adapters/ToolEngineRhi.hpp

Procedimento obrigatório:
1. Leia as definições reais de FramePacket, DrawCommand, Batch, Instance, Clip, Material e Texture.
2. Descubra como os batches são construídos e quais campos representam textura, textureSlice, material, sampler, blend, pipeline e clip.
3. Verifique se Submit() ignora campos ou assume atlas único.
4. Implemente batching apenas entre comandos consecutivos compatíveis.
5. Preserve a ordem original de desenho. Não faça sort global por textura.
6. Se um recurso não for suportado, retorne erro explícito ou registre diagnóstico inequívoco; nunca use atlas/material padrão silenciosamente.
7. Reutilize estruturas existentes. Não crie uma segunda representação de estado sem necessidade.
8. Adicione testes para:
   - textura A e textura B;
   - duas páginas de atlas;
   - materiais distintos;
   - clips diferentes;
   - blend states diferentes;
   - sequência A-B-A;
   - recurso inválido ou não suportado.
9. Verifique lifetime dos recursos e sincronização com frames em voo.

Restrições:
- Não alterar a ordem visual para reduzir draw calls.
- Não presumir que todos os backends possuem os mesmos recursos.
- Não declarar suporte antes de existir teste real.
- Mudanças pequenas, com comentários apenas onde esclarecem contratos.

Entregáveis:
- correção do adapter;
- testes unitários e/ou GPU;
- tabela de capacidades atualizada;
- explicação de compatibilidade de batch;
- benchmark antes/depois, se executável.

Faça uma segunda revisão crítica procurando mistura indevida de recursos, uso de handles expirados, sincronização incorreta e regressões de transparência.
```

---

## ETAPA 2 — RenderListCache estável e incremental

**Prioridade:** alta  
**Risco:** médio/alto  
**Dependências:** Etapa 0

### Tarefas

1. Mapear como segmentos são identificados e invalidados.
2. Remover movimentação global causada por `erase`/`insert` em alterações locais.
3. Adotar blocos estáveis, free-list ou arena segmentada.
4. Marcar dirty ranges e enviar apenas intervalos alterados ao backend.
5. Definir política de compactação e lifetime para buffers usados pela GPU.
6. Adicionar contadores de bytes copiados e segmentos atualizados.

### Prompt para o agente

```text
Você é especialista em estruturas de dados, C++20 e renderização em tempo real.

Tarefa: melhorar o RenderListCache do SpriteUI para que alterações locais não provoquem movimentação desnecessária de grandes intervalos do vetor.

Arquivo inicial:
third_party/SpriteUi/include/SpriteUi/Rendering/RenderListCache.hpp

Investigue também os arquivos de implementação e todos os consumidores do cache.

Passos:
1. Documente o algoritmo atual, invariantes, ordenação e ciclo de vida dos segmentos.
2. Identifique exatamente onde erase/insert ou operações equivalentes movem elementos.
3. Proponha uma estrutura de armazenamento estável compatível com a API atual.
4. Implemente atualização in-place quando houver capacidade.
5. Para crescimento, use alocação por blocos/free-list/arena, conforme encaixe melhor na arquitetura existente.
6. Marque intervalos dirty e exponha ao backend somente os ranges necessários.
7. Defina quando blocos antigos podem ser reutilizados. Se houver frames em voo, respeite fences ou mecanismo equivalente.
8. Evite compactação a cada update; se necessária, faça-a em ponto controlado.
9. Instrumente bytes copiados, segmentos visitados, segmentos alterados e tempo de update.

Testes:
- adicionar segmento;
- remover segmento;
- reduzir e aumentar quantidade;
- crescimento além da capacidade;
- alteração no primeiro, meio e último segmento;
- múltiplos segmentos dirty;
- preservar ordem;
- múltiplos frames em voo, se aplicável.

Não implemente uma arena complexa sem demonstrar necessidade. Compare pelo menos duas opções e escolha a mais simples que cumpra os requisitos.

Entregáveis:
- código;
- testes;
- análise de complexidade;
- benchmark antes/depois;
- riscos e limitações.

Faça revisão crítica para detectar corrupção de offsets, uso após liberação, fragmentação excessiva e invalidação incompleta.
```

---

## ETAPA 3 — Layout incremental e reutilização de memória

**Prioridade:** alta  
**Risco:** alto  
**Dependências:** Etapa 0

### Tarefas

1. Definir dependências de medida e arrange.
2. Introduzir flags dirty por nó.
3. Propagar invalidação apenas aos ancestrais/descendentes dependentes.
4. Cachear medidas intrínsecas (texto, imagem, widget).
5. Reutilizar scratch buffers.
6. Manter caminho de referência antigo temporariamente para comparar resultados.

### Prompt para o agente

```text
Atue como arquiteto de UI e engenheiro C++20.

Tarefa: transformar o layout do SpriteUI em um sistema incremental, reduzindo reconstruções e medições desnecessárias sem alterar a geometria final.

Arquivos iniciais:
third_party/SpriteUi/src/Layout/LayoutTree.cpp
third_party/SpriteUi/include/SpriteUi/Layout/

Antes de codar:
1. Leia o algoritmo completo de layout.
2. Identifique todos os modos de layout e dependências entre pai e filhos.
3. Mapeie quais propriedades afetam medida, arrange, desenho ou apenas estilo.
4. Documente como Build(), Layout() e Commit() funcionam hoje.

Implementação:
1. Defina flags de invalidação separadas para medida, arrange e subtree.
2. Faça a invalidação propagar de acordo com as dependências reais do algoritmo.
3. Cacheie medidas intrínsecas dos widgets.
4. Mudanças apenas visuais (cor, opacidade, textura sem mudança de tamanho) não devem disparar layout.
5. Reutilize buffers temporários com scratch storage.
6. Evite reconstruir a árvore temporária inteira se os nós persistentes puderem armazenar os dados necessários.
7. Preserve o algoritmo antigo como referência durante a transição, se isso for viável.
8. Adicione contadores de nós medidos, arranjados e visitados.

Testes:
- árvore vazia e árvore profunda;
- row/column/flex/wrap/anchors conforme suportados;
- mudança de texto;
- mudança de fonte;
- resize do viewport;
- alteração de padding/margin;
- alteração apenas de cor;
- adição/remoção de filho;
- mudanças simultâneas em pai e filho.

Critério principal: o layout incremental deve produzir a mesma geometria do algoritmo de referência para os mesmos inputs.

Não use dirty flags genéricas sem explicar dependências. Não elimine recálculos necessários em layouts cujos filhos influenciam a medida do pai.

Entregáveis:
- código e testes;
- diagrama de invalidação;
- comparação geométrica;
- contagem de trabalho antes/depois;
- lista de casos ainda não incrementais.

Faça segunda revisão crítica focada em invalidação perdida, ciclos, dependências entre irmãos e cache obsoleto.
```

---

## ETAPA 4 — Tipografia HD: shaping, atlas e MSDF

**Prioridade:** alta  
**Risco:** alto  
**Dependências:** Etapa 0; idealmente Etapa 1 para bindings de recursos

### Tarefas

1. Mapear o pipeline atual de texto do UTF-8 até o draw.
2. Separar decodificação, shaping, métricas, cache, rasterização e renderização.
3. Avaliar integração de FreeType e HarfBuzz.
4. Definir bitmap coverage para tamanhos pequenos e MSDF para escalas maiores, conforme resultados.
5. Definir padding, range, formato de textura e sampler.
6. Criar cache por fonte/face/estilo/glifo e estratégia de expulsão.
7. Adicionar fallback Unicode.

### Prompt para o agente

```text
Você é especialista em tipografia digital, FreeType, HarfBuzz, SDF/MSDF e C++20.

Tarefa: projetar e implementar uma evolução do pipeline de texto do SpriteUI para melhorar nitidez em HD, escala fracionária e suporte a Unicode.

Primeiro faça uma auditoria:
1. Siga o texto desde UTF-8 até as instâncias enviadas ao renderer.
2. Identifique o que BuildTruetypeAtlas realmente gera.
3. Verifique se o caminho chamado MSDF recebe de fato dados MSDF.
4. Localize métricas de glifo, avanço, bearing, kerning, clusters e fallback.
5. Não declare suporte a shaping/kerning se não estiver implementado e testado.

Arquitetura alvo:
- decodificação Unicode;
- shaping separado do rasterizador;
- HarfBuzz para glyph IDs, advances, offsets e clusters, se aprovado como dependência;
- FreeType para métricas, bitmap e contornos;
- cache de glifos;
- atlas bitmap coverage e/ou MSDF com metadados consistentes;
- shader específico para cada tipo de atlas.

Antes de adicionar dependências:
- verificar licenças e compatibilidade com o projeto;
- avaliar tamanho, build, plataformas e manutenção;
- apresentar uma alternativa mínima caso a dependência não seja aceitável.

Implementar em fases pequenas:
A. corrigir métricas/posicionamento e testes Unicode;
B. integrar shaping;
C. adicionar atlas MSDF real, com gerador validado;
D. adaptar shader e sampler;
E. adicionar política de memória/expulsão.

Testes:
- português com acentos e cedilha;
- kerning em pares conhecidos;
- ligaduras;
- texto em escala fracionária;
- DPI 100/125/150/200%;
- glifos ausentes e fallback;
- diferentes tamanhos e fontes;
- comparação de golden images.

Não misture o formato de atlas bitmap com MSDF. Documente o range e a convenção numérica usada pelo gerador e pelo shader.

Entregáveis:
- arquitetura do pipeline;
- código incremental;
- testes;
- comparação visual;
- análise de dependências/licenças;
- custo de memória e desempenho.

Faça segunda revisão crítica de shaping, clusters, fallback, alinhamento de baseline, line height, atlas bleeding e consistência entre gerador e shader.
```

---

## ETAPA 5 — Formas, curvas, bordas e antialiasing

**Prioridade:** alta para qualidade visual  
**Risco:** médio  
**Dependências:** Etapas 0 e 1

### Tarefas

1. Catalogar todas as primitivas e caminhos de desenho.
2. Definir um modelo comum de parâmetros visuais.
3. Implementar rounded-box SDF no RHI.
4. Implementar fallback CPU com tesselação adaptativa e fringe.
5. Corrigir linhas, bordas internas/externas e alinhamento de pixels.
6. Criar golden images.

### Prompt para o agente

```text
Atue como engenheiro gráfico especializado em rasterização 2D, SDF e antialiasing.

Tarefa: melhorar a qualidade de primitivas do SpriteUI, começando por retângulos arredondados, bordas e curvas.

Investigue:
- appendRoundedFan() e demais rotinas de geometria CPU;
- shaders HLSL/GLSL;
- convenções de coordenadas;
- transformações e DPI;
- clipping;
- blend e premultiplicação de alpha.

Implementação:
1. Não substitua tudo de uma vez.
2. Adicione rounded-box SDF no backend shader, se o contrato de instâncias permitir.
3. Use fwidth/derivadas para antialiasing em espaço de tela.
4. Para backend CPU/SDL, calcule tesselação adaptativa com erro máximo em pixels.
5. Considere fringe geometry para cobertura suave.
6. Defina clamp dos raios e suporte a raios diferentes por canto, se a API permitir.
7. Garanta consistência de borda interna/externa e espessura.
8. Evite antialiasing duplo (geometria fringe + shader AA simultaneamente).

Testes:
- raios pequenos e grandes;
- escalas fracionárias;
- transformações não uniformes;
- retângulos muito finos;
- bordas internas e externas;
- fundos claros/escuros;
- clipping e transparência;
- screenshot em SDL e RHI.

Entregáveis:
- código;
- documentação matemática;
- golden images;
- comparação de custo CPU/GPU;
- tabela de suporte por backend.

Faça segunda revisão crítica para detectar halos, bordas borradas, inconsistência de alpha, custo excessivo de fragment shader e diferenças entre APIs.
```

---

## ETAPA 6 — Samplers, atlas e política de memória

**Prioridade:** alta  
**Risco:** médio  
**Dependências:** Etapas 1 e 4

### Tarefas

1. Criar tipos de textura e políticas de sampler.
2. Separar nearest, linear, mipmapped e políticas específicas de SDF/MSDF.
3. Definir padding/extrusão e UVs.
4. Implementar orçamento de memória.
5. Implementar LRU/clock ou política equivalente com pinning.
6. Instrumentar hit rate, misses, bytes e expulsões.

### Prompt para o agente

```text
Você é engenheiro de renderização e gerenciamento de memória GPU.

Tarefa: revisar o sistema de texturas/atlas do SpriteUI e do editor, separando políticas de amostragem e estabelecendo limites previsíveis de memória.

Inspecione:
- apps/editor/text_atlas_set.hpp;
- FontAtlasCache e implementações relacionadas;
- criação de texturas e samplers no RHI;
- shaders HLSL/GLSL;
- caminhos SDL.

Requisitos:
1. Diferenciar textura de cor, cobertura alpha, SDF e MSDF.
2. Permitir nearest para pixel art e linear para sprites/texto quando apropriado.
3. Definir padding e extrusão para impedir bleeding.
4. Evitar mipmaps que misturem regiões vizinhas sem estratégia de atlas adequada.
5. Medir bytes CPU/GPU reais, páginas residentes, hits, misses e expulsões.
6. Implementar orçamento configurável e política de expulsão segura.
7. Não expulsar recursos ainda referenciados por frames em voo ou por widgets ativos.
8. Usar formatos compactos quando a informação armazenada permitir (por exemplo, coverage em R8).

Testes:
- sprites adjacentes no atlas;
- texto em escalas diferentes;
- carregamento de muitas fontes/tamanhos;
- pressão de memória;
- expulsão e recarga;
- encerramento de painéis/documentos;
- recursos ainda em uso.

Entregáveis:
- política documentada;
- métricas;
- testes;
- estimativa e medição real de memória;
- justificativa dos formatos e samplers.

Faça segunda revisão crítica sobre race conditions, lifetime, atlas bleeding, thrashing do cache e uso de memória acima do orçamento.
```

---

## ETAPA 7 — Extração de renderização sem alocações recorrentes

**Prioridade:** média/alta  
**Risco:** médio  
**Dependências:** Etapas 0, 1 e 2

### Tarefas

1. Mapear todas as alocações na extração.
2. Adicionar `BuildInto` ou equivalente.
3. Reutilizar capacidade dos vetores.
4. Reutilizar scratch buffers.
5. Pular sort se a sequência já estiver ordenada.
6. Evitar cópias de payloads grandes.

### Prompt para o agente

```text
Atue como engenheiro C++ de alto desempenho.

Tarefa: otimizar a extração da DrawList para FramePacket, reduzindo alocações, cópias e ordenações desnecessárias.

Arquivo inicial:
third_party/SpriteUi/src/Rendering/DrawList.cpp

Passos:
1. Trace o caminho de Build/Extract e todos os vetores temporários.
2. Meça alocações e capacidade dos containers por frame.
3. Crie uma API que escreva em FramePacket reutilizável, se compatível com o design.
4. Preserve capacity ao resetar os vetores.
5. Use scratch storage reutilizável.
6. Detecte sequências já ordenadas antes de executar sort.
7. Mantenha estabilidade e ordem visual.
8. Não introduza pools globais ou singletons sem necessidade.

Testes:
- packet vazio;
- packet pequeno e grande;
- sequência ordenada e desordenada;
- mudanças de ordem;
- reutilização por vários frames;
- ausência de dados antigos após reset.

Entregáveis:
- alteração pequena;
- testes;
- contagem de alocações;
- benchmark;
- análise de trade-offs.

Faça segunda revisão crítica sobre retenção excessiva de memória, estado residual e thread safety.
```

---

## ETAPA 8 — Testes visuais, conformidade e matriz de backends

**Prioridade:** alta  
**Risco:** baixo/médio  
**Dependências:** etapas anteriores

### Tarefas

1. Criar cenas golden.
2. Criar ferramenta de captura e comparação.
3. Rodar a mesma cena em cada backend.
4. Definir tolerâncias por tipo de conteúdo.
5. Testar APIs realmente suportadas no build.
6. Documentar recursos parciais.

### Prompt para o agente

```text
Você é responsável por qualidade gráfica e testes de conformidade de uma biblioteca UI multiplataforma.

Tarefa: criar uma suíte automatizada de testes visuais para o SpriteUI e uma matriz de conformidade por backend.

Requisitos:
1. Reutilize os testes GPU e infraestrutura de captura existentes.
2. Crie cenas determinísticas cobrindo:
   - retângulos, bordas, cantos e curvas;
   - gradientes e transparência;
   - clipping;
   - sprites e atlas com múltiplas páginas;
   - texto, acentos, kerning, shaping e escala/DPI;
   - transformações e escalas fracionárias.
3. Capture imagens de referência por backend.
4. Compare imagens com métricas e tolerâncias documentadas.
5. Gere relatório visual de diferenças quando possível.
6. Não comparar backends diferentes com igualdade exata se suas regras de rasterização forem diferentes; definir tolerância justificada.
7. Diferenciar teste estrutural, teste de pixels e teste de performance.
8. Atualizar matriz de capacidades com suporte comprovado por teste.

Entregáveis:
- cenas de teste;
- ferramenta de captura/diff;
- golden images;
- matriz de backend;
- instruções de execução local e CI;
- relatório de cobertura.

Faça segunda revisão crítica para verificar determinismo, dependência de driver, falsos positivos e golden images desatualizadas.
```

---

## ETAPA 9 — Profiling, otimização orientada por dados e estabilização

**Prioridade:** contínua  
**Risco:** baixo  
**Dependências:** todas

### Tarefas

1. Comparar baseline e versão otimizada.
2. Separar CPU, GPU, driver e sincronização.
3. Identificar gargalos reais.
4. Otimizar somente o que aparece no perfil.
5. Rodar testes de regressão e cenários longos.
6. Documentar limites e trade-offs.

### Prompt para o agente

```text
Atue como engenheiro de performance responsável por validar a evolução do SpriteUI.

Tarefa: comparar a implementação atual com o baseline e determinar se as mudanças melhoraram desempenho e qualidade sem regressões.

Procedimento:
1. Execute os mesmos benchmarks do baseline, na mesma configuração e hardware.
2. Registre build type, compilador, driver, GPU, API, resolução, DPI e número de frames.
3. Separe:
   - CPU layout;
   - CPU extraction;
   - CPU cache update;
   - CPU submit;
   - GPU time;
   - draw calls;
   - state changes;
   - allocations;
   - bytes copied;
   - atlas memory.
4. Faça warm-up e múltiplas execuções; reporte mediana e dispersão.
5. Compare imagens golden.
6. Não atribua ganho de GPU a uma redução de CPU ou vice-versa.
7. Identifique regressões e gargalos que mudaram de lugar.
8. Não faça otimizações adicionais sem evidência.

Entregáveis:
- tabela antes/depois;
- gráficos ou CSV de medições;
- screenshots/diffs;
- regressões;
- recomendações priorizadas;
- decisão de pronto ou necessidade de nova etapa.

Faça uma revisão independente dos dados procurando medições enviesadas, testes não equivalentes e conclusões maiores que as evidências.
```

---

## 7. Ordem recomendada de execução

| Ordem | Etapa | Motivo |
|---:|---|---|
| 1 | Etapa 0 — Baseline | Sem medição, otimizações podem atacar o problema errado |
| 2 | Etapa 1 — Contrato RHI | Corrige risco de renderização incorreta com múltiplos recursos |
| 3 | Etapa 2 — RenderListCache | Reduz movimentação e cópias em atualizações locais |
| 4 | Etapa 3 — Layout incremental | Ataca trabalho repetido no CPU |
| 5 | Etapa 4 — Tipografia | Ataca diretamente a qualidade do texto |
| 6 | Etapa 5 — Formas e AA | Ataca facetas e qualidade de bordas |
| 7 | Etapa 6 — Samplers/memória | Consolida recursos e atlas |
| 8 | Etapa 7 — Extração | Reduz overhead recorrente |
| 9 | Etapa 8 — Testes visuais | Impede regressões entre backends |
| 10 | Etapa 9 — Validação | Confirma os ganhos em ambiente real |

**Observação:** as etapas 2 e 3 podem ser desenvolvidas em paralelo após o baseline, desde que não alterem simultaneamente as mesmas estruturas. A etapa de tipografia pode ser dividida em subtarefas independentes, mas o contrato do atlas precisa ser acordado antes de alterar o shader.

---

## 8. Regras de engenharia para os agentes

Todos os agentes devem seguir estas regras:

1. **Inspecionar antes de editar.** Ler headers, implementação, consumidores, testes e documentação.
2. **Não presumir APIs.** Os exemplos deste relatório são referências conceituais.
3. **Mudanças pequenas.** Um objetivo por commit ou conjunto pequeno de commits.
4. **Manter compatibilidade.** Não remover API pública sem justificativa e plano de migração.
5. **Testar o caminho real.** Um teste Null não substitui um teste de pixels.
6. **Não inventar benchmark.** Se não executar, declarar que não executou.
7. **Medir antes/depois.** Informar hardware, build, backend e metodologia.
8. **Evitar abstração prematura.** Implementar a solução mais simples que resolve o problema demonstrado.
9. **Preservar ordem de UI.** Batching não pode alterar transparência ou z-order.
10. **Verificar memória e lifetime.** Especialmente atlas, buffers e recursos GPU.
11. **Segunda revisão obrigatória.** O agente deve criticar a própria implementação procurando falhas.
12. **Relatório por alteração.** Listar arquivos, mudanças, testes, resultados e limitações.

### Formato de resposta exigido do agente

```text
## Resumo
O que foi alterado e por quê.

## Arquivos inspecionados
Lista dos arquivos e dependências relevantes.

## Diagnóstico confirmado
Separar fatos observados de hipóteses.

## Implementação
Descrever mudanças e decisões de arquitetura.

## Testes
Comandos executados e resultados reais.

## Performance
Antes/depois, metodologia e hardware. Se não medido, declarar.

## Riscos e limitações
O que ainda não está resolvido.

## Segunda revisão crítica
Problemas que o próprio agente procurou e o que encontrou.

## Próxima etapa
Somente a próxima ação tecnicamente necessária.
```

---

## 9. O que não fazer

- Não reescrever todo o SpriteUI antes de medir e corrigir os contratos.
- Não migrar tudo para MSDF sem medir qualidade, memória e custo.
- Não usar `fwidth` indiscriminadamente em shaders que não trabalham com distância assinada.
- Não ordenar todos os comandos por textura: UI transparente exige ordem.
- Não assumir que um único atlas atende todos os materiais e tipos de textura.
- Não compactar buffers a cada frame.
- Não invalidar todo o layout para qualquer alteração visual.
- Não manter caches ilimitados sem orçamento e telemetria.
- Não declarar suporte a Vulkan, OpenGL ou D3D12 apenas porque há código de integração; validar cada backend no build e no hardware correspondente.
- Não considerar o backend Null como prova de qualidade visual.
- Não misturar correção funcional, refatoração ampla e otimização de performance em um único patch.

---

## 10. Critérios globais de sucesso

A evolução do SpriteUI estará tecnicamente validada quando:

### Correção
- batches respeitam textura, material, sampler, clip e blend;
- recursos não suportados falham explicitamente;
- ordem de desenho permanece correta;
- layout incremental corresponde ao layout de referência;
- atlas não apresenta bleeding nem uso após expulsão.

### Desempenho
- alterações localizadas não copiam grandes intervalos sem necessidade;
- layout não recalcula nós limpos;
- extração reutiliza memória;
- memória de atlas possui orçamento observável;
- CPU e GPU são medidas separadamente;
- benchmarks mostram ganhos reproduzíveis ou explicam por que não houve ganho.

### Qualidade visual
- cantos não apresentam facetas perceptíveis nas escalas-alvo;
- bordas têm espessura e antialiasing consistentes;
- texto permanece legível em escalas e DPI suportados;
- shaping e fallback funcionam nos idiomas declarados;
- sprites usam sampler apropriado;
- SDL e RHI têm paridade documentada ou diferenças explicitadas.

### Manutenibilidade
- contratos entre camadas estão documentados;
- cada backend declara capacidades;
- testes visuais são reproduzíveis;
- alterações são pequenas e revisáveis;
- limitações conhecidas são explícitas.

---

## 11. Referências do repositório

Os links abaixo apontam para os principais pontos de inspeção e devem ser usados pelos agentes como ponto de partida, não como substituto da leitura dos consumidores e dependências.

- [Repositório TOOLGAMEENGINE](https://github.com/espiritoheroico2/TOOLGAMEENGINE)
- [RenderListCache.hpp](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/include/SpriteUi/Rendering/RenderListCache.hpp)
- [LayoutTree.cpp](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/src/Layout/LayoutTree.cpp)
- [DrawList.cpp](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/src/Rendering/DrawList.cpp)
- [Adapter RHI ToolEngine](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/adapters/toolengine/include/SpriteUi/Adapters/ToolEngineRhi.hpp)
- [Text atlas set do editor](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/apps/editor/text_atlas_set.hpp)
- [Status de implementação do SpriteUI](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/third_party/SpriteUi/docs/IMPLEMENTATION_STATUS.md)
- [Plano SpriteUI](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/docs/planos/06_SPRITEUI.md)
- [Auditoria de desempenho](https://github.com/espiritoheroico2/TOOLGAMEENGINE/blob/main/docs/estudos/AUDITORIA_DESEMPENHO.md)

### Referências técnicas externas recomendadas

- [FreeType — documentação](https://freetype.org/freetype2/docs/)
- [HarfBuzz — documentação](https://harfbuzz.github.io/)
- [Multi-channel signed distance field — referência do projeto msdfgen](https://github.com/Chlumsky/msdfgen)
- [SDL_RenderGeometry — documentação SDL](https://wiki.libsdl.org/SDL3/SDL_RenderGeometry)
- [Diligent Engine — documentação](https://diligentgraphics.com/)

---

## 12. Conclusão

O SpriteUI tem uma fundação aproveitável, mas precisa consolidar seus contratos e reduzir a distância entre os caminhos CPU/SDL e RHI. Os riscos mais importantes são: batching que não representa todos os estados de recursos, atualização de cache com movimentação desnecessária, layout que refaz trabalho, pipeline tipográfico limitado e validação visual insuficiente.

A ordem mais segura é: medir, corrigir a semântica dos recursos, otimizar estruturas incrementais, evoluir tipografia e primitivas, e só então consolidar as otimizações com testes visuais e benchmarks.

Este relatório não afirma que todos os pontos são gargalos dominantes em produção. Ele identifica riscos e oportunidades com base na inspeção estática. A prioridade final deve ser confirmada com as métricas da Etapa 0. O objetivo é que cada agente produza uma alteração pequena, demonstrável e reversível — não uma grande refatoração difícil de revisar.
