# Falhas e avisos encontrados na engine

Levantamento feito durante a documentacao das classes, structs e funcoes da engine (2026-11-22).
Nenhum destes pontos foi corrigido: o objetivo e priorizar a correcao. Cada item aponta o local
no codigo, onde o aviso `Atenção:` tambem esta escrito como comentario.

Gravidade: **Alta** = pode causar acesso invalido, corrupcao ou travamento em uso normal.
**Média** = comportamento errado ou perda de dados em casos previstos. **Baixa** = latente, sintoma ou melhoria.

## Alta

| # | Local | Problema | Correcao sugerida | Status |
|---|-------|----------|-------------------|--------|
| 1 | `engine/input/include/tool/input/input.hpp` (~149), `InputSystem::read()` | O caso `PadButton` nao confere `b.code` com `PadButton::Count`. `buttons[]` tem 15 posicoes; codigo >= 15 le fora do array. `Key` e `Mouse` ja tem a checagem. | Adicionar `if (b.code >= PadButton::Count) ...` igual aos outros casos. | **Corrigido** |
| 2 | `engine/narrative/include/tool/narrative/narrative.hpp` (~298 e ~447), `run(Thread&)` / `update()` | `run(t)` guarda referencia para `threads_[i]`. Se `observe` dispara `tryStart`, `push_back` em `threads_` pode invalidar a referencia. | Guardar o indice, ou reservar `threads_` antes, ou usar `std::deque`. | Pendente |
| 3 | `engine/ecs/include/tool/ecs/world.hpp` (~72 e ~119) | Ponteiros e referencias de componentes ficam invalidos quando outro componente do mesmo tipo e adicionado (o vetor cresce). `each` com `fn` que adiciona componente tem o mesmo risco. | Guardar entidade e buscar de novo, ou usar armazenamento com estabilidade de endereco. Documentar a regra em `each`. | Pendente |
| 4 | `engine/core/include/tool/core/parallel.hpp` (~23), `ParallelPool::run` | Excecao em `fn` na thread chamadora deixa `job_` apontando para lambda destruida (UB). Excecao em worker chama `std::terminate`. | Capturar excecoes, guardar a primeira e relancar apos o join. | Pendente |
| 5 | `engine/rhi_diligent/src/diligent_device.cpp` (~212), `destroy()` | Destruir o handle do backbuffer nao zera `backbufferHandle_`; `isBackbuffer()` continua verdadeiro para handle morto. | Zerar `backbufferHandle_` ao destruir. | **Corrigido** |

## Média

