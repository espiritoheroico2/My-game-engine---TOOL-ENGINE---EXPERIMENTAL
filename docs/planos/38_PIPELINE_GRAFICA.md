# 38 - Pipeline gráfica: tirar a carga da CPU (2026-11-28)

## Como medir (sem export do Profiler)

```
Projeto Final\ToolEditor.exe --project <pasta vazia> --instances 2 --api vulkan|d3d12|opengl --bench-frames 600 --bench-warmup 120
```

- `TOOL_GPU_TIMERS=1`: tempo de GPU por passe (Vulkan/D3D12).
- `TOOL_BENCH_IMGDUMP=<prefixo>`: grava o último quadro em PPM, para comparar imagens.
- `TOOL_SKY_CPU=1`: força o skybox antigo da CPU (comparação A/B).

**Atenção:** meça sempre com o editor em **Release**. Qualquer build Debug sobrescreve `Projeto Final\ToolEditor.exe`, e a Debug é várias vezes mais lenta.

## Etapa 1 (feita): três gargalos de CPU

Cena de 2 objetos, 1280x720, só o render (sem painéis), média por quadro:

| API | Antes | Depois |
|---|---:|---:|
| Vulkan | 4,07 ms (246 fps) | 1,06 ms (~940 fps) |
| D3D12 | 4,62 ms (216 fps) | 2,20 ms (~455 fps) |
| OpenGL | 7,45 ms (134 fps) | 1,07 ms (~935 fps) |

Medido em Release, com 15.000 quadros e 0 erros, já com o limite de quadros em voo da etapa 1b (o tempo reflete o ritmo real da GPU). Testes: selftest 352/0 e ctest 91/91.

Os três gargalos e o que mudou em cada um:

1. **Um Flush por passe.** Cada `submit()` fazia `ctx->Flush()`: ~20 envios à fila da GPU por quadro (`vkQueueSubmit` / `ExecuteCommandLists` / `glFlush`). Agora o envio acontece uma vez por quadro, em `endFrame()` e na leitura. Arquivo: `engine/rhi_diligent/src/diligent_device.cpp`.
2. **Leitura síncrona no OpenGL.** O GL fazia `CopyTexture` + `WaitForIdle`: a CPU ficava ~5 ms parada esperando a GPU. Agora usa o mesmo caminho assíncrono do Vulkan/D3D12: o fence do Diligent GL (`glFenceSync`) e o PBO de staging.
3. **Skybox pixel a pixel na CPU** (~1,8 ms). Virou um passe de tela cheia na GPU (`assets/shaders/hlsl/skybox.hlsl`), com blend "por baixo" (cor: `INV_DEST_ALPHA`, `DEST_ALPHA`).
   - A imagem é **idêntica pixel a pixel** à da CPU nas três APIs: 0 pixels diferentes em 867 mil, testado com a cor sólida.
   - Corrige também um bug: com bloom ligado, o skybox da CPU não aparecia (a composição do bloom grava alfa 1 em tudo).

Também nesta etapa:

- **Partículas no OpenGL.** O GLSL de `particles.hlsl` e `skybox.hlsl` agora é gerado por `tools/shaders/build_glsl.py`. As ferramentas `glslangValidator` e `spirv-cross` foram compiladas das fontes em `third_party/DiligentCore/ThirdParty`. Antes o GL desligava as partículas.
- **Oclusão só com 64 ou mais objetos.** Numa cena pequena, o HiZ gastava 0,49 ms de GPU e ~10 draws para não esconder nada.

Não verificado:

- skybox com **imagens** nas faces (só a cor sólida foi comparada pixel a pixel);
- teste na tela, com os painéis abertos.

## Etapa 1b (feita): ritmo de quadros e a pergunta "por que a interface não usa D3D12?"

**Limite de 2 quadros em voo** (fence por quadro em `present()`; `IFence::Wait` no quadro N-2):

- Sem limite, a CPU (agora rápida) enfileirava quadros mais rápido do que a GPU executa.
- No Vulkan, isso gerava ~700 avisos "dynamic heap almost exhausted" em 3.000 quadros, com esperas forçadas de 1,5 a 1,9 ms.
- Com o limite: 0 erros em 15.000 quadros nas três APIs.
- O tempo medido subiu de 0,67 para 1,06 ms no Vulkan. É o valor honesto, o ritmo real da GPU; o 0,67 era a CPU correndo à frente.

**Interface (SDL) em cada driver × API da cena** (`TOOL_UI_DRIVER=<driver>`, média por quadro):

