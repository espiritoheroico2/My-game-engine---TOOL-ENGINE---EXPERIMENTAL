// ui_contract.hpp - CONTRATO DE UI: o que o editor e o jogo usam do Dear ImGui, e como cada peca deve ser
// substituida pela SpriteUI.
//
// ESTE ARQUIVO SO TEM COMENTARIOS. Nao e incluido em lugar nenhum e nao muda o build. Serve de mapa para a troca:
// cada arquivo de UI tem, no topo, um bloco "UI-CONTRATO" que aponta para as secoes daqui.
//
// Versao do contrato: 0.7.14-beta. Atualize quando um elemento novo for usado ou um antigo sair.
//
// ============================================================================================================
// 1. CAMADAS (de baixo para cima)
// ============================================================================================================
//   Plataforma   SDL3 (janela, eventos, teclado, mouse, texto). Backend: ImGui_ImplSDL3_* (main.cpp).
//   Render UI    SDL_Renderer (software ou hardware, escolhido pelo SDL). Backend: ImGui_ImplSDLRenderer3_*.
//   Quadro       NewFrame -> widgets (Begin/End, Button...) -> Render -> RenderDrawData -> SDL_RenderPresent.
//   Cena 3D      NAO passa pelo ImGui. Renderizada em buffer (SoftRenderer ou GpuRenderer), copiada para um
//                SDL_Texture (ViewTex) e mostrada com ImGui::Image. Grade, gizmos e particulas sao desenhados
//                por cima, com ImDrawList.
//   Jogo         O mesmo executavel no modo --player: uma janela "##player" sem decoracao, tela cheia, mostrando
//                a imagem da camera de jogo. A UI do jogo (UiCanvas) e desenhada com ImDrawList em cima dela.
//
// ============================================================================================================
// 2. CATALOGO DE ELEMENTOS (ImGui -> uso no editor -> troca prevista na SpriteUI)
// ============================================================================================================
// Contagens aproximadas de chamadas no codigo (versao 0.7.14-beta).
//
// 2.1 Janelas e areas
//   Begin/End (~20/35)         Janela de cada painel. Titulo = T("Hierarchy") + "###hier": o "###" fixa o ID,
//                              entao o titulo pode mudar de idioma sem perder o layout salvo.
//                              Visibilidade: bool no Editor (showHierarchy, showInspector...). Begin(..., &bool)
//                              mostra o X de fechar.
//   BeginChild/EndChild (~11)  Sub-areas com rolagem (log do console, lista de problemas de script, abas).
//   BeginTabBar/TabItem (1)    Abas da janela Settings.
//   BeginMainMenuBar (1)       Barra de menus superior (File, Edit, Interface, Help...). MenuItem/BeginMenu (~105).
//   BeginViewportSideBar (1)   Barra de status inferior (linha de status do editor).
//   SetNextWindowSize/Pos/Focus  Tamanho/posicao iniciais de janelas de dialogo.
//
// 2.2 Docking (encaixe de janelas)
//   DockSpaceOverViewport      Area central onde os paineis encaixam (main.cpp, layout principal).
//   DockBuilder* (~20)         Monta o layout padrao: Add/Split/DockWindow/Finish, criado em "Redefinir layout".
//   Flags NoUndocking/NoDockingSplit/NoResize (applyLayoutLock)  Travar layout ("Travar interface").
//   io.ConfigDockingWithShift  Encaixar so com Shift (opcao de Interface).
//   lockWindows() (main.cpp)   Desfaz arrastes indesejados de janela a cada quadro quando travado.
//                              ATENCAO: chama ClearActiveID e zera MovingWindow. Ponto a revisar na troca.
//
// 2.3 Botoes e acoes
//   Button, SmallButton (~93)  Acoes: criar, apagar, salvar, abrir, exportar. Retorna true no clique.
//   InvisibleButton (3)        Area clicavel sem desenho (cenas, tiles, camera do jogo em fx.hpp).
//   RadioButton (10), Checkbox (~50)  Opcoes booleanas e de escolha unica.
//   Selectable (~22)           Linha selecionavel: itens de listas, assets, hierarquia achatada (lista grande).
//   BeginDisabled/EndDisabled  Desabilita um bloco (ex.: exportar sem informacoes completas).
//
// 2.4 Valores e entradas
//   SliderFloat/Int (~53)      Valores com limite (escala, cores, volume, sombras). Formato em printf.
//   DragFloat/Int (~35)        Valores arrastaveis sem limite estrito: posicao, rotacao, escala (Transform).
//   InputText (~36)            Texto de uma linha. Buffer char[] PERSISTENTE (static ou membro): ver 3.1.
//   InputTextMultiline (3)     Editor de script. Buffer std::string com callback de redimensionamento.
//   InputTextWithHint (7)      Campos com dica (console, busca). Console tem callback de historico (setas).
//   InputInt/InputDouble (4)   Numeros exatos (ex.: duracao de no no Narrative).
//   Combo / BeginCombo (~32)   Listas suspensas: forma de particula, tipo de corpo, idioma, plataforma.
//   ColorEdit3/4 (~18)         Seletor de cor (RGB ou RGBA). Valores float 0..1 direto no componente.
//
// 2.5 Texto
//   Text, TextUnformatted, TextWrapped, TextDisabled (~70), TextColored (~26), BulletText, Bullet
//                              Rotulos, avisos, quebra de linha automatica (ajuda, export).
//   CalcTextSize, GetFontSize, GetTextLineHeightWithSpacing  Medidas para alinhar botoes e listas.
//   SeparatorText/Separator/Spacing/NewLine (~70)  Titulos de secao e espacamento.
//
// 2.6 Arvores e secoes
//   CollapsingHeader (15)      Secoes recolhiveis do Inspector (Transform, Mesh, Fisica, Script...).
//   TreeNodeEx/TreePop (15)    Hierarquia de objetos (pai/filho). Flags: OpenOnArrow, DefaultOpen, Selected.
//                              Lista achatada quando ha mais de 300 objetos (ImGuiListClipper).
//   SetNextItemOpen            Forca a abertura de uma secao (botoes "Abrir tudo" / "Recolher tudo").
//
// 2.7 Tabelas
//   BeginTable/TableSetupColumn/TableNextRow/TableNextColumn (~18)  Lista de projetos, Profiler, Assets.
//
// 2.8 Popups e menus de contexto
//   OpenPopup/BeginPopup/EndPopup (~35)   Menus de criar, adicionar componente, remover recurso.
//   BeginPopupModal (8)        Dialogos bloqueantes: renomear ("renobj"), confirmar exclusao, novo arquivo.
//   BeginPopupContextItem/Window (4)  Clique direito num objeto ou no fundo da hierarquia.
//   CloseCurrentPopup (16)     Fecha o popup atual (apos a acao).
//
// 2.9 Arrastar e soltar (Drag & Drop)
//   Payloads por nome:  "HIER_ENTITY" (mover objeto para outro pai na Hierarquia)
//                       "ASSET_PATH"  (arrastar arquivo dos Assets para a cena ou o inspetor)
//   SetDragDropPayload / AcceptDragDropPayload / Begin/EndDragDropSource / Target.
//
// 2.10 Desenho livre (ImDrawList)
//   GetWindowDrawList (9)      Linhas, retangulos, texto e imagens desenhados em coordenadas de tela:
//                              grade infinita, gizmos, trajetoria de particulas (AddLine), particulas
//                              (AddCircleFilled/AddTriangleFilled), UI do jogo (PushClipRect + AddText/AddRect),
//                              imagem da camera do jogo (AddImage), overlay de culling (vermelho).
//   GetColorU32 / ColorConvert  Converte cor float para o formato do ImDrawList.
//   SetCursorScreenPos/GetCursorScreenPos  Posiciona widgets em coordenadas de tela (overlays do viewport).
//
// 2.11 Imagens (viewport e jogo)
//   Image (1)                  Mostra o SDL_Texture da cena (reinterpret_cast<ImTextureID>(sdlTex)).
//   AddImage (1)               Mostra a imagem da camera de jogo dentro da janela Game (fundo do viewport do jogo).
//   Cada textura e um ViewTex (editor_state.hpp): dona do SDL_Texture, redimensionada quando a janela muda.
//
// 2.12 Graficos e progresso
//   PlotLines (1)              Grafico de tempo de quadro no Profiler (ultimos 240 quadros).
//   ProgressBar (3)            Barras de tempo por zona no Profiler e de uso de CPU (total e por nucleo).
//
// 2.13 Teclado e mouse
//   IsKeyPressed/IsKeyDown/IsKeyChordPressed (~20)  Atalhos: Ctrl+S salvar, Ctrl+Z/Y desfazer, Delete, F2
//                              renomear, teclas de movimento no modo Play (editor_scripts.hpp).
//   IsMouseClicked/Down/Released/Dragging/DoubleClicked (~15)  Selecao no viewport; arrastar com botao direito
//                              ou meio gira e move a camera do viewport; duplo clique na Hierarquia foca o objeto.
//   io.WantTextInput           REGRA: atalhos de objeto (Delete, F2) so valem quando NAO ha campo de texto ativo.
//   IsItemClicked/Hovered/Active/ToggledOpen, IsWindowFocused/Hovered/Appearing  Estado de item e janela.
//   SetKeyboardFocusHere       Foco automatico no campo "Novo nome" ao abrir o dialogo de renomear.
//   ClearActiveID, IsAnyItemActive  Controle de campo ativo (desfazer por quadro, ver lockWindows).
//
// 2.14 Estilo, tema e fonte
//   StyleColorsDark (1)        Base escura. Depois o tema aplica 5 cores-base (theme.hpp) a paleta toda.
//   GetStyle/GetStyleColorVec4/GetStyleColorName  Le e edita cores e tamanhos (lista avancada de cores).
//   s.ScaleAllSizes, FrameRounding, GrabRounding, TabRounding, ScrollbarRounding  Escala e arredondamento.
//   io.FontGlobalScale         Escala global do texto (tamanho de fonte nas configuracoes).
//   AddFontFromFileTTF (loadUiFont)  Fonte do usuario (assets/fonts/ui.ttf) ou do sistema, com faixas de glifos
//                              (acentos do portugues e do espanhol). Fallback: fonte embutida do ImGui.
//   PushFont/PopFont (1)       Fonte grande para titulos.
//   Temas: assets/themes/*.json (5 cores + arredondamento + escala). Ver theme.hpp.
//
// 2.15 Dados de tempo e clipboard
//   GetTime (4)                Relogio do ImGui: atraso de 0,4 s para verificar o script depois de digitar
//                              (main.cpp) e tempos de clique nos Assets (assets_panel.hpp).
//   SetClipboardText (2)       Botao "Copy code" dos erros de script (extra_ui.hpp) e copia do console (main.cpp).
//   GetIO().DeltaTime, GetFrameHeight, GetContentRegionAvail, GetWindowWidth/Pos  Medidas de layout.
//
// 2.16 Nos (ImNodes) - editor de grafo do Narrative
//   BeginNodeEditor/EndNodeEditor, BeginNode/EndNode, BeginNodeTitleBar, Begin/EndInput/OutputAttribute,
//   Link, IsLinkCreated/IsLinkDestroyed, NumSelectedNodes/GetSelectedNodes, SetNodeGridSpacePos.
//   Cada no tem um id (int) que e a chave para os dados do grafo (narrative_panel.hpp).
//
// 2.17 Backends e ciclo de vida
//   ImGui::CreateContext/DestroyContext, ImNodes::CreateContext/DestroyContext
//   ImGui_ImplSDL3_InitForSDLRenderer, ImGui_ImplSDLRenderer3_Init, *_NewFrame, *_ProcessEvent,
//   *_RenderDrawData, *_Shutdown.   ImGui::GetDrawData/Render/NewFrame.
//
// ============================================================================================================
// 3. PADROES DE DADOS (como o estado da UI e guardado hoje)
// ============================================================================================================
// 3.1 Buffers de texto
//   - Campo simples: "static char buf[N]" ou membro do painel. Nunca local dentro da funcao de desenho.
//   - Campo de rename: buffer preenchido quando o dialogo abre (beginRename), nao a cada quadro.
//   - Script: std::string com capacidade fixa, redimensionado por callback.
//   - Historico do console: CmdHistory (setas para cima e para baixo).
// 3.2 IDs
//   - Titulo com "###id" fixo. Itens em laco: ImGui::PushID(entity.index ^ (generation << 20)) + PopID.
//   - Botoes sem rotulo visivel: "##nome". Nunca rotulo vazio.
// 3.3 Textos
//   - T("Texto em ingles") devolve a traducao do idioma escolhido (assets/lang/*.json). A chave e o proprio
//     texto em ingles. Se nao houver traducao, aparece o ingles.
//   - W("Titulo", "id") = T(titulo) + "###id" (titulo traduzido, ID estavel).
//   - F("texto %d", x) = formatacao com traducao.
// 3.4 Estado do editor (sem ImGui)
//   - Editor (editor_state.hpp): selecao (ed.selected), visibilidade de janelas (showX), opcoes de layout
//     (lockInterface, dockWithShift, showCulled...), configuracoes (fpsMode, pixelFilter, theme, lang).
//   - Dados da cena: ECS (tool::World) com componentes (components.hpp). A UI le e escreve direto neles.
//   - Desfazer: ed.undoTick(...) recebe "algo esta sendo editado agora" para agrupar alteracoes.
// 3.5 Persistencia
//   - editor_settings.json   opcoes do editor (idioma, tema, API grafica, layout travado...).
//   - editor_layout.ini      layout dos paineis (gerenciado pelo ImGui; io.IniFilename).
//   - editor_theme.json      tema escolhido/personalizado.
//   - Project/project.json   configuracoes do projeto (inclui editorInfo para exportacao).
// 3.6 Jogo (runtime)
//   - UiCanvas (components.hpp): lista de elementos UiElem (texto, barra, botao) ligada a uma camera.
//   - drawUiOverlay (fx.hpp) desenha os elementos com ImDrawList no modo Play e no jogo exportado.
//   - Botoes do jogo: reagem so durante o Play (eventos "ui_show", "ui_hide", acoes).
//
// ============================================================================================================
// 4. REGRAS QUE A SPRITEUI PRECISA RESPEITAR (comportamento que o usuario ja conhece)
// ============================================================================================================
//   R1. Layout de janelas salvo e restaurado; "Redefinir layout" volta ao padrao.
//   R2. "Travar interface": nenhuma janela sai do encaixe nem vira flutuante por clique.
//   R3. Campo de texto nao perde o texto enquanto o usuario digita (nao reproduzido ainda; ver CHANGELOG).
//   R4. Atalhos de objeto (Delete, F2) nao agem dentro de campo de texto.
//   R5. Idiomas trocam o texto sem trocar IDs (layout e estado preservados).
//   R6. Temas: 5 cores-base geram a paleta; tamanhos e arredondamento vem do tema.
//   R7. O jogo exportado usa a mesma UI do editor (modo --player) e nao pode depender do editor.
//   R8. Desempenho: com 300+ objetos a hierarquia usa lista com clipper; cenas grandes nao podem montar
//       milhares de widgets por quadro.
//
// ============================================================================================================
// 5. MAPA ARQUIVO -> RESPONSABILIDADE
// ============================================================================================================
//   main.cpp              Loop principal, menus, layout de docking, hierarquia, inspetor, console, settings,
//                         viewport (imagem + gizmos), jogo (modo --player).
//   extra_ui.hpp          Inspetor: componentes (fisica, ferramentas, UI, particulas, script, add/remove).
//   assets_panel.hpp      Painel de Assets: pastas, arquivos, arrastar, dialogos de criar/renomear/apagar.
//   narrative_panel.hpp   Editor de grafo (ImNodes) do Narrative Controller.
//   project_dialog.hpp    Dialogo de criar/abrir projeto (tabela de projetos recentes).
//   export_ui.hpp         Janelas Editor info e Export (formulario e botao de exportar).
//   fx.hpp                Overlays desenhados com ImDrawList: particulas, UI do jogo, botoes.
//   viewport_tools.hpp    Selecao por raio e ferramentas do viewport (mouse).
//   editor_scripts.hpp    Teclas de movimento no Play (IsKeyDown).
//   theme.hpp             Temas: cores-base, escala, arredondamento, carregar/salvar.
//   i18n.hpp              T(), W(), F() e pacotes de idioma.
//   ui_test.cpp           Teste sem janela (idiomas e temas).
//   profiler.hpp          Dados do Profiler (usados pelos graficos).
