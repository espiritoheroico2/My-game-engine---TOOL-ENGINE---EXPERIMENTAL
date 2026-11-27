# 16 — Análise de usabilidade das janelas do editor

Data: 2026-11-27. Método: leitura do código dos painéis em `apps/editor/` (não foi possível ver a tela nem rodar o editor com interação). Os problemas abaixo são deduzidos do código; os que dependem de aparência real (cortes de texto, espaçamento) estão marcados como "a confirmar na tela". Linhas são aproximadas e se referem ao código antes das mudanças deste ciclo.

Legenda: **I** = impacto (A alto, M médio, B baixo). **E** = esforço (B baixo, M médio, A alto). **FEITO** = implementado neste ciclo. Janelas Settings, Coding, Canvas, Profiler e Info foram lidas só por amostragem (rótulos e estados vazios); a lista delas é menor por isso, não porque estejam melhores.

Problema transversal (vale para todas as janelas): **não existe infraestrutura de tooltip**. Há só a camada `Tooltip` em `window_manager.hpp:27`, sem nenhum painel que a use. Botões principais ficam sem dica ao passar o mouse (Hierarchy, Scene, Game, Assets, Scenes, Input Map). Criar o sistema (texto atrasado ao pairar, posição, recorte) é recurso novo grande; ficou para depois. Ver "Pendente".

---

## Hierarchy (`hierarchy_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| H1 | Cena sem objetos mostra lista em branco, sem orientação (`buildList`). | M | B | FEITO: "A cena está vazia. Clique em + Create…" (`usability::HierarchyEmptyHint`). |
| H2 | "Duplicate" e "Delete" parecem sempre ativos; sem seleção o clique não faz nada e não avisa (`onDown`, `Button::Duplicate/Delete`). | A | B | FEITO: botões apagados sem seleção (`HierarchyToolbarState`). |
| H3 | Botões da barra sem hover (só a linha da lista tem), `addButton`. | M | B | FEITO: fundo `rowHover` ao passar o mouse. |
| H4 | Olho e cadeado são só ícones, sem legenda nem dica; o significado de "travado" (só editor) não aparece aqui. `addRow`. | M | M | Pendente (precisa de tooltip). |
| H5 | O menu de contexto é a única forma de renomear, mover para cima/baixo e "Edit individual" — ações escondidas; nada na barra indica o botão direito (`buildOverlay`, `Ctx`). | M | B | Pendente: só texto de ajuda (Help) fala disso. |
| H6 | "Main Camera (Principal)" mistura inglês e português no rótulo (`kCreateLabels`). | B | B | Pendente: mudar a chave exige atualizar os pacotes de idioma e o Help. |
| H7 | Linha de elemento de UI não tem olho nem cadeado (corretamente), mas nada explica a diferença visual para objetos (`addRow`, `isElem`). | B | B | Pendente. |
| H8 | Apagar objeto bloqueado só informa na barra de status (`main.cpp` ~288), longe da janela. | B | M | Pendente. |

## Inspector (`inspector_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| I1 | Sem objeto: "no object selected" em minúscula, sem dizer como selecionar (`prepare`, ~218). | A | B | FEITO: "Nenhum objeto selecionado. Clique em um objeto na Hierarchy ou na vista Scene…" em parágrafo com quebra. |
| I2 | "Locked (editor only)" não explica que trava só edição no editor, não no jogo (~653). | B | B | Pendente. |
| I3 | Rótulos com largura fixa `kLabelMax = 118` (~164) cortam rótulos longos em português ("Intensidade da emissão"). A confirmar na tela. | M | M | Pendente. |
| I4 | O texto "Coming in a future update (locked)…" (~1110) é um bloco longo de recursos bloqueados no meio dos campos reais; confunde o que existe. | B | B | Pendente. |
| I5 | Escolhas "(none)" em listas de asset (~607, 736, 1168, 1281) usam a mesma palavra para "limpar" e "vazio". | B | B | Pendente. |
| I6 | Seção Light: aviso "No object with this name: uses this object." em `warnColor_` aparece só depois do erro; ordem do campo de alvo antes do tipo é ilógica. A confirmar. | B | M | Pendente. |