| UI \ cena | D3D12 | Vulkan | OpenGL |
|---|---:|---:|---:|
| direct3d11 (padrão) | 2,16 | 0,68 | 0,44 |
| direct3d12 | 2,97 | 3,08 | 2,19 |
| vulkan | 2,03 | 1,45 | 1,35 |
| opengl | 2,12 | 0,72 | 1,30 |

Resposta: a interface em D3D12 **piora**. O `Present` do renderizador D3D12 do SDL custa 1,7 a 2,3 ms em qualquer combinação. O D3D11 é o melhor driver para a UI enquanto ela for do SDL.

O custo extra da cena em D3D12 também não era disputa entre APIs. Medi por partes: era o `Present` de uma **swapchain inútil** numa janela oculta (1,67 ms), que o editor cria para o D3D12 desde a base da engine.

**Item aberto: D3D12 sem swapchain vaza descritores.** Removi a janela e o D3D12 caiu para 0,59 ms, mas o heap de descritores dinâmicos (GPU CBV/SRV/UAV) esgota aos poucos e o processo trava:

- ~300 quadros com o heap padrão (8.192 dinâmicos);
- ~8.000 quadros com 65.536.

O que já descartei:

- falta de `ReleaseStaleResources` (agora é chamado);
- limite de quadros em voo (com 1 ou 2, ainda vaza);
- capacidade (o heap maior só adia).

Com `WaitForIdle` a cada quadro, ou com a swapchain, não vaza. Por isso a janela voltou para o D3D12.

Próximo passo para fechar esse item: build do Diligent com `DILIGENT_DEVELOPMENT` e as estatísticas do heap por quadro (a mensagem "GPU heap max allocated size" sai no fim do processo), para ver qual alocação não volta.

## Etapa 2: o que ainda pesa

1. **D3D12: swapchain inútil** (ver o item aberto na etapa 1b). Custa ~1,2 ms por quadro até o vazamento sem swapchain ser resolvido.
2. **Ida e volta da imagem.** A cena é renderizada na GPU, lida para a CPU (3,7 MB por quadro a 720p), enviada de novo para uma textura do SDL e só então desenhada. Esse é o maior custo estrutural que sobra: banda, latência de 1–2 quadros e uma cópia na CPU.
3. **Pós-processamento (FXAA/sharpen) na CPU** (`applyPostFx`, depois da leitura). Deve ir para a GPU como passe de tela cheia, como o skybox.
4. **Draw calls da sombra.** São 3 cascatas × N objetos. Com instancing por cascata (índice de cascata no `SV_InstanceID`), vira 1 draw por lote.

## Etapa 3 (proposta): editor desenhado pelo Diligent

Por que vale a pena, com os números de agora:

- No render puro, a leitura e o envio da imagem já custam pouco (~0,1 a 0,5 ms).
- No editor real, com os painéis abertos, o "UI draw" foi 2,6 ms em Release (export das 10:40). Esse custo é o SpriteUI gerando vértices na CPU para cada painel (`spriteui_host.hpp`).
- Na GPU, com o adaptador `ToolEngineRhi`, cada sprite vira uma instância de shader, e a CPU só monta o pacote.

Objetivo: **uma API, um dispositivo, nenhuma leitura**. A Scene e o Game viram texturas do Diligent amostradas direto pela UI, e a janela é apresentada pela swapchain do Diligent.

- **3a.** Um `SpriteUiDiligentRenderer` com a mesma interface `draw(FramePacket)` do `SpriteUiSdlRenderer`. A base é o adaptador `SpriteUi::ToolEngineRhi`, que já existe e tem testes.
- **3b.** Swapchain do Diligent na janela principal. O `SDL_Renderer` sai do editor, e `SDL_RenderPresent` vira `IDevice::present()`. O vsync passa a ser o do `setPresentVsync`.
- **3c.** Vistas Scene/Game: `setView(slot, rhi::TextureId)` no lugar do `SDL_Texture`, e o `readTextureAsync` sai do caminho do editor. A leitura continua só para `--screenshot` e para o bench.
- **3d.** Troca de API em tempo real: recriar o renderizador da UI junto com o dispositivo (hoje só a cena é recriada).
- **3e.** Remover o cache por painel em render target do SDL (`spriteui_host.hpp`), que deixa de fazer sentido.

Riscos:

