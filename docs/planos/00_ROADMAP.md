# Roteiro consolidado do ToolEngine

Ultima atualizacao: 2026-11-22
Versao de referencia: `0.7.19-beta (em andamento)`
Branch de trabalho nesta revisao: `main`

Este documento e a fonte resumida para decidir o que fazer a seguir. Ele consolida os antigos documentos
operacionais de tarefas (`CODAR.MD`, `ESTADO_ATUAL.md`, `P1_IMPLEMENTACAO.md`, removidos no commit `908cbaf`),
o plano de P1 a P6 e os proximos passos. Os detalhes de cada versao ficam em `../referencia/CHANGELOG.md`, e as
verificacoes detalhadas em `00_CHECKLIST.md`.

Em caso de contradicao, esta pagina define a prioridade. O changelog define o que entrou em cada versao.

## Legenda de estado

| Marca | Significado |
|---|---|
| **[F]** | Feito e verificado por teste automatico nesta maquina |
| **[V]** | Feito no codigo; falta teste visual, em GPU real ou no Windows (registrar em `00_CHECKLIST.md`) |
| **[P]** | Parcial: parte feita e parte pendente, listado no item |
| **[ ]** | Pendente |
| **[X]** | Adiado ou descartado nesta versao, com motivo |

Regra: nenhum item vira **[F]** por texto. So testes automatizados ou observacao registrada no `00_CHECKLIST.md`.

---

## 0. Resumo: o que importa agora

1. **Interface sem falhas de clique e de camada.** Os quatro problemas relatados (arraste fora do dock,
   clique atravessando menus, barra de titulo atras de outros paineis, falta de camada numerica) tem a
   regra pronta na SpriteUI. Falta ligar os paineis do editor a ela e validar em janela real. [V]
2. **Remover o ImGui em etapas**, comecando pelas Configuracoes. A remocao total em uma etapa so depois
   de todas as janelas terem paridade testada (secao 2, Fase C).
3. **Estabilidade do teste ECS.** Guarda de memoria e captura de erro estao no codigo. Falta reproduzir o
   caso real com janela. [V]
4. **Fechar a base verificada**: P1 visual, D3D12 (diagnostico), T8 a T10 (shaders, troca de API, timestamps).
5. **Verificacao honesta**: esta maquina nao tem tela nem GPU real. Tudo que depende de aparencia ou de
   GPU fica como [V] ate o teste registrado.

---

## 1. Estado da versao

### 1.1 Funcionando e integrado [F]

- Core C++20: ECS, scene, eventos, assets, physics, audio, narrative, modding e scripting estatico.
- Renderizacao CPU e GPU por Diligent: Software, Vulkan e OpenGL; D3D11/D3D12 existem como caminhos no codigo.
- Light Prism integrada como iluminacao CPU/GPU, com SSAO, sombras CSM e de luz pontual/spot.
- Editor com Hierarchy, Inspector, Scene, Game, Console, Profiler, Assets e tela inicial SpriteUI.
- Instanciamento P1 implementado no caminho principal de render, prepass, SSAO e sombras.
- Shaders: `Instances` movido de `t15` para `t17` (conflito com `IblBrdfLut`); GLSL regenerado a partir do HLSL;
  `tools/shaders/check_bindings.py` roda no ctest como `Shaders.Bindings` e acusa registrador ou binding repetido.
- Exportacao Windows x64 e Linux x64, modo jogador, projetos em pasta propria e captura de dados do Profiler.
- Texturas, emissao, transparencia por objeto, neblina, particulas, chunks opcionais e multi-material.
- SpriteUI (vendorizada em `third_party/SpriteUi`):
  - Camada (`Layer`) no acerto de ponteiro, no visual e nos itens de desenho.
  - Arraste de janela so pelo grip (14 px a esquerda do titulo); o grip tem prioridade sobre a borda de redimensionar.
  - Limiar de 16 px para tirar uma janela encaixada do dock (`undockThreshold`); 6 px para janela solta.
  - `UIEventRouter::TopmostAt`: consulta do alvo mais alto com a mesma regra do clique.
  - Teste `SpriteUI.LayersAndInput` registrado; 39/39 testes da SpriteUI passam nesta maquina.
