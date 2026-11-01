# ECS do ToolEngine

Arquivo principal: `engine/ecs/include/tool/ecs/world.hpp` (namespace `tool`). Handles: `engine/core/include/tool/core/handle.hpp`.
Usado pelo editor (`apps/editor`) e pela cena (`engine/scene`). A interface foi escrita para poder trocar o backend por Flecs sem mudar o resto do código.

## 1. Conceitos

| Conceito | O que é | Onde |
|---|---|---|
| **Entity** | Um `Handle` = `{index, generation}`. O índice é reutilizado; a geração muda a cada reuso, então um handle antigo vira "obsoleto" (`alive()` retorna falso). | `tool::Entity` |
| **Component** | Um struct simples (POD) guardado por tipo. Cada tipo tem um "storage" próprio. | `components.hpp` do editor, `tool::Light` etc. |
| **Storage** | Conjunto esparso: `sparse[índice] → posição`, `data[]` denso com os componentes. Adicionar/remover é O(1) (remoção troca com o último). | `World::Storage<T>` |
| **Tag** | Marcador de até **128** tags por mundo (`TagId`), guardado como máscara de bits por entidade. | `tool/core/tags.hpp` |
| **Dormant** | Entidade "dormente": existe, mas some das iterações. Usada pelo `DeleteAndReuse` dos scripts. | `setDormant` |

Não há **arquétipos** (chunks de entidades com o mesmo conjunto de componentes). Cada tipo de componente é um array independente.

## 2. API

```cpp
#include "tool/ecs/world.hpp"
tool::World w;

tool::Entity e = w.create();            // nova entidade
w.add<Transform>(e, Transform{...});   // adiciona (ou substitui) um componente; devolve referência
bool ok = w.has<Transform>(e);
Transform* t = w.get<Transform>(e);     // nullptr se não tiver ou se a entidade estiver morta
w.remove<Transform>(e);
w.destroy(e);                           // apaga a entidade e TODOS os componentes/tags dela

w.addTag(e, tagId); w.hasTag(e, tagId); w.removeTag(e, tagId);
w.tagMask(e);                           // máscara completa

// Iteração (por componentes): visita entidades que têm TODOS os tipos listados
w.each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& m, Transform& t) { ... });

// Iteração por tags
w.eachTagged(mask, [&](tool::Entity e) { ... });

// Todas as entidades vivas e visíveis (painel Hierarquia)
w.eachEntity([&](tool::Entity e) { ... });
```

Regras práticas:
- `add` com o mesmo tipo **substitui** o valor. Guarde o ponteiro de `get`/`add` só até a próxima mudança do storage (o `std::vector` pode realocar).
- `each` **copia** a lista de índices antes de percorrer, então a função pode criar ou apagar entidades sem quebrar o laço. O custo é uma cópia por chamada (ver seção 5).
- Ao destruir um objeto no editor, use `Editor::destroy` (apaga filhos e o script). `world.destroy` é a operação de baixo nível.

## 3. Como o editor usa o ECS

- Cada **ToolObject** é uma entidade. Componentes: `Name`, `Transform`, `MeshRenderer`, `LightComp`, `CameraComp`, `AudioSource`, `PhysicsComp`, `ToolsComp`, `ScriptComp` (caminho do script), `UiCanvas`, `ParticleComp`, `Parent`, `Inactive` (objeto desativado).
- `Parent{e}` faz a hierarquia. O `Transform` de um filho é **local**; o mundo é pai × local.
- Na cena (`.scene.json`) só entram componentes que o `saveEntity` conhece. Componentes de **execução** (como `SpawnerComp` do teste de ECS) não são salvos.
- No Play, a física (Newtoon) tem arrays próprios (`play.ents`, `play.tf`, `play.rb`, `play.col`) e um mapa `indexOf[entidade] → corpo`. Corpos criados no Play entram com `Editor::pushRuntimeBody`.
- O Play é protegido por **snapshot**: ao parar, a cena volta ao estado de antes (inclusive objetos criados durante o Play).

