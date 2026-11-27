# Ray tracing: estado atual e planejamento

Estudo somente de leitura. Nenhum codigo foi alterado.

Legenda: **CONFIRMADA** (com caminho:linha ou URL lido) ou **ESTIMATIVA** (raciocinio, sem medicao). Numeros de desempenho nao medidos aparecem como "a medir".

## 1. Estado atual

### 1.1 O que a opcao "Enable ray tracing" faz hoje

Resumo: o ray tracing do projeto e so CPU, restrito ao AO por raios no renderizador por software (SoftRenderer). Na GPU, as opcoes de RT nao fazem nada.

- **Defaults**: `rayTracing` = true e `rtAo` = false, lidos com `ser::flag` em `apps/editor/project.hpp:381-382`. `rtAoRays` (padrao 16, faixa 1 a 64) em `project.hpp:383`; `rtAoRadius` (padrao 0.6 m) em `project.hpp:384`. Struct em `engine/render/include/tool/render/lighting.hpp:80-87`. **CONFIRMADA**.
- **Rotulos da UI**: "Enable ray tracing", "Ray-traced ambient occlusion (CPU)", "RT AO rays per pixel", "RT AO radius (m)" em `apps/editor/main.cpp:2143-2148` e `apps/editor/settings_panel.hpp:711-714`. O rotulo ja diz "(CPU)". **CONFIRMADA**.
- **Unica consumidora**: o bloco `if (lset.rayTracing && lset.rtAo)` em `apps/editor/editor_state.hpp:1348`. Fora dele, `rayTracing` so e lido na serializacao. **CONFIRMADA** (busca por `rayTracing` no repositorio).
- **Onde o RT AO entra**: `renderer.setRayTracedAo(...)` em `apps/editor/editor_state.hpp:1369`. O metodo existe so em `engine/render/include/tool/render/soft_renderer.hpp:282`. A GPU (`GpuRenderer`, `gpu_renderer.hpp`) nao tem nenhuma chamada a RT. **CONFIRMADA**.
- **Porque o usuario ve "nao funciona"**:
  1. Com a vista na GPU (Diligent -> readback, `editor_state.hpp` cabecalho, linha 6), o RT AO nao e aplicado. O AO visivel e o SSAO da GPU (`gpu_renderer.hpp:263, 378`).
  2. Na CPU, o caminho funciona, mas e caro. Medicao registrada no proprio projeto: RT AO com 24 raios ~230-300 ms por quadro a 640x360 em maquina de 2 nucleos (`docs/referencia/CHANGELOG.md:161`). **CONFIRMADA** (medicao anterior do autor, nao refeita aqui).
  3. A opcao "Enable ray tracing" vem ligada por padrao, mas, sozinha, nao muda nenhum pixel. O usuario ve a caixa marcada e nenhum efeito.

### 1.2 CPU x GPU no caminho atual

| Parte | Onde roda | Referencia |
|---|---|---|
| BVH da cena (`LightPrism::Rt::Bvh`) | CPU, reconstruido so quando a assinatura da cena muda (`rtSignature()`) | `editor_state.hpp:1350-1364`, membro `rtBvh_` em `editor_state.hpp:264` |
| Triangulos enviados ao BVH | CPU; so objetos visiveis (voxels de fora ficam de fora, "por enquanto") | `editor_state.hpp:1345-1358` |
| Raio de AO por pixel (`RtAmbientOcclusion`) | CPU, multithread por faixas de pixel | `soft_renderer.hpp:742`; `third_party/LightPrism/include/LightPrism/Rt/RtEffects.hpp:22, 70`; CHANGELOG `docs/referencia/CHANGELOG.md:159` |
| Shadow RT (`RtShadowConfig`) | So existe na biblioteca (CPU); nao esta ligado na engine | `RtEffects.hpp:15` |
| Probe grid de GI por raios (`ProbeGrid`) | So existe na biblioteca; nao ligado | `third_party/LightPrism/include/LightPrism/Rt/ProbeGrid.hpp:45` |
| AO na GPU | SSAO por kernel (nao e ray tracing) | `gpu_renderer.hpp:263, 378` |

Nota: `RtAmbientOcclusion` recebe o indice do pixel para o gerador de amostras (`RtEffects.hpp:70`). O resultado e igual com mais threads (CHANGELOG 159).

## 2. O que falta no RHI

### 2.1 Interface atual

`engine/rhi/include/tool/rhi/rhi.hpp`:
- `IDevice` (`rhi.hpp:169`): cria buffer, textura, shader, pipeline; `beginCommands`, `submit`, `present`. Capacidades: so `supportsCompute()` e `supportsIndirect()` (`rhi.hpp:221-222`). **CONFIRMADA**.
- `ICommandList` (`rhi.hpp:120`): `drawIndexed`, `dispatch` (vazio por padrao, `rhi.hpp:158`), `drawIndexedIndirect` (vazio, `rhi.hpp:162`). **CONFIRMADA**.
- `Backend` (`rhi.hpp:15`): Null, Vulkan, D3D12, WebGPU, GLES, D3D11. **CONFIRMADA**.