- Editor, regras de janela (`apps/editor/window_manager.*`): 30 verificacoes no teste `Editor.WindowManager`.
  Estas regras ainda nao estao ligadas aos paineis; ver T13.
- Editor, teste `Editor.PanelRouter` (`apps/editor/panel_router.hpp`); o roteador do menu e das Configuracoes esta integrado (ver 1.2).
- Editor, teste ECS: captura de excecao no topo (grava `crash_report.txt`), spawner para sozinho ao faltar
  memoria (`bad_alloc`) e avisa a memoria estimada antes de comecar (cerca de 7 KB por esfera).
- Build MSVC: aviso C4324 desligado de proposito (relleno de alinhamento intencional).

### 1.2 Implementado, mas nao fechado por validacao [V]

| Area | O que falta antes de declarar concluido |
|---|---|
| P1 instanciamento | Cena real em Vulkan/OpenGL, screenshot antes/depois, `DrawStats` e CSV com `--ecs 1000 --play` |
| Troca de API | Validacao visual da janela em uma sessao interativa e medicao da troca (T9) |
| SpriteUI | Validar em GPU real, migrar paineis ativos e verificar docking em janela real (T13, Fase C) |
| Menu e Settings (roteador) | Implementado; falta teste visual: lista aberta nao deixa clique passar (registrar em `00_CHECKLIST.md`) |
| Teste ECS | Reproduzir `test ECS: 10000` e cargas maiores com janela; confirmar que o editor nao fecha (T12) |
| Profiler: secao Priority | Aparencia final em negrito e tamanho; GPU % ainda nao medido (T10) |
| OpenGL | Validar texturas, bloom, sombra pontual, neblina e emissao em uma maquina com GPU |
| D3D11/D3D12 | Compilar e executar em Windows com driver real (T7, T11) |
| Profiler/export | Confirmar a aparencia final e abrir o Markdown exportado em uso real |
| ECS 500/1000 | Reproduzir a estabilidade interativa e medir ganho com render ativo |
| Texturas | Mipmaps/bilinear, sRGB e textura por parte ainda nao estao fechados |
| Fisica ECS | Amostra `--ecs 500 --play` medida sem renderizacao visivel; falta medir com objetos desenhados |

### 1.3 Nao implementar agora [X]

- Rewrite completo do ImGui em uma unica etapa. A remocao acontece janela por janela (Fase C).
- Flecs ou substituicao integral do ECS.
- D3D12, ray tracing e Hi-Z antes de estabilizar P1 e validar o caminho principal.
- GPU resident rendering ou vegetacao voxel antes de P2/P3/P5 terem dados medidos.
- QuickJS como subprocesso ou backend com acesso externo ao projeto.
- Migracao de todo o projeto para C++, porque isso nao melhora os caminhos quentes atuais.

### 1.4 Decisoes tomadas

| Data | Decisao | Motivo |
|---|---|---|
| 2026-11-18 | Um so gerenciador de janelas: a **SpriteUI** (camada, ordem, popup e grip). O `window_manager.*` do editor vira ponte e sera removido depois da migracao, **so com aprovacao** | A mesma regra serve para HUD e menus dentro do jogo |
| 2026-11-18 | Mudancas na SpriteUI sao feitas direto em `third_party/SpriteUi` neste repositorio | Autorizado pelo responsavel |
| 2026-11-18 | Aviso C4324 desligado no MSVC | Alinhamento de SIMD e intencional; aviso so polui o log |
| 2026-11-19 | Roteamento de ponteiro: o menu e as Configuracoes passam pelo `PanelRouter`; ImGui nao recebe ponteiro coberto por painel SpriteUI | Clique atras de menu aberto era o problema relatado (b) |

---

## 2. Ordem recomendada

### Fase A: fechar a base verificada

1. **P1 visual e benchmark** [V]
   - Finalizar a validacao visual do instanciamento.
   - Executar Vulkan e OpenGL na mesma cena.
   - Registrar `DrawStats`, CSV e screenshots.
   - Resultado minimo: imagem equivalente, menos draw calls e nenhum erro de binding.
   - Detalhes: secao 3, "Implementacao tecnica do P1".

