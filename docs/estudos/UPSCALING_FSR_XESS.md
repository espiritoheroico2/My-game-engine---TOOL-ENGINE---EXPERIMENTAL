# Upscalers AMD FSR e Intel XeSS: peso, compatibilidade e recomendacao

*Pesquisa em 2026-11-27. Legenda: **[web]** = confirmado em fonte da web (links ao final); **[repo]** = verificado no repositorio local; **[estimativa]** = palpite de engenharia, nao medido; **nao confirmado** = nao foi possivel achar numero confiavel.*

## 1. Estado atual no repositorio [repo]

- **Nenhum SDK de FSR, FidelityFX ou XeSS esta presente** em `third_party/` (DiligentCore 418 MB, SDL3 30 MB, SpriteUi 18 MB, LightPrism 2,3 MB, miniaudio 4,1 MB, quickjs-ng 4,3 MB, stb 288 KB) nem no restante do repo. Glob e Grep por `fsr`, `fidelity`, `xess`, `ffx` so acharam texto em documentos (`docs/estudos/AMD_FSR.md`, `docs/planos/...`) e ocorrencias falsas (`sdl_x11xfixes`, `Bvh.cpp`).
- **O "upscale" atual e 100% por software (CPU)**: `engine/render/include/tool/render/upscale.hpp` (Bilinear, Bicubic Catmull-Rom, Lanczos2) mais FXAA simplificado. Aparece em Settings → Graphics como "Upscaling (software)" (Off, Bilinear, Bicubic, Lanczos2), salvo como `upscaleMode` em `editor_settings.json`. Integrado em `apps/editor/editor_state.hpp` (`softScaling`, `finishView`) e `apps/editor/main.cpp`.
- Por isso, o pedido de "upscale" ja existe, mas nao ha nenhum upscaler por GPU/temporal.
- **Conflito com o estudo anterior**: `docs/estudos/AMD_FSR.md` diz que o FSR 4 "nao usar (risco legal e tecnico)" porque o codigo "foi publicado por engano". Isso **nao bate** com as fontes desta pesquisa (FSR 4 publicado oficialmente para desenvolvedores no SDK 2.0 e com versoes posteriores). A conclusao pratica para a RX 6500 XT nao muda, mas esse trecho do estudo antigo deve ser revisado. Nao editei o arquivo.

## 2. O que a web confirma [web]

