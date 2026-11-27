# 19 - Escala dos testes de ECS ("test ecs" e "test ecsf") rumo a 1 milhao

Regra: ganhos so por arquitetura (dados compactos, lotes, culling, instancing, memoria sem strings repetidas). Nada de esconder objetos,
baixar qualidade ou desligar recursos. Cada degrau tem medicao antes/depois.

Comando de medicao (RX 6500 XT, Vulkan, 1280x720, dt fixo 1/60, 120 quadros medidos, 60 de aquecimento, projeto de teste em copia):

    ToolEditor.exe --ecs-mover N | --ecs N  --api vulkan --bench-render 1280x720 --bench-dt 0.016667 --bench-frames 120 --project <copia>

`--ecs-mover N` (novo, apps/editor/main.cpp) = "test ecs: N" (sem fisica, MoverComp oscilando). `--ecs N` = "test ecsf: N" (esferas com fisica).
Ambos criam as entidades pelo mesmo spawner do console (startEcsTest).

## Degrau 0 - linha de base (antes de qualquer mudanca de arquitetura)

Zonas em ms medios por quadro. "Scene prep" contem "lights, shadow signature" + "occlusion buffer" + (parte de) "culling + draw list".

| Degrau | Modo | Quadro medio | p95 | Scene prep | (sombra: assinatura) | (oclusao) | GPU submit | Fisica | Update |
|---|---|---|---|---|---|---|---|---|---|
| 10k | ecs (movers) | 34,8 ms | 48,4 | 16,0 | 7,7 | 8,2 | 13,3 | 0 | 0,8 |
| 10k | ecsf (fisica) | 43,8 ms | 52,4 | 11,0 | 8,5 | 2,5 | 13,1 | 13,9 (solver 9,0) | 15,0 |
| 50k | ecs (movers) | 133,2 ms | 145,7 | 67,9 | 34,9 | 33,1 | 54,7 | 0 | 3,4 |
| 50k | ecsf (fisica) | 145,3 ms | 162,7 | 47,9 | 36,6 | 11,4 | 56,3 | 31,6 (solver 12,3) | 35,0 |

Memoria: nao medida ainda nesta tabela (estimativa dos estudos: ~0,7 KB/entidade so no MeshRenderer, ~7 KB/esfera no teste com fisica).

### Diagnostico (numeros)

- O custo cresce ~linear com N (10k -> 50k: quadro x3,8 em movers). Nenhuma zona e constante.
- Gargalo 1 (CPU, ambos os modos): "lights, shadow signature" = 35-37 ms a 50k (~0,7 us por objeto). E um laco O(N) por quadro (hashCasters) que
  percorre todos os casters mesmo sem a sombra ter mudado; em movers a assinatura muda todo quadro de verdade, entao nao da para reaproveitar o mapa.
- Gargalo 2 (CPU, movers): buffer de oclusao por software = 33 ms a 50k (movers) contra 11 ms (esferas caindo agrupadas): custo cresce com a area rasterizada.
- Gargalo 3 (GPU submit CPU-side): 55 ms a 50k = montagem/ordenacao de Draw (~230 B por objeto por quadro) e upload completo do buffer de instancias
  (160 B x N) mesmo com culling GPU ligado.
- "Update" no ecsf = 35 ms: e o tick de fisica + sincronizacao de transform de volta ao ECS (ver Physics simulation).
- Fisica (so ecsf): 31,6 ms a 50k, solver domina (12,3). Fora do escopo (engine/physics e proibido).

## Mudanca 1 - cache de matriz mundial plano + hash de casters unico por vista

Causa: `worldMat` usava `unordered_map<u64, Mat4>` limpo a cada vista (1 alocacao de no por objeto, rehash) e `hashCasters`
(laco O(N) com 16 floats por objeto) rodava DUAS vezes por quadro (assinatura do sol e das luzes pontuais).
Mudanca (apps/editor/editor_hier.hpp, editor_state.hpp): vetor plano indexado por `e.index` com carimbo de quadro (limpar = ++stamp, O(1));
`casterHash()` calcula o hash dos casters uma vez por vista e as duas assinaturas o reutilizam. Resultado numerico de cada matriz identico
(mesma funcao, so o armazenamento muda); a deteccao de sombra suja e igual (o hash entra nas duas assinaturas). Sem perda de qualidade.

