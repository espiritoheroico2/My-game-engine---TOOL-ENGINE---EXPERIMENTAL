# 34. Oclusao por GPU (HiZ), configuravel, com a CPU mantida para teste

Estado: codigo e teste de corretude prontos. As MEDICOES cpu x gpu NAO foram feitas (escopo adiado a pedido).

## Configuracao

`RenderSettings::occlusionMode` (project.json, `render.occlusionMode`): `"gpu"` (padrao), `"cpu"` ou `"off"`. Vale junto de
`occlusionCull` (chave mestra existente). `"gpu"` cai em `"cpu"` quando nao ha GPU ativa com compute/culling GPU (software, OpenGL,
camera ortografica nao usa oclusao nenhuma). O modo efetivo fica em `Editor::occlusionModeUsed` e sai no bench.
Linha de comando: `--occlusion-mode gpu|cpu|off`. O bench passou a imprimir as zonas de GPU (com `TOOL_GPU_TIMERS=1`) e o modo.

## Arquitetura (mesmo criterio nos dois modos: oclusor opaco com raio projetado >= 8 px, malha <= 2048 triangulos)

CPU: `OcclusionBuffer` como antes (`skipCulled` no editor). GPU, dentro de `GpuRenderer::end()`, depois do culling de frustum
(`runGpuOcclusion`, zonas de tempo novas):
1. `occ_depth` (occluder.hlsl): desenha, com o buffer compactado do frustum e os args indiretos dele, os lotes oclusores num alvo
   R32F com a profundidade LINEAR de view (-z), limpo com 1e30, resolucao da vista. O VS descarta instancias com raio projetado < 8 px,
   bounds invalidos e material com alpha cutoff; lotes de dupla face e malhas densas nem sao desenhados.
2. `hiz` (hiz_reduce.hlsl): niveis R32F, cada um o MAXIMO 2x2 do anterior (o ultimo texel absorve dimensao impar).
3. `occlusion` (occlusion_cull.hlsl): compute sobre as instancias que passaram no frustum. Esfera -> retangulo de tela (cubo alinhado
   a vista), nivel com 1 texel por eixo (ate 2x2 amostras), escondido so se (profundidade do centro - raio) esta atras do maximo do HiZ.
   Sai um segundo buffer compactado + args; o desenho indireto do passe opaco usa esse (nenhum Draw montado na CPU).

Conservador: bounds/matriz invalidos, esfera tocando o near ou envolvendo a camera, retangulo fora da tela ou grande demais para o
nivel mais alto, pixel sem oclusor (valor enorme) e tolerancia relativa de 1e-4: tudo fica VISIVEL.

Limite do RHI e a escolha: o RHI nao tem UAV de textura nem vista de mip, e a profundidade D32F nao e legivel. Entao: cada nivel do HiZ
e uma textura R32F propria (linear depth em cor, nao o depth buffer), reduzida por pixel shader. O compute precisa ler texturas: foi
feita UMA extensao minima no RHI (`setTexture` e `PipelineDesc::textureNames` agora valem tambem para pipelines de compute, SRV com
`Texture2D.Load`; diligent_device.cpp).

## Corretude (apps/gpu_test/occlusion_test.cpp, ctest `occlusion_gpu_vulkan`, label gpu)

Cena com parede grande, 72 objetos atras, na frente, ao lado, espiando pela borda, acima, camera dentro de um objeto e 90 caixas
aleatorias; 5 poses de camera. A "verdade" vem de amostras na superficie voltada ao olho e segmento contra todas as outras caixas.
Resultado (RX 6500 XT, Vulkan, 1280x720): falsos ocultos GPU = 0 e CPU = 0 em todas as poses. A GPU escondeu 229 e a CPU 333 objetos
no total (a CPU usa caixa e grade 1/8, e esconde mais); todos os "CPU visivel e GPU oculto" (15) conferem como realmente ocultos.
Na pose frontal a GPU esconde 72/72 atras da parede. Observacao: a primeira versao do teste acusou 2 falsos ocultos por erro da
verdade (raio vazando entre caixas coladas); corrigido usando caixas fechadas.

Selftest do editor: 337 checks, 0 falhas (inclui o novo check do modo). `ctest -R "Editor\."`: 75/75.

## O que falta

- Medicao cpu x gpu (Vulkan 1280x720, `--ecs-mover` 10k/50k/100k e `--instances` 100k, `TOOL_GPU_TIMERS=1`).
- Oclusao na cena persistente (`--gpu-motion`) e nos passes de sombra/prepasso/emissao (so o passe opaco principal).
- O passe de oclusores desenha instancias pequenas so para o VS descarta-las (custo por vertice); um estagio que compacte so os
  oclusores reduziria isso. A GPU esconde menos que a CPU em vistas rasantes (esfera em vez de caixa).
- Vulkan/D3D12 so; D3D12 nao testado. Piramide limitada a 12 niveis.
