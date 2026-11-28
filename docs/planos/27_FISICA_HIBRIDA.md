# 27 - Fisica hibrida: GPU integra e gera pares, CPU faz narrowphase e solver

Pergunta: a GPU pode integrar o movimento e gerar os pares de contato, deixando para a CPU so a colisao e o solver? Quanto custa a transferencia, e o tempo total melhora?
Experimento isolado: `tests/hybrid_physics_bench.cpp`, alvo `tool_hybrid_physics_bench` (sem add_test; `out/bin/Release/tool_hybrid_physics_bench.exe vulkan`). NAO toca em engine/physics, apps/editor nem engine/rhi. Base: docs/planos/26 (prototipo GPU) e 19 (Newtoon).

## Metodologia

Mesma cena do plano 26: esferas de raio 0,5 em pilha (10.000 = 25x25x16; 100.000 = 50x50x40), caindo na caixa com chao em y=0, 300 passos de 1/60 s, 4 iteracoes Jacobi (relax 0,5, correcao max 0,25 D, ponto fixo 65536), RX 6500 XT, Vulkan. 3 execucoes por variante; mediana e p95 calculados sobre os 900 passos (3x300).

- A) CPU pura (1 thread): integrar, grade por ordenacao por contagem, lista de pares (i<j, d<1,1 D), narrowphase + solver, velocidade PBD.
- B) GPU pura: 11 dispatches do plano 26, esperando a GPU a cada passo (readBuffer de 16 B, ~0,10 ms de sincronizacao ja incluida).
- C) Hibrida, por passo:
  1. GPU: velocidade (do passo anterior), integracao, grade, limpa contador, kernel de pares (InterlockedAdd num buffer uint2, capacidade 8 por esfera). Tempo "GPU" = gravar + submeter + readBuffer do contador (4 B, que espera a GPU).
  2. GPU->CPU: `readBuffer` sincrono das posicoes (16 B/esfera) e do buffer de pares. O RHI so tem readBuffer do buffer inteiro, entao copia a capacidade toda (80k ou 800k pares x 8 B), nao so os pares usados. Bytes "copiados" e "uteis" reportados separadamente.
  3. CPU: narrowphase (esfera-esfera sobre a lista de pares; acumulo em ponto fixo inteiro, entao a ordem nao deterministica dos pares da GPU nao muda o resultado) e solver (aplica correcao, limite, clamp de caixa; o chao e tratado como clamp y>=R em cada iteracao). Tempos separados.
  4. CPU->GPU: `updateBuffer` das posicoes corrigidas + espera da copia (menos a sincronizacao fixa de ~0,1 ms).
  O estado de velocidade e prev fica na GPU; so as posicoes vao e voltam.
- Validacao em todas as variantes e execucoes: (a) y min >= R - 1e-3; (b) sem NaN/inf; (c) hash FNV das posicoes finais identico nas 3 execucoes. Todas passaram, e o buffer de pares nunca estourou.

## Resultados (ms por passo, mediana / p95)

| Variante | 10.000 | 100.000 |
|---|---|---|
| A CPU pura (total) | 9,41 / 11,03 | 109,18 / 175,84 |
| B GPU pura (total) | 0,40 / 0,79 | 2,52 / 2,89 |
| C Hibrida (total) | 6,96 / 38,00 | 91,52 / 128,96 |

Decomposicao de A (mediana): 10k = integ+grade+pares 4,44 + narrowphase 4,37 + solver 0,32; 100k = 49,78 + 53,48 + 3,23. Pares: ~41,6k (10k), ~472k (100k).

Decomposicao de C (mediana / p95):

| Fase | 10.000 | 100.000 |
|---|---|---|
| GPU (vel+integra+grade+pares) | 0,76 / 0,86 | 4,40 / 5,78 |
| GPU->CPU (tempo) | 0,59 / 0,67 | 4,42 / 5,68 |
| GPU->CPU (bytes copiados / uteis) | 0,80 MB / 0,49 MB | 8,00 MB / 5,39 MB |
| CPU narrowphase | 4,78 / 5,82 | 74,49 / 91,22 |
| CPU solver | 0,32 / 0,36 | 3,23 / 4,18 |
| CPU->GPU (tempo) | 0,30 / 29,96 | 1,07 / 32,38 |
| CPU->GPU (bytes) | 0,16 MB | 1,60 MB |

Taxa observada: ~1,6 GB/s no readback (8 MB em 4,4 ms; inclui memcpy do vetor) e ~1,5 GB/s no upload a 100k.

Qualidade (execucao 1, estado final): chao respeitado em todos (y min = 0,5000), sem NaN. Sobreposicao: 10k media 0,8% do diametro nas tres; 100k media 2,6% nas tres, max 25,6% (A), 23,7% (B), 21,8% (C), ~2,5% das esferas com algum par >10%. A hibrida e a GPU pura chegam a estados parecidos mas NAO identicos (hash diferente: pares com corte e ordem de solver distintos).