## Scene (`scene_view_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| S1 | Botões "Culled", "Post-fx", "Magnet" (~849–872) têm rótulos ambíguos: "Culled" não diz que mostra objetos descartados pelo culling; "Magnet" é o ímã de encaixe. | M | B | Pendente: texto novo precisa caber nos botões, não verificável sem ver a tela. |
| S2 | Os botões Play/Pause/Step não mostram atalho (só os de gizmo têm "(W)", "(E)"). | M | B | Pendente. |
| S3 | A dica de navegação (~851) está bem, mas é uma linha só e some em janela estreita. A confirmar. | B | M | Pendente. |
| S4 | Nenhuma orientação quando a cena está vazia: viewport só com grade. | M | B | Pendente (compartilha texto com H1). |
| S5 | A barra tem até 13 botões que mudam conforme a seleção (Canvas: Add point, Remove point, Line, Dotted…, ~875–881); botões aparecem e somem sem animação, deslocando os outros. | M | A | Pendente. |

## Game (`game_view_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| G1 | O estado "Sem câmera Principal" tem mensagem boa (`main.cpp` ~1505), mas aparece só quando não há células; durante o Play a mensagem some. | B | M | Pendente. |
| G2 | Play/Stop/Pause/Resume/Step (~352) mudam de rótulo no mesmo botão; "Step" só aparece pausado, sem explicar que avança um quadro. | M | B | Pendente (dica). |
| G3 | Rótulo duplicado com a barra da Scene: dois botões Play em duas janelas, sem indicar que são o mesmo estado. | B | B | Pendente. |

## Coding (`coding_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| C1 | Estados vazios já tratados ("No code files found…", "Choose a code file…"); bom. | — | — | Sem ação. |
| C2 | "Save" (~381) não mostra se o arquivo está sem salvar fora do texto "(unsaved)" do título; sem feedback depois de salvar além do título. | M | M | Pendente. |
| C3 | Painel de diagnósticos mostra só 3 problemas (`drawDiagnostics`, ~520) sem aviso de que há mais; o usuário não sabe se é tudo. | M | B | Pendente. |
| C4 | Tamanhos "Code size"/"List size" aparecem como rótulo de controle sem unidade. | B | B | Pendente. |

## Canvas (Inspector: seção Canvas, `canvas_*.hpp`, Scene com "Edit canvas")

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| CV1 | "Edit canvas" (`scene_view_panel.hpp` ~875) é um botão de modo que só aparece com um Canvas selecionado; quem não selecionou não descobre a edição direta. | M | M | Pendente. |
| CV2 | Mensagens de bloqueio ("Canvas is locked…", `main.cpp` ~220, ~243) vão para a barra de status, longe de onde o clique aconteceu. | M | M | Pendente. |
| CV3 | Elementos de UI são linhas da Hierarchy; o Inspector mostra o bloco do elemento só quando selecionado, sem texto dizendo a qual Canvas ele pertence. | B | B | Pendente. |

## Scenes (`scenes_spriteui.hpp`, `scenes_model.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| SC1 | Clicar em "Open", "Rename…" ou "Delete" bloqueado não fazia nada e sem aviso (`handle`, ~636). | A | B | FEITO: mostra o motivo na linha de aviso (`SceneActionBlockedReason`). |
| SC2 | Lista de cenas vazia ficava em branco; a dica só existia no campo de nome. | M | B | FEITO: "Nenhuma cena ainda. Clique em New scene…". |
| SC3 | As mensagens do modelo (`m.status`) eram desenhadas sem `T()`: "Select a model or sound…" (chave existente nos pacotes) aparecia sempre em inglês. | M | B | FEITO: `T(model_.status.c_str())`. |
| SC4 | O aviso `m.status` fica na tela mesmo depois de trocar a seleção. | B | B | FEITO: limpa ao selecionar outra cena. |
| SC5 | "Assign asset" (Scenes) e "Assign to scene" / "Add to scene" (Assets) são três verbos para duas ações diferentes (colocar na lista de assets da cena vs. instanciar na cena). | A | M | Pendente: renomear exige verificar a largura dos botões na tela. |
| SC6 | "Save scene" não dá retorno visual na janela (só a barra de status). | M | B | Pendente. |
| SC7 | O "x" de remover asset (`assetRemove_`) não tem dica. | B | B | Pendente. |

