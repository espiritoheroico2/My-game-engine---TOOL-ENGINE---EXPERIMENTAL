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

- **`GlobalTransform` + `syncTransformsParallel` (components.hpp):** não é chamado em lugar nenhum. Entra quando houver um caminho de render que consuma `GlobalTransform`.
- **Streaming de instâncias (`GpuInstanceStreamer`, `indirect_plan`, `gpu_renderer` reescrito):** o relatório reescreve arquivos grandes do renderizador com APIs de Diligent e Light Prism que não foram verificadas contra o repositório. Precisa de build e de medição de GPU.
- **Light Prism (`ClusterActiveMark.compute.hlsl`, early-out em `ClusterCullCount`, shaders GLSL 430):** mudanças de shader sem medição de GPU. Podem ser aplicadas depois que houver um perfil do quadro.

## Próximos passos

1. Medir o quadro na cena real com `--ecs` e `--play` (Zonas Physics e Render), antes e depois.
2. Se a física aparecer como gargalo, aplicar o `GlobalTransform` e o streaming de instâncias, um de cada vez, com build e medição.
3. Revisar os shaders de cluster só com dados de tempo de GPU.
