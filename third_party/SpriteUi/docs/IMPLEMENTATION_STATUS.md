# Estado da implementação — SpriteUI 0.6.0 (verificado em 09/11/2026)

O CMake declara a versão 0.6.0; isso não significa que uma release esteja publicada. O núcleo C++20 segue independente de ECS/RHI. A arquitetura planejada não deve ser confundida com integrações já entregues.

## Correções de Formatação, Estrutura e Insuficiência de Código

- **Padronização de Formato:** Os arquivos `Layout.hpp`, `Layout.cpp`, `LayoutTree.hpp` e `LayoutTree.cpp`, que estavam compactados/sem formatação legível, foram integralmente reformatados para o padrão C++20 da biblioteca, com tipagem estrita (`std::uint8_t`, `std::uint32_t`), indentação limpa de 4 espaços e documentação clara.
- **Eliminação de Avisos de Compilação:** Resolução de todos os avisos C4100 (parâmetros não referenciados) e C4834 (`[[nodiscard]]` descartado), garantindo compilação limpa em nível `/W4 /permissive-`.
- **Limpeza de Diretórios Vazios:** O diretório órfão `src/Core` foi removido.
- **Expansão de Flexbox:** `JustifyContent`, wrap, `AlignContent`, baseline e redistribuição com limites min/max estão implementados.

## Módulos Implementados

- **ABI e GPU:**
  - ABI `SpriteInstanceGPU` de 80 bytes, alinhamento 16 e verificações de offsets/tipos por `static_assert`.
  - `UIMaterialGPU` base e configuração de Canvas Screen/World com validação do modo de iluminação.
  - Render extraction com ordenação visual estável, batches apenas consecutivos, clip rects e ABI de 80 bytes.
  - Validador de `FramePacket`; `NullRenderBackend` para execução e validação sem GPU.
  - Shader HLSL (`SpriteUI.hlsl`) consulta dimensões do atlas para normalizar `pxRange` em pixels de tela e calcula cobertura MSDF a partir de `fwidth`; compilação DXIL ativa. Paridade visual em GPU real continua pendente.

- **Layout e Cena:**
  - Flexbox com Row/Column, Fixed/Percent/Auto/Flex, grow/shrink, gap, padding, margens, min/max, `JustifyContent`, wrap, `AlignContent` e baseline.
  - `LayoutTree` com relações explícitas de filhos, validação de handles e remoção recursiva de subárvores.
  - Alinhamento transversal Stretch/Start/Center/End.
  - `BuildClipHierarchy` com interseção hierárquica pai/filho.
  - `LayoutSceneBridge`: conversão automática da `LayoutTree` em lista plana de `HitTarget`, `UIRenderItem` e hierarquia de `ClipNode`.

- **Widgets e Controle de Janela:**
  - `UISliderComponent` (domínio numérico, step e proteção contra largura zero).
  - `UIScrollViewComponent` (`ClampScroll` por `contentSize - viewportSize`).
  - `UIToggleComponent` (ligado/desligado e desabilitado).
  - `UITextInputComponent` (foco, cursor de cluster, seleção e temporizador de cursor piscante).
  - `GetResizeEdge` (detecção de 8 bordas: Left, Right, Top, Bottom e 4 cantos).
  - `UIWindowComponent` e `ProcessWindowInteraction` (arrasto por titlebar, redimensionamento nas 8 bordas respeitando `minSize` e captura no roteador).

- **Novos Módulos Implementados (Nesta Etapa):**
  - **Virtualização de Listas (`UIVirtualList`):** Algoritmo O(1) de cálculo de faixa visível `[firstIndex, lastIndex]` com overscan, viabilizando tabelas com centenas de milhares de linhas (Seção 10).
  - **Abas e Docking Estrutural (`UITabGroupComponent` e `UIDockNode`):** Gestão de abas ativas/inativas e árvore binária de divisão de dockspace com splits Left, Right, Top, Bottom e Center (Seção 24 e 25.2).
  - **Data Binding Reativo MVVM (`UIValueBinding`):** `UIFloatBinding`, `UIIntBinding`, `UIBoolBinding` e `UIStringBinding` com dirty check por limiar numérico (Seção 28 e Apêndice F.3).
  - **Gráficos Analíticos e Telemetria (`UIOscilloscopeGraph` e `UILinePlot`):** Ring buffer circular de 256 amostras com O(1) de inserção, extração cronológica ordenada e normalização [0, 1] para plotagem de frametimes e frequências (Seção 29 e 30.1).
  - **Gerenciador de Popups (`UIPopupManager`):** Rastreio de menus de contexto e dropdowns, com fechamento automático em cliques fora da barreira AABB (Seção 25.7).

