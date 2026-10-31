cmake_minimum_required(VERSION 3.20)
project(ToolEngine VERSION 0.1.0 LANGUAGES CXX)

# CMake 4+ recusa projetos de terceiros que declaram versao minima < 3.5; esta variavel os aceita.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Windows/MSVC: runtime C++ ESTATICO => o .exe roda em qualquer PC sem instalar o "VC++ Redistributable".
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Padrao = so o editor. Testes e sandbox ficam ligados nos presets dev/test/beta (ou -DTOOL_BUILD_TESTS=ON).
option(TOOL_BUILD_TESTS "Compilar os testes" OFF)
option(TOOL_BUILD_SANDBOX "Compilar o programa de demonstracao (console)" OFF)
option(TOOL_WARNINGS_AS_ERRORS "Tratar avisos como erros" OFF)
set(TOOL_DEBUG_LEVEL 2 CACHE STRING "Nivel do DebugTool: 0=release 1=teste 2=desenvolvimento")

include(cmake/Warnings.cmake)

include(cmake/Dependencies.cmake)
enable_testing()
add_compile_definitions(TOOL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(WIN32)
  # windows.h defines min/max macros that break std::min/std::max; also keep it lean.
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()
add_subdirectory(engine)
option(TOOL_BUILD_FUZZ "Alvos libFuzzer (clang)" OFF)
if(TOOL_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()
if(TOOL_WITH_EDITOR)
  set(TOOL_FINAL_DIR "${CMAKE_SOURCE_DIR}/Projeto Final")
  add_subdirectory(apps/editor)
  # Visual Studio: F5 / "Iniciar" ja abre o editor, com a pasta de trabalho na raiz do projeto (onde fica assets/).
  set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT tool_editor)
  # TUDO que o editor precisa para rodar fica na pasta "Projeto Final" (qualquer gerador/configuracao):
  #   Projeto Final/ToolEditor.exe + assets/ (shaders, idiomas, temas, exemplos) + LEIA-ME.txt
  # Ao abrir o editor, ele cria "Projeto Final/Project/" (suas cenas e assets) e grava as configuracoes ali tambem.
  set_target_properties(tool_editor PROPERTIES OUTPUT_NAME ToolEditor RUNTIME_OUTPUT_DIRECTORY "$<1:${TOOL_FINAL_DIR}>")
  set_target_properties(tool_editor PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${TOOL_FINAL_DIR}")
endif()
if(TOOL_WITH_DILIGENT AND TOOL_WITH_SDL3)
  add_subdirectory(apps/gpu_test)
endif()
if(TOOL_BUILD_SANDBOX)
  add_subdirectory(apps/sandbox)
endif()
if(TOOL_BUILD_TESTS)
  add_subdirectory(tests)
endif()
