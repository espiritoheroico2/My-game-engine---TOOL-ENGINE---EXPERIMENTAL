# Changelog

## 0.7.18-beta (em andamento) — editor de script, oclusao e ray tracing

**Ultimas mudancas: estabilidade do editor, ajuda com mais exemplos**
- Interface: campos com o mesmo rotulo dentro de um laco (Narrative) agora tem ID por no selecionado: trocar de no com o campo em foco nao mistura o texto.
- Interface: janela solta (fora do encaixe) que sair da area do editor volta para dentro, com pelo menos 80 px visiveis.
- Avisos de desempenho (TOOL-W301 a W307): ficam em silencio por 240 quadros depois de abrir uma cena, para nao acusar o carregamento (compilacao de shaders, pipelines) como problema.
- Play: Tools (Move, Jump, Spin, Follow, On Collision) nao rodam em corpos fora da simulacao por distancia.
- Ajuda: 3 exemplos novos (plataforma girando e trampolim, barra de vida com o Narrative, salvar e carregar), em portugues e ingles.

**Ultimas mudancas anteriores: simulacao por distancia e ECS**
- ECS: `each` reaproveita o vetor de indices (sem alocacao por chamada) e aceita chamadas aninhadas. Novos: `forEachDense<T>` (varredura contigua do componente), `parallelEach<Ts...>` (divide as entidades entre os nucleos pelo pool persistente, para fn que escreve so nos proprios componentes) e `count<T>()`.
- ECS: flag de simulacao por entidade (`setSimulating` / `isSimulating`). Diferente de dormente: a entidade continua visivel e nas consultas; so a simulacao a ignora.
- Simulacao por distancia (fisica): em Projeto > Fisica, "Simulation radius" (0 = tudo). Corpos dinamicos fora desse raio em volta da camera param (sem gastar CPU) e voltam ao se aproximar. Histerese de 10% evita piscar na borda. Corpos parados sao acordados ao voltar. Os Tools (Move, Jump, Spin, Follow) nao rodam em corpos fora da simulacao, para nao acordar o corpo de novo. Os scripts ainda nao tem VM no Play, entao nao ha o que pular neles por enquanto. O painel Live mostra quantos corpos estao simulados e quantos estao fora do raio.
- Testes: 4 novos em `tests/test_ecs.cpp` (iteracao aninhada, `forEachDense`, `parallelEach` igual a `each` com 5000 entidades, flag de simulacao). `tool_tests`: 147 testes, 0 falhas.

