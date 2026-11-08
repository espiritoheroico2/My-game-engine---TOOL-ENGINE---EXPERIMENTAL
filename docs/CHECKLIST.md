# Checklist do ToolEngine

Legenda: [x] feito · [~] parcial · [ ] pendente. Versões em `docs/CHANGELOG.md`.

## Feito

### 0.7.17-beta (AO, hierarquia e multi-material, projeto enxuto)
- [x] SSAO (Light Prism `Post/Ssao`) na CPU e na GPU; AO so no ambiente; gpu_test compara GPU x CPU (Vulkan)
- [x] Importacao de glTF e FBX preserva a hierarquia de nos (pai, TRS local) e cria objetos filhos no editor
- [x] Multi-material: cada parte com o proprio material (glTF por primitive, FBX por LayerElementMaterial)
- [x] Inspector: sobrepor material por parte (PartOverrides), salvo no projeto
- [x] Limpeza medida: registry do Vulkan, FormatValidation, capturas nao citadas e recompressao PNG sem perda
- [ ] Textura por parte (hoje a textura do objeto vale para todas as partes)
- [ ] Animacao e skins (ainda ignorados no importador)
- [ ] D3D12: compilar e testar na maquina Windows (nao verificado neste ambiente Linux)
- [ ] SSAO: meia resolucao exposta e medida de custo da CPU em resolucoes altas

### 0.7.16-beta (sombras de luzes pontuais e spot, CPU e GPU)
- [x] Sombra de point light (cubo de 6 faces) e de spot (1 face) no SoftRenderer, ate 4 luzes por distancia da camera
- [x] Settings > Lighting: luzes com sombra (0 a 4) e resolucao por face; salvas no project
- [x] Testes: sombra atras do bloco, orcamento e numero de faces (tool_tests: 131, 0 falhas)
- [x] gpu_test: sombra pontual GPU x CPU (1586 x 1584 pixels escurecidos), Vulkan OK
- [x] Sombras de pontual/spot na GPU (faixa R x 6R por luz, viewport por face, PointShadow.hlsli); CPU e GPU batem no teste (Vulkan/lavapipe)
- [x] Sol respeita receiveShadow na GPU (paridade com a CPU)
- [ ] D3D12: compilar e testar na maquina Windows (nao verificado neste ambiente Linux)
- [x] SSAO: feito na 0.7.17-beta
- [ ] Ray tracing: Light Prism `Rt/Bvh` e `RtEffects` (sombra de contato, AO), CPU primeiro; GPU depois
- [ ] Demais recursos da Light Prism: VSM, SDF, fog volumetrico, probes, PRT, SSR, VPL emissivo, planar shadows, CascadeCache, MemoryBudget
- [ ] Pagina de ajuda do Light Prism no editor (help_content.hpp) e textos pt-BR/es
- [ ] Um so caminho de iluminacao para CPU e GPU (hoje a fachada e o LightRig, mas o AO/RT ainda nao passam por ela)

### 0.7.15-beta (iluminacao pela Light Prism)
- [x] Light Prism integrada (third_party/LightPrism) como unica iluminacao da engine
- [x] CPU (SoftRenderer) com BRDF, sombras CSM e ambiente SH9 da Light Prism
- [x] GPU (Diligent, Vulkan) com clusters da Light Prism no mesmo dispositivo do RHI
- [x] Settings > Lighting: presets, personalizado, orcamento de luzes, sombras do sol, ambiente
- [x] Cenas antigas (`render.shadow`) convertidas ao abrir
- [x] Profiler mostra cascatas ativas e luzes descartadas
- [x] Textos da interface em pt-BR e es
- [x] Testes: tool_tests (129), gpu_test vulkan, editor_selftest
- [ ] OpenGL: iluminacao em GLSL (hoje cai para a CPU)
- [ ] D3D12: teste na GPU real (so Vulkan/lavapipe foi testado)
- [ ] Point Light e Global Light como tipos proprios (hoje sao componentes do editor sobre a Light Prism)

### 0.7.14-beta (desempenho com muitos objetos e chunks opcionais)
- [x] Benchmark headless: `--ecs N` e `--voxel N` (exige `--project` de teste)
- [x] Esfera com nivel de detalhe acima de 25 m (sombra e culling coerentes)
- [x] Hierarquia com mais de 300 objetos em lista achatada com ImGuiListClipper
- [x] Malha com revisao: GPU reenvia os buffers quando a malha muda
- [x] BVH: pilha de 128 sem descarte silencioso (ajuste de corretude)
- [x] Chunks de blocos: componente, malha de faces visiveis, culling entre chunks, RLE na cena
- [x] Chunks: Inspector com grade por camada, pincel e terreno de teste
- [x] Traducoes pt-BR e es dos textos novos
- [x] Selftest do editor: 207 checks, 0 falhas
- [~] Chunks no caminho da GPU: compila, nao executado nesta maquina
- [~] Esfera distante e hierarquia achatada: nao vistas na tela
- [ ] Colisao de fisica por chunk
- [ ] Undo para edicoes de blocos do Inspector
- [ ] Selecao de chunk no viewport
- [ ] Sombra dos chunks em mundos grandes (area do mapa de sombra)