- **Tipografia e Texto:**
  - `GlyphInstance`, `FontMetrics`, `LayoutText` (quads, quebra por palavra no subconjunto implementado, alinhamento), `GetCaretRect`, `FindInsertionOffset`, `FindClusterAtPosition` e `GenerateTextRenderItems`.
  - `GenerateTextRenderItems` aceita flags de cobertura MSDF (padrão compatível) ou bitmap em tons de cinza; o shader e o rasterizador de referência CPU distinguem os dois modos.
  - O visualizador opcional carrega Xmind Regular como fonte primária, gera um atlas Latin-1 de cobertura com métricas da fonte e aceita outro arquivo TTF/OTF por `--font`. Essa rasterização do visualizador usa GDI no Windows; não substitui HarfBuzz/FreeType, shaping complexo, bidi/RTL ou atlas MSDF de produção.
  - HarfBuzz/FreeType e bidi/RTL não estão integrados ao core.
- **Renderização auxiliar:** Nine-Slice e painéis SDF arredondados; compilação DXIL do shader verificada localmente, sem validação em GPU real.

- **Persistência Segura (JSON):**
  - `UIConfigManager` com validação de `schemaVersion`, sanitização numérica por `ReadFiniteFloat`, staging atômico e commit transacional.

- **Verificação local (MSVC/Windows):**
  - Builds Debug e Release, `spriteui_check_headers`, CTest (core + instalação/consumidor) e exemplos CLI passaram.
  - `spriteui_tests.exe` reportou 819 checks aprovados em Debug após as implementações desta rodada; o CTest passou 5/5 com SDL3 3.2.30.
  - DXC compilou os shaders VS/PS para DXIL.
  - Clang/GCC, sanitizers, CI e GPU real não foram verificados nesta rodada.
