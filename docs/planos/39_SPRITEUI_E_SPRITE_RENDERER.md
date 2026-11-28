# 39 - SpriteUI: auditoria, referências de outras engines e SpriteRenderer (2026-11-28)

Plano apenas, sem código. Os números vêm de `wc -l` e `du`. A parte 2 vem de conhecimento das engines citadas, sem pesquisa na web nesta rodada.

## 1. Auditoria e otimização do SpriteUI

Panorama (LOC = linhas de .cpp/.hpp/.h/.inc):

| Pasta | Tamanho |
|---|---|
| `third_party/SpriteUi/src` | 68 arquivos, 10,5 mil LOC |
| `include` | 99 arquivos, 8,5 mil LOC |
| `tests` | 66 arquivos, 18,1 mil LOC (mais do que a própria biblioteca) |
| `docs` | ~236 KB |
| `apps/editor` | ~48,8 mil LOC |

Os maiores arquivos do editor:

- `main.cpp`: 4.159 linhas;
- `help_content.hpp`: 2.301 linhas;
- `inspector_panel.hpp`: 2.279 linhas.

Itens, do mais valioso para o menos:

| # | Item | Ganho | Risco |
|---|---|---|---|
| 1 | Fontes Xmind não usadas: 20 TTF, 1,7 MB, em `third_party/SpriteUi/Fonts/Xmind`. O editor usa só `assets/fonts/Xmind-Regular.ttf`. Manter `static/Xmind-Regular.ttf` e `OFL.txt`, ou apontar os 2 caminhos de `apps/editor/CMakeLists.txt` (505 e 512) para `assets/fonts`. | ~1,6 MB | Baixo |
| 2 | Dois renderizadores da UI: `SpriteUiSdlRenderer` (SDL) e `UiGpuBackend`/`UiGpuSink` (GPU, fase 3a/3b). Quando a GPU virar o padrão, o SDL fica só como fallback. Saem o cache por hash (`hashPacket`), `ensureCacheTexture` e as partes SDL do `view_painter.hpp`. | ~400–600 LOC | Médio |
| 3 | Código de entrada repetido nos 18 `*_panel.hpp`: o mesmo `handleEvent` (clique, roda, texto) e o mesmo `draw()` que monta o `FrameInput`. Criar uma base `PanelInput` (ou `PanelHost<View>`) em `frame_input.hpp` e migrar um painel por vez. | ~1,2–1,8 mil LOC | Baixo/médio |
| 4 | Três MenuBars: `Elements/MenuBar.cpp`, `Widgets/MenuBar.hpp` e `Widgets/Legacy/MenuBar.hpp`. O Legacy é quase idêntico ao novo e só o `mainmenu_panel.hpp` o usa. Os testes também duplicam: `test_menubar.cpp`, `test_menu_bar.cpp` e `tests/Legacy`. | ~300–600 LOC | Baixo |
| 5 | Docs de versões antigas: `CHECKLIST_SPEC_V3.md`, `IMPLEMENTATION_STATUS.md`, `REVIEW_V4_2.md`, `V4_2_IMPLEMENTATION_MAP.md` e os 2 .md grandes da raiz. Mover para `docs/arquivo/`. | ~200 KB | Nenhum |
| 6 | Testes muito grandes: `Visual/LogoPixels.inc` (1.038 linhas de dados), `ElementCaptures.cpp` (1.504) e `test_main.cpp` (1.611). Trocar o logo por um PNG pequeno e dividir o `test_main.cpp`. | ~1 mil linhas, build mais rápido | Baixo |
| 7 | `build-vs/` (13 MB) está no `.gitignore`, mas entra no zip. Excluir do pacote. | 13 MB no pacote | Nenhum |
| 8 | `CMakeLists.txt` do SpriteUI (617 linhas): os alvos de teste repetidos viram uma macro `add_sui_test()`. | ~200 linhas | Baixo |

Caminho quente (medir antes de otimizar):

- **Vértices na CPU no backend GPU.** O `UiGpuBackend` gera 4 vértices por quad na CPU. Seria melhor uma instância por sprite, com os vértices gerados no VS. `SpriteInstanceGPU` já tem o layout.
- **Pacote refeito todo quadro.** Cada painel refaz o `FramePacket` e só depois o compara por hash FNV de todo o pacote. Trocar por uma flag de sujo na View, antes de montar o pacote.
- **Strings e vetores alocados por quadro no `view_painter.hpp`:**
  - `TriRun` guarda `std::string image`, e a busca por caminho é num `unordered_map<string>`. Trocar por id numérico, resolvido na carga.
  - `wrapLines` devolve `vector<string>` por texto. Cachear por (texto, largura, tamanho).
  - `measure()` refaz o layout a cada medição. Usar um cache LRU.

## 2. Referências de outras engines

