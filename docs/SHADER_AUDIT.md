# Auditoria dos shaders (HLSL -> GLSL e limite de buffers do Vulkan)

Data: 2026-11-10. Branch `spriteui-rhi-backend`.

## 1. GLSL commitado x HLSL

`tools/shaders/build_glsl.py` regenerado em uma copia temporaria (`glslangValidator` + `spirv-cross` do sistema) e comparado com `assets/shaders/glsl/` e `assets/shaders/lightprism/glsl/`:

- `diff -rq` nas duas pastas: sem diferenca. Os 23 arquivos estao em sincronia byte a byte com o HLSL atual.
- O `--check` do script so verificava se o arquivo existia, apesar do que a docstring dizia. Agora ele regenera e compara bytes (corrigido nesta sessao). Testado: `--check` passa no repositorio (saida 0) e acusa arquivo diferente e arquivo faltando numa copia alterada (saida 1).

## 2. Nomes de cbuffer, textura e storage (C++ x HLSL)

`pbrUniformNames`, `pbrTextureNames` e `pbrStorageNames` (`engine/render/include/tool/render/gpu_renderer.hpp`) conferidos contra as declaracoes:

- Uniform (slots 0..7): FrameBuffer (b4), ObjectBuffer (b6), GlobalLightCB (b0), SunLightCB (b1), CsmCB (b2), IblCB (b3), ClusterGridCB (b5), PointShadowCB (b7). Todos batem.
- Textura: AlbedoMap (t0), CsmMap0..3 (t3..t6), PointShadowMap0..3 (t10..t13), AoMap (t14), IblBrdfLut (t15). Todos batem.
- Storage: PointLightsBuffer (t1), SpotLightsBuffer (t2), LightGrid (t8), LightIndexList (t9). Todos batem.
- O shader do passe de emissao (`EmissionPS`) nao usa nenhum recurso no SPIR-V (`spirv-dis`: nenhum Block/Binding), entao o pipeline `emission` esta correto.
- `IblPrefiltered` (t16) esta declarado em `Ibl.hlsli` mas nao aparece no SPIR-V do PBR: nao e usado.

## 3. Os 9 buffers dinamicos do PBR

Compilado `pbr.hlsl` por estagio (`glslangValidator -D -V -S vert -e VSMain` e `-S frag -e PSMain`) e lido com `spirv-dis`:

| Estagio | Buffers uniformes (binding) |
|---|---|
| VS | FrameBuffer (4), ObjectBuffer (6) |
| PS | GlobalLightCB (0), SunLightCB (1), CsmCB (2), IblCB (3), ClusterGridCB (5), ObjectBuffer (6), PointShadowCB (7) |

- Total por estagio: 2 + 7 = **9**. Bindings unicos: 8 (b0..b7). A soma de 9 bate com o numero relatado. A hipotese (nao confirmada em runtime) e que o Diligent conta os buffers por estagio.
- **O resource extra e `ObjectBuffer` (b6) no PS.** O PS nao e para ler esse buffer segundo o comentario de `pbr.hlsl`, mas `PSMain` le `fogParams` e `fogColor` (membros 9 e 8 da struct) direto do buffer: `if (fogParams.z > 0.0)` e `lerp(color, fogColor.rgb, f)`. No SPIR-V aparecem 6 acessos a esses membros no PS (`OpAccessChain %__5 %int_8/%int_9`).
- O comentario de `pbr.hlsl` e o de `diligent_device.cpp` diziam que o PS nao lia b6. Isso e falso desde que a neblina entrou no PS. Os comentarios foram corrigidos para dizer o estado atual.

### Correcao (APLICADA em pbr.hlsl; GLSL regenerado)

Passar a neblina do VS para o PS por `nointerpolation`, como ja e feito com `baseColor`, `material`, `texInfo` e `emissive`. O valor e constante por objeto, entao o resultado por pixel e o mesmo.

```hlsl
// em struct VSOut
nointerpolation float4 fogColor  : TEXCOORD9;
nointerpolation float4 fogParams : TEXCOORD10;

// em VSMain
o.fogColor = fogColor;
o.fogParams = fogParams;

// em PSMain: trocar fogParams/fogColor por i.fogParams/i.fogColor
if (i.fogParams.z > 0.0) {
  float d = length(i.toCam);
  float f = saturate((d - i.fogParams.x) / max(i.fogParams.y - i.fogParams.x, 0.001)) * i.fogParams.z;
  color = lerp(color, i.fogColor.rgb, f);
}
```

Depois dessa mudanca: PS = 6 buffers, total por estagio = 2 + 6 = 8, que e o que o comentario original pretendia.

**Status:** aplicada. Medido de novo no SPIR-V: VS = 2 cbuffers (b4, b6), PS = 6 cbuffers (b0, b1, b2, b3, b5, b7), total 8. Os GLSL `pbr.vs`, `pbr.ps` e `pbr.emi` foram regenerados por `build_glsl.py`, `--check` sai com 0 e os tres validam no glslangValidator.

**Ainda nao verificado em runtime.** Dois motivos:
- Para testar em runtime e preciso compilar `gpu_test` com o backend Vulkan, o que e build pesado e foi proibido nesta sessao (CPU compartilhada com a medicao de CMake).
- O lavapipe (llvmpipe, que e o unico Vulkan local) reporta `maxDescriptorSetUniformBuffersDynamic = 500000`, entao ele nao reproduz o limite de 8. Um teste la so mostraria que a imagem nao mudou, nao que o erro sumiu.

Para fechar: aplicar a mudanca, rodar `gpu_test vulkan` (lavapipe) comparando a imagem antes/depois (neblina ligada e desligada) e rodar em GPU real ou com um driver que tenha o limite de 8 (conferir o log do Diligent).

## 4. Pendencias de shader (nao tratadas)

- `shadow.hlsl`, `shadow_point.hlsl`, `prepass.hlsl` e `ssao.hlsl` nao foram conferidos quanto ao limite de buffers por estagio. Nao foi feito nesta sessao.
- O caminho OpenGL usa os GLSL commitados; o caminho Vulkan e D3D12 compila o HLSL em tempo de execucao (`SHADER_SOURCE_LANGUAGE_HLSL`, `diligent_device.cpp`). Por isso uma mudanca so no HLSL nao exige recompilar o GLSL para Vulkan, mas exige regenerar `assets/shaders/glsl/pbr.*.430.glsl` (o `build_glsl.py --check` avisa).
