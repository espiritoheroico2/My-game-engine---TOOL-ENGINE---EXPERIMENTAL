# Light Prism — Convenções (Fase 0)

Estas decisões valem para C++, HLSL e testes. Mudar uma delas exige mudar os três.

| Item | Convenção |
|---|---|
| Unidade de mundo | 1.0 = 1 metro |
| Sistema de coordenadas | Mão direita, Y para cima |
| Espaço de cor | Cálculo em linear HDR; sRGB só no final (tonemap) |
| Profundidade | Z reverso (near = 1, far = 0), float32 depth |
| Direção da luz | `direction` = direção em que os raios **viajam** (luz -> cena). Shader usa `L = -direction` |
| Intensidade | Candela-like (pontual/spot) e lux-like (direcional), multiplicada por `preExposure` na CPU |
| Rugosidade | `roughness` perceptual [0,1]; `alpha = roughness²`; NDF usa `alpha²`. Mínimo 0.045 |
| Raio de luz | `radius` em metros; atenuação vai a zero exatamente em `d = radius` |
| Profundidade das sombras | Shadow maps usam profundidade **convencional** (perto = 0, longe = 1), não o Z reverso da câmera. Texel: x = (clip.x·0,5+0,5)·N, y = (0,5−clip.y·0,5)·N |
| Matrizes | `Mat4` row-major, vetor-coluna (`v' = M·v`). No HLSL: `row_major float4x4` + `mul(M, v)`. Depth [0,1]; ortográfica convencional (near=0) |
| Layout GPU | Structs de 16 em 16 bytes, validados por `static_assert` (C++) e espelhados em `LightTypes.hlsli` |

## Atenuação

`att(d) = saturate(1 - (d/R)^4)^2 / max(d², 1e-4)`

O piso de 1e-4 (1 cm) substitui o `+1` do documento original, que adicionava uma unidade espúria.

## Spot

Atenuação angular (forma do Filament): `t = saturate(cosθ * scale + offset)`, `att = t²`,
com `scale = 1 / max(cosInner - cosOuter, 1e-4)` e `offset = -cosOuter * scale`.
Pré-calculado na CPU, custa um FMA no shader.

## Orçamento de luzes

`LightingConfig::maxPointLights / maxSpotLights` limitam o que sobe para a GPU. No overflow,
ficam as luzes de maior prioridade (`intensidade * cor / (distância_à_borda² + 1)`), e o descarte
é reportado em `FrameLightView::droppedLights`. Nunca há overflow silencioso.
`BuildFrame` entrega as luzes ordenadas por prioridade decrescente (o culling em clusters depende disso).

## Tone mapping

ACES Film (Narkowicz) seguido de conversão sRGB exata (curva por partes, não `pow(1/2.2)`).
A exposição já está nas intensidades (`preExposure`); o tonemap não a reaplica. Se o render target
final for `*_SRGB`, use só `ACESFilm3()` e deixe o hardware converter.

## IBL e SSAO (Fase 4)

- SH: ordem Y00, Y1(y,z,x), Y2(xy, yz, 3z²−1, xz, x²−y²); `IblGPU` guarda coeficientes × constantes da base × (A_l/π), então
  `E(n)/π = Σ sh[i]·poli_i(n)`. Difuso indireto = `kD · albedo · E/π · intensity · ao`.
- Cubemap: ordem e orientação D3D; mip i = rugosidade perceptual i/(mips−1). LUT: x = N·V, y = rugosidade, texel centrado.
- SSAO: profundidade linear de view (positiva) e normais em view space; raio, bias em metros; AO 1 = livre.
