# 20 - Colisao com casco simplificado + LOD de colisao por distancia

## O que mudou
Antes, a capa simplificada (`PhysicsComp::simplifiedHull`) so entregava a caixa envolvente: a narrowphase so conhecia
caixa e esfera. Agora o casco colide de verdade.

- `ShapeType::CustomConvex` (`newtoon_types.hpp`): o `Collider` guarda `hull`, `hullLods` e `hullScale`.
  Os pontos ficam no espaco do corpo (origem = posicao do corpo; `offset` e ignorado nesse tipo).
- `newtoon_hull.hpp`: o casco ganha topologia (`faces`, `edges`, `faceVerts`), calculada UMA vez na construcao
  (vai para o cache). `Solid()` diz se serve para a narrowphase; senao o corpo cai para a caixa.
- `newtoon_convex.hpp` (novo): SAT convexo. Casco x casco, casco x caixa (o chao e uma caixa estatica grande; nao existe
  forma "plano" no motor), casco x esfera e raio x casco. Eixos: faces dos dois lados + arestas cruzadas (so os pares
  que formam face de Minkowski, teste do arco de Gauss). Contato de face: recorte da face incidente contra a de
  referencia; se passar de 8 pontos, mantem os 8 mais espalhados (amostragem do ponto mais distante, deterministica).
  Contato de aresta: 1 ponto. Mesmo formato (ponto, normal A->B, profundidade), mesmo warm start e solver das caixas.
  Sem alocacao por quadro (tudo em arrays fixos na pilha).
- Broadphase: a AABB do mundo vem de `ConvexHull::bounds` (escalada e rotacionada), igual a das caixas.
- `kMaxManifoldPoints` continua 8 (nao foi reduzido).

## LOD de colisao
`chooseColliderLod(distance, ColliderLodConfig)` e funcao pura, nao crescente com a distancia.
Padrao (`ColliderLodConfig`): ate 20 m = 24 pontos; ate 60 m = 12; acima = 8 (caixa de 6 faces). Distancia NaN ou
negativa conta como perto.

- Niveis: `HullCache::GetLods(malha, revisao, cfg, build)` devolve um `HullLodSet` (24/12/8), cada nivel vindo do cache por
  (malha, revisao, maxPoints). Corrigido: pedir outro `maxPoints` da mesma malha nao apaga mais os outros niveis.
  Revisoes velhas vao para uma lista "aposentada" (so `Clear()` libera) porque o `Collider` guarda ponteiro cru.
- Foco: `world.setCollisionFocus(pos)` / `clearCollisionFocus()`. Sem foco o LOD fica desligado (casco mais detalhado).
- A cada passo fixo, `Prepare` escolhe o nivel de cada corpo pela distancia ao foco: so troca um ponteiro para um casco ja
  cacheado, entao nada e reconstruido nem trava o quadro; a troca vale no proximo passo fixo.
- Editor (`editor_play.hpp`): `colliderFor` entrega os niveis do cache; `stepPlay` passa a posicao da camera principal
  como foco (sem camera, limpa o foco).

## Testes (tests/test_newtoon_convex.cpp, alvo tool_physics_tests)
repouso no chao; pilha de 3; casco pesado inclinado; normais casco x caixa/casco (sinal e direcao) e prova de que o casco
nao e tratado como caixa (octaedro); casco x esfera e raio; LOD (monotonia, config fora de ordem, sem foco = maximo,
troca so no proximo passo); determinismo (bit a bit). Resultado: 33 testes, 0 falhas (os 26 anteriores intactos).

## Medicao (tool_newtoon_scale_test, Release, 200 corpos = 20 colunas x 10 empilhados, sono desligado)
| caso | ms/passo | narrowphase ms | contatos/passo |
|---|---|---|---|
| ANTES: colisor de caixa (o que a capa entregava) | 1.12 | 0.17 | 235 |
| DEPOIS: casco cubo 8 pts (mesma geometria da caixa) | 3.08 | 0.40 | 1087 |
| DEPOIS: casco cilindro 24 pts | 5.66 | 1.85 | 1589 |

Os numeros de contatos NAO sao iguais: o caminho convexo gera o poligono de contato completo (4 pontos por face alinhada),
enquanto o `BoxBox` antigo, com pilhas alinhadas e penetracao ~0, gera poucos pontos (depth > 0 estrito). O tempo total
cresce principalmente no solver (mais linhas). So a narrowphase, com os mesmos pares: 0.17 -> 0.40 ms (cubo, ~2.3x) e
1.85 ms (24 pontos). Otimizacoes ja feitas: plano do arco de Gauss pre-calculado por aresta, saida antecipada, caminho
sem divisao com escala 1. Medidas em maquina de desenvolvimento, sem repeticoes estatisticas: valem como ordem de grandeza.

## Riscos / fora do escopo
- O k-DOP com 24 pontos usa 14 direcoes (6 eixos + 8 cantos): um cilindro vira quase uma caixa com cantos cortados. Mais
  fidelidade exigiria mais direcoes (ate 26) / maxPoints maior; nao mexi no gerador.
- Troca de LOD muda a geometria de contato (poucos mm); o warm start casa por posicao, mas uma pilha exatamente no limite
  de distancia pode alternar de nivel se o foco oscilar. Nao ha histerese (candidata a melhoria).
- `Collider.hull/hullLods` sao ponteiros crus: o `HullCache` global deve viver mais que o mundo (nao chame `Clear()` em Play).
- Casco x casco e mais caro que caixa x caixa (ver tabela); cenas com milhares de cascos devem usar o LOD (8 pontos = caixa).
- Cilindro/esfera/casco x esfera de casco nao tem teste de pilha; casco x esfera so tem teste unitario.
