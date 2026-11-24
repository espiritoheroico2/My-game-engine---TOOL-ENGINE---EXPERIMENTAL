# 12 — Otimização de Física e ECS (estado atual)

Documento de referência para as mudanças vindas do relatório "Otimização de Física e ECS em Game Engine" e das correções feitas sobre ele.

## Estado da verificação

- `cmake --build --preset editor`: sem erros.
- `ctest --preset editor`: 47/47.
- `ToolEditor --selftest`: 250 checks, 0 falhas.
- `ToolEditor --play --frames 300` (offscreen): roda até o fim.
- `ToolEditor --ecs 2000 --project <pasta> --bench-frames 120`: roda até o fim.

Limite: o benchmark de física com a cena de teste mede 0,02 ms, ou seja, a carga é pequena. Isso confirma estabilidade, não ganho de desempenho. O ganho real precisa de medição na cena com milhares de corpos.

## Física (Newtoon) — `engine/physics/include/tool/physics/`

| Arquivo | Origem | Conteúdo |
|---|---|---|
| `newtoon_types.hpp` | relatório, com `AddImpulse` adicionado | `Transform`, `Rigidbody` (com `velocity`, `IsDynamic()`, `IsStatic()`, `WakeUp()`, `AddForce`, `AddTorque`, `AddImpulse`), `Collider`, `FrameArenaAllocator`, `AABB`, `RaycastHit` |
| `newtoon_jobs.hpp` | cabeçalho do relatório + `JobSystem` completa do commit | `ConstraintColorGraph` (coloração por bitsets, até 16 cores) e `JobSystem::ParallelFor` |
| `newtoon_world.hpp` | `newtoon_world.cpp` enviado, com correções | `PhysicsWorld`: broad-phase em blocos, narrow-phase paralela, solver por cores, CCD, sono, eventos |

### Por que a física estava quebrada

O commit `82269d9` tinha `newtoon_world.hpp` usando `Transform`, `velocity` e `IsDynamic()`, mas os tipos commitados (`BodyCore`/`MotionProperties`) não tinham esses membros. Além disso o `newtoon_world.hpp` estava truncado. A combinação do relatório (tipos + jobs + world) fecha a API inteira.

### Correções sobre o relatório

1. **Corrida de dados no solver.** `SolveSingleContact` escrevia velocidades também em corpos estáticos e cinemáticos. A coloração só garante exclusão para corpos dinâmicos, então duas threads podiam escrever no mesmo corpo estático ao mesmo tempo. Agora o `apply` só escreve em corpos dinâmicos.
2. **Arena não usada.** `FrameArenaAllocator` era criada por mundo (16 MB) e só recebia `Reset()`. Foi removida do `PhysicsWorld`. O tipo continua em `newtoon_types.hpp` para uso futuro.
3. **Cópia de pontos de contato.** `res.pts = localPts` foi trocado por `assign`, que reaproveita a capacidade do vetor.
4. **`AddImpulse` ausente.** O editor (`editor_play.hpp`) já chamava `Rigidbody::AddImpulse`, que não existia nos tipos do relatório. Adicionado: `velocity += j * inverseMass`.
5. **Texto do PDF cortado.** Algumas linhas longas estavam cortadas na margem do PDF. A classe `JobSystem` foi restaurada do arquivo commitado, que tem o mesmo código.

## ECS — `engine/ecs/include/tool/ecs/world.hpp`

- Cauda do arquivo reconstruída: no commit `91c3f14` o arquivo terminava no meio da declaração de `storages_`. Foram adicionados `storages_`, `tags_`, `dormant_` (`uint8_t`), `sim_`, `keepScratch_`, `scratch_` e `scratchDepth_`, todos usados pelo próprio arquivo.
- `getByIndex`, `hasByIndex`, `parallelEachDense` e `keepScratch_` já estavam no arquivo; não foram alterados.

## Math — `engine/math/include/tool/math/math.hpp`

- `Mat4::identity()` adicionado (usado em `apps/editor/components.hpp`).

## Play — `apps/editor/play_runtime.hpp`

- `updateSimulationRadiusParallel` agora limita o laço ao menor dos vetores (`ents`, `tf`, `rb`, `dead`, `culled`). Antes indexava `dead[i]` e `culled[i]` sem conferir o tamanho.
- O relatório propunha copiar `Transform` apenas dos corpos cinemáticos no `stepPlay`. O `editor_play.hpp` já faz isso (`!play.rb[i].isKinematic`), então a versão do relatório foi descartada.

## Não aplicado (e por quê)

- **`GlobalTransform` + `syncTransformsParallel` (components.hpp):** removidos. Não tinham nenhum chamador, e a matriz global já é calculada por `worldMatrix` onde é usada. Se um caminho de render precisar de cache de matriz global no futuro, ele deve ser reintroduzido junto com a medição.
- **Plano indireto por quadro (`gpu_renderer.hpp`):** a chamada `planIndirectArgs` em todo quadro foi removida, junto com o membro `opaqueIndirectPlan_` e o accessor, que não tinham chamadores. O RHI não tem draw indireto (`drawIndexed` e `drawIndexedRange` apenas). O helper `indirect_plan.hpp` continua no repositório, coberto por `tests/test_render_helpers.cpp`, para quando houver draw indireto.
- **Streaming de instâncias (`GpuInstanceStreamer`, `indirect_plan`, `gpu_renderer` reescrito):** o relatório reescreve arquivos grandes do renderizador com APIs de Diligent e Light Prism que não foram verificadas contra o repositório. Precisa de build e de medição de GPU.
- **Light Prism (`ClusterActiveMark.compute.hlsl`, early-out em `ClusterCullCount`, shaders GLSL 430):** mudanças de shader sem medição de GPU. Podem ser aplicadas depois que houver um perfil do quadro.

