# CODAR.MD — tarefas de código pendentes (para resolver no código)

Este documento lista o que falta na engine para a interface ficar completa.
Cada tarefa tem: contexto, arquivos, passos e critério de aceite. Idioma padrão da engine: inglês.

Estado de referência: branch `ui-integrado` (engine) e `sui-integrado` (SpriteUI).

## Estado atual (atualizado em 2026-11-18)

**Nada foi apagado do que estava pronto.** Conferido no git: nenhum controle do ImGui foi removido de
`apps/editor` em relacao ao `origin/main` do usuario, e nenhum arquivo do editor foi apagado nos commits.
As opcoes de tema continuam no codigo: `drawAppearanceTab` em `main.cpp` (seletor de tema, temas do usuario,
cores base, forma e tamanho) e os presets salvos em `ui_presets.hpp`. O que mudou: a reescrita do Settings
(`ui-settings-wip`) ainda nao esta ligada ao editor, e so portou essas opcoes como dados.

**O que nao esta pronto e por que:**
- Settings em SpriteUI: a reescrita esta em `ui-settings-wip` (commit `eac213e`) e nao compila. Falta a ponte
  com o Editor (`settings_editor.hpp`) e a adaptacao de `main.cpp`. Enquanto isso, o `ui-integrado` usa o Settings antigo.
- Janelas do ImGui: 1445 chamadas. Nao da para trocar tudo de uma vez sem ver a tela.
- Plugin de interface na GPU para o jogo: falta validacao com placa de video.
- Melhoria de desempenho do SpriteUI (instancias em vez de vertices): falta teste de pixel na GPU.
- Bloco `engine` do `project.json`: gravado e lido, mas a janela ainda nao o aplica.

**Commits relevantes:** `ui-integrado`: 73bb6c6 (driver de render), aae4527 (Help SpriteUI), e537377 (analise),
517ec74 (bloco engine no project.json), aaa68d1 (lista do ImGui). `ui-settings-wip`: eac213e.
`ui-export-wip`: dfd1f5b (modelos de Informacoes do Editor e Exportar, preservados).

## T0. Metodo de migracao: uma janela por vez (decisao do usuario)

Nao apagar a janela do ImGui antes de a versao SpriteUI estar testada. Para cada janela da lista T3:
1. Criar a versao SpriteUI ao lado da do ImGui, com a mesma lista de controles (checklist abaixo).
2. Deixar as duas abertas por um tempo, com uma opcao para escolher qual usar.
3. O usuario faz os testes da secao "Testes que o usuario faz" e marca cada item.
4. So depois de tudo marcado, apagar a janela do ImGui e a opcao.

**Checklist de paridade (por janela):** cada botao, caixa, deslizante, campo e menu da janela ImGui tem
um equivalente SpriteUI com o mesmo nome e o mesmo resultado. Nada de "vazio" ou placeholder.

## Testes que o usuario faz (para eu validar)

Rodar no seu computador com placa de video. Para cada item, diga "ok" ou descreva o problema com a captura de tela.

1. **Build e testes automatizados**
   - `cmake --build <pasta-de-build> --target tool_editor -j2` deve terminar sem erro.
   - `ctest` deve passar em todos os testes (hoje sao 23).
2. **Escala e texto**
   - Abrir o editor: a escala deve estar em 125% e o texto nitido (sem serrilhado).
   - Idioma padrao: ingles.
3. **Cliques**
   - Cada botao deve responder ao primeiro clique, sem precisar de varios.
4. **Janelas e docking**
   - Arrastar uma janela: ela fica sempre por cima das outras.
   - Clicar no titulo sem arrastar: a janela nao sai do encaixe.
   - Os icones de encaixe aparecem semitransparentes ao arrastar.
   - Divisor da Hierarquia (borda direita e esquerda): deve redimensionar nos dois sentidos.
5. **Menus**
   - Menu Janela: caixas de selecao e icones, nada de `[x]` em texto.
