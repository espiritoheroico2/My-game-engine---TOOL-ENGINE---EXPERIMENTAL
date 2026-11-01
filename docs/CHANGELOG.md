# Changelog

## 0.7.7-beta — desempenho (stutters, partículas, skybox por imagens, heap)

**Corrigido**
- **Partículas com stutter**: no editor parado, a vista reaproveitava a imagem antiga e o laço dormia até 100 ms, então as partículas congelavam e pulavam. Agora há emissor vivo (ou partículas) a vista é sempre redesenhada.
- **Frame time instável / picos de 100 ms**: a espera do modo ocioso era de 100 ms. Agora é de 16 ms, e acontece ANTES da medição do quadro.
- **Zona "Events + input" inflada**: a espera ociosa aparecia como trabalho. Agora fica fora da zona.
- **"Post-processing" alto sem pós-processamento**: o skybox era contado dentro dessa zona. Agora tem zona própria: **Skybox (images)**.
- **Dynamic heap**: o Vulkan usa 64 MB (configurável com `-DTOOL_DYNAMIC_HEAP_MB=128`). Antes o Diligent usava 8 MB no D3D12/GL e o log acusava "Space in dynamic heap is exhausted".
- **Validação**: no Vulkan a validação estava forçada para desligada. Agora `DeviceInfo::validation` é o interruptor. Ele vem desligado (camadas custam CPU). Para ligar: variável de ambiente `TOOL_GPU_VALIDATION=1`. Com ela desligada, o editor emite aviso (TOOL-W201/W202/W203).
- **Formato de textura desconhecido**: cai para RGBA8_UNORM com aviso TOOL-W200 (antes ficava inválido).
- **ECS**: `resetTo` (reuso de entidades) usava `std::find` dentro do laço; agora usa um vetor de bits. `nextComponentId` aborta antes de dar a volta no id de componente.

**Mudou (pedido do usuário)**
- **Skybox não é mais procedural.** Agora são **6 slots de imagem** (+X, -X, +Y, -Y, +Z, -Z) no Settings > Game, cada um com o seletor de Assets (PNG, JPG, BMP, TGA). Lado sem imagem usa a cor sólida. Cenas antigas com o skybox procedural ficam sem imagens (cor sólida).
- Amostragem: para cada pixel de fundo, a direção do raio escolhe o lado e lê a imagem pelo vizinho mais próximo. A orientação segue a tabela de cube map do OpenGL. Se algum lado aparecer girado ou espelhado, confira visualmente (a troca está comentada em `skybox.hpp`).

**Não mudou nesta versão (plano em `docs/PERFORMANCE_PLAN.md`)**
- O caminho da GPU ainda faz leitura de volta (readback) com espera total a cada quadro. Esse é o custo principal que sobrou. O readback assíncrono está no plano.
- FXAA ainda roda na CPU.
- OpenGL: a janela já é sempre criada pelo editor (`applyApi`). A textura continua desligada no GLES.

**Testes**
- Engine: 133 testes, 0 falhas.
- Editor (selftest): 134 checks, 0 falhas.
- gpu_test (Vulkan): OK (IoU 0,998; textura GPU x CPU batendo).

## 0.7.6-beta — correção do build Windows (Direct3D 12)

