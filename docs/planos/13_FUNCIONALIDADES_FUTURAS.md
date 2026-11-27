# 13 — Funcionalidades futuras pedidas

Nove funcionalidades pedidas pelo usuário. Nenhuma está implementada. Cada item diz o que existe hoje (conferido no código em 2026-11-27) e o que falta. Prioridade: P0 bloqueia outras; P1 próximo ciclo; P2 depois.

Documentos relacionados: `07_UI_DE_JOGO.md`, `08_PARTICULAS.md`, `09_TEXTURAS_E_MATERIAIS.md`, `../estudos/RENDER_MILHOES.md`, `../engineering/00_ROADMAP_EXECUCAO.md`.

## Concluídas (fora da lista a-i)

Estes itens já estão prontos no código e não são parte das funcionalidades a-i:

- Presets de configuração em `<projeto>/Configs` (render + physics), com Edit config / Save / Delete em Settings; engrenagem na janela Scenes para config por cena.
- Tooltips nas opções de Settings.
- Toggle "View collision shape" na barra da Scene e por objeto no Inspector (bloco Physics).
- RHI com compute, UAV, draw indireto e readBuffer (backend Diligent; testado só em Vulkan).

## Pendentes

Continuam pendentes, sem implementação: HiZ (oclusão por profundidade na GPU, ver `../estudos/RENDER_MILHOES.md`), colisão por casco na narrowphase (hoje só caixa e esfera, ver `10_FISICA.md`), fontes (a), imagens em UI (c), escala de elementos (d), 2D (e), Pong (i), partículas (f) e materiais (g).

## Resumo

| Id | Funcionalidade | Prioridade | Depende de |
|---|---|---|---|
| a | Fontes do projeto | P1 | — |
| b | Tamanho de fonte livre | P0 | — |
| c | Imagens no Canvas e na UI, botão com textura | P1 | — (usa `createTexture` do RHI) |
| d | Escala em todo elemento de Canvas/UI | P0 | — |
| e | Cena 2D | P1 | — |
| f | Biblioteca de partículas nova | P2 | g (malhas e shaders), `RENDER_MILHOES.md` |
| g | Materiais e shaders como assets | P1 | h |
| h | Color picker com alpha | P0 | — |
| i | Projeto de exemplo Pong (2D e 3D) | P2 | e |

Ordem sugerida: b, d, h (pequenas, destravam o resto) → a, c, e, g → f, i.

---

## a. Fontes do projeto

**Objetivo.** O usuário usa fontes próprias nos textos do jogo, em vez da única fonte da engine.

**O que o usuário vê.** Coloca arquivos `.ttf`/`.otf` numa pasta de fontes do projeto. No Inspector, em um elemento de texto e no Canvas, aparece o campo "Fonte" com a lista das fontes do projeto e o botão **Procurar fonte**, que abre o seletor de arquivos e copia o arquivo escolhido para a pasta do projeto.

**Estado hoje.**
- Existe uma única fonte: `Xmind-Regular.ttf`, embutida no executável em Base64 (`apps/editor/embedded_font.hpp`), com fallback quando `assets/fonts` não existe (`TextAtlasSet::load` em `apps/editor/text_atlas_set.hpp`).
- `UiElem` (`apps/editor/components.hpp`) não tem campo de fonte; só `fontScale`.
- O seletor de arquivos nativo já é usado para importar assets: `SDL_ShowOpenFileDialog` em `apps/editor/main.cpp` (importação).
- `TextAtlasSet` guarda um conjunto de bytes de fonte e um atlas por tamanho. Para várias fontes é preciso um atlas por par (fonte, tamanho).

