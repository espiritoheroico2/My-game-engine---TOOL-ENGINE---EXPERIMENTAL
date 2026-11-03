# SpriteUi: plano de implementacao (0.7.8)

Base: documento "SpriteUi: Arquitetura e Engenharia (Especificacao Revisada V2.0)".
Este arquivo e o PLANO por fases. Nada disso esta implementado nesta versao: o editor continua com ImGui
e a engine com `UiCanvas` (componente de UI de jogo, `docs/UI.md` e `docs/UI_FX.md`).

## 1. Decisoes antes de codar

Pontos do documento que nao batem com o projeto, e como tratamos:

| Ponto do documento | Situacao no ToolEngine | Decisao |
|---|---|---|
| Base ECS em Flecs | O projeto tem ECS proprio (`engine/ecs`, `docs/ECS.md`). Flecs nao e dependencia. | Usar o ECS proprio. Nao adicionar Flecs so por causa da UI. Registrar a decisao em ADR. |
| HarfBuzz e FreeType | Nao estao em `docs/THIRD_PARTY.md`. | Revisao de licenca antes de vendorizar (ambas sao permissivas/compativeis, mas precisam entrar na tabela). |
| API QuickJS `UI.createPanel`, `UI.bind` | `UiCanvas` ja tem API de script e componentes em uso. A API do documento e outra. | Migrar `UiCanvas` como camada de compatibilidade. Nao quebrar cenas existentes. |
| "Uma draw call para milhares de elementos" | O proprio documento corrige isso (V2.0). | Meta: reduzir draw calls por material/textura/clip, sem quebrar ordem visual. Medir antes de prometer numero. |
| Tudo POD no ECS | O documento permite handles para strings e dados variaveis. | Igual ao documento: handles/pools para texto e dados variaveis. |

## 2. Fases

Cada fase termina com testes passando e documentacao atualizada (CHANGELOG, CHECKLIST, README).

### Fase 0: decisoes (sem codigo de runtime)
- ADR: ECS proprio no lugar de Flecs.
- Revisao de licenca de HarfBuzz e FreeType; atualizar `docs/THIRD_PARTY.md`.
- Escolher o alvo da primeira UI: o editor (menos risco) ou a engine (mais impacto).

### Fase 1: nucleo (layout e transformacao)
- Componentes `UIRectTransform`, `UILayoutComponent`, `UIComputedLayout` no ECS proprio.
- Ancoras (`Rmin = P0 + anchorMin * parentSize + offsetMin`), Measure (bottom-up) e Arrange (top-down).
- Flex (grow/shrink) e Grid. Nine-slice com a regra de reducao proporcional.
- Testes: matematica (ancoras, flex, grid) e snapshots de arvore em varias resolucoes e DPI.

### Fase 2: renderizacao de sprites
- `UIRenderItem` gerado na extracao (imutavel), ordenacao por layer/siblingOrder, batch por material, textura, clip e blend.
- Instancia compacta `SpriteInstanceData`, alfa pre-multiplicado.
- Clipping por retangulo e cantos arredondados.
- Backends: Vulkan e GL pelo Diligent. A CPU ignora o shader, como o material ja faz hoje.
- Testes: visual regression (comparacao de pixels) e contagem de draw calls num cenario fixo.

### Fase 3: texto
- Fase 3a: FreeType com atlas MSDF e fallback. Layout de quebra de linha e alinhamento.
- Fase 3b: HarfBuzz para shaping (ligaduras, scripts complexos). So entra depois da 3a estar medida.
- IME, undo e redo ficam para depois (edicao de texto ativa).

### Fase 4: widgets e migracao do editor
- Widgets basicos: painel, botao, rotulo, slider, campo de texto, lista.
- Migrar o editor de ImGui para SpriteUi uma tela por vez (Console, Hierarchy, Inspector, Assets, Settings), mantendo o ImGui como fallback ate cada tela estar validada.
- Manter o selftest verde a cada tela migrada.

### Fase 5: scripting e UiCanvas
- API de script para criar e alterar a UI (`UI.createPanel`, `UI.bind` do documento), com typings no `toolengine.d.ts` (gerados junto com o autocomplete do VS Code).
- `UiCanvas` continua funcionando; a migracao e opcional e automatica quando possivel.

### Fase 6: escala e dados
- Virtualizacao de lista e DataGrid (`first`, `last`, overscan).
- Data binding reativo (OneWay, TwoWay, OneTime).
- Stress test: 100.000 itens em DataGrid com virtualizacao, medindo `T_UI_CPU = T_input + T_state + T_layout + T_text + T_extract + T_upload`.

### Fase 7: acessibilidade e mundo 3D
- Arvore semantica, foco por teclado e gamepad.
- Contraste com conversao sRGB para linear no Theme Editor.
- UI no mundo (billboard e raycast para interacao).

## 3. Criterios de aceite por fase
- Nenhuma regressao no selftest da engine e do editor.
- Medicao antes e depois (Profiler, zona de UI), sem afirmar ganho sem numero.
- Cena existente com `UiCanvas` continua abrindo e renderizando igual.

## 4. Fora do escopo desta versao
- Substituir o ImGui de uma vez.
- Flecs.
- Prometer draw call fixa.