| Degrau | Modo | Quadro antes -> depois | Scene prep | Sombra (assinatura) | Observacao |
|---|---|---|---|---|---|
| 50k | ecs (movers) | 133,2 -> 109,9 ms (p95 145,7 -> 115,8) | 67,9 -> 55,3 | 34,9 -> 25,6 | -17% |
| 50k | ecsf (fisica) | 145,3 -> 116,6 ms (p95 162,7 -> 125,2) | 47,9 -> 34,7 | 36,6 -> 27,5 | -20% |

Testes: selftest 278 checks, 0 falhas; ctest "Editor\." 74/74.

Ainda no 50k, o que sobra (numeros pos-mudanca 1, movers): oclusao por software 29,7 ms, assinatura de sombra 25,6 ms (primeira visita calcula
worldMat com seno/cosseno por objeto e le MeshRenderer de ~650 B por objeto: memoria fria), GPU submit 47,4 ms (montagem/ordenacao de Draw
+ upload de 160 B x N por quadro), fisica 28,6 ms (so ecsf; fora de escopo).

## Revisao Opus (docs/planos/21): estado

(2) Causa da queda da assinatura de sombra: a cena de teste (Project/Scenes/Main.scene.json) so tem o Global Light (sol) e nenhuma luz
pontual/spot com sombra; `pointShadowSignature` nao roda (`pointShadows` = false). Logo o compartilhamento do casterHash NAO contribuiu
nas tabelas acima: todo o ganho vem do cache plano de worldMat. O ruido entre execucoes (fisica -10% sem mudanca) tambem invalida
comparar uma execucao so; a tabela "Mudanca 1" e PROVISORIA ate (1) ser refeita com 3 execucoes/mediana.

(1) Infra de medicao: adicionado ao bench a linha "RAM working set X MB, sizeof(MeshRenderer), sizeof(Transform)" (main.cpp).
BLOQUEIO: outro agente esta editando engine/physics (newtoon_world.hpp usa collisionFocus_/hasCollisionFocus_ ainda nao declarados) e a
arvore nao compila no momento; alem disso rhi.hpp, diligent_device.cpp, settings_*, newtoon_* sao mudancas nao-ECS no mesmo binario.
Medicao limpa exige esperar essa edicao terminar. Mudancas ECS minhas ainda nao medidas depois do ultimo build bom: cache de assets por
quadro (editor_state.hpp), liberacao de textura GPU em invalidate (project.hpp, gpu_renderer.hpp::forgetTexture, selftest novo),
ordenacao por indices em GpuRenderer::end(). Ultimo build bom (antes do edit de RAM) compilou todas; selftest/ctest dessas 3 ainda NAO rodados.

### Escrito sem compilar (build parado a pedido, aguardando fim das edicoes de physics)
- casterHash(): o cache por vista agora guarda o olho da camera (casterHashCamEye_) e recalcula se a camera mudar na mesma vista.
- selftest.hpp: 6 checks novos (hierarquia de 3 niveis = sem cache; mover o avo invalida o filho; indice reutilizado com nova geracao no
  mesmo quadro; casterHash estavel parado, muda ao mover 1 caster, recalcula com outra camera). Teste sol+pontual nao feito (exige LightFrame);
  o casterHash e a parte comum as duas assinaturas.
- Ordem ao liberar: build -> selftest -> ctest -> 3 execucoes/mediana (10k, 50k, 100k, copia restaurada, RAM) -> medir as 3 mudancas recentes.

## Medicao com rigor (binario atual = mudanca 1 + cache de assets + forgetTexture + ordenacao por indices + selftest/hash de camera)