**Mudancas anteriores: Vulkan, luz e IBL**
- Correcao (D3D12): `pbr.ps` falhava com `X4610 SRV binding ranges overlap for range T[4:4]`. `IblBrdfLut` (IBL) e `CsmMap1` (sombra) usavam `t4`, e `IblPrefiltered` e `CsmMap0` usavam `t3`. Os registradores de IBL agora sao `t15` e `t16`. No Vulkan isso passava despercebido; no D3D12 o compilador recusa. Os avisos `X4000` (variavel possivelmente nao inicializada) continuam: nao derrubam a compilacao, mas ainda serao limpos.
- Verificado: `gpu_test vulkan` e `ctest` iguais. D3D12 ainda nao foi compilado aqui (sem `dxc` nesta maquina): precisa testar no Windows.
- Frame time (1/3): o Profiler mostra tambem o "trabalho da CPU" (sem a espera do VSync), com grafico e desvio padrao proprios.
- Frame time (2/3): movimento do mouse sem botao nao conta mais como atividade. Antes, cada pixel de mouse refazia as duas vistas por 3 quadros.
- Frame time (3/3): o renderizador CPU criava threads a cada quadro (raster, sombras, SSAO). Agora usa o pool persistente (`tool::parallelFor`). O bloom reaproveita os buffers.
- Sombras: Scene e Game dividiam um renderizador e uma assinatura, entao o shadow map de uma apagava o da outra e nada era reaproveitado. Agora cada vista tem o seu renderizador e a sua assinatura (CPU). A GPU ainda e compartilhada: pendente.
- Avisos de recurso no Console (TOOL-W301..W307): oscilacao do trabalho da CPU, pico de zona, objetos, triangulos, pixels, sombras refeitas a cada quadro e memoria. Dispara na transicao e repete a cada 10 s enquanto continuar. Limites ajustaveis em `PerfWarnings::Limits`.
- Janelas "Edit Mesh" e "Paint Mesh" (menu Window): so aviso de recurso em desenvolvimento.
- OpenGL (1/2): os shaders agora sao GLSL 430 gerados offline de HLSL (`tools/shaders/build_glsl.py`: glslang -> SPIRV-Cross), em vez do tradutor interno do Diligent, que nao aceitava o codigo. Os compute da Light Prism tambem (`assets/shaders/lightprism/glsl/`; a Light Prism carrega o GLSL no OpenGL). Correcoes: nomes de textura (`SPIRV_Cross_Combined...`), inversao do Y do clip nos vertex shaders (exceto a tela cheia), e a entrada do SSAO (`PSMain` -> `SsaoMain`: o glslang gerava um shader vazio sem erro). Verificado: objetos (IoU 1.000), texturas, SSAO e sombra do sol batem com o Vulkan.
- OpenGL (2/2): pendente: bloom e sombra pontual ainda diferem do Vulkan (aviso TOOL-W142). `gpu_test opengl` reporta PENDENTE nesse caso, nao falha.
- Testes: `ctest` 4/4, `tool_tests` 140/140, selftest do editor 224/224, `gpu_test vulkan` igual. Medidas de frame time nao foram feitas aqui (sem GPU real): precisam ser medidas no Windows.
- Correcao (Vulkan): o pipeline PBR falhava com "dynamic uniform buffers (10) excede o limite do dispositivo (8)" (`TOOL-E150`/`TOOL-E153`) em GPUs que aceitam so 8 buffers uniformes dinamicos. O Vulkan conta cada cbuffer por estagio, e o PBR tinha `FrameBuffer` e `ObjectBuffer` nos dois (VS e PS). Agora o pixel shader nao le esses dois: o vertex shader calcula o que o PS precisa e passa por interpolacao (valores por objeto com `nointerpolation`). Total: 8.
- Teste: `gpu_test` (Vulkan/llvmpipe) passa com os mesmos numeros de antes, e `ctest` 4/4. Teste em GPU real ainda pendente.
- Nao usar `MUTABLE` nesses recursos: a tentativa quebrou sombras e texturas no `gpu_test`.
- Correcao (editor, API grafica): quando a inicializacao de Vulkan/OpenGL falhava, o editor tentava de novo a cada quadro e gravava a API como CPU no projeto. Agora a escolha do usuario fica em `apiRequested` (radio e projeto), e a falha so troca a renderizacao para CPU.
- OpenGL: aviso `TOOL-W141` (antes erro `TOOL-E141`): a iluminacao Light Prism ainda nao tem shaders GLSL, entao o OpenGL usa o renderizador CPU.
- Avisos de validacao do driver (`TOOL-I201` Vulkan, `TOOL-I202` OpenGL, `TOOL-I203` D3D12) viraram informativos.
- Luz pontual/spot: "Sync rotation with Transform" faz a direcao seguir o eixo -Y do objeto (ou do objeto com o nome digitado em "Object name to follow"; vazio = o proprio objeto). Aviso quando o nome nao existe. Salvo no projeto (`syncRot`, `target`) e coberto pelo selftest do editor.
- IBL especular: `pbr.hlsl` agora calcula o ambiente como `Ibl::IblEvaluate` da Light Prism (difuso com `kD` + especular split-sum). A LUT da BRDF e gerada uma vez (`sharedBrdfLut()`, 32x32) e enviada como textura (slot 10 do PBR); o renderizador CPU usa a mesma tabela.
- Limitacao: o ambiente da engine ainda e uniforme (cor em SH9). Com ambiente uniforme o especular e exato; com mapa de ambiente sera preciso o cubemap pre-filtrado, que ainda nao existe.
- Efeito nos numeros do `gpu_test`: SSAO passou de 1344 (GPU) x 1339 (CPU) para 1308 x 1288 pixels escurecidos, por causa do `kD`. Sombra pontual e texturas continuam iguais na GPU e na CPU.
- Traducoes: 4 frases novas em pt-BR e es (`tools/gen_lang_packs.py`).
- Emissivos: qualquer objeto 3D com `MeshRenderer` pode emitir luz. Cor de emissao e intensidade (0 a 50) no inspector. O emissivo entra na cor final do pixel (GPU e CPU), sem iluminar o entorno.
- Bloom (Settings > Lighting): desligado por padrao. Limiar (0 a 1) e forca (0 a 4). Passa so o que esta acima do limiar, em meia resolucao, com blur gaussiano de 9 taps. No GPU sai em passes fullscreen (`bloom.hlsl`); no CPU, `tool::applyBloomRgba8`. Projeto salva `bloom`, `bloomThreshold` e `bloomStrength`.
- Correcao (bloom GPU): a composicao escrevia no mesmo alvo que lia a cena, o que escurecia a imagem. Agora tem alvo proprio (`outFull_`).
- Teste: `gpu_test` mede o halo ao redor de um cubo emissivo: GPU 1867 x CPU 1878 pixels, dentro de 40%. `tests/test_render.cpp` tem 2 testes de bloom no CPU (halo espalha e imagem escura nao muda).
- Limitacoes: o skybox nao entra no bloom do caminho GPU (o CPU aplica depois do skybox). Emissivo nao ilumina objetos vizinhos (so o brilho). Testado so em llvmpipe; visual no editor e em GPU real ainda pendentes.

**Mapa de teste de chunks (view distance)**
- Menu Criar > "Test map of chunks...": gera um mapa quadrado de chunks de terreno (1 a 256 por lado, 16x16x16 blocos cada), centrado na origem. Substitui os chunks que ja existem.
- Linha de comando: `--map N` (com `--project`). O `--voxel N` continua sendo o benchmark de quadros.
- Teste: `chunk map: ...` no selftest do editor (espacamento, centro e contagem).
- Limitacao: cada chunk e uma entidade. O custo cresce com o quadrado do lado: use Draw distance (Settings > Render) para ver so o perto.

**Iluminacao: otimizacoes (sem mudar os 4 perfis de qualidade)**
- Sombras de luzes pontuais e spot nao dependem mais da camera: quando so a camera anda, os mapas de face sao reaproveitados (CPU e GPU). Teste: `point_shadow_maps_reused_when_only_the_camera_moves_give_the_same_image` (imagem identica a renderizar do zero).
- Luzes pontuais/spot so sao removidas e readicionadas na LightPrism quando a lista muda. Teste: `light_rig_keeps_point_lights_registered_and_follows_changes`.
- Oclusao ambiente por raios (RT AO) e a conversao final de cor agora rodam em varias threads (faixas de pixels). O resultado e o mesmo, porque cada raio depende so do indice do pixel.
- Correcao: objetos que projetam sombra de luz pontual nao eram protegidos do culling quando o sol estava desligado. Agora sao sempre desenhados.
- Medicao (renderizador CPU, 640x360, Release, maquina de 2 nucleos): perfil 0 ~65-75 ms, perfil 1 ~65-75 ms, perfil 2 ~80-90 ms, perfil 3 ~130-145 ms por quadro. RT AO com 24 raios ~230-300 ms. Com a camera andando, o reuso das sombras pontuais economiza ~5-15%. Mais nucleos reduzem o RT AO.

