# P1 - Instanciamento no renderizador (passo a passo) e planos P2 a P6

Data: 2026-11-18. Base: `ui-integrado` em `b25398d`.
Referencia: `docs/PESQUISA_MILHARES_OBJETOS.md` (fontes e motivo de cada tecnica).

## 0. Estado atual

| Item | Estado |
|---|---|
| Agrupamento por malha e textura (`engine/render/include/tool/render/instance_batch.hpp`) | Implementado e coberto por testes |
| Layout `InstanceGPU` (160 bytes, igual ao `ObjectCB`) | Implementado, com `static_assert` |
| Buffer de instancias no RHI Diligent (VS e PS) | Implementado |
| Shaders lendo instancias (`pbr`, `prepass`, `shadow`, `shadow_point`) | Implementado |
| Renderizador usando lotes | Implementado no passe opaco, prepass e sombras |

### Estado validado e pendencias

- Builds CPU e GPU e testes automatizados anteriores passaram; `tool_tests` registrou 197 testes sem falhas e o teste Vulkan passou.
- Os builds atualizados do Editor (CPU e GPU), após a adicao da exportacao do Profiler, tambem passaram; `Editor.ProfilerModel` e `Editor.ProfilerExport` passaram nos dois.
- Os testes OpenGL configurados pelo CTest nao executaram: `xvfb-run` nao estava disponivel no ambiente.
- A verificacao visual obrigatoria ainda falta: comparar a cena `--ecs 1000 --play`, confirmar equivalencia visual e medir `DrawStats`/CSV do benchmark.
- O bloom e os objetos transparentes continuam sendo desenhados por objeto intencionalmente; isso nao bloqueia o P1 implementado.

Regra de validacao: nao considerar o P1 visualmente concluido antes da comparacao de imagem e do benchmark listados na secao 7.

## 1. Restricoes que ja conhecemos (leia antes de mexer)

1. **Limite de UBO = 8 no Vulkan.** O pbr.hlsl (`pbr.hlsl` linhas 1-6) conta VS + PS somados.
   Hoje VS usa b4 (FrameBuffer) e b6 (ObjectBuffer); PS usa b0,b1,b2,b3,b5,b7. Total 8.
   **Trocar o b6 (ObjectBuffer) por um buffer de instancias (StructuredBuffer) NAO conta como UBO**,
   e o b6 novo (BatchCB, so com `first`) conta. Saldo: o mesmo numero de UBOs.
2. **Storage hoje so vai para o pixel shader.** Em `engine/rhi_diligent/src/diligent_device.cpp`,
   `setStorage` (linha ~457) chama `GetVariableByName(dl::SHADER_TYPE_PIXEL, ...)`.
   Precisa ligar tambem no vertex shader.
3. **`drawIndexed(count, n)` nao tem "base instance".** O shader sempre recebe `SV_InstanceID` a partir de 0.
   Por isso cada lote grava `first` num pequeno UBO (BatchCB) e o shader le `Instances[first + SV_InstanceID]`.
4. **Nao existe desenho indireto no RHI.** P1 nao precisa disso. P3 precisa.

## 2. Fase 1a - Buffer de instancias no RHI (Diligent)

Arquivo: `engine/rhi_diligent/src/diligent_device.cpp` e `engine/rhi/include/tool/rhi/rhi.hpp`.

1. Em `setStorage` (linha ~457), trocar a busca pelo pixel shader por uma busca nos dois estagios:
   ```cpp
   // antes: GetVariableByName(dl::SHADER_TYPE_PIXEL, name)
   // depois: ligar no vertex e no pixel, quando a variavel existir em cada um
   for (dl::SHADER_TYPE st : {dl::SHADER_TYPE_VERTEX, dl::SHADER_TYPE_PIXEL}) {
     if (auto* var = pipe_->srb->GetVariableByName(st, pipe_->storageNames[slot].c_str()))
       var->Set(buf->GetDefaultView(dl::BUFFER_VIEW_SHADER_RESOURCE));
   }
   ```
   **Verificar:** a versao do Diligent vendorizada aceita `GetVariableByName` por estagio para o mesmo nome.
   Se o nome nao existir no estagio, o `if` evita erro.
2. Atualizar o comentario de `storageNames` em `rhi.hpp` (linha 69): "SRV somente leitura em VS e PS".
3. Garantir que o `setStorage` aceite buffers criados como `StructuredBuffer` (copie o padrao usado em
   `imp_.points`, `imp_.indices` no import da Light Prism; procure `createBuffer` com `Storage` ou
   `Structured` no `gpu_renderer.hpp`).

