# ToolEngine 0.1.0-alpha

## O que ja funciona
- Nucleo, DebugTool (codigos TOOL-Exxx/Wxxx, zonas, orcamentos, watchdog), ECS, eventos com arbitro, DeleteAndReuse.
- Entrada (gamepad por posicao, mapa de acoes), audio (mixer em software), janela (nula e SDL3), audio real (miniaudio).
- RHI com dispositivo nulo validador, modelo de iluminacao, shaders HLSL -> SPIR-V -> GLSL.
- **SoftRenderer**: renderizador 3D em CPU (viewport do editor e sandbox) - alpha sem GPU.
- Controlador Narrativo (compilador, VM, saves atomicos), ModTool, NetTool (camada confiavel), pacotes .tpak.
- Editor ImGui: Hierarquia, Inspetor, Viewport 3D, Console, Profiler, Assets, Controlador Narrativo (nos visuais).
- 97 testes + 6 fuzzers.

## O que NAO esta pronto (proximas etapas)
- Backends GPU reais (Diligent: Vulkan/D3D12/WebGPU/GLES) - hoje so a interface, o validador e o SoftRenderer.
- Sombras, texturas, modelos glTF, animacao, fisica.
- Scripts de jogo (QuickJS/AssemblyScript), exportador completo (esbuild/qjsc/wasm-opt/KTX2), compressao e assinatura de pacotes.
- WebRTC real (libdatachannel), Steam Audio, backend UWP de entrada, KOF.
- Android/Web: nada compilado ainda (so a arquitetura).

## Como testar
    cmake --preset full && cmake --build --preset full
    ./build/full/bin/tool_editor        # editor (Linux precisa dos pacotes dev do X11/Wayland para o SDL3)
    ./build/full/bin/sandbox cena.ppm   # renderiza uma cena em CPU
    ./build/full/bin/tool_tests
Sem X11/Wayland (servidor/CI): `--preset ci-full` e `tool_editor --frames 60`.
