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

# Teste com GPU do RHI: compute, buffers escritos pela GPU (StorageRW) e draw indireto. Vulkan offscreen (sem janela).
add_executable(rhi_compute_test rhi_compute_test.cpp)
target_link_libraries(rhi_compute_test PRIVATE tool::rhi_diligent tool::render)
add_test(NAME rhi_compute_vulkan COMMAND rhi_compute_test vulkan)
set_tests_properties(rhi_compute_vulkan PROPERTIES LABELS gpu)

# Tempo de GPU por passe (timestamps): zonas > 0 em Vulkan e 0 no Null. "bench" mede o custo com a flag ligada/desligada.
add_executable(gpu_time_test gpu_time_test.cpp)
target_link_libraries(gpu_time_test PRIVATE tool::rhi_diligent tool::render)
add_test(NAME gpu_time_vulkan COMMAND gpu_time_test vulkan)
set_tests_properties(gpu_time_vulkan PROPERTIES LABELS gpu)

# Corretude da oclusao por GPU (HiZ) contra a verdade geometrica e contra a oclusao por software (CPU): zero falsos ocultos.
add_executable(occlusion_test occlusion_test.cpp)
target_link_libraries(occlusion_test PRIVATE tool::rhi_diligent tool::render)
add_test(NAME occlusion_gpu_vulkan COMMAND occlusion_test vulkan)
set_tests_properties(occlusion_gpu_vulkan PROPERTIES LABELS gpu)

# Teste com GPU do FSR 1 (EASU + RCAS): padrao 64x64 ampliado para 128x128, comparado com o bilinear. Vulkan offscreen.
add_executable(fsr1_test fsr1_test.cpp)
target_link_libraries(fsr1_test PRIVATE tool::rhi_diligent tool::render)
add_test(NAME fsr1_vulkan COMMAND fsr1_test vulkan)
set_tests_properties(fsr1_vulkan PROPERTIES LABELS gpu)

# Teste com GPU do FSR 4 (FidelityFX ffx_api sobre o Vulkan do Diligent): consulta real de suporte + upscale 64x64 -> 128x128.
# Pula (exit 0, "SKIP: ...") se a GPU nao suportar FSR 4. So existe se o SDK estiver em third_party/fidelityfx.
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/fidelityfx/include/ffx_api.hpp)
  add_executable(fsr4_test fsr4_test.cpp)
  target_link_libraries(fsr4_test PRIVATE tool::rhi_diligent tool::render tool::fidelityfx)
  add_test(NAME fsr4_vulkan COMMAND fsr4_test vulkan)
  set_tests_properties(fsr4_vulkan PROPERTIES LABELS gpu SKIP_REGULAR_EXPRESSION "SKIP: FSR 4")
endif()
