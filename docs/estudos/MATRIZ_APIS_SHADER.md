# Matriz de APIs do shader PBR (assets/shaders/pbr.hlsl)

Auditoria estatica do PBR nas APIs DX11, DX12, Vulkan e OpenGL, mais o caso GLES/WebGL2.
Base: branch `spriteui-rhi-backend`, HEAD `de82d40` (commit de correcao da fog: `e404d30`).

Legenda:
- **[medido]**: rodado neste ambiente; o comando esta na secao "Comandos" e o resultado foi lido na hora.
- **[lido: arquivo:linha]**: afirmacao tirada do codigo; nao foi executada.
- **[nao verificado]**: depende de codigo que nao esta na arvore (DiligentCore) ou de GPU/driver real.
- **[spec]**: limite de API citado de memoria da especificacao; nao foi consultado nem medido aqui.

## Resumo

| API | Compila/valida | Bindings no limite | Storage buffers | Flip de Y | Veredito estatico |
|---|---|---|---|---|---|
| Vulkan | SPIR-V valido [medido] | UBO 8 de 8 [medido] | 4 readonly [medido] | sem ajuste no HLSL [lido: pbr.hlsl:56]; convencao depende do Diligent [nao verificado] | Plausivel. Bindings duplicados no SPIR-V do glslang (sem shift) [medido]: nao prova defeito, o caminho real usa o compilador do Diligent [nao verificado] |
| DX12 | nao compilado (sem dxc) [medido] | mesmos 8 registradores b0..b7 [medido no SPIR-V]; root signature gerada pelo Diligent [nao verificado] | 4 SRV [lido] | igual Vulkan [nao verificado] | Nao verificado alem dos nomes e contagens |
| DX11 (SM5) | nao compilado (sem fxc) [medido] | UBO por estagio: VS 2, PS 6 (limite 14) [medido + spec]; SRV PS t0..t15 (limite 128) [medido + spec]; sampler 0 [medido] | 4 StructuredBuffer SRV [lido] | igual D3D [nao verificado] | Pela contagem, cabe. Compilacao nao testada |
| OpenGL 4.3 (desktop) | GLSL 430 valido [medido] | UBO VS 4,6; PS 0,1,2,3,5,7 [medido]; limites GL 4.3 [spec] | 4 SSBO readonly na PS [medido] = minimo da spec (4) [spec] | VS inverte Y no GLSL [medido]; winding compensado [lido: diligent_device.cpp:159] | Nomes batem com o C++ [medido]. Compila; runtime nao testado |
| GLES 3.0 / WebGL2 | NAO: GLSL 430 nao compila em ES 3.0 [medido] | - | SSBO nao existe em ES 3.0 [spec] | - | Nao roda como esta |
| GLES 3.1 | nao gerado (build usa `--no-es`) [lido: tools/shaders/build_glsl.py:111] | - | - | - | Precisa de nova geracao de GLSL ES [lido + experimento] |

## Vulkan

- **Compila/valida [medido]**: VS, PS e EmissionPS compilam com `glslangValidator -D -V --target-env vulkan1.2` (rc 0) e passam em `spirv-val --target-env vulkan1.2` (rc 0).
- **Bindings [medido]**: VS usa 2 UBO (`ObjectBuffer` b6, `FrameBuffer` b4). PS usa 6 UBO (`CsmCB` b2, `PointShadowCB` b7, `GlobalLightCB` b0, `SunLightCB` b1, `ClusterGridCB` b5, `IblCB` b3). Nao ha `b6` na PS. A uniao dos estagios e 8 UBO, b0..b7, no limite minimo da spec para UBO dinamico (`maxDescriptorSetUniformBuffersDynamic` >= 8) [spec].
- **Texturas e samplers [medido]**: PS usa 11 texturas (`AlbedoMap` t0, `CsmMap0-3` t3-t6, `PointShadowMap0-3` t10-t13, `AoMap` t14, `IblBrdfLut` t15). Nenhum sampler entra no SPIR-V, porque `IblSampler` (s2) e `IblPrefiltered` (t16) de `Ibl.hlsli:20-22` nao sao usados pelo PBR.
- **Storage [medido]**: PS le 4 buffers somente leitura: `PointLightsBuffer` t1, `SpotLightsBuffer` t2, `LightGrid` t8, `LightIndexList` t9.
- **Conflito de binding no glslang [medido]**: sem `-fvk-*-shift`, o glslang atribui o numero do registrador como binding do set 0. Na PS os bindings 0, 1, 2, 3 e 5 ficam com dois recursos de tipos diferentes (UBO e textura/SSBO). O repositorio nao tem nenhum `-fvk-*-shift` [lido: grep em engine/rhi_diligent vazio], e o compilador real do caminho Vulkan e o do Diligent, cujo codigo nao esta na arvore. Por isso isto nao e defeito provado. Fica como item aberto.
- **Flip de Y [lido: pbr.hlsl:56]**: `clip = mul(viewProj, wp)`, sem ajuste no HLSL. O engine nao ajusta a projecao (grep vazio em engine/ e apps/). A convencao de Y do Vulkan depende de como o Diligent compila o HLSL [nao verificado].
- **Runtime**: nao executado.

