# Análise: AMD FSR no ToolEngine

*Pesquisa em 2026-10-28. Fontes ao final. Os números de versão e licenças abaixo vêm dessas fontes; confirme no repositório da AMD antes de distribuir.*

## O que mudou
- **FSR 3.1** (jul/2024) trouxe suporte a **Vulkan** (a 3.0 era focada em Direct3D 12), com melhor estabilidade temporal e uso junto de outros upscalers.
- **FSR 4** é baseado em ML e exige GPUs Radeon RX 9000; o código chegou a ser publicado por engano e a AMD o chamou de erro → **não usar** (risco legal e técnico).
- O **FSR 3.1.x** (upscaling) roda em GPUs sem hardware ML; o fabricante ainda permite usar a variante de upscaling em RX 7000 e anteriores.

## Faz sentido para esta engine?
Hoje o editor renderiza (GPU ou CPU) e **devolve a imagem para a CPU** (readback) para mostrá-la como textura SDL. O custo dominante do caminho GPU é esse readback, proporcional ao tamanho FINAL da imagem — então fazer upscale na GPU antes do readback **não economiza** o custo principal. Por isso:

| Fase | O que | Pré-requisito | Esforço |
|---|---|---|---|
| 0 (já existe) | *Render scale* (0.25–2.0) com filtro linear/nearest | — | feito |
| 1 | **FSR 1 (EASU + RCAS)**, upscale espacial: só entra com cor, sem vetores de movimento | apresentar direto na GPU (NEXT_STEPS #3) + passe de pós na RHI | baixo (2 passes de compute/pixel shader, porte do HLSL do SDK) |
| 2 | **FSR 3.1 upscaler temporal** via FidelityFX SDK (backend Vulkan) | profundidade + vetores de movimento + jitter de câmera; HDR linear | médio/alto |
| 3 | Frame generation | FSR 3.1 + swapchain própria | não planejado |

**Recomendação:** não implementar agora. Primeiro apresentar direto na GPU (#3). Depois a fase 1 (qualidade boa a 0.7× de escala, quase sem custo). A fase 2 só quando houver TAA/jitter e vetores de movimento.
Integração via `PostPass` na RHI para não prender a engine a um fornecedor (mesma interface serviria para um upscaler próprio ou DLSS/XeSS no futuro).
No SoftRenderer (CPU) FSR não se aplica; o equivalente é o *render scale* + `postfx` de nitidez.

## Fontes
- [AMD FSR 3.1 Announced With Vulkan Support — Phoronix](https://www.phoronix.com/news/AMD-FSR-3.1-Announced)
- [AMD releases FidelityFX SDK 1.1.0 with FSR 3.1 and Vulkan support — GamingOnLinux](https://www.gamingonlinux.com/2024/07/amd-releases-fidelityfx-sdk-110-with-fsr-31-and-vulkan-support/)
- [FidelityFX SDK 2.0 / FSR 4 (relato) — TechEnclave](https://techenclave.com/t/amd-releases-fidelityfx-sdk-2-0-fsr4-briefly-goes-open-source-allegedly-for-rx-7000-series-gpus/393963)
