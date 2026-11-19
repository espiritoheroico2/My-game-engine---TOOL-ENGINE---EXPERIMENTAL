# Trocar janelas ImGui por SpriteUI: processo padrão

Objetivo: remover o Dear ImGui do editor e usar só SpriteUI. Este documento é o processo a repetir,
janela por janela, e a lista do que falta. Quem for fazer o backend de uma janela segue a seção 2.

Estado de referência: branch `ui-settings-router`. Janela piloto: **Editor info** (`apps/editor/editor_info_spriteui.hpp`).

---

## 1. Processo (uma janela por vez, nesta ordem)

Cada janela passa pelas mesmas 7 etapas. Não avance uma etapa sem a anterior verde.

| # | Etapa | Entrega | Critério para passar |
|---|---|---|---|
| 1 | **Modelo** | `<janela>_model.hpp`: estado e regras, sem desenho nem ImGui | Teste de modelo (`tests/test_<janela>_model.cpp`) passa |
| 2 | **Esqueleto visual** | `<janela>_spriteui.hpp`: View com moldura, textos, campos, botões e ícones. Dados fixos ou vindos do modelo | Print gerado sem erro (captura headless) |
| 3 | **Testes de elementos** | `tests/test_<janela>_view.cpp`: clique em cada botão, X, grip, campo, foco, digitação e batches | Todos os checks passam |
| 4 | **Backend** | Ligar ações ao editor (salvar, abrir, selecionar etc.) e o host (`main.cpp`) | Teste de modelo e de ação passam |
| 5 | **Print + desempenho** | Captura do editor (`--screenshot`) e contagem de draw calls | Print confere; batches registrados |
| 6 | **Teste visual** | Você testa no seu PC e registra no `CODAR.MD` | Você aprova |
| 7 | **Remover a antiga** | Apagar a janela ImGui e a chamada no `main.cpp` | Só depois da etapa 6 aprovada |

Regra: **nenhuma janela ImGui é removida sem a etapa 6 aprovada.** Durante a migração, a ImGui e a SpriteUI
convivem; o menu escolhe qual abrir.

### 1.1 Padrão da moldura (toda janela)

Toda janela SpriteUI tem, nesta ordem, da esquerda para a direita, no cabeçalho:
1. **Grip** (ícone de arrastar, 14 px). Só o grip inicia o arraste e o docking.
2. **Ícone de ajuda "?"** (círculo, à direita do nome). Ao clicar, abre a janela Help na seção desta janela.
   *Status: pendente (ver seção 4).*
3. **Título** em negrito e um pouco maior que o corpo (highlight). *Status: título atual é normal; negrito pendente.*
4. **Minimizar** e **X** (fecham a janela; X dispara `Action::Close`).

A moldura vem de `SpriteUi::Widgets::WindowFrame` (`BuildWindowFrameItems`, `WindowFrameHeader`,
`WindowFrameTitleRect`, `WindowFrameButtonRect`, `WindowFramePartAt`). O título e o grip são desenhados
pela janela (a moldura não os desenha). Cores e altura ficam na view, em `buildTree()`.

---

## 2. Como fazer o backend de uma janela (passo 4)

1. A view só reporta ações (`enum class Action`). Ela não grava nada.
2. O host (`main.cpp`) recebe a ação, chama o modelo e grava. Exemplo: `Action::Save` chama
   `model.applyTo(settings)` e depois `settings.save(project)`.
3. O modelo não depende de SpriteUI nem de ImGui. Teste-o sem janela.
4. Use a mesma assinatura de `View::Frame(w, h, FrameInput, actions)` em toda janela.

---

## 3. Testes padrão

Cada janela tem dois testes automáticos:
- **Modelo**: regras (ex.: `missing()`, limite de bytes, `applyTo`).
- **View**: clique sintético em cada elemento via `View::Frame`. Modelo de referência:
  `apps/editor/tests/test_editor_info_view.cpp`. Os helpers `Run` e `Click` servem para qualquer janela.

Elementos que todo teste de view cobre:
- botões (clique dispara a ação certa)
- X (dispara `Close`)
- grip (arrasta a janela; o mesmo arraste no conteúdo não arrasta)
- campo de texto (clique foca; digitar entra no campo)
- clique fora (nenhuma ação)
- contagem de batches (desenha algo)

Comando: `ctest -R EditorInfo`. Resultado da janela piloto: 9 checks passam (2026-11-19).

---

## 4. Pendências (lista para outro agente ou para a próxima etapa)

### 4.1 Janela Settings (prioridade alta)
- **Sintoma relatado:** o menu interno de Configurações aparece vazio; as opções e os dados sumiram.
- **Reproduzido no print headless (2026-11-19):** as abas (Settings, Game, Lighting) aparecem e o corpo fica vazio.
- **Causa encontrada (2026-11-19):** o editor abria na aba 0 (Appearance), que não desenha nada. Os controles
  (API, escala, dropdown, caixas) só existem na aba 1 (Settings). **Correção feita:** abre na aba 1.
- **Ainda vazias na SpriteUI:** Appearance, Game, Lighting, Optimizing e Language. A migração deve trazer
  o conteúdo que existia na versão ImGui (`main.cpp`, função do Settings, ~linhas 2027-2050). Hipótese a verificar: a ponte `settings_bridge.hpp` não recebe os itens
  depois da troca de aba, ou a lista de opções não é preenchida no quadro. Verificar com um teste de
  modelo (`Editor.SettingsModel`, `Editor.SettingsBridge`) e um print.