### 0.7.13-beta (exportacao Windows x64 e Linux x64, Editor info, modo jogador, Newtoon)
- [x] Exportacao Windows x64 e Linux x64 com launcher
- [x] Janela Editor info (salva em project.json)
- [x] Modo jogador (`--player`)
- [x] Integracao do Newtoon como arquivos e patch
- [x] Selftest do editor: 196 checks, 0 falhas
- [ ] Android, WebGL, UWP
- [ ] Versao do executavel no Windows

### 0.7.12-beta (particulas e correcoes do editor)
- [x] Particulas escuras corrigidas (cor e alfa reais, sem iluminacao)
- [x] Delete e F2 nao agem sobre o objeto enquanto um campo de texto esta ativo
- [x] Particulas: formatos (bolinha, quadrado, triangulo, estrela, anel, risco)
- [x] Particulas: area de nascimento aleatoria (caixa e esfera)
- [x] Particulas: 4 cores ao longo da vida
- [x] Particulas: opacidade ao longo do tempo e atraso da opacidade
- [x] Particulas: trajetoria com curva suave (Trace route) e desenho no editor
- [x] Particulas: tornado (giro em funil)
- [x] Selftest do editor: 188 checks, 0 falhas
- [~] Texto apagando sozinho no console/renomear: nao reproduzido; precisa de tecla e campo exatos
- [~] Aparencia das particulas e da trajetoria: nao vista na tela
- [ ] Particulas com teste de profundidade (passe com mistura alfa no renderizador)
- [ ] Gravidade e quique para trajetoria e tornado

### 0.7.11-beta (formas 3D, interface travada, bloco da cena, Inspector)
- [x] Formas 3D: cilindro, cone, capsula, roda, prisma triangular e hexagonal (menu e Mesh Renderer)
- [x] Interface: travar layout (sem desencaixar nem dividir), encaixe so com Shift, Redefinir layout
- [x] Narrative: bloco padrao da cena com o nome da cena, indestrutivel
- [x] Inspector: Transform X/Y/Z, Active e Locked na mesma linha, rodape (Adicionar componente, Recolher/Abrir tudo)
- [x] Selftest do editor: 182 checks, 0 falhas
- [~] Inspector: secoes de Fisica, Ferramentas e Script ainda no formato antigo
- [~] Aparencia do Inspector e do menu Interface: nao vista na tela, conferir
- [ ] Copiar componente e Remover componente no Inspector
- [ ] Colisor exato para cilindro, cone, capsula, roda e prismas (hoje caixa envolvente)
- [ ] Build Windows/MSVC desta versao: nao compilado nesta maquina

### 0.7.10-beta (profiler do sistema e revisao de iluminacao)
- [x] Profiler: CPU total, nucleos em uso, CPU por nucleo e do editor
- [x] Profiler: RAM do sistema e do editor
- [x] Profiler: VRAM do processo (DXGI no Windows, amdgpu no Linux) ou motivo
- [x] Profiler: tamanhos de cache (L1, L2, L3)
- [x] Help: "Casts shadows" em luz pontual corrigido (nao existe)
- [x] Revisao da iluminacao em docs/LIGHTING_REVIEW.md
- [x] Selftest do editor: 172 checks, 0 falhas
- [~] Caminho Windows do Profiler (DXGI, NtQuerySystemInformation): nao compilado nesta maquina
- [ ] Taxa de acerto do cache (exige contadores de hardware)
- [ ] VRAM do motor (recursos criados pelo renderizador)
- [ ] Tempo de GPU do passe de sombra (timestamps)
- [ ] Sombra de luz pontual (mapa de cubo) e spot
- [ ] Cascatas de sombra (CSM) para o sol; remover ou ligar `cascadeSplits` e `assignLights`

