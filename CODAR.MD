# CODAR.MD — tarefas de código pendentes (para resolver no código)

## Revisão de 2026-11-18 (tarde): arquitetura, shaders, API e profiler

Resultado da revisão: o que foi feito e o que ainda falta. Detalhes em docs/ESTADO_ATUAL.md.

### Feito nesta revisão
- P1 (instanciamento): conflito `t15` (Instances x IblBrdfLut) corrigido: Instances agora em `t17`.
- Verificação de bindings sem GPU: `tools/shaders/check_bindings.py` (ctest `Shaders.Bindings`).
- GLSL 430 regenerado: estava desatualizado e sem instanciamento; OpenGL agora lê as instâncias.
- Troca de API: o pedido responde na hora (o seletor já mostra a escolha e o status "Switching to ..."),
  e a troca pesada roda no inicio do quadro seguinte. Pedidos seguidos valem só o último.
- Profiler: seção "Priority" no topo, com draw calls, objetos, triângulos, CPU %, tempo de CPU,
  tempo de GPU (zona), render ms, memória do processo, VRAM e atraso (média, p99, máximo, jitter).
  Essas linhas saem em destaque (fonte maior e negrito simulado). O gráfico de frame time também.

### Teste que o usuário faz (validação)
1. Build do editor e `ctest`: deve dar 100% (32 testes).
2. Clicar em Settings > API grafica: o radio deve mudar na hora, com o status "Switching to ..."; uma troca
   por clique (sem clicar varias vezes). Anote o tempo de troca (a janela pode ficar parada por ~1 s, isso ainda e o passo 2 abaixo).
3. Profiler: confirme que a seção Priority aparece primeiro, com os números em negrito e maiores.
4. Cena com `--ecs 1000 --play` em Vulkan e em OpenGL: a imagem deve ser igual nos dois. Envie prints.
5. D3D12 (Windows): envie o log completo ao iniciar com D3D12. Procure "compil", "shader", "dxcompiler", "TOOL-E152".

### T7. D3D12 não funciona: diagnóstico (prioridade alta)
- Causa provável: o backend não escolhe o compilador (`SHADER_COMPILER_DEFAULT`) e o DXC
  (`dxcompiler.dll`) pode não estar junto do executável. O erro aparece só como "falha ao compilar shader".
- Passos: (a) em `createShader` (diligent_device.cpp), capturar `ppCompilerOutput` do ShaderCreateInfo
  e registrar o texto no debug(); (b) confirmar a versão vendorizada do Diligent antes de usar o campo;
  (c) no CMake, copiar `dxcompiler.dll` e `dxil.dll` para o diretório do executável quando D3D12 estiver ligado.
- Critério: o log do D3D12 mostra o erro exato do compilador (ou o shader compila).
- Não verificado aqui: não há Diligent compilado neste ambiente nem Windows.

### T8. Shaders nativos em HLSL e GLSL (sem tradução) — decisão de arquitetura
- Hoje: o GLSL é gerado do HLSL (glslang + spirv-cross) e tem ajustes manuais (inversão de Y, renomeação
  de texturas). Isso e tradução, e foi a fonte de problemas.
- Proposta: `assets/shaders/hlsl/` (D3D11, D3D12, Vulkan) e `assets/shaders/glsl/` (OpenGL 430), escritos à mão.
  Mesmo contrato de nomes e registradores nos dois. O `check_bindings.py` passa a comparar os dois.
  O `build_glsl.py` vira ferramenta de comparação, não de produção.
- Passos: (1) criar a pasta glsl nativa com pbr, prepass, shadow, shadow_point, ssao, bloom, spriteui;
  (2) fazer o RHI escolher a pasta pelo backend (`ShaderDesc::lang` já existe); (3) testar cada API.
- Critério: `check_bindings.py` passa nas duas pastas; a imagem é igual em OpenGL e Vulkan.

### T9. Troca de API sem travar (fase 2)
- Hoje a troca roda num quadro só (destruir dispositivo, compilar shaders, criar pipelines). Isso ainda trava
  a janela por um tempo.
- Passo: transformar `applyApi` em máquina de estados que avança um passo por quadro
  (encerrar, criar dispositivo, criar shaders, criar pipelines), com o aviso "Trocando..." em cada passo.

