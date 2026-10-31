# NVIDIA RTX Neural Texture Compression (NTC) — viabilidade

**Fonte:** github.com/NVIDIA-RTX/RTXNTC (SDK v0.10.0 **beta** na data da consulta).

## O que é
Comprime todas as texturas PBR de um material **juntas** (9–10 canais) em um "bundle": pesos de uma MLP pequena + dados latentes.
Três modos: **descomprimir ao carregar**, **transcodificar para BCn**, ou **inferência na amostragem** (no shader). Meta: ~5 bits/texel com qualidade próxima de BCn.

## Requisitos
- API: **DirectX 12** ou **Vulkan 1.3**; Windows 10/11 x64, Windows 11 ARM64, Linux x64.
- Cooperative Vector (mais rápido): driver NVIDIA recente (Vulkan ≥ 570). O caminho DX12 com LinAlg é só para testes (não distribuir).
- Sem CoopVec: caminho DP4a funciona em qualquer GPU SM6, porém lento na inferência por amostra.
- Compressão exige GPU Turing+ (melhor Ada+). Descompressão ao carregar: qualquer SM6.
- Licença: "NVIDIA RTX SDKs LICENSE" — **ler o texto antes de redistribuir** (ela não é MIT).

## Veredito
**Viável, mas não agora.** Motivos: (1) a engine ainda **não tem texturas nos materiais** (só cor/rugosidade/metálico) — NTC comprime texturas PBR; (2) o renderizador de editor lê pixels de volta para a CPU, então o ganho de VRAM não aparece; (3) é SDK beta, só NVIDIA acelera de verdade; (4) precisa de DX12/Vulkan nativo no `rhi`, e hoje o caminho Vulkan passa pelo Diligent.

## Plano
1. Materiais com texturas (albedo/normal/ORM) + carregar PNG/JPG — `NEXT_STEPS.md`.
2. Ferramenta **offline** no editor: botão **"Compress as NTC"** no material → roda `ntc-cli` (fornecido pelo usuário/baixado do SDK, executado direto, sem shell) com um manifesto gerado → grava `Assets/<nome>.ntc` → o material passa a apontar para o `.ntc` e guarda a textura original como fallback.
3. Runtime: **descompressão ao carregar → BCn** (modo mais compatível e rápido), com fallback para a textura original em GPUs sem suporte e no renderizador de CPU.
4. Só depois, inferência na amostragem (GPUs Ada+).
O botão só será ligado quando os passos 1 e 3 existirem; hoje ele não faria nada útil.
