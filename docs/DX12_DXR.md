# Direct3D 12 e Ray Tracing (DXR)

## Como compilar com Direct3D 12 (Windows)
Pré-requisitos: Windows 10/11, Visual Studio 2022 (workload "Desenvolvimento para desktop com C++"), CMake ≥ 3.25, Git, Windows SDK recente.
Abra o "x64 Native Tools Command Prompt for VS 2022" na pasta do projeto e rode:

```
cmake --preset editor-gpu-dx12
cmake --build --preset editor-gpu-dx12 --config RelWithDebInfo
```
(o preset liga `-DTOOL_WITH_D3D12=ON`; o DiligentCore compila o backend D3D12). Execute o editor e escolha **Direct3D 12** em *Settings > Graphics*,
ou use `ToolEditor.exe --api d3d12`.
Se a escolha não aparecer, o backend não foi compilado nesse build (confira o preset). O backend D3D12 **ainda não foi testado em hardware** —
se der erro, copie a mensagem do Console (ela aparece em *Settings > Graphics*) para eu corrigir.

## Ray tracing com DXR — viabilidade
- O DiligentCore (que usamos por baixo do RHI) já expõe a API de ray tracing: BLAS/TLAS (`IBottomLevelAS`/`ITopLevelAS`), `IShaderBindingTable` e `TraceRays`, em D3D12 (DXR) e Vulkan (VK_KHR_ray_tracing).
- Para usar na engine seria preciso: (1) estender o nosso RHI com construção de BLAS/TLAS e `TraceRays`; (2) shaders HLSL de ray tracing compilados com DXC (hoje só VS/PS);
  (3) detectar suporte em tempo de execução (GPU RTX/RDNA2+); (4) caminho de apresentação direto na GPU (item 3 do `NEXT_STEPS.md`) — hoje cada quadro volta para a CPU, o que desperdiça o ganho.
- **Primeiro alvo realista:** sombras (e depois reflexos) por ray tracing de 1 raio por pixel, ligadas por opção e só quando a GPU suportar. Isso **substitui** o cubemap de sombra de point light com qualidade melhor.
- Estimativa: média/grande. Recomendação: fazer depois de (a) apresentar direto na GPU e (b) pós-processamento na GPU.
