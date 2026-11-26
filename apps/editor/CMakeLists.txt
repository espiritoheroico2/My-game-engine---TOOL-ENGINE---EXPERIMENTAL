add_executable(tool_editor main.cpp image_io.cpp)
target_include_directories(tool_editor PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(tool_editor PRIVATE tool::scene tool::canvas tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod SDL3::SDL3-static)
if(TOOL_WITH_MINIAUDIO)
  target_link_libraries(tool_editor PRIVATE tool::audio_miniaudio)
endif()
# TOOL_HAS_MINIAUDIO=1 liga o dispositivo real; sem ele o editor roda sem saida de audio (ensureAudio).
if(TOOL_WITH_MINIAUDIO)
  target_compile_definitions(tool_editor PRIVATE TOOL_HAS_MINIAUDIO=1)
endif()
if(MSVC)
  target_compile_options(tool_editor PRIVATE /bigobj)
endif()
if(TOOL_WITH_QUICKJS AND TARGET qjs)
  target_link_libraries(tool_editor PRIVATE qjs)
  target_compile_definitions(tool_editor PRIVATE TOOL_WITH_QUICKJS=1)
endif()
if(TOOL_WITH_DILIGENT)
  target_link_libraries(tool_editor PRIVATE tool::rhi_diligent)
  target_compile_definitions(tool_editor PRIVATE TOOL_EDITOR_GPU=1)
endif()
if(TOOL_WITH_SPRITEUI)
  target_link_libraries(tool_editor PRIVATE SpriteUi::SpriteUI)
  target_link_libraries(tool_editor PRIVATE SpriteUi::TextStb)
  add_executable(editor_spriteui_native_widgets_tests tests/test_spriteui_native_widgets.cpp)
  target_link_libraries(editor_spriteui_native_widgets_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.SpriteUiNativeWidgets COMMAND editor_spriteui_native_widgets_tests)
  # Camada L4b (tela de inicio sem janela): estado dos campos, foco, selecao e acoes do modelo.
  add_executable(editor_start_screen_model_tests tests/test_start_screen_model.cpp)
  target_include_directories(editor_start_screen_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_start_screen_model_tests PRIVATE SpriteUi::SpriteUI tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_start_screen_model_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.StartScreenModel COMMAND editor_start_screen_model_tests)

  add_executable(editor_info_model_tests tests/test_editor_info_model.cpp)
  target_include_directories(editor_info_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_info_model_tests PRIVATE SpriteUi::SpriteUI tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_info_model_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.EditorInfoModel COMMAND editor_info_model_tests)

  # Testes de elementos da janela Editor info (X, grip, campo, Save, batches). Padrao para as outras janelas.
  add_executable(editor_info_view_tests tests/test_editor_info_view.cpp)
  target_include_directories(editor_info_view_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(editor_info_view_tests PRIVATE EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/fonts/Xmind-Regular.ttf")
  target_link_libraries(editor_info_view_tests PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_info_view_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.EditorInfoView COMMAND editor_info_view_tests)

  # Secao Tags do Inspector (inspector_panel.hpp): entra no bloco e ocupa altura. Sem janela.
  add_executable(editor_inspector_tags_tests tests/test_inspector_tags.cpp)
  target_include_directories(editor_inspector_tags_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(editor_inspector_tags_tests PRIVATE EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/fonts/Xmind-Regular.ttf")
  target_link_libraries(editor_inspector_tags_tests PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_inspector_tags_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.InspectorTags COMMAND editor_inspector_tags_tests)

  # Paleta da SpriteUI: padrao, fallback e contraste do texto (sem janela, sem SpriteUI).
  add_executable(sui_palette_tests tests/test_sui_palette.cpp)
  target_include_directories(sui_palette_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME Editor.SuiPalette COMMAND sui_palette_tests)

  # Simplificacao de malha (faces removidas sem buracos, bordas e quinas preservadas).
  add_executable(mesh_simplify_tests tests/test_mesh_simplify.cpp)
  target_link_libraries(mesh_simplify_tests PRIVATE tool::render)
  add_test(NAME Editor.MeshSimplify COMMAND mesh_simplify_tests)

  # Moldura do Console (console_frame.hpp): posicao, X, arraste pelo grip e limites. Sem janela.
  add_executable(editor_window_frame_tests tests/test_window_frame.cpp)
  target_include_directories(editor_window_frame_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_window_frame_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.WindowFrame COMMAND editor_window_frame_tests)

  # Docking das janelas (dock_layout.hpp): layout padrao, abas, encaixe por arraste e flutuar. Sem janela.
  add_executable(editor_dock_layout_tests tests/test_dock_layout.cpp)
  target_include_directories(editor_dock_layout_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_dock_layout_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.DockLayout COMMAND editor_dock_layout_tests)

  # Cursor e destaque da borda ao redimensionar (resize_hover.hpp): regras puras.
  add_executable(editor_resize_hover_tests tests/test_resize_hover.cpp)
  target_include_directories(editor_resize_hover_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_resize_hover_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.ResizeHover COMMAND editor_resize_hover_tests)

  # Gestos do host do dock (grip, borda, repeticao): sem janela, so SDL_Event e geometria.
  add_executable(editor_dock_host_tests tests/test_dock_host.cpp)
  target_include_directories(editor_dock_host_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_dock_host_tests PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  add_test(NAME Editor.DockHost COMMAND editor_dock_host_tests)

  # Captura headless da janela Editor info (SpriteUI). Nao e um teste do ctest: gera PPM para conferir visualmente.
  add_executable(editor_info_capture tools/editor_info_capture.cpp)
  target_include_directories(editor_info_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_info_capture PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_info_capture PRIVATE tool::audio_miniaudio)
  endif()

  # Captura headless da janela Assets (assets_spriteui.hpp): PPM por cenario, sem janela.
  # Captura headless da janela Narrative (SpriteUI): grafo de exemplo para conferir o desenho.
  add_executable(editor_narrative_capture tools/narrative_capture.cpp)
  target_include_directories(editor_narrative_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_narrative_capture PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  add_executable(editor_assets_capture tools/assets_capture.cpp)
  target_include_directories(editor_assets_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_assets_capture PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_assets_capture PRIVATE tool::audio_miniaudio)
  endif()

  # Modelo da janela Help (help_model.hpp): blocos do texto, busca por idioma e edicao da busca.
  add_executable(editor_help_model_tests tests/test_help_model.cpp)
  target_include_directories(editor_help_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_help_model_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.HelpModel COMMAND editor_help_model_tests)

  # Modelo da janela Scenes (scenes_model.hpp): ordem das cenas, acoes por selecao, nome e assets da cena.
  add_executable(editor_scenes_model_tests tests/test_scenes_model.cpp)
  target_include_directories(editor_scenes_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_scenes_model_tests PRIVATE SpriteUi::SpriteUI tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TARGET tool::audio_miniaudio)
    target_link_libraries(editor_scenes_model_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.ScenesModel COMMAND editor_scenes_model_tests)

  # Modelo da janela Assets (assets_model.hpp): migalhas, ordem, filtro, acoes por tipo, nomes e selecao. Sem janela.
  add_executable(editor_assets_model_tests tests/test_assets_model.cpp)
  target_include_directories(editor_assets_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_assets_model_tests PRIVATE SpriteUi::SpriteUI tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_assets_model_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.AssetsModel COMMAND editor_assets_model_tests)

  # Janela Scenes em SpriteUI (scenes_spriteui.hpp): lista, botoes, campo de nome, assets, X e grip. Sem janela.
  add_executable(editor_scenes_view_tests tests/test_scenes_view.cpp)
  target_include_directories(editor_scenes_view_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(editor_scenes_view_tests PRIVATE EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/fonts/Xmind-Regular.ttf")
  target_link_libraries(editor_scenes_view_tests PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_scenes_view_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.ScenesView COMMAND editor_scenes_view_tests)

  # Janela Assets em SpriteUI (assets_spriteui.hpp): cliques, duplo clique, renomear, apagar, filtro, texto, rolagem.
  add_executable(editor_assets_view_tests tests/test_assets_view.cpp)
  target_include_directories(editor_assets_view_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(editor_assets_view_tests PRIVATE EDITOR_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/fonts/Xmind-Regular.ttf")
  target_link_libraries(editor_assets_view_tests PRIVATE SDL3::SDL3-static SpriteUi::SpriteUI SpriteUi::TextStb tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::assets tool::pack tool::mod)
  if(TOOL_WITH_MINIAUDIO)
    target_link_libraries(editor_assets_view_tests PRIVATE tool::audio_miniaudio)
  endif()
  add_test(NAME Editor.AssetsView COMMAND editor_assets_view_tests)

  # Bloco "engine" do project.json: modelo puro, limites e ida-e-volta JSON.
  add_executable(editor_project_engine_settings_tests tests/test_project_engine_settings.cpp)
  target_include_directories(editor_project_engine_settings_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_project_engine_settings_tests PRIVATE tool::core)
  add_test(NAME Editor.ProjectEngineSettings COMMAND editor_project_engine_settings_tests)

  target_compile_definitions(tool_editor PRIVATE TOOL_WITH_SPRITEUI=1)
endif()

add_executable(tool_ui_test ui_test.cpp)
target_link_libraries(tool_ui_test PRIVATE tool::core)
add_test(NAME ui_editor COMMAND tool_ui_test)

# Presets do visual em JSON (ui_presets.hpp): ida e volta, limites, leitura tolerante e arquivos.
add_executable(editor_window_manager_tests tests/test_window_manager.cpp window_manager.cpp)
target_include_directories(editor_window_manager_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.WindowManager COMMAND editor_window_manager_tests)

if(TOOL_WITH_SPRITEUI)
  add_executable(editor_panel_router_tests tests/test_panel_router.cpp)
  target_include_directories(editor_panel_router_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_panel_router_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.PanelRouter COMMAND editor_panel_router_tests)

  # Adaptador da SpriteUI para o ECS da ToolEngine: entidade, pai, layout, visual e extracao.
  add_executable(editor_spriteui_ecs_tests tests/test_spriteui_ecs.cpp)
  target_include_directories(editor_spriteui_ecs_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_spriteui_ecs_tests PRIVATE tool::ecs SpriteUi::SpriteUI)
  add_test(NAME Editor.SpriteUiEcs COMMAND editor_spriteui_ecs_tests)
endif()

add_executable(editor_ui_presets_tests tests/test_ui_presets.cpp)
target_include_directories(editor_ui_presets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_ui_presets_tests PRIVATE tool::core)
add_test(NAME Editor.UiPresets COMMAND editor_ui_presets_tests)

# Politica de remocao do cache de malhas de chunks (chunk_cache.hpp, F-08): teto, lote menos recente e simulacao.
add_executable(editor_chunk_cache_tests tests/test_chunk_cache.cpp)
target_include_directories(editor_chunk_cache_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.ChunkCache COMMAND editor_chunk_cache_tests)

# Modelo do painel do Profiler (profiler_model.hpp): quais numeros aparecem, formatacao fixa e taxa de atualizacao.
add_executable(editor_profiler_model_tests tests/test_profiler_model.cpp)
target_include_directories(editor_profiler_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(editor_profiler_model_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/include)
target_link_libraries(editor_profiler_model_tests PRIVATE tool::core)
add_test(NAME Editor.ProfilerModel COMMAND editor_profiler_model_tests)

# Exportacao do Profiler: snapshots completos e serializacao Markdown/CSV, sem janela nem SDL.
add_executable(editor_profiler_export_tests tests/test_profiler_export.cpp)
target_include_directories(editor_profiler_export_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(editor_profiler_export_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/include)
target_link_libraries(editor_profiler_export_tests PRIVATE tool::core)
add_test(NAME Editor.ProfilerExport COMMAND editor_profiler_export_tests)

# Modelo da barra de status (statusbar_model.hpp): textos, tons e refresh de 4 vezes por segundo.
add_executable(editor_statusbar_model_tests tests/test_statusbar_model.cpp)
target_include_directories(editor_statusbar_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_statusbar_model_tests PRIVATE tool::core)
add_test(NAME Editor.StatusBarModel COMMAND editor_statusbar_model_tests)

# Modelo da janela Test map of chunks (test_map_model.hpp): contagens, aviso de mapa grande e cursor do trilho.
add_executable(editor_test_map_model_tests tests/test_test_map_model.cpp)
target_include_directories(editor_test_map_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_test_map_model_tests PRIVATE tool::core)
add_test(NAME Editor.TestMapModel COMMAND editor_test_map_model_tests)
# Modelo do painel do Console (console_model.hpp): niveis, filtro, contagens, copia, campo de linha e historico.
add_executable(editor_console_model_tests tests/test_console_model.cpp)
target_include_directories(editor_console_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_console_model_tests PRIVATE tool::core)
add_test(NAME Editor.ConsoleModel COMMAND editor_console_model_tests)

# Modelo da barra de menus principal (mainmenu_model.hpp): titulos, itens, marcas, pedidos e botoes Play/Pausa.
add_executable(editor_mainmenu_model_tests tests/test_mainmenu_model.cpp)
target_include_directories(editor_mainmenu_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_mainmenu_model_tests PRIVATE tool::core)
add_test(NAME Editor.MainMenuModel COMMAND editor_mainmenu_model_tests)

# Modelo da janela Configuracoes (settings_model.hpp): abas, limites de escolha, tamanho do jogo, sombra e divisao.
add_executable(editor_settings_model_tests tests/test_settings_model.cpp)
target_include_directories(editor_settings_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.SettingsModel COMMAND editor_settings_model_tests)

# Ponte da janela Configuracoes para os elementos SpriteUI (settings_bridge.hpp).
add_executable(editor_settings_bridge_tests tests/test_settings_bridge.cpp)
target_include_directories(editor_settings_bridge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_settings_bridge_tests PRIVATE SpriteUi::SpriteUI)
add_test(NAME Editor.SettingsBridge COMMAND editor_settings_bridge_tests)

# Modelo do Controlador Narrativo (narrative_model.hpp): nos, ligacoes, bloco fixo, variaveis e teste rapido.
add_executable(editor_narrative_model_tests tests/test_narrative_model.cpp)
target_include_directories(editor_narrative_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_narrative_model_tests PRIVATE tool::narrative)
add_test(NAME Editor.NarrativeModel COMMAND editor_narrative_model_tests)

# Captura do mouse dos paineis SpriteUI (pointer_capture.hpp): Up fora do painel e movimento sem botao.
add_executable(editor_pointer_capture_tests tests/test_pointer_capture.cpp)
target_include_directories(editor_pointer_capture_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.PointerCapture COMMAND editor_pointer_capture_tests)

# Regra que grava os pedidos da janela Configuracoes no Editor (settings_changes.hpp), com alvo falso.
add_executable(editor_settings_changes_tests tests/test_settings_changes.cpp)
target_include_directories(editor_settings_changes_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.SettingsChanges COMMAND editor_settings_changes_tests)

# Campo numerico dos deslizantes (slider_value_edit.hpp): texto digitado, arredondamento e limites.
add_executable(editor_slider_value_edit_tests tests/test_slider_value_edit.cpp)
target_include_directories(editor_slider_value_edit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.SliderValueEdit COMMAND editor_slider_value_edit_tests)

# Modelo da janela Hierarchy (hierarchy_model.hpp): arvore, selecao por id, teclas e faixa visivel.
add_executable(editor_hierarchy_model_tests tests/test_hierarchy_model.cpp)
target_include_directories(editor_hierarchy_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_hierarchy_model_tests PRIVATE SpriteUi::SpriteUI)
add_test(NAME Editor.HierarchyModel COMMAND editor_hierarchy_model_tests)

# Indice id -> posicao do Sync (mesmos ids, reordenados, removidos, novos, rotulo alterado).
add_executable(editor_hierarchy_index_tests tests/test_hierarchy_index.cpp)
target_include_directories(editor_hierarchy_index_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_hierarchy_index_tests PRIVATE SpriteUi::SpriteUI)
add_test(NAME Editor.HierarchyIndex COMMAND editor_hierarchy_index_tests)

# Modelo do Inspector (inspector_model.hpp): expansores, limites de slider, NaN, numero digitado e cor HEX. Sem janela.
add_executable(editor_inspector_model_tests tests/test_inspector_model.cpp)
target_include_directories(editor_inspector_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.InspectorModel COMMAND editor_inspector_model_tests)

# Modelo da janela Scene (scene_view_model.hpp): projecao com pixel de erro menor que 1, gizmo, arrasto, ima e grade.
add_executable(editor_scene_view_model_tests tests/test_scene_view_model.cpp)
target_include_directories(editor_scene_view_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_scene_view_model_tests PRIVATE tool::math tool::render)
add_test(NAME Editor.SceneViewModel COMMAND editor_scene_view_model_tests)

add_executable(editor_game_grid_tests tests/test_game_grid.cpp)
target_include_directories(editor_game_grid_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_game_grid_tests PRIVATE tool::render)
add_test(NAME Editor.GameGrid COMMAND editor_game_grid_tests)

# Menu flutuante da Scene (scene_transform_menu.hpp): teclas W, E, R, Q, passo do ima e arrasto com e sem encaixe.
add_executable(editor_scene_transform_menu_tests tests/test_scene_transform_menu.cpp)
target_include_directories(editor_scene_transform_menu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_scene_transform_menu_tests PRIVATE tool::math tool::render)
add_test(NAME Editor.SceneTransformMenu COMMAND editor_scene_transform_menu_tests)

# Modelo da janela Jogo (game_view_model.hpp): letterbox, divisao entre duas cameras e recorte do preencher.
add_executable(editor_game_view_model_tests tests/test_game_view_model.cpp)
target_include_directories(editor_game_view_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_game_view_model_tests PRIVATE SpriteUi::SpriteUI)
add_test(NAME Editor.GameViewModel COMMAND editor_game_view_model_tests)

add_executable(editor_ui_icons_tests tests/test_ui_icons.cpp)
target_include_directories(editor_ui_icons_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.UiIcons COMMAND editor_ui_icons_tests)
add_executable(editor_ui_tokens_tests tests/test_ui_tokens.cpp)
target_include_directories(editor_ui_tokens_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.UiTokens COMMAND editor_ui_tokens_tests)

if(TOOL_WITH_SPRITEUI)
  add_executable(editor_spriteui_layers_input_tests ${CMAKE_SOURCE_DIR}/third_party/SpriteUi/tests/test_layers_and_input.cpp)
  target_link_libraries(editor_spriteui_layers_input_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.SpriteUiLayersInput COMMAND editor_spriteui_layers_input_tests)
  add_executable(editor_spriteui_frame_cost_tests ${CMAKE_SOURCE_DIR}/third_party/SpriteUi/tests/test_frame_cost.cpp)
  target_link_libraries(editor_spriteui_frame_cost_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.SpriteUiFrameCost COMMAND editor_spriteui_frame_cost_tests)
  add_executable(editor_spriteui_splitter_tests ${CMAKE_SOURCE_DIR}/third_party/SpriteUi/tests/test_splitter.cpp)
  target_link_libraries(editor_spriteui_splitter_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.SpriteUiSplitter COMMAND editor_spriteui_splitter_tests)
  add_executable(editor_spriteui_dock_guides_tests ${CMAKE_SOURCE_DIR}/third_party/SpriteUi/tests/test_dock_guides.cpp)
  target_link_libraries(editor_spriteui_dock_guides_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.SpriteUiDockGuides COMMAND editor_spriteui_dock_guides_tests)
  add_executable(editor_spriteui_ui_scale_tests ${CMAKE_SOURCE_DIR}/third_party/SpriteUi/tests/test_ui_scale.cpp)
  target_link_libraries(editor_spriteui_ui_scale_tests PRIVATE SpriteUi::TextStb)
  target_compile_definitions(editor_spriteui_ui_scale_tests PRIVATE
    "SPRITEUI_UI_SCALE_TEST_FONT=\"${CMAKE_SOURCE_DIR}/third_party/SpriteUi/Fonts/Xmind/static/Xmind-Regular.ttf\"")
  add_test(NAME Editor.SpriteUiUiScale COMMAND editor_spriteui_ui_scale_tests)

  if(TOOL_BUILD_TESTS)
    add_executable(spriteui_frame_benchmark ${CMAKE_SOURCE_DIR}/third_party/SpriteUi/benchmarks/spriteui_frame_benchmark.cpp)
    target_link_libraries(spriteui_frame_benchmark PRIVATE SpriteUi::TextStb)
    target_compile_definitions(spriteui_frame_benchmark PRIVATE
      "SPRITEUI_BENCH_FONT_PATH=\"${CMAKE_SOURCE_DIR}/third_party/SpriteUi/Fonts/Xmind/static/Xmind-Regular.ttf\"")
  endif()
endif()

add_test(NAME editor_selftest COMMAND tool_editor --selftest WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(editor_selftest PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 120)

# Copia o que o editor precisa para rodar para "Projeto Final" (ver CMakeLists.txt da raiz)
add_custom_command(TARGET tool_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${TOOL_FINAL_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/referencia/LEIA_ME_PROJETO_FINAL.txt" "${TOOL_FINAL_DIR}/LEIA-ME.txt"
  COMMAND_EXPAND_LISTS VERBATIM)