Metodo: 1 execucao descartada + 3 validas por celula, copia do projeto restaurada antes de cada execucao, Vulkan 1280x720, dt fixo,
120 quadros, oclusao ligada. Base: HEAD 04104de + arvore suja (inclui mudancas NAO-ECS de outros agentes: physics/hull, rhi, settings).
Selftest 287/0 falhas, ctest Editor 74/74. (A falha anterior "casterHash: moving one caster" era erro do TESTE: o helper cacheOn nao
invalidava casterHashFrameValid_ como renderScene faz, entao o "novo quadro" devolvia o hash velho; casterHash esta correto.)

Quadro medio (mediana, min-max, desvio):
| Degrau | Modo | Mediana | min-max | desvio | p95 (med.) | Sombra-assin. | Oclusao | GPU submit | Fisica | RAM (working set) |
|---|---|---|---|---|---|---|---|---|---|---|
| 10k | ecs (movers) | 24,71 ms | 24,55-24,93 | 1,5% | 27,6 | 5,6 | 6,2 | 8,8 | 0 | 1480 MB |
| 50k | ecs (movers) | 104,04 | 103,53-108,39 | 4,7% | 111,3 | 27,3 | 30,3 | 39,3 | 0 | 1558 MB |
| 100k | ecs (movers) | 221,09 | 220,22-221,33 | 0,5% | 233,6 | 54,9 | 65,9 | 90,6 | 0 | 1658 MB |
| 10k | ecsf (fisica) | 28,65 | 28,57-28,67 | 0,3% | 30,6 | 5,6 | 1,3 | 8,5 | 9,3 | 1503 MB |
| 50k | ecsf (fisica) | 113,43 | 108,16-114,65 | 6,0% | 124,2 | 29,1 | 8,9 | 37,2 | 31,7 | 1657 MB |
| 100k | ecsf (fisica) | 222,80 | 222,37-227,00 | 2,1% | 248,8 | 57,4 | 19,2 | 79,7 | 56,9 | 1867 MB |

Desvio < 3% em 4 de 6 celulas; 50k tem 4,7% (movers) e 6,0% (fisica): meta nao atingida ali (variacao de GPU/driver), tratar com cautela.
RAM: base do processo ~1,4 GB (drivers/shaders); marginal por entidade ~1,5-1,8 KB (movers) e ~3,6 KB (fisica) -> 1M = ~1,5 GB (movers) / ~3,7 GB (fisica) de RAM, sem a base.
Custo ~linear: 100k = 2,1x de 50k. A 1M, extrapolado ~2,2 s por quadro; a meta de 1M continua muito longe: o trabalho que falta e arquitetural (Transform por versao,
buffer persistente, oclusao HiZ), nao micro-otimizacao.
Antes/depois das 3 ultimas mudancas isoladas: NAO medidas (nao existe binario base sem elas e nao posso usar git stash/checkout); comparando com a unica
medicao anterior (50k, mudanca 1 so): movers 109,9 -> 104,0 (-5%), ecsf 116,6 -> 113,4 (-3%), dentro do ruido de 50k (4-6%) e com mudancas de physics no meio.
O cache de assets nao tem efeito neste teste (as esferas nao tem textura nem material), so em cenas com textura.

## Degrau 2 - assinatura por valor dos vetores densos + matriz mundial compartilhada entre vistas/quadros (rev. Opus 23)