2. **Diagnosticar D3D12 (T7)** [ ]
   - Capturar `ppCompilerOutput` em `createShader`.
   - Confirmar o campo na versao vendorizada do Diligent.
   - Copiar `dxcompiler.dll` e `dxil.dll` para o executavel.
   - Nao trocar o compilador nem o shader antes de obter o erro completo.
   - Trabalhar em uma maquina Windows com GPU.
   - Criterio: o log do D3D12 contem a mensagem completa do compilador, ou a causa esta descartada.

3. **Corrigir estabilidade do editor (T12)** [V]
   - Feito no codigo: captura de excecao no topo (`crash_report.txt`), parada controlada do spawner ao faltar
     memoria, aviso de memoria estimada antes do teste.
   - Reproduzir o crash de `test ECS: 500/1000` (corrigido de `500/10000` nesta revisao) e depois `10000`
     e cargas maiores, com janela.
   - Capturar `crash_report.txt`, console e zonas do Profiler.
   - Tratar erros de memoria sem ocultar falhas.
   - Criterio: com janela, `test ECS: 10000` roda; N grande mostra o aviso e o editor continua aberto.

4. **Unificar verificacao** [F]
   - Depois de qualquer codigo novo, rodar pelo menos o alvo relevante, `tool_tests` e o
     `ctest` da configuracao usada.
   - Nunca marcar uma tarefa grafica como concluida sem teste visual em janela real.
   - Estado atual: `ctest` do editor 34/34; `ctest` da SpriteUI 39/39.

5. **Diagnostico do build Windows (T11)** [ ]
   - Causa dos 393 avisos `LNK C4743` ("vftable possui tamanho diferente"): as libs do DiligentCore foram
     compiladas com outro CRT e `_ITERATOR_DEBUG_LEVEL` que o projeto (`CMakeLists.txt` linha 8: MultiThreaded,
     Debug no Debug).
   - Compilar o Diligent com o mesmo runtime e nivel de depuracao do projeto.
   - Avisos do proprio projeto ainda pendentes: C4456/C4457 (`bloom.hpp:35`, `soft_renderer.hpp:289`,
     `components.hpp:454`, `fbx.hpp:444`, `selftest.hpp` em 11 pontos). Avisos de C4146, C4702, C4701 e C4098
     estao no quickjs e no SPIRV-Tools (codigo de terceiros; nao editar o vendorizado sem decisao).
   - Criterio: build limpo sem `LNK C4743`; contagem de C4456/C4457 do projeto igual a zero.

### Fase B: estabilizar APIs e shaders

6. **T8 - shader nativo por backend** [P]
   - HLSL continua para D3D11/D3D12/Vulkan.
   - Criar GLSL OpenGL 430 dedicado em vez de depender da traducao como caminho de producao.
   - Compartilhar nomes, semantica e registradores.
   - Fazer `check_bindings.py` validar HLSL e GLSL.
   - Feito: checador de bindings no ctest; GLSL regenerado; `build_glsl.py --check` confere a sincronia com o HLSL. A contagem de arquivos aparece como 23 no changelog e 26 no relatorio de 2026-11-18; conferir antes de citar.
   - Feito nesta rodada: fontes HLSL em `assets/shaders/hlsl`; GLSL gerado em `assets/shaders/glsl`; caminhos do
     runtime e das ferramentas apontam para as pastas novas. Pendente: compilar e validar os backends.
   - Pendente conhecido (`../estudos/AUDITORIA_SHADERS.md`): PBR com 9 buffers uniformes dinamicos no Vulkan (limite de 8).
     Correcao proposta, nao aplicada.
   - Limite do checador: so entende `#ifdef` simples. Se usar `#if` com expressao, revisar o checador.

7. **T9 - troca de API por etapas** [P]
   - Transformar a troca em uma maquina de estados por quadro.
   - Preservar a escolha solicitada pelo usuario.
   - Continuar em CPU somente apos falha controlada do backend.
   - Validar Vulkan, OpenGL, D3D11, D3D12 e Software.
   - Feito: o pedido responde na hora (seletor mostra a escolha e o status "Switching to ..."); a troca pesada
     roda no inicio do quadro seguinte; pedidos seguidos valem so o ultimo (`requestApi`, `applyPendingApi`).
   - Pendente: a troca ainda bloqueia a janela enquanto aplica; medir e validar visualmente.