**Verificacao da fase 1a:** o build passa; `tool_tests` passa; a engine abre e o jogo renderiza exatamente
como antes (nada usa o novo caminho ainda).

## 3. Fase 1b - Shader PBR le instancias (so passe principal)

Arquivo: `assets/shaders/pbr.hlsl`.

1. Substituir o `cbuffer ObjectBuffer : register(b6)` por:
   ```hlsl
   // Dados por instancia (mesmo layout de InstanceGPU, 160 bytes). Lido por SV_InstanceID.
   struct InstanceData {
     float4x4 world;
     float4 baseColor;
     float metallic, roughness, receiveShadow, _pad1;
     float4 texInfo;
     float4 emissive;
     float4 fogColor;
     float4 fogParams;
   };
   StructuredBuffer<InstanceData> Instances : register(t15); // t15 livre (t14 = AoMap)
   cbuffer BatchBuffer : register(b6) { uint batchFirst; uint3 _padB; };
   ```
2. `VSIn` ganha `uint iid : SV_InstanceID;`.
3. No `VSMain`, no inicio: `InstanceData d = Instances[batchFirst + i.iid];` e trocar cada uso de
   `world`, `baseColor`, `metallic`, `roughness`, `receiveShadow`, `texInfo`, `emissive`, `fogColor`, `fogParams`
   por campos de `d`.
4. `PSMain` nao muda (recebe os valores via `nointerpolation`, como hoje).
5. Em `engine/render/include/tool/render/gpu_renderer.hpp`, `pbrUniformNames()` (linha ~70) passa a ter
   `BatchBuffer` no lugar de `ObjectBuffer`, e `pbrStorageNames()` ganha `"Instances"` no slot 4
   (slots 0 a 3 ja sao `imp_.points`, `imp_.spots`, `imp_.cells` e `imp_.indices`).
   **Mudou o HLSL? Mude aqui tambem** (o comentario do arquivo ja avisa isso).

## 4. Fase 1c - Renderizador desenha por lote (so passe principal opaco)

Arquivo: `engine/render/include/tool/render/gpu_renderer.hpp`.

1. Incluir `#include "tool/render/instance_batch.hpp"`.
2. Criar membros:
   ```cpp
   rhi::BufferId instBuf_; u32 instCap_ = 0;   // capacidade em instancias (cresce por potencia de 2)
   rhi::BufferId batchCb_;                      // BatchCB: 16 bytes (uint first + pad)
   std::vector<InstanceGPU> instCpu_;
   ```
   Criar `batchCb_` em `init` como os outros: `dev.createBuffer({16, rhi::BufferUsage::Uniform, true}, nullptr);`.
3. No `end()`, no passe principal (depois de `std::stable_sort`, e so com `draws_` opacos):
   - Montar `items` (`BatchItem{ (u64)d.mesh, (u64)d.tex.id }`, mesmo tipo de id que o RHI usa).
   - `BatchPlan plan = planInstanceBatches(items, kMaxBatch);` com `kMaxBatch = 4096`.
   - Preencher `instCpu_[k]` a partir de `draws_[plan.order[k]]` (mesmos campos de `ObjectCB oc` hoje:
     world, color, metallic, roughness, receiveShadow, texInfo, emissive, fogColor, fogParams).
     Use `static_assert(sizeof(InstanceGPU) == sizeof(ObjectCB))` para garantir o layout.
   - Se `instCpu_.size() > instCap_`, recriar `instBuf_` com capacidade nova (potencia de 2) e
     `Usage` de storage, como os buffers de luz.
   - `dev_->updateBuffer(instBuf_, instCpu_.data(), instCpu_.size() * sizeof(InstanceGPU));`
