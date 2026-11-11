# Padrões de código — SpriteUi

## Arquivos
- Headers: **`.hpp`**, em `include/SpriteUi/<Modulo>/<Nome>.hpp`. Nunca `.h`.
- Implementação: **`.cpp`**, em `src/<Modulo>/<Nome>.cpp`, com o mesmo nome do header. Um par `.hpp`/`.cpp` por unidade; sem arquivos duplicados.
- Header-only é permitido apenas para tipos POD/`constexpr` e funções triviais (`Math`, `Transform2D`, `Handle`, `DirtyFlags`, componentes de dados, `NullRenderBackend`). Lógica com laços, estado ou mais de ~15 linhas vai para `.cpp` (ex.: `SpatialNavigation.cpp`).
- Todo header usa `#pragma once`, inclui o que usa (self-contained) e é validado por `-DSPRITEUI_CHECK_HEADERS=ON` (alvo `spriteui_check_headers`).
- Todo `.cpp` novo entra em `CMakeLists.txt`; todo header novo entra em `SpriteUi/SpriteUi.hpp`.
- Ordem de includes: header do próprio módulo, `SpriteUi/...`, biblioteca padrão.

## Nomes
- Namespace raiz `ToolEngine::SpriteUi`; submódulos: `Input`, `Rendering`, `Widgets`, `Text`, `Animation`, `Binding`, `Config`. `Core` e `Layout` ficam no namespace raiz.
- Tipos/funções `PascalCase`; membros de struct `camelCase`; membros privados de classe `camelCase_`.
- Componentes de dados usam prefixo `UI` (`UISliderComponent`, `UIHandle`); enums são `enum class`.
- Código, includes e namespace: `SpriteUi`. Alvo/pacote CMake exportado: `SpriteUI` (alias `SpriteUi::SpriteUI`) — não renomear, é API consumida por terceiros. Shader: `shaders/SpriteUI.hlsl`.

## Robustez (V4.2)
- Todo valor externo (float, vetor, tamanho) passa por `IsFinite` antes de uso; conversões float→inteiro só após `clamp`.
- Contratos de ABI GPU têm `static_assert` de tamanho, alinhamento e offsets.
- Sem `std::function`/alocação por frame em caminhos quentes (ToonMotion usa PODs).
- Dados do usuário/rede (JSON, texto): validar estruturalmente e só gravar no destino após validar tudo (escrita transacional).

## Build e qualidade
- C++20, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` (GCC/Clang) ou `/W4 /permissive-` (MSVC) sem avisos.
- Testes: `ctest` (`tests/test_main.cpp`). Todo bug corrigido ganha um caso em `TestReviewRegressions` ou no teste do módulo.
- Não versionar `build/`, `.cache/` (clangd) nem binários — já estão no `.gitignore`.
- Formatação: `.clang-format` e `.editorconfig` na raiz (LF, 4 espaços).
