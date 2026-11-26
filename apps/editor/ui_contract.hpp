// ui_contract.hpp - CONTRATO DE UI: como a interface do editor e do jogo e feita hoje, com SpriteUI.
//
// ESTE ARQUIVO SO TEM COMENTARIOS. Nao e incluido em lugar nenhum e nao muda o build. Serve de mapa: cada arquivo
// de UI tem, no topo, um bloco "UI-CONTRATO" que aponta para as secoes daqui.
//
// Versao do contrato: 0.7.14-beta. Atualize quando um elemento novo for usado ou um antigo sair.
//
// ============================================================================================================
// 1. CAMADAS (de baixo para cima)
// ============================================================================================================
//   Plataforma   SDL3 (janela, eventos, teclado, mouse, texto). O laco principal e o main.cpp.
//   Render UI    SpriteUI sobre SDL_Renderer (sui_panel.hpp). Texto em atlas de glifos por tamanho
//                (text_atlas_set.hpp).
//   Janelas      Cada janela e um painel SpriteUI. As janelas com moldura usam framed::Chrome (framed_window.hpp,
//                window_frame.hpp). O encaixe das janelas fica em dock_host.hpp e dock_layout.hpp.
//   Quadro       Host do dock: beginFrame, desenho das janelas visiveis, overlay do dock por ultimo.
//   Cena 3D      Renderizada em buffer (SoftRenderer ou GpuRenderer), copiada para um SDL_Texture (ViewTex, em
//                editor_state.hpp) e mostrada pela janela Scene. Grade, gizmos e particulas sao gravados em
//                view::ShapeList e desenhados por view_painter.hpp.
//   Jogo         O mesmo executavel no modo --player: uma janela sem decoracao, mostrando a imagem da camera de
//                jogo. A UI do jogo (Canvas) e desenhada por cima dela.
//
// ============================================================================================================
// 2. ENTRADA E FOCO
// ============================================================================================================
//   - Eventos de ponteiro passam antes pelo dock_host (onEvent). Quando ele devolve true, a janela nao recebe o
//     evento.
//   - panel_router.hpp decide quem recebe o ponteiro quando paineis SpriteUI se sobrepoem.
//   - Cada campo de texto guarda o proprio texto e o foco. A tecla vai ao campo que esta com foco.
//   - Atalhos globais so valem quando nenhum campo de texto esta digitando.
//
// ============================================================================================================
// 3. PADROES DE DADOS
// ============================================================================================================
// 3.1 Textos
//   - T("Texto em ingles") devolve a traducao do idioma escolhido (assets/lang/*.json). A chave e o proprio
//     texto em ingles. Se nao houver traducao, aparece o ingles.
//   - F("texto %d", x) = formatacao com traducao.
// 3.2 Estado do editor (editor_state.hpp)
//   - Selecao (ed.selected), visibilidade de janelas (showX), opcoes de layout (lockInterface, dockWithShift,
//     showCulled...), configuracoes (fpsMode, pixelFilter, theme, lang).
//   - Dados da cena: ECS (tool::World) com componentes (components.hpp). A UI le e escreve direto neles.
//   - Desfazer: ed.undoTick(...) recebe "algo esta sendo editado agora" para agrupar alteracoes.
// 3.3 Persistencia
//   - editor_settings.json   opcoes do editor (idioma, tema, API grafica, layout travado...).
//   - editor_dock_layout.txt layout das janelas (salvo por Janela > Salvar layout).
//   - editor_theme.json      tema escolhido/personalizado.
//   - Project/project.json   configuracoes do projeto (inclui editorInfo para exportacao).
// 3.4 Jogo (runtime)
//   - CanvasComp (components.hpp): canvas com ancora e tamanho, lista de elementos UiElem (texto, barra, botao) ligada a uma camera.
//   - fx.hpp formata o texto de UI do jogo ("{variavel}"); view_overlays.hpp desenha os elementos.
//   - Botoes do jogo: reagem so durante o Play (eventos "ui_show", "ui_hide", acoes).
//
// ============================================================================================================
// 4. REGRAS QUE A INTERFACE RESPEITA (comportamento que o usuario ja conhece)
// ============================================================================================================
//   R1. Layout de janelas salvo e restaurado pelo arquivo editor_dock_layout.txt.
//   R2. Atalhos de objeto (Delete, F2) nao agem dentro de campo de texto.
//   R3. Temas: 5 cores-base geram a paleta (sui_palette.hpp); escala e arredondamento vem do tema (theme.hpp).
//   R4. O jogo exportado usa a mesma UI do editor (modo --player) e nao pode depender do editor.
//
// ============================================================================================================
// 5. MAPA ARQUIVO -> RESPONSABILIDADE
// ============================================================================================================
//   main.cpp                       Laco SDL, menus, dock_host, roteamento de eventos e ligacao dos paineis com o
//                                  Editor (hierarquia, inspetor, configuracoes, cena).
//   dock_host.hpp, dock_layout.hpp Encaixe das janelas, grip, abas e guias de encaixe.
//   framed_window.hpp,             Moldura SpriteUI (titulo, X, "?", grip).
//   window_frame.hpp
//   hierarchy_panel.hpp,           Paineis SpriteUI das janelas: Hierarchy, Inspector, Scene, Jogo, Assets,
//   inspector_panel.hpp,           Scenes, Console, Profiler, Settings, Coding, Script, Narrative, Export,
//   scene_view_panel.hpp,          Input Map, Help, barra de status, menus e tela de inicio.
//   game_view_panel.hpp,
//   assets_spriteui.hpp, scenes_spriteui.hpp, console_panel.hpp, profiler_panel.hpp, settings_panel.hpp,
//   coding_panel.hpp, script_window.hpp, narrative_panel.hpp, export_panel.hpp, input_map_panel.hpp,
//   help_panel.hpp, statusbar_panel.hpp, mainmenu_panel.hpp, test_map_panel.hpp, start_screen_spriteui.hpp
//   settings_form.hpp              Linhas de formulario da janela Configuracoes.
//   sui_palette.hpp, theme.hpp     Paleta, cores-base e temas.
//   ui_tokens.hpp, ui_style.hpp,   Tokens, estilo e presets de interface.
//   ui_presets.hpp
//   i18n.hpp                       T(), F() e pacotes de idioma.
//   fx.hpp                         Particulas e formatacao do texto de UI do jogo.
//   view_overlays.hpp              Overlays desenhados no viewport e UI do jogo.
//   viewport_tools.hpp             Selecao por raio e ferramentas do viewport (mouse).
//   editor_scripts.hpp             Teclas de movimento no Play.
//   ui_test.cpp                    Teste sem janela (idiomas e temas).
//   profiler.hpp                   Dados do Profiler (usados pelos graficos).
