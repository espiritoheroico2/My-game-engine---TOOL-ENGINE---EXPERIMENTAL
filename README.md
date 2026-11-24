# ToolEngine (nome de trabalho)

Engine 3D leve, multi-API, em C++20. Estrutura pensada para o **Visual Studio Code**.
Criado por: João Vitor Dias da Silva
ver: 0.8 beta


## Como abrir
1. Instale: VS Code, CMake >= 3.25, um compilador (GCC/Clang/MSVC).
2. `File > Open Folder` nesta pasta. Aceite instalar as extensões recomendadas
   (CMake Tools, clangd, C/C++, clang-format).
3. `Ctrl+Shift+B` configura e compila (preset **dev**). `F5` depura o *sandbox*.
4. Testes: tarefa **test** (ou `ctest --preset dev`). Sanitizers: tarefa **asan**.

## Abrir o editor (caminho direto)
- Windows: dois cliques em `build_editor.bat` (qualquer Visual Studio recente com "Desenvolvimento para desktop com C++"). Linux/macOS: `./build_editor.sh`.
- O editor inteiro e desenhado com SpriteUI (sem Dear ImGui). A tela inicial (criar/abrir projeto) tambem.
- Sem Git, Ninja ou internet para o build legado (dependências embutidas em `third_party/`). O painel experimental SpriteUI e habilitado por padrao e e obtido pelo CMake; para build offline, configure com `-DTOOL_WITH_SPRITEUI=OFF`. Passo a passo: `COMO_COMPILAR.txt` / `HOW_TO_BUILD.txt`.

## Linha de comando (núcleo e testes)
```
cmake --preset dev && cmake --build --preset dev
ctest --preset dev
```
Presets: `editor` (só o editor, CPU), `editor-gpu` (+Vulkan/OpenGL), `editor-gpu-dx11` (Windows + D3D11), `dev`, `test`, `release`, `asan`, `beta`.

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
apps/editor     editor ImGui + painel experimental SpriteUI (preset editor)   fuzz/  fuzzers   tools/  compilador de shaders   assets/shaders/hlsl e glsl
apps/sandbox    jogo de teste
tests           testes automatizados (sem dependências)
docs/           notas de construção
```
Dependências apontam sempre para baixo: scene → events/ecs → debug → core.

## Renderizador GPU (em evolucao)

O renderizador esta sendo dividido em etapas, sem substituir o `GpuRenderer` de uma vez:

- filas por indices para opacos, prepass, emissores e casters de sombra;
- batches instanciados para opacos, prepass, emissores e sombras;
- transparentes ordenados do mais distante para o mais perto; somente sequencias adjacentes com a mesma malha e textura compartilham um draw;
- metricas por passe e vista, alem de culling e cache de sombras por regiao/face.

A comparacao visual das mudancas recentes ainda esta pendente. O estado detalhado e as proximas etapas estao em
[`docs/planos/04_PENDENCIAS_EDITOR.md`](docs/planos/04_PENDENCIAS_EDITOR.md), e o plano de renderizacao esta em
[`docs/planos/02_RENDER_DRAW_CALLS.md`](docs/planos/02_RENDER_DRAW_CALLS.md).

Status: **0.7.19-beta (em andamento)** - veja `docs/referencia/CHANGELOG.md`, `docs/estudos/LIGHT_PRISM.md`, `docs/planos/06_SPRITEUI.md`, `docs/referencia/ERROS_SCRIPT.md`, `docs/estudos/REVISAO_ILUMINACAO.md`, `docs/planos/08_PARTICULAS.md`, `docs/estudos/AUDITORIA_DESEMPENHO.md`, `docs/estudos/ESCALONAMENTO_RENDER.md`, `docs/referencia/ECS.md`, `docs/planos/03_DESEMPENHO_EDITOR.md`, `docs/estudos/AMD_FSR.md`, `docs/estudos/SEGURANCA_ARQUIVOS.md`, `docs/planos/00_CHECKLIST.md`, `docs/estudos/PESO_REPOSITORIO.md` (peso do repositorio), `docs/estudos/AUDITORIA_SHADERS.md` (shaders GLSL/HLSL e limite de buffers do Vulkan).

Para a ordem recomendada de implementacao, prioridades, criterios de aceite e tarefas consolidadas:
**`docs/planos/00_ROADMAP.md`**. O roteiro vence duvidas entre documentos de tarefa antigos; os documentos
especializados continuam contendo os detalhes tecnicos e historicos.

O que ficou pendente, e o que foi feito na ultima sessao: ver a secao *0.7.19-beta (em andamento)* em `docs/referencia/CHANGELOG.md`.

**Novidades da 0.7.9-beta (erros de script):** verificador de scripts enquanto voce digita (linha e trecho), erros de execucao com linha, try/catch, avisos para objeto inexistente e NaN, e filtros no Console. Codigos em `docs/referencia/ERROS_SCRIPT.md`.

**0.7.8-beta (editor):** escala de UI 125% por padrao; icones de olho e cadeado na Hierarchy; botao direito na cena (Editar/Excluir); barra flutuante na cena com ima e "Encostar no contato"; campo de comando no Console com historico; autocomplete de scripts no VS Code (`toolengine.d.ts`, snippets e `jsconfig.json` criados ao abrir o projeto). Detalhes em `docs/referencia/CHANGELOG.md`.

## Novidades da v0.4.0-beta
Sombras em tempo real (cast/receive por objeto), gizmos mover/girar/escalar (W/E/R), navegação livre (Alt), foco por duplo clique,
tags editáveis com ligações (`jogador` → `player`), script ToolObject em todo objeto, pós-processamento global/local,
Settings > Lighting / Tags / Project (exportação, desativada), Profiler detalhado. Detalhes em `docs/referencia/CHANGELOG.md`.

## Recursos do editor (v0.3.0-beta)
- APIs gráficas trocáveis em Settings: CPU (multithread), Vulkan, OpenGL (Direct3D 12 compilado, ainda não testado).
- Importa modelos `.glb/.gltf/.fbx` e sons `.wav/.mp3` (arraste o arquivo para a janela do editor ou use o painel Assets).
- Objetos: Câmera (tag `Principal` = câmera ativa; duas Principais = split screen), Luz Global (sol, pode ser dinâmica), Fonte de Áudio, luz pontual.
- Câmera com "Sound Ear" (ouvinte de áudio).
- Cenas separadas em `Project/Scenes/*.scene.json`; assets em `Project/Assets/`.
- Grid 3D com eixos X/Y/Z no editor; no jogo só aparece se ativado em Settings > Game.
- Painel Assets: pastas, lista/ícones, renomear, apagar, nova pasta.
- Build com GPU: `build_editor.bat gpu` (Windows) ou `./build_editor.sh --gpu` (Linux); precisa clonar third_party/DiligentCore (veja HOW_TO_BUILD.txt); a primeira vez leva ~12 min.