**Corrigido**
- Build `editor-gpu-dx12` falhava no MSBuild com `FTK1011: could not create the new file tracking log file` (projetos Diligent GraphicsEngineNextGenBase e GraphicsEngineOpenGL). Causa: o caminho dos arquivos `.tlog` passava de 260 caracteres (limite MAX_PATH do Windows), somando a pasta do projeto com `build\editor-gpu-dx12\_deps\...`.
- Preset `editor-gpu-dx12` agora usa a pasta de build `out\` (antes `build\editor-gpu-dx12\`), o que tira 18 caracteres do caminho.

**Como atualizar**
- Apague a pasta `build\editor-gpu-dx12` antiga (o cache dela aponta para o caminho antigo).
- Rode de novo `build_editor.bat gpu`. Os arquivos de build passam a ficar dentro de `out\`.
- Se ainda falhar, duas opções extras: mover a pasta do projeto para um caminho curto (ex.: `C:\TE`) ou ativar caminhos longos no Windows (`LongPathsEnabled = 1` em `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem`, com reinício).

**Avisos do build (não são erros)**
- Os avisos C4334 e C4702 do `quickjs-ng` são de terceiros e não impedem o build.

## 0.7.5-beta — texturas de albedo em MeshRenderer

**Novo**
- Campo **Texture** no componente Mesh Renderer (Inspector): uma imagem da pasta Assets (PNG, JPG, BMP, TGA) que multiplica a cor do objeto. Salva na cena.
- Carregamento de imagem com `stb_image` (domínio público, em `third_party/stb`). Limites: 64 MB por arquivo, 8192 px por lado.
- UV nas formas primitivas (plano, cubo, esfera), para a textura cair certo.
- Textura de exemplo: `assets/samples/textura_xadrez.png`.

**Como funciona**
- Amostragem "mais próximo" com repetição (UV fora de 0..1 repete). Origem do UV no canto superior esquerdo, como no glTF.
- Os valores da textura são usados como estão (sem conversão sRGB), igual à cor do material. CPU e GPU dão o mesmo resultado.
- Na GPU, o RHI agora **envia os dados iniciais** das texturas (antes `createTexture` ignorava os dados).

**Testes**
- `tool_tests`: +2 testes (amostragem e textura no renderizador de software). 133/0.
- `gpu_test` (Vulkan): plano xadrez vermelho/azul, GPU e CPU com a mesma cor nos dois lados.
- Selftest: 133 checks, 0 falhas (carregamento do PNG e origem do UV).

**Limites conhecidos**
- **OpenGL/GLES: texturas desligadas** (com aviso no Console). Na GPU de teste a textura não chega ao shader; ainda não encontrei a causa no binding do Diligent GLES.
- **Direct3D 12: não testado aqui.** Usa o mesmo HLSL do Vulkan; confira na sua máquina.
- Sem filtragem bilinear nem mipmaps.

## 0.7.4-beta — split com preenchimento, câmera 2D, skybox de 6 lados

**Novo**
- **Split screen "Fill each half"** (Settings → Game): cada câmera preenche a própria metade, sem barras pretas com resolução 16:9. A câmera mantém o campo de visão vertical; as laterais são cortadas. Padrão: desligado (comportamento anterior).
- **Câmera 2D** (Inspector → Camera → "2D camera (orthographic)"): projeção ortográfica com altura visível em unidades do mundo. Salva no objeto.
- **Skybox procedural de 6 lados** (Settings → Game): cada lado (+X, -X, +Y, -Y, +Z, -Z) escolhe entre Gradiente de céu, Montanhas e mar, Mar ou Cor sólida. Padrão em cena nova: laterais com montanhas e mar, topo com gradiente, base com mar. Cenas antigas continuam sem skybox. Ver `apps/editor/skybox.hpp`.
- `docs/CHECKLIST.md`: tudo o que foi feito e o que falta.

**Mudanças no núcleo**
- `tool::Camera` ganhou `ortho` e `orthoHeight` e o método `projection(w, h)`, usado pelo renderizador de software e pela GPU.
- `SoftRenderer::begin` e `GpuRenderer::begin` aceitam alfa de fundo (`clearAlpha`). Com skybox ligado, o fundo sai com alfa 0 e o céu é pintado depois, só nesses pixels.

**Testes**
- Selftest: 131 checks, 0 falhas (novos: split e skybox salvos na cena, projeção 2D, faces do skybox, pixels com geometria preservados).
- `tool_tests`: 131/0. `ctest` editor-gpu: 4/4.
- Imagem gerada pela CPU conferida visualmente (montanhas, mar, céu, objetos na ordem certa).

**Limites conhecidos**
- Grid e gizmos ainda usam a projeção perspectiva; na câmera 2D não ficam alinhados.
- O skybox custa uma conta por pixel de fundo, em paralelo. Em telas grandes com pouca geometria, pesa.
- A orientação da imagem do skybox na GPU não foi conferida visualmente aqui (o caminho da CPU foi).

## 0.7.3-beta — janela de início: nome, autor e pasta do projeto

**Novo**
- Ao abrir o editor, uma janela pede o **nome do projeto**, o **autor** e a **pasta onde ele será salvo** (com botão "Procurar..." nativo). Também oferece **abrir uma pasta de projeto existente** e **abrir o último projeto**.
- Todos os arquivos do projeto (`project.json`, `Assets/`, `Scenes/`, `Saves/`, scripts e cenas) ficam dentro da pasta escolhida. Os ajustes globais do editor (`editor_settings.json`, `editor_layout.ini`, tema) continuam ao lado do executável.
- Espaço reservado à direita da janela para o **logo da engine** (`drawLogoSlot` em `apps/editor/project_dialog.hpp`).
- `--project <pasta>` abre (ou cria) um projeto direto, sem a janela. Útil para testes e linha de comando.
- O título da janela mostra o nome do projeto.

**Testes**
- Selftest: +5 checks (criar projeto, gravar nome e autor, recusar pasta ocupada, recusar nome com `..`, recusar pasta inexistente ao abrir). Total 124/0.

**Limites**
- A janela de início não pôde ser clicada em ambiente sem tela; foi verificado que ela roda sem travar e que `--project` grava tudo na pasta indicada.

## 0.7.2-beta — etapa 2: upscaling e antisserrilhado por software, partículas em lotes, auditoria

**Novo**
- Upscaling por software (Bilinear, Bicubic Catmull-Rom, Lanczos2) e FXAA simplificado, em `engine/render/include/tool/render/upscale.hpp`. Configurável em Settings → Graphics; aplicado às vistas Scene e Game. Ver `docs/RENDER_SCALING.md`.
- Orçamento global de partículas (`particleBudget`, padrão 100 000).
- `GpuRenderer::drawDynamic`: malhas dinâmicas reaproveitadas por slot.
- Testes: 4 de upscaling/FXAA em `tests/test_render.cpp`; 3 checks de orçamento e lotes no selftest.

**Mudanças**
- Partículas desenhadas em lotes por cor (≤ 64 chamadas por vista) com teste de profundidade, no lugar de uma chamada por partícula e do overlay 2D. Ver `docs/PARTICLES.md`.
- Criação e integração de partículas em paralelo por emissor.
- Remoção de emissores sem `std::find` (marca de quadro).
- `meshBounds` limitado a 4096 entradas e limpo ao abrir cena.
- Configurações `upscaleMode` e `aaMode` salvas em `editor_settings.json`.

**Correções**
- Cache de malhas da GPU: validação por ponteiro e tamanho, recriação no mesmo nó, eviction de malhas não usadas por 240 quadros (evita vazamento e geometria obsoleta).
- Pesos do bilinear deslocados em um pixel.
- Uso após liberação em `selftest` (ponteiro de `ScriptComp` após `duplicate`), achado pelo ASan.

**Verificação**
- ASan + UBSan: selftest 119/0, sem erros de memória nem de UB. LeakSanitizer reporta 96 bytes de bibliotecas carregadas com `dlopen` (miniaudio/JACK), fora do projeto. Ver `docs/PERFORMANCE_AUDIT.md`.
- `tool_tests`: 131/0. `ctest` editor-gpu: 4/4.

**Documentação**
- Novos: `docs/PARTICLES.md`, `docs/PERFORMANCE_AUDIT.md`, `docs/RENDER_SCALING.md`.
- Retroativo à etapa 1: `docs/ECS.md` (ECS, uso, desempenho e teste `test ECS: N`).

**Pendente (medido ou identificado, não corrigido)**
- Readback GPU → CPU a cada quadro (maior custo de sincronização restante).
- `World::each` copia a lista de índices a cada chamada.
- Contexto QuickJS por objeto com script.

## 0.7.1-beta — etapa 1: partículas com profundidade, economia ociosa, Console e teste de ECS

- **Partículas atrás dos objetos**: cada partícula agora é um cubo pequeno no pipeline 3D (vale para GPU e CPU), então o teste de profundidade a esconde quando há um objeto na frente. Continua emitindo. Antes era um desenho 2D por cima de tudo (removido).
- **Economia com o editor ocioso**: sem evento (mouse, teclado, janela) por 3 quadros e sem Play, a espera do laço vai a até 100 ms e as vistas Scene e Game reaproveitam a imagem anterior. Causa do uso alto de GPU: a cena era renderizada a cada quadro mesmo parada, com VSync ligado. Qualquer interação volta a renderizar na hora.
- **Console com linha de comando**: `help`, `clear`, `test ECS: N`.
- **Teste de ECS** (`docs/ECS.md`): `test ECS: N` cria o objeto `ECScall` e N esferas filhas dele, que caem do céu com Newtoon, em lotes de ~N/240 por quadro. Padrão 10 000, máximo 1 000 000. Parar o Play remove tudo.
- **Documentação do ECS**: `docs/ECS.md` (conceitos, API, como o editor usa, como adicionar sistema, pontos de desempenho, resposta sobre "entidade única por tag/código" e caminho recomendado).
- Testes: selftest 116 checks, 0 falhas (inclui ECS e comandos do Console).

### Pendências desta etapa (próximas etapas, em ordem)
1. **Etapa 2 (camada de imagem)**: upscaling por software (bilinear, bicúbico, Lanczos), antialiasing por software (FXAA-like sobre o buffer), e base para FSR depois. ReShade: não se encaixa como camada interna (é um injetor para jogos com DLL nas APIs D3D/OpenGL/Vulkan e não enxerga o renderer da engine); o caminho é implementar os efeitos dentro da engine, com custo controlado.
2. **Etapa 3 (câmera 2D)**: opção de câmera ortográfica 2D no Inspector.
3. **Etapa 4 (Sprites)**: biblioteca 2D, entidade `Sprite` (cor e tamanho agora; textura quando o upload de textura existir), física Newtoon em modo 2D, e a janela **Sprite Editor** criada, desativada por padrão e bloqueada.
4. **Etapa 5 (Skybox)**: suporte a skybox de 6 faces; skybox padrão de montanhas e mar gerado pelo editor.
5. **Auditoria de desempenho**: três pontos já visíveis no código: (a) a imagem da GPU é lida de volta para a CPU e reenviada ao SDL a cada quadro (o maior custo de sincronização; o caminho ideal é apresentar a textura direto); (b) `World::each` copia índices a cada chamada; (c) cache de limites de malha (`meshBounds`) cresce sem remoção. Não rodei ASan/Valgrind nesta etapa, então vazamentos ainda não estão comprovados nem descartados.
6. **Lote anterior ainda pendente**: fontes HD e importação de .ttf/.otf; imagens em elementos de UI; UI aninhada; texturas/shader/malha no MeshRenderer; occlusion culling além do frustum; avaliação do `microsoft/DirectX-Graphics-Samples`.
7. **Bloqueados**: sombras de point light, .tmat / shader por objeto, NTC, DXR.


## 0.7.0-beta — scripts ToolObject executam (QuickJS), edição de objetos e UI

- **Scripts ToolObject rodam no Play** com QuickJS-ng (MIT, `third_party/quickjs-ng`, opção `TOOL_WITH_QUICKJS`, ligada por padrão). Cada objeto tem o seu contexto. Funções: `start(obj)`, `update(obj, dt)`, `OnEventTrigger(obj, name)`; host: `send`, `setActive`, `instantiate`, `destroy(nome, segundos)`, `addComponent`/`removeComponent` (MeshRenderer, AudioSource, ParticleComp), `setPosition(nome, x, y, z)`, `vec3`, `debug.console`, `time.now/dt`, `obj.transform.pos/rot/scale`.
- **Limites**: 16 MB de memória (todos os scripts juntos), pilha 256 KB, 5 ms por chamada (interrupt). Um erro para só o script daquele objeto; a mensagem vai ao Console e ao Inspector (`Script status`). `Reload scripts` reinicia durante o Play.
- **Scripts opcionais**: objeto novo não cria script; "Add Script" cria sob demanda. "Remove Resource" remove qualquer componente.
- **Narrative**: `send`/`setActive` na fila de eventos; `send` também chega aos `OnEventTrigger` dos scripts.
- **Objetos ativos/inativos** (`Active` no Inspector, `setActive`): inativo não desenha, não simula, não tem luz nem sombra e não roda update.
- **Hierarquia**: renomear (menu e F2). **Inspector**: seções recolhíveis; tooltips nos controles principais; botões "?" abrem o tópico certo do Help (corrigido: a barra de título não consumia o clique).
- **Help**: tópicos "Script API" (exemplos: controlar objetos, enviar eventos, instanciar/destruir, adicionar/remover componentes, desativar) e "JS library" (planejado).
- **Culling**: frustum culling com opção em Settings (objetos que projetam sombra nunca são cortados). Contador `culled` no profiler.
- **Textos**: 537 frases em EN/PT-BR/ES (`tools/gen_lang_packs.py`).
- Testes: selftest do editor 110 checks (inclui scripts reais: start/update, evento, erro isolado, Reload, instantiate); ctest do `editor-gpu` 4/4.
- **Diferenças para o pedido original**: a sintaxe `player.transform = vec3(...)` ainda não existe; use `setPosition("player", 4, 4, 5)`. Escrever direto no transform de outro objeto não é suportado. `import` de bibliotecas JS ainda não (o script recebe erro claro). O "editor de código em runtime" é a caixa de texto do Inspector, não uma janela com realce de sintaxe.
- **Não feito neste lote (planejado):** occlusion culling além do frustum (Hi-Z / occlusion queries); fontes HD e importação de .ttf/.otf; imagens em elementos de UI (falta caminho de textura no renderer); UI aninhada; texturas, shader e modificação de malha no MeshRenderer; avaliação do `microsoft/DirectX-Graphics-Samples`.
- **Ainda bloqueados**: sombras de point light, .tmat / shader por objeto, NTC, DXR.

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