- **Preparação para release adicionada:** workflow CI para MSVC/GCC/Clang e ASan/UBSan; teste CTest de instalação/consumidor `find_package`; benchmark CPU opcional de layout e render extraction. O smoke test de pacote e benchmark foram executados localmente; o workflow remoto ainda precisa executar com sucesso no GitHub Actions.
- **Documentação de adoção:** README com quick start e screenshots de software; `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, notas de thread-safety, `docs/API_REFERENCE.md` e `docs/TOOLENGINE_INTEGRATION_GUIDE.md` adicionados. A geração HTML por Doxygen é opcional e não verificada porque a ferramenta não está instalada. O guia separa APIs existentes dos adaptadores que dependem do checkout da ToolEngine.
- **Integração opcional SDL3:** wrapper de janela com ownership RAII e criação/configuração básica; adaptador de mouse/foco, navegação semântica por teclado/gamepad, entrada de texto/composição IME e clipboard UTF-8 global. Testes sintéticos, checagem de headers e smoke test do target instalado aprovados localmente com SDL 3.2.30. APIs SDL exigem thread principal; clipboard também requer suporte do backend. O workflow usa essa versão fixada; execução remota ainda não confirmada.
- **Visualizador interativo opcional:** `spriteui_visualizer` mostra uma cena demonstrativa numa janela SDL3, usando `RenderExtractor` e o rasterizador CPU de referência já exercitado pelos testes. Usa Xmind Regular como fonte principal, aceita outra fonte TTF/OTF com `--font`, e aplica estilos de texto predefinidos com tamanhos e margens consistentes para título, subtítulo, corpo, rótulo, legenda, métrica e rodapé. Permite trocar paleta, ativar toggle, arrastar slider, selecionar abas, experimentar uma aba selecionada com gradiente horizontal de duas cores, ativar botões flutuantes e animar o fade de um botão via `ToonMotionSystem`; também salva PNG com `S`. O campo lateral inicia com “god only knows”, recebe texto UTF-8 por eventos SDL, permite mover o cursor e apagar por codepoint; Enter envia o valor atual ao console da aplicação. CTest headless exercita inserção, readback, backspace UTF-8 e presença visual do campo. Build MSVC, CTest (5/5) e inspeção da captura foram aprovados nesta rodada. A fonte é rasterizada por GDI no Windows para um atlas Latin-1 em tons de cinza; isso verifica layout e cobertura bitmap, não shaping complexo, MSDF de produção ou GPU.
- **Ícones e opacidade:** `Rendering::UIIcon` descreve tamanho de origem, slice e região UV normalizada de um asset já inserido num atlas; `MakeIconRenderItem` produz um quad renderizável. Testes validam UV/slice/bounds e extração de opacidade parcial, clamp e rejeição de NaN. Não há decodificação de SVG/PNG no core: rasterização/importação, atlas, DPI e lifetime de recursos permanecem no pipeline de assets/host. A opacidade escala tint RGB e alpha juntos durante extração, sem alterar a ABI GPU de 80 bytes.
- **Imagem no visualizador:** `fotos/LOGO.png` é carregada por WIC e apresentada num cartão de anúncio através de `UIIcon` e do caminho real CPU de extração/renderização. `--image` permite apontar outro PNG; o teste headless confirma pixels claros na região do logo. WIC/GDI são dependências apenas do alvo opcional Windows, não do core.
- **Timeline de keyframes:** `Animation::ToonTimeline` valida tracks e ordenação/valores dos keyframes e amostra `Vec4` com easing por segmento, com clamp antes/depois da faixa. `ToonTimelinePlayer` implementa play/pause/stop, seek e loop usando delta fornecido pelo host; relógio, editor visual, persistência e escrita no ECS permanecem responsabilidades do host. Testes cobrem amostragem, limites, easing, tempo inválido, looping, pausa, seek, parada e remoção/duração.
- **Input multi-pointer:** `Input::UIMultiPointerEventRouter` mantém hover/capture/foco por ponteiro, valida snapshots duplicados ou de frame inconsistente e cancela/limpa ponteiros ausentes de modo determinístico. O adaptador SDL3 traduz eventos finger down/motion/up/cancel em coordenadas lógicas, gera IDs por contato, filtra mouse sintetizado por touch e expõe `BuildPointerSnapshots`; `BuildSnapshot` segue compatível e retorna apenas mouse. Testes cobrem dois contatos, posição/delta, release, cancelamento e fim de contato. Foco de teclado entre ponteiros requer política do host.
- **Refatoração de navegação:** validação de candidatos focáveis agora é compartilhada pelo cálculo de vizinhos espaciais e pelo roteador semântico, evitando divergência entre os dois caminhos; comportamento público preservado.
- **Referência de API:** `docs/API_REFERENCE.md` documenta módulos, ciclo de frame, alpha, animação e limites. CMake disponibiliza alvo opcional `spriteui_docs` com Doxygen; o executável Doxygen não está instalado neste ambiente, então a geração HTML não foi validada.
- **Independência de ImGui e ToolEngine:** o CMake, os headers e o código-fonte do SpriteUI não dependem de ImGui. A remoção do ImGui do editor da ToolEngine ainda não começou. A decisão é migrar uma tela por vez, começando pelo Console, com fallback temporário para telas não migradas; reutilizar o ECS, a janela/input SDL3 e o RHI existentes na engine; remover a dependência do ImGui do host somente após testes de paridade e regressão passarem.
- **Limpeza do workspace:** removidos os diretórios locais ignorados `build`, `build-sdl3`, `build-release` e `snapshots`, incluindo o SDK SDL temporário e artefatos regeneráveis (aprox. 312 MiB). Nenhum arquivo versionado de fonte, teste, imagem ou documentação foi removido; `.cache/clangd` foi preservado.
- **Varredura do shader MSDF:** corrigida a conversão de `FontMetrics::pxRange` (texels do atlas) para pixels de tela, que antes não considerava as dimensões da textura. Os shaders VS/PS recompilaram para DXIL e o CTest passou (2/2) após a correção.

## Pendências para integração e produção

| Módulo | Estado |
|---|---|
| Adaptador para ECS próprio da ToolEngine | Pendente; a integração não deve adicionar Flecs |
| Backend de renderização via RHI da ToolEngine | Pendente; a interface atual não cobre diretamente todas as necessidades do shader/ABI SpriteUI (instâncias no vertex stage, textura-array/sampler e base-instance) |
| Integração do editor ToolEngine sem dependência final de ImGui | Não iniciada; migração gradual planejada, com remoção do ImGui após paridade e regressão |
| Adaptador SDL3 standalone: janela, mouse/foco, navegação teclado/gamepad, texto/IME e clipboard | Implementado e validado localmente com SDL 3.2.30; execução CI remota pendente; para ToolEngine, reutilizar `tool::IWindow` e encaminhar eventos/snapshot sem um segundo event loop |
| Visualizador de qualidade de interface em janela | Abas selecionáveis, gradiente demonstrativo, botões flutuantes e fade via ToonMotion; build MSVC, CTest 5/5 e captura headless aprovados; desktop real/GPU fora do escopo |
| Multi-pointer routing no core | Implementado e coberto por teste com dois ponteiros simultâneos, click, cancelamento e entrada inválida |
| SDL3 touch/finger e snapshots multi-pointer | Implementado e testado com SDL 3.2.30 (down/motion/up/cancel e dois contatos simultâneos) |
| Adaptador GLFW | Pendente |
| HarfBuzz/FreeType, atlas MSDF dinâmico e suporte bidi/RTL | Pendente |
| Widgets visuais integrados e drag-and-drop de docking | Pendente |
| Light Prism ClusterLit, normal mapping, Acrylic Blur, Bloom e sombras | Pendente |
| KTX2/Basis, streaming e eviction de recursos por fences | Pendente |
| Amostragem de timeline/keyframes | Implementada como `ToonTimeline` stateless; build/teste verificado nesta rodada |
| Playback/looping/seek de timeline | Implementado por `ToonTimelinePlayer`, controlado por delta do host; editor visual, persistência e integração ECS continuam pendentes |
| Scheduler on-demand baseado em dirty flags e orçamento de desempenho em engine hospedeira | Pendente |
| QuickJS sandbox/FFI, partículas e nodes de editor | Pendente |
| CI configurado para MSVC/Clang/GCC e job ASan/UBSan | Implementado; falta observar execução remota em GitHub Actions |
| Fuzz contínuo e cobertura | Fuzz target libFuzzer para parser JSON com ASan/UBSan e corpus-semente adicionado à CI Clang (30 s/execução); cobertura total ainda não configurada |
| Referência de API | Página de entrada implementada em `docs/API_REFERENCE.md`; geração HTML opcional via Doxygen configurada, não verificada (Doxygen indisponível neste ambiente) |
| Guia de integração na ToolEngine | Implementado em `docs/TOOLENGINE_INTEGRATION_GUIDE.md`; detalhes de APIs e adaptadores ainda dependem do checkout da ToolEngine |
| Benchmarks CPU de layout e render extraction | Implementado como alvo opcional e executado localmente; faltam baseline comparável e orçamento por plataforma |
| Teste de instalação/consumidor com `find_package` | Implementado como teste CTest e aprovado localmente em MSVC Debug e Release; falta validar em outras plataformas/toolchains |
| Validação em GPU real | Pendente |

### Próximas pendências ordenadas por dificuldade/dependência

| Faixa | Próximo item | Bloqueio / critério para avançar |
|---|---|---|
| Baixa | Gerar/inspecionar HTML da API | Doxygen não está instalado; instalar no ambiente de docs e validar o target `spriteui_docs` |
| Média | Adaptador GLFW e medição de cobertura | Escolher e testar a plataforma/toolchain; fuzzing do parser JSON agora roda na CI Clang; SDL touch e routing core já estão implementados |
| Alta | HarfBuzz/FreeType e atlas MSDF de produção; importação SVG e atlas dinâmico | Escolher/licenciar dependências, implementar shaping/atlas real e ampliar a matriz de idioma/DPI; não confundir o rasterizador GDI do visualizador com produção |
| Alta | Backend GPU e paridade de shader | Precisa de uma implementação RHI suportada e testes em GPU; não criar backend paralelo para afirmar integração à ToolEngine |
| Dependente da engine | ECS da ToolEngine, migração ImGui, Light Prism, fences/KTX2 e QuickJS/FFI | Requer interfaces/checkouts, ownership e políticas do host que não existem neste repositório |

Foram concluídos nesta continuação itens independentes de baixa/média dependência: logo PNG no
visualizador, API reference de entrada, keyframe sampling e playback com loop/seek, roteador
multi-pointer do core, tradução SDL3 de eventos touch e fuzzing libFuzzer do parser JSON configurado
na CI Clang. A execução do fuzz depende da CI; cobertura total, demais plataformas e itens restantes
permanecem explícitos para não declarar integração sem backend, dependências ou código da engine.
