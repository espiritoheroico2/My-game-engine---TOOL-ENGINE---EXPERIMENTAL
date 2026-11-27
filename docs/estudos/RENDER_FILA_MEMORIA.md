# Estudo: fila unica de render, memoria e texturas para escalar objetos

Data: 2026-11-27. Escopo: somente leitura do codigo. Nada foi compilado, executado ou alterado.

Convencoes: **CONFIRMADA** = lida no codigo (caminho:linha). **MEDIDA** = resultado de medicao registrada no repositorio (origem citada). **ESTIMATIVA** = conta de ordem de grandeza ou suposicao, sem medicao nesta maquina.

Estudos relacionados: `RENDER_MILHOES.md` (E0 a E7, culling por GPU, draw indireto), `ESCALONAMENTO_RENDER.md`, `UPSCALING_FSR_XESS.md`. Este documento detalha o que acontece no codigo hoje, por objeto e por quadro, e propoe a ordem das mudancas de memoria e textura que `RENDER_MILHOES.md` cita mas nao detalha.

---

## PASSO 1: estado atual

### 1.1 Como o renderer GPU monta lotes hoje

Caminho: `engine/render/include/tool/render/gpu_renderer.hpp`, metodo `end()` (linhas 427 a 452 e 684 a 749).

1. Cada `draw()` ou `drawDynamic()` gera um `Draw` (struct em linha 931) e o empurra em `draws_` ou `tdraws_` (transparente, `addDraw` na linha 1316). `Draw` contem `Mat4 world` (64 B), uma copia inteira de `Material` (ESTIMATIVA: ~130 B pelo layout), `tex`, `tw/th`, `ntex`. Total estimado: **~230 B por objeto por quadro** (ESTIMATIVA).
2. `end()` faz `std::stable_sort` de `draws_` (os proprios `Draw`, nao indices) pela chave do vertex buffer (linha 430). Para 1 milhao de objetos isso move ~230 MB de structs varias vezes por quadro. Isso e um risco de CPU grande, ainda nao medido.
3. Frustum da CPU por objeto, em paralelo (`parallelFor` linhas 442 a 451), e `buildRenderQueues` (linha 452) separam as filas.
4. `planQueuedBatches` (`instance_batch.hpp` linhas 65 a 73) ordena **indices** por (mesh, textura) com `stable_sort` e corta em lotes de no maximo **4096** (linha 688). A chave de lote e `textureKey` (linhas 934 a 937): albedo + normal map + dupla face. Cor, metalico, emissao, neblina e UV **nao** quebram lote, porque ja estao por instancia no `InstanceGPU`.
5. Para cada lote opaco (linhas 720 a 749): um `uploadBatchCb` (16 B, `updateBuffer` na linha 924), um `setTexture`, um `setVertexBuffer`/`setIndexBuffer` e um `drawIndexed` (ou `drawIndexedIndirect` quando o culling GPU esta ativo, linha 743).

**Quantos draws e quantas escritas por quadro.** Para N objetos opacos com M pares (malha, textura) distintos: draws por passe = soma, por grupo, de ceil(n_grupo / 4096). Um `BatchCB` e escrito por lote e por passe. O mesmo planejador roda para o passe de sombras de cada cascata (CSM, ate 4), cada face de luz pontual (ate 6 por luz), o prepasso do SSAO, o passe de emissao e o transparente. Ou seja, um quadro com 1 milhao de cubos de uma mesma textura gera ~245 lotes por passe (CONFIRMADA pela regra; a conta e ESTIMATIVA), mas uma cena com 50 mil objetos espalhados por 5 mil pares (malha, textura) gera ~5 mil lotes por passe. Este segundo caso e o que quebra o desempenho de draw calls.

**Reescrita do buffer de instancias.** Todo quadro, `instCpu_` e preenchido para **todos** os opacos (linhas 692 a 698, com `makeInstanceGPU`) e enviado inteiro com `updateBuffer(instBuf_, ...)` (linha 705). Isso acontece **mesmo quando o culling GPU esta ligado**: o compute (`runGpuCull`, linhas 1280 a 1314) so le esse buffer, nao evita a escrita. O buffer e criado com `dynamic = true` (padrao de `ensureBuffer`, linha 1267). Em `diligent_device.cpp` (linhas 285 a 299), `updateBufferRange` nao aceita buffer dinamico, entao nao existe escrita parcial hoje: a unica forma e reescrever tudo.

Medido no proprio codigo (comentario, `editor_state.hpp` linhas 305 a 307): com 5.000 instancias no Vulkan, largar o frustum da CPU custou +1 a +1,5 ms por quadro, porque os objetos continuaram sendo empacotados e enviados. (MEDIDA, origem: comentario do autor no codigo.)

### 1.2 Quanto custa cada instancia

`InstanceGPU` (`instance_batch.hpp` linhas 12 a 21; `static_assert` de 160 B, linha 21):

