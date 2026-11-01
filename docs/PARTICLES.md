# Sistema de partículas

Código: `apps/editor/fx.hpp` (simulação e lotes), `apps/editor/components.hpp` (`ParticleComp`), `apps/editor/editor_state.hpp` (`renderScene`).
Documento de referência recebido: "Design de Sistema de Partículas de Próxima Geração" (compute shaders, indirect draw, zero-copy, sub-emitters, malhas, fraturamento, áudio espacial).

## 1. Resposta direta: a solução anterior era perigosa?

Sim. Antes desta etapa, **cada partícula era uma chamada de desenho completa** com um cubo de 12 triângulos. Um emissor com `maxParticles = 5000` gerava até 5000 chamadas por quadro; vários emissores somavam milhares. Além disso, as partículas eram desenhadas como overlay 2D (sem profundidade). Isso causava custo de CPU (uma chamada de desenho por partícula) e de GPU (triângulos desnecessários), e o limite era por emissor, sem teto global.

Agora:
- **Lotes:** todas as partículas do quadro viram quads voltados para a câmera, agrupados por cor. Cada lote é **uma** malha dinâmica = **uma** chamada de desenho. Com 4 níveis por canal de cor, há no máximo **64 lotes** por vista, independente de quantas partículas existem.
- **Teste de profundidade:** os lotes passam pelo pipeline 3D normal, então a partícula fica atrás dos objetos que estão na frente (e continua emitindo).
- **Culling:** partículas fora do frustum são descartadas antes de entrar no lote (esfera contra os 6 planos).
- **Orçamento global:** `particleBudget` (padrão 100 000 partículas vivas somando todos os emissores). O limite por emissor continua valendo.
- **Simulação em paralelo:** a fase de criação e integração roda em `tool::parallelFor`, um emissor por tarefa. A leitura do mundo continua serial (o acesso a `worldMat` não é thread-safe).
- **Limpeza sem `std::find`:** emissores apagados saem do mapa por marca de quadro (antes era O(emissores × vivos)).
- **Malhas dinâmicas na GPU:** `GpuRenderer::drawDynamic(slot, ...)` reaproveita os buffers por slot e só recria quando a malha cresce além da capacidade (com folga de 1,5×).

Limites que continuam:
- A simulação é na **CPU**. Com 100 mil partículas, o custo de integração é linear e aparece no Profiler (zona Scene/Game render e o tempo do update).
- A cor é quantizada em 4 níveis por canal (16 tons de cada cor base). Gradientes longos podem mostrar degraus.
- Partículas não têm alpha real: o "some" é escurecer e encolher.

## 2. O que do documento de referência é viável agora

| Item do documento | Situação nesta engine | Motivo |
|---|---|---|
| Estrutura SoA (`ParticleStateComponent`) | Parcial | Cada emissor tem sua lista de partículas; SoA só compensa quando a simulação vai para a GPU ou para SIMD. Próximo passo. |
| Simulação em compute shader | **Não agora** | O RHI não tem compute nem buffers estruturados; e a imagem da GPU ainda é lida de volta para a CPU a cada quadro. Sem zero-copy, a simulação na GPU não ganha, porque o resultado precisa voltar. |
| Indirect draw + culling na GPU | **Não agora** | Mesmo motivo (exige compute e indirect no RHI). Equivalente aproximado já feito: culling na CPU + lotes. |
| Zero-copy de apresentação | **Não agora** | Exige apresentar a textura da GPU direto no ImGui (sem `readback`). É o maior custo de sincronização do editor, listado em `docs/PERFORMANCE_AUDIT.md`. |
| Vento por textura 3D | Viável depois | Campo de vetores amostrado na CPU (trilinear) em cada partícula: custo alto sem SIMD; começar com campo de baixa resolução. |
| Espiral parametrizada (`cos/sin` de idade) | **Viável** | Barato na CPU; pode entrar como opção do emissor. |
| Quaternião e Slerp para rotação | Viável | Só relevante quando houver rotação por partícula (quads já são billboards). |
| Distribuição esférica por rejeição/Fibonacci | Viável | Hoje o cone usa amostragem uniforme; trocar é local. |
| Billboards por Right/Up da câmera | **Feito** | Os quads são montados com a base da câmera. |
| Mesh emission (vértices/triângulos) | Viável depois | Amostrar vértices de um `Mesh` como origem. |
| Sub-emitters, fraturamento, áudio espacial | Depois | Dependem de eventos de colisão do Newtoon e de um sistema de áudio por fonte; planejados. |
| Integração Euler semi-implícita | **Feito** | `v += g·dt; p += v·dt` já é a ordem usada. |

## 3. Como usar

No Inspector, o componente **Particle System** (`ParticleComp`) define: taxa, vida, velocidade, cone (`spread`), tamanho inicial/final, cor inicial/final, alpha, máximo de partículas, quique no plano (`bounce`, `floorY`).

Para a sua máquina (10/20 núcleos): o orçamento padrão de 100 mil é alto. Para testar o custo, ajuste `particleBudget` no código (`apps/editor/editor_state.hpp`) ou use o teste de ECS com partículas e observe a zona **Update** no Profiler.

## 4. Testes

- `particles: emitter spawns and respects the max count` (selftest, já existente).
- `particle budget caps the live particles across emitters`: 60 quadros com orçamento 25 → nunca mais de 25 partículas vivas.
- `particle batches: at most one quad per live particle and at most 64 batches`.
- `particle batches: fewer draw calls than particles (colors share a batch)`.