4. Desenhar por lote, substituindo o laco atual de `cl_->drawIndexed(d.mesh->count, 1)`:
   ```cpp
   cl_->setStorage(4, instBuf_);   // Instances (slot 4 de pbrStorageNames)
   for (const InstanceBatch& b : plan.batches) {
     BatchCB bc{b.first, 0, 0, 0};
     dev_->updateBuffer(batchCb_, &bc, sizeof bc);
     cl_->setUniform(6, batchCb_);
     const Draw& d = draws_[plan.order[b.first]];   // primeiro item do lote: malha e textura iguais
     cl_->setTexture(0, d.tex.isNull() ? white_ : d.tex);
     cl_->setVertexBuffer(d.mesh->vertex); cl_->setIndexBuffer(d.mesh->index);
     cl_->drawIndexed(d.mesh->count, b.count);
   }
   ```
   Observacao: `setStorage` precisa ser chamado depois de `setPipeline` (ja acontece em `bindScene`).
   O laco antigo de objetos fazia `setUniform(1, objCb_)`; esse slot (ObjectBuffer) sai do pipeline PBR
   junto com o b6 antigo, e o `uniformNames` correspondente muda (ver 1b, passo 5).
5. Remover o `objCb_` do passe principal opaco. **Nao remova ainda** do passe de sombra, prepass e brilho:
   eles continuam por objeto nesta fase.

**Verificacao da fase 1c (teste visual obrigatorio):**
- Cena com 1000 esferas (`--ecs 1000 --play`). Compare com a versao anterior: a imagem deve ser igual.
- Objetos com textura e sem textura, metais e nao metais, emissivos, neblina: todos iguais.
- Abra o log e procure erros de "setStorage" ou de "variable not found".
- Me envie: `DrawStats` (draws, triangulos, bufferUploads, pipelineSwitches) antes e depois.

## 5. Fase 1d - Sombras e prepass por lote

1. **Sombra (`assets/shaders/shadow.hlsl`):** `ShadowBuffer` passa a ter so `lightViewProj` (b0).
   Adicionar `Instances` (t0 do VS, em outro registro livre) e `BatchBuffer` (b1). O VS calcula
   `world = Instances[batchFirst + iid].world`.
   No `gpu_renderer.hpp`, separar uma lista `castCpu_` (so `d.mat.castShadow`) e um `plan` proprio,
   e desenhar por lote por cascata. Nao reaproveite a lista principal: ela inclui objetos que nao projetam sombra.
2. **Sombra de pontuais (`shadow_point.hlsl`):** mesmo padrao. Atencao ao `o.depth = o.clip.w`: nao mudar.
3. **Prepass (`prepass.hlsl`):** `ObjectBuffer` vira `Instances` + `BatchBuffer`, igual a 1b.
   Confira no `gpu_renderer.hpp` quais pipelines usam `{"FrameBuffer", "ObjectBuffer"}` (o prepass aparece
   como `ed.uniformNames` ao redor da linha 139) e troque para os novos nomes nesses pipelines.
4. Remover os `updateBuffer(objCb_)` de sombra e prepass quando nao houver mais uso.

**Verificacao da fase 1d:** sombras com a mesma posicao de antes (comparar captura de tela);
SSAO com a mesma aparencia; a luz do sol e as luzes pontuais nao mudam de lugar.

## 6. Fase 1e - Brilho (bloom) e transparentes

- Brilho: o passe `emiPipe_` desenha so os objetos emissivos. **Mantenha por objeto** ate o fim do P1,
  porque a lista e pequena. Otimize depois, se o `DrawStats` mostrar que pesa.
- Transparentes (`tdraws_`): **nao agrupar.** A ordem de profundidade importa. Continuam por objeto.

## 7. Criterios de sucesso do P1

- Imagem identica (ou diferenca so de precisao) em todas as fases.
- `DrawStats.draws` cai de ~ N objetos para ~ numero de lotes (com 1000 esferas iguais, perto de 1).
- `DrawStats.bufferUploads` cai (uma escrita de instancias por quadro, em vez de N escritas de objeto).
- Frame time (benchmark `--ecs 1000 --play`, com `TOOL_BENCH_CSV`) nao piora. Mande o CSV.

## 8. Plano P2 a P6 (depois do P1 validado)

### P2 - Culling por frustum na CPU
- **Pronto:** `engine/render/include/tool/render/frustum_cull.hpp` (testado).
- **Falta:** raio e centro de cada malha (calcular no `upload`, a partir das posicoes; guardar em `GpuMesh`).
- **Ligar em `draw()`:** transformar o centro pela matriz `world` e testar `sphereInFrustum` com o frustum da camera.
  Descartar o objeto antes de `addDraw`.
- **Sombras:** NAO usar o frustum da camera para cortar objetos que projetam sombra. Uma sombra pode vir
  de um objeto fora da tela. Use o frustum da luz, ou nao corte sombras nesta fase.
- **Teste visual:** girar a camera devagar; nenhum objeto pode sumir na borda. Conferir com `DrawStats.draws`.