| Campo | Bytes | Muda por quadro (objeto parado)? |
|---|---|---|
| `world[16]` (matriz) | 64 | Nao, se o objeto nao se mexe |
| `color[4]`, `metallic`, `roughness`, `receiveShadow`, `pad` | 32 | Nao |
| `texInfo[4]` (tamanho da textura, flag, tiling Y) | 16 | Nao |
| `emissive[4]` (o 4o float = offset U) | 16 | Nao |
| `fogColor[4]` (o 4o float = offset V) | 16 | Nao |
| `fogParams[4]` (o 4o float = normal strength e alpha cutoff empacotados) | 16 | Nao |

Resultado: **160 B por instancia, quase tudo estatico**. Para objetos parados, a informacao que muda por quadro e zero. Nao ha espaco livre no layout: os 4 floats de sobra ja estao ocupados (comentarios de `gpu_renderer.hpp` linhas 1247 a 1248 e 94 a 95). Para acrescentar um indice de camada de textura de array (opcao D), e preciso empacotar em um campo existente ou crescer para 176 B, e atualizar o HLSL.

Bytes enviados por quadro (ESTIMATIVA pela conta 160 B x N, sem medir):

| Opacos | Upload por quadro | A 60 fps | Obs |
|---|---|---|---|
| 5 mil | 0,8 MB | ~48 MB/s | desprezivel |
| 50 mil | 8 MB | ~480 MB/s | |
| 100 mil | 16 MB | ~960 MB/s | |
| 1 milhao | 160 MB | ~9,6 GB/s | acima do que um link PCIe 4.0 x4 entrega na pratica |

A RX 6500 XT usa PCIe 4.0 x4 (especificacao publica do produto, nao medida aqui). Com 1 milhao, o upload sozinho nao cabe em 60 fps. Este e o principal argumento da opcao B.

Memoria GPU por objeto, pior caso (ESTIMATIVA): `instBuf_` 160 B + `compactBuf_` 160 B (saida do culling, `runGpuCull` linhas 1282 e 1309) + 64 B por caster por cascata de sombra (`ShadowInstanceGPU`, reconstruido em `buildShadowQueue`/`appendShadowInstances`, linhas 991 a 994) + 64 B no prepasso do SSAO (linhas 607 a 615). Com 1 milhao e 4 cascatas, a soma passa de 0,6 GB so em buffers de instancia. Os buffers crescem por dobra (`ensureBuffer`, linhas 1270 a 1271): com 600 mil instancias a capacidade vira 1.048.576, desperdicando ~40%.

### 1.3 Como o editor monta a lista de objetos por quadro

Caminho: `apps/editor/editor_state.hpp`.

- `renderView` (linha 963 em diante) chama `scene.world().each<MeshRenderer, Transform>` em varios lugares no mesmo quadro: buffer de oclusao (linhas 1119 a 1143), `hashCasters` para a assinatura de sombra (linhas 929 a 937), e a montagem final (linhas 1260 a 1262 e 1339 a 1341). Cada visita chama `lodMeshOf` e `worldMat`.
- `worldMat` tem cache por quadro (`renderWorldMatCache_`, `editor_hier.hpp` linhas 49 a 53), mas e um `unordered_map` por entidade, portanto ainda e um lookup por objeto por visita.
- O lambda `material` (linhas 1056 a 1070) faz, **por objeto e por quadro**: `materials.find` (lookup por string de 64 B), `textureOf`, `lib.texture(tex)` (lookup por string de 160 B), `normalMapOf`, e `lib.texture` de novo para o normal map. `lib.mesh`/`lib.model` tambem fazem lookup por string (`project.hpp` linha 126). `models_` e `textures_` sao `std::map<std::string, ...>` (`project.hpp` linhas 229 e 233). Ou seja: **varias comparacoes de string por objeto por quadro**, em `std::map`.
- `pieces` (linhas 1073 a 1096): um pedaco por parte de modelo, ou o objeto inteiro. `pickMesh` (linha 817) pode trocar a malha por LOD simplificado.
- Culling da CPU: `skipCulled` (linha 1167) testa a esfera de cada objeto contra o frustum (ou nao, se o culling GPU esta valendo, linhas 1101 a 1105). Oclusao por software (linhas 1107 a 1144): todo objeto visivel e rasterizado no buffer de oclusao, com corte em 8 px (`kOccluderMinPx`, linha 1115).
- Sombras: `shadowSignature` (linha 910) e `pointShadowSignature` (linha 948) percorrem todos os casters a cada quadro (`hashCasters`, linhas 929 a 944) para decidir se o mapa de sombra pode ser reaproveitado. Isso e O(N) por quadro mesmo quando as sombras nao mudam (provavel: nao li o ponto exato em que a assinatura e consultada; confirmar).
- Sombras pontuais: em `gpu_renderer.hpp` linhas 480 a 485, `buildShadowQueue` roda **para toda face de cada luz pontual, em todo quadro**, antes de comparar a assinatura com o cache. O cache so poupa o desenho (linhas 504 a 506), nao a classificacao de casters. Com 4 luzes x 6 faces e 100 mil casters, sao 2,4 milhoes de testes de frustum por quadro. (CONFIRMADA pelo codigo; custo ESTIMATIVA.)

