# Desempenho de render: pós-processamento e sombras (v0.6)

## Pós-processamento (global/local)
Medido numa máquina de **2 núcleos** (CPU, imagem aleatória, todos os efeitos ligados): 720p 8,8 → ~6 ms (sem nitidez) e 17 → ~10,5 ms (com nitidez); 1080p 15 → ~10,7 ms e 39 → ~25 ms.
O que mudou: ponto fixo (inteiros) em uma única passada (LUT → matriz 3×3 saturação·tint → tabela de vinheta sem divisão/sqrt), **pool de threads persistente**
(antes criava e juntava 8 threads por quadro) e nitidez com 3 linhas de buffer por faixa (antes copiava a imagem inteira).
**Limite real:** pós-fx em CPU custa por pixel e depende do número de núcleos. A correção definitiva é fazê-lo **na GPU** (um passe de tela cheia: ~0,1 ms), o que exige apresentar o quadro direto na GPU (`NEXT_STEPS.md`, itens 3 e 6).
Dicas hoje: use *Render scale* < 100% (o pós-fx roda na resolução renderizada) e deixe a nitidez em 0 quando não precisar.

## Sombras — "sleeper"
O editor calcula uma assinatura (matriz da luz + resolução + mesh/matriz de mundo de cada objeto que projeta sombra). Se for igual à do quadro anterior **daquele renderer**,
o shadow map é reaproveitado: nenhum triângulo de sombra é rasterizado (CPU) e o passe de sombra é pulado (GPU). Aparece no Profiler como *shadow map reused*.
Cena parada = custo de sombra ≈ 0; mover qualquer objeto que projeta sombra (ou o sol) reconstrói.

## Sombras de point light / spot — projeto (não implementado)
- Cubemap de profundidade (6 faces) por luz marcada com `castShadow`, no máximo N=2–4 luzes com sombra por quadro (priorizando as mais próximas da câmera).
- O mesmo **sleeper por luz**: assinatura = posição/alcance da luz + casters dentro do alcance; luz estática + casters parados = cubemap reaproveitado (6 passes → 0).
- Mudanças: `shadow.hlsl/.glsl` (passe omni), `pbr.hlsl/.glsl` (amostragem por direção), `LightCB` (índice da sombra por luz), SoftRenderer (6 mapas) e testes.
- Alternativa com melhor qualidade: sombras por **ray tracing (DXR)** quando a GPU suportar (`DX12_DXR.md`).
