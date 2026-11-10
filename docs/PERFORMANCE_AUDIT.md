# Auditoria de desempenho e núcleo da engine

Escopo desta etapa: núcleo (ECS, GPU, paralelismo), editor (cena e jogo), sistema de partículas e memória (ASan/UBSan/LSan).

## 1. Ambiente das medições

- Máquina de desenvolvimento: **Windows x64, Intel Xeon E5-2666 v3 @ 2,90 GHz, 20 processadores lógicos**. Resultados absolutos variam por configuração e backend; use-os como referência deste ambiente, não como garantia para outra máquina.
- Os valores abaixo foram medidos aqui. Quando não houve medição, está marcado como **estimativa de código**.

## 2. Achados e estado

| # | Achado | Impacto | Estado |
|---|---|---|---|
| A1 | Uma chamada de desenho por partícula (overlay 2D sem profundidade) | Alto (CPU e GPU) | **Corrigido**: lotes por cor (≤ 64 chamadas por vista), teste de profundidade |
| A2 | Partículas sem teto global | Alto | **Corrigido**: `particleBudget` (padrão 100 000) |
| A3 | Remoção de emissores com `std::find` (O(E×V)) | Médio | **Corrigido**: marca de quadro |
| A4 | Simulação de partículas serial | Médio | **Corrigido parcialmente**: integração e criação em `parallelFor` por emissor |
| A5 | Cache de malhas da GPU com vazamento e geometria obsoleta possível | Alto (memória e corretude) | **Corrigido**: validação por `posKey` e tamanho, recriação no mesmo nó, eviction de malhas não usadas por 240 quadros (varredura a cada 64) |
| A6 | `meshBounds` crescia sem limite entre cenas | Baixo/médio (memória) | **Corrigido**: limite de 4096 entradas e limpeza na abertura de cena |
| A7 | GPU a ~70% com editor quase vazio (ociosa) | Alto (percebido) | **Corrigido em etapa anterior** (renderização só quando há mudança) |
| A8 | **Readback GPU → CPU a cada quadro** antes do `SDL_UpdateTexture` | Alto (sincronização CPU/GPU) | **Pendente**: exige apresentação direta da textura (zero-copy) no ImGui/RHI |
| A9 | `ecs.each<...>` copia a lista de índices a cada chamada | Médio com muitas entidades (uma alocação por sistema por quadro) | **Pendente**: `each` sem cópia quando a função não cria nem apaga entidades |
| A10 | Hierarquia percorre todas as entidades a cada quadro | Médio com milhares de filhos | **Pendente** (medir com `test ECS: 100000` e o Profiler) |
| A11 | `eachTagged` percorre até o maior índice usado | Baixo/médio | **Pendente**: índice por tag |
| A12 | Upscaling por software: custo por quadro na CPU | Médio (só quando ativo) | **Implementado** com paralelismo por linha; "Off (SDL linear)" é o padrão do caminho rápido |
| A13 | Contexto QuickJS por objeto com script | Alto com muitos objetos com script | **Pendente**: um contexto por arquivo + tabela de estado (ver `docs/ECS.md` §6) |

## 3. Memória: sanitizadores

Build com AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer (`build/asan`).

- **Primeira execução:** `heap-use-after-free` em `runSelfTest` (ponteiro de `ScriptComp` obtido antes de `duplicate()`, que realoca o storage). **Corrigido** em `apps/editor/selftest.hpp` (cópia de `scr->path` antes da duplicação).
- **Execução final (`--selftest` sob ASan+UBSan):** **119 checks, 0 falhas**, nenhum erro de ASan ou UBSan.
- **LeakSanitizer:** 96 bytes em 2 alocações, feitas dentro de bibliotecas carregadas com `dlopen` (inicialização estática do miniaudio ao sondar JACK). **Não é código do projeto.** Não foi suprimido; fica registrado aqui.

Observação: a execução sob ASan com `detect_leaks=1` encerra com código 1 por causa desse relatório externo. Para o uso normal do editor, o build sanitizado não é entregue; o binário de `editor-gpu` é o oficial.

## 4. Testes