8. **T10 - tempo real da GPU** [ ]
   - Adicionar timestamp queries ao RHI.
   - Medir cada passe, nao apenas uma zona aproximada da CPU.
   - Separar CPU, GPU, readback, apresentacao e espera ociosa.
   - Hoje o Profiler mostra "GPU %" como nao medido, com o aviso que aponta para este item.

9. **Zero-copy** [ ]
   - Depois de estabilizar a troca e o readback assincrono, avaliar apresentacao direta.
   - Usar como pre-requisito FSR e pos-processamento GPU.

### Fase C: migracao incremental da interface (SpriteUI no lugar do ImGui)

Esta fase substitui a antiga remocao "de uma vez". Cada etapa tem criterio de entrada, criterio de saida e
um teste visual registrado em `00_CHECKLIST.md`.

9. **Settings SpriteUI** [P]
   - Ja feito: `settings_bridge.hpp` integrado ao painel (`settings_panel.hpp` e `main.cpp` usam).
   - Ja feito: menu e Settings roteados pelo `PanelRouter`; lista de menu aberta
     bloqueia cliques atras dela; ImGui nao recebe ponteiro coberto por painel SpriteUI.
   - Completar Appearance, Settings, Game, Lighting, Optimizing e Language.
   - Entrada: `ctest` verde e roteador validado no teste visual T-1 a T-4.
   - Saida: T-3 (regressao das Configuracoes) sem falhas em janela real.
   - Nao remover os controles ImGui enquanto a paridade nao estiver testada.

10. **Migrar uma janela por vez** [ ]
    - Ordem sugerida: Assets/Scenes, Export/Editor info, Input Map, Script, Project dialog,
      Profiler/Console/Inspector/Scene/Game e, por ultimo, Narrative.
    - Cada janela deve passar por modelo/teste, paridade visual, fallback e remocao do ImGui.
    - Ordem detalhada e criterios por janela:
      1. **Console** (`console_panel`): testes de `test ECS`, filtros, Copiar e clique que seleciona objeto.
      2. **Profiler** (`profiler_panel`): secao Priority em negrito, grafico de frame time, exportacao Markdown.
      3. **Hierarchy e Inspector**: selecionar, renomear, arrastar para pai, adicionar componente, menu de
         contexto. Sao as mais complexas; entram depois do Console e do Profiler.
      4. **Assets, Projeto e Narrative**: por ultimo, com teste de importacao e de edicao de cena.
    - Contagem de referencia para acompanhar o progresso: `grep -c "ImGui::Begin" apps/editor/main.cpp`.

11. **Remover o ImGui do build** [ ]
    - So depois do item 10 inteiro. Sai o ImGui do `main`, do `CMakeLists.txt` e dos `third_party`.
    - Criterio: build e `ctest` verdes sem ImGui; teste visual de todas as janelas registrado.

12. **Integrar cache e Splitter** [ ]
    - Aplicar `TextLayoutCache` aos paineis reais.
    - Registrar benchmark antes/depois com a mesma cena e a mesma maquina.
    - Melhorar layout por medicao, nao por supostas draw calls.

13. **Docking e camadas (T13)** [P]
    - Feito: regras na SpriteUI (camada, popup captura clique, arraste so pelo grip, limiar de dock);
      testes `SpriteUI.LayersAndInput`; `PanelRouter` do editor ligado a menu e Settings.
    - Decisao tomada: a SpriteUI e o gerenciador unico (ver 1.4). `window_manager.*` do editor fica como
      ponte ate a migracao e depois sera removido **somente com aprovacao**.
    - Pendente: ligar as demais janelas ao roteador (item 10); validar popup, arraste pelo grip, limiar de dock e
      multicamada em janela real; desenhar o grip (icone) em cada janela.
    - Criterio: teste visual de menu e Settings sem falhas, registrado em `00_CHECKLIST.md`.

#### Inventario das janelas ainda em ImGui (2026-11-19)

