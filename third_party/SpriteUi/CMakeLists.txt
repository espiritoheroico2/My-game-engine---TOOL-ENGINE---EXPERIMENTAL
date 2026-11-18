cmake_minimum_required(VERSION 3.21)
project(SpriteUI VERSION 0.6.0 LANGUAGES CXX)

option(SPRITEUI_BUILD_EXAMPLES "Build SpriteUI examples" ON)
option(SPRITEUI_BUILD_TESTS "Build SpriteUI tests" ON)
option(SPRITEUI_BUILD_BENCHMARKS "Build lightweight CPU benchmarks" OFF)
option(SPRITEUI_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(SPRITEUI_BUILD_SDL3_ADAPTER "Build the optional SDL3 window-input adapter" OFF)
option(SPRITEUI_BUILD_VISUALIZER "Build the interactive SDL3 CPU visualizer" OFF)
option(SPRITEUI_BUILD_DOCS "Build the Doxygen API reference" OFF)
option(SPRITEUI_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(SPRITEUI_WITH_TEXT_STB "Build the optional stb_truetype glyph atlas (SpriteUi::TextStb)" OFF)

add_library(SpriteUI
    src/Input/InputRouter.cpp
    src/Input/MultiPointerRouter.cpp
    src/Input/SpatialNavigation.cpp
    src/Animation/ToonMotion.cpp
    src/Rendering/DrawList.cpp
    src/Rendering/NineSlice.cpp
    src/Layout/Layout.cpp
    src/Layout/LayoutTree.cpp
    src/Layout/LayoutBridge.cpp
    src/Text/DebugShaper.cpp
    src/Text/FontAtlasCache.cpp
    src/Text/FontRegistry.cpp
    src/Text/LineBreak.cpp
    src/Text/TextLayout.cpp
    src/Text/TextLines.cpp
    src/Widgets/Widgets.cpp
    src/Widgets/VirtualList.cpp
    src/Widgets/Docking.cpp
    src/Widgets/DockInteraction.cpp
    src/Widgets/DockOps.cpp
    src/Widgets/TextEdit.cpp
    src/Widgets/TextField.cpp
    src/Elements/Element.cpp
    src/Elements/ElementTree.cpp
    src/Elements/Label.cpp
    src/Elements/PressGesture.cpp
    src/Elements/Paint.cpp
    src/Elements/Checkbox.cpp
    src/Elements/Slider.cpp
    src/Elements/Button.cpp
    src/Elements/TextField.cpp
    src/Widgets/Controls.cpp
    src/Widgets/StylePresets.cpp
    src/Widgets/Graphs.cpp
    src/Widgets/WindowFrame.cpp
    src/Widgets/WindowFramePaint.cpp
    src/Widgets/WindowFrameTheme.cpp
    src/Widgets/Popup.cpp
    src/Widgets/FloatingWindows.cpp
    src/Core/RefreshPolicy.cpp
    src/Widgets/TreeView.cpp
    src/Widgets/Table.cpp
    src/Widgets/ScrollBar.cpp
    src/Widgets/Menu.cpp
    src/Widgets/Tooltip.cpp
    src/Widgets/Modal.cpp
    src/Widgets/ColorPicker.cpp
    src/Widgets/ProgressBar.cpp
    src/Widgets/TextArea.cpp
    src/Widgets/DragDrop.cpp
    src/Config/UIConfig.cpp
    src/Elements/RadioButton.cpp
    src/Elements/Toggle.cpp
    src/Elements/ListBox.cpp
    src/Elements/Dropdown.cpp
    src/Elements/TabBar.cpp
    src/Elements/Window.cpp
    src/Elements/Panel.cpp
    src/Elements/MenuBar.cpp
    src/Elements/Image.cpp
    src/Elements/Graph.cpp
    src/Elements/Splitter.cpp
    src/Widgets/DockGuides.cpp
    src/Text/UIScale.cpp
)
add_library(SpriteUi::SpriteUI ALIAS SpriteUI)

if(SPRITEUI_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_WARN_AS_ERROR NO)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/docs/API_REFERENCE.md")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    doxygen_add_docs(spriteui_docs
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/API_REFERENCE.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/TOOLENGINE_INTEGRATION_GUIDE.md"
        COMMENT "Generating SpriteUI API reference"
    )
endif()

if(SPRITEUI_BUILD_SDL3_ADAPTER)
    find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3)

    add_library(SpriteUI_SDL3Input
        src/Platform/SDL3InputAdapter.cpp
        src/Platform/SDL3Window.cpp
    )
    add_library(SpriteUi::SDL3Input ALIAS SpriteUI_SDL3Input)
    set_target_properties(SpriteUI_SDL3Input PROPERTIES EXPORT_NAME SDL3Input)
    target_compile_features(SpriteUI_SDL3Input PUBLIC cxx_std_20)
    target_include_directories(SpriteUI_SDL3Input
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(SpriteUI_SDL3Input
        PUBLIC SpriteUi::SpriteUI SDL3::SDL3
    )
    if(SPRITEUI_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(SpriteUI_SDL3Input PRIVATE /W4 /permissive-)
        else()
            target_compile_options(SpriteUI_SDL3Input PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        endif()
    endif()
endif()

target_compile_features(SpriteUI PUBLIC cxx_std_20)
target_include_directories(SpriteUI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(SPRITEUI_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(SpriteUI PRIVATE /W4 /permissive-)
    else()
        target_compile_options(SpriteUI PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

# Atlas de glifos com stb_truetype. Fica num alvo separado para que o nucleo SpriteUI continue sem dependencias.
if(SPRITEUI_WITH_TEXT_STB)
    add_library(SpriteUI_TextStb
        src/Text/TruetypeAtlas.cpp
        src/Text/TextShaper.cpp
    )
    add_library(SpriteUi::TextStb ALIAS SpriteUI_TextStb)
    target_compile_features(SpriteUI_TextStb PUBLIC cxx_std_20)
    target_include_directories(SpriteUI_TextStb
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    # stb_truetype.h e codigo de terceiros: SYSTEM suprime os avisos dele (ex.: -Wconversion), sem tocar no projeto.
    target_include_directories(SpriteUI_TextStb
        SYSTEM PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb>
    )
    target_link_libraries(SpriteUI_TextStb PUBLIC SpriteUi::SpriteUI)
endif()

if(SPRITEUI_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "SPRITEUI_BUILD_FUZZERS requires Clang with libFuzzer support.")
    endif()
    target_compile_options(SpriteUI PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(SpriteUI INTERFACE -fsanitize=address,undefined)

    add_executable(spriteui_fuzz_config tests/fuzz_config.cpp)
    target_link_libraries(spriteui_fuzz_config PRIVATE SpriteUi::SpriteUI)
    target_compile_options(spriteui_fuzz_config PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(spriteui_fuzz_config PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(SPRITEUI_BUILD_EXAMPLES)
    add_executable(spriteui_hello examples/hello_spriteui.cpp)
    target_link_libraries(spriteui_hello PRIVATE SpriteUi::SpriteUI)

    add_executable(spriteui_interaction_demo examples/interaction_demo.cpp)
    target_link_libraries(spriteui_interaction_demo PRIVATE SpriteUi::SpriteUI)
endif()

if(SPRITEUI_BUILD_VISUALIZER)
    if(NOT WIN32)
        message(FATAL_ERROR "The visualizer's TTF/OTF font rasterizer currently requires Windows GDI.")
    endif()
    find_package(SDL3 3.2 CONFIG REQUIRED COMPONENTS SDL3)

    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/Fonts/Xmind"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/Fonts")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/fotos"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

    add_executable(spriteui_visualizer
        examples/BitmapFont.cpp
        examples/BitmapImage.cpp
        examples/spriteui_visualizer.cpp
    )
    target_include_directories(spriteui_visualizer PRIVATE tests)
    target_link_libraries(spriteui_visualizer PRIVATE SpriteUi::SpriteUI SDL3::SDL3)
    if(WIN32)
        target_link_libraries(spriteui_visualizer PRIVATE gdi32 ole32 windowscodecs)
    endif()
    add_custom_command(TARGET spriteui_visualizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Fonts/Xmind"
            "$<TARGET_FILE_DIR:spriteui_visualizer>/Fonts/Xmind"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/fotos"
            "$<TARGET_FILE_DIR:spriteui_visualizer>/fotos"
        VERBATIM
    )
    if(SPRITEUI_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(spriteui_visualizer PRIVATE /W4 /permissive-)
        else()
            target_compile_options(spriteui_visualizer PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        endif()
    endif()
    if(WIN32)
        add_custom_command(TARGET spriteui_visualizer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:spriteui_visualizer>"
            VERBATIM
        )
    endif()
    if(SPRITEUI_BUILD_TESTS)
        add_test(NAME SpriteUI.VisualizerSnapshot
            COMMAND spriteui_visualizer --headless)
        set_tests_properties(SpriteUI.VisualizerSnapshot PROPERTIES
            ENVIRONMENT "SPRITEUI_SNAPSHOT_DIR=${CMAKE_CURRENT_BINARY_DIR}/visualizer-snapshots")
    endif()
endif()

if(SPRITEUI_BUILD_TESTS)
    enable_testing()
    if(SPRITEUI_BUILD_SDL3_ADAPTER)
        add_executable(spriteui_sdl3_input_tests tests/sdl3_input_adapter_tests.cpp)
        target_link_libraries(spriteui_sdl3_input_tests PRIVATE SpriteUi::SDL3Input)
        if(WIN32)
            add_custom_command(TARGET spriteui_sdl3_input_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL3::SDL3>"
                    "$<TARGET_FILE_DIR:spriteui_sdl3_input_tests>"
                VERBATIM
            )
        endif()
        add_test(NAME SpriteUI.SDL3Input COMMAND spriteui_sdl3_input_tests)
        set_tests_properties(SpriteUI.SDL3Input PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
        add_test(NAME SpriteUI.SDL3Clipboard
            COMMAND spriteui_sdl3_input_tests --clipboard)
        set_tests_properties(SpriteUI.SDL3Clipboard PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            SKIP_RETURN_CODE 77)
    endif()

    add_executable(spriteui_tests
        tests/test_main.cpp
        tests/test_graph_sampling.cpp
        tests/test_text_breaking.cpp
        tests/test_text_layout.cpp
        tests/test_flex_wrap.cpp
        tests/test_nine_slice.cpp
        tests/test_visual.cpp
        tests/test_robustness.cpp
        tests/Legacy/LegacyLayout.cpp
    )
    target_include_directories(spriteui_tests PRIVATE tests tests/Legacy)
    target_link_libraries(spriteui_tests PRIVATE SpriteUi::SpriteUI)
    # Camada L3 (docking, interacao): roteiros de eventos do ponteiro.
    add_executable(spriteui_dock_tests tests/test_dock_interaction.cpp)
    target_link_libraries(spriteui_dock_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.DockInteraction COMMAND spriteui_dock_tests)

    add_executable(spriteui_layers_input_tests tests/test_layers_and_input.cpp)
    target_link_libraries(spriteui_layers_input_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.LayersAndInput COMMAND spriteui_layers_input_tests)

    # Camada L4 (campo de texto): edicao, selecao, UTF-8, desfazer e persistencia do texto.
    add_executable(spriteui_text_edit_tests tests/test_text_edit.cpp)
    target_link_libraries(spriteui_text_edit_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.TextEdit COMMAND spriteui_text_edit_tests)

    # Campo de texto generico (TextField): rolagem que segue o caret, recorte a caixa e clique com rolagem.
    add_executable(spriteui_text_field_tests tests/test_text_field.cpp)
    target_link_libraries(spriteui_text_field_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.TextField COMMAND spriteui_text_field_tests)

    # Base de elementos (Element, ElementTree): geometria da caixa, ancoras, min/max e handles da arvore.
    add_executable(spriteui_element_tests tests/test_elements.cpp)
    target_include_directories(spriteui_element_tests PRIVATE tests)
    target_link_libraries(spriteui_element_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Elements COMMAND spriteui_element_tests)
    # Registro de fontes (FontRegistry): ids estaveis, fonte padrao e slice por fonte. Sem atlas real.
    add_executable(spriteui_font_registry_tests tests/test_font_registry.cpp)
    target_link_libraries(spriteui_font_registry_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.FontRegistry COMMAND spriteui_font_registry_tests)
    # Politica de resolucao do atlas bitmap (BitmapRasterPolicy.hpp): buckets, DPI e O configuravel.
    add_executable(spriteui_bitmap_raster_policy_tests tests/test_bitmap_raster_policy.cpp)
    target_link_libraries(spriteui_bitmap_raster_policy_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.BitmapRasterPolicy COMMAND spriteui_bitmap_raster_policy_tests)
    # Derivacao da flag do shader a partir do formato do atlas (FontRenderMetadata.hpp).
    add_executable(spriteui_font_render_metadata_tests tests/test_font_render_metadata.cpp)
    target_link_libraries(spriteui_font_render_metadata_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.FontRenderMetadata COMMAND spriteui_font_render_metadata_tests)
    # Cache de atlas por FontAtlasKey: reuso, limite de entradas e bytes, e eviction que respeita uso.
    add_executable(spriteui_font_atlas_cache_tests tests/test_font_atlas_cache.cpp)
    target_link_libraries(spriteui_font_atlas_cache_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.FontAtlasCache COMMAND spriteui_font_atlas_cache_tests)
    # Camada L0 (layout): padroes de construcao (LayoutPatterns.hpp) medidos pelo LayoutTree.
    add_executable(spriteui_layout_pattern_tests tests/test_layout_patterns.cpp)
    target_include_directories(spriteui_layout_pattern_tests PRIVATE tests)
    target_link_libraries(spriteui_layout_pattern_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.LayoutPatterns COMMAND spriteui_layout_pattern_tests)

    if(SPRITEUI_WITH_TEXT_STB)
        add_executable(spriteui_truetype_tests tests/test_truetype_atlas.cpp)
        target_link_libraries(spriteui_truetype_tests PRIVATE SpriteUi::TextStb)
        target_compile_definitions(spriteui_truetype_tests PRIVATE
            "SPRITEUI_TEST_FONT_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/Fonts/Xmind/Xmind-VariableFont_wght.ttf\"")
        add_test(NAME SpriteUI.TruetypeAtlas COMMAND spriteui_truetype_tests)
    endif()

    # Visual tests write PNG screenshots here (also asserted on pixel values).
    add_test(NAME SpriteUI.CoreTests COMMAND spriteui_tests)
    set_tests_properties(SpriteUI.CoreTests PROPERTIES
        ENVIRONMENT "SPRITEUI_SNAPSHOT_DIR=${CMAKE_CURRENT_BINARY_DIR}/snapshots")

    add_test(NAME SpriteUI.PackageSmoke
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-smoke"
            "-DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DDEPENDENCY_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
            "-DPACKAGE_HAS_SDL3=${SPRITEUI_BUILD_SDL3_ADAPTER}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DEXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/PackageSmokeTest.cmake"
    )

    # Controles como modelos puros (Controls.hpp): checkbox, slider, dropdown, abas e lista. Sem janela.
    add_executable(spriteui_controls_tests tests/test_controls.cpp)
    target_link_libraries(spriteui_controls_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Controls COMMAND spriteui_controls_tests)

    # Presets de estilo (StylePresets.hpp): Classic, Modern e Effects. Forma e paleta separadas.
    add_executable(spriteui_style_preset_tests tests/test_style_presets.cpp)
    target_link_libraries(spriteui_style_preset_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.StylePresets COMMAND spriteui_style_preset_tests)

    # Janelas flutuantes (FloatingWindows.hpp): clamp no canvas, tamanho minimo, ordem Z, Float/Dock e encaixe.
    add_executable(spriteui_floating_window_tests tests/test_floating_windows.cpp)
    target_link_libraries(spriteui_floating_window_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.FloatingWindows COMMAND spriteui_floating_window_tests)

    # Politica de atualizacao por janela (RefreshPolicy.hpp): taxa em Hz, sujo e relogio injetado.
    add_executable(spriteui_refresh_policy_tests tests/test_refresh_policy.cpp)
    target_link_libraries(spriteui_refresh_policy_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.RefreshPolicy COMMAND spriteui_refresh_policy_tests)

    # Arvore de nos (TreeView.hpp): expansao, linhas visiveis com profundidade, teclado e selecao escondida.
    add_executable(spriteui_tree_view_tests tests/test_tree_view.cpp)
    target_link_libraries(spriteui_tree_view_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.TreeView COMMAND spriteui_tree_view_tests)

    # Tabela (Table.hpp): colunas fixas e proporcionais, cabecalho, linha sob o ponto e lista virtual.
    add_executable(spriteui_table_tests tests/test_table.cpp)
    target_link_libraries(spriteui_table_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Table COMMAND spriteui_table_tests)

    # Barra de rolagem (ScrollBar.hpp): polegar, pagina no trilho, arrasto, roda e eixo bloqueado.
    add_executable(spriteui_scroll_bar_tests tests/test_scroll_bar.cpp)
    target_link_libraries(spriteui_scroll_bar_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.ScrollBar COMMAND spriteui_scroll_bar_tests)

    # Menus (Menu.hpp): destaque, submenus em cascata, separadores, atalhos exibidos e barra de menus.
    add_executable(spriteui_menu_tests tests/test_menu.cpp)
    target_link_libraries(spriteui_menu_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Menu COMMAND spriteui_menu_tests)

    # Dica de ferramenta (Tooltip.hpp): atraso, troca de alvo, transicoes e posicao dentro do canvas.
    add_executable(spriteui_tooltip_tests tests/test_tooltip.cpp)
    target_link_libraries(spriteui_tooltip_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Tooltip COMMAND spriteui_tooltip_tests)

    # Grupo de radio (RadioButton.hpp) e chave Toggle (Toggle.hpp): exclusao mutua, teclado, geometria e itens.
    add_executable(spriteui_radio_toggle_tests tests/test_radio_toggle.cpp)
    target_link_libraries(spriteui_radio_toggle_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.RadioToggle COMMAND spriteui_radio_toggle_tests)

    # Lista (ListBox.hpp): selecao por identificador, Ctrl e Shift, teclas, rolagem e altura que cresce.
    add_executable(spriteui_list_box_tests tests/test_list_box.cpp)
    target_link_libraries(spriteui_list_box_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.ListBox COMMAND spriteui_list_box_tests)

    # Caixa de combinacao (Dropdown.hpp): abertura, escolha, Escape, posicao acima ou abaixo e rolagem da lista.
    add_executable(spriteui_dropdown_tests tests/test_dropdown.cpp)
    target_link_libraries(spriteui_dropdown_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Dropdown COMMAND spriteui_dropdown_tests)

    # Barra de abas (TabBar.hpp): abas que crescem com rolagem, setas, Enter e paineis ocultos com estado preservado.
    add_executable(spriteui_tab_bar_tests tests/test_tab_bar.cpp)
    target_link_libraries(spriteui_tab_bar_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.TabBar COMMAND spriteui_tab_bar_tests)

    # Janela e painel (Window.hpp, Panel.hpp): arrasto pela barra, botoes, minimo no redimensionamento e rolagem.
    add_executable(spriteui_window_panel_tests tests/test_window_panel.cpp)
    target_link_libraries(spriteui_window_panel_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.WindowPanel COMMAND spriteui_window_panel_tests)

    # Moldura de janela (WindowFrame.hpp, WindowFramePaint.hpp): geometria, botoes, aba, raio, rolagem e itens.
    add_executable(spriteui_window_frame_tests tests/test_window_frame.cpp)
    target_link_libraries(spriteui_window_frame_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.WindowFrame COMMAND spriteui_window_frame_tests)

    # Barra de menus (MenuBar.hpp): cascata, Escape por nivel, Alt, atraso de submenu, atalhos e rolagem.
    add_executable(spriteui_menu_bar_tests tests/test_menu_bar.cpp)
    target_link_libraries(spriteui_menu_bar_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.MenuBar COMMAND spriteui_menu_bar_tests)

    # Imagem e botao de imagem (Image.hpp): modos de ajuste, UV limitado, marcador de erro, nine-slice e estados.
    add_executable(spriteui_image_tests tests/test_image.cpp)
    target_link_libraries(spriteui_image_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Image COMMAND spriteui_image_tests)

    # Graficos (Graph.hpp): anel de historico, escala com NaN ignorado, area de plotagem e itens por coluna.
    add_executable(spriteui_graph_tests tests/test_graph.cpp)
    target_link_libraries(spriteui_graph_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Graph COMMAND spriteui_graph_tests)

    # Dialogo modal (Modal.hpp): bloqueio do fundo, foco preso, Esc cancela, Enter confirma e retorno do foco.
    add_executable(spriteui_modal_tests tests/test_modal.cpp)
    target_link_libraries(spriteui_modal_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Modal COMMAND spriteui_modal_tests)

    # Seletor de cor RGBA (ColorPicker.hpp): campos numericos 0 a 255, texto invalido, passos e reverter.
    add_executable(spriteui_color_picker_tests tests/test_color_picker.cpp)
    target_link_libraries(spriteui_color_picker_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.ColorPicker COMMAND spriteui_color_picker_tests)

    # Barra de progresso e separador (ProgressBar.hpp, Separator.hpp): faixa, valor, preenchimento e linha centrada.
    add_executable(spriteui_progress_tests tests/test_progress_separator.cpp)
    target_link_libraries(spriteui_progress_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.ProgressSeparator COMMAND spriteui_progress_tests)

    # Area de texto de varias linhas (TextArea.hpp): quebra, subir e descer, rolagem que segue o caret e recorte.
    add_executable(spriteui_text_area_tests tests/test_text_area.cpp)
    target_include_directories(spriteui_text_area_tests PRIVATE tests)
    target_link_libraries(spriteui_text_area_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.TextArea COMMAND spriteui_text_area_tests)

    # Arrastar e soltar com carga (DragDrop.hpp): limiar, filtro por tipo, alvo de cima e cancelamento.
    add_executable(spriteui_drag_drop_tests tests/test_drag_drop.cpp)
    target_link_libraries(spriteui_drag_drop_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.DragDrop COMMAND spriteui_drag_drop_tests)

    # Primitivas de desenho (Rendering/Primitives.hpp): retangulo, linha girada e retangulo arredondado.
    add_executable(spriteui_primitive_tests tests/test_primitives.cpp)
    target_link_libraries(spriteui_primitive_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Primitives COMMAND spriteui_primitive_tests)

    add_executable(spriteui_sprite_tests tests/test_sprite.cpp)
    target_link_libraries(spriteui_sprite_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.Sprite COMMAND spriteui_sprite_tests)

    add_executable(spriteui_render_list_tests tests/test_render_list.cpp)
    target_link_libraries(spriteui_render_list_tests PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.RenderList COMMAND spriteui_render_list_tests)

    # Demonstracao visual dos componentes novos (tests/Visual/ComponentsShowcase.cpp): cinco cenas em PNG com
    # checagem de pixels, pelo renderizador de software do harness.
    add_executable(spriteui_components_showcase tests/Visual/ComponentsShowcase.cpp)
    target_include_directories(spriteui_components_showcase PRIVATE tests)
    target_link_libraries(spriteui_components_showcase PRIVATE SpriteUi::SpriteUI)
    add_test(NAME SpriteUI.ComponentsShowcase
        COMMAND spriteui_components_showcase "${CMAKE_CURRENT_BINARY_DIR}/components-snapshots")

    # Capturas dos elementos novos com a fonte real do projeto (exige o atlas stb_truetype).
    if(SPRITEUI_WITH_TEXT_STB)
        add_executable(spriteui_element_captures tests/Visual/ElementCaptures.cpp)
        target_include_directories(spriteui_element_captures PRIVATE tests)
        target_compile_definitions(spriteui_element_captures PRIVATE
            "SPRITEUI_FONT_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/Fonts/Xmind/static/Xmind-Regular.ttf\"")
        target_link_libraries(spriteui_element_captures PRIVATE SpriteUi::SpriteUI SpriteUi::TextStb)
        add_test(NAME SpriteUI.ElementCaptures
            COMMAND spriteui_element_captures "${CMAKE_CURRENT_BINARY_DIR}/element-captures")

        # Captura do layout de docking com tres janelas (progress/docking-01.png).
        add_executable(spriteui_docking_capture tests/Visual/DockingCapture.cpp)
        target_include_directories(spriteui_docking_capture PRIVATE tests)
        target_compile_definitions(spriteui_docking_capture PRIVATE
            "SPRITEUI_FONT_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/Fonts/Xmind/static/Xmind-Regular.ttf\"")
        target_link_libraries(spriteui_docking_capture PRIVATE SpriteUi::SpriteUI SpriteUi::TextStb)
        add_test(NAME SpriteUI.DockingCapture
            COMMAND spriteui_docking_capture "${CMAKE_CURRENT_BINARY_DIR}/docking-01.png")
    endif()
endif()

if(SPRITEUI_BUILD_BENCHMARKS)
    add_executable(spriteui_benchmarks benchmarks/spriteui_benchmarks.cpp)
    target_link_libraries(spriteui_benchmarks PRIVATE SpriteUi::SpriteUI)
endif()

include(GNUInstallDirs)
install(TARGETS SpriteUI
    EXPORT SpriteUITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(SPRITEUI_BUILD_SDL3_ADAPTER)
    install(TARGETS SpriteUI_SDL3Input
        EXPORT SpriteUITargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    set(SPRITEUI_CONFIG_SDL3_DEPENDENCY
        "include(CMakeFindDependencyMacro)\nfind_dependency(SDL3 3.2 CONFIG COMPONENTS SDL3)")
else()
    set(SPRITEUI_CONFIG_SDL3_DEPENDENCY "")
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(SPRITEUI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/SpriteUI")
install(EXPORT SpriteUITargets
    FILE SpriteUITargets.cmake
    NAMESPACE SpriteUi::
    DESTINATION ${SPRITEUI_INSTALL_CMAKEDIR}
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpriteUIConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SpriteUIConfig.cmake"
    INSTALL_DESTINATION "${SPRITEUI_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SpriteUIConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SpriteUIConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SpriteUIConfigVersion.cmake"
    DESTINATION "${SPRITEUI_INSTALL_CMAKEDIR}"
)

# Optional offline shader compilation. The CPU library remains buildable without DXC.
find_program(SPRITEUI_DXC_EXECUTABLE NAMES dxc)
if(SPRITEUI_DXC_EXECUTABLE)
    set(SPRITEUI_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${SPRITEUI_SHADER_OUTPUT_DIR}")
    add_custom_command(
        OUTPUT "${SPRITEUI_SHADER_OUTPUT_DIR}/SpriteUI.vs.dxil"
        COMMAND "${SPRITEUI_DXC_EXECUTABLE}" -T vs_6_0 -E VSMain
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SpriteUI.hlsl"
                -Fo "${SPRITEUI_SHADER_OUTPUT_DIR}/SpriteUI.vs.dxil"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SpriteUI.hlsl"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${SPRITEUI_SHADER_OUTPUT_DIR}/SpriteUI.ps.dxil"
        COMMAND "${SPRITEUI_DXC_EXECUTABLE}" -T ps_6_0 -E PSMain
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SpriteUI.hlsl"
                -Fo "${SPRITEUI_SHADER_OUTPUT_DIR}/SpriteUI.ps.dxil"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SpriteUI.hlsl"
        VERBATIM
    )
    add_custom_target(spriteui_compile_shaders ALL
        DEPENDS
            "${SPRITEUI_SHADER_OUTPUT_DIR}/SpriteUI.vs.dxil"
            "${SPRITEUI_SHADER_OUTPUT_DIR}/SpriteUI.ps.dxil"
    )
else()
    message(STATUS "DXC not found: HLSL compilation target is skipped")
endif()

# Adaptador opcional para o RHI da ToolEngine. Header-only; NAO faz parte do nucleo.
# O consumidor informa o alvo CMake que fornece tool::rhi (padrao: tool_rhi).
option(SPRITEUI_WITH_TOOLENGINE_RHI "Build the optional ToolEngine RHI adapter (header-only)" OFF)
set(SPRITEUI_TOOLENGINE_RHI_TARGET "tool_rhi" CACHE STRING "CMake target that provides tool::rhi")
if(SPRITEUI_WITH_TOOLENGINE_RHI)
    add_library(SpriteUi_ToolEngineRhi INTERFACE)
    add_library(SpriteUi::ToolEngineRhi ALIAS SpriteUi_ToolEngineRhi)
    target_compile_features(SpriteUi_ToolEngineRhi INTERFACE cxx_std_20)
    target_include_directories(SpriteUi_ToolEngineRhi INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/toolengine/include>)
    target_link_libraries(SpriteUi_ToolEngineRhi INTERFACE SpriteUi::SpriteUI ${SPRITEUI_TOOLENGINE_RHI_TARGET})
endif()

# Padrao de projeto: todo header publico deve compilar sozinho (self-contained).
# Uso: cmake -DSPRITEUI_CHECK_HEADERS=ON ... && cmake --build . --target spriteui_check_headers
option(SPRITEUI_CHECK_HEADERS "Compile each public header as its own translation unit" OFF)
if(SPRITEUI_CHECK_HEADERS)
    file(GLOB_RECURSE SPRITEUI_PUBLIC_HEADERS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/include"
         "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
    set(SPRITEUI_HEADER_CHECK_SOURCES "")
    foreach(header IN LISTS SPRITEUI_PUBLIC_HEADERS)
        string(MAKE_C_IDENTIFIER "${header}" header_id)
        set(tu "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${header_id}.cpp")
        file(WRITE "${tu}" "#include \"${header}\"\n")
        list(APPEND SPRITEUI_HEADER_CHECK_SOURCES "${tu}")
    endforeach()
    add_library(spriteui_check_headers OBJECT ${SPRITEUI_HEADER_CHECK_SOURCES})
    target_link_libraries(spriteui_check_headers PRIVATE SpriteUi::SpriteUI)
endif()