Desvio de projeto (justificado): em vez de "versao por entidade via API", usei assinatura POR VALOR dos vetores densos de Transform/Parent
(soma paralela de hashes por entidade: indice + geracao + bytes) + MeshRenderer so dos campos que decidem a sombra. Detecta qualquer
escrita, inclusive por ponteiro bruto (sem API para esquecer; o selftest escreve por ponteiro bruto e a assinatura muda). Custo O(N) mas
~0,3-2,8 ms a 100k (era ~50 ms). A matriz mundial agora e reaproveitada entre vistas e quadros enquanto a assinatura nao muda; quando muda,
raizes e filhos diretos de raiz sao pre-calculados em paralelo (bit-identico ao calculo sob demanda; o resto continua sob demanda).
Arquivos: engine/ecs world.hpp (denseView/aliveIndex/generationAt/dormantAt, so leitura), editor_hier.hpp (beginWorldMatFrame,
casterSignatureDense), editor_state.hpp (casterHash/renderScene), selftest.hpp, main.cpp (bench: RAM privada, hash/dump de imagem).
Limite conhecido: o LOD da esfera por distancia nao entra na assinatura das sombras de PONTUAL (para o sol a camera ja esta nela);
modelo carregado pela primeira vez so aparece na sombra um quadro depois (lib.meshCount entra na assinatura no quadro seguinte).

A/B intercalado no MESMO binario (A = TOOL_AB_LEGACY_SIG=1 + LEGACY_SORT=1 + NO_MEMO=1; B = padrao). Em vez de git worktree (rebuild
completo de Diligent em outra pasta), usei chaves de ambiente, alternativa citada na revisao 23; A isola as mudancas ECS recentes (hash por
objeto + carimbo por vista, stable_sort de Draw, sem cache de assets) do resto da arvore (physics/rhi/settings iguais nos dois lados).
Protocolo: 4 pares A,B por celula, o 1o par descartado, 3 validos, copia do projeto restaurada a cada execucao. Quadro medio (ms), 3 execucoes:

| Degrau | Modo | A (antigo) | B (novo) | ganho | sombra-assin. A -> B | desvio A / B |
|---|---|---|---|---|---|---|
| 10k | movers | 25,3 (25,26-25,68) | 19,9 (19,77-20,04) | -5,4 | 5,3 -> 0,32 | 1,7% / 1,4% |
| 50k | movers | 109,1 (108,3-109,4) | 77,7 (77,5-78,1) | -31,4 | 25,6 -> 1,58 | 1,0% / 0,8% |
| 100k | movers | 231,1 (230,2-233,8) | 160,8 (160,7-161,8) | -70,3 | 50,3 -> 2,8 | 1,6% / 0,7% |
| 10k | ecsf | 30,0 (29,99-30,10) | 24,0 (23,94-24,14) | -6,0 | 5,3 -> 0,42 | 0,4% / 0,8% |
| 50k | ecsf | 115,9 (115,7-117,0) | 80,6 (79,8-81,3) | -35,3 | 26,8 -> 1,32 | 1,1% / 1,9% |
| 100k | ecsf | 243,0 (242,1-245,0) | 165,4 (165,0-165,9) | -77,6 | 53,9 -> 2,08 | 1,2% / 0,5% |
| 10k | parada | 10,0 (9,94-10,09) | 7,84 (7,82-7,98) | -2,2 | 0,38 -> 0,13 | 0,8% / 1,0% |
| 50k | parada | 37,7 (37,69-38,04) | 23,15 (23,14-23,15) | -14,6 | 3,2 -> 0,32 | 0,5% / 0,0% |
| 100k | parada | 70,4 (69,95-70,43) | 40,3 (39,86-40,39) | -30,1 | 6,7 -> 0,79 | 0,7% / 0,6% |
(desvio = (max-min)/mediana; todos < 3%.) "parada" = `--instances N` (entidades planas sem Parent, sem movimento, sem fisica).
Parte do ganho vem de zonas vizinhas (GPU submit 109 -> 84 ms a 100k movers; oclusao parada 29,6 -> 11,9 ms): matrizes ja prontas
(sem seno/cosseno repetido por vista), ordenacao por indices e menos trabalho de CPU no submit. Isolar cada uma das 3 mudancas pequenas
individualmente (cache de assets, ordenacao) NAO foi feito: o cache de assets nao e exercitado por esta cena (sem texturas), e a
ordenacao e pulada quando a lista ja esta em ordem (uma malha so).

