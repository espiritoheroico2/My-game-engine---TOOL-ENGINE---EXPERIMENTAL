# 37 - FSR no DX12 e frame generation: pesquisa e plano

Status: pesquisa e plano. Nada disto foi implementado nesta etapa.
Regra desta fase: sem medicao de desempenho. Cada passo termina com build, selftest e ctest; a medicao (3 execucoes, mediana) fica para a bateria de testes depois do core.

Legenda: **[web]** = confirmado em fonte da web (GPUOpen, pesquisa de 2026-11-28); **[repo]** = verificado no repositorio; **[estimativa]** = engenharia, nao medido.

## 1. Estado atual [repo]

- O SDK esta em `third_party/fidelityfx/` (gitignored): headers `ffx_api.hpp`, `ffx_upscale.hpp`, `ffx_framegeneration.hpp`, e binarios de DX12 e Vulkan (`amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll`, e os equivalentes `_vk`). A pasta `include/dx12/` tambem existe.
- `engine/fidelityfx/src/fidelityfx.cpp` so cria o contexto de upscale por **Vulkan**: `initVulkan(VulkanHandles, ...)` recebe VkInstance, VkPhysicalDevice e VkDevice (`fidelityfx.hpp`, linhas 35 a 99). Nao existe caminho DX12.
- Os handles Vulkan vem do Diligent por `tool::rhi::vulkanNative`, `vulkanImage` e `vulkanCommandBuffer` (`engine/rhi_diligent/include/tool/rhi/diligent_device.hpp`). **Nao existe equivalente D3D12** (nada de `ID3D12Device`, `ID3D12Resource` ou `ID3D12GraphicsCommandList`).
- O FSR 1 (EASU + RCAS) e um pixel shader do proprio RHI (`engine/render/include/tool/render/fsr1.hpp`). Ele ja **independe de API**: roda em D3D12 e em Vulkan sem mudanca de codigo de shader, desde que os shaders HLSL compilem para o backend.
- O upscale por CPU (`upscale.hpp`: Bilinear, Bicubic, Lanczos2) continua como caminho de teste e de referencia, como definido no plano 35.
- A engine ja le o DX12 como backend do Diligent (`rhi_diligent/src/diligent_device.cpp`), entao o caminho D3D12 existe no RHI; falta so o lado nativo do FFX.

## 2. O que a pesquisa confirma [web]