### T10. Medição de GPU no profiler
- "GPU %" não é medido. É preciso consultas de tempo da GPU (timestamp queries) no RHI. A linha "GPU zone ms"
  mede só o tempo de CPU gasto na zona de GPU.

### Enxugamento e limpeza
- Ver a seção "Enxugamento do projeto" abaixo (aguardando aprovação).

---


## Estado P1 - Instanciamento no renderizador (2026-11-18)

- Fases 1a-1d: implementadas (binding VS/PS, PBR instanciado, passe opaco, prepass SSAO e sombras por lote).
- Transparências e bloom permanecem por objeto, conforme o escopo do P1.
- Verificação automatizada anterior: build CPU+GPU concluído; `tool_tests` passou com 197 testes; Vulkan passou. Os dois testes OpenGL não executaram porque `xvfb-run` não estava disponível.
- Pendente: validação visual em cena real e benchmark; executar `--ecs 1000 --play`, comparar imagem, e registrar `DrawStats`, CSV e log Vulkan.

## Estado da tarefa - Export Data do Profiler

- Implementado na janela Profiler: botão `Export data` registra cinco snapshots, um por segundo, e salva todas as linhas e os históricos dos gráficos em Markdown no diretório de trabalho atual.
- O Profiler informa progresso, caminho salvo ou erro de gravação. A coleta continua se a janela for fechada durante os cinco segundos.
- Builds atualizados CPU e GPU aprovados; `Editor.ProfilerModel` e `Editor.ProfilerExport` passaram nas duas configurações.
- Teste automatizado de captura, cadência e formatação Markdown: 33 verificações aprovadas.
- Pendente: testar a interação visual no editor e confirmar que o arquivo exportado abre corretamente em uso real.

Este documento lista o que falta na engine para a interface ficar completa.
Cada tarefa tem: contexto, arquivos, passos e critério de aceite. Idioma padrão da engine: inglês.

Estado de referência: branch `ui-integrado` (engine) e `sui-integrado` (SpriteUI).

## Estado atual (atualizado em 2026-11-18)

**Nada foi apagado do que estava pronto.** Conferido no git: nenhum controle do ImGui foi removido de
`apps/editor` em relacao ao `origin/main` do usuario, e nenhum arquivo do editor foi apagado nos commits.
As opcoes de tema continuam no codigo: `drawAppearanceTab` em `main.cpp` (seletor de tema, temas do usuario,
cores base, forma e tamanho) e os presets salvos em `ui_presets.hpp`. O que mudou: a reescrita do Settings
(`ui-settings-wip`) ainda nao esta ligada ao editor, e so portou essas opcoes como dados.

**O que nao esta pronto e por que:**
- Settings em SpriteUI: o Editor continua usando a janela ImGui em `main.cpp`; `settings_tabs.hpp` e
  `settings_editor.hpp` nao existem no checkout atual. `settings_bridge.hpp` e seu teste existem e seguem usados.
- Janelas do ImGui: 1176 chamadas na busca desta revisao. Nao da para trocar tudo de uma vez sem ver a tela.
- Plugin de interface na GPU para o jogo: falta validacao com placa de video.
- Melhoria de desempenho do SpriteUI (instancias em vez de vertices): falta teste de pixel na GPU.
- Bloco `engine` do `project.json`: gravado e lido, mas a janela ainda nao o aplica.

**Implementacao iniciada nesta revisao:**
- `Splitter.cpp`, `DockGuides.cpp` e `UIScale.cpp` agora fazem parte da biblioteca SpriteUI, com alvos de teste no CMake.
- Guias de docking desenham cinco pictogramas com destaque de cor/opacidade; dimensoes pequenas e estilos invalidos
  nao geram geometrias fora da folha.
- Modelos de icones e tokens ganharam testes; indices de cor fora do enum nao acessam arrays fora dos limites.
  A escala minima dos tokens foi alinhada ao Settings atual (70%).
- Na validacao inicial desta revisao, builds `tool_editor` CPU e GPU concluiram e cinco testes focados (`UiIcons`, `UiTokens`, `SpriteUiSplitter`,
  `SpriteUiDockGuides`, `SpriteUiUiScale`) passaram nas duas configuracoes.