Criterios de aceite: sombra-assin. a 100k <= 2 ms: movers 2,8 (NAO atingido), ecsf 2,08 (NAO atingido por pouco), parada 0,79 (ok).
Quadro movers 100k >= 45 ms menor: -70,3 ms (ok). Selftest 291 checks, 0 falhas; ctest Editor 74/74.
RAM PRIVADA de pico (PeakPagefileUsage, MB), A -> B: movers 10k 1759->1761, 50k 1866->1879, 100k 2057->2086 (+29 MB = ~290 B/entidade);
ecsf 100k 2273->2297 (+24 MB); parada 100k 1903->1903 (+0). Criterio +16 B/entidade NAO atingido nas cenas com Parent: causa nao
identificada (suspeita: realocacoes do vetor de 72 B/entidade durante o nascimento, ja que a assinatura muda a cada quadro de spawn); a
cena sem Parent nao tem aumento. Base do processo ~1,7 GB (commit inclui memoria de driver/GPU).
Imagem: ultimo quadro medido (Game e Scene com camera diferente, 1280x720), comparado por bytes (cmp): movers 10k A-A 0, B-B 3 bytes,
A-B 3 bytes (1 pixel); parada game 0 em todos, scene B-B 3 bytes. Ou seja, <= 1 pixel em 921.600 e do tamanho do ruido B-B; A-A repetido nao
variou (0), entao ha dependencia de TEMPO (corrida latente) que aparece quando o quadro fica mais rapido: nao investigada; as chaves LEGACY_SORT
isoladas ficaram deterministicas, as demais nao (a causa nao e a ordem dos Draw). Chaves TOOL_AB_* sao so de benchmark e continuam no codigo.

## Rev. Opus 24, item 1 - corrida de imagem / worldMat so-leitura

Feito: worldMat() com o cache ativo agora e SOMENTE LEITURA (sem resize, sem escrita; erro de cache calcula sem gravar); beginWorldMatFrame
pre-calcula TODOS os niveis (raizes e filhos diretos em paralelo + passo serial para netos/cadeias profundas). Selftest 292/0 falhas
(novo: tamanho do vetor nao muda com o cache ativo, 3 niveis pre-calculados, entidade tardia sem gravar).
A hipotese "corrida em worldMat" foi REFUTADA: depois da correcao a diferenca de imagem continua. Fatos medidos (last frame, Scene = 2a vista, 1280x720):
- Em um mesmo processo, quadros consecutivos sao identicos (penultimo == ultimo, 6/6 execucoes): nao e leitura antes do fim do quadro.
- Entre processos, o estado alterna: 1 pixel (3 bytes, sempre x=786,y=368 na cena parada). Game view: 0 bytes em 40+ execucoes da cena parada.
- Persiste com: caminho antigo (chaves LEGACY), GPU culling desligado (--no-gpu-cull), oclusao desligada, particulas desligadas, sombras
  desligadas, SSAO desligado; a cena parada (--instances) nao tem Parent, entao o caminho de hierarquia nem e exercitado.
- Movers 10k (com hierarquia): game 0-6 bytes, scene 0-5 bytes entre 8 execucoes, mesmo com worldMat so-leitura.
Conclusao honesta: causa NAO identificada; nao e worldMat, nem mudanca nova (o caminho antigo tambem oscila: A-A = 3 bytes em 1 de 3 pares).
Suspeitas restantes: nao-determinismo do driver/GPU na rasterizacao da vista Scene ou algo por processo (semente/ordem de threads)
que so a vista Scene usa. Nao aceito como "ruido provado": e um item aberto.

## Rev. Opus 24, item 2 - causa do +290 B/entidade (memoria privada de pico)