## 4. Como adicionar um sistema novo

1. Crie o componente (struct simples, sem ponteiros para o mundo).
2. Se ele precisa ser salvo, adicione `saveEntity`/`loadEntity` em `components.hpp`.
3. Escreva o sistema como função que chama `w.each<...>` uma vez por quadro, dentro do `tick` do editor (ordem: Narrative → scripts → spawners → física → render).
4. Se o sistema cria entidades no meio do laço, junte os handles numa lista e aplique depois (como a fila de comandos dos scripts).

## 5. Desempenho: o que é verdade hoje

Medições deste documento são **estimativas de código**, não benchmarks. Os números reais saem do comando `test ECS: N` (Console) e do Profiler.

- **Bom:** cada tipo fica contíguo em memória (`data[]`). Um sistema que usa **um** componente percorre um array denso.
- **Custa:** `each<A, B>` percorre o storage **menor** e, para cada entidade, faz busca esparsa nos outros. Com muitos componentes, isso espalha os acessos pela memória.
- **Custa:** `each` copia o vetor de índices a cada chamada (uma alocação por sistema por quadro). Com 10 mil entidades e vários sistemas, isso aparece no Profiler.
- **Custa:** `eachTagged` percorre **todos** os índices até o maior já usado.
- **Custa:** o painel Hierarquia percorre todas as entidades a cada quadro; com 10 mil filhos, a árvore fica pesada mesmo fechada.

## 6. "Todo ToolObject como entidade otimizada" e "mesma tag + mesmo código = uma entidade só"

Resposta direta: **a parte de "uma entidade só para muitos objetos" não é viável como você descreveu**, e o motivo importa.

- Cada objeto tem estado próprio: posição, corpo físico, script, estado de UI, pai. Juntar 10 mil objetos numa "entidade" perderia esse estado individual.
- Um script por objeto custa uma **contexto JS** (memória e tempo de criação). Hoje cada objeto com script tem o seu contexto (isolamento). Com 10 mil objetos com script, isso não cabe: o caminho é **um contexto por arquivo de script** e uma tabela de estado por objeto.

O que **é viável** e dá o ganho que você procura:

1. **Lote por mesh + material (instancing).** Objetos iguais viram **um** desenho instanciado na GPU (uma chamada, um array de matrizes). É o ganho de "mesma coisa, muitas vezes", sem perder os objetos individuais. Ainda não existe no renderer.
2. **Arquétipos/chunks.** Entidades com o mesmo conjunto de componentes ficam em blocos contíguos. Isso melhora a iteração com vários componentes (hoje há busca esparsa). É uma mudança grande no `World`, mas a API pública pode continuar igual.
3. **Contexto de script compartilhado por arquivo.** Mesma lógica → um contexto; estado por objeto → tabela.
4. **Tags como consulta.** Já existe (`eachTagged`); falta um índice por tag para não percorrer tudo.

Ordem sugerida (do mais barato ao mais caro): (a) `each` sem cópia quando a função não cria/apaga; (b) lote de instancing para meshes repetidas; (c) chunks; (d) contexto de script por arquivo.

## 7. Teste de eficiência

`test ECS: N` no Console (padrão 10 000, máximo 1 000 000):
- Cria o objeto `ECScall` e as N esferas como filhas dele, nascendo no céu em alturas e posições aleatórias, com Newtoon (colisor de esfera).
- Cria `perFrame = ceil(N / 240)` esferas por quadro (~4 s no total), não todas de uma vez.
- Ao terminar, registra no Console o tempo de criação e o total de corpos. O Profiler mostra o tempo de cada zona por quadro.
- Parar o Play (Stop) remove todas as esferas (snapshot).

Para comparar: rode com N = 10 000, 50 000 e 100 000 e anote o FPS médio e o tempo da zona **Physics** e **Scene render** no Profiler.
