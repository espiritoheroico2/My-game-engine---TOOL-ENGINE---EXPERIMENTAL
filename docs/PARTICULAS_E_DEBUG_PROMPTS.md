# Partículas 2D/3D, debug visual e gizmos: plano e prompts

Data: 2026-11-19. Branch: `ui-settings-router`.

Este documento separa o trabalho de **partículas, gizmos e desenho de apoio** (grade, ícones, informações) do
trabalho de **janelas**. Enquanto o Claude foca nas janelas (ver `docs/trocarjanelas.md`), você pode tocar esta
parte com outro agente ou com ajuda, seguindo os prompts da seção 6.

---

## 1. Estado atual (verificado no código)

| Parte | Arquivo | Situação |
|---|---|---|
| Simulação de partículas | `apps/editor/fx.hpp` (`Editor::updateParticles`, `EmitterJob`) | Feita em CPU. Lotes por cor, até 64 por vista (ver `docs/PARTICLES.md`) |
| Desenho das partículas | `apps/editor/fx.hpp` (`drawParticleOverlay`) | **ImGui** (`ImDrawList`), 15 usos |
| Grade, ícones, física, objetos ocultos | `apps/editor/view_overlays.hpp` (`buildGrid`, `buildSceneIcons`, `buildPhysics`, `buildCulled`) | **Sem ImGui**. Gera formas em `view::ShapeList` |
| Gizmos da janela Scene | `apps/editor/viewport_tools.hpp` | **ImGui** (3 usos) |
| Overlay de UI de jogo na Scene | `apps/editor/ui_contract.hpp` | **ImGui** (4 usos) |
| Iluminação | `third_party/LightPrism`, `docs/LIGHT_PRISM.md` | Ativa nos dois renderizadores (CPU e GPU) |
| Partículas em 3D (malha/quads com luz) | não existe como sistema separado | Hoje as partículas são overlay 2D sem profundidade real |

Pontos que já sabemos:
- `docs/PARTICLES.md` diz que cada partícula era um draw call até a etapa anterior; agora há lotes.
- As partículas atuais são desenhadas **depois** da cena, como overlay. Por isso não sofrem sombra nem oclusão.
- `ParticleComp` (em `apps/editor/components.hpp`) já tem rota, tornado, área, cores e opacidade.

---

## 2. O que você pediu (objetivos)

1. **Dois tipos de partícula:**
   - **2D com SpriteUI:** partículas desenhadas pela SpriteUI, mas **espaciais**: ficam em posição 3D no mundo, são
     projetadas pela câmera, têm tamanho por distância e ordem de profundidade.
   - **3D com Light Prism:** partículas como quads/malhas que recebem luz e sombra do Light Prism.
2. **As duas bibliotecas conversam:** o mesmo emissor (dados e simulação) alimenta os dois caminhos. A troca de
   código existe para desempenho: a simulação é uma só; cada caminho só desenha.
3. **Remover o ImGui dos gizmos e dos elementos de desenho** (grade, informações, gizmos, overlays), criando classes
   na SpriteUI:
   - **Debug visual** (`DebugDraw`): grade, caixas, colisores, ícones, rótulos, informações.
   - **Gizmos** (`Gizmos`): mover, girar, escalar, eixos; usa o `DebugDraw`.
   - **Canvas de desenho** (`Canvas2D`, estilo *GameMaker draw* e *Turtle do Python*): desenho por comandos, com
     cálculo e funções nativas (linha, retângulo, círculo, texto, polígono), e modo "tartaruga" (andar, girar, caneta).

---

## 3. Arquitetura proposta (para aprovar antes de codar)

### 3.1 Núcleo compartilhado (sem SDL, sem ImGui, sem SpriteUI)
Arquivo sugerido: `engine/fx/include/tool/fx/particles_core.hpp` (ou `apps/editor/fx_core.hpp` se ficar no editor).
- `EmitterDesc`: taxa, vida, velocidade, cone, área, rota, tornado, cores, tamanho, opacidade.
- `ParticleSoA`: posição, velocidade, idade, vida, cor, tamanho (arrays separados, para cache).
- `Simulate(desc, state, dt, rng)`: passo de simulação. Função pura, testável sem janela.
- `Cull(state, frustum)`: esfera contra os 6 planos (já existe em `fx.hpp`; mover para cá).
- `SortByDepth(state, camera)`: ordem de profundidade para o desenho transparente.

Testes: `tests/test_fx_core.cpp` (mesmo padrão de `Editor.InspectorTags`): taxa, vida, rota, culling, ordem.

