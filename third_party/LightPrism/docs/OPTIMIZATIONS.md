# Fase 5a — Melhorias e otimizações (10 itens) + redução de memória

Todas as medições vêm do renderizador por software (`lp_opt_tests`, `lp_rt_tests`): cena demo, sem GPU. Onde o ganho depende da GPU
(ex.: tempo de shader) o texto diz o que foi medido de verdade (contagem de trabalho, bytes, erro) e o que não foi.

| # | Melhoria | Onde | Resultado medido |
|---|---|---|---|
| 1 | **Raio automático de luz**: `cullRadius` = raio onde a contribuição cai abaixo de `autoRadiusCutoff` (`PointLightGPU::cullRadius`, 0 = usa `radius`) | `LightingSystem`, `Cluster.hlsli` | −21 % de avaliações de luz (luzes com raio exagerado de 30 m), erro HDR máx. 0,02 |
| 2 | **Clusters ativos**: só culla os clusters que algum pixel usa (`MarkActiveClusters`) | `ClusterCulling` | 6,2 % dos clusters ativos, cull 1,2 → 0,5 ms (CPU), imagem idêntica, some o overflow com 320 luzes |
| 3 | **Orçamento adaptativo de índices** do cluster (cresce no overflow, encolhe após 30 quadros calmos) | `ClusterSystem` | 949 KiB fixos → 345 KiB (encolhe) / 837 KiB quando precisa; sem overflow |
| 4 | **Cache de cascatas do CSM** + snapping também em Z (matriz estável) | `Shadow/CascadeCache.hpp`, `Csm.cpp` | câmera lenta (3 mm/quadro): 0,56 cascata re-renderizada por quadro; rápida (25 mm): 2,23. Casters dinâmicos: cascata i a cada 2^i quadros |
| 5 | **SSAO em meia resolução** com upsample consciente de profundidade | `Post/Ssao` | 2,6× mais rápido; erro 0,096 vs 0,087 nos pixels ocluídos |
| 6 | **Formatos compactos do IBL** (RGB9E5/R11G11B10F no cubemap; LUT em RG8) | `Ibl` | 399 → 132 KiB (−67 %), erro do IBL ≤ 0,6 % |
| 7 | **SH L1** (4 coeficientes no mobile; flag `IblFlag_ShL1`) | `Ibl` | erro médio 6,95 %, máx. 25,6 % (contra SH9) — só para tier mobile |
| 8 | **Sombra planar de Blinn** (projeção achatada para chão simples) | `Shadow/PlanarShadow.hpp` | teste exato (ponto projetado cai no plano; luz direcional e pontual) |
| 9 | **Presets + estimador de memória** (`QualitySettings` novos campos, `EstimateMemory`) | `Quality.hpp`, `MemoryBudget.hpp` | 1080p: High 270 MiB, Medium 58, Low 11,4, Mobile 7,9; otimizações no Medium: 61 → 34 MiB (−44 %) |
| 10 | **Ray tracing com limite de distância** (BVH SAH/LBVH, sombra e AO RT) | `Rt/` — veja `RAYTRACING.md` | AO RT = verdade analítica (erro 0,0005); limite corta ~70 % dos nós |
| + | **RT com cache (PRT)**: céu sem raios, re-bake regional | `Rt/Transfer.hpp` | 0 raios por troca de céu; re-bake incremental 8× menor e exato |

## Memória: onde foi reduzida
- IBL −67 % (item 6); índices de cluster −64 % no estado calmo (item 3); `shadowDepth16`/`ssaoHalfRes`/`iblCubeSize` por tier (item 9).
- `MemoryEstimate` soma cada recurso (shadow maps, cubemaps, clusters, G-buffer de AO, BVH) para o preset escolhido: dá para mostrar o orçamento na UI da engine.

## Honestidade
- Os shaders HLSL (inclusive `shaders/Rt/*`) **nunca foram compilados** (sem `dxc` aqui) e o backend Diligent nunca rodou em GPU.
- O passe HLSL de marcação de clusters ativos (item 2) **não foi escrito**: a CPU é a referência e o `ClusterSystem` em GPU ainda não o usa.
- O ganho do item 4 é grande para câmera lenta/estática e cascatas distantes; com câmera rápida a economia é pequena.
- O item 7 tem erro alto de propósito (troca qualidade por ALU no mobile); não use nos outros tiers.

## Validação por mutação
22 mutações injetadas à mão nos módulos novos (BVH, PRT, efeitos RT, raio automático, clusters ativos, orçamento adaptativo, cache de cascatas):
**20 pegas pelos testes**. As 7 que sobreviveram na primeira rodada revelaram lacunas de teste; 5 foram corrigidas (orçamento do bake, sombra suave invertida,
profundidade 0 nos clusters ativos, salto de câmera e `maxStride` no cache) e agora morrem. Duas ficam como **sobreviventes aceitos**: a folga de 25 % ao crescer e o
limiar de 1/2 ao encolher o orçamento adaptativo — são constantes heurísticas de ajuste, não contratos. O teste de mutação também achou um bug real do BVH SAH
(`Aabb::Grow` com caixa vazia contaminava o custo e deixava o SAH pior que o LBVH).