### 0.7.9-beta (erros de script e console)
- [x] Verificador de scripts enquanto digita (Inspector), com linha e trecho
- [x] Verificacao ao salvar, ao dar Play e pelo botao "Check scripts" / comando `scripts`
- [x] Sintaxe com linha e coluna (QuickJS, sem executar)
- [x] Regras: obj.position, class, this, import, console.log, funcao desconhecida com sugestao, argumentos, teclas, maiusculas
- [x] Erro de execucao com funcao, linha e coluna; try/catch funciona no script
- [x] Argumento invalido lanca TypeError (nao vira NaN em silencio)
- [x] Aviso para objeto inexistente (S005) e valor invalido no Transform (S006)
- [x] debug.warn e debug.error; snippet ttry
- [x] Console: filtros por nivel e texto, Copy, clique seleciona o objeto
- [x] Help: secao Erros e try/catch (EN e PT); docs/SCRIPT_ERRORS.md
- [x] Help: tutoriais de FPS e de portas com E (EN e PT), codigo testado no selftest
- [x] Selftest do editor: 166 checks, 0 falhas
- [~] Lista de problemas no Inspector e filtros do Console: nao verificados visualmente
- [ ] Verificador tambem no editor de texto de arquivos (hoje so no Inspector e no Console)

### 0.7.8-beta (usabilidade do editor e scripts)
- [x] Cache da lista de assets e dos combos (sem varredura de pasta a cada quadro)
- [x] Escala padrao da UI em 125% (migracao de temas antigos)
- [x] Icones de olho (ocultar) e cadeado (travar) na Hierarchy
- [x] Duplicar filho mantem o pai
- [x] Menu de botao direito na cena: Editar e Excluir
- [x] Barra flutuante na Cena com Mover, Girar, Escalar, ima e "Encostar no contato"
- [x] Campo de comando no Console com historico
- [x] Scripts: `position`, `setRotation`, `setColor`, `input`, `instantiate` com posicao e nome
- [x] Template de script em funcoes (start, update, OnEventTrigger)
- [x] Help: API e 12 exemplos testados (selftest roda todos)
- [x] Autocomplete do VS Code: `toolengine.d.ts`, snippets e `jsconfig.json` gerados ao abrir o projeto
- [x] Selftest do editor: 146 checks, 0 falhas
- [~] Verificacao visual (icones, barra flutuante, escala 125%, console): nao feita neste ambiente
- [~] Ganho de desempenho do editor: nao medido (usar Profiler, zona "Panels")
- [ ] Propriedades de shader expostas aos scripts
- [ ] Biblioteca SpriteUi (plano em `docs/UI_SPRITE_PLAN.md`; implementacao com o usuario)

### Etapa 1 (0.7.1-beta)
- [x] Partículas com teste de profundidade (ficam atrás de objetos da frente, continuam emitindo)
- [x] Economia de GPU com o editor ocioso (renderiza só quando há mudança)
- [x] Console com comandos (`help`, `clear`, `test ECS: N`)
- [x] Teste de eficiência do ECS (`test ECS: N`, até 1 000 000 esferas com Newtoon)
- [x] Documentação do ECS (`docs/ECS.md`)

### Etapa 2 (0.7.2-beta)
- [x] Upscaling por software: Bilinear, Bicubic (Catmull-Rom), Lanczos2 (Settings → Graphics)
- [x] Antisserrilhado por software: FXAA simplificado
- [x] Testes de upscaling e FXAA (`tests/test_render.cpp`)
- [x] Partículas em lotes por cor (≤ 64 chamadas por vista) em vez de uma chamada por partícula
- [x] Orçamento global de partículas (`particleBudget`)
- [x] Simulação de partículas em paralelo por emissor
- [x] Cache de malhas da GPU corrigido (validação, eviction, malhas dinâmicas)
- [x] Limite de `meshBounds` e limpeza ao abrir cena
- [x] ASan + UBSan: selftest sem erros de memória nem de UB; uso após liberação corrigido
- [x] Documentação: `PARTICLES.md`, `PERFORMANCE_AUDIT.md`, `RENDER_SCALING.md`

### Etapa 3 (0.7.3-beta)
- [x] Janela de início: nome do projeto, autor e pasta onde salvar
- [x] Abrir pasta de projeto existente e abrir o último projeto
- [x] Todos os arquivos do projeto ficam na pasta escolhida
- [x] Espaço reservado para o logo da engine
- [x] `--project <pasta>` (linha de comando)

### Etapa 5 (0.7.5-beta)
- [x] Texturas de albedo em MeshRenderer (PNG, JPG, BMP, TGA): campo "Texture" no Inspector, salvo na cena
- [x] Texturas no software e na GPU (Vulkan) com o mesmo resultado; teste GPU x CPU em `gpu_test`
- [x] UV nas formas primitivas (plano, cubo, esfera)
- [x] Textura de exemplo: `assets/samples/textura_xadrez.png`
- [~] Texturas no OpenGL/GLES: desligadas com aviso (ver Pendente). Direct3D 12: mesmo HLSL do Vulkan, mas NAO testado aqui

