# Auditoria de desempenho e núcleo da engine

Escopo desta etapa: núcleo (ECS, GPU, paralelismo), editor (cena e jogo), sistema de partículas e memória (ASan/UBSan/LSan).

## 1. Ambiente das medições

- Máquina de desenvolvimento com **2 núcleos**. Os tempos absolutos são **pessimistas** em relação à sua máquina (10 cores físicos / 20 lógicos). Use-os para comparar antes/depois, não como número final.
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
- Selftest do editor (build normal): 119 checks, 0 falhas (incluindo 3 checks novos de orçamento e lotes de partículas).

## 5. Medições

Os números são deste ambiente (2 núcleos). Para os seus números, use `test ECS: N` (Console) com N = 10 000, 50 000 e 100 000, e leia as zonas **Physics**, **Update** e **Scene render** no Profiler.

Medição de upscaling (`/home/claude/scratch/bench_up.cpp`, `-O2`, 2 threads): ver `docs/RENDER_SCALING.md`.

## 6. Próximos passos, por custo/benefício

1. **A8 zero-copy de apresentação.** Maior ganho de sincronização restante. Exige suporte no RHI para apresentar a textura direto no ImGui.
2. **A9 `each` sem cópia.** Simples e local; reduz uma alocação por sistema por quadro.
3. **A13 script por arquivo.** Necessário antes de escalar objetos com script.
4. **A10/A11 hierarquia e tags.** Medir primeiro.
