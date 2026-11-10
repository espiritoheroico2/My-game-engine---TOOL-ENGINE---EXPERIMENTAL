cmake_minimum_required(VERSION 3.16)
project(LightPrism VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LP_BUILD_TESTS      "Compila os testes (golden tests CPU)" ON)
option(LP_BUILD_SOFT       "Compila o renderizador por software e a ferramenta lp_softrender" ON)
option(LP_ENABLE_DILIGENT  "Compila o backend Diligent (requer DiligentCore)" OFF)

# ---------------------------------------------------------------------------
# Núcleo: independente de engine e de API gráfica.
# ---------------------------------------------------------------------------
add_library(LightPrism STATIC
    src/Core/LightingSystem.cpp
    src/Cluster/ClusterCulling.cpp
    src/Cluster/ClusterSystem.cpp
    src/Shadow/Csm.cpp
    src/Ibl/Ibl.cpp
    src/Post/Ssao.cpp
    src/Rt/Bvh.cpp
    src/Rt/Transfer.cpp
    src/Rt/ProbeGrid.cpp
    src/Rhi/NullDevice.cpp
)
add_library(LightPrism::LightPrism ALIAS LightPrism)

target_include_directories(LightPrism PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(MSVC)
    target_compile_options(LightPrism PRIVATE /W4)
else()
    target_compile_options(LightPrism PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Renderizador por software (referência visual, sem API gráfica)
# ---------------------------------------------------------------------------
if(LP_BUILD_SOFT)
    add_library(LightPrismSoft STATIC
        src/Soft/Png.cpp
        src/Soft/SoftRenderer.cpp
    )
    add_library(LightPrism::Soft ALIAS LightPrismSoft)
    target_link_libraries(LightPrismSoft PUBLIC LightPrism::LightPrism)
    if(MSVC)
        target_compile_options(LightPrismSoft PRIVATE /W4)
    else()
        target_compile_options(LightPrismSoft PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(lp_softrender tools/softrender/main.cpp)
    target_link_libraries(lp_softrender PRIVATE LightPrism::Soft)
endif()

# ---------------------------------------------------------------------------
# Backends gráficos (cada um é uma lib separada que implementa IGraphicsDevice).
# ---------------------------------------------------------------------------
if(LP_ENABLE_DILIGENT)
    add_subdirectory(src/Backends/Diligent)
endif()

# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------
if(LP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