### Desempenho (0.7.7-beta)
- [x] Partículas sem stutter no editor parado (vista não reaproveita imagem com emissor vivo)
- [x] Espera ociosa de 16 ms (antes 100 ms) e fora da zona "Events + input"
- [x] Zona própria do skybox ("Skybox (images)"), separada de "Post-processing"
- [x] Dynamic heap do Vulkan em 64 MB, configurável (`TOOL_DYNAMIC_HEAP_MB`)
- [x] Validação de camadas desligada por padrão; `TOOL_GPU_VALIDATION=1` liga; avisos TOOL-W201/W202/W203
- [x] Fallback de formato de textura com aviso (TOOL-W200)
- [x] ECS: `resetTo` sem `std::find`; guarda de overflow em `nextComponentId`
- [x] Skybox de 6 slots de imagem (substitui o procedural)

### Correções (0.7.6-beta)
- [x] Build Windows Direct3D 12: caminho dos `.tlog` acima de 260 caracteres (pasta de build encurtada para `out\`)

### Etapa 4 (0.7.4-beta)
- [x] Split screen: opção "Fill each half" (sem barras pretas com resolução 16:9)
- [x] Câmera 2D (projeção ortográfica, altura visível em unidades do mundo)
- [x] Skybox procedural de 6 lados (montanhas, mar, gradiente, cor sólida), padrão em cena nova
- [x] Opções de split e skybox salvas na cena
- [x] Testes: 131 checks no selftest do editor, 131 testes na engine, ctest 4/4

## Pendente

### Renderização
- [ ] Readback assíncrono com anel de staging (principal custo que sobrou; ver docs/PERFORMANCE_PLAN.md, etapa 2)
- [ ] Apresentação sem cópia para a CPU (zero-copy, etapa 3 do plano)
- [ ] FXAA e nitidez na GPU (etapa 4 do plano)
- [ ] Opção de validação em Settings > Graphics (hoje só variável de ambiente)
- [~] Texturas no OpenGL/GLES: a textura nao chega ao shader (cinza no teste). Investigar o binding de `AlbedoMap` no Diligent GLES; hoje fica desligada com aviso
- [ ] Filtragem bilinear/mipmaps nas texturas (hoje "mais proximo")
- [ ] Espaco de cor sRGB para texturas (hoje os valores sao usados como estao)
- [ ] Shaders por objeto (`.tmat`)
- [x] Skybox com imagens (6 slots, 0.7.7)
- [ ] Cache da imagem do skybox enquanto a câmera não muda
- [ ] Grid e gizmos adaptados à câmera 2D (hoje usam a projeção perspectiva)
- [ ] Culling por oclusão, além do frustum
- [ ] Apresentação direta da GPU sem readback (zero-copy) — maior custo de sincronização restante
- [ ] FSR e outras técnicas (exigem compute e vetores de movimento)
- [ ] Sombras de luz pontual
- [ ] Avaliação das amostras do DirectX (documento de análise)

### 2D
- [ ] Biblioteca de sprites (importar imagens) — agora possivel (texturas prontas)
- [ ] Entidade Sprite
- [ ] Janela Sprite Editor (desativada e bloqueada por padrão)
- [ ] Newtoon com configurações 2D (travar eixo Z, colisores 2D)

### Interface
- [ ] Fontes `.ttf` importadas para o projeto e escolha de fonte da UI
- [ ] Imagens na UI — agora possivel (texturas prontas)
- [ ] Aninhamento de elementos de UI

### Partículas (do documento de referência)
- [~] Estrutura SoA — parcial (listas por emissor); completar quando houver simulação em GPU
- [ ] Simulação em compute shader — depende de compute no RHI e de zero-copy
- [ ] Indirect draw e culling na GPU — depende de compute no RHI
- [ ] Vento por campo de vetores 3D
- [ ] Espirais parametrizadas
- [ ] Emissão por malha (vértices/triângulos)
- [ ] Sub-emitters, fraturamento e áudio espacial (dependem de eventos do Newtoon e de áudio por fonte)

### Núcleo e desempenho
- [ ] Zero-copy de apresentação (ver Renderização)
- [ ] `World::each` sem cópia da lista de índices
- [ ] Índice por tag (`eachTagged`)
- [ ] Hierarquia com muitos filhos (medir com `test ECS: 100000`)
- [ ] Lote de instancing para meshes repetidas
- [ ] Arquétipos/chunks no ECS
- [ ] Um contexto de script por arquivo (hoje um por objeto)

### Projeto e editor
- [ ] Editar nome e autor pela aba Projeto das Configurações
- [ ] Logo da engine na janela de início (o espaço já existe)

## Observações
- As medições de desempenho deste projeto foram feitas em 2 núcleos; os números reais na sua máquina são outros.
- O skybox e a câmera 2D foram verificados por teste e por imagem gerada pela CPU. O caminho da GPU foi verificado sem erros, mas a orientação da imagem na GPU precisa de conferência visual na sua máquina.