**Medicao existente (MEDIDA, origem: `docs/estudos/RENDER_MILHOES.md`, RX 6500 XT, Vulkan, 1280x720, 120 quadros, esferas com fisica):**

| Objetos | Quadro | Obs |
|---|---|---|
| 10 mil | 18 ms | |
| 100 mil | 146 ms | **78 ms sao oclusao por software na CPU** |
| 1 milhao | estoura o heap dinamico do Vulkan | |

Gargalo por objeto, em ordem de peso medido ou provavel: (1) oclusao por software na CPU (MEDIDA: 78 de 146 ms a 100 mil); (2) reescrita total do buffer de instancias e o heap dinamico (MEDIDA: estouro a 1 milhao; a causa e a escrita total, ver 1.1); (3) trabalho de CPU por objeto por quadro (lookups de string, copia de `Draw`, `stable_sort` de `Draw`, classificacao de sombras; ESTIMATIVA, ainda sem medicao separada).

### 1.4 Texturas

Caminho: `engine/render/include/tool/render/texture.hpp`, `apps/editor/project.hpp`, `apps/editor/image_io.cpp`, `gpu_renderer.hpp`, `engine/rhi/include/tool/rhi/rhi.hpp`, `engine/rhi_diligent/src/diligent_device.cpp`.

- **Carga**: `Project::texture` (`project.hpp` 204 a 214) chama `loadImageFile` (PNG, JPG, BMP, TGA; `image_io.cpp`). O resultado e uma `tool::Texture` RGBA8 na CPU (`texture.hpp` linhas 17 a 20). Cada textura carregada tem id unico (`image_io.cpp` linha 36, `gNextId++`).
- **CPU mantem a copia**: `textures_` guarda o `Texture` com `rgba` (`project.hpp` linha 212). O `SoftRenderer` tambem usa esses texels (`texel()`, `texture.hpp` 26 a 33). Enquanto a API for Software, a copia da CPU e necessaria. Com GPU, ela so serve de origem do upload e **dobra** o uso de memoria da textura.
- **Upload**: `GpuRenderer::textureFor` (`gpu_renderer.hpp` 416 a 425) cria `RGBA8` **com `mips = 1`** (linha 421) a partir de `t->rgba.data()`. `createTexture` (`diligent_device.cpp` 126 a 153) so copia o nivel 0 e so aceita `Format::RGBA8` (linha 145).
- **Sem mipmap**: `TextureDesc` ja tem o campo `mips` (`rhi.hpp` linha 70), mas o renderer nunca passa mais de 1 nivel, e nao ha geracao de mips.
- **Sem sampler**: `rhi.hpp` linha 87 documenta que `textureNames` nao expoe sampler: a amostragem e `Load`/`texelFetch`, ou seja, **vizinho mais proximo, sem filtragem e sem mip**. Isso causa serrilhado em distancia (ESTIMATIVA de efeito visual, nao vi na tela).
- **Sem compressao**: `enum class Format` (`rhi.hpp` linha 53) tem `RGBA8`, `RGBA8_SRGB`, `BGRA8`, `RGBA16F`, `R32F`, `D32F`, `D24S8`. Nenhum BC, nenhum ASTC, nenhum ETC2.
- **Sem array nem atlas**: `TextureDesc` so descreve 2D (`rhi.hpp` linha 70). Cada textura diferente exige um `setTexture` diferente, e portanto um lote diferente.
- **Sem descarte**: `texs_` (`gpu_renderer.hpp` linha 1212) so e limpo em `release()` (linha 1153). `invalidate` (`project.hpp` linhas 195 a 199) apaga a textura da CPU, mas **nao** a textura da GPU, e a proxima carga ganha um id novo (`image_io.cpp` linha 36). **Resultado: cada recarga de textura editada deixa uma textura GPU orfã ate o fim da execucao.** (CONFIRMADA pela leitura; o tamanho do vazamento e ESTIMATIVA.)
- **Tamanho de referencia**: RGBA8 sem mip ocupa 4 B por texel: 1024^2 = 4 MB, 2048^2 = 16 MB, com a copia da CPU, 2x isso.

### 1.5 Memoria por objeto no ECS

Caminho: `apps/editor/components.hpp` linhas 49 a 73; `engine/ecs/include/tool/ecs/world.hpp` linhas 237 a 241.

`MeshRenderer` tem 4 strings de caminho em disco, todas dentro do proprio componente:

