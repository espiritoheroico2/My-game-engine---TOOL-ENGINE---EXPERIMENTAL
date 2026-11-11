# Guia de implementação do SpriteUI

Objetivo: um pacote de UI independente, que serve à ToolEngine e a outros projetos. Ele desenha UI de editor e de jogo
(janelas com docking, campos de texto, painéis) e também sprites e partículas 2D. A substituição do ImGui depende
deste pacote estar pronto e testado por camadas.

Este guia é a referência de trabalho. Ele separa o que está **verificado** do que está **pendente**. Não marque um
item como concluído sem o critério de aceitação da seção 5.

---

## 1. Premissas

1. **Independência.** O núcleo (`include/`, `src/`) compila sozinho, sem o engine, sem SDL e sem Diligent.
2. **Adaptadores são opcionais.** Cada integração (SDL3, RHI da ToolEngine) é uma opção de CMake que fica desligada
   por padrão. Um adaptador nunca é pré-requisito para o núcleo.
3. **Um dono para cada contrato.** O formato do quadro (`FramePacket`) é a fronteira estável entre o layout e
   qualquer renderizador. Nada do núcleo conhece a API gráfica.
4. **Sem ImGui.** A biblioteca não inclui nem depende do ImGui.
5. **Testar em camadas.** Uma camada só começa depois que a anterior passa. Ver seção 4.

## 2. Estrutura do pacote

```
include/SpriteUi/          API pública do núcleo (Core, Layout, Input, Text, Rendering, Widgets, Animation, Binding, Config)
src/                       implementação do núcleo
adapters/toolengine/       adaptador header-only para o RHI da ToolEngine (opcional)
shaders/toolengine/        HLSL e GLSL 430 usados pelo adaptador (cópias de assets/shaders do engine: manter iguais)
third_party/stb/           stb_truetype (opcional, para o atlas de texto)
Fonts/Xmind/               fonte para testes e exemplos (OFL 1.1)
tests/                     testes do núcleo (test_main, test_text_layout, test_truetype_atlas, ...) e visuais
tests/package_consumer/    teste de consumo: o pacote instalado é usado por um projeto de fora
docs/                      esta documentação e as de API
```

Regra de fronteira: arquivos em `include/SpriteUi/` **não** podem incluir cabeçalhos de `adapters/`, de SDL, de Diligent
ou de `tool/`. A verificação é o alvo `SPRITEUI_CHECK_HEADERS` (cada cabeçalho público precisa compilar sozinho).

## 3. Contratos

Estes contratos estão no código. Mudá-los exige atualizar o adaptador, os testes e este guia.

### 3.1 Quadro (FramePacket)

- `SpriteInstanceGPU`: `transformRow0[3]` = (m00, m01, tx); `transformRow1[3]` = (m10, m11, ty); o canto (u, v) do
  quadrado unitário vai para `tx + m00·u + m01·v` (x) e `ty + m10·u + m11·v` (y). O 4º elemento de `transformRow1`
  é o raio do canto quando `FlagRoundedPanel` está ativo.
- `uvRect` = [u, v, largura, altura] normalizado no atlas. O UV do canto é `uvRect[0] + u·uvRect[2]`, etc.
- `resourceIds[0]` = slice da textura. `WhiteTextureSlice` (0) é o retângulo branco sólido.
- `resourceIds[2]` = índice do recorte em `clipRects` (`NoClipIndex` se não há recorte).
- `tint` = cor **premultiplicada linear**.
- `DrawBatch` agrupa instâncias contíguas; a ordem de pintura é a ordem das instâncias.

### 3.2 Backend (IRenderBackend)

- `Submit(const FramePacket&)` desenha o quadro inteiro. Retorna `false` com mensagem de erro, nunca trava.
- O backend nunca altera o pacote.

### 3.3 Adaptador ToolEngine (RHI)

