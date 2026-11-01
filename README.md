# ToolEngine (Game engine leve sem nome definido)

Engine 3D leve, multi-API, em C++20. Estrutura pensada para o **Visual Studio Code**.

## Como abrir
1. Instale: VS Code, CMake >= 3.25, um compilador (GCC/Clang/MSVC).
2. `File > Open Folder` nesta pasta. Aceite instalar as extensões recomendadas
   (CMake Tools, clangd, C/C++, clang-format).
3. `Ctrl+Shift+B` configura e compila (preset **dev**). `F5` depura o *sandbox*.
4. Testes: tarefa **test** (ou `ctest --preset dev`). Sanitizers: tarefa **asan**.

## Abrir o editor (caminho direto)
- Windows: dois cliques em `build_editor.bat` (qualquer Visual Studio recente com "Desenvolvimento para desktop com C++"). Linux/macOS: `./build_editor.sh`.
- Sem Git, Ninja ou internet (dependências embutidas em `third_party/`). Passo a passo: `COMO_COMPILAR.txt` / `HOW_TO_BUILD.txt`. O programa sai em `Projeto Final/ToolEditor(.exe)` (a pasta já contém tudo que o editor precisa). Idioma padrão: inglês.

## Linha de comando (núcleo e testes)
```
cmake --preset dev && cmake --build --preset dev
ctest --preset dev
```
Presets: `editor` (só o editor, CPU), `editor-gpu` (+Vulkan/OpenGL), `dev`, `test`, `release`, `asan`, `beta`.

## Estrutura
```
engine/core     tipos, handles com geração, tags internadas, log
engine/debug    DebugTool: diagnósticos TOOL-Exxx/Wxxx, zonas, orçamentos, watchdog
engine/ecs      ECS embutido (esparso/denso), tags, consultas
engine/events   EventBus + EventArbiter (exclusão mútua, one-shot, cooldown, ciclos)
engine/scene    Scene, ToolObject, Blueprint, DeleteAndReuse (pool)
engine/input, platform, audio   entrada/gamepad, janela, mixer de audio
engine/math, rhi, render        matematica, interface de GPU, iluminacao, SoftRenderer (CPU)
engine/narrative, mod, net, pack   Controlador Narrativo, ModTool, NetTool, pacotes .tpak
apps/editor     editor ImGui (preset editor)   fuzz/  fuzzers   tools/  compilador de shaders   assets/shaders  HLSL
apps/sandbox    jogo de teste
tests           testes automatizados (sem dependências)
docs/           notas de construção
```
Dependências apontam sempre para baixo: scene → events/ecs → debug → core.

Status: **0.7.7-beta** - veja `docs/CHANGELOG.md`, `docs/PARTICLES.md`, `docs/PERFORMANCE_AUDIT.md`, `docs/RENDER_SCALING.md`, `docs/ECS.md`, `docs/PERFORMANCE_PLAN.md`, `docs/NEXT_STEPS.md`, `docs/FSR.md`, `docs/SECURITY.md`, `docs/CHECKLIST.md`.

## Novidades da v0.4.0-beta
Sombras em tempo real (cast/receive por objeto), gizmos mover/girar/escalar (W/E/R), navegação livre (Alt), foco por duplo clique,
tags editáveis com ligações (`jogador` → `player`), script ToolObject em todo objeto, pós-processamento global/local,
Settings > Lighting / Tags / Project (exportação, desativada), Profiler detalhado. Detalhes em `docs/CHANGELOG.md`.

## Recursos do editor (v0.3.0-beta)
- APIs gráficas trocáveis em Settings: CPU (multithread), Vulkan, OpenGL (Direct3D 12 compilado, ainda não testado).
- Importa modelos `.glb/.gltf/.fbx` e sons `.wav/.mp3` (arraste o arquivo para a janela do editor ou use o painel Assets).
- Objetos: Câmera (tag `Principal` = câmera ativa; duas Principais = split screen), Luz Global (sol, pode ser dinâmica), Fonte de Áudio, luz pontual.
- Câmera com "Sound Ear" (ouvinte de áudio).
- Cenas separadas em `Project/Scenes/*.scene.json`; assets em `Project/Assets/`.
- Grid 3D com eixos X/Y/Z no editor; no jogo só aparece se ativado em Settings > Game.
- Painel Assets: pastas, lista/ícones, renomear, apagar, nova pasta.
- Build com GPU: `build_editor.bat gpu` (Windows) ou `./build_editor.sh --gpu` (Linux); precisa clonar third_party/DiligentCore (veja HOW_TO_BUILD.txt); a primeira vez leva ~12 min.