| Campo | Bytes | Linha |
|---|---|---|
| `model[160]` | 160 | 53 |
| `texture[160]` | 160 | 54 |
| `material[64]` | 64 | 55 |
| `normalMap[160]` | 160 | 67 |
| **Subtotal de strings** | **544** | |

Somando os demais campos (cores, floats, bools, `node`, fog, UV, cutoff): o `MeshRenderer` fica em ~650 B (ESTIMATIVA pela soma do layout; `ecs_test.hpp` linha 154 comenta "~0,7 KB por entidade" com dois caminhos de 160 B, o que bate). Strings representam ~84% do componente.

O armazenamento do ECS e um **sparse set**: `Storage<T>` tem `sparse`, `denseIdx` e `data` (`world.hpp` 237 a 241). Entao cada `MeshRenderer` ocupa ~650 B **no vetor denso**, sem reaproveitamento de strings. Para 1 milhao de objetos: ~650 MB so de `MeshRenderer` (ESTIMATIVA), mais `Transform` (36 B) e o resto.

**Alternativa**: trocar cada caminho por um id `u32` (4 B) apontando para uma tabela de assets do projeto (nome/caminho guardado uma vez). Com 4 ids (model, texture, material, normalMap), o componente cai para ~124 B (ESTIMATIVA: 650 - 544 + 16). Economia de ~5x. A string continua existindo no arquivo de cena e na UI; so o componente em memoria muda. Os acessos atuais (`lib.texture(mr.texture)`, `materials.find(matRoot, mr.material)`) viram lookup por id, o que tambem remove a comparacao de string por objeto por quadro (1.3).

### 1.6 Malhas (para completar)

- `GpuMesh` (`gpu_renderer.hpp` linha 930) guarda vertex/index buffers, contagem, bounds e `lastUse`. Chave: ponteiro da `Mesh` do editor (`meshes_`, linha 1335).
- Descarte de malha: a cada 64 quadros, malha sem uso ha mais de 240 quadros e liberada (linhas 397 a 398). Isso e bom para malha, e ausente para textura (1.4).
- Vertice empacotado: 8 floats = 32 B (`packVertices`, linhas 1017 a 1021): posicao, normal e UV, todos `f32`. Indice `u32`. Compressao possivel (normal em 2x16 bits, UV em half), mas e um ganho menor que textura: a malha pesa pouco se comparada a instancia quando os objetos repetem a mesma malha.
- `DrawIndexedIndirectArgs` (`rhi.hpp` linha 62) **ja tem** `firstIndex`, `baseVertex` e `firstInstance`. Isso permite, no futuro, um buffer unico de vertices com varias malhas (opcao A) sem mudar o formato do comando indireto.

### 1.7 Culling GPU (fase 1) e FSR, o que ja existe

- `frustum_cull.hlsl`: `SrcInstances` (SRV) e `DstInstances` (UAV) por lote (linhas 35 a 36), esfera por lote (`CullBatchGPU`, 32 B), e `DrawArgs` indireto. O compute roda sobre o buffer de **todos** os opacos a cada quadro (`runGpuCull`, dispatch em linha 1311).
- O VS le `Instances[batchInfo.x + iid]` (`pbr.hlsl` linha 42; `prepass.hlsl` linha 13): o indice por lote e um deslocamento na faixa contigua. Isso e o que impede mover instancias no buffer sem recalcular o lote.
- FSR no repositorio e **FSR 1** (`fsr1.hpp`: EASU + RCAS). Como citado em `UPSCALING_FSR_XESS.md`, FSR 1 nao precisa de motion vectors. Motion vectors so entram se um FSR 2 ou superior for adotado.

---

## PASSO 2: opcoes

Para cada opcao: o que e, vantagem, risco, e o que ja existe.

### Opcao A. Fila unica (megabuffer + draw indireto por malha/material)

**O que e**: um unico vertex buffer e um unico index buffer para todas as malhas (megabuffer), com `firstIndex`/`baseVertex` por malha no comando indireto. Texturas por array (opcao D) tiram a textura da chave de lote. Resultado: os draws ficam indexados por (malha, classe de material), nao por lote de 4096.

- **Vantagens**: menos troca de vertex/index buffer; menos draws quando ha muitas malhas pequenas; a fila de opacos vira um vetor so, que o culling compacta.
- **Riscos**: fragmentacao do megabuffer (ao liberar malha, precisa de alocador de faixas); malhas que mudam de tamanho exigem realocacao; o LOD da esfera (`sphereFar`, `lodMeshOf`) vira outra faixa; o culling precisa de um contador de instancias por malha (ja existe por lote, `cullArgs`).
- **Ja existe**: planejador de lotes (`planInstanceBatches`), `DrawIndexedIndirectArgs` com os campos certos, compute de culling que compacta por lote (`runGpuCull`).
- **Ganho real**: depende de quantas (malha, textura) distintas existem. Hoje, com muitas texturas, o gargalo e a textura, nao a malha. Por isso A so faz sentido depois de D.
- **Classificacao**: ESTIMATIVA para o ganho; CONFIRMADA para a estrutura existente.

