# ToolEngine — Plano de execução de melhorias de arquitetura

**Projeto:** espiritoheroico2/TOOLGAMEENGINE  
**Branch de trabalho:** `work/architecture-improvements-2026-11`  
**Branch protegida por procedimento:** `main` (não será alterada durante este trabalho)  
**Baseline de partida:** `1a3c1d23ab672153a3aa2c2fc049bf331643baa6`  
**Versão declarada no README:** 0.8 beta · **Padrão:** C++20

## Objetivo

Evoluir a arquitetura existente sem reescrever a engine e sem remover subsistemas funcionais. Cada alteração deve ter escopo pequeno, motivação rastreável, testes correspondentes e documentação. Otimizações de CPU/GPU só serão aceitas quando preservarem o comportamento e puderem ser medidas.

## Regras de execução

1. Trabalhar somente na branch de desenvolvimento.
2. Antes de alterar um módulo, ler a implementação, os consumidores e os testes atuais.
3. Preservar APIs públicas quando possível; mudanças incompatíveis exigem adaptador, migração e justificativa.
4. Não substituir uma implementação funcional por uma técnica mais complexa sem benchmark.
5. Toda correção deve incluir teste de regressão ou explicar por que não é testável automaticamente.
6. Não remover ImGui enquanto cada janela migrada para SpriteUI não tiver paridade funcional e visual.
7. Manter o caminho CPU-only funcional; recursos GPU devem ser opcionais e capability-gated.
8. Não marcar build, teste visual, sanitizer ou benchmark como aprovado sem execução real.
9. Cada etapa termina com relatório de alterações, testes, riscos residuais e arquivos entregues.
10. Fazer segunda revisão crítica antes de encerrar cada etapa.

## Etapas

### Etapa 0 — Cópia e baseline documental

**Estado:** branch criada; inventário documental iniciado.

- Criar branch isolada a partir da main.
- Registrar SHA inicial, versão declarada, presets e subsistemas.
- Mapear os testes existentes antes de introduzir novos.
- Fixar critérios de aceite e política de commits.
- Não modificar a branch main.

**Aceite:** branch independente confirmada e plano de execução versionado.

**Limite atual:** a inspeção foi feita via GitHub. Nenhum build ou benchmark é declarado nesta etapa.

### Etapa 1 — SpriteUI: cache da lista de renderização

**Achado:** `RenderListCache` usa `vector::erase/insert` para cada nó cujo número de itens muda. Alterações em vários nós podem provocar múltiplos deslocamentos do restante da lista.

**Mudança planejada:**
- Manter a lista pública contígua para não quebrar consumidores.
- Construir os segmentos sujos em um buffer temporário reutilizável.
- Se nenhum segmento mudar de tamanho, atualizar somente os intervalos alterados.
- Se um ou mais segmentos mudarem de tamanho, reconstruir a lista contígua uma única vez.
- Calcular offsets e tamanhos com verificação de overflow.
- Manter os segmentos sujos se o builder falhar, permitindo nova tentativa sem publicar um frame parcialmente atualizado.

**Testes:** atualização sem mudança; atualização de mesmo tamanho; múltiplos segmentos crescendo/encolhendo; preservação da ordem; falha do builder e retry; estabilidade de endereços quando não há mudança estrutural.

**Risco:** a API ainda exige uma lista contígua. Uma mudança estrutural continuará exigindo uma reconstrução O(N), porém no máximo uma por atualização, em vez de várias operações de deslocamento.

### Etapa 2 — SpriteUI: layout incremental

- Auditar `LayoutTree`, `LayoutEngine`, `FlexItem` e todos os mutadores.
- Definir invalidação explícita para measure, arrange, paint e input.
- Propagar invalidação aos ancestrais somente quando a regra de layout exigir.
- Preservar o comportamento atual em árvores pequenas.
- Evitar reconstruir árvores temporárias quando nada mudou.
- Avaliar travessia iterativa e limites de profundidade antes de alterar recursão.

**Aceite:** testes de layout existentes preservados; testes de invalidação por estilo, visibilidade, criação, remoção e viewport; benchmark com árvore estática e árvore altamente mutável.

### Etapa 3 — SpriteUI: texto, atlas e batches

- Auditar `TextAtlasSet`, cache de fonte e adapters.
- Definir budget CPU/GPU, política de eviction e lifetime/fences.
- Validar fallback Unicode e metadados de formato do atlas.
- Fechar o contrato de `FramePacket`, batches, textura, slice, sampler, clip e ordem visual.
- Preservar cobertura bitmap para texto pequeno; avaliar MSDF/MTSDF apenas para escalas onde seja vantajoso.
- Padronizar alpha e espaço de cor; adicionar regressões visuais.

**Aceite:** testes de orçamento/eviction; validação de ranges; conformidade entre backends; snapshots visuais comparáveis.

### Etapa 4 — Editor e migração gradual