**Janela de edicao de script**
- "Edit script" no Inspector abre uma janela propria (`apps/editor/script_window.hpp`), com editor multilinha, Ctrl+S para salvar e Ctrl+Espaco para a lista completa.
- Autocomplete da API lido do `toolengine.d.ts` (a mesma fonte do VS Code): funcoes, metodos e propriedades. Aparece ao digitar 2 letras; clique aceita.
- Snippets: os exemplos do Help (aba "script_examples") viram o menu "Snippets" e o arquivo `.vscode/toolengine.code-snippets`, gerado a partir do Help (nao pode sair de sincronia).
- Verificador de scripts na propria janela (erros e avisos com a linha).
- Limitacao: a interacao do ImGui (callback do campo, clique para completar) foi verificada so por compilacao e pelo selftest da logica; precisa de teste manual na janela.

**Help: exemplos de script**
- 40 exemplos novos (numeros 14 a 53) em portugues e ingles, rodados pelo selftest do editor (59 blocos sem erro).
- Exemplos 1 a 13: comentarios do bloco em ingles traduzidos. Exemplos que exigem objetos com nome certo (Porta, Alvo, Jogador, Inimigo 3, tag inimigo) dizem isso no texto.

**Oclusao (occlusion culling)**
- `engine/render/include/tool/render/occlusion.hpp`: buffer de profundidade grosso (1/8 da resolucao) com os triangulos dos objetos. Um objeto so e pulado se a caixa dele estiver inteira atras de uma superficie grande que cobre as celulas por inteiro.
- Conservadora: na duvida, desenha. Um par de triangulos planos e convexos (face de cubo, plano) conta como quad: a celula e coberta inteira, mesmo com a diagonal passando por ela. Triangulos soltos, nao planos ou nao convexos ainda caem na limitacao da diagonal.
- Editor: pass de oclusores a cada quadro (malhas com ate 2048 triangulos), so em camera de perspectiva. Objetos que projetam sombra nunca sao pulados. Ativa por "Occlusion culling" em Settings (`render.occlusionCull`, padrao ligado).
- Teste: `occlusion_buffer_hides_box_behind_wall` e `occlusion_culling_does_not_change_the_image` (a imagem e identica com e sem a oclusao).

**Ray tracing (CPU)**
- Settings > Lighting: "Ray-traced ambient occlusion (CPU)", com raios por pixel (1 a 64) e raio em metros. Substitui o SSAO quando ligado.
- Usa o BVH da Light Prism (`LightPrism::Rt`) sobre os triangulos dos objetos visiveis. O BVH e refeito so quando a cena muda (malhas e transformacoes).
- Limitacoes: so o renderizador de software (a GPU segue com SSAO); sombras do sol e voxels nao sao ray traced; a primeira montagem do BVH pesa em cenas grandes.
- Teste: `rt_ambient_occlusion_darkens_plane_next_to_a_wall`. Settings de oclusao e RT tem checagem de ida e volta no selftest do editor.

## 0.7.17-beta — AO (SSAO), hierarquia e multi-material na importacao, projeto mais enxuto

**Correcao: sombras de luzes pontuais e spot no editor**
- Antes, o editor zerava `castShadow` de toda luz pontual/spot ao montar a iluminacao, e nao havia controle no inspector. Logo, as sombras de 0.7.16 nunca apareciam no editor (so funcionavam em testes diretos).
- Agora cada luz pontual/spot tem "Projeta sombra" no inspector e o flag e salvo no projeto. Luzes criadas pelo editor nascem com ele ligado; cenas antigas (sem o campo) precisam marcar a caixa.
- Continua valendo o limite de Settings > Lighting (ate 4, as mais proximas da camera).
- Teste: `point light castShadow survives save and load` no selftest do editor.

**Ambient occlusion (SSAO), CPU e GPU**
- Porta do `LightPrism::Ssao` (`Post/Ssao`): AO bruto por kernel + desfoque bilateral. `LightingSettings`: `ssao` (liga/desliga), `ssaoKernel` (1 a 32), `ssaoRadius`, `ssaoBias`, `ssaoPower`. Aparece em Settings > Lighting e e salvo no projeto.
- CPU: `SoftRenderer` guarda cor direta e ambiente separados, profundidade e normal por pixel; a AO e aplicada so ao ambiente (convencao da Light Prism).
- GPU: prepasse que grava normal e profundidade (RGBA16F), dois passes de tela cheia (`assets/shaders/ssao.hlsl`) e o `AoMap` multiplicando o ambiente no `pbr.hlsl`.
- Correcao no caminho da CPU: triangulos com vertice atras do plano near eram descartados, o que sumia com o chao em alguns angulos. Agora sao recortados contra o near.
- Teste: `gpu_test` compara a AO na GPU e na CPU (1344 x 1339 pixels escurecidos, Vulkan/lavapipe).
- Limitacoes: a SSAO da CPU usa o `ComputeSsao` single-thread da Light Prism (nao medido em resolucoes altas); sem meia resolucao exposta; D3D12 nao compilado neste ambiente.

**Hierarquia de modelos importados**
- Antes: o importador achatava a cena e assava a matriz de mundo nos vertices, e o editor criava um objeto so. Agora cada no do arquivo vira um objeto filho, com a transformacao local (TRS) e o link `Parent` do editor.
- `ModelData::nodes` (pai, matriz local, partes) em glTF e FBX. Nos com escala espelhada ou cisalhamento sao assados pelo `normalizeNodes()`, que passa a transformacao para os filhos.
- O objeto "pai" leva o nome do arquivo. Modelos antigos (sem `node` no `.scene.json`) continuam carregando como antes.