### Opcao B. Dados por instancia so quando mudam (persistente com faixas sujas)

**O que e**: guardar `InstanceGPU` (ou so o `world` e o material) num buffer **nao dinamico**, com slot estavel por entidade, e enviar apenas as faixas sujas com `updateBufferRange` (que so funciona em buffer nao dinamico, `diligent_device.cpp` 285 a 299). Um slot por objeto, com free list para reaproveitar.

**Ponto critico que o estudo precisa resolver**: o VS usa `Instances[batchInfo.x + iid]`, ou seja, supoe que cada lote ocupa uma faixa contigua do buffer ordenada por (malha, textura). Se o buffer persistente tem slot estavel, um objeto que troca de textura muda de lote e nao esta mais na faixa. Duas saidas:

- **B1 (simples, mas mantem a escrita por quadro)**: manter a ordenacao atual, mas so reescrever o buffer quando o conjunto de lotes muda. Pouco ganho se a cena muda de forma constante.
- **B2 (recomendada)**: buffer persistente com slot estavel + **um indice `u32` por instancia** (4 B) agrupado por lote, que o compute de culling ja produz para a saida compacta. O VS le `Instances[indice[iid]]`. Por quadro, o CPU escreve so o indice (4 B por instancia, ou nada se o compute gerar a lista). Com 1 milhao: 4 MB por quadro, contra 160 MB hoje (ESTIMATIVA).

- **Vantagens**: nenhum upload de instancia parada; o culling GPU vira a unica fonte de compactacao; prepasso e sombra podem reusar o mesmo buffer persistente (hoje cada um tem sua copia: `shadowInstBuf_`, `prepassInstBuf_`, `emiInstBuf_`, `transInstBuf_`, `instBuf_`, `compactBuf_`).
- **Riscos**: sincronia de "sujo" (quem marca: editor ao mover, fisica a cada quadro para corpos que se movem, animacao); reciclagem de slot quando um objeto e removido; o mapeamento entidade -> slot precisa sobreviver a `Stop` e snapshot (o editor desfaz tudo ao parar, `editor_state.hpp` 1448 a 1466).
- **Ja existe**: `updateBufferRange` no RHI (`rhi.hpp` 190 a 193); o compute de culling com saida compacta; `ensureBuffer` com capacidade.
- **Classificacao**: CONFIRMADA a restricao de `updateBufferRange` e a dependencia de faixa contigua; ESTIMATIVA para o ganho de bytes.

### Opcao C. Id de asset em vez de caminho de 160 B no componente

Ja detalhada em 1.5. Vantagens: componente ~5x menor (ESTIMATIVA), sem comparacao de string por objeto por quadro (CONFIRMADA: os lookups existem), tabela de assets compartilhada. Riscos: migracao da cena (o arquivo deve continuar como texto; so a memoria muda); a UI do inspector e o `assets_panel.hpp` (linha 46) precisam trocar string por id; o `material` tem 64 B, precisa do mesmo tratamento. Ganho de CPU: provavelmente o menor dos quatro, mas e o mais barato de fazer.

### Opcao D. Texturas: array, mips, compressao, streaming

- **Array de texturas (`Texture2DArray`)**: uma textura GPU com N camadas do mesmo tamanho (mesmo de atlas por camada), indice por instancia. Tira a textura da chave de lote. Requisitos: o RHI ganha `arraySize` em `TextureDesc` e um `setTexture` que aceita array. Limite de camadas por array depende do driver (tipicamente 2048; ESTIMATIVA, verificar). O indice cabe no `texInfo[2]` (flag 0/1 vira "camada+1", 0 = sem textura), sem crescer a instancia.
  - Vantagem: remove o maior divisor de lote (textura) sem atlas e sem mexer em UV. Tiling continua igual (`uvTiling`).
  - Risco: texturas de tamanhos diferentes precisam caber no mesmo array (redimensionar, ou um array por tamanho: 256, 512, 1024, 2048, o que ainda reduz a quebra de lote). Cada array aloca o pior caso de tamanho.