- **Troca de API:** o UI precisa ser recriado sem perder estado.
- **OpenGL:** contexto e janela são os mesmos da UI.
- **Fontes e ícones:** os atlas precisam ser recriados no dispositivo novo.

Faça uma fase por vez, com benchmark e imagem a cada passo.

### 3a + 3b (feitas, opcionais por enquanto): `TOOL_UI_GPU=1` ou `--ui-gpu`

- **Backend novo:** `apps/editor/ui_gpu_backend.hpp` e `ui_gpu_sink.hpp`, com o shader `assets/shaders/hlsl/editor_ui.hlsl` (e o GLSL gerado). O quadro inteiro da UI é gravado na CPU e enviado de uma vez: um upload de vértices e índices, uma lista de comandos e um draw por lote. O shader faz os cantos arredondados por SDF e escolhe entre bilinear e nearest.
- **Mesmo visual do SDL:**
  - backbuffer UNORM (`DeviceInfo::srgbSwapchain = false`), com mistura em espaço sRGB;
  - cores convertidas como no `sdlColor`;
  - texturas enviadas como RGBA8 cru.
- **Fachada:** o `SpriteUiSdlRenderer` passou a ter dois backends, então os painéis quase não mudaram.
  - `sui::ViewImage` substitui o `SDL_Texture*` das vistas Scene e Game.
  - `sui::UiWindowOf()` substitui `SDL_GetRenderWindow`.
  - No modo GPU, os painéis recebem um `SDL_Renderer` de software 1x1, que não desenha nada.
- **Fallback para o SDL:** modo CPU, falha da GPU, tela inicial e driver de vídeo `dummy`.
- **RHI:**
  - `IDevice::updateTexture` e `IDevice::backbufferFormat()`;
  - `DeviceInfo::srgbSwapchain` e `DeviceInfo::presentToWindow`;
  - o destrutor do dispositivo espera a GPU terminar antes de liberar.
- **D3D12/D3D11 com tearing.** Patch no Diligent (`SwapChainD3DBase.hpp`, `SwapChainD3D12Impl.cpp`, `SwapChainD3D11Impl.cpp`): `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` e `Present(0, DXGI_PRESENT_ALLOW_TEARING)`.
  - Sem o patch, o modelo flip em janela, junto com o objeto de latência, travava o D3D12 no refresh do monitor: 14,4 ms, ~60 fps, mesmo com vsync desligado.
  - Com o patch: 1,63 ms.

Medidas (Release, render puro com `--bench-frames 1500`, janela real `SDL_VIDEODRIVER=windows`):

| API | UI SDL | UI na GPU |
|---|---:|---:|
| D3D12 | 2,24 ms | 1,63 ms |
| Vulkan | 1,07 ms | 1,82 ms |
| OpenGL | 1,08 ms | 1,19 ms |

- No modo GPU, o present acontece de verdade numa janela de 1600x900.
- No modo SDL, o Vulkan e o OpenGL renderizam fora da tela, sem swapchain. Por isso esse teste não compara os dois modos de forma justa.
- Com a interface desenhada (`--screenshot`), o "UI draw" caiu de ~2,2 para ~1,75 ms nas três APIs, e "Panels" de ~1,2 para ~0,8 ms.

Imagem (Vulkan contra o SDL D3D11, 1600x900):

- Hierarchy: 2.199 de 115.500 pixels diferentes;
- barra de menu: 239 pixels, diferença máxima de 1;
- o resto é animação do fogo e a borda AA dos cantos (o SDF da GPU contra o leque de triângulos do SDL).

Limites conhecidos:

- Depois de uma swapchain D3D12 na janela principal, o Vulkan não consegue criar a dele na mesma janela (`ERROR_FORMAT_NOT_SUPPORTED`, AMD). Nesse caso a UI fica no SDL e um aviso vai para o log. D3D12 → OpenGL → Vulkan funciona.
- O texto e a geometria do `spriteui_diagnostics` não aparecem no modo GPU.
- `uploadMaterialThumb` reenvia o atlas inteiro (4 MB) a cada miniatura.

Não verificado na tela: painéis Assets e Game, e o modo `--player`.

### Correções depois do primeiro teste do usuário (2026-11-28)