- Origem do recorte e do viewport: **topo-esquerdo**, como o readback. O Diligent em OpenGL espelha o recorte em Y
  para alvos offscreen; a compensação está em `engine/rhi_diligent` (não no adaptador). Validado com teste de pixels
  em OpenGL e Vulkan.
- Blend: premultiplicado (`ONE`, `INV_SRC_ALPHA`).
- O atlas é RGBA8 com RGB = 1 e A = cobertura. O pixel (0,0) é branco reservado: o atlas precisa de padding ≥ 1.
- O adaptador não depende de `tool::debug`. Erros vão para um `ErrorSink` (ou stderr).

## 4. Camadas de teste

Cada camada tem um critério objetivo. "Passando" significa que o comando indicado rodou com zero falhas nesta data.

| Camada | O que verifica | Onde roda | Estado | Dor de ImGui relacionada |
|---|---|---|---|---|
| **L0 Núcleo** | Layout, Flex, roteamento de eventos, texto (medidas e quebra), docking (modelo) | `ctest` no pacote (`SpriteUI.CoreTests`) | **Passando: 819 verificações** | Base de tudo |
| **L1 Texto (atlas)** | Atlas stb_truetype: cobertura, geometria do 'A', espaço sem área, clusters UTF-8, '?' de fallback, tab, controles, rejeição de entrada inválida, quebra de linha | `SpriteUI.TruetypeAtlas` (`SPRITEUI_WITH_TEXT_STB=ON`) | **Passando: 55/55** | Texto ruim |
| **L2 Docking (modelo)** | Dividir nós, abas, soltar no centro, limites de divisão | `tests/test_main.cpp` (`TestTabsAndDocking`, testes de `UIDockNode`) | **Passando (parte do L0)** | Docking mal feito |
| **L3 Docking (interação)** | Arrasto com limiar, zonas de soltura (borda = dividir, centro = aba, fora = flutuar), cancelamento, sem movimento sem entrada, janela presa na tela | `SpriteUI.DockInteraction` (`tests/test_dock_interaction.cpp`) | **Passando: 70/70** (controlador e aplicação na árvore). Ainda não testado com janelas reais nem ligado ao editor | Janelas bugando |
| **L4 Campo de texto** | Caret, seleção, inserir/apagar, UTF-8 (apagar 'é' inteiro), Shift+setas, Home/End, Ctrl+A/C/X/V (via operações), Undo, texto persiste por 1000 quadros sem entrada | `SpriteUI.TextEdit` (`tests/test_text_edit.cpp`) | **Passando: 43/43** (modelo `TextEditState`). Ainda não ligado ao `UIEventRouter`, sem renderização do campo, sem clipboard do sistema | Campos apagando sozinho (regra do modelo coberta; integração pendente) |
| **L5 Backend RHI (contrato)** | Pipeline e chamadas corretas sobre o dispositivo Null: lotes, recorte de área zero, ordem de uso, rejeição de estado inválido, cantos arredondados | `tool_tests` (`spriteui_rhi_*`, 4 testes) na engine | **Passando: 4/4** (dentro de 154/154) | Base de render |
| **L6 Pixels GPU** | Sólido, recorte, blend premultiplicado, glifo com cobertura 255 e 128, fora do quad, fundo | `spriteui_gpu_test opengl` e `spriteui_gpu_test vulkan` (engine) | **Passando em OpenGL (Mesa) e Vulkan (lavapipe)** | Interface feia (base de cor e blend) |
| **L7 Sprites 2D** | Sprite com UV de atlas, tint, rotação e escala no pacote; sprites sem clicar nem mudar | Visual (`tests/test_visual.cpp`) e L6 | **Parcial**: sprite com UV único coberto por L6; rotação e escala não testadas em GPU | Requisito de jogo |
| **L8 Partículas 2D** | Emissor determinístico (semente fixa): contagem, vida, curva de alfa, pixels no quadro | Ainda não existe | **Pendente** (o sistema de partículas não está implementado) | Requisito de jogo |
| **L9 Desempenho** | Orçamento de tempo de quadro com N widgets, e tempo de layout | `SPRITEUI_BUILD_BENCHMARKS` (existe, sem orçamento fixado) | **Pendente**: sem números de referência | "Demora nas respostas" |
| **L10 Consumo** | O pacote instalado compila num projeto externo | `tests/package_consumer` | Existe; executar a cada release | Independência |