- **Atlas**: mais compacto, mas o UV muda, e tiling acima de 1 precisa de `fract` dentro do retangulo do atlas. Mais complexo que array, com o mesmo efeito em lote. Nao recomendado como primeiro passo.
- **Bindless**: exige indexacao de descritores; o RHI expoe apenas slots (`setTexture(slot, t)`). Muito mais trabalho no RHI e no GL. Nao recomendado agora.
- **Mipmaps**: `mips` ja existe no `TextureDesc`. Gerar mips na CPU na carga (+33% de memoria, ESTIMATIVA) ou no GPU. Precisa de amostragem com sampler (filtro trilinear/anisotropico) em vez de `Load`. Isso e uma mudanca de RHI e de shader, nao so de upload: a interface atual nao expoe sampler (`rhi.hpp` 87). Efeito visual: sem mip o serrilhado a distancia e real (ESTIMATIVA visual, a medir com imagem).
- **Compressao BC**: o `Format` do RHI nao tem BC. Diligent suporta BC (DXT) no backend Vulkan e D3D12, mas nao no GLES; BC nao e opcao no GLES (ASTC ou ETC2 seriam). Proposta: BC7 (sRGB) para albedo e BC5 para normal map (dois canais, Z reconstruido no shader), BC4 para mascaras. Tamanho: 1 B/texel para BC7 e BC5 (1/4 do RGBA8), 0,5 B/texel para BC1 (ESTIMATIVA pela tabela padrao). Requer conversao offline (ferramenta externa ou cache em disco) e carga de DDS/KTX no editor (o loader atual e PNG/JPG/BMP/TGA).
- **Streaming**: nao existe. O caminho mais simples e: `texs_` e `textures_` com orcamento de VRAM e LRU (ver E). Streaming de mips parciais e mais complexo e so vale com cenas de muitas texturas grandes.
- **Liberar a copia da CPU**: quando a API ativa e GPU, `Texture::rgba` pode ser descartado depois do upload (ganho de 50% da memoria de textura, CONFIRMADA a duplicacao). Precisa de cuidado: o `SoftRenderer` le os texels e a troca de API (`apiChoice`) pode voltar a Software. Manter a copia so no modo Software, ou recarregar do disco ao trocar.

### Opcao E. Memoria: pools, liberacao e limite por VRAM

- **Orcamento de VRAM**: somar bytes de texturas, malhas, instancias e buffers de sombra, e recusar/evictar acima de um limite (por exemplo ~2,5 GB em uma placa de 4 GB, deixando folga para o SO e o compositor; ESTIMATIVA). Hoje nao existe contador de bytes de GPU.
- **Eviction**: `meshes_` ja tem `lastUse` e descarta apos 240 quadros (linhas 397 a 398). `texs_` nao tem `lastUse` e nao descarta (CONFIRMADA). Acrescentar `lastUse` a `texs_` e descartar por LRU fecha o vazamento de recarga (1.4).
- **Pool de buffers**: `ensureBuffer` dobra a capacidade (linhas 1270 a 1271). Para 600 mil instancias, a alocacao vai a 1.048.576 (+75%). Crescimento de 1,5x ou blocos de 64 mil a 256 mil instancias (buffer por bloco, lotes que cruzam bloco ficam separados) reduz o desperdicio e o custo de realocacao (ESTIMATIVA).
- **Heap dinamico**: `RENDER_MILHOES.md` ja registra que subir o heap (`TOOL_DYNAMIC_HEAP_MB`, 8 -> 64 MB) adia o estouro e nao resolve. Com a opcao B, o buffer deixa de ser dinamico e o problema some.
- **Liberacao de malha e textura**: `invalidate` ja chama `forget` para malha (`project.hpp` 195 a 196 e `gpu_renderer.hpp` 916 a 920). Textura nao tem o equivalente (1.4).

### Opcao F. Relacao com o culling GPU (fase 1), o FSR e os motion vectors

- **Culling GPU**: hoje roda sobre `instBuf_` inteiro a cada quadro (O(N) na GPU, barato: teste de esfera) e compacta. Com B2, o compute le o slot persistente e gera a lista de indices, sem o upload de 160 B por objeto. O custo que sobra e O(N) na GPU por quadro (ESTIMATIVA: ~1 a 2 ms a 1 milhao, a medir).
- **Dropar o frustum da CPU** (`gpuCullKeepCpuFrustum`, `editor_state.hpp` 308): so faz sentido depois de B2, senao a CPU continua empacotando os objetos fora da tela. A medicao de 1 a 1,5 ms a 5 mil (MEDIDA, comentario) mostra o custo de manter a CPU no meio.
- **Oclusao por software**: a fase de HiZ na GPU (E7 de `RENDER_MILHOES.md`) e o que realmente substitui os 78 ms medidos. Antes disso, o corte de 8 px e o orcamento da oclusao precisam ser medidos separados (E3).
- **FSR e motion vectors**: FSR 1 nao precisa. Se um FSR 2 ou superior vier, a versao por instancia precisa de `prevWorld`. Com B2 isso fica natural: o buffer persistente guarda a matriz anterior, e so os objetos que se mexem (lista suja) precisam de velocidade por pixel. Objetos parados so usam o movimento da camera. Nao criar isso antes do FSR 2.

---

## PASSO 3: plano progressivo

Regra comum (vinda de `RENDER_MILHOES.md` E0 e da regra 7 do roadmap): medir antes e depois na mesma maquina e na mesma cena, separar CPU e GPU, manter o caminho CPU funcionando, fechar cada etapa so com o criterio cumprido. Se falhar, registrar como experimento rejeitado.

