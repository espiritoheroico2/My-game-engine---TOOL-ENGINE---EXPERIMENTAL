# Changelog

## 0.6.0-beta — hierarquia, UI de jogo, partículas, Narrative em execução

- **Hierarquia pai/filho**: árvore na janela Hierarchy (arrastar objeto sobre outro = filho; soltar no vazio = desanexa; menu "Create child"). Transform do filho é local; mundo = pai × local. Salva/undo com pais; apagar o pai apaga a subárvore; corpos físicos filhos são soltos do pai ao dar Play.
- **+ Add Resource** no Inspector: um botão com a lista de recursos (Física, Tools, Malha, Point Light, Câmera, Áudio, Partículas, UI).
- **Pause / Resume / Step** no Play.
- **UI de jogo** (`UiCanvas`): camada 2D por câmera (várias; escolhe a câmera), Texto/Painel/Botão/Barra, `{variáveis}`, botões enviam eventos. Exemplo na cena padrão.
- **Partículas** (`ParticleComp`): emissor com gravidade da física do mundo (Newtoon), cor/tamanho início→fim, quique.
- **Narrative em execução**: gatilhos Start / Update / Create(objeto); blocos SavePoint / LoadPoint (`Project/Saves`); ações log/spawn/destroy/ui_show/ui_hide/particles_on/off; **toda cena ganha grafo + código** (`Assets/Narrative`, `Assets/Scripts/Scenes`) e o Play executa o grafo.
- **Desempenho**: pós-fx em ponto fixo + pool de threads persistente (`tool::parallelFor`); **sleeper de sombras** (reaproveita o shadow map se luz/casters não mudaram, CPU e GPU).
- **Build**: preset `editor-gpu-dx12` (Windows) para Direct3D 12.
- Docs: `JS_VS_CPP.md`, `DX12_DXR.md`, `TEXTURES.md`, `UI_FX.md`, `RENDER_PERF.md`.
- Correção de build no Windows: `far` é macro do windows.h (selftest.hpp); recursos não implementados aparecem BLOQUEADOS no menu "+ Add Resource".
- Correções: `d` das Tools não era lido ao carregar; avisos de sombra/parâmetro não usado.
- **Não feito (planejado):** sombras de point light, texturas/normal map/materiais .tmat, shader por objeto, NTC, DXR, pós-fx na GPU.

## 0.5.0-beta — física Newtoon, Tools, Input Map, Help

- **Física Newtoon** completa (`engine/physics`, `docs/PHYSICS.md`): colisão esfera/caixa, solver com atrito, sono, CCD, gatilhos, eventos, raycast, BVH, pool de threads.
- **Vector physics**: gravidade própria por objeto que substitui a do mundo. **Settings > Physics**: gravidade do mundo em X/Y/Z, iterações, passos/s, desenhar colliders.
- **Inspector**: componentes *Physics* e *Tools* (Jump, Move, Follow Target, Spin, On Collision, Script library). O Play simula e, ao parar, restaura a cena.
- **Input Map** (janela): vários mapas de controle, ação = tecla OU botão/eixo do gamepad escolhidos em listas, botão "Listen"; SDL3 gamepad (XInput no Windows). Salvo em `Project/input_map.json`.
- **Help** (F1 / ícone "?" em cada janela): 16 tópicos PT/EN com exemplos (pular, andar, câmera seguindo, colisão/moeda, gravidade).
- **Edit external** nos scripts (Settings > Project define o editor; executado direto, sem shell).
- **Narrative Controller**: largura dos nós fixa e texto cortado (corrige nós esticando); posições limitadas.
- Docs: `THIRD_PARTY.md` (APIs, donos, repositórios, licenças), `NTC.md`, `RUNTIME.md` (recomendação: QuickJS + WASM depois).
- Testes: 124 de motor, selftest do editor 79 checks.
- **Não feito nesta versão:** sombras de point light, Material/shader por objeto, botão "Compress as NTC" (ver `NTC.md`), execução de scripts.

## 0.4.1-beta — desfazer/refazer e seleção por raio

- **Desfazer / Refazer** (`Ctrl+Z`, `Ctrl+Y` ou `Ctrl+Shift+Z`; menu **Edit**). `apps/editor/undo.hpp`: cada estado é a cena serializada
  (mesmo JSON do arquivo); ao terminar uma interação (soltar o mouse, sair de um campo, tecla Delete) o editor compara com o último
  estado e, se mudou, cria um passo. Cobre gizmo, Inspector, hierarquia, criar/duplicar/apagar sem código por comando.
  Limites: 100 passos / 16 MB. Fica desligado durante o Play. Abrir/criar cena zera o histórico.
