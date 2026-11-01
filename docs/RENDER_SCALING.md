# Escalonamento e antisserrilhado por software

Código: `engine/render/include/tool/render/upscale.hpp`. Integração: `apps/editor/editor_state.hpp` (`finishView`, `softScaling`) e `apps/editor/main.cpp` (Scene e Game). Configuração: **Settings → Graphics**.
Testes: `tests/test_render.cpp` (4 testes, todos passam).

## 1. O que existe

**Upscaling por software** (`UpscaleFilter`):

| Opção | Núcleo | Observação |
|---|---|---|
| Off (SDL linear) | Filtro linear do SDL na apresentação | Padrão do caminho rápido. Não usa CPU extra. |
| Bilinear | 2×2 amostras | Mais barato; suave. |
| Bicubic (Catmull-Rom) | 4×4 amostras | Mais nítido que bilinear; pode ter pequeno anel em bordas fortes. |
| Lanczos2 | 4×4 amostras, janela sinc | Borda mais nítida; pode ter anel. |

Implementação: filtro **separável** (horizontal e vertical). Os índices e pesos de cada eixo são pré-calculados uma vez por tamanho (`Axis`), com os índices já limitados às bordas da imagem. O laço externo roda por linha em `tool::parallelFor`.

**Antisserrilhado por software** (`fxaaRgba`): versão simplificada de FXAA. Calcula luma uma vez por pixel, detecta borda por contraste local (limiar relativo e absoluto) e mistura ao longo da direção da borda. Constantes: `kReduceMul = 1/8`, `kReduceMin = 1/128`, `kSpanMax = 8`, `kEdgeThreshold = 1/8`, `kEdgeThresholdMin = 1/32`.

**Controles** (Settings → Graphics): "Upscaling (software)" (Off, Bilinear, Bicubic, Lanczos2) e "Anti-aliasing (software)" (Off, FXAA). Ambos são salvos em `editor_settings.json` (`upscaleMode`, `aaMode`) e limitados ao intervalo válido ao carregar.

## 2. Erros corrigidos durante o desenvolvimento

- **Pesos do bilinear deslocados em um pixel.** A saída de uma rampa saía `48 80 ...` em vez de `16 48 80 ...`. Causa: o índice inicial era `base` em vez de `base − 1`. Corrigido para todos os filtros; o bilinear usa só os dois pontos centrais.
- **Teste de bilinear com valor errado no teste** (esperado 176, correto 192 com o clamp da borda). Corrigido o teste.
- **FXAA não suavizava borda vertical reta.** Isso é o comportamento correto (o FXAA não mexe em borda sem serrilhado). O teste passou a usar uma escada diagonal.

## 3. Testes

- `upscale_flat_image_stays_flat_for_every_filter`: imagem lisa continua lisa em todos os filtros.
- `upscale_bilinear_linear_ramp_is_exact`: rampa linear reproduz os valores exatos (`dst[7*4] ≈ 192`).
- `upscale_lanczos_and_bicubic_keep_edges_monotonic_bounds`: valores ficam dentro dos limites da entrada (sem overshoot fora da faixa).
- `fxaa_flat_image_unchanged_and_edge_is_softened`: imagem lisa inalterada; escada diagonal ganha valores intermediários.

Resultado: `tool_tests` **131 testes, 0 falhas**.

## 4. Medições

Medidas com `/home/claude/scratch/bench_up.cpp` (`-O2`, 2 núcleos, ambiente de desenvolvimento):

| Operação | Tempo |
|---|---|
| Bilinear 960×540 → 1920×1080 | 36 ms |
| Bicubic 960×540 → 1920×1080 | 94 ms |
| Lanczos2 960×540 → 1920×1080 | 88 ms |
| FXAA 1920×1080 | 175 ms |

Leitura honesta:
- Em **2 núcleos**, nenhum desses filtros cabe em 16 ms (60 fps). Em uma máquina de 20 threads o custo cai por um fator próximo do número de núcleos, mas **não medi isso aqui**. Confirme no seu computador antes de usar em tempo real.
- **FXAA é o mais caro** e por isso fica **Off por padrão**. O custo é por pixel de tela, então cresce com a resolução.
- O upscaling por CPU **só se justifica** quando a janela de jogo está em resolução fixa menor que a tela. Com a janela cheia, o filtro linear do SDL já é gratuito.

## 5. Por que não GPU agora (e FSR depois)

Os filtros acima rodam na CPU porque a imagem final já é lida da GPU para a CPU (readback, ver `docs/PERFORMANCE_AUDIT.md` A8). Fazer o upscaling na GPU (shader) só é vantajoso depois de eliminar esse readback; caso contrário, a imagem sobe e desce de novo.

**FSR** (AMD FidelityFX Super Resolution) e outras técnicas (TAA, DLSS, XeSS) são o próximo passo planejado. Elas exigem:
1. Shader de computação ou de pixel no RHI (hoje o RHI não tem compute).
2. Entradas de movimento e profundidade (vetores de movimento por objeto).
3. Apresentação sem readback.

Por isso o FSR fica para uma etapa posterior; não foi implementado nesta etapa. A análise de FSR (versões, licenças, o que faz sentido aqui) está em `docs/FSR.md`; o desempenho de pós-processamento e sombras está em `docs/RENDER_PERF.md`.