### P3 - Culling em compute e comando indireto
- **Pre-requisito no RHI:** `dispatch` de compute, buffer de contagem e `drawIndexedIndirect`.
  Hoje o RHI nao tem nada disso (ver `rhi.hpp`). Diligent suporta os tres.
- **Passos:** (1) compute recebe o buffer de instancias com o raio de cada uma, escreve as visiveis num
  buffer novo com `atomicAdd` no contador; (2) o desenho indireto le o contador.
- **Teste:** comparar a imagem com a da fase P2; medir `DrawStats`. Precisa de GPU.

### P4 - Oclusao Hi-Z
- **Pre-requisito:** P3 pronto.
- **Passos:** gerar a piramide de profundidade do quadro anterior (reducao por maximo, um mip por passe);
  no compute de P3, testar a esfera contra o mip certo.
- **Custo:** um quadro de latencia. Objetos que aparecem de repente podem falhar um quadro.
- **Teste:** atras de uma parede grande, `DrawStats.draws` deve cair. Teste visual com a camera se movendo rapido.

### P5 - LOD por distancia
- **Passos:** guardar 2 ou 3 malhas por objeto (a original e versoes simplificadas);
  escolher pela razao raio / distancia; instancias de LOD diferentes viram lotes diferentes.
- **Ferramenta:** simplificacao de malha (ex.: quadric edge collapse). Gerar offline e salvar no pacote.
- **Teste:** `DrawStats.triangles` deve cair com a distancia. Aparencia: nada de "pop" visivel perto da camera.

### P6 - Vegetacao distante em voxels ou impostors
- **Passos:** para objetos muito distantes, trocar por um proxy (impostor: quad que sempre olha para a camera,
  ou voxel grosso). Troca por limiar de distancia.
- **Teste:** floresta densa (ex.: 100 mil arvores). Comparar FPS e aparencia de longe.
- **Risco:** e o item mais caro. So comece depois de P1 a P5 medidos.

## 9. Metricas que voce me envia em cada fase

1. `DrawStats` (draws, triangles, pipelineSwitches, bufferUploads) do Jogo, em uma cena fixa.
2. CSV do benchmark com `TOOL_BENCH_CSV` (`--ecs 1000 --play`).
3. Dois screenshots (antes e depois) da mesma cena.
4. Log do driver (a linha `Interface SpriteUI: driver de render ...`) e qualquer linha com "error" ou "warning".

## 10. Prompt para implementar no VS Code

Use este prompt com o assistente do VS Code (ou com o Claude Code), na raiz do repositorio, na branch `ui-integrado`:

```text
Contexto: engine C++20 "ToolEngine" (branch ui-integrado). Objetivo: implementar o P1 (instanciamento) descrito
em docs/P1_IMPLEMENTACAO.md. Siga as fases 1a a 1d nesta ordem, uma por commit.

Regras:
1. Leia docs/P1_IMPLEMENTACAO.md inteiro antes de editar. Leia tambem engine/render/include/tool/render/instance_batch.hpp
   e tests/test_instance_batch.cpp (ja prontos e testados).
2. Nao apague arquivos. Nao mude a interface publica de tool::rhi sem necessidade; se mudar, explique.
3. Limite de UBO: o pbr.hlsl ja esta no limite de 8 (VS + PS). Nao aumente o numero de cbuffers no total.
4. Nao mexa em transparentes (tdraws_) nem no brilho nesta etapa.
5. Depois de cada fase: compile com a build existente (cmake --build, alvo tool_tests e a engine),
   rode tool_tests (deve dar 0 falhas) e mostre a saida resumida.
6. Se algo do Diligent nao existir na versao vendorizada (GetVariableByName por estagio, buffer estruturado),
   pare, descreva o que encontrou e me pergunte. Nao invente API.
7. Ao final de cada fase, escreva em CODAR.MD uma linha: fase, arquivos mudados, e o teste visual que o usuario deve rodar.
8. Mensagem de commit em portugues, com o formato: "Render: P1 fase 1x - <resumo>".

Entregue: lista de arquivos alterados por fase, saida do tool_tests, e o que NAO foi testado (nao ha GPU no ambiente).
```

## 11. Atualizacao do CODAR.MD

Ao concluir cada fase, o usuario faz o teste visual indicado na secao 4/5/6 e envia os resultados.
Nao marque o P1 como concluido sem o teste visual.