Contagem feita com `grep` em `apps/editor`. "Host" = janela ImGui que hoje so enquadra conteudo SpriteUI.

| Janela | Arquivo | Linhas | Situacao | Observacao |
|---|---|---:|---|---|
| Editor info | `export_ui.hpp` | 136 (com Export) | ImGui | Formulario com InputText e InputTextMultiline; rascunho antes de salvar |
| Export game | `export_ui.hpp` | (acima) | ImGui | Combo de plataforma, botao, aviso de campos faltando |
| Input Map | `extra_ui.hpp` | 540 (com recursos) | ImGui | Inclui popups "addres" e "remres" |
| Script | `script_window.hpp` | 183 | ImGui | BeginChild de completar e de problemas |
| Assets e Scenes | `assets_panel.hpp` | 480 | ImGui | Listas, menus de contexto, 8 modais (renomear, apagar, nova pasta, novo arquivo, nova cena) |
| Edit file (texto) | `assets_panel.hpp` | (acima) | ImGui | Janela de texto de arquivo |
| Narrative Controller | `narrative_panel.hpp` | 280 | ImGui + ImNodes | Ultima na ordem; grafo com ImNodes precisa de decisao propria |
| Project dialog (inicio) | `project_dialog.hpp` | 139 | ImGui (fallback) | Tela de inicio ja e SpriteUI; este e o `--imgui-start` e o caminho sem fonte |
| Hierarchy | `main.cpp` | ~350-480 | Host ImGui | Conteudo SpriteUI (`hierPanel`); menus de criar e renomear ainda ImGui |
| Inspector | `main.cpp` | ~667-900 | Host ImGui | Conteudo SpriteUI (`inspPanel`); popup "Add Component" ainda ImGui |
| Profiler | `main.cpp` | ~629 | Host ImGui | Conteudo SpriteUI (`profPanel`) |
| Console | `main.cpp` | ~650 | Host ImGui | Conteudo SpriteUI (`consolePanel`) |
| Scene e Game | `main.cpp` | ~892-1440 | Host ImGui | Imagem da cena e barra flutuante; menu de contexto da cena em ImGui |
| Settings | `main.cpp` | ~2018 | Host ImGui | Conteudo SpriteUI (`settingsPanel`) |

Decisao necessaria antes de remover qualquer host: **moldura SpriteUI** (titulo, grip, encaixe, camada) para
as janelas. Sem ela, o ImGui continua sendo a moldura mesmo quando o conteudo e SpriteUI.

Ordem proposta (uma janela por commit, cada uma com modelo testavel quando possivel):
1. Moldura SpriteUI (`WindowFrame`) ligada ao `WindowManager`/roteador, testada com Settings.
2. Editor info e Export game (formularios simples).
3. Input Map e Script.
4. Assets, Scenes e Edit file (maior volume de popups).
5. Hierarchy, Inspector, Profiler, Console, Scene e Game (hosts).
6. Narrative Controller (ImNodes).
7. Remover ImGui do build (item 11).

### Fase D: fechar tarefas tecnicas pendentes

14. **D3D12, T7** [ ] (mesmo item 2 da Fase A; ver la)

15. **Instabilidade do editor, T12** [V] (mesmo item 3 da Fase A; ver la)

---

## 3. Plano de escala P2-P6

| Etapa | Acao | Pre-requisito | Aprovar somente com |
|---|---|---|---|
| P2 | Culling por frustum na CPU | Malha com centro/raio; P1 medido | Camera sem objetos sumindo na borda |
| P3 | Compute culling + indirect draw | P2 estabilizado; RHI com dispatch/indirect | Mesma imagem e ganho medido em GPU real |
| P4 | Oclusao Hi-Z | P3 | Cena atras de paredes; aceitar uma latencia de quadro |
| P5 | LOD por distancia | P1-P3 medidos | Triangulos menores sem pop visual |
| P6 | Impostores ou voxels distantes | P1-P5 medidos | Comparacao visual e FPS em floresta densa |

Nao aplicar P3-P6 antes de P1-P2 terem sido medidos. Hi-Z pode causar atrasos de aparecimento em
movimentos rapidos; P5 exige simplificacao offline; P6 e especulativo ate haver uma cena de referencia.