**Multi-material**
- Cada parte (primitive no glTF, material por poligono no FBX) desenha com o seu material. FBX: antes so o primeiro material era usado; agora a malha e separada por `LayerElementMaterial`.
- Inspector: em um no de modelo, cada parte mostra o material do arquivo e um interruptor "Sobrepor material" (cor, rugosidade, metalico), salvo como `partMaterials` (componente `PartOverrides`).
- Limitacao: a textura (`Texture`) do objeto ainda vale para todas as partes, nao por parte.

**Projeto mais enxuto (medido)**
- Repositorio sem build/ e .git: 263 MB -> 223 MB (fontes e vendorizados; as remocoes abaixo).
- Removidos: `third_party/DiligentCore/ThirdParty/Vulkan-Headers/registry` (12 MB, so para instalar os headers), `third_party/DiligentCore/BuildTools/FormatValidation` (6,4 MB, so para validar formatacao, desligada neste build), `out/` e `build_log.txt` (gerados), e 6 capturas de `third_party/LightPrism/docs/images` que nenhum `.md` cita (cópias em scratchpad da sessao; a LightPrism as regenera com as ferramentas dela).
- Imagens da LightPrism recomprimidas sem perda (PNG otimizado, pixels conferidos iguais): 24,6 MB -> 1,5 MB.
- Executavel Linux (Release): 24,2 MB -> 20,7 MB com `strip`. Nao aplicado ao build; no Windows medir o .exe final.
- Nao removido por ser usado ou por exigir mudar CMake de terceiros: glslang (45 MB, compila shaders), SPIRV-Tools/Cross, SDL3, glew (backend OpenGL), `DiligentCore/Tests` e googletest (~10 MB, referenciados pelo CMake do Diligent mesmo desligados).
- Pre-compilar shaders/empacotar recursos: ganho pequeno (os shaders ja somam poucos KB); o peso esta nas bibliotecas.

**Testes**
- `import_gltf_keeps_node_hierarchy_and_per_part_materials`: hierarquia, transformacao herdada, materiais por parte e no espelhado.
- tool_tests: 132 testes, 0 falhas. Editor ctest: 4/4 (inclui o selftest, que cria o modelo com hierarquia).

## 0.7.16-beta — sombras de luzes pontuais e spot (CPU e GPU)

**Por que luzes pontuais nao projetavam sombra**
- A Light Prism so tem sombras do sol (CSM, direcional). Pontuais e spots nao tinham sombra em lugar nenhum: a biblioteca documenta isso em `docs/SHADOWS.md` ("point/spot shadows not implemented") e a engine nao criava esse caminho.

**Sombras de luzes pontuais e spot (CPU e GPU)**
- Luz com `castShadow` ganha um mapa de profundidade por face: 6 faces (cubo, 90 graus) para pontual e 1 face (cone do spot) para spot.
- Ate 4 luzes com sombra por quadro, as mais proximas da camera (`LightingSettings::pointShadowLights`, 0 desliga). Resolucao por face: 64 a 2048 (`pointShadowResolution`).
- Profundidade em distancia ao longo do eixo da face, interpolada com correcao de perspectiva (1/w na tela). O teste compara distancias lineares, com viés na normal e constante.
- Filtro 3x3 (PCF). Objeto sem `castShadow` nao projeta; chao sem `receiveShadow` nao recebe.
- `LightRig::build` monta `LightFrame::shadowLights` (`ShadowLight`: tipo, posicao, faces e matrizes `vp[6]`). A visibilidade e aplicada em `SoftRenderer::shade()` pela posicao da luz (a LightPrism guarda a mesma posicao).

**Settings > Lighting**
- "Luzes pontuais e spot com sombra" (0 a 4) e "Resolucao da sombra pontual/spot (por face)" (256, 512, 1024, 2048). Salvas em `render.lighting` no `.scene.json`.

**Limitacoes (honestas)**
- Na GPU: uma faixa R x 6R por luz (ate 4), cada face com `setViewport` (novo no RHI, implementado no Diligent e no backend nulo). Shader `shadow_point.hlsl` grava distancia; `lightprism/Common/PointShadow.hlsli` faz o PCF. Testado em Vulkan (lavapipe). D3D12 nao foi compilado neste ambiente.
- Correcao de paridade: o sol na GPU agora respeita `receiveShadow` (antes ignorava o flag, a CPU nao).
- Triangulos com vertice atras do near da face (0,05 m) sao descartados: sombra incompleta so muito perto da luz.
- Sem reuso de mapas de pontual: refeitos a cada quadro.

**Testes**
- `point_light_shadow_darkens_ground_behind_blocker`: o cubo sombreia o chao atras dele; sem castShadow nao sombreia.
- `point_and_spot_shadow_plan_respects_budget_and_faces`: orcamento, ordem por distancia e numero de faces (6 e 1).
- tool_tests: 131 testes, 0 falhas. Editor ctest: 4/4.

**Nao feito nesta versao (fila)**
- SSAO (Light Prism `Post/Ssao`) ligado no pipeline; ray tracing (Light Prism `Rt/Bvh`, `RtEffects`); VSM, SDF, fog volumetrico, probes, PRT, SSR, VPL emissivo, planar shadows; pagina de ajuda do Light Prism no editor.

## 0.7.15-beta — iluminacao pela Light Prism (CPU e GPU)

**Iluminacao substituida**
- A iluminacao da engine e calculada pela biblioteca Light Prism (`third_party/LightPrism`): luzes pontuais, spot e sol, clusters, sombras em cascata (CSM) e luz ambiente em SH9, com BRDF e tone map ACES da propria biblioteca.
- Renderizador CPU (`SoftRenderer`) e GPU (`GpuRenderer`, Diligent) usam o mesmo quadro de iluminacao (`LightRig`). A GPU calcula os clusters com o `ClusterSystem` da Light Prism no mesmo dispositivo e contexto do RHI.
- Sombras: as cascatas do sol substituem o antigo `shadow.hpp`. Cada cascata e um mapa R32F; ate 4 cascatas.
- Fundo do CPU: a cor de limpeza e codificada em sRGB direto (como o alvo sRGB da GPU), sem a curva ACES.

