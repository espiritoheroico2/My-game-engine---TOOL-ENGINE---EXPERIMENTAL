# Dependências de terceiros

Este documento lista tudo que o SpriteUI usa, de onde vem, a licença e quando entra no build.
Regra do pacote: **o núcleo não tem dependência obrigatória**. Cada item abaixo é opcional, salvo indicação.

Ao adicionar uma dependência nova, registre aqui: nome, versão fixa, licença, o que usa, em qual opção de CMake
entra e se ela é distribuída junto ou só exigida do consumidor.

## Incluído no pacote

| Item | Versão | Licença | Onde está | Quando entra |
|---|---|---|---|---|
| stb_truetype | 1.26 | Public domain ou MIT (escolha do usuário) | `third_party/stb/stb_truetype.h` | `SPRITEUI_WITH_TEXT_STB=ON` (padrão OFF) |
| Fonte Xmind (variável, regular e itálico) | arquivos em `Fonts/Xmind` | SIL Open Font License 1.1 (`Fonts/Xmind/OFL.txt`) | `Fonts/Xmind` | Só testes e exemplos. A biblioteca não precisa dela. |

Notas:
- `third_party/stb/README.md` registra a procedência do stb_truetype.
- A fonte Xmind é OFL: pode ser redistribuída, mas o aviso de licença precisa acompanhar o arquivo.

## Opcionais, sem distribuição (o consumidor fornece)

| Item | Versão mínima | Licença | Usado por | Opção de CMake |
|---|---|---|---|---|
| SDL3 | 3.2 | zlib | Adaptador de entrada `SpriteUi::SDL3Input` e o visualizador | `SPRITEUI_BUILD_SDL3_ADAPTER`, `SPRITEUI_BUILD_VISUALIZER` (padrão OFF) |
| Doxygen | qualquer recente | GPL-2.0 (apenas a ferramenta; a documentação gerada não herda a licença) | Referência de API | `SPRITEUI_BUILD_DOCS` (padrão OFF) |
| DXC (compilador HLSL) | qualquer recente | MIT / Apache-2.0 (LLVM) | Gera DXIL dos shaders HLSL, quando `dxc` existe no PATH | Automático, se `dxc` for encontrado |
| Diligent Engine (DiligentCore) | v2.5.6 | Apache-2.0 | Só o consumidor. O adaptador `SpriteUi::ToolEngineRhi` compila contra o RHI da ToolEngine, que usa Diligent | `SPRITEUI_WITH_TOOLENGINE_RHI=ON` e um alvo RHI do consumidor (`SPRITEUI_TOOLENGINE_RHI_TARGET`) |

## Ferramentas de build e teste (não entram no pacote)

| Item | Licença | Uso |
|---|---|---|
| glslang | BSD-3-Clause | Gera SPIR-V a partir do HLSL. Usado por `tools/shaders/build_glsl.py` na engine para produzir os GLSL |
| SPIRV-Cross | Apache-2.0 | Converte SPIR-V para GLSL 430 (mesmo script) |
| Xvfb, Mesa (llvmpipe) e lavapipe | MIT / X11 | Ambiente de teste com GPU virtual: OpenGL (Mesa) e Vulkan (lavapipe) |

## Planejados (ainda não incluídos, precisam de revisão de licença)

Estes itens estão no roadmap do guia de implementação. Nenhum deles entrou no código ainda.

| Item | Licença provável | Para quê | Condição antes de entrar |
|---|---|---|---|
| HarfBuzz | MIT | Shaping (kerning, ligaduras, marcas) | Revisar dependências transitivas e versão fixa |
| FreeType | FreeType License (FTL) ou GPL-2.0 | Leitura de glifos de fontes | Escolher explicitamente **FTL**; GPL-2.0 não entra no pacote |
| ICU ou SheenBidi | Unicode License / MIT | Quebra de linha e direção (bidi) | Só se o texto bidirecional virar requisito |
| msdfgen / msdf-atlas-gen | MIT | Atlas SDF/MSDF para texto nítido em qualquer tamanho | Verificar a licença do gerador e de cada dependência dele |

## Não usados

- **Dear ImGui**: o SpriteUI não usa ImGui. A engine mantém ImGui apenas no editor até a migração ser concluída.
- **Bibliotecas de fonte com licença GPL-2.0 sem exceção** (ex.: FreeType em modo GPL): não entram no pacote distribuído.