- Tokens, icones, Splitter, guias, escala e cache de texto ainda nao estao ligados visualmente aos paineis ativos.
  Persistencia/ColorPicker de tokens, integracao do cache aos paineis ativos e teste visual continuam pendentes.
  O benchmark SpriteUI foi executado em Release; a referencia atual e o resultado, sem comparacao historica, estao em
  `docs/IMPLEMENTATION_STATUS.md`.
- Grid, eixos e marcadores X/Y/Z do Game convertidos para geometria 3D transparente nos renderizadores CPU e GPU;
  a profundidade oculta esses elementos atras dos objetos. As duas rotas do Game informam o slot correto para cameras divididas.
- Faixa de abas do Settings elevada para a camada Docked, preservando a ordem interna de pintura.
- Builds do Editor CPU e GPU aprovados; 10 testes focados passaram em ambas as configuracoes
  (GameGrid, SettingsBridge, SpriteUI nativo, camadas/entrada, cache de texto, Splitter, DockGuides, UIScale,
  UiIcons e UiTokens).
- Suite completa sequencial: CPU 30/30 aprovados; GPU 32 aprovados e dois testes OpenGL nao executados porque
  `xvfb-run` nao esta disponivel. O teste GameGrid, incluindo a comparacao de pixels sob um objeto opaco, passou
  novamente nas duas configuracoes.
- Teste visual manual ainda pendente: conferir grid/eixos atras de objetos no Game nas duas APIs, inclusive com
  cameras divididas; e confirmar que a faixa de abas do Settings fica visivel sobre os demais elementos.
- Nenhum arquivo foi apagado nesta revisao.

**Commits relevantes:** `ui-integrado`: 73bb6c6 (driver de render), aae4527 (Help SpriteUI), e537377 (analise),
517ec74 (bloco engine no project.json), aaa68d1 (lista do ImGui). `ui-settings-wip`: eac213e.
`ui-export-wip`: dfd1f5b (modelos de Informacoes do Editor e Exportar, preservados).

## T0. Metodo de migracao: uma janela por vez (decisao do usuario)

Nao apagar a janela do ImGui antes de a versao SpriteUI estar testada. Para cada janela da lista T3:
1. Criar a versao SpriteUI ao lado da do ImGui, com a mesma lista de controles (checklist abaixo).
2. Deixar as duas abertas por um tempo, com uma opcao para escolher qual usar.
3. O usuario faz os testes da secao "Testes que o usuario faz" e marca cada item.
4. So depois de tudo marcado, apagar a janela do ImGui e a opcao.

**Checklist de paridade (por janela):** cada botao, caixa, deslizante, campo e menu da janela ImGui tem
um equivalente SpriteUI com o mesmo nome e o mesmo resultado. Nada de "vazio" ou placeholder.

## Testes que o usuario faz (para eu validar)

Rodar no seu computador com placa de video. Para cada item, diga "ok" ou descreva o problema com a captura de tela.

1. **Build e testes automatizados**
   - `cmake --build <pasta-de-build> --target tool_editor -j2` deve terminar sem erro.
   - Atualmente, o CTest registra 30 testes CPU e 34 GPU. A suite CPU passou completa; na GPU, 32 passaram e dois
     testes OpenGL nao executaram por falta do wrapper `xvfb-run`.
2. **Escala e texto**
   - Abrir o editor: a escala deve estar em 125% e o texto nitido (sem serrilhado).
   - Idioma padrao: ingles.
3. **Cliques**
   - Cada botao deve responder ao primeiro clique, sem precisar de varios.
4. **Janelas e docking**
   - Arrastar uma janela: ela fica sempre por cima das outras.
   - Clicar no titulo sem arrastar: a janela nao sai do encaixe.
   - Os icones de encaixe aparecem semitransparentes ao arrastar.
   - Divisor da Hierarquia (borda direita e esquerda): deve redimensionar nos dois sentidos.
5. **Menus**
   - Menu Janela: caixas de selecao e icones, nada de `[x]` em texto.