## Conclusao quantitativa

1. A hibrida e mais rapida que a CPU pura? Pouco, e so na mediana: 10k 6,96 vs 9,41 ms (1,35x); 100k 91,5 vs 109,2 ms (1,19x). No p95 de 10k ela PERDE para a CPU pura (38,0 vs 11,0 ms) por picos de upload (~30 ms, ocasionais, provavelmente a copia de staging do updateBuffer esperando a fila da GPU); a 100k o p95 da hibrida (129 ms) ainda e menor que o da CPU (176 ms), mas ambos sao ruins. Nenhum tamanho testado deixa a hibrida perto de 60 fps com folga (16,7 ms); a 100k nem A nem C servem.
2. Gargalo da hibrida: a CPU, nao a transferencia. A 100k: narrowphase 74,5 ms de 91,5 ms (81%) + solver 3,2 ms; transferencia total (down+up) = 5,5 ms (6%); GPU 4,4 ms (5%). A 10k: narrowphase 4,8 de 7,0 ms (69%); transferencia 0,9 ms (13%); GPU 0,8 ms. A narrowphase na hibrida e ate mais lenta que na CPU pura (74,5 vs 53,5 ms a 100k) porque os pares chegam desordenados (InterlockedAdd): acesso aleatorio a posicoes e ao vetor de acumulo, sem localidade de cache. A CPU pura ja produz os pares ordenados por esfera.
3. O que a GPU poupa: integracao + grade + pares custa 49,8 ms na CPU (100k) e 4,4 ms na GPU (so ~10x, e isso INCLUI a sincronizacao). O ganho (~45 ms) e quase todo gasto na narrowphase pior e nas transferencias.
4. Transferencia: 8 MB de readback + 1,6 MB de upload a 100k = 5,5 ms. 39% dos bytes lidos sao capacidade nao usada (o RHI nao tem leitura parcial); com `readBuffer` por faixa ou compactacao a CPU lendo so os pares usados, cairia para ~3,5 ms. Mesmo zerando a transferencia, o total seria ~86 ms.
5. Readback assincrono de 1 a 2 quadros (PROJECAO analitica, nao medida: o RHI so tem readBuffer sincrono): se a CPU processar o quadro k enquanto a GPU calcula k+1, o total vira ~max(GPU+up, down+CPU) = 5,7 ms (10k) e 82,1 ms (100k) com 1 quadro; ~max(GPU, down+up, CPU) = 5,1 ms (10k) e 77,7 ms (100k) com 2 quadros. Ou seja, 100k so cairia de 91,5 para 78-82 ms: o limite e a CPU. Alem disso exigiria aplicar correcoes atrasadas na GPU (a GPU andaria 1-2 quadros sem as correcoes da CPU), com impacto de qualidade nao medido.
6. A alternativa que o proprio experimento mostra: a GPU pura (B) faz 100k em 2,5 ms, 36x mais rapido que a hibrida e 43x mais rapido que a CPU pura, nesta cena. Para esferas simples, dividir o trabalho com a CPU nao compensa; compensaria somente se a narrowphase da CPU for muito mais cara do que nesta cena (formas complexas) e o solver/colisao gerarem poucos pares.
7. Para ter a hibrida valendo a pena, seria preciso: narrowphase em varias threads e/ou pares ordenados (sort por esfera na GPU), e envio apenas dos pares uteis.

## Ressalvas (leia antes de usar os numeros)

- A CPU aqui e UMA thread simples. O Newtoon (plano 19: ~57 ms de fisica a 100k, ~9,3 ms a 10k) usa outra estrutura e tem multithread; a CPU pura aqui (109 / 9,4 ms) NAO e o Newtoon, so a referencia do mesmo algoritmo.
- As qualidades NAO sao equivalentes ao Newtoon, em nenhuma das tres variantes: so esferas de mesmo raio, sem rotacao, sem atrito entre esferas (so atrito simples de chao), sem restituicao, sem sleeping, sem warm starting (pilha com ~2,5% de esferas com penetracao >10% a 100k), sem formas, juntas, raycast, eventos nem CCD. Nao ha comparacao justa de "estabilidade de pilha" com o Newtoon.
- Readback e upload sincronos (WaitForIdle); o p95 do upload tem outliers de ~30 ms que dominam o p95 total da hibrida a 10k.
- Resultados de 1 GPU/1 maquina (RX 6500 XT, Vulkan); PCIe da RX 6500 XT e x4, o que limita a banda (~1,6 GB/s efetivo aqui, incluindo memcpy).
- O determinismo vale na mesma maquina/driver (ponto fixo na soma); nao e garantido entre GPUs.