- **Seleção por raio** na janela Scene: o clique dispara um raio e testa caixa/esfera de cada objeto no espaço local (respeita
  rotação e escala; modelos usam a caixa envolvente da malha). Sem acerto, mantém a regra antiga (centro mais próximo) para luzes,
  câmeras e objetos vazios. O chão continua não disputando o clique.
- Testes: 12 novos checks no `editor_selftest` (67 no total), ctest 4/4 (GPU) e 113 testes de motor.

## 0.4.0-beta — sombras, gizmos, tags, scripts, pós-processamento

### Renderização
- **Sombras em tempo real** do sol (shadow mapping ortográfico, texel-snapping, bias + slope bias + normal bias, PCF 3x3/5x5, força).
  CPU (`SoftRenderer`) e GPU (`GpuRenderer` + shaders HLSL/GLSL) usam a MESMA matriz de luz (`render/shadow.hpp`).
  O mapa de sombra cobre só a região dos objetos que projetam sombra: mais nitidez e **custo zero** se ninguém projeta.
- **Cast Shadow / Receive Shadow por objeto**, independentes (Inspector > Mesh Renderer). O sol liga/desliga em Global Light.
- **Pós-processamento global (cena) e local (câmera)**: exposição, contraste, saturação, tint, vinheta, nitidez. Uma única passada
  CPU multithread com tabelas (`render/postfx.hpp`); custo zero se desligado. A câmera soma ao global ou substitui.
- `GpuRenderer`: desenho agora é adiado até `end()` (passe de sombra → passe principal) e agrupado por malha.
- **Correções 3D** (achadas na varredura): (1) OpenGL aplicava `z = 2z - w` com contexto de clip 0..1, jogando metade da profundidade fora
  (objetos perto da câmera sumiam; sombras cortadas); (2) OpenGL aplicava gamma duas vezes (alvo sRGB + gamma no shader) → agora alvo UNORM +
  gamma no shader, igual em qualquer driver; clear do OpenGL também convertido; (3) viewport definido explicitamente a cada passe
  (alvos de tamanhos diferentes no mesmo quadro).
- Verificado por `gpu_test` em Vulkan e OpenGL: silhueta IoU 0.998 contra a CPU e 171 pixels de sombra idênticos nas duas APIs.

### Editor
- **Gizmo de transformação** na janela Scene: mover (W), girar (E), escalar (R) arrastando setas/anéis/cubinhos; Ctrl = passos.
- **Navegação livre (Alt)**: câmera em primeira pessoa (WASD, Q/E, Shift, roda = velocidade; mouse preso; Alt/Esc sai).
- **Duplo clique** na Hierarquia foca o objeto na cena (também no menu de contexto > Focus).
- **Tags editáveis**: adicionar/remover/renomear no Inspector (renomeia em todos os objetos e nos arquivos das outras cenas).
- **Settings > Tags**: tags pré-definidas (`Principal`, `player`, `enemy`) e **ligações** (ex.: `jogador` → `player`); salvas em `Project/project.json`.
- **Scripts**: todo objeto nasce com `Assets/Scripts/<Objeto>.tool.js` (ToolObject com `start`, `update`, `OnDelete`, `DeleteAndReuse`);
  edição e salvamento no Inspector. *A execução dos scripts ainda não existe* (ver NEXT_STEPS.md).
- **Settings > Lighting**: sol, ambiente, sombras (qualidade, distância, suavidade, força, bias) e pós-processamento global, por cena.
- **Settings > Graphics**: qualidade de sombra, detalhes da GPU. **Settings > Project**: preset de plataforma de exportação (guardado, **desativado**).
- **Profiler**: FPS médio/mín/máx, 1% low, jitter, gráfico, tempo por zona (eventos, update, painéis, render Scene/Game, pós-fx, UI, present),
  contadores (views, pixels, objetos, triângulos, sombras, GPU), entidades, memória do processo (atual/pico).
- Cenas de versões antigas ganham scripts automaticamente ao abrir.

### Projeto
- Pasta `Projeto Final/` recebe o executável e tudo que o editor precisa (assets, LEIA-ME).
- Limpeza: removidos `bin/`, `out/`, arquivos de configuração e `Project/` de teste da árvore de código; `.gitignore` atualizado.
- Idiomas: pt-BR e es atualizados (323 frases).

### Testes
tool_tests 113 testes (inclui sombras, postfx e tags), `editor_selftest` 55 checks, `gpu_test` com verificação de sombra (Vulkan/OpenGL).