**Settings > Lighting (por cena)**
- Preset de qualidade: Mobile, Baixa, Media, Alta. Opcao "Valores personalizados" substitui o preset.
- Personalizado: cascatas (1-4), resolucao do mapa por cascata (512-4096), suavidade PCF (1, 4 ou 9 amostras), luzes e indices por cluster, corte automatico do raio.
- Orcamento: maximo de luzes pontuais e de spots (luzes alem do orcamento sao descartadas e contadas).
- Sombras do sol: distancia maxima, divisao das cascatas (lambda), mistura, extensao de quem projeta, viés de profundidade e da normal, alinhamento aos texels.
- Ambiente: intensidade da luz ambiente e pre-exposicao.
- Profiler > Rendering mostra as sombras ativas e as luzes descartadas pelo orcamento.

**Cenas e formato**
- Novo bloco `render.lighting` no `.scene.json`. Cenas antigas com `render.shadow` sao convertidas ao abrir (habilitar, resolucao e PCF).

**Engine**
- RHI: `ICommandList::setStorage` (StructuredBuffer somente leitura, `PipelineDesc::storageNames`), `IDevice::importBuffer`, `nativeDevice()`/`nativeContext()`.
- `GpuRenderer` reescrito: `init` recebe o dispositivo da Light Prism; `setLighting(LightRig&, view, proj, reuse)` por quadro.
- `loadHlsl` resolve `#include` relativo ao arquivo que o contem e inclui cada arquivo uma vez (como `#pragma once`).
- Matrizes: a engine guarda por coluna e a Light Prism por linha. A conversao (`LightRig::toLp`, `toTool`) transpoe.
- Removidos: `engine/render/include/tool/render/shadow.hpp`, `assets/shaders/lighting.hlsli` e o modelo antigo de clusters e atenuacao.

**Limitacoes desta versao**
- OpenGL ainda nao tem a iluminacao em GLSL: o editor cai para a CPU e mostra o motivo (`TOOL-E141`). Vulkan e D3D12 primeiro.
- O caminho GPU foi testado so em Vulkan por software (lavapipe). D3D12 nao foi testado.
- Os objetos Point Light e Global Light continuam sendo componentes do editor, agora alimentados pela Light Prism (nao foram criados tipos novos).

**Testes**
- `tool_tests`: 129 testes ok (novos testes do `LightRig`: mapeamento, orcamento, sombra do sol, presets).
- `gpu_test vulkan`: a imagem da GPU bate com a CPU (IoU 1.0), a textura confere e ha sombra no chao.
- `editor_selftest`: ok.

## 0.7.14-beta — desempenho com muitos objetos (benchmark, chunks de blocos opcionais, LOD da esfera, hierarquia achatada)

**Desempenho**
- Esfera com nivel de detalhe: acima de 25 m da camera, a esfera usa malha 8x5 (~80 triangulos) em vez de 24x12 (~576). A sombra e o culling usam a mesma escolha. Distancia em `Editor::kSphereFarDistance`.
- Hierarquia com mais de 300 objetos vira lista achatada e indentada dentro de um `ImGuiListClipper`: so as linhas visiveis sao montadas. Abaixo do limite, a arvore com setas continua igual. Nesse modo nao ha setas para recolher ramos.
- Malha com numero de revisao (`Mesh::revision`): a GPU reenvia os buffers quando a malha muda, mesmo com o mesmo tamanho.
- BVH (Newtoon): a pilha de percurso tem 128 entradas e nao descarta filhos em silencio (antes, `sp < 62` podia perder pares). Nao era a causa do travamento com 5000 objetos; e um ajuste de corretude.

**Chunks de blocos (recurso opcional)**
- Componente `VoxelChunk`: grade 16x16x16, 8 tipos de bloco, paleta e tamanho do bloco editaveis. Salvo na cena em RLE (pares tipo/quantidade).
- Malha so com as faces visiveis, uma malha por tipo de bloco; faces na borda de um chunk sao escondidas pelo chunk vizinho.
- Cache refeito quando o chunk ou um vizinho muda (carimbo de revisoes).
- Menu Hierarquia > Criar > "Voxel chunk (blocks)" e Adicionar recurso > "Voxel chunk (blocks)".
- Inspector: grade da camada escolhida (y), pincel por tipo, "Preencher com terreno" e "Limpar".
- Projeto sem chunks nao muda: o desenho dos chunks nao faz nada quando nao existem.

**Benchmark (headless)**
- `ToolEditor --project <pasta de teste> --ecs N`: cria N esferas (como `test ECS: N`) e imprime media, p50, p95 e maximo por quadro, com o tempo de cada zona do Profiler.
- `ToolEditor --project <pasta de teste> --voxel N`: o mesmo, com N chunks de terreno.
- Sem janela de projeto, sem gravar cena nem configuracoes. `--project` e obrigatorio, para o teste nao tocar no projeto.

**Medicoes** (renderizador CPU, sandbox sem GPU; apresentacao ~105-220 ms e do SDL software desta maquina)

| Cena | Triangulos | Raster da cena | Media do quadro |
|---|---|---|---|
| 5000 esferas (antes) | 2,88 M | 145 ms | 760 ms |
| 5000 esferas (depois) | 0,80 M | 82 ms | 485 ms |
| 500 chunks (128 mil blocos) | 0,73 M | 27 ms | 150 ms |
| 1000 chunks (512 mil blocos) | 1,46 M | 56 ms | 181 ms |