**O que muda na engine.**
- `components.hpp`: campo `font[160]` em `UiElem` e no `CanvasComp` (fonte padrão do canvas); serialização em `saveEntity`/`loadEntity` com valor padrão vazio (cenas antigas continuam abrindo).
- `text_atlas_set.hpp`: mapa fonte → bytes → atlas por tamanho, com orçamento de memória e despejo (o contrato de atlas e despejo é a Etapa 3 de `../engineering/00_ROADMAP_EXECUCAO.md`).
- `inspector_panel.hpp`: seletor de fonte e botão "Procurar fonte" (mesmo padrão de `assetRow`).
- `project_paths.hpp` e importador (`queueImport`): pasta de fontes e cópia do arquivo.
- Exportador (`export.hpp`): incluir as fontes usadas no pacote.
- Fonte ausente ou inválida cai na fonte embutida e registra aviso no Console.

**Dependências.** Nenhuma obrigatória. Combina com (b) para o tamanho.

**Critério de aceite.**
- Fonte colocada na pasta aparece na lista sem reiniciar o editor.
- Trocar a fonte de um texto muda o desenho na Scene e na Game.
- Cena salva e reaberta mantém a fonte; fonte removida cai na embutida sem travar.
- Teste automático: carregar fonte válida, fonte corrompida, arquivo ausente.
- O custo de memória dos atlas aparece no Profiler.

**Prioridade.** P1.

---

## b. Tamanho de fonte livre

**Objetivo.** O valor do tamanho não tem teto fixo; o usuário digita o número.

**O que o usuário vê.** Ao lado do slider de tamanho há um campo numérico. Digitar 300 vale 300. O slider cobre uma faixa útil, mas não limita o campo. Valor abaixo do mínimo de segurança é corrigido para o mínimo.

**Estado hoje.**
- O campo numérico ao lado do slider já existe (`apps/editor/slider_value_edit.hpp`), mas prende o valor digitado em `[lo, hi]` do slider (`settings_form::ClampSliderValue`).
- O tamanho do texto de UI é `UiElem::fontScale`, com slider `{0.5, 4}` (`inspector_panel.hpp`) e limite na leitura da cena `0.3..8` (`ser::clampNum` em `components.hpp`). Ou seja, há dois tetos: o do slider e o do arquivo.

**O que muda na engine.**
- `slider_value_edit.hpp`: opção "sem teto" (`hi` aberto) no campo digitado; o slider continua usando a faixa visual.
- `components.hpp`: trocar o teto `8` da leitura por um teto de segurança alto (a definir por medição) e mínimo de `0.3` mantido; valores não finitos viram o padrão.
- `text_atlas_set.hpp`: atlas por tamanho com limite de textura. Texto muito grande não pode alocar atlas sem limite; acima de um tamanho de glifo, desenhar escalando um atlas existente ou rejeitar com aviso.
- Teste do campo: digitar valor acima da faixa do slider, valor negativo, texto inválido, NaN.

**Dependências.** Nenhuma. Interage com (a) (atlas por fonte e tamanho).

**Critério de aceite.**
- Digitar um valor acima do máximo do slider é aceito, salvo e recarregado.
- Valor abaixo do mínimo de segurança é elevado ao mínimo; entrada inválida não muda nada (regra atual).
- Tamanho muito grande não estoura memória (o teste mede o atlas gerado).
- Os dois tetos (slider e arquivo) deixam de divergir.

**Prioridade.** P0.

---

## c. Imagens: Canvas, UI e botão com textura

**Objetivo.** Mostrar imagens na UI de jogo e trocar a aparência de um botão por uma imagem.

**O que o usuário vê.** Novo elemento **Imagem** no Canvas, com campo de textura (assets de imagem do projeto). O botão ganha três campos de textura: normal, hover e pressionado. Sem textura, o botão continua como hoje.

**Estado hoje.**
- `UiKind` tem só `Text, Panel, Button, Bar` (`components.hpp`). Não existe tipo imagem.
- `canvas_ui.hpp` (API nativa, sobre SpriteUI) tem `Kind { Text, Button }`.
- O RHI tem `createTexture(const TextureDesc&, const void* data)` (`engine/rhi/include/tool/rhi/rhi.hpp`) e o editor carrega imagens (`apps/editor/image_io.cpp`). O Inspector já tem seletor de asset de imagem (`assetRow(..., AssetKind::Image, ...)`).
- Não verifiquei se a SpriteUI do repositório já aceita itens com textura própria no editor; é o primeiro ponto a conferir.