| Tema | Achado | Fonte |
|---|---|---|
| Backend DX12 do FFX | Entra na criacao do contexto encadeando `ffxCreateBackendDX12Desc` (tipo de header `FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12`) com o `ID3D12Device`. Memoria de scratch: `ffxGetScratchMemorySizeDX12(maxContexts)`. | [GPUOpen DX12 Backend](https://gpuopen.com/manuals/fidelityfx_sdk/reference_documentation/sdk/backends/dx12_backend/d_x12_backend/), [ffx_api](https://gpuopen.com/manuals/fidelityfx_sdk2/getting-started/ffx-api/) |
| DLLs separadas (SDK 2.0+) | Upscaler, frame generation e loader sao DLLs distintas. Quem carregava `amd_fidelityfx_dx12.dll` deve carregar `amd_fidelityfx_loader_dx12.dll`. A pasta `bin` do repo ja tem esses arquivos. | [GPUOpen DX12 FrameInterpolation](https://gpuopen.com/?p=58124) |
| FSR 3.1 upscaler | Mesmas entradas do FSR 2: cor, profundidade, motion vectors no resolucao de render, jitter, reset. Suporta DX12 e Vulkan. | [GPUOpen FSR 3.1.4](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/) |
| Frame generation, swapchain | No DX12 o FFX cria uma swapchain propria (implementa `IDXGISwapChain4`) com `ffxCreateFrameinterpolationSwapchainForHwndDX12`, ou substitui a existente com `ffxReplaceSwapchainForFrameinterpolationDX12`. Tem modo passthrough para desligar o FG sem recriar a swapchain. | [GPUOpen FrameInterpolation](https://gpuopen.com/manuals/fidelityfx_sdk/fidelityfx_sdk-group__dx12frameinterpolation), [FSR Frame Generation (Swapchain)](https://gpuopen.com/manuals/fidelityfx_sdk2/techniques/frame-interpolation-swap-chain/) |
| FG: UI | A API recebe um back buffer **sem HUD** (HUDless) para extrair a UI, e um callback de composicao de UI. AMD avisa que o erro de movimento da UI afeta a legibilidade do texto. | [GPUOpen FSR 3.1 release](https://gpuopen.com/learn/amd_fsr_3_1_release/) |
| FG: motion vectors e camera | Motion vectors e profundidade no resolucao de render; flags para vetores em resolucao de display e para jitter ja aplicado. A camera precisa de posicao, up, right e forward; dado errado ou de baixa precisao gera imagem errada. | [GPUOpen FSR Frame Generation](https://gpuopen.com/amd-fsr-framegeneration/) |
| FG: plataforma | FSR 3.1 lista DX12 e Vulkan. O modo analitico (sem ML) precisa de **Shader Model 6.2**. A versao ML (FSR 4.0.x) exige Windows 11, GPU **RX 9000 ou superior** e Agility SDK 1.4.9+. | [GPUOpen FSR 3.1.x](https://gpuopen.com/?p=62532), [FSR 4.0.2 FG ML](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-ml/) |
| FSR 4 upscaling | Segundo o estudo `UPSCALING_FSR_XESS.md`, a AMD inclui RDNA 2 (RX 6000) na versao FSR 4.1 (INT8). Isso e o que o repo ja usa na RX 6500 XT pelo caminho Vulkan. O suporte no DX12 segue a mesma versao, mas **nao testei**. | estudo do repo, [Tom's Hardware FSR 4.1](https://www.tomshardware.com/pc-components/gpu-drivers/amd-makes-fsr-4-upscaling-official-for-radeon-rx-7000-and-6000-series-cards-rdna-3-and-rdna-2-chips-will-soon-enjoy-improved-visuals) |

**Testado pelo usuario na RX 6500 XT [teste do usuario]:** FSR 4 (upscale) funciona, FSR 3.1 frame generation funciona, e o upscaling do XeSS e compativel. Esses resultados sao a base do plano; o numero de versao exata do driver usado nao foi registrado e deve ser anotado no proximo teste. O FG do XeSS 2.0 **nao foi testado** nessa GPU (ver Fase G).

## 3. Por que o DX12 e um passo separado

1. **O FFX pede o device e as texturas nativas do D3D12.** O RHI do Diligent esconde `ID3D12Device`, `ID3D12Resource` e `ID3D12GraphicsCommandList`. Falta expor esses ponteiros em um `d3d12Native(IDevice&, D3D12Native&)`, espelhando o `vulkanNative` atual.
2. **Estado de recurso.** O Diligent controla as transicoes de barreira (`D3D12_RESOURCE_STATE`). O FFX recebe o estado de cada recurso. A regra segura: antes de chamar o FFX, usar a transicao do proprio Diligent (`TransitionResourceStates`) e informar ao FFX o estado que o Diligent registrou. Confirmar na API do Diligent se o estado atual de uma textura pode ser lido (uma opcao e manter o estado no nosso lado para as texturas que entram no FFX).
3. **Mesma command list.** O FFX grava na command list que recebe. Em D3D12 isso deve ser a command list de contexto imediato do Diligent, e nao uma lista separada, para evitar sincronizacao manual. Confirmar se o `IDeviceContextD3D12::GetD3D12CommandList` devolve a lista ativa do frame.
4. **O caminho Vulkan nao muda.** Os dois backends ficam atras de uma interface unica (`fidelityfx.hpp`), e o `initVulkan` vira um dos ramos.

## 4. Plano de implementacao

### Fase A - FSR 1 em D3D12 (sem SDK, baixo risco)
- Verificar que o `fsr1.hlsl` compila e roda com o backend D3D12 do Diligent (o RHI ja e neutro).
- Teste funcional: `fsr1_test` com `--api d3d12` (novo argumento) gera PPM que bate com o Vulkan em pixels (tolerancia definida no teste).
- Entrega: FSR 1 selecionavel nos dois backends.

### Fase B - Lado nativo D3D12 no RHI
- Criar `D3D12Native { ID3D12Device*; ID3D12GraphicsCommandList*; }` e as funcoes `d3d12Native`, `d3d12Resource(IDevice&, TextureId)` e `d3d12CommandList(IDevice&)`, em `rhi_diligent`. Sem ifdef: fora do D3D12 retornam nulo.
- Teste: um teste pequeno cria textura, pega o `ID3D12Resource` e confirma que nao e nulo (so em D3D12).

### Fase C - Upscaler FFX em D3D12
- Em `fidelityfx.cpp`: `initDx12(device, maxRenderW, maxRenderH, maxOutW, maxOutH)` com `ffxCreateBackendDX12Desc` e o loader `amd_fidelityfx_loader_dx12.dll` carregado por caminho (`TOOL_FIDELITYFX_BIN_DIR`, como ja acontece com o Vulkan).
- `dispatchDx12(...)`: monta o `ffxDispatchDescUpscale` com cor, profundidade, motion vectors, saida e jitter. Reset na primeira frame e em corte de camera.
- Consulta de suporte (`ffxQuery`, GET_VERSIONS) no DX12, igual ao probe atual do Vulkan. O probe ganha o argumento `--api dx12`.
- Entradas que o renderer ainda nao tem (ver Fase D).

### Fase D - Entradas do renderer (comum aos dois backends)
- **Motion vectors:** um alvo R16G16 (ou RG32F) em resolucao de render. Preenchido pelo G-buffer: vetor da posicao do pixel no frame anterior (matriz de vista-projecao anterior, por objeto que se move). Objetos sem movimento reaproveitam a matriz atual. Particulas: usar o movimento do emissor (aproximacao) e marcar como sem vetor se for transparente.
- **Jitter:** sequencia de Halton(2,3) aplicada na projecao, com fase por frame. Flag do FFX informa que o jitter ja foi aplicado.
- **Profundidade:** o FFX aceita profundidade invertida e infinita (flag). Conferir a convencao atual do renderer antes de marcar a flag.
- **Exposicao:** valor vindo do pos-processo (ou 1.0 enquanto nao houver auto-exposicao).
- **Reset:** em troca de cena, troca de resolucao, camera cortada ou primeiro frame.
- **Interacao com TAA:** o FSR 3.1 ja faz o papel de TAA. Com FSR ligado, o AA temporal (M2) fica desligado ou reduzido, para nao dupla-filtragem.

### Fase E - Frame generation (FSR 3.1 analitico) em D3D12
Pre-requisitos: Fase C funcionando e Fase D completa.
- **Swapchain:** primeiro um teste de viabilidade: criar a swapchain FG a partir do HWND do SDL e copiar o frame final do Diligent para o back buffer dela. Se o Diligent nao permitir (a swapchain do Diligent e a unica com `Present`), a alternativa e `ffxReplaceSwapchainForFrameinterpolationDX12` sobre a swapchain que o Diligent criou. Decidir com o teste.
- **HUDless:** renderizar a UI (SpriteUI do editor e UI do jogo) em um passe separado, depois da composicao. O FG recebe o frame sem UI e o callback compoe a UI no frame interpolado.
- **Camera:** enviar posicao, up, right e forward a cada frame (vem da camera ativa).
- **Async compute:** o FG pode usar uma fila de compute separada. Comecar sem (tudo na fila grafica) e so depois avaliar o ganho, sem medir agora.
- **Passthrough:** tecla e opcao em Settings > Graphics para ligar e desligar o FG sem recriar a swapchain.
- **Latencia:** o FG adiciona um frame de latencia. Mostrar isso na opcao, junto com o aviso de UI.

### Fase F - FSR 4 (upscale ML) em D3D12
- Mesmo contexto da Fase C, trocando a versao da DLL do upscaler. A RX 6500 XT ja roda FSR 4 pelo caminho Vulkan (teste do usuario); no DX12 a consulta de suporte (`GET_VERSIONS`) continua obrigatoria antes de liberar a opcao.
- FSR 4 frame generation (ML) exige RX 9000 e Windows 11. Nao aplicavel a RX 6500 XT: fica so como caminho para a GPU do futuro, com a consulta de suporte escondendo a opcao quando a GPU nao atende.

### Fase G - XeSS 2.0 (upscale e frame generation)
Pre-requisitos: Fases C e D (o XeSS usa as mesmas entradas do FSR: cor, profundidade, motion vectors, jitter, exposicao, reset).
- **G1 - Upscale XeSS 2.0 em D3D12 e Vulkan:** o upscaling do XeSS ja funciona na RX 6500 XT (teste do usuario). Integrar pela API do XeSS SDK 2.x, com consulta de suporte no inicio (caminho DP4a com SM 6.4 em GPU nao Intel). Escolher o caminho por GPU: XMX (Arc) ou DP4a (RX 6500 XT).
- **G2 - Frame generation XeSS 2.0:** **nao testado na RX 6500 XT.** Fica como opcao experimental, escondida atras de consulta de suporte e de uma flag em Settings > Graphics marcada como "experimental". Antes de liberar, confirmar que o FG roda nessa GPU (o SDK 2.1 liberou o FG para AMD, mas o desempenho e a qualidade ainda nao foram verificados aqui).
- **G3 - HUDless, camera e passthrough:** mesmas exigencias da Fase E. Se o FG do FSR 3.1 ja funciona, reaproveitar a infraestrutura de HUDless e de camera, e so trocar a API.
- **Licenca:** o XeSS 2 e binario fechado e a licenca reportada proibe modificar o codigo. Antes de copiar as DLLs para o Projeto Final, ler o texto completo da licenca (ainda nao lido).

## 5. Riscos

| Risco | Efeito | Mitigacao |
|---|---|---|
| Diligent e dono da swapchain | FG nao consegue apresentar | teste de viabilidade da Fase E antes de tudo |
| Estado de barreira divergente | corrupcao ou crash no FFX | usar so transicoes do Diligent; validacao D3D12 ligada em Debug |
| Motion vectors ruins em particulas e objetos skinned | fantasmas | flag por material; particulas sem vetor |
| UI com rastro | texto ilegivel | HUDless obrigatorio na Fase E |
| Suporte oficial na RX 6500 XT | FG pode nao rodar | consulta de suporte e opcao escondida quando nao suportado |
| Licenca | FidelityFX (MIT) | conferir `third_party/fidelityfx/LICENSE.md` antes de redistribuir as DLLs no Projeto Final |
| XeSS FG nao testado na RX 6500 XT | FG pode falhar ou ter qualidade ruim | opcao experimental escondida atras de consulta e flag; so libera depois de teste |
| Licenca do XeSS 2 (binario fechado) | Pode proibir redistribuir ou modificar | ler a licenca completa antes de copiar as DLLs |

## 6. Ordem e criterio de pronto

1. Fase A (FSR 1 em D3D12): build, `fsr1_test` nos dois backends.
2. Fase B (nativo D3D12): build, teste de ponteiros nao nulos.
3. Fase D (motion vectors, jitter, reset) no renderer, nos dois backends.
4. Fase C (FSR 3.1/4 upscaler em D3D12): consulta de suporte e selftest.
5. Fase E (frame generation): teste de viabilidade da swapchain primeiro; depois HUDless, camera e passthrough.
6. Fase F (FSR 4 ML): so com consulta de suporte.
7. Fase G (XeSS 2.0): G1 upscale primeiro; G2 frame generation so depois de teste na RX 6500 XT, como experimental.

Cada fase so e concluida com build, selftest e ctest. Medicao de desempenho fica para a bateria de testes depois do core.

## 7. Perguntas em aberto

- O Diligent permite criar uma swapchain FG sem tomar a propria? (teste de viabilidade, Fase E)
- O estado de barreira de cada textura pode ser lido pela API do Diligent? (Fase B)
- Qual versao de driver foi usada nos testes da RX 6500 XT (FSR 4, FSR 3.1 FG, XeSS)? Registrar no proximo teste.
- O FG do XeSS 2.0 funciona na RX 6500 XT? (nao testado)
- O estudo `docs/estudos/AMD_FSR.md` ainda diz que o FSR 4 foi "publicado por engano". A pesquisa desta fase nao confirma isso; revisar o estudo antes de citar.
