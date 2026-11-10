# Light Prism

Biblioteca de iluminação e sombras 3D, leve e independente de engine e de API gráfica.

## Estrutura

```
LightPrism/
├── CMakeLists.txt
├── docs/
│   ├── CONVENTIONS.md             # unidades, handedness, Z, layouts, fórmulas fixadas
│   ├── CLUSTERS.md                # pipeline do clustered culling, memória, bindings, ClusterSystem
│   ├── SHADOWS.md                 # CSM: estabilidade, fluxo por frame, validação
│   ├── IBL.md                     # IBL (SH9 + split-sum) e SSAO: matemática, uso, validação
│   ├── OPTIMIZATIONS.md           # 10 melhorias/otimizações e redução de memória, com números medidos
│   ├── PHASE6.md                  # VSM, sombras por SDF, neblina volumétrica, probes de GI
│   ├── RAYTRACING.md              # BVH, RT com limite de distância, PRT (RT com cache), limitações
│   └── images/                    # saídas do lp_softrender (lit, heatmap, slices, diff)
├── include/LightPrism/
│   ├── Math.hpp / Mat4.hpp        # Vec3, Mat4 (LookAt, perspectiva Z reverso, ortográfica)
│   ├── LightTypes.hpp             # descrições CPU + structs GPU (static_assert de layout)
│   ├── Rhi.hpp                    # IGraphicsDevice: buffers (SRV/UAV/CBV/readback), compute, barreiras, cópia
│   ├── Quality.hpp                # presets Mobile/Low/Medium/High + SuggestTier()
│   ├── LightingSystem.hpp         # API pública: luzes -> BuildFrame -> UploadToGPU
│   ├── Cluster/                   # ClusterTypes, ClusterCulling (referência + fallback CPU), ClusterSystem (GPU)
│   ├── Shadow/Csm.hpp             # cascatas, CsmGPU, referência de amostragem, CsmSystem
│   ├── Ibl/Ibl.hpp                # SH9, cubemap pré-filtrado, LUT da BRDF, EvaluateIndirect, IblGPU
│   ├── Post/Ssao.hpp              # SSAO (kernel, bruto + blur bilateral), SsaoGPU
│   ├── Shadow/Vsm.hpp, Sdf/Sdf.hpp, Volumetric/Fog.hpp  # Fase 6: VSM, SDF, neblina em froxels
│   ├── Rt/                        # ProbeGrid (GI), Bvh (SAH/LBVH, tMax = limite de distância), RtEffects (sombra/AO), Transfer (PRT com cache)
│   ├── MemoryBudget.hpp           # estimador de memória por preset
│   ├── Soft/                      # SoftRenderer, DemoScene, Png (renderizador por software)
│   ├── Backends/DiligentDevice.hpp# CreateDiligentDevice(device, context)
│   └── Reference/                 # Brdf.hpp, Tonemap.hpp (referência CPU, golden tests)
├── src/
│   ├── Core/                      # LightingSystem, SlotMap (interno)
│   ├── Cluster/                   # ClusterCulling.cpp, ClusterSystem.cpp
│   ├── Shadow/Csm.cpp             # ComputeCsm, CsmSystem
│   ├── Ibl/Ibl.cpp / Post/Ssao.cpp
│   ├── Rhi/NullDevice.cpp         # backend em memória (testes/CI): registra comandos, Update/Copy/ReadBack reais
│   ├── Soft/                      # SoftRenderer.cpp, Png.cpp
│   └── Backends/Diligent/         # IGraphicsDevice sobre Diligent (-DLP_ENABLE_DILIGENT=ON)
├── shaders/
│   ├── Common/                    # LightTypes, LightBindings, PBR, Tonemap, Cluster, ClusterLighting, Shadow, Ibl (.hlsli)
│   ├── PostProcess/               # Ssao.compute.hlsl (bruto + blur)
│   └── Cluster/                   # 4 compute shaders (prep, count, scan [2 kernels], fill)
├── tools/softrender/              # lp_softrender: gera PNGs + estatísticas
└── tests/                         # 7 executáveis de teste (ctest)
```