### 2.2 O que falta

Nenhum destes conceitos existe no RHI do projeto (busca por `RayTrac|RayQuery|AccelerationStructure|BottomLevel|TopLevel|ShaderBinding` em `engine/` so acha `newtoon_bvh.hpp`, que e fisica). **CONFIRMADA**.

Falta, em ordem:
1. Capacidade: `supportsRayTracing()` e, se necessario, `supportsRayQuery()` em `IDevice`, default `false`, como `supportsCompute`.
2. Acceleration structures: BLAS por malha (geometria triangular, buffers de vertice/indice), TLAS por quadro (instancias com transformacao).
3. Acesso a AS no shader: ou ray query (`RayQuery` em HLSL/GLSL, inline, no pixel shader) ou pipeline de RT (ray gen, hit, miss) com shader binding table.
4. Build e refit de AS, com barreiras e scratch buffer, no `ICommandList`.
5. Novos tipos de buffer/bind para AS no `BufferUsage` (`rhi.hpp:59`) e no descritor de bind.

### 2.3 O que o Diligent ja tem (a camada de baixo)

O DiligentCore instalado tem a API completa; o RHI do projeto nao a expoe.

- Interfaces: `third_party/DiligentCore/Graphics/GraphicsEngine/interface/BottomLevelAS.h`, `TopLevelAS.h`, `ShaderBindingTable.h`. **CONFIRMADA**.
- Build de AS: `BuildBLASAttribs` (`DeviceContext.h:1378`), `BuildTLASAttribs` (`DeviceContext.h:1499`). **CONFIRMADA**.
- Pipeline de RT: `CreateRayTracingPipelineState` (`RenderDevice.h:200`). **CONFIRMADA**.
- Backend Vulkan: `third_party/DiligentCore/Graphics/GraphicsEngineVulkan/src/BottomLevelASVkImpl.cpp`, `TopLevelASVkImpl.cpp`, `include/ShaderBindingTableVk.h`, `RenderDeviceVkImpl.cpp:577`. **CONFIRMADA**.
- Caps: `RayTracingProperties` (`GraphicsTypes.h:2236`), com `MaxInstancesPerTLAS`, `ShaderGroupHandleSize`, etc. (`GraphicsTypes.h:2248-2250`). Flags `RAY_TRACING_CAP_FLAG_INLINE_RAY_TRACING` (ray query) e `STANDALONE_SHADERS` (pipeline RT) em `GraphicsTypes.h:2216-2232`. **CONFIRMADA**.
- Backend D3D12 (DXR) tem testes de BLAS/TLAS/SBT em `third_party/DiligentCore/Tests/IncludeTest/GraphicsEngineD3D12/`; implementacao nao lida aqui. **a confirmar**.
- D3D11 e GLES: sem API de RT. **ESTIMATIVA** (nao foi buscado nos headers dos backends; o projeto assume isso em `docs/estudos/DIRECTX12_DXR.md`).

### 2.4 Ponto bloqueante: RT esta desligado na criacao do dispositivo

- `DeviceFeatures::RayTracing` tem default `DEVICE_FEATURE_STATE_DISABLED` (`GraphicsTypes.h:1662-1663`). **CONFIRMADA**.
- O Vulkan so habilita as extensoes de RT se `EnabledFeatures.RayTracing != DISABLED` (`third_party/DiligentCore/Graphics/GraphicsEngineVulkan/src/EngineFactoryVk.cpp:956, 972-1032`). **CONFIRMADA**.
- A engine cria o dispositivo Vulkan em `engine/rhi_diligent/src/diligent_device.cpp:817-824` e nao define `Features.RayTracing`. Logo, hoje, o Vulkan nao carrega VK_KHR_ray_query nem VK_KHR_acceleration_structure. **CONFIRMADA** (a leitura do default do `EngineVkCreateInfo` nao foi feita; se o default for OPTIONAL, o efeito e o mesmo porque o driver ainda precisa aceitar).
- Consequencia: a F0 pode consultar as capacidades sem mudar o dispositivo. Pedir `OPTIONAL` ou `ENABLED` so comeca na F1, pois muda a criacao para todos os usuarios.
- Mapeamento util: `RAY_TRACING_CAP_FLAG_INLINE_RAY_TRACING` = ray query (`EngineFactoryVk.cpp:221-222`), que e o caminho escolhido para a F1.

## 3. Hardware: RX 6500 XT

