add_executable(gpu_test main.cpp)
target_link_libraries(gpu_test PRIVATE tool::render tool::rhi_diligent SDL3::SDL3-static)
add_test(NAME gpu_test_vulkan COMMAND gpu_test vulkan)
add_test(NAME gpu_test_opengl COMMAND xvfb-run -a $<TARGET_FILE:gpu_test> opengl)
set_tests_properties(gpu_test_vulkan gpu_test_opengl PROPERTIES LABELS gpu)

# Teste com GPU do backend SpriteUI (OpenGL via Xvfb no Linux). Pixels conferidos no proprio programa.
if(TOOL_WITH_SPRITEUI)
  add_executable(spriteui_gpu_test spriteui_gpu.cpp)
  target_link_libraries(spriteui_gpu_test PRIVATE SpriteUi::ToolEngineRhi tool::rhi_diligent SDL3::SDL3-static)
  add_test(NAME spriteui_gpu_opengl COMMAND xvfb-run -a $<TARGET_FILE:spriteui_gpu_test>)
  set_tests_properties(spriteui_gpu_opengl PROPERTIES LABELS gpu)
endif()