| Prio. | Recurso | Referência | Por quê | Esforço |
|---|---|---|---|---|
| P0 | Sprite Editor com fatiamento por grade e por alfa | Unity Sprite Editor; Godot hframes/vframes, SpriteFrames | Sem ele, animação 2D exige um arquivo por quadro | médio |
| P0 | Sorting layers e ordem dentro da camada | Unity SortingLayer; Godot z_index, CanvasLayer | Ordem de desenho previsível | baixo |
| P0 | Sprite Atlas e batching | Unity Sprite Atlas; Godot AtlasTexture | Poucas draw calls | médio |
| P0 | Clips por quadros, com eventos | Unity Animator; Godot AnimatedSprite2D, AnimationPlayer | Gameplay sincronizado com a animação | médio |
| P1 | 9-slice | Unity Sliced; Godot NinePatchRect | O SpriteUI já tem `NineSlice.hpp` | baixo |
| P1 | Tilemap | Unity Tilemap; Godot TileMap | Base de jogo 2D; já citado no plano 15 | alto |
| P1 | Colliders 2D | Unity Collider2D; Godot CollisionShape2D | `newtoon_2d.hpp` já existe | médio |
| P1 | Pivô, flip X/Y e tint por sprite | Unity SpriteRenderer | Básico, esperado no Inspector | baixo |
| P1 | Temas e containers de UI | Godot Theme, HBox/VBox; Unity UI Toolkit | O SpriteUI tem Flex, mas o editor usa tokens manuais | médio |
| P2 | Câmera pixel perfect | Unity Pixel Perfect Camera | Pixel art sem borrão | baixo |
| P2 | Material e outline por sprite | Unity Sprite-Lit; Godot CanvasItem shader | Identidade visual | médio |
| P2 | Flipbook e colisão gerada do contorno alfa | Unreal Paper2D | Colisão automática | médio |

## 3. Verificação: já existe um componente Sprite?

**Não.** A busca por `SpriteRenderer`, `sorting layer`, `sprite sheet`, `Animator` e `Collider2D` em `engine/`, `apps/` e `docs/` não acha nenhum componente de sprite. O plano 15 já registrava: "Sprite sheets e fatiamento: não existe".

O que existe e pode ser reaproveitado:

- `apps/editor/components.hpp`:
  - `MeshRenderer` (equivalente 3D, serializado como `"mesh"`);
  - `Transform`;
  - `CameraComp`, que tem `ortho2D` e `orthoSize`;
  - `CanvasComp`/`UiElem`, que já tem imagem e `z`, mas em espaço de tela, como UI de jogo;
  - `ParticleFxComp.mode2D`.
- `engine/physics/include/tool/physics/newtoon_2d.hpp`: `Transform2D`, `Rigidbody2D`, `Collider2D` (com `layer` e raycast) e `Step`. Só os testes usam; o editor ainda não.
- Não existe tipo de cena 2D.

### Design proposto

Os componentes ficam em `components.hpp`, ao lado do `MeshRenderer`:

```
SpriteRenderer  { sprite (caminho .sprite ou imagem), frame, color[4], flipX, flipY, pivot[2],
                  sortingLayer, orderInLayer, visible, material opcional }
SpriteAnimator  { clips, clip, time, speed, loop, playing }   // eventos por quadro
Collider2DComp  { shape Box/Circle/Capsule/Polygon, size, offset, isTrigger, layer }
Rigidbody2DComp { Static/Kinematic/Dynamic, mass, friction, gravityScale, freezeRot }
```

- **Serialização:** `"sprite"`, `"spriteAnim"`, `"collider2d"` e `"rigidbody2d"`. Os colliders e corpos 2D mapeiam direto para `newtoon_2d`.
- **Asset `.sprite`** (JSON em `<projeto>/Sprites/`, no padrão de `material_asset.hpp`):
  - imagem de origem;
  - quadros, cada um com retângulo, pivô e 9-slice opcional;
  - clips: nome, quadros, fps, loop e eventos.
- **Render:**
  - chave de ordenação de 64 bits (`layer | order | atlas | índice`), reordenada só quando algo muda;
  - um draw instanciado por atlas e material;
  - culling pela câmera ortográfica;
  - passe depois dos opacos e antes da UI do Canvas.
- **Inspector:** um arquivo novo, `inspector_sprite.hpp`, para não engordar o `inspector_panel.hpp`.
- **Ferramentas:**
  - janela "Sprite Editor": grade, fatiamento por alfa, pivô, 9-slice e linha do tempo do clip;
  - gizmo de collider 2D em `view_overlays.hpp`.
- **Cena 2D:**
  - `Scene.mode = 2D`;
  - câmera ortográfica por padrão;
  - grade e gizmos só em XY;
  - o Z do `Transform` serve para ordenar as camadas;
  - no Play roda só o solver 2D.

### Fases

| Fase | Escopo | Depende de | Validação |
|---|---|---|---|
| F0 | Sorting layers + `SpriteRenderer` com a imagem inteira (soft e GPU), serialização e Inspector básico | câmera ortográfica (já existe) | selftest de serialização; 1.000 sprites a 60 fps; captura comparada com uma referência |
| F1 | Asset `.sprite`, Sprite Editor (grade e alfa) e atlas | F0 | salvar e reabrir idêntico |
| F2 | `SpriteAnimator`: clips, fps, loop e eventos | F1 | teste com dt fixo, cada evento dispara uma vez |
| F3 | Collider e rigidbody 2D com `newtoon_2d`, gizmos, triggers e `OnCollision` | F0 | testes do newtoon 2D; cena de plataforma |
| F4 | Tipo de cena 2D, pixel perfect, instancing e culling | F0–F3 e a UI GPU estável | draw calls e tempo de CPU |
| F5 | Tilemap e 9-slice no sprite | F1 | pintar, salvar e carregar |

## Roadmap combinado

1. **Já (baixo risco):** fontes Xmind não usadas, docs antigos e `build-vs/` fora do pacote. Ganho: ~1,9 MB no repositório e 13 MB no pacote.
2. **Curto prazo:** remover o MenuBar Legacy e criar a base `PanelInput`, migrando um painel por vez (~1,5–2,4 mil LOC a menos).
3. **Em paralelo:** F0 do SpriteRenderer. Medir o hash por quadro e as strings no painter antes de otimizar.
4. **Médio prazo:** F1 a F3 (Sprite Editor, animação e colliders 2D).
5. **Quando a UI GPU for o padrão:** cortar o `SpriteUiSdlRenderer` e o cache por hash, e fazer F4 e F5.