6. **Configuracoes (quando a janela SpriteUI estiver pronta)**
   - Os icones das abas aparecem com dica ao passar o mouse: pincel (Aparencia), engrenagem (Configuracoes),
     controle (Jogo), lanterna (Iluminacao), blocos (Otimizacao) e G (Idioma).
   - Trocar uma cor no tema: a janela muda na hora. Fechar e abrir de novo: a cor continua.
   - Trocar o tema inteiro e salvar um tema do usuario: ele aparece na lista.
7. **Render (GPU)**
   - Trocar a API grafica (Software, Vulkan, OpenGL, D3D12, D3D11): a Cena e o Jogo nao podem ficar pretos.
   - No log, a linha "Interface SpriteUI: driver de render ..." deve mostrar o driver escolhido.
8. **Projeto**
   - Salvar o projeto e abrir o `Project/project.json`: deve existir o bloco `engine` com idioma, escala, renderer, API, render scale e FPS.
9. **Desempenho**
   - Anotar o tempo de quadro no Profiler com a interface parada e com ela mudando (ms por quadro). Isso
     decide a etapa de desempenho do SpriteUI.


---

## T1. Terminar a janela Settings (estado parado)

**Contexto.** O painel de Settings está sendo trocado por uma barra de ícones com tooltip
(pincel = Appearance, engrenagem = Settings, controle = Game, lanterna = Lighting,
blocos = Optimizing, G = Language). Há um arquivo novo `apps/editor/settings_tabs.hpp` e
mudanças em `settings_model.hpp`, `settings_panel.hpp` e `settings_changes.hpp` ainda sem commit.

**Pendência.** `apps/editor/settings_bridge.hpp` e `apps/editor/tests/test_settings_bridge.cpp`
aparecem como removidos no índice, mas `apps/editor/main.cpp` (função `settingsApiBuilt`) e
`apps/editor/CMakeLists.txt` (alvo `editor_settings_bridge_tests`) ainda os usam.
Ação: ou restaurar os dois arquivos do HEAD (`git restore --staged --worktree ...`), ou
migrar `settingsApiBuilt` para o painel novo e então remover o bridge, o alvo CMake e o teste.

**Passos.**
1. Resolver a pendência acima e compilar `tool_editor`.
2. Aba Optimizing (nova): limite de quadros, cache de malhas (`chunkCacheMax`), escala de
   interface, opções de desempenho. Cada opção ligada ao modelo, com teste.
3. Conferir que nenhuma opção antiga sumiu: comparar com a função de Settings antiga em
   `main.cpp` (linha ~1966 no código do usuário) e com `settings_model.hpp`.

**Aceite.** Todas as abas abrem; cada opção muda o modelo e persiste em `settings.json`;
`ctest` verde.

## T2. Aparência: cores e formatos do tema

**Contexto.** O usuário quer personalizar as cores de cada parte da engine.
Já existem tokens de cor e de forma (commit `860f0c8`).

**Passos.**
1. Na aba Appearance, listar os tokens por grupo (janela, título, botão, campo, menu, seleção).
2. Cada token abre o seletor de cor da SpriteUI (`ColorPicker`), com HEX e alfa.
3. Formatos: arredondamento de canto, espaçamento, altura de botão (padrão 28 px) e escala (125%).
4. Botões "Restaurar padrão" por grupo e global.

**Aceite.** Mudar um token muda a janela na hora; o tema salvo volta ao reabrir.

## T3. Janelas ainda em ImGui

Contagem atual de chamadas `ImGui::` em `apps/editor`: 1445.
Por arquivo: `extra_ui.hpp` 235, `assets_panel.hpp` 139, `project_dialog.hpp` 40,
`export_ui.hpp` 40, `script_window.hpp` 37, `narrative_panel.hpp` 54 (por último), `fx.hpp` 15,
`editor_scripts.hpp` 4, `theme.hpp` 5, `ui_contract.hpp` 4, `viewport_tools.hpp` 3,
`view_shapes.hpp` 1, `ui_test.cpp` 8, `main.cpp` 578.

