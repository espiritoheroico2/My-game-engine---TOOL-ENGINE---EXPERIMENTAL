# 26 - Prototipo de fisica de esferas na GPU (compute)

Objetivo: medir viabilidade e custo de um passo de fisica de esferas inteiramente em compute (RHI/Diligent, Vulkan, RX 6500 XT), para comparar com a fisica na CPU (Newtoon).
NAO substitui o Newtoon, NAO toca em engine/physics, apps/editor ou engine/rhi. Um unico executavel: `tests/gpu_sphere_physics_bench.cpp`, alvo `tool_gpu_sphere_physics_bench`
(registrado em tests/CMakeLists.txt, sem add_test; rodar manualmente: `out/bin/Release/tool_gpu_sphere_physics_bench.exe vulkan`).

## Metodologia

Cenas: pilhas em grade 3D de esferas de raio 0,5 (diametro 1,0), espacamento inicial 1,05, base a 2 m do chao, jitter deterministico de +-0,02 em x/z. Caixa fechada (chao y=0 e quatro paredes + teto, por clamp) com folga de 4 m nas laterais.
- 10.000 esferas: 25x25x16 camadas, grade de 35x27x35 = 33.075 celulas.
- 100.000 esferas: 50x50x40 camadas, grade de 61x52x61 = 193.492 celulas.
300 passos de 1/60 s (5 s simulados), 4 iteracoes de Jacobi, relaxamento 0,5. 3 execucoes sincronizadas por cenario + 3 enfileiradas.

Pipeline de um passo (11 dispatches; tudo em RWStructuredBuffer, `setStorageRW`, o Diligent insere as barreiras UAV):
1. integrar: `prev = pos; vel.y -= g*dt; pos += vel*dt;` e clamp de chao/paredes (uma thread por esfera);
2. limpar `cellCount` (uma thread por celula);
3. contar: `InterlockedAdd(cellCount[cell], 1)` guarda o slot da esfera na celula;
4. prefixo exclusivo: 1 grupo de 1024 threads, cada uma soma um trecho contiguo, scan de Hillis-Steele nos totais, depois escreve o trecho;
5. espalhar: `cellList[cellStart[cell] + slot] = i`;
6. solver Jacobi x4 (ping-pong posA <-> posB): cada esfera visita as 27 celulas vizinhas, para cada par com distancia < D soma a correcao `n*(D-dist)*0.5*relax`; limite de 0,25 D por iteracao; clamp da caixa (chao: y >= R);
7. velocidade PBD: `v = (pos - prev)/dt`, amortecimento 0,995, no chao `v.y >= 0` e `v.xz *= 0,9` (atrito simples), limite de 30 m/s.

Grade: tamanho da celula = diametro maximo = 1,0 (todas as esferas tem o mesmo raio, entao vizinhos de contato sempre estao nas 27 celulas). A grade e construida uma vez por passo, apos a integracao; as 4 iteracoes reutilizam a mesma pertinencia de celula.
Determinismo: as correcoes de cada esfera sao somadas em ponto fixo (int, escala 65536), e soma inteira e associativa, entao a ordem dos vizinhos na celula (que depende da ordem dos atomicos) nao muda o resultado.

Tempo: `ms por passo` = relogio de parede de gravar + submeter + esperar a GPU (esperar = readBuffer de um buffer de 16 bytes, que faz WaitForIdle). Essa sincronizacao custa ~0,12 ms (medida com submit vazio) e esta incluida. Tambem ha o modo "enfileirado": 300 passos submetidos sem esperar e uma unica espera no fim, dividido por 300 (mais proximo do custo real de GPU quando a fisica faz parte de um quadro).
Mediana/p95 sao calculados sobre os 900 passos das 3 execucoes (cada execucao reinicia a cena).

## Resultados (RX 6500 XT, Vulkan)

| Esferas | Mediana ms/passo | p95 ms/passo | Media | Enfileirado ms/passo |
|---|---|---|---|---|
| 10.000 | 0,42 | 1,17 (execucoes: 1,49 / 1,45 / 0,89) | ~0,59 | 0,23 |
| 100.000 | 2,74 | 4,23 | ~2,89 | 2,28 |

Por execucao (sincronizado, mediana / p95): 10k: 0,447/1,491, 0,416/1,448, 0,403/0,885; 100k: 2,753/5,067, 2,737/4,219, 2,708/4,213. Enfileirado: 10k 0,225/0,257/0,211; 100k 2,261/2,279/2,308.

Estado final (execucao 1):
- 10k: y min 0,5000, y max 8,18, media 3,33, velocidade max 1,9 m/s; sobreposicao media 0,8% do diametro, max 6,4%; nenhuma esfera com >10%.
- 100k: y min 0,5000, y max 39,2 (inicial 43,45), media 9,75, velocidade max 10 m/s (a pilha de 40 camadas ainda esta desabando aos 5 s); sobreposicao media 2,6%, max 23,7%; 1,4% das esferas com algum par >10% de penetracao.