- **Partículas só apareciam na frente de malhas.** O skybox na GPU rodava depois das partículas, e ele mistura "por baixo", usando o alfa do destino. Como as partículas aditivas não gravam alfa, o céu as cobria por inteiro. Agora o skybox roda logo depois dos opacos, antes dos transparentes e das partículas (`gpu_renderer.hpp`).
- **A aba inicial era o Narrative Controller.** Janelas abertas fora do layout salvo entravam como aba ativa depois de a Scene ter sido selecionada. Agora a Scene é selecionada depois desses attach (`dock_host.hpp`, `beginFrame`).
- **OpenGL com PBR desatualizado.** O `pbr.hlsl` mudou em 27/11 (commit 04104de), mas o GLSL era de 22/11. O mesmo valia para o `skybox`. Regenerei `pbr*`, `pbr_object*` e `skybox.ps`. Diferença na cena contra o Vulkan: 2.550 de 162 mil pixels acima de 8 níveis, menor que a do D3D12 contra o Vulkan (4.486, quase tudo a animação do fogo).
  - Não há GLSL para os computes (cull, HiZ, oclusão, motion) nem para o FSR 1, então o OpenGL usa o caminho de CPU nesses recursos.
  - Para não acontecer de novo: rodar `python tools/shaders/build_glsl.py --check` antes de commitar mudanças em HLSL. Esse check acusa diferenças só de versão das ferramentas; comparar com as mesmas versões.

### Resolvido: D3D12 lento com duas vistas no quadro

Causa: medi com temporizadores provisórios dentro do `renderView`. O tempo estava em `ClusterSystem::Execute`, os clusters de luz da Light Prism: ~7,7 ms de CPU por quadro no D3D12, contra ~0,6 ms no Vulkan.

- A Scene e a Game têm tamanhos diferentes, então o número de clusters alternava a cada vista.
- Com isso, `EnsureBuffers` recriava os 4 buffers de cluster (vários MB) duas vezes por quadro, e o fast-path sem luzes reenviava o LightGrid zerado.
- No D3D12, criar e liberar recursos é caro, e isso também segurava a GPU: todos os passes pareciam 4 a 6 vezes mais lentos.

Correção em `third_party/LightPrism/src/Cluster/ClusterSystem.cpp`:

- os buffers só crescem, nunca encolhem;
- o grid zerado continua valendo enquanto couber no buffer;
- os shaders usam o `Grid.clusterCount`/`maxIndices` do constant buffer, então buffers maiores são seguros.

Resultado no editor real (Scene + Game, UI na GPU, Release, 1.000 quadros):

| API | Antes | Depois |
|---|---:|---:|
| D3D12 | 13,8 ms (~72 fps) | 4,41 ms (~227 fps) |
| Vulkan | 4,92 ms | 4,76 ms |
| OpenGL | 3,95 ms | 3,36 ms |

O SSAO no D3D12 foi de 3,0 para 0,58 ms, e o opaque de 0,99 para 0,19 ms.

Descartados no caminho:

- o compilador de shader (DXC deu o mesmo tempo que FXC);
- buffers de instância dinâmicos contra estáticos (sem diferença).

### (histórico) Diagnóstico inicial

Sintoma: no editor real (Scene + Game no mesmo quadro), o D3D12 gasta ~14 ms por quadro, e o Vulkan ~5 ms. Medido:

- **Não é a UI na GPU.** Com a UI do SDL, é o mesmo (16 ms).
- **Não é o compilador de shader.** O DXC (SM 6) deu o mesmo tempo que o FXC.
- **Só com a vista Game** (1280x720), o D3D12 empata com o Vulkan: 1,78 contra 1,82 ms, com os passes de GPU iguais (ssao 0,37 contra 0,39 ms).
- **Com as duas vistas**, todos os passes de GPU do D3D12 ficam 4 a 6 vezes mais lentos:

| Passe | D3D12 | Vulkan |
|---|---:|---:|
| ssao | 3,0 ms | 0,65 ms |
| opaque | 0,99 ms | 0,25 ms |
| skybox | 0,33 ms | 0,05 ms |

- Os alvos por tamanho já ficam em cache (`GpuRenderer::resize`), então não é recriação de textura.

Suspeita: a segunda vista do quadro reescreve os mesmos buffers (instâncias, CBs) que a primeira ainda usa. No D3D12, o `UpdateBuffer` do Diligent com transições de estado serializa a GPU, e na AMD a troca de render target para SRV pode descomprimir as superfícies. Próximos passos:

1. dar um conjunto de buffers por vista;
2. medir com PIX ou Radeon GPU Profiler.

Falta:

- tornar o modo GPU o padrão, depois do teste do usuário;
- 3c: as vistas como texturas da GPU, sem leitura;
- 3e: remover o cache por painel do SDL.