**O que muda na engine.**
- `components.hpp`: `UiKind::Image`; campos `image[160]` e, no botão, `imageHover[160]`, `imagePressed[160]`; modo de ajuste (esticar, manter proporção, 9-slice como P2).
- `canvas_ui.hpp` e `spriteui_ecs.hpp`: item de desenho com textura, estados do botão mapeados para a troca de textura.
- Cache de textura por caminho (reaproveitar o carregador de imagens), com contagem de referências.
- Inspector: linhas de textura; Scene/Game: desenho.
- Exportador: incluir as imagens usadas.
- Limite atual de `kMaxUi = 16` elementos por canvas continua; imagens contam nele.

**Dependências.** (d) ajuda para animar a imagem. Texturas em atlas (para reduzir trocas de textura) é melhoria posterior.

**Critério de aceite.**
- Imagem aparece no Canvas na Scene e na Game, em qualquer resolução (posição em fração, como hoje).
- Botão com três imagens troca ao passar o mouse e ao pressionar; o evento de clique continua disparando.
- Imagem ausente mostra um marcador visível e um aviso no Console, sem travar.
- Cena salva e reaberta mantém as imagens.
- Alpha da PNG é respeitado.

**Prioridade.** P1.

---

## d. Escala em todo elemento de Canvas/UI

**Objetivo.** Animar crescer e diminuir sem mexer em `w`/`h` e sem refazer o layout.

**O que o usuário vê.** Em todo elemento de Canvas/UI, o campo **Escala** (uniforme) e, expandido, **Escala X** e **Escala Y**. O pivô é o centro do elemento (opção de pivô como P2). Pode ser animada por keyframes ou por script.

**Estado hoje.**
- `UiElem` tem posição e tamanho em fração (`x, y, w, h`) e `fontScale`. Não há escala de elemento.
- Não existe sistema de keyframes/animação de UI no código que eu tenha encontrado; scripts existem (`ScriptComp`, `*.tool.js`) e o Narrative (`ui_show`, `ui_hide`).

**O que muda na engine.**
- `components.hpp`: `scale[2]` (e `scaleUniform`) em `UiElem`; padrão `{1,1}` e leitura com valor padrão (cenas antigas iguais).
- Desenho do canvas (`canvas_ui.hpp`, `spriteui_ecs.hpp`): aplicar a escala ao retângulo e ao texto em torno do pivô, sem marcar o layout dos vizinhos como sujo (escala é só de pintura).
- Hit-test: usar o retângulo escalado.
- API de script e do Narrative: `ui_scale(id, sx, sy)` e leitura da escala atual; os elementos já têm id único (`ui_ids.hpp`).
- Keyframes: uma trilha simples por elemento (tempo → escala, interpolação linear e ease). Pode ser P2 se o script bastar.

**Dependências.** Nenhuma para o parâmetro. Keyframes dependem de um relógio de animação do Play.

**Critério de aceite.**
- Escala 1 não muda nada nas cenas existentes.
- Escala 2 dobra o elemento em torno do centro; texto acompanha.
- Um script que anima a escala de 1 a 1,2 e volta roda sem refazer o layout (medir: zero reconstruções de layout durante a animação).
- Clique funciona na área escalada.
- Valor negativo ou não finito é rejeitado (ou vira espelhamento, se for decisão explícita).

**Prioridade.** P0.

---

## e. Cena 2D

**Objetivo.** Um tipo de cena que não carrega recursos 3D e oferece ferramentas de 2D.

**O que o usuário vê.** Ao criar a cena, escolhe **2D** ou **3D**. A cena 2D abre com grade 2D (plano XY), gizmos 2D (mover em X/Y, girar em Z, escalar em X/Y) e câmera ortográfica. Não carrega malhas, sombras nem luzes de cena ao abrir.