Estado: `frustum_cull.hpp` tem testes (`tests/test_frustum_cull.cpp`) e **esta ligado ao renderizador**: `GpuRenderer` aplica o culling de camera em paralelo a cada quadro (`gpu_renderer.hpp`, avaliacao de `queueItems_`). Pendente do P2: validacao visual de borda (objetos nao podem sumir na borda da tela), que precisa de execucao com renderizacao.

### Implementacao tecnica do P1

- O lote deve agrupar por malha e textura/material.
- Um `StructuredBuffer` envia matriz e dados por instancia uma vez por quadro.
- `BatchBuffer` guarda o indice inicial e cabe em um UBO pequeno.
- O Vertex Shader usa `batchFirst + SV_InstanceID`.
- Nao agrupar transparentes nem bloom nesta fase, pois precisam de estado/ordem diferentes.
- Sombras e prepass podem ser agrupados separadamente, mas somente apos validar o passe principal.
- Criterios obrigatorios: imagem equivalente, draw calls perto do numero de lotes, menos buffer
  uploads e nenhuma falha de binding.
- Registro atual: `Instances` em `t17` (nao conflita com `IblBrdfLut` em `t15`); a pesquisa e o plano estao nas secoes P1-P6 deste documento.

### Implementacao tecnica P2-P6

- P2 precisa de centro e raio calculados na carga da malha e transformados pela matriz do mundo.
- Sombras nao devem usar o frustum da camera; objetos fora da tela ainda podem projetar sombra.
- P3 exige `dispatch`, buffer de contagem e `drawIndexedIndirect` no RHI.
- P4 usa a depth piramide do quadro anterior e aceita uma latencia de um quadro.
- P5 cria lotes separados por malha simplificada e escolhe pelo raio/distancia.
- P6 usa impostores/voxels somente apos P1-P5 terem sido medidos em cena real.

---

## 4. Performance por beneficio

### Referencia atual do SpriteUI

Benchmark CPU em Release, executado em Windows com MSVC:

| Fase | Tempo medio |
|---|---:|
| Layout | 0.4352 ms/quadro |
| Texto | 0.1277 ms/quadro |
| Extracao | 0.2998 ms/quadro |
| Assembleia | 0.3806 ms/quadro |
| Input | 0.0035 ms/quadro |
| Total | 1.2953 ms/quadro (772 quadros/s) |

Cenario: 20 janelas, 200 botoes e labels TrueType; 1000 quadros medidos apos 20 de aquecimento.
Os numeros dependem da maquina e nao comparam uma versao anterior nem o Editor integrado.
Antes de afirmar ganho, integrar `TextLayoutCache` aos paineis ativos e repetir a mesma medicao.

### Medicoes do ECS e da fisica

- `--ecs 500 --play` (502 corpos, 60 quadros apos o spawn): CPU 1,29 ms/quadro, Physics 0,04 ms; Vulkan 1,40
  ms/quadro, Physics 0,03 ms. As vistas nao renderizaram objetos nessa execucao (0 draws): e uma medicao da
  simulacao, nao um benchmark visual.
- Por esfera no spawner: cerca de 7 KB de memoria (50 mil esferas = cerca de 370 MB; medido em 2026-11-18).
- Custo por quadro do spawner: cerca de 700 ms de CPU com 50 mil esferas (medido em 2026-11-18, antes da
  reducao do Rebuild por quadro). Repetir a medicao depois de T12.

### Ordem tecnica recomendada

1. P1-P2: reduzir escritas de buffer e objetos fora da tela.
2. Readback assincrono e, depois, apresentacao direta.
3. Filtros/texturas: mipmaps, sRGB e filtragem bilinear.
4. `World::each` sem copia e indice por tag.
5. Hierarquia de muitos filhos medida com o Profiler.
6. Contexto QuickJS por arquivo.
7. Compute, indirect draw, Hi-Z e LOD.

Nao otimizar o ECS com SoA ou execution paralelo apenas por possivel ganho. Primeiro medir o caminho
real com a cena benchmark e manter callbacks seguros que possam criar/destruir entidades.