A fisica (Update) ficou em 29-30 ms com 5000 objetos: nao e o gargalo.

**Correcoes de build**
- Windows (MSVC): o nome `near` era macro do windows.h e quebrava o build dos chunks; renomeado para `neighborAt`. Checado tambem com o mingw (`-fsyntax-only`).

**Testes**
- Selftest: 207 checks, 0 falhas (novos: faces visiveis, edicao e revisao, cache, ida e volta na cena, posicao dos chunks).
- `TOOL_VOXEL_SHOT=<arquivo.ppm>` no selftest grava uma imagem de 3x3 chunks para conferencia.

**Pendente / nao verificado**
- Chunks sem colisao de fisica (o caminho esperado e um colisor estatico por chunk).
- Edicoes do Inspector nao entram no Undo; clicar num chunk no viewport nao seleciona (a selecao funciona pela Hierarquia).
- Caminho da GPU (Diligent) com chunks e a revisao da malha: compila, nao executado nesta maquina.
- Esfera distante e hierarquia achatada: nao vistas na tela.
- Mapa de sombra dos chunks: cobre so a area dos objetos com malha; em mundos grandes pode cortar sombras.
- Texto apagando sozinho: ainda nao reproduzido. Precisa do log de teclas (Interface > Registrar eventos de teclado) e do campo exato.

## 0.7.13-beta — exportacao para Windows x64 e Linux x64, janela Editor info, modo jogador, integracao do Newtoon

> Entrada reconstruida a partir do registro de trabalho. Confira os itens antes de publicar.

**Exportacao**
- File > Export game: gera uma pasta com o editor, as DLLs (Windows), `assets/`, `Data/Project` e um launcher (`Play.bat` no Windows, `play.sh` no Linux). Plataformas: Windows x64 e Linux x64. Android, WebGL e UWP ainda nao.
- A exportacao recusa projeto com informacoes incompletas e nao substitui uma pasta que nao seja de exportacao (`export.json`).
- Versao do executavel (propriedades do arquivo no Windows) ainda nao e gravada.

**Editor info**
- File > Editor info: nome do produto, criador, estudio, pais, cidade, versao (formato 1 a 4 partes numericas), descricao, copyright e site. Salvo em `Project/project.json`.

**Modo jogador**
- `--player`: abre `Data/Project`, inicia o Play em tela cheia, Esc fecha, nada e salvo. Usado pelo launcher exportado.

**Newtoon**
- Integracao das adicoes da fisica (matematica, BVH, pool de threads, modulos de esqueleto, veiculo e ragdoll) para o repositorio oficial, entregue como arquivos e patch. Nao foi commitado no repositorio oficial.

**Editor**
- Forma "Smoke" para particulas, com predefinicao.

**Testes**
- Selftest: 196 checks, 0 falhas.

**Pendente**
- Android, WebGL e UWP.
- Versao do executavel no Windows.
- Visualizacao do modo jogador em tela cheia (so testado sem janela).

## 0.7.12-beta — particulas: formatos, area, trajetoria, tornado, 4 cores e opacidade; correcoes do editor

**Correcoes**
- Particulas escuras: a cor era multiplicada por um fator que ia a zero no fim da vida e as malhas passavam pela iluminacao (lado de tras ficava escuro). Agora cor e alfa sao reais, sem iluminacao. Ao tocar o chao ou no fim da vida a particula fica transparente, nao preta.
- Delete e F2 (renomear) agiam sobre o objeto selecionado mesmo com um campo de texto ativo (ex.: buscar na Hierarquia). Agora esses atalhos so valem fora de campos de texto. Tambem vale para o Delete dos nos do Narrative e para o F2 dos Assets.
- Nao foi possivel reproduzir o "texto apagando sozinho" no console ou ao renomear pelo codigo. Se continuar, anote a tecla usada e em qual campo acontece.

**Particulas: novos recursos**
- Formatos: Bolinha (com halo), Quadrado, Triangulo, Estrela, Anel e Risco (na direcao do movimento).
- Area de nascimento: Ponto, Caixa aleatoria (tamanho X/Y/Z) e Esfera aleatoria.
- 4 cores ao longo da vida: inicio, 1/3, 2/3 e fim (antes eram 2).
- Opacidade ao longo do tempo (ligada por padrao, para manter o visual de sumir) e Atraso da opacidade (segundos com opacidade total antes de sumir).
- Trajetoria (Trace route): ate 8 pontos locais ao objeto. A particula percorre a curva suave que passa por eles durante a vida. A Scene desenha a trajetoria (linha amarela) com o objeto selecionado.
- Tornado: a particula gira em torno do eixo +Y e sobe abrindo em funil. Velocidade em voltas/s, raio da base e altura. Se a trajetoria estiver ligada, ela tem prioridade.
- Serializacao: os campos novos sao salvos na cena. Cenas antigas abrem com o visual anterior (as cores 2 e 3 sao calculadas a partir da inicial e da final).

**Limitacoes**
- As particulas agora sao desenhadas por cima da vista, sem teste de profundidade: aparecem na frente de paredes (antes eram escondidas por objetos). Um passe com mistura alfa no renderizador resolveria isso; ainda nao existe.
- Trajetoria e tornado nao usam gravidade nem quique.

**Testes**
- Selftest: 188 checks, 0 falhas (cores em 4 paradas, opacidade com atraso, curva que passa pelos pontos, area de nascimento dentro da esfera).

**Pendente / nao verificado**
- Aparencia das particulas na tela e no Inspector: nao vi o resultado; confira no seu computador.
- Build Windows/MSVC: nao compilado nesta maquina.

## 0.7.11-beta — formas 3D, interface travada, bloco da cena no Narrative, Inspector reorganizado