- `ctest --test-dir build/editor-gpu`: 4/4.
- `tool_tests` (build/dev): **131 testes, 0 falhas**, incluindo os 4 testes novos de upscaling e FXAA.
- `editor_selftest` no build Windows Release: falha com access violation (`-1073741819`) durante os checks do runtime QuickJS; o teste `ui_editor` passa. O último check impresso envolve o aviso S005 para alvo ausente. Essa falha ainda precisa ser isolada; não foi mascarada.

## 5. Medições

Medição de 30 quadros ativos com `--ecs 5 --bench-frames 30 --project .\Project`, no backend Software (CPU): média **60,97 ms** (16,4 FPS), p50 **60,57 ms**, p95 **63,92 ms**, máximo **66,66 ms**. A amostra continha 11 corpos no total (incluindo os objetos da cena padrão). Hotspots: Scene view render **44,55 ms**, CPU rasterization **44,22 ms** e Post-processing **13,09 ms**; rasterização está incluída na zona de renderização e não deve ser somada a ela. Esse resultado caracteriza apenas o caminho CPU/software desta máquina, não o backend GPU.

Nova varredura dos caminhos críticos: `SoftRenderer::rasterize` executa interpolação perspectiva, normalização e avaliação PBR por fragmento coberto; esse caminho corresponde à zona CPU rasterization dominante. O `resolve` percorre a imagem inteira e o FXAA/sharpen também processa pixels em CPU quando habilitado, compatível com o custo medido em Post-processing. Esses caminhos devem ser priorizados para otimização; mudanças de algoritmo devem manter paridade visual e ser comparadas com uma cena e configuração fixas. Também há ordenações por quadro de draws transparentes (`gpu_renderer.hpp`) e candidatos de luz (`lighting.hpp`), mas não foram isolados como gargalo por esta medição.

Para repetir no seu sistema, use `test ECS: N` (Console) com N = 10 000, 50 000 e 100 000, e leia as zonas **Physics**, **Update** e **Scene render** no Profiler.

Medição de upscaling (`/home/claude/scratch/bench_up.cpp`, `-O2`, 2 threads): ver `docs/RENDER_SCALING.md`.

## 6. Próximos passos, por custo/benefício

1. **A8 zero-copy de apresentação.** Maior ganho de sincronização restante. Exige suporte no RHI para apresentar a textura direto no ImGui.
2. **A9 `each` sem cópia.** Simples e local; reduz uma alocação por sistema por quadro.
3. **A13 script por arquivo.** Necessário antes de escalar objetos com script.
4. **A10/A11 hierarquia e tags.** Medir primeiro.

## 7. Instrumentação e limites de quadro

- O Profiler agora separa preparação da cena, rasterização CPU, submissão GPU e readback; as zonas internas podem se sobrepor ao tempo total da vista.
- `CPU work` é calculado como tempo total menos espera ociosa, espera do limite manual e Present/VSync, em vez de somar zonas aninhadas.
- Os cronômetros de zona do loop principal reutilizam armazenamento na pilha, evitando alocações de heap por quadro.
- O limite inicial de FPS passou a ser ilimitado. VSync e limites de 30/60/120 FPS continuam disponíveis quando selecionados em Settings > Graphics.
- Ainda não há uma comparação numérica antes/depois feita no mesmo hardware nesta revisão; use `--ecs N` ou `--voxel N` com `--project <pasta>` para coletar média, p50, p95 e tempos das zonas no próprio sistema. `--bench-frames N` ajusta a amostra medida (padrão 300; intervalo 1–10000), após o aquecimento. Exemplo de render ativo: `tool_editor --ecs 5 --bench-frames 30 --project .\Project`. As médias das zonas usam os mesmos quadros medidos; zonas aninhadas podem se sobrepor.

## 8. Direct3D 11

- O backend D3D11 agora pode ser habilitado opcionalmente no Windows com o preset `editor-gpu-dx11`; o padrão continua desligado para não aumentar o tempo de compilação GPU dos presets existentes.
- O suporte compartilha o RHI e os shaders HLSL usados por D3D12. Deve ser validado em uma máquina Windows com driver D3D11; não foi possível confirmar a execução em hardware gráfico neste ambiente.