- **CONFIRMADA (resultado de busca, paginas nao abertas)**: Navi 24 (RDNA 2), 16 CUs, com um acelerador de raios por CU, portanto 16 aceleradores. Ray tracing via DirectX Raytracing. Fontes: https://en.wikipedia.org/wiki/RDNA_2 ; https://www.tomshardware.com/reviews/amd-radeon-rx-6500-xt-review-xfx ; https://videocardz.com/newz/amd-entry-level-rdna2-radeon-rx-6500xt-to-feature-1024-stream-processors-rx-6400-with-768 .
- Ressalva da mesma fonte: a Tom's Hardware descreveu o RT do cartao como "mostly just a checkbox feature", e a VRAM de 4 GB limita o RT. Isso e opinião de reviewer, mas e um aviso real para o planejamento de custo.
- Extensoes Vulkan necessarias (CONFIRMADA pelo codigo do Diligent, `EngineFactoryVk.cpp:980-1032`): VK_KHR_ray_query e VK_KHR_acceleration_structure (ambas exigidas pelo Diligent para RT), mais VK_KHR_buffer_device_address, VK_KHR_deferred_host_operations e SPIR-V 1.4.
- Driver AMD: Mesa/RADV habilita ray query e build de AS por padrao em RDNA 2 (Phoronix, Mesa 22.2: https://www.phoronix.com/scan.php?page=news_item&px=RADV-Ray-Queries-Default). Navi 24 nao e citado pelo nome. **ESTIMATIVA** para Navi 24 no RADV.
- Driver Windows (Adrenalin): **a confirmar nesta maquina**. Comando sugerido: `vulkaninfo --summary` (ou `vulkaninfo | findstr ray_query`) e checar `VK_KHR_ray_query` e `VK_KHR_acceleration_structure` na lista de extensoes do dispositivo "AMD Radeon RX 6500 XT".

**Veredito de hardware**: a RX 6500 XT tem aceleracao de raios por hardware (confirmado por fonte de review). Se o driver Windows expoe VK_KHR_ray_query, a F1 e viavel nesta placa. Isso precisa ser verificado na maquina do usuario antes da F1.

## 4. Planejamento em fases

### F0: diagnostico honesto (pequena, sem risco de desempenho)

- Entrega: a opcao "Enable ray tracing" mostra o estado real. Textos possiveis: "Sem suporte RT no RHI" (estado atual) ou "RT via ray query disponivel" (quando o dispositivo tem a capacidade). A caixa de RT AO fica desabilitada e explica "RT AO so na CPU" quando a vista esta na GPU.
- Como: `IDevice::supportsRayTracing()` (default false, como `supportsCompute`, `rhi.hpp:221`); implementacao no Diligent lendo `GetDeviceInfo().Features.RayTracing` e `RayTracing.CapFlags`. Ponto de extensao: `rhi_diligent/src/diligent_device.cpp:476` (ja faz isso para compute).
- Aceite: com backend Null/GL/D3D11, a UI mostra "sem suporte". Com Vulkan na RX 6500 XT (apos verificar a extensao), mostra "ray query disponivel" sem alterar o render. Sem mudar nenhum pixel. Teste novo em `apps/editor/selftest.hpp` para o texto do estado.
- Risco: baixo. O texto pode enganar se o driver expuser a extensao mas o build nao a habilitar. Por isso o F0 deve checar tambem `Features.RayTracing` (habilitado no dispositivo), nao so o driver.

### F1: RT AO por ray query na GPU (primeira entrega real)

- Escopo: AO por raio no fragment shader (ray query inline), substituindo o SSAO quando a opcao esta ligada e o dispositivo suporta. Sem pipeline de RT, sem SBT.
- Componentes:
  1. Ligar `Features.RayTracing = OPTIONAL` em `diligent_device.cpp:818-824`, para que a extensao entre so se o driver a tiver.
  2. BLAS por malha: construida uma vez por malha (ao carregar), com `BuildBLASAttribs`. Sem reconstrucao por quadro.
  3. TLAS por quadro: uma instancia por objeto visivel. Reconstrucao completa ou refit quando so as transformacoes mudam.
  4. Shader: `assets/shaders/hlsl/` novo passe de AO, usando `RayQuery` (SM 6.5+ / DXC) ou o equivalente SPIR-V; alimentado por TLAS e pelo mesmo buffer de normais/posicoes que o SSAO usa.
  5. Substituir o SSAO quando `rtAo` e `rayTracing` estao ligados e a capacidade existe; senao, mantem o SSAO. Fallback obrigatorio.
- Custo da TLAS (ESTIMATIVA, a medir):
  - Cada instancia Vulkan tem 64 bytes (struct VkAccelerationStructureInstanceKHR, conhecimento do spec, a conferir no header do SDK). 5.000 instancias = ~320 KB por quadro se for reenviada; 100.000 = ~6,4 MB por quadro. Reenvio por quadro com 100k e caro e deve ser evitado com refit.
  - Build de TLAS e aproximadamente O(N log N) para build completo e O(N) para refit. Nenhuma medicao feita.
  - Limite: `RayTracingProperties::MaxInstancesPerTLAS` (`GraphicsTypes.h:2249`) mostra o teto real do driver; a F1 deve ler esse valor e recusar a cena acima dele com aviso.
  - Para 100k objetos, a engine precisa de culling antes da TLAS (so o que a camera ve e o que entra). A TLAS deve conter so objetos visiveis em um raio de AO.
- Aceite (medido, a definir): (a) com RT AO desligado, o tempo de quadro e o mesmo de hoje (regra: RT desligado nao custa nada); (b) com 5.000 instancias, custo de TLAS por quadro dentro de um orcamento a definir (ex.: X ms na RX 6500 XT, medido no Windows); (c) imagem de AO na GPU bate com a CPU (`RtAmbientOcclusion`) em cena de teste, dentro de tolerancia; (d) `gpu_test vulkan` continua passando.
- Riscos:
  - Ruido: 1 raio por pixel e ruidoso. Precisa de filtro (bilateral ou temporal) e, com TAA/FSR, de reprojecao.
  - Custo de TLAS com 100k: pode nao caber no orcamento da RX 6500 XT (4 GB de VRAM). Medir antes de prometer.
  - Sem RT em GL/D3D11: o fallback SSAO tem que ser o caminho padrao desses backends.
  - Ordem de passes: AO precisa das normais e da profundidade do G-buffer ou do depth pre-pass; hoje o PBR e forward (`pbr.hlsl`). Verificar antes.

### F2: sombras por raio (opcional)

- Escopo: sombra do sol e de pontuais por ray query (1 raio por pixel e luz), para substituir o CSM ou o cubemap de pontual (`shadow_point.hlsl`).
- Componentes: mesma TLAS da F1; filtro de ruido (PCSS-like ou temporal); manter o CSM como fallback.
- Aceite: borda de sombra e contato melhores que o CSM em cena de teste; custo por luz medido; desligado = zero custo.
- Riscos: sombra com 1 raio e muito ruidosa; custo cresce com o numero de luzes; a LightPrism tem `RtShadowConfig` (CPU) que serve de referencia, nao de pronto para GPU.

### F3: reflexos por raio (por ultimo)

- Escopo: reflexo especular de superficies lisas (rugosidade baixa), com ray query ou pipeline RT (o pipeline so entra aqui, porque reflexo com recursao pede ray gen/hit/miss e SBT).
- Aceite: reflexo correto em superficie plana de teste; custo medido; fallback para IBL (`Ibl.hlsli`) quando desligado.
- Riscos: pipeline RT e SBT somam uma camada grande no RHI; e o efeito mais caro. Fazer por ultimo ou nao fazer.

## 5. Relacao com FSR e temporal

- O FSR nao esta no repositorio (`docs/estudos/UPSCALING_FSR_XESS.md:7` confirma que nao ha SDK de FSR/XeSS em `third_party/`). Para usar FSR, antes precisa de SDK ou de implementacao propria. **CONFIRMADA** (documento e busca).
- RT AO e RT sombra por 1 raio por pixel sao ruidosos. Um filtro temporal precisa de **motion vectors** (vetores de movimento) por pixel. O renderer atual nao os expoe ao passe de AO (busca nao feita em detalhe; ESTIMATIVA: precisam ser criados).
- Upscaling e denoise juntos exigem reprojecao consistente. Se o FSR entrar antes do RT AO, o denoise deve rodar antes do upscale (na resolucao nativa).

## 6. Recomendacao

1. Fazer F0 agora: e barato, honesto e corrige a principal confusao (a caixa marcada que nao faz nada na GPU). Adicionar `supportsRayTracing()` no `IDevice`.
2. Antes de F1, verificar na maquina do usuario: `vulkaninfo` com `VK_KHR_ray_query` e `VK_KHR_acceleration_structure` para a RX 6500 XT.
3. F1 com ray query, RT AO apenas, com fallback SSAO. Medir TLAS com 5k e 100k instancias antes de definir o limite de cena.
4. F2 e F3 so depois de F1 medida. F3 pode nao ser feita.

## 7. Pontos em aberto

- Verificar em qual backend o editor abre por padrao (`apps/editor/settings_panel.hpp` / `Settings > Graphics`), porque isso define se o usuario ve o RT AO.
- Confirmar o default do `EngineVkCreateInfo::Features` lendo `EngineFactoryVk.h` (nao lido aqui).
- Verificar `Features.RayTracing` no D3D12 (DXR) em `third_party/DiligentCore/Graphics/GraphicsEngineD3D12/`.
- Medir custo real de TLAS e de RT AO na GPU (nenhum numero deste arquivo e medido na GPU).