| # | Local | Problema | Correcao sugerida | Status |
|---|-------|----------|-------------------|--------|
| 6 | `engine/mod/include/tool/mod/mod.hpp` (~240), `guarded()` | Quando um mod estoura o orcamento de tempo, recebe falha mas os efeitos NAO sao desfeitos, ao contrario do cabecalho. | Desfazer efeitos ao estourar o tempo, ou corrigir o cabecalho. | Pendente |
| 7 | `engine/mod/include/tool/mod/mod.hpp` (~206), `call()` / `invoke()` | A cadeia guarda ponteiros para o vetor de mods. Um `modFunction` chamado durante a execucao pode invalidar esses ponteiros. | Copiar a cadeia ou usar indices. | Pendente |
| 8 | `engine/events/include/tool/events/events.hpp` (~46), `tryFire` | `messageId` e marcado como visto antes das demais checagens. Recusa posterior (ciclo, profundidade, cooldown) faz a repeticao virar `Duplicate`. | Marcar `messageId` so depois que o evento for aceito. | **Corrigido** |
| 9 | `engine/events/include/tool/events/events.hpp` (~122), `emit` | Se um handler lancar excecao, a cadeia de causa (push/pop) nao e desempilhada. | Usar RAII para o push/pop. | **Corrigido** |
| 10 | `engine/physics/include/tool/physics/newtoon_world.hpp` (~259), `TrimEvents` | Passando de 20000 eventos, apaga os 10000 mais antigos sem aviso. Perde Enter/Exit se `ClearEvents()` nao for chamado a cada quadro. | Avisar no log ao cortar, ou aumentar o limite e documentar. | Pendente |
| 11 | `engine/physics/include/tool/physics/newtoon_jobs.hpp` (~87), `ParallelFor` | Chamar `ParallelFor` de dentro de `fn` pode travar: o worker espera blocos sem thread livre. | Executar a chamada aninhada na thread atual, ou proibir e checar. | Pendente |
| 12 | `engine/physics/include/tool/physics/newtoon_types.hpp` (~117), `SetMassFromShape` | Esfera de raio 0 gera inercia zero e inverso infinito. | Limitar o raio minimo ou recusar a forma. | **Corrigido** |
| 13 | `engine/rhi_diligent/src/diligent_device.cpp` (~79), `createBuffer` | Dado inicial e ignorado em buffers dinamicos e uniform, sem aviso. | Copiar com `updateBuffer` ou avisar. | Pendente |
| 14 | `engine/rhi_diligent/src/diligent_device.cpp` (~108), `createTexture` | Dado inicial so e copiado para RGBA8; outros formatos ignoram em silencio. | Recusar o formato ou implementar a copia. | Pendente |
| 15 | `engine/rhi_diligent/src/diligent_device.cpp` (~459), `setTargets` | Nao atualiza `targetW_`/`targetH_`, viewport nem scissor. Alvos de outro tamanho usam valores anteriores. | Atualizar os tres ao trocar o alvo. | **Corrigido** |
| 16 | `engine/rhi/include/tool/rhi/null_device.hpp` (~74) | `NullDevice` nao sobrescreve `setTargets()` nem `setStorage()`; esses comandos nao sao validados. | Sobrescrever e validar. | Pendente |
| 17 | `engine/render/include/tool/render/gpu_renderer.hpp` (~121, comentario do init e `mk`) | No OpenGL, se `glsl/<nome>.430.glsl` nao existir, o erro e so reportado e o shader e criado com fonte vazia; o init continua. | Falhar o init ou usar um shader de fallback explicito. (Nao marcado como `Atenção` no codigo.) | Pendente |
| 18 | `engine/render/include/tool/render/soft_renderer.hpp` (~547), `pointShadowFor()` | Acha a luz pela posicao. Duas luzes na mesma posicao sempre usam o mapa de face da primeira. | Achar pelo indice da luz. (Nao marcado como `Atenção` no codigo.) | Pendente |
| 19 | `engine/platform/src/sdl_window.cpp` (~44), `~SdlWindow` | Chama `SDL_Quit()`, que afeta qualquer outra `SdlWindow` aberta. | Contar as janelas e chamar `SDL_Quit` so na ultima. | Pendente |
| 20 | `engine/platform/src/sdl_window.cpp` (~54), `pump()` | Se `SDL_OpenGamepad` falhar, o slot fica nulo, mas `DeviceChanged` e disparado. | Disparar so quando abrir. | Pendente |
| 21 | `engine/net/include/tool/net/net.hpp` (~33) e `engine/assets/include/tool/assets/inflate.hpp` (~133) | Tabela de CRC e tabelas de Huffman sao inicializadas na primeira chamada sem sincronizacao (corrida entre threads). | Inicializar com `std::call_once` ou funcao estatica local. | Pendente |
| 22 | `engine/debug/include/tool/debug/debug.hpp` (~93), `count` | Le `counts_` sem o mutex. | Usar o mutex na leitura. | **Corrigido** |
| 23 | `engine/core/include/tool/core/log.hpp` (~41), `log` | Sem mutex: chamadas concorrentes (ex.: de jobs) podem intercalar linhas ou correr com `setSink`. | Proteger com mutex. | **Corrigido** |
| 24 | `engine/core/include/tool/core/json.hpp` (~159), numeros | `strtod` depende do locale: em locale com virgula decimal o numero e recusado (falha segura). | Usar `std::from_chars` ou fixar o locale. | Pendente |
| 25 | `engine/core/include/tool/core/json.hpp` (~274 e ~281), `write` | Numeros `inf`/`NaN` saem como texto (`inf`/`nan`), que nao e JSON valido. | Escrever `null` ou recusar. | Pendente |
| 26 | `engine/scene/include/tool/scene/scene.hpp` (~142), `spawn` | Ao reaproveitar um objeto dormente, `setSimulating` nao e reiniciado (ao contrario de `destroy()`). | Reiniciar o estado no reuso. | **Corrigido** |

## Baixa

| # | Local | Problema | Correcao sugerida | Status |
|---|-------|----------|-------------------|--------|
| 27 | `engine/narrative/include/tool/narrative/narrative.hpp` (~73), `compile` | `nodes_`, `out_`, `varIdx_`, `pcOf_` e `stack_` nao sao limpos entre chamadas. Hoje cada chamador cria um `Compiler` novo. | Limpar no inicio de `compile`. | **Corrigido** |
| 28 | `engine/core/include/tool/core/tags.hpp` (~18), `TagMask` | Ids >= 128 nao sao validados e caem na mascara (ex.: `kInvalidIndex` aliasa um bit). | Validar o id. | **Corrigido** |
| 29 | `engine/platform/include/tool/platform/sdl_window.hpp` (~25), `nativeHandle()` | Devolve `SDL_Window*`, mas `IWindow` promete HWND/ANativeWindow/canvas. Latente: nenhum chamador usa hoje. | Ajustar a interface quando houver chamador. | Pendente |
| 30 | `engine/audio/include/tool/audio/decode.hpp` (~14) | Usa `std::tolower` sem `<cctype>`; compila por include indireto. | Incluir `<cctype>`. | **Corrigido** |
| 31 | `engine/platform/src/sdl_window.cpp` e `engine/mod/include/tool/mod/mod.hpp` | Usam `std::max` / `std::sort` / `std::erase_if` sem incluir `<algorithm>`. | Incluir `<algorithm>`. | **Corrigido** |

## Como usar este documento

- Corrigir pela ordem: Alta, depois Média, depois Baixa.
- Ao corrigir, trocar o comentario `Atenção:` correspondente por uma descricao do comportamento correto.
- Itens 17 e 18 nao tem a marcacao `Atenção:` no codigo; adicionar quando forem tratados.