6. **Configuracoes (quando a janela SpriteUI estiver pronta)**
   - Os icones das abas aparecem com dica ao passar o mouse: pincel (Aparencia), engrenagem (Configuracoes),
     controle (Jogo), lanterna (Iluminacao), blocos (Otimizacao) e G (Idioma).
   - Trocar uma cor no tema: a janela muda na hora. Fechar e abrir de novo: a cor continua.
   - Trocar o tema inteiro e salvar um tema do usuario: ele aparece na lista.
7. **Render (GPU)**
   - Trocar a API grafica (Software, Vulkan, OpenGL, D3D12, D3D11): a Cena e o Jogo nao podem ficar pretos.
   - No log, a linha "Interface SpriteUI: driver de render ..." deve mostrar o driver escolhido.
8. **Projeto**
   - Salvar o projeto e abrir o `Project/project.json`: deve existir o bloco `engine` com idioma, escala, renderer, API, render scale e FPS.
9. **Desempenho**
   - Anotar o tempo de quadro no Profiler com a interface parada e com ela mudando (ms por quadro). Isso
     decide a etapa de desempenho do SpriteUI.


---

## T1. Terminar a janela Settings (estado parado)

**Contexto.** O painel de Settings precisa migrar para uma barra de ícones com tooltip
(pincel = Appearance, engrenagem = Settings, controle = Game, lanterna = Lighting,
blocos = Optimizing, G = Language). `settings_model.hpp`, `settings_panel.hpp` e `settings_changes.hpp`
existem; `settings_tabs.hpp` e `settings_editor.hpp` nao existem no checkout atual.

**Estado.** `settings_bridge.hpp` e `tests/test_settings_bridge.cpp` existem e estao ligados ao Editor/CMake.
O Settings ativo ainda e o ImGui em `main.cpp`; a nova interface SpriteUI e a persistencia dela nao foram integradas.
Nao remover a ponte enquanto houver consumidores.

**Passos.**
1. Resolver a pendência acima e compilar `tool_editor`.
2. Aba Optimizing (nova): limite de quadros, cache de malhas (`chunkCacheMax`), escala de
   interface, opções de desempenho. Cada opção ligada ao modelo, com teste.
3. Conferir que nenhuma opção antiga sumiu: comparar com a função de Settings antiga em
   `main.cpp` (linha ~1966 no código do usuário) e com `settings_model.hpp`.

**Aceite.** Todas as abas abrem; cada opção muda o modelo e persiste em `settings.json`;
`ctest` verde.

## T2. Aparência: cores e formatos do tema

**Contexto.** O usuário quer personalizar as cores de cada parte da engine.
Já existem tokens de cor e de forma (commit `860f0c8`).

**Passos.**
1. Na aba Appearance, listar os tokens por grupo (janela, título, botão, campo, menu, seleção).
2. Cada token abre o seletor de cor da SpriteUI (`ColorPicker`), com HEX e alfa.
3. Formatos: arredondamento de canto, espaçamento, altura de botão (padrão 28 px) e escala (125%).
4. Botões "Restaurar padrão" por grupo e global.

**Aceite.** Mudar um token muda a janela na hora; o tema salvo volta ao reabrir.

## T3. Janelas ainda em ImGui

Contagem atual de chamadas `ImGui::` em `apps/editor`: 1176 (busca nesta revisao).
Por arquivo: `extra_ui.hpp` 235, `assets_panel.hpp` 139, `project_dialog.hpp` 40,
`export_ui.hpp` 40, `script_window.hpp` 37, `narrative_panel.hpp` 54 (por último), `fx.hpp` 15,
`editor_scripts.hpp` 4, `theme.hpp` 5, `ui_contract.hpp` 4, `viewport_tools.hpp` 3,
`view_shapes.hpp` 1, `ui_test.cpp` 8, `main.cpp` 591.

Ordem sugerida:
1. **Assets e Scenes** (`assets_panel.hpp`): árvore de pastas, importação, arrastar e soltar,
   menus de contexto, editor de texto (`Edit file`) e modais (renomear, excluir, nova pasta,
   novo texto, nova cena, renomear cena, excluir cena). Modelo puro + painel SpriteUI.