**Estado hoje.**
- Não há tipo de cena (grep por `Scene2D`, `sceneType`, `sceneKind` não achou nada).
- A câmera já tem modo 2D: `CameraComp::ortho2D` e `orthoSize` ("2D camera (orthographic)" no Inspector).
- A grade da Scene é a do chão em Y = 0, que acompanha a câmera (`gridLines` em `apps/editor/scene_view_model.hpp`).
- Gizmos existem para 3D no Scene (`scene_view_panel.hpp`); não verifiquei se há restrição de eixo.
- Não há componente de sprite 2D; hoje um objeto 2D seria um `MeshRenderer` com forma plana, ou UI.

**O que muda na engine.**
- Cena: campo `kind` (2D/3D) no arquivo `.scene.json`; cenas antigas = 3D.
- `scene_view_panel.hpp` / `scene_view_model.hpp`: grade XY, gizmos 2D, câmera ortográfica travada em Z.
- Renderer: no modo 2D, pular passes de sombra, luzes de cena e IBL; carregar só o que a cena usa. Precisa de medir o que o carregamento de uma cena vazia custa hoje.
- Componente de sprite 2D (textura, cor, ordem de desenho) — depende de (c) para texturas.
- Física 2D: o Newtoon não tem 2D ligado ao editor (`10_FISICA.md`). Para o Pong, travar Z e usar colisores de caixa e esfera em XY basta como primeiro passo; física 2D própria fica fora.
- Menu "Nova cena" e `scenes_panel.hpp`.

**Dependências.** (c) para sprites com imagem. Alimenta (i).

**Critério de aceite.**
- Nova cena 2D abre sem criar recursos de sombra nem de luz de cena (conferir no Profiler e no log de recursos).
- Grade 2D e gizmos 2D funcionam; mover um objeto não altera Z.
- Cena 3D existente abre igual a hoje.
- Medição: tempo e memória de abrir cena 2D vazia contra cena 3D vazia, registrados antes e depois.

**Prioridade.** P1.

---

## f. Biblioteca de partículas nova

**Objetivo.** Substituir o sistema atual por um orientado a dados, capaz de milhões de partículas.

**O que o usuário vê.** O componente de partículas atual continua abrindo; a nova biblioteca entra com emissores que aceitam malhas simples, depois fluido, fumaça, nuvem e faíscas, e recebem malhas e shaders (g).

**Estado hoje** (`08_PARTICULAS.md`, `apps/editor/fx.hpp`, `ParticleComp`).
- Emissor na CPU. Cada emissor guarda a própria lista de partículas; SoA está marcado como "parcial/próximo passo" no documento.
- Todas as partículas do quadro viram quads voltados para a câmera, agrupados por cor: no máximo 64 lotes por vista, uma chamada de desenho por lote. Há teste de profundidade, culling por frustum e orçamento global `particleBudget` de 100 mil.
- Simulação em paralelo, um emissor por tarefa (`tool::parallelFor`).
- Sem alpha real; cor em 4 níveis por canal.
- O RHI tem compute, UAV e draw indireto no backend Diligent (testados só em Vulkan, ver `../planos/12_OTIMIZACAO_FISICA_ECS.md`); o documento 08 ainda marca simulação em GPU como "não agora".
- Não há malha por partícula, fluido, fumaça com volume, nuvem nem faíscas.

**O que muda na engine.**
- Novo módulo de dados: ECS com arrays contíguos por atributo (posição, velocidade, idade, tamanho, cor), uma única tabela por pool, índices estáveis, remoção por troca com o último. A simulação roda em blocos paralelos e é elegível a SIMD.
- Fase 1: partículas com malhas simples (cubo, esfera, quad) desenhadas por instancing. Depende do caminho de instâncias do renderer (`RENDER_MILHOES.md`, etapas E1 a E3).
- Fase 2: tipos de efeito. Faíscas (partículas curtas com rastro), fumaça e nuvem (sprites com alpha e ordenação), fluido (a decidir: partículas com forças de vizinhança, que exigem grade espacial).
- Fase 3: simulação em GPU quando o RHI tiver compute e buffers estruturados, e draw indireto.
- Entrada de malhas e shaders: via assets de Material e Shader (g).
- Migração: conversor de `ParticleComp` antigo para a nova biblioteca; manter o antigo até a paridade.

