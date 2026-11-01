# Plano de desempenho do editor (0.7.7)

Este documento junta o diagnóstico dos stutters, o que já foi corrigido, o que está no plano e a avaliação
do relatório externo de otimização. A ordem das etapas segue o impacto medido no próprio editor.

## 1. Diagnóstico (a partir do Profiler)

As duas capturas do Profiler (RX 6500 XT, Vulkan, 1 vista, 8 objetos) mostram:

| Sintoma | Causa encontrada no código | Estado |
|---|---|---|
| "Events + input" 16 ms | A espera do modo ocioso (`SDL_WaitEventTimeout`) ficava dentro da zona de medição | Corrigido: espera fora da zona, limite de 16 ms |
| Picos de ~100 ms no gráfico | Espera ociosa de 100 ms em quadros sem evento | Corrigido: limite de 16 ms |
| "Post-processing" 4 a 8 ms com o pós-processamento desligado | O skybox era pintado dentro da zona de pós-processamento | Corrigido: zona própria "Skybox (images)" |
| Partículas com stutter | Vista ociosa reaproveitava a imagem antiga enquanto as partículas andavam; o laço dormia entre os quadros | Corrigido: `fxAnimating` impede o modo ocioso com emissor vivo |
| FPS preso em ~57 com VSync ou FXAA | VSync de monitor de 60 Hz e FXAA em CPU (ver seção 3) | Parcial: VSync é esperado; FXAA segue no plano |
| "Present + frame limiter" 3 a 5 ms | Espera do VSync e a cópia para a janela | Esperado com VSync; reduz com a etapa 2 |
| "Space in dynamic heap is exhausted" | Heap de 8 MB no Diligent (padrão) | Corrigido no Vulkan: 64 MB, configurável |

## 2. Caminho da GPU: o custo que sobrou

Hoje, em cada vista e em cada quadro, a GPU faz:

1. Renderiza a cena offscreen.
2. `CopyTexture` para uma textura de leitura (staging).
3. `WaitForIdle()`: a CPU para e espera a GPU terminar (`engine/rhi_diligent/src/diligent_device.cpp`, `readTexture`).
4. Copia os 8 MB (1080p) para a CPU.
5. Aplica skybox e pós-processamento na CPU.
6. Envia a imagem de volta para uma textura do SDL (mais 8 MB) e apresenta.

Os passos 3 e 4 serializam CPU e GPU: os dois tempos se somam em vez de se sobrepor. Esse é o principal
custo que sobrou.

### Etapa 2 (próxima): readback assíncrono com anel de staging

Padrão usado por engines (Unreal faz o mesmo com `FRHIGPUTextureReadback`: copia para a memória de leitura e
consulta o resultado em quadros seguintes, sem esperar a GPU):

- Dois ou três buffers de staging por vista (um anel), com o valor de um fence por slot.
- No quadro N: `CopyTexture` para o slot atual e `ctx->EnqueueSignal(fence, valor)`.
- No quadro N+1 (ou N+2): se `fence->GetCompletedValue() >= valor`, mapear o slot e copiar para `rgba`.
- O Diligent expõe isso em `IFence` e `IDeviceContext::EnqueueSignal` (`Graphics/GraphicsEngine/interface/Fence.h`
  e `DeviceContext.h` no `third_party/DiligentCore`).

**Custo:** a imagem da vista fica 1 quadro atrás (16 ms a 60 Hz). Para edição de cena é aceitável; para a
Game view, sentir o atraso é possível. Por isso a etapa exige uma opção (`Settings > Graphics > Readback
assíncrono`), desligada até validar na sua máquina.

**Risco:** o `gpu_test` compara um quadro imediato. Ele precisa ler dois quadros para medir o anel.

### Etapa 3: zero-copy (apresentar a textura da GPU sem voltar para a CPU)

O caminho ideal: a vista vira uma textura que a própria UI (ImGui) desenha. Hoje a UI usa o renderer do SDL,
então isso exige o backend ImGui do Diligent ou um backend próprio. É a mudança mais cara; fica depois da
etapa 2, que já tira a espera.

### Etapa 4: FXAA e pós-processamento na GPU

`applyPostFx` (FXAA, nitidez) roda em CPU sobre a imagem inteira, em paralelo por faixas. Com a etapa 3,
vira um passo de tela cheia no Diligent (um shader). Até lá, a cópia para a CPU continua sendo o gargalo.

## 3. Outros custos conhecidos

- **Skybox em CPU**: uma conta por pixel de fundo, em paralelo. Já é mais barato que o procedural anterior
  (sem `atan2`/`sin` por pixel). Próximo passo: cachear a imagem do skybox enquanto a câmera não mudar.
- **Validação de camadas**: custa CPU alta. Desligada por padrão; use `TOOL_GPU_VALIDATION=1` só para depurar.
- **Partículas**: simulação em paralelo por emissor e lotes por cor (≤ 64 chamadas). A próxima melhoria
  depende de compute no RHI (ver CHECKLIST).

## 4. Avaliação do relatório externo ("Optimization Report")

O relatório foi lido contra o código. Resultado:

| Item do relatório | Veredito | Ação |
|---|---|---|
| `resetTo` com `std::find` dentro do laço | Procede (custo quadrático em componentes) | Feito: vetor de bits |
| `nextComponentId` sem proteção de overflow | Procede, mas improvável na prática | Feito: aborta antes de dar a volta |
| `ensure` sem `reserve` em `tags_`/`dormant_` | Pouco relevante: `resize` já cresce de forma amortizada | Não mudado |
| `each` copia o vetor de índices | Proposital: a callback pode destruir entidades durante a iteração | Mantido; medir com `test ECS: 1000000` antes de mudar |
| `Storage::remove` deixa `denseIdx` inconsistente | Não procede: o código atualiza `sparse` e `denseIdx` corretamente, inclusive quando o removido é o último | Sem ação |
| Paralelizar `each` com `std::execution::par` | Arriscado: `each` escreve componentes e pode criar/destruir entidades | Não feito |
| SoA para `Transform` | Procede como meta de longo prazo; exige reescrever o acesso a componentes | Pendente (CHECKLIST, "Arquétipos/chunks") |
| Pool de storages e arena | Ganho pequeno frente aos itens acima | Pendente |
| Profiler integrado | Já existe (`Profiler`, zonas por quadro) | Ampliado: zona do skybox e espera fora da medição |

Outras fontes de referência para a etapa 2 e 3: a documentação de readback da Unreal Engine
([FRHIGPUTextureReadback](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/RHI/FRHIGPUTextureReadback?application_version=5.4))
descreve o mesmo padrão de copiar e consultar depois.

## 5. Como medir antes e depois

1. Abrir o Profiler e deixar a cena parada por 10 s. "Events + input" deve ficar perto de 0 (a espera não conta).
2. Com partículas na cena: o gráfico de frame time não pode ter platôs de 100 ms.
3. Ligar e desligar o skybox e comparar "Skybox (images)" e "Post-processing".
4. `ctest` (4 testes) e `ToolEditor --selftest` devem continuar sem falhas.
5. Para a etapa 2: comparar "Present + frame limiter" e o FPS médio com a opção ligada e desligada.