## Próximos passos

1. Medir o quadro na cena real com `--ecs` e `--play` (Zonas Physics e Render), antes e depois.
2. Se a física aparecer como gargalo, aplicar o `GlobalTransform` e o streaming de instâncias, um de cada vez, com build e medição.
3. Revisar os shaders de cluster só com dados de tempo de GPU.

## Threads: um pool, escala com os núcleos

- Antes: dois pools independentes (`tool::ParallelPool` com teto de 8 threads e o `JobSystem` da física com 7 workers), divisão estática com um bloco por thread.
- Agora: um único pool (`tool::ParallelPool`), com teto de 64 threads (`hardware_concurrency - 1` workers). O `JobSystem` da física delega para ele.
- A carga vira `threads × 4` blocos e cada thread pega o próximo bloco livre, então um bloco lento não deixa os outros núcleos parados.
- Exceções em qualquer bloco são capturadas e relançadas no chamador depois que todos os blocos terminam.
- Soft renderer: bandas de 8 para 32 no máximo.
- Limite: a máquina de desenvolvimento tem 2 núcleos. O ganho com mais núcleos precisa de medição no hardware do usuário. Testes: `ctest` 47/47 repetido 40 vezes, `--selftest` 250/250, teste novo `jobs_splits_into_oversubscribed_blocks`.

## Perfil da física com carga real (medido em 2026-11-24)

Benchmark standalone (pilha de esferas caindo sobre o chão, 300 quadros, `Simulate` por quadro; 2 núcleos). Código do benchmark fora do repositório.

| Corpos | Tempo médio/quadro | Pior quadro | Corpos dormindo | Velocidade média | Altura média |
|---:|---:|---:|---:|---:|---:|
| 1000 | ~1,4–1,6 ms | ~4,4–5,0 ms | 175 | 0,0331 | 4,9295 |
| 3000 | ~3,9–4,1 ms | ~7,6–24 ms | 17 | 0,0821 | 6,5622 |

Perfil com `valgrind --tool=callgrind` (3000 corpos, 60 quadros):

- `SolveSingleContact` (solver por cores, 10 iterações): ~51% do tempo.
- `BVH::Query` na broad-phase, dentro de `Step`: ~26%.
- `PreSolve`, `Step` e `Prepare`: ~10%. `MakeShape` e alocação: ~2%.

Observação: a medição `--ecs 500 --play` (0,04 ms) usa uma cena com poucos contatos. Ela não representa carga de pilha.

### Experimento rejeitado: warm starting dos impulsos

Hipótese: reaplicar os impulsos do quadro anterior (por par e posição do ponto de contato) reduziria as iterações necessárias.

Resultado com 3000 corpos, 300 quadros:

| Variante | Tempo médio | Corpos dormindo | Velocidade média | Altura média |
|---|---:|---:|---:|---:|
| Linha de base | ~4,0 ms | 17 | 0,0821 | 6,5622 |
| Escala 0,85 | ~4,0 ms | 0 | 0,1107 | 6,6282 |
| Escala 0,5 | ~4,2 ms | 105 | 0,0486 | 6,5994 |

Decisão: rejeitado. Não houve ganho de tempo e a simulação mudou (com escala 0,85 os corpos nunca dormem). Não foi commitado; o código foi revertido.

Próximo passo com evidência na física: reduzir o custo de `BVH::Query` (por exemplo, consultar só pares `i < j`, ou evitar consultas de corpos que já dormem) e, depois, medir o solver com menos iterações contra a métrica de qualidade acima.

### Experimento rejeitado: broad-phase por travessia árvore contra árvore

Hipótese: trocar uma consulta por corpo ativo por uma travessia dupla (cada par de folhas visitado uma vez) reduziria o custo da `BVH::Query`, que é ~26% do tempo.

Implementação testada: travessia dupla dividida em 64 tarefas fixas (saída determinística, igual com 1 ou N threads), com um teste de equivalência contra a consulta antiga em cenas aleatórias. Todos os testes de pares e de física passaram.

Resultado (3000 corpos, 300 quadros; 2 núcleos):

| Variante | Tempo médio | Instruções (callgrind, 3000×60) | Corpos dormindo | Velocidade média |
|---|---:|---:|---:|---:|
| Consulta por corpo (atual) | ~4,0 ms | ~1,40 bi | 17 | 0,0821 |
| Travessia dupla sequencial | ~5,3 ms | ~1,20 bi | 26 | 0,0395 |
| Travessia dupla em 64 tarefas | ~5,0 ms | ~1,21 bi | 59 | 0,0421 |

Decisão: rejeitado nesta rodada. Caiu o número de instruções, mas o tempo de parede piorou em 2 núcleos e a ordem dos pares muda o resultado da simulação (o solver depende da ordem). Antes de voltar a tentar, medir a broad-phase isolada com mais núcleos, e só então aceitar a mudança no comportamento. Código preservado fora do repositório (patch e teste).

Observação de verificação: o alvo `tool_tests` do preset `test` não compila neste checkout, porque `tests/test_spriteui_rhi.cpp` depende de `SpriteUi/Adapters/ToolEngineRhi.hpp`, que não existe mais no `third_party` (removido em `c4e82e8`). Isso já estava assim antes destas mudanças. Os testes de física foram executados com um runner isolado (`tests/main.cpp` + arquivos de física).