### 3.2 Caminho 2D (SpriteUI), espacial
Arquivo sugerido: `apps/editor/fx_spriteui.hpp`.
- Para cada partícula viva: projeta a posição 3D com a câmera. Se atrás da câmera, descarta.
- Tamanho na tela = tamanho do mundo × escala da projeção / profundidade.
- Monta **um** lote por cor (como hoje), com quads na SpriteUI, na ordem de profundidade.
- Limitação: a SpriteUI é 2D. A partícula **não é ocluída por objetos 3D** (só por ordem). Isso precisa ser dito no
  texto da janela. Se for preciso oclusão, use o caminho 3D.

### 3.3 Caminho 3D (Light Prism), com luz
Arquivo sugerido: `engine/render/include/tool/render/particles_lightprism.hpp`.
- Partícula como quad voltado para a câmera (billboard), com textura e cor.
- Recebe luz e sombra do Light Prism (usa o mesmo `LightRig` de `docs/LIGHT_PRISM.md`).
- Passa pelo teste de profundidade normal; é ocluída pelos objetos.
- Desempenho: um lote instanciado por emissor (ou por cor), no máximo um desenho por lote.
- **A confirmar no código**: se o Light Prism expõe um caminho de partículas/instância; se não, o lote usa o
  pipeline de malha dinâmica já existente (`fx.hpp`).

### 3.4 Troca de código entre as bibliotecas
- A simulação e os dados ficam **só** no núcleo (3.1). Os dois caminhos recebem o mesmo `ParticleSoA`.
- Quando uma partícula muda de caminho (ex.: o usuário troca o tipo do emissor), nada é recalculado: o estado é
  o mesmo, só o desenho muda.
- Para desempenho: a ordenação e o culling são feitos uma vez e reaproveitados pelos dois caminhos.

### 3.5 Classes de debug e gizmos (SpriteUI)
Arquivos sugeridos: `apps/editor/debug_draw.hpp`, `apps/editor/gizmos.hpp`, `apps/editor/canvas2d.hpp`.

**`DebugDraw`** (debug visual):
- `Grid(cam, step, color)`, `Box(min, max, color)`, `Sphere(c, r)`, `Collider(...)`, `Label(pos, text)`, `Info(text)`.
- Tudo vira formas de `view::ShapeList` (como `view_overlays.hpp` já faz); a SpriteUI desenha o lote.

**`Gizmos`** (usa `DebugDraw`):
- `Move(pos, axes)`, `Rotate(pos, rings)`, `Scale(pos, handles)`; devolve o que o usuário arrastou (eixo, delta).
- Mesmo modelo do teste de view (`Editor.EditorInfoView`): clique sintético em cada alça.

**`Canvas2D`** (estilo GameMaker / Turtle):
- Estado: caneta (cor, espessura), posição, ângulo, pilha de transformações (`Push`/`Pop`).
- Comandos nativos: `Line`, `Rect`, `Circle`, `Polygon`, `Text`, `Image`.
- Modo tartaruga: `Forward(d)`, `Left(a)`, `Right(a)`, `PenUp()`, `PenDown()`, `GoTo(x,y)`.
- Cálculo: `Lerp`, `Bezier`, `Sin/Cos` na própria classe, para curvas de rota e de trajetória.
- Sai como um lote da SpriteUI (uma chamada de desenho por cor/textura).

### 3.6 Remover o ImGui dessas partes
Ordem sugerida (uma por vez, com teste de view e print, como em `trocarjanelas.md`):
1. `view_overlays.hpp` já está sem ImGui: manter.
2. `viewport_tools.hpp` (gizmos da Scene): trocar por `Gizmos`.
3. `ui_contract.hpp` (overlay de UI de jogo na Scene): trocar por `DebugDraw::Label` / `Canvas2D::Rect`.
4. `fx.hpp` (partículas): trocar por `fx_spriteui.hpp` (2D) e `particles_lightprism.hpp` (3D).
5. Só depois: remover `imgui` do CMake, do `main.cpp` e de `third_party` (ver `docs/trocarjanelas.md`, seção 4.6).

---

## 4. Critérios de aceite (padrão do projeto)

Cada peça segue as 7 etapas de `docs/trocarjanelas.md`:
- **Modelo:** teste sem janela (`test_fx_core`, `test_gizmos_model`, `test_canvas2d`).
- **Esqueleto visual:** print headless (`--screenshot`).
- **Testes de elemento:** clique em cada alça, traço, texto.
- **Backend:** ligado no editor.
- **Print e desempenho:** draw calls por quadro (antes e depois) registrados no doc.
- **Teste visual seu:** você aprova; só então o ImGui antigo sai.

