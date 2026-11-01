# Dependencias externas.
#
# As bibliotecas pequenas ja vem COPIADAS em third_party/ (SDL3, ImGui, imnodes, miniaudio): compilar nao exige
# internet nem Git. Se uma dessas pastas for apagada, o CMake baixa a versao fixada (ai sim precisa de Git).
# Opcoes (PADRAO = o que o editor precisa):
#   TOOL_WITH_SDL3 / TOOL_WITH_MINIAUDIO / TOOL_WITH_EDITOR  ligados
#   TOOL_WITH_DILIGENT (GPU: Vulkan/OpenGL)  DESLIGADO por padrao: e opcional, baixa ~200 MB e demora; o editor
#                      funciona completo com o renderizador em CPU. Veja HOW_TO_BUILD.txt para liga-lo.
# Os presets dev/test/release/asan desligam tudo isso (compilacao so do nucleo, em segundos).
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(TOOL_THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)

option(TOOL_WITH_SDL3 "Janela/entrada reais via SDL3 (zlib)" ON)
option(TOOL_WITH_DILIGENT "Backends graficos de GPU via Diligent Engine (Vulkan e OpenGL). Opcional." OFF)
option(TOOL_WITH_D3D12 "Backend Direct3D 12 (so Windows; AINDA NAO TESTADO)" OFF)
option(TOOL_WITH_QUICKJS "Runtime de scripts ToolObject (QuickJS-ng, MIT, vendorizado em third_party/quickjs-ng)" ON)
option(TOOL_WITH_EDITOR "Editor ImGui + imnodes (requer TOOL_WITH_SDL3)" ON)
option(TOOL_WITH_MINIAUDIO "Saida de audio real via miniaudio (dominio publico)" ON)

option(TOOL_SDL_CONSOLE_BUILD "SDL3 sem X11/Wayland (CI/servidor sem pacotes de desktop)" OFF)
if(TOOL_WITH_SDL3)
  if(TOOL_SDL_CONSOLE_BUILD)
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    set(SDL_X11 OFF CACHE BOOL "" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
  endif()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  if(EXISTS ${TOOL_THIRD_PARTY}/SDL3/CMakeLists.txt)
    add_subdirectory(${TOOL_THIRD_PARTY}/SDL3 ${CMAKE_BINARY_DIR}/_deps/sdl3-build EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.2.8 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(SDL3)
  endif()
endif()

# miniaudio e um arquivo unico (miniaudio.h): so precisamos da pasta de inclusao, sem rodar o CMake deles.
if(TOOL_WITH_MINIAUDIO)
  if(EXISTS ${TOOL_THIRD_PARTY}/miniaudio/miniaudio.h)
    set(miniaudio_SOURCE_DIR ${TOOL_THIRD_PARTY}/miniaudio)
  else()
    FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.24 GIT_SHALLOW ON SOURCE_SUBDIR _sem_cmake)
    FetchContent_MakeAvailable(miniaudio)
  endif()
endif()

if(TOOL_WITH_EDITOR)
  if(NOT TOOL_WITH_SDL3)
    message(FATAL_ERROR "TOOL_WITH_EDITOR requer TOOL_WITH_SDL3=ON")
  endif()
  if(EXISTS ${TOOL_THIRD_PARTY}/imgui/imgui.cpp AND EXISTS ${TOOL_THIRD_PARTY}/imnodes/imnodes.cpp)
    set(imgui_SOURCE_DIR ${TOOL_THIRD_PARTY}/imgui)
    set(imnodes_SOURCE_DIR ${TOOL_THIRD_PARTY}/imnodes)
  else()
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b-docking GIT_SHALLOW ON)
    FetchContent_Declare(imnodes GIT_REPOSITORY https://github.com/Nelarius/imnodes.git GIT_TAG v0.5 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(imgui imnodes)
  endif()
  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    ${imnodes_SOURCE_DIR}/imnodes.cpp)
  target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imnodes_SOURCE_DIR})
  # imnodes usa operadores de ImVec2 que o ImGui so expoe com esta definicao; silenciamos os avisos do codigo de terceiros.
  if(MSVC)
    set_source_files_properties(${imnodes_SOURCE_DIR}/imnodes.cpp PROPERTIES COMPILE_OPTIONS "/DIMGUI_DEFINE_MATH_OPERATORS;/w")
  else()
    set_source_files_properties(${imnodes_SOURCE_DIR}/imnodes.cpp PROPERTIES COMPILE_OPTIONS "-DIMGUI_DEFINE_MATH_OPERATORS;-w")
  endif()
  target_link_libraries(imgui_lib PUBLIC SDL3::SDL3-static)
endif()

if(TOOL_WITH_DILIGENT)
  set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
  # D3D12 so entra se pedido (Windows). Desligado, o Diligent nem compila esse backend: build mais rapido.
  if(NOT (WIN32 AND TOOL_WITH_D3D12))
    set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
  endif()
  set(DILIGENT_NO_FORMAT_VALIDATION ON CACHE BOOL "" FORCE)
  set(DILIGENT_INSTALL_CORE OFF CACHE BOOL "" FORCE)
  # Usa o clone local em third_party/ se existir; senao baixa (com submodulos) a versao fixada.
  if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/DiligentCore/CMakeLists.txt)
    set(FETCHCONTENT_SOURCE_DIR_DILIGENTCORE ${CMAKE_SOURCE_DIR}/third_party/DiligentCore)
  endif()
  FetchContent_Declare(DiligentCore GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git GIT_TAG v2.5.6 GIT_SHALLOW ON GIT_SUBMODULES_RECURSE ON)
  FetchContent_MakeAvailable(DiligentCore)
endif()

# QuickJS-ng (MIT): runtime dos scripts ToolObject. So a biblioteca (qjs); os executaveis do QuickJS ficam fora do build.
if(TOOL_WITH_QUICKJS AND EXISTS ${CMAKE_SOURCE_DIR}/third_party/quickjs-ng/CMakeLists.txt)
  set(QJS_BUILD_EXAMPLES OFF)
  set(QJS_ENABLE_INSTALL OFF)
  set(QJS_BUILD_CLI_STATIC OFF)
  add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/quickjs-ng ${CMAKE_BINARY_DIR}/_deps/quickjs-build EXCLUDE_FROM_ALL)
endif()
