# Checklist do ToolEngine

Legenda: [x] feito · [~] parcial · [ ] pendente. Versões em `docs/CHANGELOG.md`.

## Feito

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