Causa (isolada por chave, 100k movers, pico privado): BASE 2087 MB; LEGACY_SORT=1 2057 MB (-30 MB); LEGACY_SIG=1 2088; NO_MEMO=1 2087.
Ou seja: era a ordenacao por indices de GpuRenderer::end(), que mantinha um segundo vetor `drawsSorted_` de Draw (~300 B x N, capacidade
retida entre quadros). NAO era o cache de matrizes (72 B/ent, igual nos dois caminhos). Correcao: permutacao aplicada NO LUGAR (ciclos), so
um Draw temporario + 8 B/ent de indices/chaves (u32). Resultado: movers 100k 2056 MB (antes de A: 2057), ecsf 100k 2275 (A: 2273):
delta ~0 B/ent (meta <= +16 B/ent atingida); quadro movers 100k 159,7 ms (sem perda). Imagem: Game da cena parada identica ao stable_sort (0 bytes).
Orcamento por componente (sizeof x N a 1M): MeshRenderer 652 B = 652 MB, Transform 36 B = 36 MB, cache de matriz 72 B = 72 MB,
indices/chaves do sort 8 B = 8 MB; Draw transiente ~300 B so existe durante o quadro (vetor draws_, ~300 MB a 1M: alvo do buffer persistente).
Selftest 292/0 falhas; ctest Editor 74/74 (rodados antes e depois).
Item aberto (Scene, 1 pixel): ver secao acima; nao tratado como ruido.

## Plano do item 3 (versao por API) - escrito antes de concluir o codigo (rev. Opus 25)

Isolamento anterior das chaves (movers 100k, 1 execucao cada, quadro medio / pico privado): padrao 159,7 ms / 2087 MB; LEGACY_SORT=1 183,9 / 2057
(o sort em indices ganha ~24 ms e custava +30 MB, ja corrigido); LEGACY_SIG=1 205,3 / 2088 (hash por objeto + carimbo por vista, ~45 ms a mais);
NO_MEMO=1 160,9 / 2087 (cache de assets sem efeito na cena sem textura). As chaves TOOL_AB_* foram removidas do codigo de producao.
Desenho: (a) "API" = o proprio acesso ao World. Todo acesso MUTAVEL a um componente (get/getByIndex nao-const, each/parallelEach/forEachDense,
add, remove) sobe a versao (epoch) do armazenamento; leitura usa get const / eachRead / denseView (nao sobe). O caminho de render foi migrado
para leitura. (b) `World::epochOf<T>()` O(1): o cache de matrizes mundiais e a assinatura de sombras comparam VERSOES (Transform, Parent,
MeshRenderer, Inactive, estrutura: create/destroy/dormant) em vez de varrer a cena; sem mudanca = custo O(1). (c) O hash por valor deixa de ser
mecanismo e vira VERIFICADOR: em Debug, ao fim de cada renderScene recalcula as somas por valor e compara com as do inicio (pega escrita por
ponteiro guardado depois do beginWorldMatFrame, que devolveria matriz velha sem aviso), e se as versoes nao mudaram entre quadros as somas
devem ser iguais. O selftest chama o verificador explicitamente (Release). (d) Falsos positivos da versao (acesso mutavel que so leu)
custam uma reconstrucao, nunca um resultado velho; um contador de reconstrucoes/falhas de cache por quadro vai ao bench para nao esconder custo.
Limite: em movers todo Transform muda por quadro, entao a reconstrucao das matrizes continua O(N) (paralela); a assinatura em si vira O(1).

## Pendente (ordem proposta, ver docs/estudos/RENDER_FILA_MEMORIA.md)

1. Etapa 1: ordenar indices em vez de `Draw` em `GpuRenderer::end()`; cache de resolucao de asset por objeto (strings de 160 B no MeshRenderer);
   corrigir texs_ orfa. 2. Etapa 3: id u32 no MeshRenderer (menos memoria fria no laco de casters/oclusao). 3. Etapa 2: buffer de instancias
   persistente (B2) para tirar o upload de 160 B x N por quadro. 4. Oclusao: HiZ na GPU (E7) substitui a rasterizacao por software.
Degraus 100k+ nao medidos: a fatia de 50k ainda esta em 110-117 ms; nao faz sentido subir antes das etapas 1-2.
Memoria (RAM) por degrau tambem nao foi medida.
