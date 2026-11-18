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
Inspector > **Add Physics** (Static/Dynamic/Kinematic, massa, atrito, quique, CCD, gatilho, **Vector physics**); **Tools** (Jump, Move, Follow Target, Spin, On Collision, Script library); Settings > **Physics** (gravidade do mundo em X/Y/Z, iterações, passos/s); janela **Input Map**. Colliders são desenhados na Scene. O Play guarda um snapshot da cena e restaura ao parar. O Profiler inclui a zona **Physics simulation** para medir `PhysicsWorld::Simulate`.

Quando nao ha corpos dinamicos ativos nem corpos cinematicos em movimento e as entradas da forma nao mudaram, o passo limpa os acumuladores e eventos de saida, mas pula a preparacao de formas e a reconstrucao da BVH. Mudancas externas de transform/collider ou na quantidade de corpos invalidam esse cache. Contatos esfera-caixa escrevem no buffer reutilizavel; as matrizes de inercia no mundo sao preparadas somente para corpos presentes nos contatos. Esferas sem offset rotacionado nao precisam construir uma matriz de rotacao para a forma.

Testes: 14 em `tests/test_physics.cpp`, incluindo 500 esferas adormecidas, collider esferico com offset rotacionado e atualizacao de transform durante passo ocioso; o selftest do editor valida o ciclo do spawner com 500 esferas e a limpeza ao parar.