### Etapa 0: medicao (sem mudar render)

Ferramentas que ja existem: `--instances N` (`ecs_test.hpp` 150 a 180; sem fisica), `--no-occlusion`, `--no-gpu-cull`, `--gpu-cull-drop-cpu-frustum`, `Profiler`/`ZoneTimer` (ja usados em `editor_state.hpp` 1109), `renderStats()`, `batchCbUploads()`.

Acrescentar contadores (pequena mudanca, sem alterar o caminho de desenho):
- bytes enviados por quadro por buffer (`instBuf_`, `shadowInstBuf_`, `prepassInstBuf_`, `emiInstBuf_`, `transInstBuf_`, `batchCb_`);
- numero de lotes por passe, e de pares (malha, textura) distintos;
- tempo de CPU por fase: montagem de `Draw`, `stable_sort` de `draws_` (linha 430), `planQueuedBatches`, oclusao, shadow queues, empacotamento `instCpu_`;
- tamanho de `sizeof(MeshRenderer)` e `sizeof(Draw)` (imprimir em tempo de execucao; nao compilei aqui).

**Criterio de aceite**: tabela com 5 mil, 50 mil, 100 mil e 1 milhao, repetida 3 vezes com variacao menor que 5%, nos cenarios (a) objetos estaticos, (b) 10% em movimento, (c) camera parada e andando.

### Etapa 1: ganhos baratos de CPU (sem mudar formato)

1. Ordenar indices, nao `Draw`, em `end()` (linha 430): `draws_` e reordenado por ponteiro/indice. Aceite: tempo de `end()` menor a 100 mil e 1 milhao; imagem igual.
2. Cachear a resolucao de assets por objeto: mapa `mr -> ponteiros` invalidado por um contador de edicao do componente, em vez de `lib.texture(string)` e `materials.find(string)` a cada quadro (`editor_state.hpp` 1056 a 1070). Aceite: `renderView` com menos tempo a 100 mil; imagem igual.
3. Corrigir o vazamento de textura GPU em recarga (`texs_` sem descarte; 1.4). Aceite: contador de texturas GPU estavel apos recarregar a mesma textura 10 vezes.
4. Sombras pontuais: nao reconstruir a fila de cada face em todo quadro quando a luz e os casters nao mudaram. **Cuidado**: a assinatura hoje e calculada a partir da fila (`pointShadowFaceSignature`, `gpu_renderer.hpp` 485). A troca precisa calcular a assinatura sem montar a fila, ou guardar um contador de versao dos casters. Nao pular a classificacao de casters (ver secao "nao fazer"). Aceite: tempo de sombra pontual menor com 100 mil; sombras iguais em cena de teste.

### Etapa 2: buffer de instancias persistente (opcao B2)

1. Criar `InstanceGPU` em buffer nao dinamico, com slot estavel por entidade, free list e contador de versao por entidade (incrementado na edicao e no movimento da fisica).
2. Enviar faixas sujas com `updateBufferRange`; o VS le por indice `u32` agrupado por lote.
3. Migrar o culling para ler o slot persistente e gerar a lista de indices.

**Criterio de aceite**:
- 100 mil estaticos: bytes de instancia por quadro perto de zero apos o primeiro quadro (medido no contador da etapa 0);
- 1 milhao: sem estouro de heap dinamico, ou erro claro (nao crash);
- imagem igual ao caminho atual em cena de teste (comparacao de imagem, tolerancia definida);
- caminho CPU (sem GPU) continua funcionando.

### Etapa 3: id de asset no componente (opcao C)

Trocar os caminhos de `MeshRenderer` por ids `u32` com tabela de assets; manter a string no arquivo de cena e na UI.

**Criterio de aceite**: `sizeof(MeshRenderer)` medido (alvo ~130 B, ESTIMATIVA); cena salva e carregada de volta sem diferenca (teste de ida e volta); nenhuma regressao em `assets_panel.hpp` e no inspector.

### Etapa 4: texturas (opcao D), em ordem

1. Amostragem com sampler e mips no RHI (`Format` e `TextureDesc.mips` ja existem; falta sampler). Aceite: imagem de referencia em distancia com tolerancia; `gpu_test vulkan` continua passando (o mesmo teste citado em `RAYTRACING.md`).
2. Liberar a copia da CPU em API GPU (com recarga ao trocar para Software). Aceite: memoria de textura cai ~50% medida.
3. Array de texturas por tamanho, com indice em `texInfo[2]`. Aceite: draws por quadro com 50 mil objetos e 1000 texturas diferentes cai de ~mil lotes para o numero de malhas x classes (medido).
4. Compressao BC7/BC5 offline, separada por backend (GLES usa ETC2 ou ASTC). Aceite: memoria de textura cai ~4x; imagem dentro de tolerancia para normal map.
5. LRU e orcamento de VRAM (opcao E). Aceite: cena que excede o orcamento nao cresce sem limite e a imagem continua correta (ao custo de recarga).

