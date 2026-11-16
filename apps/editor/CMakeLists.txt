add_executable(tool_editor main.cpp image_io.cpp)
target_include_directories(tool_editor PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(tool_editor PRIVATE tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::audio_miniaudio tool::assets tool::pack tool::mod imgui_lib)
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
  # Camada L4b (tela de inicio sem janela): estado dos campos, foco, selecao e acoes do modelo.
  add_executable(editor_start_screen_model_tests tests/test_start_screen_model.cpp)
  target_include_directories(editor_start_screen_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_start_screen_model_tests PRIVATE SpriteUi::SpriteUI tool::scene tool::render tool::narrative tool::physics tool::input tool::rhi tool::audio tool::audio_miniaudio tool::assets tool::pack tool::mod)
  add_test(NAME Editor.StartScreenModel COMMAND editor_start_screen_model_tests)

  # Modelo da janela Help (help_model.hpp): blocos do texto, busca por idioma e edicao da busca.
  add_executable(editor_help_model_tests tests/test_help_model.cpp)
  target_include_directories(editor_help_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(editor_help_model_tests PRIVATE SpriteUi::SpriteUI)
  add_test(NAME Editor.HelpModel COMMAND editor_help_model_tests)
  target_compile_definitions(tool_editor PRIVATE TOOL_WITH_SPRITEUI=1)
endif()

add_executable(tool_ui_test ui_test.cpp)
target_link_libraries(tool_ui_test PRIVATE imgui_lib tool::core)
add_test(NAME ui_editor COMMAND tool_ui_test)

# Presets do visual em JSON (ui_presets.hpp): ida e volta, limites, leitura tolerante e arquivos.
add_executable(editor_ui_presets_tests tests/test_ui_presets.cpp)
target_include_directories(editor_ui_presets_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_ui_presets_tests PRIVATE tool::core)
add_test(NAME Editor.UiPresets COMMAND editor_ui_presets_tests)

# Modelo do painel do Profiler (profiler_model.hpp): quais numeros aparecem, formatacao fixa e taxa de atualizacao.
add_executable(editor_profiler_model_tests tests/test_profiler_model.cpp)
target_include_directories(editor_profiler_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_profiler_model_tests PRIVATE tool::core)
add_test(NAME Editor.ProfilerModel COMMAND editor_profiler_model_tests)

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

# Regra que grava os pedidos da janela Configuracoes no Editor (settings_changes.hpp), com alvo falso.
add_executable(editor_settings_changes_tests tests/test_settings_changes.cpp)
target_include_directories(editor_settings_changes_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.SettingsChanges COMMAND editor_settings_changes_tests)

# Modelo da janela Hierarchy (hierarchy_model.hpp): arvore, selecao por id, teclas e faixa visivel.
add_executable(editor_hierarchy_model_tests tests/test_hierarchy_model.cpp)
target_include_directories(editor_hierarchy_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_hierarchy_model_tests PRIVATE SpriteUi::SpriteUI)
add_test(NAME Editor.HierarchyModel COMMAND editor_hierarchy_model_tests)

# Modelo do Inspector (inspector_model.hpp): expansores, limites de slider, NaN, numero digitado e cor HEX. Sem janela.
add_executable(editor_inspector_model_tests tests/test_inspector_model.cpp)
target_include_directories(editor_inspector_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME Editor.InspectorModel COMMAND editor_inspector_model_tests)

# Modelo da janela Scene (scene_view_model.hpp): projecao com pixel de erro menor que 1, gizmo, arrasto, ima e grade.
add_executable(editor_scene_view_model_tests tests/test_scene_view_model.cpp)
target_include_directories(editor_scene_view_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_scene_view_model_tests PRIVATE tool::math tool::render)
add_test(NAME Editor.SceneViewModel COMMAND editor_scene_view_model_tests)

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

add_test(NAME editor_selftest COMMAND tool_editor --selftest WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(editor_selftest PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 120)

# Copia o que o editor precisa para rodar para "Projeto Final" (ver CMakeLists.txt da raiz)
add_custom_command(TARGET tool_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${TOOL_FINAL_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/LEIA-ME_Projeto_Final.txt" "${TOOL_FINAL_DIR}/LEIA-ME.txt"
  COMMAND_EXPAND_LISTS VERBATIM)
