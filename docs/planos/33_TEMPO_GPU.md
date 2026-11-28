# 33. Tempo de GPU por passe (timestamps no RHI)

Antes o RHI nao expunha tempo de GPU (lacuna do Cap. 3 do livro). Agora cada passe do `GpuRenderer` pode ser medido com
consultas de timestamp, de forma assincrona (sem bloquear o quadro) e desligada por padrao.

## API (engine/rhi/include/tool/rhi/rhi.hpp)

```cpp
// ICommandList
virtual void beginGpuZone(const char* name);   // abre uma zona (mesmo nome no quadro soma; pode aninhar)
virtual void endGpuZone();                     // fecha a zona aberta mais recente

// IDevice
virtual bool   gpuTimersSupported() const;     // backend tem timestamps?
virtual void   setGpuTimers(bool on);          // liga/desliga em tempo de execucao
virtual bool   gpuTimersEnabled() const;       // pedido E suportado
virtual void   gpuZoneBegin(const char* name); // igual a ICommandList::beginGpuZone, direto no dispositivo
virtual void   gpuZoneEnd();                   // (zona que cobre varias listas/submits)
virtual double gpuZoneMs(const char* name) const;                         // ultimo valor resolvido, em ms
virtual std::vector<std::pair<std::string,double>> gpuZones() const;      // todas as zonas do ultimo quadro resolvido

// DeviceInfo
bool gpuTimers = false;                         // liga na criacao do dispositivo
```

O contexto imediato do Diligent e unico, entao a zona vive no dispositivo: `ICommandList::beginGpuZone` apenas repassa
para `IDevice::gpuZoneBegin`. O `GpuRenderer` usa o RAII `GpuZoneScope` (gpu_renderer.hpp) porque um passe tem varias listas.

## Como ligar

- Codigo: `DeviceInfo::gpuTimers = true` na criacao, ou `dev->setGpuTimers(true)` a qualquer momento.
- Editor: variavel de ambiente `TOOL_GPU_TIMERS=1` antes de abrir o editor (so Vulkan/D3D12). O Profiler mostra a linha
  `GPU: passes opaque x.xx ms | shadow ...` (sem flag, mostra a dica de como ligar). As zonas de CPU nao mudaram.

## Zonas medidas (GpuRenderer::end)

`shadow` (CSM + pontual, soma), `ssao` (SSAO + blur; o prepasso fica fora), `cull` (computes de culling), `opaque`,
`transparent`, `particles`, `bloom`. So existem no quadro se o passe rodou.

## Como funciona (Vulkan, Diligent)

Confirmado em `third_party/DiligentCore`: `QUERY_TYPE_TIMESTAMP`, `QueryDataTimestamp {Counter, Frequency}`,
`IRenderDevice::CreateQuery`, `IDeviceContext::EndQuery` (para timestamp nao existe BeginQuery), `IQuery::GetData(..., false)`
nao bloqueia e `Invalidate()`. A feature e pedida na criacao (`EngineCreateInfo::Features.TimestampQueries = OPTIONAL`) em Vulkan
e D3D12.

Cada zona usa um par de consultas. Os pares ficam em um anel de 4 quadros; em `present()` o backend tenta ler, sem bloquear,
os quadros N-1..N-3 do mais antigo ao mais novo. O resultado de um quadro costuma chegar 1 a 2 quadros depois. Se a GPU nao
entregou ate o slot ser reutilizado, aquele quadro e descartado (o valor anterior permanece).

## Medidas (RX 6500 XT, Vulkan, 640x360, ~400 cubos + esfera + chao, sol com CSM + luz pontual, SSAO ligado)

Tempo medio por zona (ms), 3 execucoes de `gpu_time_test vulkan` (30 quadros cada):

| zona   | exec 1 | exec 2 | exec 3 |
|--------|--------|--------|--------|
| opaque | 0.534  | 0.509  | 0.512  |
| shadow | 0.421  | 0.409  | 0.375  |
| ssao   | 0.317  | 0.269  | 0.277  |
| cull   | 0.098  | 0.109  | 0.100  |

Custo da medicao (`gpu_time_test vulkan bench`, 300 quadros por rodada, espera a GPU no fim; ms por quadro):

| rodada | flag desligada | flag ligada |
|--------|----------------|-------------|
| 1      | 1.354          | 1.494       |
| 2      | 1.369          | 1.479       |
| 3      | 1.351          | 1.493       |

Flag desligada: o custo e uma chamada virtual por zona (`gpuTimersEnabled()`), nenhuma consulta e criada. Flag ligada:
cerca de +0.12 ms por quadro (~9%) nesta cena, porque o timestamp no meio do fluxo obriga o Diligent a fechar o render pass
antes de gravar. A medicao serve para diagnostico, nao para deixar sempre ligada.

## Limitacoes

- Timestamps so em Vulkan (e D3D12, nao testado aqui). OpenGL e D3D11 nao pedem a feature: `gpuZoneMs()` devolve 0 e
  `setGpuTimers(true)` avisa uma vez (TOOL-W205). Null device devolve 0 sempre.
- Ordem das zonas e a ordem de aparicao no quadro; zonas com o mesmo nome somam.
- Atraso de ~2 quadros; o valor pode oscilar bastante em cenas pequenas (resolucao do contador e cargas de clock da GPU).
- O timestamp quebra o render pass aberto (custo extra so com a flag ligada), entao a soma das zonas pode passar do tempo
  real sem medicao.

## Teste

`apps/gpu_test/gpu_time_test.cpp` (alvo `gpu_time_test`, ctest `gpu_time_vulkan`): `gpu_time_test null` confere 0 e flag
desligada; `gpu_time_test vulkan` liga a flag, desenha 40 quadros e exige `opaque > 0` e `shadow > 0`; `gpu_time_test vulkan
bench` roda as 3 rodadas de custo.
