# Light Prism na engine

A iluminacao da ToolEngine e calculada pela biblioteca **Light Prism** (`third_party/LightPrism`). Ela substitui o
sistema antigo (Forward+ proprio, atenuacao e `shadow.hpp`) nos dois renderizadores.

## Quem faz o que

| Camada | Arquivo | Papel |
|---|---|---|
| Ponte | `engine/render/include/tool/render/lighting.hpp` | `Light`, `LightingSettings` (Settings > Lighting), `LightRig` (monta o quadro) |
| CPU | `engine/render/include/tool/render/soft_renderer.hpp` | `setLighting(frame, reuse)`: BRDF, `CsmVisibility`, SH9 |
| GPU | `engine/render/include/tool/render/gpu_renderer.hpp` | `setLighting(rig, view, proj, reuse)`: sobe luzes, roda clusters, importa buffers |
| Dispositivo | `engine/rhi_diligent/src/lightprism_bridge.cpp` | `createLightPrismDevice`: dispositivo Light Prism sobre o RHI Diligent |
| Shaders | `assets/shaders/pbr.hlsl`, `assets/shaders/lightprism/` | PBR com `EvaluateClusteredLights`, sombra CSM adaptada ao RHI |

## Fluxo por quadro

1. `collectLights` monta as luzes da cena (sol da Global Light, luzes pontuais e spot).
2. `LightRig::configure` aplica as configuracoes. Recria o `LightingSystem` so quando o orcamento de luzes muda.
3. `LightRig::build` remove e readiciona as luzes, monta o quadro (`BuildFrame`), calcula as cascatas (`ComputeCsm`,
   so se o sol projeta sombra) e o ambiente em SH9.
4. CPU: `SoftRenderer::setLighting`. GPU: `GpuRenderer::setLighting` (upload, clusters, importacao dos buffers).
5. Se a GPU falhar, o editor desenha o quadro na CPU e mostra o motivo.

## Convencoes que importam

- A engine guarda `Mat4` por coluna (`m[coluna][linha]`, translacao em `m[3][0]`). A Light Prism guarda por linha
  (`m[linha][coluna]`, translacao em `m[0][3]`). A conversao transpoe: `LightRig::toLp` e `SoftRenderer::toTool`.
- `Light::direction` e a direcao em que a luz viaja (como na Light Prism).
- Sombras do sol: 4 mapas R32F no maximo (`CsmMap0..3`). A engine nao tem `Texture2DArray` com `SampleCmp`, entao o
  shader le cada texel com `Load` e faz o PCF na mao.
- Reuso de sombras: a assinatura do quadro (sol, objetos que projetam, camera, ajustes) decide se os mapas sao
  reaproveitados. Ver `Editor::shadowSignature`.

## Configuracao (Settings > Lighting)

Salva em `render.lighting` no `.scene.json`. Presets 0 a 3 (Mobile, Baixa, Media, Alta). Com "Valores personalizados"
ligado, cascatas, resolucao, PCF e parametros de cluster valem no lugar do preset. Orcamento de luzes e sombras do
sol sempre valem.

## Testes

- `tool_tests` (`test_render.cpp`): `LightRig` (mapeamento, orcamento, sombra do sol, presets), renderizador CPU,
  reuso de sombra, textura.
- `gpu_test vulkan`: compara a GPU com a CPU (mesma silhueta, mesma textura) e confere a sombra no chao.
- `gpu_test opengl`: pula com aviso (GLSL pendente).

## Pendencias

- OpenGL: iluminacao em GLSL (`TOOL-E141` hoje).
- D3D12: teste na GPU real.
- Sombras da GPU: o teste de cena encontra 172 pixels escurecidos (limite 150). A cena de teste e pequena na tela, por
  isso a diferenca de luminancia total e pequena (razao 0,996). Vale conferir visualmente no editor.
