# ToolEngine v0.2.0-beta

Meta: **editor funcional + APIs graficas funcionando.**

## O que foi verificado de verdade (neste ambiente, sem GPU fisica)
| API | Como | Resultado |
|---|---|---|
| Vulkan (Diligent) | lavapipe (Vulkan por software) | `gpu_test vulkan`: IoU 0,981 vs SoftRenderer, 0 erros |
| OpenGL 4.3 (Diligent) | Mesa llvmpipe + Xvfb | `gpu_test opengl`: IoU 0,981, 0 erros |
| Software (CPU) | SoftRenderer | referencia e fallback do editor |
| D3D12 | so compila no Windows | **NAO testado** |
| WebGPU / GLES-Android / Metal | nao implementados | **NAO testado** |

Testes: `ctest --test-dir build/beta` (tool_tests + gpu_test_vulkan + gpu_test_opengl). dev e asan passam.

## Editor
Layout padrao estilo Unity (Hierarquia/Assets | Viewport | Inspetor/Config. Grafica | Console/Profiler/Narrativo),
janelas redimensionaveis e acopláveis, "Janela > Restaurar layout". O Viewport mostra a cena renderizada pela API
escolhida em "Configuracao Grafica" (troca em tempo de execucao) ou por `--api software|vulkan|opengl|d3d12`.
Screenshots: `docs/editor_vulkan.png`, `docs/editor_opengl.png`.

## Decisoes tecnicas
- HLSL e a fonte unica. Vulkan e D3D12 compilam o HLSL direto no Diligent.
- O conversor HLSL->GLSL interno do Diligent rejeitou o shader; o backend OpenGL usa GLSL 430 pre-gerado
  (`assets/shaders/glsl/`, via `tools/compile_shaders.py`: glslang + SPIRV-Cross, `--fixup-clipspace --flip-vert-y`).
  Se mudar o HLSL, regenere com o script.
- Semantics de vertice sao `ATTRIB0..2` (convencao do Diligent).
- No Linux o contexto GL e criado via SDL antes do Diligent.

## Build
```
cmake --preset beta && cmake --build --preset beta
xvfb-run -a build/beta/bin/tool_editor --api opengl   # ou direto em uma maquina com tela
```
Requer `third_party/DiligentCore` (tag v2.5.6) ou rede para o FetchContent.

## Ainda nao feito
Sombras no GPU, texturas, glTF, fisica, scripting de jogo, exportador completo, Android/Web, WebRTC real, swapchain Vulkan em janela real (so offscreen testado).