- Mapear autoridade única de janela, z-order, foco, docking, captura e modal.
- Migrar uma janela por vez: Console, Hierarchy, Inspector, Assets, Settings, Profiler e Scene/Game.
- Corrigir hit testing, foco, teclado, mouse, redimensionamento e DPI.
- Preservar ImGui como fallback até paridade funcional/visual.
- Separar modelo, estado e apresentação nas janelas com maior acoplamento.

**Aceite:** checklist de interação por janela, snapshots e ausência de regressões de fluxo de trabalho.

### Etapa 5 — Light Prism

- Separar revisões de topologia, transform, fotometria, shadow caster, câmera e settings.
- Evitar rebuild de dados de luz quando apenas uma propriedade independente mudou.
- Tratar ponteiros nulos com contagem zero explicitamente.
- Testar a conversão de matrizes ToolEngine ↔ Light Prism com casos determinísticos e property-based.
- Adicionar validação de limites/overflow para clusters.
- Culling e cache de sombras por cascata/face, com bounds conservadores.

**Aceite:** testes matemáticos, casos de overflow/NaN/Inf, contadores de cache hit/miss e comparação de imagem.

### Etapa 6 — Pipeline de renderização e hardware

- Instrumentar CPU submit e GPU elapsed separadamente.
- Consultar timestamps de GPU de forma assíncrona.
- Substituir readback síncrono por ring buffer/fences onde ainda for necessário.
- Priorizar Scene/Game como textura GPU consumida diretamente pela UI; manter CPU fallback.
- Separar visibilidade da câmera, shadow caster e visibilidade por cascata/face.
- Validar chaves de batch completas e preservar ordenação de transparência.
- Aplicar state cache, buffers persistentes e indirect draw somente quando capability e medição justificarem.

**Aceite:** CSV p50/p95/p99 por passe, bytes de upload/readback, waits de fence, imagens de referência e testes CPU-only.

### Etapa 7 — Segurança e estabilidade

- Endurecer `.tpak`: ponteiro nulo, checked arithmetic, budgets e lifetime do buffer.
- Revisar paths e operações finais para reduzir TOCTOU (handles nativos por plataforma).
- Salvar cenas/arquivos com escrita temporária e substituição atômica.
- Aplicar budgets a importadores/decoders e falha transacional.
- Fuzzing para JSON, TPAK, glTF/GLB, FBX, imagem, áudio e paths.
- Revisar sandbox de scripts/mods/shaders e protocolo de rede separadamente.
- Documentar ownership, lifetime, thread, erro, limites, shutdown e device-lost por subsistema.

**Aceite:** testes negativos, fuzz targets executados, sanitizers e matriz de plataforma documentada.

### Etapa 8 — Revisão integrada e pacote

- Reexecutar testes relevantes e a matriz de build disponível.
- Comparar métricas antes/depois em cena fixa e hardware identificado.
- Fazer revisão de regressões, APIs, compatibilidade, memória e concorrência.
- Atualizar README, roadmap e changelog.
- Entregar commits, relatórios e snapshot da branch.

## Matriz mínima de validação

| Área | Verificação |
|---|---|
| C++ | GCC/Clang/MSVC conforme ambiente disponível |
| Build | Debug e Release; Release sem depender de assert |
| Testes | CTest e selftests relevantes |
| Memória | ASan/UBSan; TSan para caminhos concorrentes |
| CPU-only | Editor/núcleo sem GPU |
| GPU | APIs realmente disponíveis no ambiente, com GPU/driver registrados |
| UI | snapshots e testes de mouse/teclado/foco/docking |
| Performance | CPU e GPU separadas; p50/p95/p99 |
| Segurança | entradas inválidas, limites, fuzzing e falha transacional |

## Registro de execução

| Etapa | Estado | Evidência |
|---|---|---|
| 0 — Cópia e baseline documental | Em andamento | Branch criada a partir do SHA registrado |
| 1 — SpriteUI RenderListCache | Planejada | Testes de regressão serão adicionados |
| 2 — SpriteUI LayoutTree | Planejada | Depende da auditoria dos mutadores |
| 3 — SpriteUI texto e batches | Planejada | Depende do contrato dos adapters |
| 4 — Editor | Planejada | Migração incremental |
| 5 — Light Prism | Planejada | Revisões e testes matemáticos |
| 6 — Renderização | Planejada | Instrumentação e CPU fallback |
| 7 — Segurança | Planejada | Hardening e fuzzing |
| 8 — Revisão integrada | Planejada | Matriz final |

## Referências de auditoria

- `docs/planos/00_ROADMAP.md`
- `docs/planos/00_CHECKLIST.md`
- `docs/planos/03_DESEMPENHO_EDITOR.md`
- `docs/planos/06_SPRITEUI.md`
- `third_party/SpriteUi/include/SpriteUi/Rendering/RenderListCache.hpp`
- `third_party/SpriteUi/src/Layout/LayoutTree.cpp`
- `apps/editor/text_atlas_set.hpp`
- `third_party/LightPrism/include/LightPrism/LightingSystem.hpp`
- `engine/pack/include/tool/pack/pack.hpp`

**Nota de engenharia:** esta é uma proposta de execução versionada. Uma etapa só passa a "concluída" quando os arquivos, testes e evidências correspondentes forem entregues.