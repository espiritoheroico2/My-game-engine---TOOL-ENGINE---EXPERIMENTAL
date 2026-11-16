# Peso do repositorio

Medido em 2026-11-10, branch `spriteui-rhi-backend`, HEAD `337b74e`.

## Medidas

| Item | Valor |
|---|---|
| Arquivos rastreados | 2008 |
| Tamanho dos arquivos rastreados (`git ls-files` + `du`) | 47 MB |
| `third_party/` | 44 MB (SDL3 1,4 MB em 1436 arquivos; miniaudio 4 MB; quickjs-ng 2,2 MB; LightPrism 1,6 MB; imgui 0,2 MB) |
| `apps/`, `engine/`, `tests/`, `tools/`, `fuzz/`, `cmake/` | ~1,8 MB (somados) |
| `assets/` | 448 KB |
| `docs/` | 572 KB |
| `.git` | 11 MB (2 packs, 9,6 MB; objetos soltos 344 KB; sem lixo) |

`git count-objects -vH` mostra 53 objetos soltos e 2426 em pacotes. Nao ha nada a limpar no `.git` sem reescrever historia, que nao foi feito.

Nao ha binarios de build (.exe, .dll, .lib, .obj, .pdb, .o) rastreados. Os unicos binarios sao amostras em `assets/samples` (usadas pelo autoteste do editor) e a fonte `assets/fonts/Xmind-Regular.ttf` (usada pela tela inicial).

## Removido neste commit

| Item | Tamanho | Motivo | Prova de que nada referencia |
|---|---|---|---|
| `build_log.txt` | 316 KB | Log gerado pelos scripts `build_editor.bat` e `build_editor.sh` a cada execucao (saida do MSVC de outra maquina, com caminhos `C:\Users\espir\...`). Nao e entrada de build. Adicionado ao `.gitignore`. | `git grep -nI build_log` mostra so os dois scripts (que o escrevem) e os textos `HOW_TO_BUILD.txt` / `COMO_COMPILAR.txt`, que o descrevem em geral. Nenhum CMake ou codigo C++ o le. |

## Precisa de decisao do dono (nao removido)

| Item | Tamanho | Por que e ambiguo | Prova |
|---|---|---|---|
| `docs/ui_default_en.png`, `docs/ui_en_language.png`, `docs/ui_theme_ember_es.png` | 128 + 116 + 116 KB | Sao capturas de tela. Nenhum texto as referencia, mas podem ser usadas em release ou no README externo. | `git grep -nI 'ui_default\|ui_en_language\|ui_theme_ember'` sem resultados. |
| `git add.txt` | 4 KB | Parece uma anotacao pessoal (uma linha: `third_party/LightPrism/`). Nao e referenciado. | `git grep -nI 'git add'` sem uso no build. |
| `third_party/quickjs-ng/tests`, `third_party/quickjs-ng/.github` | 612 + 60 KB | Testes JS e CI de terceiros. Nenhum alvo do CMake do quickjs les esses diretorios. `LEIA-ME-THIRDPARTY.txt` diz "pasta completa", entao a copia integral foi uma escolha. | `grep -n 'tests/\|\.github' third_party/quickjs-ng/CMakeLists.txt` sem resultados. |
| `third_party/LightPrism/tests` | 192 KB | Testes de terceiros, nao usados pelo CMake raiz (`add_subdirectory(third_party/LightPrism)` so). | `git grep -n 'LightPrism.*tests'` sem resultados. |
| `third_party/SDL3/cmake/test` | 44 KB | Helpers de teste do SDL. O projeto desliga `SDL_TESTS` e `SDL_TEST_LIBRARY` em `cmake/Dependencies.cmake`, entao nada os usa. | `grep -n 'SDL_TEST' cmake/Dependencies.cmake` mostra as duas opcoes em OFF. |

## Mantido de proposito (checado e em uso)

- `assets/shaders/glsl/*.glsl` e `assets/shaders/lightprism/glsl/*.glsl`: saida gerada, mas carregada em tempo de execucao (OpenGL). Ficam no repositorio de proposito.
- `assets/samples/*`: usados por `apps/editor/selftest.hpp`.
- `assets/themes/meu-tema.json`: tema padrao do editor (`themeName`), lido por `ed.theme.scan("assets/themes")`.
- `third_party/stb/stb_image.h`: usado por `apps/editor/image_io.cpp`. A copia em `third_party/SDL3/src/video/stb_image.h` pertence ao SDL.
- `third_party/LightPrism/docs/images/*` (1,5 MB): referenciadas por `docs/CLUSTERS.md` e `docs/IBL.md` do proprio LightPrism.
- `third_party/quickjs-ng/gen` e `examples`: `gen/` e compilado pelo CMake do quickjs; `examples/` e referenciado (opcao `QJS_BUILD_EXAMPLES`, desligada por padrao).
- `third_party/miniaudio/miniaudio.h` (4 MB, maior arquivo rastreado): biblioteca de audio em header unico, necessaria.