**Dependências.** (g) para malhas e shaders; (h) para cores com alpha; `RENDER_MILHOES.md` para instâncias e draw indireto; alpha real depende de ordenação de transparência.

**Critério de aceite.** (por fase, com medição na mesma máquina)
- Fase 0 (linha de base): medir o sistema atual com 100 mil partículas (tempo de update e de render) antes de mexer.
- Fase 1: 1 milhão de partículas com malha simples a um tempo de quadro definido antes, e pior quadro registrado. O número alvo só é fixado depois da linha de base; não prometer sem medir.
- Resultado determinístico com a mesma semente.
- Sem alocação por quadro no caminho quente.
- Cena com partículas antigas abre e funciona.

**Prioridade.** P2.

---

## g. Materiais e shaders como assets

**Objetivo.** Material e Shader passam a ser assets editáveis e reaproveitáveis.

**O que o usuário vê.** Cria um asset **Material** (cor, textura, alpha, metálico, rugosidade), arrasta para o objeto e edita num painel. Depois, cria um asset **Shader** e o escolhe no material.

**Estado hoje** (`09_TEXTURAS_E_MATERIAIS.md`, `components.hpp`).
- O material é embutido no `MeshRenderer`: `color`, `roughness`, `metallic`, `texture` (caminho), `opacity`, emissivo e névoa. Há override por parte (`PartOverride`) com cor, rugosidade, metálico e textura.
- O shader PBR declara `AlbedoMap` (`pbrTextureNames` em `gpu_renderer.hpp`). O documento 09 afirma que o renderer não amostra texturas e marca o plano como "não implementado"; o código sugere que ao menos o albedo já está ligado. Conferir o estado real antes de começar e corrigir o documento 09.
- Não existe formato `.tmat` nem asset de Shader. O RHI aceita `PipelineDesc` com nomes de uniformes e texturas (`rhi.hpp`).

**O que muda na engine.**
- Primeiro passo (listado): formato `.tmat` (JSON) com `{ cor, textura, alpha, metálico, rugosidade }`, referenciado pelo `MeshRenderer` por caminho. Migração: ao abrir cena antiga, os campos do `MeshRenderer` viram um material embutido; nada quebra.
- Painel de edição do material no Inspector e na janela de Assets, com pré-visualização.
- Biblioteca de materiais com cache por caminho e contagem de referências.
- Passo seguinte: asset de Shader (HLSL/GLSL) com contrato de uniformes e texturas fixo, validado na carga, e seleção no material. Os GLSL são mantidos à mão hoje (`../estudos/MATRIZ_APIS_SHADER.md`, `../estudos/AUDITORIA_SHADERS.md`).
- Depois: normal map, ORM, emissivo, tiling (lista do documento 09).
- Alpha exige decisão sobre transparência (ordenação e passe separado); o documento 02 trata a transparência.
- Renderer CPU (SoftRenderer): precisa continuar funcionando (regra 7 do roteiro de execução).

**Dependências.** (h) para escolher cor com alpha; (c) para o carregador de textura compartilhado.

**Critério de aceite.**
- Dois objetos com o mesmo `.tmat` mudam juntos quando o material muda.
- Cenas antigas abrem com o mesmo visual (comparação de imagem).
- Alpha aparece e ordena corretamente em uma cena de teste com 3 camadas transparentes.
- Shader inválido mostra o erro de compilação no Console e usa o PBR padrão.
- Caminho CPU-only continua funcionando.

**Prioridade.** P1 (material), P2 (shader).

---

## h. Color picker com alpha e mais cores