- Refazer a janela Settings do zero no padrão deste documento (seção 1), e não só corrigir.

### 4.2 Campos de texto
- **Cursor de mouse:** ao passar sobre um campo, o cursor deve virar I-beam (`SDL_SYSTEM_CURSOR_TEXT`).
  Hoje não muda. Ver `start_screen_spriteui.hpp` (já há `SDL_SetCursor` na tela principal, reaproveitar).
- **Selecionar arrastando:** hoje o clique só posiciona o caret. Falta: pressionar, arrastar e soltar
  selecionando vários caracteres (chamar `SetCaret(offset, extend=true)` durante o arraste).
- **Campo multilinha:** a descrição do ImGui é multilinha; a view atual é uma linha.

### 4.3 Ícone de ajuda "?" e janela Help
- Círculo com "?" ao lado do nome da janela. Clique envia `Action::Help` com o id da janela.
- A janela Help (`help_panel.hpp`) deve abrir na seção da janela clicada.
- Falta desenhar o círculo: usar um item com `cornerRadius` igual à metade do lado.

### 4.4 Título em negrito e maior
- Hoje o título usa o mesmo tamanho do corpo com a fonte regular (`Xmind-Regular.ttf`).
- Falta uma fonte em negrito (ou um traço de contorno) e um tamanho maior (20 px sugeridos).

### 4.5 Renderização CPU/GPU e draw calls
- **Medido (2026-11-19):**
  - Editor info: 17 lotes por quadro antes; **4 depois** (206 instâncias). Causas corrigidas: `clipOverflow`
    em campo e painel criava um clip por nó (quebra o lote), e o clip do texto só existe quando o texto
    passa da caixa.
  - Editor inteiro, modo software (`--frames 120`): **21,9 `SDL_RenderGeometry` da SpriteUI por quadro**.
    O número é impresso no fim da execução.
- **Medição só vale com as janelas SpriteUI criadas.** Antes, `--frames` desligava todas (`fontReady` exigia
  `!headless`), então os prints antigos não tinham janela SpriteUI nenhuma. Corrigido: com `--screenshot`
  as janelas SpriteUI aparecem.
- **Unificar a UI (ainda não feito):** cada painel SpriteUI envia o próprio pacote com `draw`. Juntar os
  pacotes num único envio por quadro, na ordem de pintura, reduz o custo de submissão; a redução real de
  `SDL_RenderGeometry` vem de juntar lotes adjacentes com mesma textura e mesmo clip. Próximo passo.
- **Alternância CPU/GPU da UI (ainda não feito):** o modo software é usado com `--frames N`. Falta opção
  na tela de configurações.

### 4.6 Janelas que ainda são ImGui (inventário)
Em `apps/editor/main.cpp` há 47 `ImGui::Begin`. As janelas a migrar, na ordem:

| Janela | Situação | Observação |
|---|---|---|
| Editor info | SpriteUI com moldura e testes; ImGui ainda no menu | Piloto deste processo |
| Settings | Host ImGui + conteúdo SpriteUI | Refazer (seção 4.1) |
| Console | SpriteUI com moldura (título, grip, X), teste `Editor.WindowFrame`. "?" e negrito pendentes. **Etapa 6 (sua aprovação) pendente**: o código ImGui antigo (`drawConsoleFrame`) ficou como legado até lá | `window_frame.hpp` + `console_panel.hpp` |
| Profiler | SpriteUI com a moldura do Console (título, grip, X) e botão Exportar na barra do topo. Sem teste de view ainda (pendente). **Etapa 6 (sua aprovação) pendente**; ImGui antigo (`drawProfilerFrame`) fica como legado | `window_frame.hpp` + `profiler_panel.hpp` |
| Hierarchy | SpriteUI com a moldura do Console (título, grip, X). Teste de view pendente. **Etapa 6 (sua aprovação) pendente**; ImGui antigo (`drawHierarchy`) fica como legado | `window_frame.hpp` + `hierarchy_panel.hpp` |
| Inspector | ImGui (host) + SpriteUI. Seções **Tags**, **ToolObject Script**, **Physics**, **Tools** e **Voxel** já são SpriteUI (`Editor.InspectorTags`: altura, Play, gravidade própria, ferramentas, grade do voxel). Em Tools, cada ferramenta é um bloco fixo (sem recolher por ferramenta) e o combo de acao usa a lista do Input Map. Na grade do voxel, cada célula é um botão (sem texto). Ainda em ImGui: UI Canvas, Particles e Add Resource (`extra_ui.hpp`). Renomear tag com duplo clique ainda não existe. Moldura só depois de portar as seções | `inspector_panel.hpp` |
| Scene | ImGui | Já tem `scene_view_panel.hpp` |
| Game | ImGui | Já tem `game_view_panel.hpp` |
| Assets / Projeto | ImGui | |
| Scenes | ImGui | |
| Export game | ImGui (`export_ui.hpp`) | |
| Input map | ImGui | |
| Script | ImGui | |
| Narrative | ImGui + ImNodes | **Por último** |
| Project dialog (fallback) | ImGui | Ver `project_dialog.hpp` |

Depois da lista, faltam: remover `imgui` e `imnodes` do build, do `main.cpp` e do `CMakeLists.txt`.

---

## 5. Como continuar

1. Escolha a próxima janela da tabela 4.6 (Console é a menor).
2. Siga as etapas 1 a 7 da seção 1, com os testes da seção 3.
3. Atualize a tabela 4.6 e o `CODAR.MD` (teste visual para você).
4. Não apague nada da ImGui antes da etapa 6 aprovada.