## Build, testes e imagens

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
./build/lp_softrender docs/images 960 540 120 12 16 1.5 1 1   # dir largura altura pontuais spots tile sol sombras ibl
```

Não precisa de GPU, driver nem API gráfica: o `lp_softrender` renderiza no CPU com a mesma matemática da
biblioteca e prova que o culling em clusters dá exatamente a mesma imagem que percorrer todas as luzes.

| Teste | O que cobre |
|---|---|
| `lp_tests` | layouts GPU, BRDF/GGX, atenuação, spot, handles, orçamento, upload, Mat4, tonemap |
| `lp_cluster_tests` | grid/fatias, AABBs, interseções, invariante de culling, teto, overflow, scan hierárquico |
| `lp_soft_tests` | ponta a ponta: clusters == todas as luzes (vários tiles/fatias), orçamento, teto por cluster, PNG |
| `lp_cluster_system_tests` | ordem dos dispatches e barreiras, bindings conferidos contra os `.hlsl`, readback, fallback CPU, erros |
| `lp_shadow_tests` | CSM: splits, cobertura, estabilidade do texel snapping, blend, shadow map vs oclusão analítica, CsmSystem |
| `lp_ibl_tests` | SH9 vs integral, cubemap, LUT da BRDF vs força bruta, pré-filtro, split-sum completo vs integral, espelhos HLSL |
| `lp_ssao_tests` | SSAO vs AO por raios, piso plano sem auto-oclusão, raio monotônico, kernel, linearização do reverso-Z, layout |
| `lp_opt_tests` | raio automático, clusters ativos, orçamento adaptativo, cache de cascatas, sombra planar, SSAO meia-res, empacotamento, SH L1, estimador de memória |
| `lp_phase6_tests` | VSM (Chebyshev = limite superior, vazamento, acne, ponta a ponta no CSM), SDF (dura/suave vs RT, volume int8, rebake), neblina (solução analítica, vs marcha fina), probes de GI (vs verdade, vazamento, orçamento, invalidação) |
| `lp_rt_tests` | BVH (SAH e LBVH) vs força bruta com e sem limite de distância, degenerados, Refit, AO/sombra RT vs verdade, PRT (consistência com IBL, vs força bruta, incremental == do zero) |

## Uso mínimo

```cpp
LightPrism::LightingSystem lights;

LightPrism::PointLightDesc torch;
torch.position = {0, 2, 0};
torch.radius = 10.0f;
torch.intensity = 1500.0f;
lights.AddPointLight(torch);
lights.SetDirectionalLight({});

auto device = LightPrism::CreateDiligentDevice(pRenderDevice, pImmediateContext);
LightPrism::ClusterSystem clusters;              // shaderDirectory = pasta shaders/ instalada

// por frame
LightPrism::FrameParams fp;
fp.cameraPosition = camPos;
fp.preExposure = exposure;
const auto& frame = lights.BuildFrame(fp);
lights.UploadToGPU(*device);
clusters.Execute(*device, lights, frame, LightPrism::MakeClusterCamera(view, proj, w, h, zNear, zFar));
// passe de iluminação: GridBuffer(), LightGridBuffer(), LightIndexBuffer() + shaders/Common/ClusterLighting.hlsli
```

## Roadmap

| Fase | Conteúdo | Estado |
|---|---|---|
| 0 | Convenções, layouts, golden tests | feito |
| 1 | PBR (Cook-Torrance), point/spot/directional, orçamento de luzes | feito (CPU + HLSL) |
| 1b | Backend Diligent, Mat4, tone mapping ACES | feito (Diligent: sintaxe verificada, ainda sem teste em GPU) |
| 2 | Clustered culling com prefix sum | feito (CPU e orquestração testadas; HLSL escrito, ainda não compilado) |
| 2b | RHI com compute/UAV/barreiras/readback, `ClusterSystem`, renderizador por software | feito (Diligent: sintaxe verificada, sem teste em GPU) |
| 3 | CSM estável (texel snapping) | feito (validado no renderizador por software; HLSL não compilado; shadow pass é da engine) |
| 4 | IBL (SH9 + split-sum), SSAO | feito (validado no renderizador por software, 15 mutações pegas; HLSL não compilado; texturas/passes são da engine) |
| 5 | 10 otimizações + memória (`OPTIMIZATIONS.md`), ray tracing por software com limite de distância e PRT com cache (`RAYTRACING.md`) | feito (validado no renderizador por software; HLSL não compilado; sem backend DXR; passe HLSL de clusters ativos não escrito) |
| 6 | VSM, sombras suaves por SDF, neblina volumétrica, probes de GI (`PHASE6.md`) | feito (validado em CPU; HLSL não compilado; sem atualização HLSL dos probes) |
| 7 | backend DXR/Vulkan RT, cascatas de SDF/probes, reprojeção temporal da neblina | |