**Objetivo.** Um seletor de cor único, com alpha, usado em toda escolha de cor.

**O que o usuário vê.** Em qualquer campo de cor (luz, fundo do Canvas, borda, partículas, material), abre o mesmo popup com HSV, RGBA, hexadecimal, controle de alpha e uma paleta de cores salvas.

**Estado hoje.**
- O popup compartilhado já existe: `apps/editor/color_popup.hpp`, sobre `SpriteUi/Widgets/ColorPicker.hpp`. O widget da SpriteUI já tem canal Alpha (`ColorChannel::Alpha`, `SetAlpha`).
- O popup passa alpha fixo em 1 (`picker_.SetColor(Vec4{rgb..., 1.0f})` e leitura só de RGB, `ib::ReadColorPicker(picker_, rgb_)`). A API do popup recebe `float rgb[3]`.
- Inspector usa o popup (`inspector_panel.hpp`). A seção "Sun (Global Light)" das Configurações (`apps/editor/main.cpp`, por volta das linhas 1737-1740) mostra a cor do sol com três barras R/G/B ("Light color"). Esse é o caso a substituir pelo popup, com alpha. Também há três barras para "Solid color" do céu (`main.cpp` por volta da linha 1646).
- Cores com alpha já existem em `CanvasComp::bg`, `edge` e `UiElem::color` (`float[4]`); as outras (luz, `MeshRenderer`, partículas) são RGB.

**O que muda na engine.**
- `color_popup.hpp`: aceitar `float*` com 3 ou 4 canais; mostrar a barra de alpha só quando houver 4.
- Passar os campos RGBA existentes (`bg`, `edge`, `UiElem::color`) ao popup com alpha.
- Substituir qualquer controle de cor feito com barras pelo popup.
- Cores salvas do projeto (paleta) em `project.json`.
- Em campos RGB que ganham alpha (material, partículas), manter o arquivo antigo legível (alpha padrão 1).

**Dependências.** Nenhuma. Bloqueia (g) e melhora (f).

**Critério de aceite.**
- Todos os campos de cor abrem o mesmo popup.
- Campos RGBA mostram e salvam o alpha; campos RGB não mostram alpha.
- Hexadecimal de 6 e 8 dígitos funciona nos dois sentidos.
- Teste automático: valores extremos e entrada inválida.
- Teste visual no Inspector e nas Configurações.

**Prioridade.** P0.

---

## i. Projeto de exemplo Pong (2D e 3D)

**Objetivo.** Um projeto de exemplo, criado passo a passo, que mostra o fluxo completo de fazer um jogo.

**O que o usuário vê.** Projeto de exemplo "Pong" em duas versões (2D e 3D), com um guia passo a passo e um diagrama do fluxo do usuário: criar projeto → criar cena → criar objetos (raquetes, bola, paredes) → física e colisores → scripts de entrada e pontuação → Canvas com placar → Play → exportar.

**Estado hoje.** Não existe. A engine já tem os blocos: física Newtoon, Tools (Move, OnCollision), Canvas com texto e variáveis (`{score}`), Narrative, exportador. O exemplo 2D depende da cena 2D (e); o 3D pode ser feito com o que existe.

**O que muda na engine.** Nenhuma mudança de motor obrigatória além de (e). Entregáveis:
- Pasta de exemplo versionada, aberta pelo botão "Projetos de exemplo" (a criar).
- Documento com os passos e o diagrama do fluxo do usuário (criado pelo agente de documentação em etapa posterior; este documento só registra a funcionalidade).
- Cada passo vira um teste de roteiro (abrir, Play por N quadros, conferir placar).

**Dependências.** (e) para a versão 2D. Usa (d) e (c) se o exemplo animar o placar ou usar imagens.

**Critério de aceite.**
- Um usuário novo segue o guia e chega ao Play sem pergunta fora do guia.
- Versões 2D e 3D abrem, rodam e exportam.
- Lacunas encontradas durante o passo a passo viram itens neste documento.

**Prioridade.** P2.