## Assets (`assets_spriteui.hpp`, `assets_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| A1 | Rótulos "Add to scene" e "Assign to scene" (~952–953) são quase sinônimos; ver SC5. | A | M | Pendente. |
| A2 | O botão "Close" do editor de texto reutiliza o mesmo botão de "Rename…" (`rename_`, ~947); posição muda sem aviso. | B | M | Pendente. |
| A3 | A dica de arrastar arquivos (~994) some quando um arquivo de texto está aberto, que é quando mais se pensa em importar. | B | B | Pendente. |
| A4 | Estados vazios existem ("This folder is empty.", "No items match the filter."); mas o primeiro não cita o botão Import. | M | B | Pendente (texto em lang e largura). |
| A5 | Mensagens de falha (`ed.statusLine = "Import failed: …"`, `assets_panel.hpp` ~117) em inglês fixo, sem `T()`. | M | B | Pendente. |

## Console (`console_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| CO1 | Dica do campo de comando "test ECS: 10000   (help)" (~835) era um texto cru e sem `T()`; dizia pouco. | M | B | FEITO: "comando (ex.: test ECS: 10000, ou help)". |
| CO2 | Estados vazios tratados ("No messages yet.", "No messages match the filter."). | — | — | Sem ação. |
| CO3 | Filtro e comando são dois campos parecidos lado a lado, sem rótulo fixo (só placeholder que some ao digitar). | M | M | Pendente. |
| CO4 | Botão de limpar sem confirmação nem desfazer (`Requests::clear`). | B | B | Pendente. |

## Settings (`settings_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| ST1 | O texto "Not available in this version yet" (~399) não diz qual recurso nem quando. | B | B | Pendente. |
| ST2 | Muitas dicas já existem (ex.: "Tip: lower render scale…", ~656); bom padrão. Seção Physics não foi tocada (outro agente). | — | — | Sem ação. |
| ST3 | Não há "restaurar padrão" por seção, só campo a campo. A confirmar. | M | A | Pendente. |

## Input Map (`input_map_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| IM1 | "Reset to defaults" apagava todos os mapas com um clique, sem confirmação nem desfazer (`dispatch`, `Hit::ResetDefaults`). | A | B | FEITO: dois cliques ("Confirmar restauração?"). |
| IM2 | "Delete map" apagava o mapa inteiro com um clique (`Hit::SchemeDelete`). | A | B | FEITO: dois cliques ("Confirmar exclusão?"); outro clique em qualquer lugar desarma. |
| IM3 | Posições fixas em pixels (`x0 + 380`, `x0 + 520`, ~422–436) quebram em janela estreita: texto "Saved in Project/input_map.json" é cortado. A confirmar. | M | M | Pendente. |
| IM4 | O "x" de remover linha (`Hit::RowDelete`, ~493) apaga sem confirmar; menor dano (uma linha). | B | B | Pendente. |
| IM5 | "Listen" e "Invert" ficam ao lado do campo de eixo sem texto dizendo o que fazem (~477). | M | B | Pendente (dica). |

## Export (`export_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| EX1 | Plataforma indisponível selecionada: o botão fica apagado sem dizer por quê junto dele (a nota genérica fica abaixo da lista). | M | B | FEITO: aviso "Este build não exporta esta plataforma…" perto do botão (`ExportBlockedReason`). |
| EX2 | O texto final ("Open the output folder and double-click Play.bat…", ~258) fica só no fim da janela; some com rolagem. | B | B | Pendente. |
| EX3 | A pasta de saída é texto, não botão "abrir pasta". | M | M | Pendente (recurso novo pequeno). |
| EX4 | Resultado verde/vermelho (`resultOk_`) sem ícone; só cor. | B | B | Pendente. |

## Help (`help_panel.hpp`, `help_content.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| HP1 | Botão de copiar código (`click`, ~395) copia sem nenhum retorno ("Copiado!"). | M | B | Pendente (precisa estado temporal no painel). |
| HP2 | Busca sem resultados: a lista fica vazia sem mensagem. A confirmar. | M | B | Pendente. |
| HP3 | `help_content.hpp` tem 2293 linhas de texto fixo; conteúdo novo desta análise (confirmação em dois cliques) ainda não está no Help. | B | B | Pendente. |

