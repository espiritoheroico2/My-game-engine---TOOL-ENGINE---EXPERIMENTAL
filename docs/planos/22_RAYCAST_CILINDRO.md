# 22 - Cilindro e raycast unico (Fase 1)

Referencia de comportamento: Unity (`Physics.Raycast`, `RaycastAll`, `layerMask`, `QueryTriggerInteraction`).

## A. Cilindro (`ShapeType::Cylinder`)
- Eixo Y local. Raio em `Collider::radius`; meia altura em `Collider::halfHeight` (se negativa, usa `extents.y`).
- **Narrowphase**: o cilindro vira um prisma convexo de 16 lados (`UnitCylinderTopology`, 32 vertices, 18 faces,
  48 arestas, escalado por `(raio, meiaAltura, raio)`) e usa o MESMO SAT/recorte de `newtoon_convex.hpp` do casco e da
  caixa. Cobre cilindro x chao/caixa, x cilindro, x casco; a esfera usa `SphereConvexCollide` (esfera exata contra o
  prisma). Motivo da escolha: um unico caminho de codigo, sem alocacao, contatos de face com ate 8 pontos (repouso e
  pilha estaveis).
- Limite conhecido: o prisma e inscrito (vertices no circulo), ate ~2% menor que o cilindro analitico, e vertice contra
  vertice entre dois cilindros inclina a normal em ate 11.25 graus. Rolar vira "rolar em facetas".
- **Raycast** usa o cilindro ANALITICO (`RayCylinder`: laje em Y x quadratica lateral), nao o prisma.
- **Massa/inercia** (`SetMassFromShape`): `Iy = m r^2 / 2`, `Ix = Iz = m (3 r^2 + h^2) / 12`, `h = 2 * meiaAltura`.
- **Broadphase**: AABB exata do cilindro orientado (`|R_y| * hh + r * sqrt(1 - R_y^2)` por eixo).

## B. Raycast unico (3D)
- `PhysicsWorld::Raycast(origem, direcao, alcance, RaycastFilter)` percorre a BVH4 de broadphase e testa a forma exata
  de cada folha (caixa, esfera, cilindro, casco convexo). Atalho antigo `Raycast(o, d, alcance, Entity ignore)` mantido.
- `RaycastAll(origem, direcao, alcance, filtro, out)` devolve todos os acertos ordenados por distancia (desempate por
  entidade).
- `RaycastBruteForce` e a referencia O(N) (so para testes/medicao).
- `RaycastFilter`: `layerMask` (padrao todas; camada em `Collider::layer`), `ignore`, `triggers`
  (`Collide`, `Ignore`, `UseGlobal` -> `PhysicsWorld::queryTriggersByDefault`, padrao true) e `ignoreBackface`
  (nao acerta se o raio nasce dentro da forma; padrao false).
- `RaycastHit`: distancia, ponto, normal, entidade e `triangleIndex` (-1: reservado para malha estatica, ainda inexistente).
- A varredura O(N) antiga ja nao existia no 3D (o raycast ja usava a BVH); o que mudou foi unificar filtro/forma.

## B5. Raycast 2D
- `PhysicsWorld2D::Raycast` agora usa uma BVH 2D de caixas (mediana, `nth_element`), construida de forma preguicosa na
  primeira consulta apos cada `Prepare` (sem custo para quem nao usa raycast; mutex protege consultas concorrentes).
  Mesmo filtro (`RaycastFilter`, `Collider2D::layer`). `RaycastBruteForce` e a varredura O(N) original.
- Resultado igual a forca bruta (teste com 500 formas e 1000 raios; em empate exato de distancia vence a maior entidade,
  como na varredura).

## D. Medicoes (Release, MSVC, maquina de desenvolvimento)
| Medicao | Resultado |
|---|---|
| 1000 raios x 1000 formas (mistas, 3D), forca bruta | ~49-52 ms |
| 1000 raios x 1000 formas, BVH | ~0.50-0.53 ms (~100x) |
| 200 cilindros (grade 5x5, 8 andares), 300 passos | ~1100-1250 ms total, ~3.7-4.2 ms/passo (24 ainda acordados no fim) |

### API sem alocacao e em lote (requisitos adicionais)
- `RaycastNonAlloc(o, d, alcance, filtro, buffer, capacidade)`: escreve no buffer do chamador, ordenado, mantem os mais
  proximos se encher. `RaycastAll` reaproveita o `std::vector` do chamador (clear mantem a capacidade).
- Camada, entidade, gatilho e alcance sao checados antes do teste exato da forma; a BVH (caixas + alcance) descarta
  candidatos antes disso.
- `RaycastBatch(origens, direcoes, n, alcance, filtro, saida, grain)`: blocos via `tool::parallelFor` (pool persistente,
  pela JobSystem); `saida[i]` corresponde ao raio i (ordem da entrada), sem alocar por raio. Testado: igual a raios
  individuais, com 1 thread e com varias.
- Medido (3 execucoes, 1000 formas, 20000 raios): 1 thread (grain = n) 9.9-10.4 ms; grain 256 em 20 threads do pool
  0.99-1.06 ms (~10x). Os 1000 raios x 1000 formas: forca bruta 49.6-53.3 ms, BVH 0.49-0.55 ms.

## Fora da Fase 1 (pendente)
Malha triangular estatica (e `triangleIndex` real), SphereCast / BoxCast / CapsuleCast (Fase 3), fiacao das opcoes no
editor (Fase 2), capsula.

## Riscos
- Prisma de 16 lados: custo do SAT cilindro x cilindro (48x48 arestas com saida antecipada) maior que caixa x caixa.
- `Collider` ganhou `halfHeight` e `layer` (no fim da struct, default seguro); `RaycastHit` ganhou `triangleIndex`.
- `PhysicsWorld2D` passou a conter `std::mutex`/`std::atomic` (nao copiavel; nada no repositorio o copia).
- Raycast 3D ve o estado do ultimo `Prepare` (passo fixo ou `Rebuild`).