| Tema | Achado | Fontes |
|---|---|---|
| SDK atual | FidelityFX SDK **2.0** e a linha principal; traz FSR 4 (ML) para desenvolvedores. A linha **1.x** (com FSR 3.1) esta em manutencao e as funcoes SDK 1 foram marcadas como depreciadas. Patch mais recente da linha 1.x citado: 1.1.4. Numero exato da versao mais nova do SDK 2.x **nao confirmado**. | [gpuopen SDK 2.0](https://gpuopen.com/learn/amd-fidelityfx-sdk-2-0/), [Phoronix SDK 1.1](https://www.phoronix.com/news/AMD-FidelityFX-SDK-1.1) |
| FSR Upscaling (pagina atual) | Integra FSR 2, 3 e 4; quando necessario, cai para FSR 3.1.x. Pagina cita Vulkan 1.2 com paridade de recursos com DX12. Versao de download citada: 4.1.2 (com FSR 2.3.4 e 3.1.6 dentro). | [gpuopen FSR Upscaling](https://gpuopen.com/fidelityfx-superresolution-4/) |
| FSR 1 (EASU + RCAS) | Upscaler espacial, so cor, sem motion vectors. Ja documentado no estudo anterior. Pacote de shaders; **nao confirmei a versao atual do SDK onde ele vive**. | [repo] estudo `AMD_FSR.md` |
| FSR 2.3.x | Exige cor, profundidade (D32S8 aceito em Vulkan), motion vectors (2D, em pixels de render), jitter de camera (motion vectors sem jitter, salvo flag). DX12 e Vulkan. | [gpuopen FSR 2.3.4](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-temporal/) |
| FSR 3.1.x (upscaler) | Mesmas entradas do FSR 2; recomenda profundidade invertida e infinita (suporta outras via flags). DX12 e Vulkan. Frame generation e um modulo a parte. | [gpuopen FSR 3.1.4](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/) |
| FSR 4 (ML) | **So RDNA 4** (RX 9070 / 9070 XT no lancamento; depois, drop-in em 85 jogos via driver, ainda so RDNA 4). Motivo citado: instrucoes FP8 WMMA exclusivas do RDNA 4. Nao ha compromisso publico com RDNA 3 ou RDNA 2. Ha relatos de "DLL swap" para jogos com FSR 3.1, mas so em RDNA 4. Uma manchete sugere suporte "em todas as GPUs" (pcgameshardware); **nao confirmado** e contradiz as fontes oficiais citadas. | [Tom's Hardware FSR 4 85 jogos](https://www.tomshardware.com/pc-components/gpu-drivers/amd-expands-fsr-4-with-drop-in-support-for-85-games-with-latest-radeon-driver-update-but-you-still-need-an-rdna-4-gpu), [Notebookcheck FSR 4 RDNA 4](https://www.notebookcheck.net/AMD-talks-FSR-4-Hypr-RX-and-new-Adrenalin-software-FSR-4-uses-proprietary-model-and-is-limited-to-RDNA-4-cards-for-now.969986.0.html), [Tom's Hardware DLL swap](https://www.tomshardware.com/pc-components/gpus/you-can-upgrade-fsr-3-1-games-to-fsr-4-with-manual-dll-swapping-github-community-discovers-fsr-swapping-works-similar-to-dlss-upgrades) |
| XeSS 2 requisitos | Developer guide: DX12 com Intel Iris Xe ou superior; outros fornecedores com **SM 6.4 + DP4a por hardware**; DX11 exige Arc. XMX e o caminho da Intel (Arc). Frame generation (XeSS-FG) originalmente exigia XMX; depois a Intel removeu esse requisito e passou a aceita-lo em GPUs DP4a. Conflitos entre fontes sobre o DP4a no FG. | [Intel XeSS-SR Developer Guide](https://www.intel.com/content/www/us/en/developer/articles/technical/xess-sr-developer-guide.html), [GitHub intel/xess](https://github.com/intel/xess) |
| XeSS 2 licenca | Reportado: a licenca **nao concede direitos sobre a propriedade intelectual** da Intel e **proibe modificar o codigo**; codigo-fonte nao e liberado (so binarios/SDK). Texto da licenca **nao lido** — conferir antes de redistribuir DLL. | [Tom's Hardware XeSS 2.0 SDK](https://tomshardware.com/pc-components/gpus/intel-finally-releases-the-xess-2-0-sdk-for-developers-technology-still-gated-by-closed-source-barriers) |
| XeSS SDK 2.1 | Liberou o frame generation para GPUs AMD e NVIDIA compativeis. | [Tom's Hardware XeSS SDK 2.1](https://www.tomshardware.com/pc-components/gpus/xess-sdk-2-1-release-opens-up-intels-framegen-tech-to-compatible-amd-and-nvidia-gpus-xe-low-latency-also-goes-cross-platform-if-framegen-is-enabled) |

## 3. Peso (tamanho) [estimativa, exceto onde marcado]

| Upscaler | Peso no projeto | Base |
|---|---|---|
| FSR 1 | **Muito baixo**: codigo de shader (EASU + RCAS) portado para o RHI; sem DLL. Dezenas de KB. | [estimativa] |
| FSR 2.3.x / 3.1.x | **Baixo a medio**: biblioteca de shaders compilados + codigo C/C++ do SDK; poucos MB. | [estimativa] — **nao confirmado** |
| FSR 4 | **Alto**: driver traz `amdxcffx64.dll` de **51,5 MB** (driver do Radeon, nao o upscaler por jogo). Tamanho do upscaler por jogo **nao confirmado** (relatos de arquivos de 13,8 MB e 16,8 MB em listagem vazada, sem certeza de qual arquivo). | [web] driver 51,5 MB; resto [nao confirmado] |
| XeSS 2 | **Medio/alto**: `libxess` DX12/DX11, `libxell.dll`, `libxess_fg.dll` (DLLs binarias, sem fonte). Tamanhos **nao confirmados**. | [web] nomes dos arquivos; tamanhos [nao confirmado] |

Para medir de verdade: baixar o SDK oficial e somar os binarios de `redist/` (nao feito nesta pesquisa, por ser download de terceiro).

## 4. Compatibilidade com a GPU do usuario (RX 6500 XT, RDNA 2, Vulkan)

| Upscaler | Funciona? | Motivo |
|---|---|---|
| FSR 4 | **Sim, por suporte oficial da AMD (versao FSR 4.1, modelo INT8), segundo reportagens de out/2026** (driver Adrenalin 26.10.1 estende a RDNA 2 e as RX 6000). A RX 6500 XT pertence a linha RX 6000, mas a lista exata do driver **nao foi confirmada** para esse modelo: conferir as notas de versao da AMD antes de contar com ele. Ver [Tom's Hardware](https://www.tomshardware.com/pc-components/gpu-drivers/amd-makes-fsr-4-upscaling-official-for-radeon-rx-7000-and-6000-series-cards-rdna-3-and-rdna-2-chips-will-soon-enjoy-improved-visuals) e [TechSpot](https://techspot.com/news/114277-amd-brings-fsr-41-upscaling-radeon-6000-gpus.html). |
| FSR 3.1 (upscaler) | **Sim** | DX12 e Vulkan, sem exigencia de hardware ML [web]. Precisa de depth + motion vectors + jitter. |
| FSR 2.3.x | **Sim** | Idem [web]. |
| FSR 1 | **Sim** | Shader puro, roda em qualquer GPU [estimativa; coerente com a documentacao de FSR 1]. |
| XeSS 2 | **Incerto** | Em GPU nao-Intel, o caminho e DP4a com SM 6.4 [web]. Nao confirmei se a RX 6500 XT (RDNA 2) atende a esse caminho nem se o caminho Vulkan esta disponivel no SDK. **nao confirmado**. |

## 5. Esforco de integracao no RHI [estimativa]

| Upscaler | Entradas necessarias | Esforco |
|---|---|---|
| FSR 1 | Cor (HDR linear ou sRGB), tamanho de render e de saida; sem motion vectors | **Baixo**: 2 passes de compute, sem dependencia de profundidade. Depende de apresentar direto na GPU (readback da CPU e o gargalo, ver `ESCALONAMENTO_RENDER.md`). |
| FSR 2/3.1 | Cor, **profundidade**, **motion vectors**, **jitter de camera** (projecao com subpixel), reset em cortes de camera, exposicao | **Medio/alto**: exige TAA-like, buffers de vetores de movimento no renderer e integracao do jitter. |
| XeSS 2 | Mesmas entradas do FSR 2 (cor, profundidade, motion vectors, jitter, exposicao) | **Medio/alto**, mais o problema de licenca (binario fechado) e de cobertura de GPU. |
| FSR 4 | Mesmas entradas do FSR 2 | **Alto** (driver de 51,5 MB e modelo ML). Agora tem suporte oficial em RDNA 2 (FSR 4.1, INT8), o que o torna candidato real para a RX 6500 XT, depois do FSR 1 e do FSR 2/3.1. |

## 6. Recomendacao

1. **Implementar FSR 1 (EASU + RCAS) primeiro.** Menor peso (sem binario), funciona na RX 6500 XT, nao exige motion vectors, e encaixa na fase 1 do `AMD_FSR.md` (apresentar direto na GPU, depois passe de pos no RHI).
2. **Se o motor ganhar TAA/jitter e vetores de movimento, implementar FSR 3.1 (ou FSR Upscaling, que cobre 2.3.x e 3.1.x)** como segundo passo, pois e um unico SDK de AMD, roda em RDNA 2 e tem o maior ganho de qualidade para a GPU do usuario.
3. **FSR 4 (4.1, INT8)**: candidato depois do FSR 2/3.1, se a AMD confirmar a RX 6500 XT na lista oficial do driver. Peso alto (ver secao 3) e exige motion vectors, como os outros.
4. **XeSS 2 por ultimo ou fora do escopo**: binario fechado com restricao de modificacao, cobertura incerta em GPU AMD, e custo de integracao igual ao FSR 2/3.

Resposta a regra "se for muito pesado, implementar so um": **sim, implementar so um agora (FSR 1)**. Os tres outros (FSR 2/3.1 como segundo passo; FSR 4 e XeSS fora) nao precisam entrar nesta etapa.

## 7. Duvidas em aberto

- Qual e a versao atual do FidelityFX SDK 2.x e o tamanho real de `redist/` (nao confirmado).
- A licenca do FidelityFX (MIT) permite redistribuir os binarios e o codigo do FSR 1 e 2/3.1? (nao lido nesta pesquisa; conferir o arquivo LICENSE do SDK).
- Texto completo da licenca do XeSS 2 (permite redistribuir DLL no instalador do editor?).
- XeSS roda em Vulkan em GPU AMD RDNA 2 (caminho DP4a)?
- O backend de render da engine (Diligent D3D12 ou Vulkan) ja permite apresentar direto na GPU sem readback? Isso decide se FSR 1 entra como passe de tela ou como passe de pos-processo offscreen.
- O estudo `AMD_FSR.md` ainda descreve FSR 4 como "publicado por engano": revisar com estas fontes.

## Fontes [web]

- [AMD FidelityFX SDK 2.0 (GPUOpen)](https://gpuopen.com/learn/amd-fidelityfx-sdk-2-0/)
- [AMD FidelityFX SDK 1.1 (Phoronix)](https://www.phoronix.com/news/AMD-FidelityFX-SDK-1.1)
- [AMD FSR Upscaling (GPUOpen)](https://gpuopen.com/fidelityfx-superresolution-4/)
- [FSR 4 manual (GPUOpen)](https://gpuopen.com/manuals/fidelityfx_sdk2/techniques/super-resolution-ml/)
- [FSR 2.3.4 manual (GPUOpen)](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-temporal/)
- [FSR 3.1.4 upscaler manual (GPUOpen)](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/)
- [FSR 4 limitado a RDNA 4 (Notebookcheck)](https://www.notebookcheck.net/AMD-talks-FSR-4-Hypr-RX-and-new-Adrenalin-software-FSR-4-uses-proprietary-model-and-is-limited-to-RDNA-4-cards-for-now.969986.0.html)
- [FSR 4 em 85 jogos, ainda so RDNA 4 (Tom's Hardware)](https://www.tomshardware.com/pc-components/gpu-drivers/amd-expands-fsr-4-with-drop-in-support-for-85-games-with-latest-radeon-driver-update-but-you-still-need-an-rdna-4-gpu)
- [Troca de DLL FSR 3.1 para FSR 4 (Tom's Hardware)](https://www.tomshardware.com/pc-components/gpus/you-can-upgrade-fsr-3-1-games-to-fsr-4-with-manual-dll-swapping-github-community-discovers-fsr-swapping-works-similar-to-dlss-upgrades)
- [Intel XeSS-SR Developer Guide 2.0](https://www.intel.com/content/www/us/en/developer/articles/technical/xess-sr-developer-guide.html)
- [intel/xess (GitHub)](https://github.com/intel/xess)
- [XeSS 2.0 SDK e licenca (Tom's Hardware)](https://tomshardware.com/pc-components/gpus/intel-finally-releases-the-xess-2-0-sdk-for-developers-technology-still-gated-by-closed-source-barriers)
- [XeSS SDK 2.1 (Tom's Hardware)](https://www.tomshardware.com/pc-components/gpus/xess-sdk-2-1-release-opens-up-intels-framegen-tech-to-compatible-amd-and-nvidia-gpus-xe-low-latency-also-goes-cross-platform-if-framegen-is-enabled)