Como rodar (no pacote, sem engine):

```
cmake -S . -B build -DSPRITEUI_BUILD_EXAMPLES=OFF -DSPRITEUI_WITH_TEXT_STB=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Como rodar a GPU (na engine, com `TOOL_WITH_DILIGENT=ON`):

```
cmake --build build/gpu-gl --target spriteui_gpu_test tool_tests
xvfb-run -a ./build/gpu-gl/bin/spriteui_gpu_test opengl
./build/gpu-gl/bin/spriteui_gpu_test vulkan
./build/gpu-gl/bin/tool_tests
```

## 5. Roteiro de implementação

Cada fase tem um critério de aceitação. Não avance sem ele.

### Fase A: base e separação (concluída)
- [x] Núcleo independente, sem dependências obrigatórias. Critério: configurar e testar sem o engine. **Ok.**
- [x] Atlas de texto stb_truetype, opcional. Critério: L1 passa. **Ok.**
- [x] Adaptador RHI movido para o pacote, com sink de erro e sem `tool::debug`. Critério: engine compila e
      passa L5 e L6. **Ok** (GL e Vulkan).
- [x] Recorte e blend corretos sob GL e Vulkan. Critério: L6. **Ok.**

### Fase B: janelas e docking (controlador pronto; integração pendente)
Passos:
1. [x] `DockDragController` (`include/SpriteUi/Widgets/DockInteraction.hpp`): recebe pressionar, mover, soltar e
   cancelar; devolve `None`, `Dock`, `Tab` ou `Float`. Não renderiza nem altera a árvore.
2. [x] Zonas de soltura por fração da borda (`ComputeDropSlot`, padrão 25%); centro vira aba.
3. [x] Regra: só há ação com arrasto que passou do limiar (6 px). Clique e tremor não fazem nada.
4. [x] `ClampWindowToScreen` mantém a janela flutuante na tela e reduz o tamanho se ela for maior que a tela.
5. [~] Aplicar a ação na árvore: feito em `DockOps` (`ApplyDropResult`, `RemoveFromDock`, `FindLeafContaining`), com colapso de folhas vazias e checagem do alvo antes de tirar a janela. Falta ligar o `UIEventRouter` ao controlador.
6. [ ] Testes com janelas reais (SDL ou Xvfb): arrastar, soltar, redimensionar, trocar aba.
Critério (L3): o item 5 e o 6 precisam passar. Hoje só o controlador (itens 1 a 4) está provado.

### Fase C: campo de texto (modelo pronto; integração pendente)
Passos:
1. [x] Modelo de edição: `TextEditState` (`include/SpriteUi/Widgets/TextEdit.hpp`). Texto UTF-8, caret e âncora em offset de byte sobre fronteira de codepoint, histórico de Undo (limite 256).
2. [~] Operações prontas: caracteres (controles descartados), Backspace, Delete, setas, Home/End, Shift+setas, Ctrl+A (`SelectAll`), Cut/Copy/Paste, Undo. Falta mapear teclas do `UIEventRouter` e o callback de clipboard do sistema.
3. [x] Estado é o dono do texto: só operações explícitas mudam o texto. Teste de 1000 quadros de leitura comprova que nada muda. Renderização do campo ainda não existe.
4. [x] Sem IME nesta fase. Limite documentado no cabeçalho de `TextEdit.hpp`.
Critério (L4): cada operação tem um teste (feito, 43/43); "texto persiste": digitar, passar 1000 quadros sem entrada, texto e caret
idênticos ao final (feito no modelo; falta repetir com o campo real na janela).
Verificação de mutação: quebrar o recuo UTF-8 derruba 3 verificações (`Backspace`, `setas`); restaurado depois.

### Fase D: sprites e partículas 2D (pendente)
Passos:
1. Sprites: confirmar rotação e escala no pacote (`transformRow0/1`) com teste de pixels (L6 estendido).
2. Emissor de partículas com semente fixa: taxa, vida, velocidade, gravidade, curva de cor e alfa.
3. Partículas viram `SpriteInstanceGPU` (um lote por emissor) e passam pelo mesmo backend.
Critério (L8): mesma semente gera os mesmos pixels em GL e Vulkan; contagem de vivas bate com a taxa × vida.

### Fase E: desempenho (pendente)
1. Fixar cenários de referência (ex.: 500 e 2000 widgets, 10000 quadrados de partícula).
2. Medir tempo de layout e de montagem do quadro numa máquina de referência e registrar em `docs/`.
3. Definir orçamento (ex.: quadro de UI abaixo de 2 ms no cenário de 500 widgets). Critério: o benchmark falha se
   passar do orçamento.

### Fase F: aparência (pendente)
1. Tokens de tema (cores, raios, espaçamento, fontes) em um arquivo só.
2. Testes de imagem (snapshots em `tests/snapshots`) para os widgets principais.

### Fase G: migração do editor (depois de A a E)
1. Migrar uma tela por vez, começando por Configurações ou Console, com ImGui como plano B.
2. Remover ImGui só quando todas as telas tiverem passado nas camadas L3 a L6.

## 6. Como adicionar um backend novo

1. Implemente `IRenderBackend` em `adapters/<nome>/` (header-only se possível).
2. Respeite os contratos da seção 3 (origem topo-esquerdo, premultiplicado, atlas com pixel (0,0) branco).
3. Adicione uma opção de CMake desligada por padrão, e um alvo `SpriteUi::<Nome>`.
4. Escreva as camadas L5 (contrato com dispositivo falso) e L6 (pixels) para o novo backend.
5. Documente as dependências em `docs/THIRD_PARTY.md`.

## 7. Consumo em outro projeto

```cmake
include(FetchContent)
FetchContent_Declare(spriteui GIT_REPOSITORY <url-do-pacote> GIT_TAG <commit-fixo>)
set(SPRITEUI_WITH_TOOLENGINE_RHI OFF CACHE BOOL "" FORCE)   # ou ON, com o alvo do RHI
FetchContent_MakeAvailable(spriteui)
target_link_libraries(meu_app PRIVATE SpriteUi::SpriteUI)
```

Fixe sempre um commit. Nunca use uma branch como versão.

## 8. Checklist de entrega de release

- [ ] Configura e compila sem o engine (`cmake -S . -B build`).
- [ ] L0 e L1 passam (`ctest`).
- [ ] L10 passa (consumo externo).
- [ ] Cabeçalhos públicos compilam sozinhos (`SPRITEUI_CHECK_HEADERS=ON`).
- [ ] Nenhum include de `tool/`, SDL ou Diligent em `include/SpriteUi/`.
- [ ] `docs/THIRD_PARTY.md` atualizado com as dependências novas.
- [ ] README e este guia refletem o estado real (não o planejado).

## 9. Riscos conhecidos

- **Duplicação de shaders.** `shaders/toolengine/` e `assets/shaders/` da engine são cópias. Qualquer alteração precisa
  ser feita nas duas. Melhoria planejada: teste que compara as duas pastas.
- **Orientação em OpenGL.** O recorte do Diligent em OpenGL precisou de compensação para alvos offscreen. O backbuffer
  do OpenGL não foi testado por pixels.
- **Vulkan testado só em lavapipe (CPU).** Driver real de GPU ainda não foi testado. D3D12 não foi testado.
- **Sem IME, sem bidi, sem kerning** nesta etapa. Estão fora do escopo da Fase C e do atlas atual.
- **Partículas e campo de texto ainda não existem.** Os testes L4 e L8 são o critério de aceitação, não resultado.