**Formas 3D (novas)**
- Menu Hierarquia > Criar > "Mais formas": Cilindro, Cone, Capsula, Roda, Prisma triangular e Prisma hexagonal. Tambem aparecem no campo Forma do Mesh Renderer (shapes 4 a 9).
- Todas as malhas tem a face de fora virada para fora (a ordem dos triangulos e corrigida ao gerar) e sao testadas no selftest.
- Fisica: as formas novas usam caixa envolvente como colisor (aproximacao). Esfera continua como esfera.

**Interface travada e encaixe**
- Novo menu **Interface**:
  - **Travar interface**: abas e janelas nao saem dos encaixes, nao viram janela flutuante ao clicar e os divisores nao se mexem. Vale para os encaixes ja existentes. Para desfazer, desligue a opcao ou use Redefinir layout.
  - **Encaixar so segurando Shift**: encaixar ou desencaixar uma janela exige Shift, o que evita que janelas se soltem ao clicar.
  - Redefinir layout.
- As duas opcoes sao salvas em editor_settings.json.
- Limite: janela que ja estiver flutuando antes de travar continua flutuando; use Redefinir layout para encaixa-la de volta.

**Narrative Controller: bloco padrao da cena**
- Todo grafo de cena tem um bloco fixo (inicio) com o nome da cena: "Cena: <nome>".
- Indestrutivel: a tecla Delete nao apaga esse bloco.
- Acompanha o nome da cena; nao precisa vincular o codigo do Narrative a cena. O grafo ja e Assets/Narrative/<cena>.narr.json.
- Em cenas antigas, o bloco e criado ao abrir a cena e gravado no arquivo da cena na hora.

**Inspector (reorganizado)**
- Transform com linhas X / Y / Z coloridas e rotulos alinhados.
- Active e Locked na mesma linha.
- Rodape: Adicionar componente (o mesmo menu do Adicionar recurso), Recolher tudo, Abrir tudo.
- Limite: o redesenho nao cobre todos os cards (ex.: as secoes de Fisica, Ferramentas e Script continuam no formato atual) e ainda nao ha Copiar componente nem Remover componente.

**Testes**
- Selftest: 182 checks, 0 falhas (eram 172): geometria das formas, mapeamento de shape e bloco do Narrative.
- Teste sem janela (--frames) executado com o Inspector e o menu novos, sem erros.

**Pendente / nao verificado**
- Aparencia do Inspector e do menu Interface: nao vi a tela; confira no seu computador.
- Build Windows/MSVC: nao compilado nesta maquina (ambiente Linux).

## 0.7.10-beta — profiler com CPU, RAM, VRAM e cache; revisao de iluminacao

**Correcao de build (Windows / MSVC)**
- `script_check.hpp`: a variavel `near` foi renomeada para `closest`. Em Windows, `<windows.h>` define `near` como macro vazia e o build parava com erro C2059/C2513.
- `sysmon.hpp`: a leitura do nome do processador usa a variavel de ambiente `PROCESSOR_IDENTIFIER` (sem linkar advapi32).

**Profiler: secao "System: CPU, RAM, VRAM and cache"**
- CPU total, nucleos em uso (acima de 25%) e uso do proprio editor, em % de todos os nucleos.
- CPU por nucleo (lista expansivel).
- RAM do sistema (usada e total) e memoria do editor (com pico).
- VRAM do processo, como o driver informa. Windows: DXGI (`QueryVideoMemoryInfo`). Linux: driver amdgpu (`/sys/class/drm`). Onde nao ha medida, o painel diz o motivo.
- Tamanhos de cache por nivel (L1 dados e instrucoes, L2, L3) e tamanho da linha.
- Amostragem a cada 0,5 s: custo baixo, sem medir por quadro.
- Nao medido: taxa de acerto do cache (exige contadores de hardware). A VRAM do motor (recursos que ele criou) ainda nao e contada separadamente.
- Teste no selftest: um laco ocupado aparece como uso do editor; percentuais no intervalo; RAM e cache presentes.
- Windows: codigo escrito sem compilar nem rodar nesta maquina (ambiente Linux). Revise no seu build.

**Iluminacao: revisao (docs/LIGHTING_REVIEW.md)**
- Corrigido: o Help dizia que luz pontual tinha sombra com "Casts shadows". Era falso (a caixa nao existe). Texto corrigido em EN e PT.
- Achados: sombra so para o sol; mapa unico de 40 m; PCF fixo; bias constante; `cascadeSplits` e `assignLights` nao usados; sem tempo de GPU do passe de sombra.
- Nenhuma sombra nova nesta versao: planejada para a biblioteca de iluminacao.

## 0.7.9-beta — erros de script: verificador, try/catch e console

**Por que um cubo "nao funcionava"**
- Erros comuns eram silenciosos: `obj.position` (o certo e `obj.transform.pos`), nome de objeto errado em `setPosition`, argumento faltando, texto no lugar de numero (que virava NaN e podia fazer o objeto sumir). Agora cada caso tem mensagem.

**Verificador de scripts (novo)**
- Enquanto voce digita, o Inspector verifica o script (0,4 s depois da ultima tecla) e lista os problemas com a linha e o trecho.
- Ao salvar, ao dar Play e pelo botao "Check scripts" do Console (ou o comando `scripts`), o resultado vai para o Console com o nome do objeto.
- Sintaxe: o QuickJS compila sem executar e informa linha e coluna (TOOL-S001).
- Regras: `obj.position`, `obj.transform.pos.x`, `class`, `this`, `import`, `console.log`, funcao desconhecida (com sugestao, ex.: `setpos` -> `setPosition`), numero de argumentos, tecla invalida, `Input.` com maiuscula, nome de funcao parecido com o da Unity, objeto sem start/update/OnEventTrigger.
- Os 14 exemplos do Help passam sem erro.

