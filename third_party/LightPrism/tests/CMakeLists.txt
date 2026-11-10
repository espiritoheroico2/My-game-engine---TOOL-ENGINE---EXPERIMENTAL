# ---------------------------------------------------------------------------
# Helper: registra um alvo de teste com flags comuns.
# ---------------------------------------------------------------------------
function(lp_add_test NAME SOURCE)
    cmake_parse_arguments(ARG "" "LINK" "DEFS" ${ARGN})
    if(NOT ARG_LINK)
        set(ARG_LINK LightPrism::LightPrism)
    endif()
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE ${ARG_LINK})
    if(MSVC)
        target_compile_options(${NAME} PRIVATE /W4)
    else()
        target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    foreach(d IN LISTS ARG_DEFS)
        target_compile_definitions(${NAME} PRIVATE ${d})
    endforeach()
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

# ---------------------------------------------------------------------------
# Testes que dependem apenas da lib núcleo.
# ---------------------------------------------------------------------------
lp_add_test(lp_tests test_main.cpp)
lp_add_test(lp_cluster_tests test_cluster.cpp)

# ---------------------------------------------------------------------------
# Testes que também precisam do renderizador por software.
# ---------------------------------------------------------------------------
if(TARGET LightPrismSoft)
    lp_add_test(lp_soft_tests           test_soft.cpp           LINK LightPrism::Soft DEFS LP_TEST_OUTPUT_DIR="${LP_TEST_OUTPUT_DIR}")
    lp_add_test(lp_cluster_system_tests test_cluster_system.cpp LINK LightPrism::Soft DEFS LP_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
    lp_add_test(lp_shadow_tests         test_shadow.cpp         LINK LightPrism::Soft DEFS LP_TEST_OUTPUT_DIR="${LP_TEST_OUTPUT_DIR}")
    lp_add_test(lp_ibl_tests            test_ibl.cpp            LINK LightPrism::Soft DEFS LP_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
    lp_add_test(lp_ssao_tests           test_ssao.cpp           LINK LightPrism::Soft DEFS LP_TEST_OUTPUT_DIR="${LP_TEST_OUTPUT_DIR}")
    lp_add_test(lp_opt_tests            test_opt.cpp            LINK LightPrism::Soft)
    lp_add_test(lp_rt_tests             test_rt.cpp             LINK LightPrism::Soft DEFS LP_TEST_OUTPUT_DIR="${LP_TEST_OUTPUT_DIR}")
    lp_add_test(lp_phase6_tests         test_phase6.cpp         LINK LightPrism::Soft DEFS LP_TEST_OUTPUT_DIR="${LP_TEST_OUTPUT_DIR}")
endif()