Ordem sugerida:
1. **Assets e Scenes** (`assets_panel.hpp`): árvore de pastas, importação, arrastar e soltar,
   menus de contexto, editor de texto (`Edit file`) e modais (renomear, excluir, nova pasta,
   novo texto, nova cena, renomear cena, excluir cena). Modelo puro + painel SpriteUI.
2. **Export e Editor info** (`export_ui.hpp`): a lógica de pacote continua na engine; o painel
   só mostra e pede.
3. **Input Map** (`extra_ui.hpp`, função com `Input Map`): lista de ações, captura de tecla,
   conflitos.
4. **Script** (`script_window.hpp`): usar `TextArea` da SpriteUI.
5. **Caixa de projeto** (`project_dialog.hpp`): remover o fallback ImGui se a tela de início
   SpriteUI (`start_screen_spriteui.hpp`) cobrir o caso.
6. **Profiler, Console, Inspector, Scene, Game** (`main.cpp`): remover os placeholders
   `ImGui::Begin` que servem de docking. O docking passa a ser o da SpriteUI (`Docking.hpp`).
7. **Narrative** (`narrative_panel.hpp`, ImNodes): por último. Precisa de editor de grafo
   nativo na SpriteUI.

**Aceite de cada item.** Modelo testado no `ctest`; janela abre pelo menu Janela; sem
`ImGui::` no arquivo; `tool_editor` compila.

**Ao final.** Quando `grep -rn "ImGui::" apps/editor` retornar zero, remover ImGui do CMake
(`imgui_lib`, `ImGui_ImplSDL3`, `ImNodes`) e do `third_party`.

## T4. Ajustes de janela e docking (usuário relatou)

- Janela arrastada sempre na frente de todas (camada de arrasto).
- Ícones de encaixe semitransparentes ao arrastar.
- Clicar no título sem arrastar não tira a janela do encaixe (limiar de 6 px).
- Redimensionar o divisor nos dois sentidos (ex.: Hierarchy, borda esquerda).
- Campos de marcação: usar `CheckBox`, `Toggle` e `RadioGroup` da SpriteUI, não quadrados.
- Os menus usam caixa de seleção e ícone; nada de `[x]` em texto.

**Aceite.** Cada item tem teste automatizado na SpriteUI ou no editor, e captura de tela.

## T5. Desempenho da interface

- Medir com `benchmarks/spriteui_frame_benchmark.cpp` (200 botões, 20 janelas).
- Cache de layout de texto (`TextLayoutCache.hpp`) já existe; aplicar também aos painéis do editor.
- Evitar `std::string` temporário no laço de desenho.

**Aceite.** Número de quadro registrado antes e depois, no `docs/IMPLEMENTATION_STATUS.md`.

## T6. Segurança e críticas pendentes (auditoria)

- **F-09**: mover implementação pesada de headers para `.cpp`, um módulo por vez (render primeiro).
- **F-11**: decidir política de dependências (vendor completo ou FetchContent com versão fixa).
- **F-14**: versão única. Hoje: CMake `0.1.0`, README/CHANGELOG `0.7.19-beta`, guias `0.2.3-beta`.
- **F-15**: benchmarks de performance com critério de aceite.

---

## Enxugamento do projeto (aprovar antes de apagar)

Itens que parecem lixo ou duplicados. Nenhum foi apagado ainda:

- `Projeto Final/` — cópia de assets e um binário ELF de ~10 MB. Remover do pacote.
- `out/` — saída de build. Não deve ir no zip.
- `build_log.txt`, `git add.txt` — logs e rascunhos.
- `COMO_COMPILAR.txt` e `HOW_TO_BUILD.txt` — duplicados; manter um só (sugestão: `docs/BUILD.md`).
- `LEIA-ME-THIRDPARTY.txt` — integrar ao `docs/BUILD.md`.
- `third_party/` — 49 MB. SDL3, ImGui, imnodes, miniaudio e quickjs não precisam ir no zip.
  Manter `third_party/SpriteUi` e `third_party/LightPrism`.
- Documentos de revisão antigos (`docs/REVIEW_V4_2.md`, `docs/V4_2_IMPLEMENTATION_MAP.md`):
  marcar como histórico ou remover.