Metas numéricas sugeridas (a confirmar):
- Partículas 2D: um lote por cor; no máximo 64 por vista (como hoje).
- Partículas 3D: um desenho por emissor (ou por cor).
- Gizmos e grade: um lote por quadro.

---

## 5. Riscos e perguntas abertas (decida antes de codar)

1. **2D espacial sem oclusão** (SpriteUI). Aceitável? Ou o 2D só para UI/efeitos sem profundidade?
2. **Light Prism e partículas**: a biblioteca tem caminho de instância para partículas, ou fazemos malha dinâmica?
3. **Canvas2D**: a API será estilo `draw_line(...)` (GameMaker) ou `forward(...)` (Turtle)? Quero as duas, por
   camada: comandos e tartaruga.
4. **Estado dos gizmos ImGui**: o ImGui é usado para arrastar (interação). A nova classe precisa de eventos próprios
   (já há `PanelRouter` e `UIEventRouter` na SpriteUI).

---

## 6. Prompts prontos (copie e cole)

**P1. Núcleo de partículas (sem janela)**
> Crie `particles_core.hpp` com `EmitterDesc`, `ParticleSoA`, `Simulate`, `Cull` e `SortByDepth`, sem SDL, ImGui
> nem SpriteUI. Mova a lógica de `apps/editor/fx.hpp` (`updateParticles`, `EmitterJob`, `areaOffset`, `routeSample`,
> `particleColor`, `particleAlpha`) para cá. Escreva `tests/test_fx_core.cpp` (taxa, vida, rota, culling, ordem)
> seguindo `test_inspector_tags.cpp`. Não mude o comportamento visível. Rode `ctest -R Editor`.

**P2. Partículas 2D espaciais (SpriteUI)**
> Crie `fx_spriteui.hpp`: projete cada partícula do `ParticleSoA` com a câmera, descarte as que estão atrás,
> calcule tamanho por distância e monte um lote por cor na SpriteUI, em ordem de profundidade. Substitua o
> `drawParticleOverlay` (ImGui) de `fx.hpp`. Documente que o 2D não é ocluído por objetos 3D. Teste: projeção,
> atrás da câmera, ordem e número de lotes.

**P3. Partículas 3D (Light Prism)**
> Leia `docs/LIGHT_PRISM.md` e `third_party/LightPrism`. Descubra se há caminho de partículas/instâncias. Crie
> `particles_lightprism.hpp` com billboards que recebem luz do `LightRig`, usando o mesmo `ParticleSoA` do núcleo.
> Diga no relatório se precisou de malha dinâmica e quantos desenhos por quadro saem.

**P4. Classe de debug visual (`DebugDraw`)**
> Crie `debug_draw.hpp` com `Grid`, `Box`, `Sphere`, `Collider`, `Label` e `Info`, gerando `view::ShapeList` (como
> `view_overlays.hpp`). Troque o ImGui de `ui_contract.hpp` pelos rótulos. Teste: a grade gera linhas; os rótulos
> saem nas posições certas.

**P5. Gizmos (`Gizmos`)**
> Crie `gizmos.hpp`: `Move`, `Rotate` e `Scale`, com alças e arraste que devolvem eixo e delta. Troque os gizmos de
> `viewport_tools.hpp`. Teste de view com clique sintético em cada alça (padrão de `test_editor_info_view.cpp`).

**P6. Canvas2D (GameMaker / Turtle)**
> Crie `canvas2d.hpp`: caneta (cor, espessura), comandos `Line`, `Rect`, `Circle`, `Polygon`, `Text`, e modo
> tartaruga `Forward`, `Left`, `Right`, `PenUp`, `PenDown`, `GoTo`, com `Push`/`Pop` e funções `Lerp` e `Bezier`.
> Saída: um lote SpriteUI por cor. Teste: após `Forward(10)` e `Left(90)` a posição e o ângulo estão certos; `Pop`
> restaura a transformação.

---

## 7. Onde isso fica no projeto

- Este documento: `docs/PARTICULAS_E_DEBUG_PROMPTS.md`.
- Partículas hoje: `docs/PARTICLES.md`, `apps/editor/fx.hpp`, `apps/editor/components.hpp` (`ParticleComp`).
- Luz: `docs/LIGHT_PRISM.md`.
- Processo de janelas (não mexer aqui sem combinar): `docs/trocarjanelas.md`.
- Testes do editor: `apps/editor/tests/`, rodar com `ctest -R Editor`.