## DX12

- **Compilacao**: nao testada. O ambiente nao tem `dxc` nem `fxc` [medido: `which dxc fxc` vazio].
- **Bindings**: os mesmos registradores do HLSL [lido: pbr.hlsl:14,19,31-32; lightprism/Common/*.hlsli]. O Diligent cria a root signature a partir das declaracoes [nao verificado: DiligentCore ausente]. O limite de 8 UBO citado no commit `e404d30` e do Vulkan; o DX12 usa tabelas de descritor [spec] e nao foi medido.
- **Flip de Y**: igual ao Vulkan [nao verificado].

## DX11 (SM5)

- **Compilacao**: nao testada (sem `fxc`/`dxc`). Os recursos usados sao todos SM5 (StructuredBuffer, Texture2D.Load, cbuffer, nointerpolation) [lido].
- **Indices de registrador [medido]**: cbuffer em b0..b7 (limite de 14 slots por estagio [spec]); SRV em t0..t6 e t8..t15 (limite de 128 por estagio [spec]); nenhum sampler nem UAV na PS.
- **Storage**: `StructuredBuffer` somente leitura em t1, t2, t8, t9 [lido: lightprism/Common/LightBindings.hlsli:7-8; ClusterLighting.hlsli:23-24]. SRV, nao UAV.
- **Mapeamento Diligent**: se o Diligent usa os numeros de registrador do bytecode, bate. Nao verificado (DiligentCore ausente).
- **Flip de Y**: igual a D3D [nao verificado].

## OpenGL 4.3 (desktop)

O backend Diligent de GL usa o mesmo enum `Backend::GLES` para o GL desktop [lido: diligent_device.cpp:543 e :578-589]. O init pede GL 4.3+ [lido: diligent_device.cpp:589, mensagem]. Os shaders PBR de GL vem de `assets/shaders/glsl/*.430.glsl` [lido: gpu_renderer.hpp:116-120].

- **Compila/valida [medido]**: `glslangValidator -S vert` em `pbr.vs.430.glsl` (rc 0); `-S frag` em `pbr.ps.430.glsl` e `pbr.emi.430.glsl` (rc 0). Os 5 GLSL de compute da Light Prism tambem compilam (rc 0). `tools/shaders/build_glsl.py --check` sai com 0 e diz "GLSL em dia com os HLSL (23 arquivos)" [medido].
- **Nomes [medido]**: os 8 blocos uniform, 11 texturas e 4 storage de `gpu_renderer.hpp:69-79` batem exatamente com os recursos do SPIR-V de VS e PS. Nenhum recurso sem nome e nenhum nome sem uso [medido por comparacao automatica].
- **Bindings de UBO [medido]**: VS `ObjectBuffer` 6, `FrameBuffer` 4. PS `CsmCB` 2, `PointShadowCB` 7, `GlobalLightCB` 0, `SunLightCB` 1, `ClusterGridCB` 5, `IblCB` 3. Sem repeticao dentro do programa. Limites GL 4.3 de `MAX_UNIFORM_BUFFER_BINDINGS` >= 36, `MAX_FRAGMENT_UNIFORM_BLOCKS` >= 12 [spec]: 8 e 6 cabem.
- **Texturas [medido]**: PS declara 11 `uniform sampler2D` (sem binding; Diligent associa pelo nome). Limite `MAX_TEXTURE_IMAGE_UNITS` >= 16 [spec].
- **Storage [medido]**: PS tem 4 `readonly buffer` (SSBO) em binding 8, 9, 1, 2. O minimo de GL 4.3 para `MAX_FRAGMENT_SHADER_STORAGE_BLOCKS` e 4 [spec]: o PBR usa exatamente o minimo. Qualquer driver de GL 4.3 que cumpra a spec cabe, mas sem folga.
- **Flip de Y [medido]**: `pbr.vs.430.glsl` termina com `gl_Position.y = -gl_Position.y`, gerado por `build_glsl.py:81-93`. O winding e compensado com `FrontCounterClockwise = false` para este backend [lido: diligent_device.cpp:159]. A afirmacao do comentario de `build_glsl.py` de que o Diligent faz essa inversao no HLSL nao pode ser conferida: o codigo citado so troca o winding.
- **Matrizes [derivado, nao medido numericamente]**: o GLSL usa `vec4 * mat4` com `layout(row_major)` (`pbr.vs.430.glsl:44-45`). Pela algebra, para os mesmos bytes, isso equivale a `mul(M, v)` do HLSL com a convencao column-major padrao do DXC. Confirmar com um teste numerico em GPU.
- **Runtime**: nao executado. Riscos abertos: o indice vertical do cluster em GL (`gl_FragCoord` com origem em baixo) e a ordem de linhas do AO. Ambos dependem da convencao de Y e precisam de GPU.
- **Pendencia declarada no codigo**: `gpu_renderer.hpp:115` avisa que bloom e sombra pontual ainda diferem do Vulkan.

## GLES (ES 3.0, WebGL2 e Android)

- **Plataformas [lido: engine/rhi/include/tool/rhi/rhi.hpp:25-29]**: Android e Web listam GLES. Web usa GLES como WebGL2 (ES 3.0).
- **Problema [medido]**: o GLSL gerado e `#version 430` e `build_glsl.py:111` passa `--no-es`. Testei a mesma fonte trocando so a linha de versao e adicionando `precision`:
  - `#version 300 es`: falha em `0:123` com `'binding' : not supported for this version` (o layout `binding` da UBO nao existe em ES 3.0).
  - `#version 310 es`: compila (rc 0). Isso mostra o que ES 3.1 aceita, mas nao prova que o caminho ES funciona: o build nao gera GLSL ES.
- **Conclusao [medido + lido]**: o PBR nao roda em GLES 3.0/WebGL2. Em GLES 3.1 (Android) precisa de nova geracao com `spirv-cross --es`, que nao existe no repositorio hoje.
- **GLES offscreen**: nao existe um caminho GLES 3.0 separado no repositorio. O "offscreen" da scissor e o mesmo backend GL desktop [lido: diligent_device.cpp:486-492].

## Comandos usados (medidos)

```
python3 tools/shaders/build_glsl.py --check                       # rc 0, 23 arquivos
glslangValidator -D -V --target-env vulkan1.2 -S vert -e VSMain -o vs.spv pbr_flat.hlsl
glslangValidator -D -V --target-env vulkan1.2 -S frag -e PSMain -o ps.spv pbr_flat.hlsl
glslangValidator -D -V --target-env vulkan1.2 -S frag -e EmissionPS -o emi.spv pbr_flat.hlsl
spirv-val --target-env vulkan1.2 <spv>                             # rc 0 nos 3
spirv-dis <spv>   # extracao de OpDecorate Binding, OpVariable, ArrayStride, OpMemberDecorate Offset
glslangValidator -S vert assets/shaders/glsl/pbr.vs.430.glsl       # rc 0
glslangValidator -S frag assets/shaders/glsl/pbr.ps.430.glsl       # rc 0
glslangValidator -S comp <compute>.430.glsl                         # rc 0 nos 5
sed "1s/.*/#version 300 es/" + precision (copia temporaria)         # falha: binding
```

`pbr_flat.hlsl` e o `pbr.hlsl` com os `#include` achatados como em `build_glsl.py:55-68`. Os scripts de apoio ficaram em scratchpad, fora do repositorio.

## Tamanho dos blocos (medido, SPIR-V std140 vs C++)

| Bloco | HLSL (bytes) | C++ | Fonte C++ |
|---|---|---|---|
| FrameBuffer | 144 | 144 | `gpu_renderer.hpp:61` |
| ObjectBuffer | 160 | 160 | `gpu_renderer.hpp:61` |
| CsmCB | 352 | 352 | `third_party/LightPrism/include/LightPrism/Shadow/Csm.hpp:68` |
| IblCB | 160 | 160 | `third_party/LightPrism/include/LightPrism/Ibl/Ibl.hpp:46` |
| PointShadowCB | 1680 | 16 + 4*416 = 1680 | `gpu_renderer.hpp:65` |
| GlobalLightCB | 32 | 32 | `third_party/LightPrism/include/LightPrism/LightTypes.hpp:96` |
| SunLightCB | 32 | 32 | `LightTypes.hpp:95` |
| ClusterGridCB | 64 | 64 | `third_party/LightPrism/include/LightPrism/Cluster/ClusterTypes.hpp:50` |
| Stride PointLight / SpotLight / ClusterCell / uint | 48 / 64 / 8 / 4 | 48 / 64 / 8 / - | `LightTypes.hpp:88-96`, `ClusterTypes.hpp:23` |

## Pendente, sem GPU real

1. Vulkan real: validacao de camadas e pipeline layout no driver (conferir se o Diligent aplica shift por tipo de registrador).
2. DX11 e DX12: compilar o HLSL com fxc/dxc (SM5 e SM6), criar root signature e confirmar registradores.
3. OpenGL 4.3: renderizar com `gpu_test` e comparar com Vulkan; validar o flip de Y, a ordem do cluster e do AO, e o winding.
4. GLES: nao suportado hoje. Decidir se Android/Web entram no escopo.
5. Limites reais do dispositivo (UBO dinamico, SSBO em fragmento) em vez dos minimos da spec.
6. Matriz de matrizes (row/column) validada numericamente.
7. Diligent (DiligentCore) nao esta na arvore: as afirmacoes sobre o que ele faz com HLSL (shift, inversao de Y) nao foram conferidas.