### Etapa 5: fila unica (opcao A), so depois da etapa 4.3

Megabuffer de malhas com `firstIndex`/`baseVertex` no comando indireto (campos ja existem em `rhi.hpp` 62). Aceite: numero de comandos por quadro igual ao de malhas x classes, nao ao de lotes; imagem igual.

### Etapa 6: culling e oclusao (opcao F e E3/E7 de `RENDER_MILHOES.md`)

Dropar o frustum da CPU so apos a etapa 2. Oclusao por HiZ na GPU substitui a oclusao por software. Aceite: os 78 ms (MEDIDA a 100 mil) caem para uma fracao definida na etapa 0; sem objeto visivel sumindo (teste de imagem).

### Escala, para cada etapa

| Escala | O que medir | Meta |
|---|---|---|
| 5 mil | quadro, CPU por fase, bytes/quadro | Meta definida na etapa 0 (ainda nao medida; nao chutar) |
| 50 mil | mesmo + lotes/pares distintos | idem |
| 100 mil | mesmo + oclusao separada | idem; e o ponto onde a etapa 1 deve ja ter reduzido a oclusao |
| 1 milhao | mesmo + heap, VRAM, bytes/quadro | sem estouro de heap; bytes/quadro perto de zero em estatico (etapa 2) |

As metas numericas nao foram definidas. `RENDER_MILHOES.md` tambem deixa isso para depois da E0. Nao escrever meta sem a tabela da etapa 0.

### Metricas em todas as etapas

- Quadro total e CPU por fase (ms), p50 e p95, 120 quadros, mesma cena, mesma maquina (RX 6500 XT, Vulkan).
- Bytes enviados por quadro por buffer.
- Lotes e draws por passe; pares (malha, textura) distintos.
- Memoria: VRAM (contador de bytes GPU), RAM (CPU com copias de textura), e `sizeof(MeshRenderer)`/`sizeof(Draw)`.
- Qualidade: comparacao de imagem com tolerancia, nas cenas de teste.

---

## O que NAO fazer

- **Nao esconder objetos sem culling.** Desligar o frustum ou a oclusao para "ganhar" tempo muda o que o usuario ve e mascara o gargalo. Culling e a medida, nao a falha.
- **Nao pular a classificacao.** Nao reusar a classificacao de casters/sombras sem uma assinatura que prove que nada mudou (ver etapa 1, item 4). Pular a classificacao com flag errada faz sombra e objeto sumirem.
- **Nao mudar o layout de `InstanceGPU` sem atualizar tudo junto**: os `static_assert` (`instance_batch.hpp` 21; `gpu_renderer.hpp` 73 e 74), o `ObjectCB`, e o HLSL (`pbr.hlsl`, `prepass.hlsl`, `shadow.hlsl`, `frustum_cull.hlsl`).
- **Nao usar `updateBufferRange` em buffer dinamico.** O backend recusa (TOOL-E134, `diligent_device.cpp` 296). Buffer persistente precisa de uso padrao.
- **Nao subir o heap dinamico como solucao.** Isso e o que `RENDER_MILHOES.md` ja desaconselha; ele so adia o estouro.
- **Nao trocar `Nearest` por sampler sem imagem de referencia.** Muda o visual de toda a cena.
- **Nao usar BC em GLES.** Ele nao e suportado; usar ETC2/ASTC la.
- **Nao fazer megabuffer antes de remover o divisor de textura.** Enquanto cada textura for um lote, o megabuffer nao reduz draws.
- **Nao remover o caminho CPU** (regra 7 do roadmap). O software renderer e o caminho de referencia.
- **Nao criar motion vectors por instancia antes do FSR 2.** FSR 1 nao precisa.
- **Nao escrever meta de quadro sem a medicao da etapa 0.**

---

## Lacunas deste estudo

- Nenhuma medicao nova: tudo que e MEDIDA vem de `RENDER_MILHOES.md` ou de comentarios do codigo, com origem informada.
- `sizeof(MeshRenderer)`, `sizeof(Draw)` e os totais de memoria sao ESTIMATIVA pelo layout. Medir com um teste ou `static_assert` em tempo de compilacao na etapa 0.
- Nao li a chamada exata de `shadowSignature` em cada quadro; a afirmacao de O(N) por quadro precisa ser confirmada.
- Nao verifiquei o comportamento do `SV_InstanceID` com `firstInstance` no D3D12 (diferente do Vulkan). Por isso o estudo prefere o indice por lote (B2) a usar `firstInstance` para remover `BatchCB`.
- Limite de camadas de array e suporte BC no backend Diligent (Vulkan e D3D12) sao ESTIMATIVA; verificar no driver da RX 6500 XT.
- O comportamento do Diligent com `UpdateBuffer` em buffer nao dinamico (staging interno) nao foi lido.