---

## 5. Scripts e runtime

- A recomendacao continua QuickJS como runtime padrao.
- Usar `IScriptHost` com um contexto por arquivo ou pool controlado.
- Expor somente objeto, fisica, Input, Audio e Scene.
- Nunca expor `fs`, rede, processo ou acesso arbitrario ao projeto.
- Definir limite de memoria e tempo por quadro.
- Tools nativos continuam sendo o caminho rapido para Jump, Move, Follow, Physics e Gameplay comum.
- C++ deve ser opcional para plugins/mods de alto desempenho, nao o substituto dos scripts.
- WASM pode ser estudado posteriormente para modding seguro e scripts portaveis.

Detalhes: `../estudos/RUNTIME_CODIGO.md` e `../estudos/JS_VS_CPP.md`.

---

## 6. Trabalho pendente por modulo

### Render

- Zero-copy e readback assincrono.
- FXAA/nitidez e bloom na GPU.
- Validacao opcional em Settings (hoje so por variavel de ambiente).
- Textura por parte tem override manual no Inspector; mapas embutidos no importador, mipmaps e sRGB seguem pendentes.
- Canal alpha no mesh; "use object as light"; bloom mais forte e skybox.
- Cache do skybox enquanto a camera nao muda.
- Grid/gizmos 2D adaptados a camera ortografica (hoje usam a projecao perspectiva).
- Culling por oclusao.
- FSR com compute e vetores de movimento.
- Sombras pontuais ja implementadas; fechar paridade OpenGL e teste visual.
- Shaders nativos por API (T8).
- OpenGL: iluminacao em GLSL (hoje cai para a CPU); texturas no GLES (cinza no teste; investigar `AlbedoMap`).
- Limite de 8 buffers dinamicos no Vulkan (PBR usa 9): correcao proposta, nao aplicada.

### 2D e UI

- Entidade Sprite e Sprite Editor (janela desativada e bloqueada por padrao).
- Migracao ImGui -> SpriteUI incremental (Fase C).
- SpriteUI sobre DX11: nao iniciado.
- TextArea, Browse (botao sem teste fora de janela real), fontes do projeto e imagens.
- Campos de texto que se apagam sozinhos: nao reproduzido; nao considerar corrigido.
- DataGrid virtualizado e binding reativo.
- FreeType e, depois, HarfBuzz.
- Aninhamento de elementos de UI.
- UI no mundo em 3D.
- Newtoon com configuracoes 2D (travar eixo Z, colisores 2D).

### Particulas e fisica

- Profundidade ja implementada no caminho atual; fechar gravidade/quique e vento.
- Compute shader, indirect draw e emission por malha somente apos compute no RHI.
- Sub-emissores, fraturamento e audio espacial.
- Colisores exatos de cilindro, cone, capsula, roda e prismas (hoje caixa envolvente).
- Colisao por chunk usa proxies estaticos em colunas; falta validacao. Undo para paint/fill/clear foi implementado e aguarda validacao. Selecao de chunk no viewport segue pendente.
- Sombra dos chunks em mundos grandes (area do mapa de sombra).

### Projeto e exportacao

- Editar nome e autor pela aba Project.
- Versao do executavel no Windows.
- Android, WebGL e UWP somente depois de fechar os backends principais.
- Exportacao segura com manifesto e assinatura antes de expandir plataformas.
- Gravacao atomica de cenas em `.tmp` + rename.
- Logo da engine na janela de inicio (o espaco ja existe).
- Fontes `.ttf` importadas para o projeto e escolha de fonte da UI.

### Editor e ferramentas

- Inspector: copiar e remover componente; secoes de Fisica, Ferramentas e Script ainda no formato antigo.
- Verificador de scripts no editor de texto para arquivos .tool.js foi implementado e aguarda validacao visual.
- Propriedades de shader expostas aos scripts.
- Um contexto de script por arquivo (hoje um por objeto).
- Animacao e skins (ainda ignorados no importador); aplicacao automatica das texturas embutidas por parte.

### Seguranca e repositorio

