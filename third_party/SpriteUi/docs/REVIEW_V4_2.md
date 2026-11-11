# Revisão de conformidade — SpriteUi × Documento V4.2 (0.5.1)

Base normativa: seções 1–7 do documento V4.2 (as demais são histórico legado). Build verificado com GCC 13 (Debug + ASan/UBSan) e Clang 18 (Release): 0 avisos, 298 verificações passando.

## Conformidade por seção
| Seção V4.2 | Resultado |
|---|---|
| 1.1 `SpriteInstanceGPU` 80 B / align 16, `UIMaterialGPU` | Conforme (`static_assert` de tamanho e offsets). |
| 1.2 `UICanvasSpace`/`UIShadingMode`/`UICanvasRenderConfig` | Conforme (+ `IsValid`). |
| 2 Input: snapshot, roteamento, captura, 8 bordas | Conforme. Desvio intencional: handles `UIHandle` no lugar de `flecs::entity` (núcleo independente de ECS; adaptador Flecs pendente). |
| 3.1 Slider (valor bruto no domínio) / ClampScroll | Conforme; adicionados `Normalized()` e `ClampScroll(scroll, viewportSize)` livre, como no documento. |
| 3.2 Navegação semântica Activate/Cancel | Conforme. |
| 4 `GlyphInstance` + MSDF com `fwidth`/`pxRange` | Struct conforme; shader tinha `pxRange` fixo (4.0) — agora vem do cbuffer (`MsdfPxRange`). HarfBuzz/FreeType pendentes. |
| 5 ToonMotion V2 (POD, Override/Queue/Ignore, Shake sem drift, alpha premultiplicado) | Conforme; adicionado `ApplyOpacityPremultiplied`. |
| 6 `UIDirtyBits` | Nomes do documento (`DirtyMeasure`…) adicionados como constantes; o tipo segue `enum class` (mais seguro). |
| 7 Persistência JSON atômica | Conforme e endurecida (ver abaixo). |

## Bugs corrigidos
1. **Layout:** percentuais eram resolvidos contra o viewport raiz, não contra o pai → agora contra a área interna do pai.
2. **Layout:** `Stretch` (padrão) ignorava tamanho transversal `Fixed`/`Percent` → só estica em Auto/Flex. min/max agora respeitados na distribuição grow/shrink (algoritmo iterativo com *freeze*); cross-size também respeita min/max.
3. **Texto:** `GenerateTextRenderItems` gerava itens com handle inválido, que o `RenderExtractor` descartava (nenhum texto seria desenhado) → novo parâmetro `owner`.
4. **LayoutBridge:** `HitTarget::parent` apontava para pai sem HitTarget (bubbling quebrava) → usa o ancestral interagível mais próximo.
5. **Docking:** `Split` em nó já dividido destruía a subárvore; `Center` descartava o conteúdo → recusa/agrupa em abas (`UIDockNode::tabs`). `RemoveTab` mudava a aba ativa ao remover uma anterior.
6. **Bindings:** valores sentinela (`-1e9f`, `-999999`) nunca disparavam na 1ª leitura → flag `initialized`.
7. **VirtualList:** conversão float→`size_t` indefinida com scroll gigante → clamp antes do cast.
8. **Navegação:** `Activate` sem foco prévio focava *e* ativava o primeiro item → só adquire foco.
9. **UIConfig:** chaves desconhecidas só eram puladas se strings (JSON válido com números/objetos era mal interpretado); sem escape de `"` (injeção ao serializar); aceitava objeto não fechado e lixo final; `schemaVersion` 1.5 passava → parser estrito, escapes, limites (profundidade 32, 1024 janelas, nome 256 B em fronteira UTF-8), serialização com locale clássico.
10. `PremultiplyLinear` clampava antes de checar NaN; includes faltando/sobrando; função morta `MatchString`.

## Padronização
- Não havia pares `.cpp`/`.hpp` duplicados nem `.h`; a inconsistência real era *header-only vs .cpp*. `SpatialNavigation` (lógica pesada inline) foi movido para `src/Input/SpatialNavigation.cpp`. Regras em `docs/CODING_STANDARDS.md`.
- Versão unificada em 0.5.1 (README/CMake/docs divergiam: 0.3.0 vs 0.5.0). Enum duplicado `UISemanticAction` removido.
- `.clang-format`, `.editorconfig`, `.gitignore` (`.cache/`), alvo `spriteui_check_headers`. `build/` e `.cache/` removidos da entrega.

## Limitações conhecidas (não alteradas)
- `TextWrap::Word` quebra por glifo (`GlyphInstance` não carrega informação de espaço/ponto de quebra).
- `GetCaretPosition` usa a caixa do glifo, não o *pen origin* (caret pode divergir pelo *bearing*).
- Flexbox sem wrap/baseline; `alignItems` é lido do filho (semântica *align-self*).
- `GetOrderedSamples` com buffer menor que o histórico devolve as amostras mais antigas.
- Shader HLSL não compilado aqui (sem DXC); alteração é mínima (1 constante no cbuffer).
