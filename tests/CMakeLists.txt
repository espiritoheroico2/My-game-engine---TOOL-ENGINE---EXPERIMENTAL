add_executable(tool_tests main.cpp test_core.cpp test_debug.cpp test_ecs.cpp test_events.cpp test_scene.cpp
  test_input.cpp test_audio.cpp test_backends.cpp test_render.cpp test_json.cpp test_narrative.cpp test_mod.cpp test_net.cpp test_pack.cpp test_assets_smoke.cpp test_import_models.cpp test_physics.cpp test_project_paths.cpp test_newtoon_jobs.cpp test_newtoon_bvh.cpp test_newtoon_advanced.cpp test_newtoon_2d.cpp test_newtoon_module_smoke.cpp test_frustum_cull.cpp test_instance_batch.cpp test_render_helpers.cpp)
target_link_libraries(tool_tests PRIVATE tool::scene tool::platform tool::audio tool::render tool::narrative tool::mod tool::net tool::pack tool::assets tool::physics)
if(TOOL_WITH_SDL3)
  target_link_libraries(tool_tests PRIVATE tool::platform_sdl3)
  target_compile_definitions(tool_tests PRIVATE TOOL_TEST_SDL3)
endif()
if(TOOL_WITH_MINIAUDIO)
  target_link_libraries(tool_tests PRIVATE tool::audio_miniaudio)
  target_compile_definitions(tool_tests PRIVATE TOOL_TEST_MINIAUDIO)
endif()
target_compile_definitions(tool_tests PRIVATE TOOL_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/assets/samples")
if(TOOL_WITH_SPRITEUI)
  target_sources(tool_tests PRIVATE test_spriteui_rhi.cpp)
  target_link_libraries(tool_tests PRIVATE SpriteUi::ToolEngineRhi)
  target_compile_definitions(tool_tests PRIVATE TOOL_WITH_SPRITEUI=1 TOOL_SHADERS_DIR="${CMAKE_SOURCE_DIR}/assets/shaders")
endif()
add_test(NAME tool_tests COMMAND tool_tests)

# Teste de escala da fisica (100k corpos 3D/2D): alvo separado, sem add_test (rodar manualmente).
add_executable(tool_newtoon_scale_test newtoon_scale_test.cpp)
target_link_libraries(tool_newtoon_scale_test PRIVATE tool::physics)

# Fisica Newtoon (2D e 3D): executavel proprio, para validar a fisica sem depender do tool_tests (que tem o SpriteUI).
add_executable(tool_physics_tests main.cpp test_newtoon_jobs.cpp test_newtoon_bvh.cpp test_newtoon_advanced.cpp
  test_newtoon_2d.cpp test_newtoon_module_smoke.cpp test_newtoon_hull.cpp test_newtoon_convex.cpp
  test_newtoon_raycast.cpp test_newtoon_cylinder.cpp)
target_link_libraries(tool_physics_tests PRIVATE tool::physics)
add_test(NAME tool_physics_tests COMMAND tool_physics_tests)

# Canvas (engine/canvas): executavel proprio, para compilar sem depender do resto dos testes do engine.
add_executable(tool_canvas_tests main.cpp test_canvas.cpp)
target_link_libraries(tool_canvas_tests PRIVATE tool::canvas)
add_test(NAME tool_canvas_tests COMMAND tool_canvas_tests)

# Particulas (engine/particles): executavel proprio de testes e benchmark de 1 milhao de particulas (sem add_test; rodar manualmente).
add_executable(tool_particles_tests main.cpp test_particles.cpp)
target_link_libraries(tool_particles_tests PRIVATE tool::particles)
add_test(NAME Particles.Core COMMAND tool_particles_tests)
add_executable(tool_particles_bench particles_bench.cpp)
target_link_libraries(tool_particles_bench PRIVATE tool::particles)
add_executable(tool_particles_bench_v2 particles_bench_v2.cpp)
target_link_libraries(tool_particles_bench_v2 PRIVATE tool::particles)

# PROTOTIPO de fisica de esferas na GPU (compute, Vulkan offscreen): so mede custo/viabilidade, sem add_test (rodar manualmente).
if(TOOL_WITH_DILIGENT)
  add_executable(tool_gpu_sphere_physics_bench gpu_sphere_physics_bench.cpp)
  target_link_libraries(tool_gpu_sphere_physics_bench PRIVATE tool::rhi_diligent)
  # EXPERIMENTO hibrido (GPU integra + pares, CPU narrowphase + solver): ver docs/planos/27_FISICA_HIBRIDA.md.
  add_executable(tool_hybrid_physics_bench hybrid_physics_bench.cpp)
  target_link_libraries(tool_hybrid_physics_bench PRIVATE tool::rhi_diligent)
endif()