**Erros de execucao**
- Erro durante start/update/evento mostra a funcao, a linha e a coluna (TOOL-S002).
- Argumento invalido (texto, NaN, faltando) lanca TypeError com mensagem clara: o script pode usar try/catch.
- Comando para objeto inexistente: aviso TOOL-S005, uma vez por Play, e o script continua.
- NaN ou valor fora da faixa no Transform: aviso TOOL-S006 dizendo o campo, e a mudanca e ignorada.
- Falha interna do sistema de scripts no tick: o editor continua e os scripts param (TOOL-S009).

**Scripts: novas funcoes**
- `debug.warn(msg)` e `debug.error(msg)` (aviso e erro no Console; o script continua).
- Snippet de try/catch no VS Code (`ttry`).

**Console**
- Filtros por nivel (Info, Warnings, Errors) e por texto (ex.: nome do objeto ou codigo TOOL-S007).
- Botao Copy: copia as linhas visiveis.
- Clique numa linha "[Nome] ..." seleciona o objeto na cena.
- Botao "Check scripts" e comando `scripts`.

**Help**
- Secao "Erros e try/catch" nos topicos da API (EN e PT). Tabela de codigos em `docs/SCRIPT_ERRORS.md`.
- Dois tutoriais novos no grupo Scripts (EN e PT): "Tutorial: criar um jogo de FPS" (movimento, mira, tiro com instantiate, alvo que muda de cor) e "Tutorial: portas que abrem com E" (porta que sobe perto do jogador, com evento "abrir"). O codigo dos tutoriais roda no selftest.

**Testes**
- Selftest do editor: 166 checks, 0 falhas. Novos: cada regra do verificador (com a linha), exemplos do Help sem erro, try/catch, erro com linha, argumento invalido, NaN, objeto inexistente.
- Nao verificado visualmente nesta versao (ambiente sem tela): lista de problemas no Inspector e filtros do Console.

## 0.7.8-beta — usabilidade do editor, scripts com autocomplete

**Desempenho do editor**
- Lista de assets em cache: a pasta so e relida quando muda, quando o projeto muda ou a cada 2 s (antes varria a pasta e fazia um stat por arquivo a cada quadro).
- Lista de assets por tipo (combos) em cache, invalidada por revisao de assets.
- Medido: nao medido ainda. Confira no Profiler, zona "Panels".

**Interface**
- Escala padrao da UI em 125% (como o Windows em Full HD). Fica no editor de tema, secao "Shape and size" ("UI scale", com botoes 100% e 125%).
- Temas salvos antes desta versao (sem `uiVersion` 2 no arquivo) passam a usar 125%. Temas novos guardam a escala escolhida.

**Hierarquia**
- Icone de olho: oculta/mostra o objeto no editor.
- Icone de cadeado: trava o objeto no editor (nao aparece no jogo nem no Narrative). Travado nao pode ser movido pelo gizmo, nem trocar de pai, e o Inspector fica desabilitado.
- Duplicar um filho cria a copia dentro do mesmo pai.
- Botao direito no objeto: Editar ou Excluir.

**Cena**
- Barra flutuante na Cena (estilo Unity): Mover, Girar, Escalar, cadeado de snap (ima) e "Encostar no contato".
- Ima: com ele ligado, o movimento segue o passo escolhido (0,05 a 2 m), e "Encostar no contato" cola o objeto na face do vizinho mais proximo, sem vao e sem sobrepor. Clashes com outros objetos sao rejeitados.

**Console**
- Campo de texto para digitar comandos, com historico (setas para cima e baixo) e botao Executar. O campo fica sempre visivel.

**Scripts (QuickJS)**
- Novas funcoes: `position(nome)`, `setRotation`, `setColor` (MeshRenderer, cores 0 a 1), `instantiate(modelo, x, y, z)` com posicao e nome da copia.
- Novo objeto `input`: `input.down("W")`, `input.pressed("Space")`, `input.mouseDelta()`. Teclas: W A S D Q E R F 1 2 Space Shift Ctrl Alt setas Enter Escape, Mouse0 e Mouse1.
- Template de script novo: funcoes `start(obj)`, `update(obj, dt)` e `OnEventTrigger(obj, name)` (antes era uma classe, que nao rodava).
- Autocomplete no VS Code: ao abrir o projeto o editor escreve `toolengine.d.ts` (tipos de todas as funcoes e objetos), `.vscode/toolengine.code-snippets` (13 snippets) e `jsconfig.json` (so se nao existir).

**Help**
- Topicos "API de script" e "Scripts: exemplos" reescritos (EN e PT) com a API real.
- 12 exemplos testados: mover e girar, andar com WASD, primeira pessoa, terceira pessoa, projetil e tiro, cor do mesh, evento, receber evento, trigger para o Narrative, andar ate outro objeto, start/instantiate/destroy/componentes, ligar e desligar.
- Exemplo de shader (propriedade de shader) segue como roteiro: a API ainda nao expoe parametros de shader.
- Blocos de codigo invalidos em outros topicos foram trocados por um texto que aponta para o exemplo correto.

**Testes**
- Selftest do editor: 146 checks, 0 falhas. Inclui: cada exemplo do Help roda sem erro, o `toolengine.d.ts` declara toda funcao registrada, snap sem vao e sem sobreposicao, duplicar mantem o pai, travar salva e carrega.
- Nao verificado visualmente nesta versao (ambiente sem tela): layout dos icones, barra flutuante, escala 125% e console.

**Planejado (nao implementado)**
- Biblioteca SpriteUi (UI e sprites no lugar do ImGui): plano por fases em `docs/UI_SPRITE_PLAN.md`.
- Propriedades de shader expostas aos scripts.

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