## Test Map (`test_map_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| TM1 | "Generate map" substitui os chunks da cena (texto no começo), mas o botão age no primeiro clique, sem confirmar. | A | M | Pendente: depende de existir desfazer para a geração (não verificado). |
| TM2 | Sem retorno depois de gerar (nenhum texto "N chunks criados"). | M | B | Pendente. |
| TM3 | O texto de descrição (~169) é um parágrafo longo antes do controle principal; o campo "Chunks per side" está depois dele. Ordem pouco lógica. | B | M | Pendente. |

## Node (`node_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| N1 | Estado vazio já orienta ("Right-click an object in the Hierarchy and choose Edit individual."); bom. | — | — | Sem ação. |
| N2 | Botão "Exit isolation" fica visível mas apagado quando não há isolamento; sem dica de que só serve depois de isolar. | B | B | Pendente. |
| N3 | Linha "(unnamed)" para nós sem nome: pouco útil (`kRowH` sem ícone de tipo). | B | M | Pendente. |

## Profiler (`profiler_panel.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| P1 | "Export data" (~369) não diz para onde vai o arquivo antes do clique; só o status depois. | M | B | Pendente. |
| P2 | O botão apagado durante a exportação não diz que está exportando fora o status ao lado (~378). | B | B | Pendente. |
| P3 | Muitas seções sem legenda de unidade em alguns campos. A confirmar. | B | M | Pendente. |

## Info (`editor_info_spriteui.hpp`, `editor_info_model.hpp`)

| # | Problema | I | E | Estado |
|---|---|---|---|---|
| IN1 | Campos obrigatórios marcados com "*" no rótulo (~71–79), mas a legenda "* obrigatório" não aparece perto; a lista "Missing:" só aparece depois. | M | B | Pendente. |
| IN2 | "Copyright (empty = automatic)" no rótulo, ok; mas "Save info" não mostra aviso de alterações não salvas. | M | M | Pendente. |

---

## Resumo

| Janela | Achados | Feitos |
|---|---|---|
| Hierarchy | 8 | 3 |
| Inspector | 6 | 1 |
| Scene | 5 | 0 |
| Game | 3 | 0 |
| Coding | 3 (+1 ok) | 0 |
| Canvas | 3 | 0 |
| Scenes | 7 | 4 |
| Assets | 5 | 0 |
| Console | 2 (+1 ok) | 1 |
| Settings | 2 (+1 ok) | 0 |
| Input Map | 5 | 2 |
| Export | 4 | 1 |
| Help | 3 | 0 |
| Test Map | 3 | 0 |
| Node | 2 (+1 ok) | 0 |
| Profiler | 3 | 0 |
| Info | 2 | 0 |
| **Total** | **66** | **12** |

## O que foi implementado (sem mudar formato de arquivo salvo)

Regras puras em `apps/editor/usability_hints.hpp`, com teste `tests/test_usability_hints.cpp` (`Editor.UsabilityHints`): estado vazio da Hierarchy e do Inspector, botões da Hierarchy sem seleção, motivo de ação bloqueada em Scenes e Export, e `ConfirmArm` (confirmação em dois cliques). Ligadas em `hierarchy_panel.hpp`, `inspector_panel.hpp`, `scenes_spriteui.hpp`, `export_panel.hpp`, `input_map_panel.hpp` e `console_panel.hpp`. Textos novos com chave em `assets/lang/pt-BR.json` e `es.json`.

## Pendente

- **Tooltips** (H4, S2, G2, SC7, IM5, P1, N2…): precisam de um sistema novo (dica ao pairar, atraso, posição, recorte). É a melhoria de maior alcance; sugerido como próximo ciclo, com um modelo puro `hover_hint_model.hpp` (qual dica, quando) testável sem pixel.
- **Rótulos que mudam de largura** (SC5/A1, S1): trocar o texto exige ver o botão na tela; sem isso há risco de corte.
- **Test Map (TM1)**: confirmar antes de substituir chunks só faz sentido depois de saber se existe desfazer.
- **Retornos temporais** (HP1 "Copiado!", SC6, TM2): exigem estado com tempo em cada painel; fazer junto com o sistema de dicas.
- **Posições fixas em pixels** (IM3, I3): precisam de teste visual em larguras estreitas.