2. **Export e Editor info** (`export_ui.hpp`): a lógica de pacote continua na engine; o painel
   só mostra e pede.
3. **Input Map** (`extra_ui.hpp`, função com `Input Map`): lista de ações, captura de tecla,
   conflitos.
4. **Script** (`script_window.hpp`): usar `TextArea` da SpriteUI.
5. **Caixa de projeto** (`project_dialog.hpp`): remover o fallback ImGui se a tela de início
   SpriteUI (`start_screen_spriteui.hpp`) cobrir o caso.
6. **Profiler, Console, Inspector, Scene, Game** (`main.cpp`): remover os placeholders
   `ImGui::Begin` que servem de docking. O docking passa a ser o da SpriteUI (`Docking.hpp`).
7. **Narrative** (`narrative_panel.hpp`, ImNodes): por último. Precisa de editor de grafo
   nativo na SpriteUI.

**Aceite de cada item.** Modelo testado no `ctest`; janela abre pelo menu Janela; sem
`ImGui::` no arquivo; `tool_editor` compila.

**Ao final.** Quando `grep -rn "ImGui::" apps/editor` retornar zero, remover ImGui do CMake
(`imgui_lib`, `ImGui_ImplSDL3`, `ImNodes`) e do `third_party`.

## T4. Ajustes de janela e docking (usuário relatou)

- Janela arrastada sempre na frente de todas (camada de arrasto).
- Ícones de encaixe semitransparentes ao arrastar.
- Clicar no título sem arrastar não tira a janela do encaixe (limiar de 6 px).
- Redimensionar o divisor nos dois sentidos (ex.: Hierarchy, borda esquerda).
- Campos de marcação: usar `CheckBox`, `Toggle` e `RadioGroup` da SpriteUI, não quadrados.
- Os menus usam caixa de seleção e ícone; nada de `[x]` em texto.

**Aceite.** Cada item tem teste automatizado na SpriteUI ou no editor, e captura de tela.

## T5. Desempenho da interface

- Medir com `benchmarks/spriteui_frame_benchmark.cpp` (200 botões, 20 janelas).
- Cache de layout de texto (`TextLayoutCache.hpp`) já existe; aplicar também aos painéis do editor.
- Evitar `std::string` temporário no laço de desenho.

**Estado.** Referencia atual registrada em `docs/IMPLEMENTATION_STATUS.md` (Release, 1000 quadros). O benchmark ja usa
cache de layout de texto, mas ainda nao mede uma versao anterior nem o Editor com o cache integrado.

**Aceite.** Aplicar o cache aos paineis ativos e registrar uma comparacao antes/depois, mantendo a mesma configuracao
do benchmark em `docs/IMPLEMENTATION_STATUS.md`.

## T6. Segurança e críticas pendentes (auditoria)

- **F-09**: mover implementação pesada de headers para `.cpp`, um módulo por vez (render primeiro).
- **F-11**: decidir política de dependências (vendor completo ou FetchContent com versão fixa).
- **F-14**: versão única. Hoje: CMake `0.1.0`, README/CHANGELOG `0.7.19-beta`, guias `0.2.3-beta`.
- **F-15**: benchmarks de performance com critério de aceite.

---

## Enxugamento do projeto (aprovar antes de apagar)

Itens que parecem lixo ou duplicados. Nenhum foi apagado ainda:

- `Projeto Final/` — cópia de assets e um binário ELF de ~10 MB. Remover do pacote.
- `out/` — saída de build. Não deve ir no zip.
- `build_log.txt`, `git add.txt` — logs e rascunhos.
- `COMO_COMPILAR.txt` e `HOW_TO_BUILD.txt` — duplicados; manter um só (sugestão: `docs/BUILD.md`).
- `LEIA-ME-THIRDPARTY.txt` — integrar ao `docs/BUILD.md`.
- `third_party/` — 49 MB. SDL3, ImGui, imnodes, miniaudio e quickjs não precisam ir no zip.
  Manter `third_party/SpriteUi` e `third_party/LightPrism`.
- Documentos de revisão antigos (`docs/REVIEW_V4_2.md`, `docs/V4_2_IMPLEMENTATION_MAP.md`):
  marcar como histórico ou remover.