- Nao executar scripts externos ou limpar limites de tamanho.
- Validar todos os numeros de render, path, package e shader.
- Mover implementacoes pesadas de headers para `.cpp` em modulos, com render primeiro.
- Fixar ou vendorizar dependencias com licenca registrada (DiligentCore: pin ou vendor, ver `../referencia/COMPILAR.md`).
- Reduzir artefatos de build fora do pacote; manter os dos terceiros necessarios.
- Atualizar nomes de versao em um unico lugar.
- `build_log.txt` fora do repositorio (`.gitignore`); ver `../estudos/PESO_REPOSITORIO.md`.
- Remover `proj_ecs/` (projeto de teste) do repositorio local antes de commitar.

### Nucleo e desempenho do ECS

- `World::each` sem copia da lista de indices.
- Indice por tag (`eachTagged`).
- Hierarquia com muitos filhos (medir com `test ECS: 100000`).
- Lote de instancing para meshes repetidas.
- Arquetipos/chunks no ECS.
- Zero-copy de apresentacao (ver Renderizacao).

---

## 7. Criterios de aceite global

Nenhuma tarefa fica pronta sem:

1. Build relevante concluido.
2. Testes automatizados verdes.
3. Teste visual ou interativo registrado quando houver interface/GPU.
4. Medicao antes/depois na mesma configuracao para qualquer afirmacao de desempenho.
5. Changelog, checklist e este roteiro atualizados.
6. Itens nao verificados escritos explicitamente como pendentes, sem linguagem de sucesso.

---

## 8. Onde encontrar detalhes

| Tema | Documento |
|---|---|
| Estado por versao | `../referencia/CHANGELOG.md` |
| Checklist detalhada | `00_CHECKLIST.md` |
| Testes visuais e estado por versao | `00_CHECKLIST.md` |
| Tarefas e estado P1-P6 | Este documento |
| Pesquisa de milhoes de objetos | Secoes P1-P6 deste documento |
| Performance | `03_DESEMPENHO_EDITOR.md`, `../estudos/AUDITORIA_DESEMPENHO.md` |
| SpriteUI | `06_SPRITEUI.md`, `../referencia/IDIOMAS_E_TEMAS.md`, `07_UI_DE_JOGO.md` |
| Shaders | `../estudos/AUDITORIA_SHADERS.md`, `../estudos/MATRIZ_APIS_SHADER.md` |
| Build | `../referencia/COMPILAR.md` |
| Scripts | `../estudos/RUNTIME_CODIGO.md`, `../estudos/JS_VS_CPP.md`, `../referencia/ERROS_SCRIPT.md` |
| Seguranca | `../estudos/SEGURANCA_ARQUIVOS.md` |
| Peso do repo | `../estudos/PESO_REPOSITORIO.md` |

Removidos no commit `908cbaf`: `CODAR.MD` (raiz), `P1_IMPLEMENTACAO.md` (raiz), `docs/ESTADO_ATUAL.md`,
`docs/IMPLEMENTATION_STATUS.md`, `docs/NEXT_STEPS.md`, `docs/ALPHA.md`, `docs/BETA.md`, `docs/BUILD_TIME.md`,
`docs/RENDER_PERF.md`. O conteudo util deles esta nas secoes deste documento.

---

## 9. Correcoes feitas nesta revisao

- Fase A, item 3: `test ECS: 500/10000` corrigido para `test ECS: 500/1000` (typo provavel; confirmar no teste).
- Fase C, item 9: "Integrar `settings_bridge.hpp`" estava desatualizado; a ponte ja esta integrada.
- Profiler: texto de GPU apontava para `CODAR.MD`, removido; agora aponta para o item 8 deste documento.
- T11, T12 e T13 nao estavam no roteiro; agora estao nas Fases A, C e D.
- Estado de SpriteUI e dos paineis separado em "feito" e "pendente" para nao confundir o que esta so na biblioteca com o que o editor usa.

## 10. Perguntas em aberto

1. Aprovar a remocao de `window_manager.*` do editor depois da migracao?
2. Aprovar a ordem de migracao da Fase C (Console, Profiler, Hierarchy/Inspector, Assets/Narrative)?
3. O item T11 (CRT do Diligent) deve ser feito na sua maquina Windows, com o build do Diligent recompilado?
