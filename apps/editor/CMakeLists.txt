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

add_executable(tool_ui_test ui_test.cpp)
target_link_libraries(tool_ui_test PRIVATE imgui_lib tool::core)
add_test(NAME ui_editor COMMAND tool_ui_test)

add_test(NAME editor_selftest COMMAND tool_editor --selftest WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(editor_selftest PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 120)

# Copia o que o editor precisa para rodar para "Projeto Final" (ver CMakeLists.txt da raiz)
add_custom_command(TARGET tool_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${TOOL_FINAL_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/LEIA-ME_Projeto_Final.txt" "${TOOL_FINAL_DIR}/LEIA-ME.txt"
  COMMAND_EXPAND_LISTS VERBATIM)