Validacoes (todas passaram nos dois cenarios):
- (a) nenhuma esfera atravessa o chao: y min = 0,5000 >= raio - 1e-3. OK.
- (b) sem NaN/inf; altura maxima <= inicial; velocidade <= limite. OK.
- (c) determinismo: hash FNV de posicao+velocidade finais identico nas 3 execucoes sincronizadas e nas 3 enfileiradas, em ambos os cenarios (bit a bit). OK.

## Comparacao com o Newtoon (honesta)

Referencia: docs/planos/19_ESCALA_ECS.md, "test ecsf" (esferas com fisica), tempo de fisica por quadro: 10k ~9,3 ms, 50k ~31,7 ms, 100k ~56,9 ms (solver ~ metade ou menos disso).

| Esferas | Newtoon (CPU) fisica | Prototipo GPU | Razao |
|---|---|---|---|
| 10k | ~9,3 ms | 0,42 ms (mediana sinc.) / 0,23 ms (enfileirado) | ~22x / ~40x |
| 100k | ~56,9 ms | 2,74 ms (sinc.) / 2,28 ms (enfileirado) | ~21x / ~25x |

Ressalvas importantes, para nao enganar:
- Os numeros NAO sao equivalentes em qualidade. O prototipo e so esferas, mesmo raio, sem rotacao, sem atrito entre esferas, sem restituicao, sem repouso/sleeping, sem juntas, sem raycast, sem outros formatos, sem eventos de colisao, sem continuous collision. O Newtoon faz tudo isso.
- Pilhas: Jacobi de posicao com 4 iteracoes e sem warm starting e fraco; a pilha de 100k ainda tinha 1,4% das esferas com mais de 10% de penetracao e pico de 24% ao fim de 5 s. A pilha tambem se espalha pela caixa (sem atrito esfera-esfera) em vez de assentar como solido; os comprimentos finais nao devem ser lidos como "estavel". Mais iteracoes custam quase linearmente no solver (o solver e 4 dos 11 dispatches).
- A medicao e de GPU isolada: nao inclui trazer os transformes de volta ao ECS/render, o que na pratica seria o proximo custo (ou nenhum, se o render ler direto do buffer da GPU).
- Nao inclui o custo de trazer eventos/queries (raycast, trigger) de volta a CPU; readBuffer sincrono custa pelo menos ~0,1 ms + copia (a 100k, posicoes = 1,6 MB).
- A 100k o custo e dominado pelo solver (27 celulas x ocupacao). Cenas reais com densidade/raios variaveis mudam isso.
- GPU compartilhada com o render: o ganho some se a GPU ja esta saturada pelo quadro grafico (RX 6500 XT e uma GPU pequena; mesmo assim 2,3-2,7 ms a 100k).

## O que faltaria para qualidade equivalente ao Newtoon

1. Estabilidade de pilhas: warm starting/impulsos persistentes (cache de contatos entre passos), mais iteracoes ou solver Gauss-Seidel por cor (graph coloring) ou substeps; restituicao e repouso (sleeping) por ilha.
2. Atrito entre esferas e rolagem: exige velocidade angular, torque e impulso tangencial por contato (nao ha rotacao no prototipo).
3. Outros formatos: caixas, cascos convexos, cilindros, mesh; raios variaveis (grade com celulas hierarquicas ou BVH/LBVH na GPU; hoje a celula = diametro maximo, ruim para raios muito diferentes).
4. Estruturas de consulta: raycast, overlap, triggers e eventos de colisao devolvidos a CPU (readback assincrono em fila de quadros, 1-2 quadros de latencia).
5. Integracao com o ECS: fonte de verdade na GPU, sincronizacao com Transform/hierarquia e com jogadores/scripts na CPU (corpos cinematicos, forcas, teleporte), e o render lendo posicoes direto do buffer (evita readback).
6. Juntas e restricoes, CCD, filtros de colisao/camadas, massas diferentes e forcas externas.
7. Determinismo entre GPUs/drivers: o ponto fixo resolveu a ordem dos atomicos, mas `sqrt/round` em outra GPU/driver pode diferir; o resultado e deterministico na mesma maquina, nao garantidamente entre fabricantes.
8. Backends: so Vulkan foi testado; D3D12/OpenGL (sem compute completo) nao.

## O que decidir a seguir

- Se a meta e "muitos corpos simples" (particulas fisicas, detritos, multidoes de esferas): o ganho de ~20x justifica um caminho GPU separado e opcional, so para esse tipo de corpo, coexistindo com o Newtoon para corpos "de jogo". Proximo passo barato: subir iteracoes/substeps e medir estabilidade (pilha repousando) e custo, depois adicionar atrito e rotacao simples para esferas.
- Se a meta e substituir o Newtoon: nao e viavel por esse caminho a curto prazo (itens 1-8), o prototipo so mostra que o teto de desempenho da GPU para esferas e muito maior.
- Decidir o contrato com o ECS/render (fonte de verdade na GPU, readback assincrono) antes de investir mais: e ele que define o custo real, nao o solver.
- Decidir criterio de qualidade (penetracao max por tempo de repouso, altura final de pilha) para comparar com o Newtoon na mesma cena antes de qualquer migracao.
