# Física (Newtoon integrada)

Origem: **Newtoon Physics** (MIT, João Vitor Dias da Silva). O repositório original era um esqueleto (~500 linhas, sem colisão; cabeçalhos salvos como `.cpp`). Foi **completado** em `engine/physics/include/tool/physics/`:

| Arquivo | Conteúdo |
|---|---|
| `newtoon_math.hpp` | Vec3, Quat, Mat3 |
| `newtoon_types.hpp` | Rigidbody, Collider, materiais, eventos, RaycastHit, **vector physics** (`overrideGravity`) |
| `newtoon_bvh.hpp` | BVH reconstruída por passo; consultas de caixa e raio |
| `newtoon_jobs.hpp` | JobSystem com pool de threads persistente (o original criava threads a cada chamada) |
| `newtoon_world.hpp` | Passo fixo, esfera/caixa (SAT), solver de impulsos com atrito de Coulomb e restituição, sono, CCD, gatilhos, eventos Enter/Stay/Exit, raycast |
| `newtoon_modules.hpp` | Verlet (ossos/tecido), suspensão de veículo por raycast, ragdoll (controle) |

## Fora do escopo desta versão (honesto)
Cilindro e malha convexa (tratados como caixa), solver de **juntas** (só a estrutura de dados), 2D (o original só tinha integração 2D; não foi ligado ao editor), ragdoll com juntas reais.

## No editor
Inspector > **Add Physics** (Static/Dynamic/Kinematic, massa, atrito, quique, CCD, gatilho, **Vector physics**); **Tools** (Jump, Move, Follow Target, Spin, On Collision, Script library); Settings > **Physics** (gravidade do mundo em X/Y/Z, iterações, passos/s); janela **Input Map**. Colliders são desenhados na Scene. O Play guarda um snapshot da cena e restaura ao parar.
Testes: 11 em `tests/test_physics.cpp` + 12 no selftest do editor.
