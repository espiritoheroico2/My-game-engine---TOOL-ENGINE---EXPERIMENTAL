# Etapa 1 — SpriteUI RenderListCache

**Branch:** `work/architecture-improvements-2026-11`  
**Módulo:** `third_party/SpriteUi`  
**Escopo:** cache de lista de renderização por segmento/nó  
**Status:** implementação versionada; validação de compilação pendente

## 1. Problema identificado

A implementação anterior atualizava cada nó sujo diretamente em `items_`. Quando o novo segmento tinha tamanho diferente, executava `vector::erase` seguido de `vector::insert`. Como o vetor é contíguo, essas operações podem deslocar os itens de todos os nós posteriores. Se vários nós mudassem de tamanho no mesmo frame, o custo de movimentação podia se repetir para cada nó.

## 2. Estratégia aplicada

A API pública permanece a mesma:

- `Reset(nodeCount)`
- `MarkDirty(node)`
- `Update(builder)`
- `LastRebuildCount()`

A atualização agora tem duas fases.

### Fase A — staging

1. Limpa buffers temporários reutilizáveis.
2. Executa cada builder sujo na ordem dos nós.
3. Armazena o resultado de cada nó em um buffer temporário agregado.
4. Registra `node`, `offset` e `count` de cada segmento temporário.
5. Calcula o tamanho final com verificação contra `vector::max_size()`.

Nenhum segmento publicado nem `items_` é alterado durante essa fase. Se um builder lançar exceção, a exceção é propagada, a lista anterior permanece disponível e os nós continuam sujos para uma nova tentativa.

### Fase B — commit

**Sem mudança estrutural:** quando todos os segmentos sujos mantêm sua contagem, os dados são copiados somente para seus intervalos existentes. A capacidade e os endereços dos itens não alterados permanecem estáveis.

**Com mudança estrutural:** a lista final é montada em `rebuiltItems_` com uma única reserva de capacidade e uma passagem pelos nós. Segmentos limpos são copiados da lista anterior; segmentos sujos vêm do staging. Só depois de a nova lista estar pronta ocorre `swap`, seguido da atualização dos metadados dos segmentos.

Isso não elimina o custo O(N) quando a estrutura realmente muda: a API ainda entrega um `std::vector` contíguo. A melhoria é evitar uma sequência de deslocamentos do vetor para cada segmento alterado; a reconstrução ocorre uma vez por chamada de `Update`.

## 3. Buffers reutilizados

Foram adicionados buffers membros:

- `pendingItems_`: resultados agregados dos builders sujos;
- `pendingSegments_`: metadados dos segmentos preparados;
- `rebuiltItems_`: destino temporário da reconstrução estrutural.

O buffer `scratch_` continua sendo reutilizado para a saída individual de cada builder. Após o aquecimento, esses vetores podem preservar suas capacidades entre frames. O comportamento exato de alocação depende do volume máximo observado.

## 4. Testes de regressão adicionados

No arquivo `tests/test_render_list.cpp`:

1. **Múltiplas mudanças estruturais:** três segmentos alteram seus tamanhos na mesma atualização (crescimento, esvaziamento e crescimento), verificando contagem e ordem final.
2. **Falha de builder:** o segundo builder lança uma exceção depois que o primeiro já foi preparado. O teste verifica que a lista anterior não foi parcialmente publicada e que ambos os segmentos continuam pendentes para retry.
3. Os testes anteriores continuam cobrindo atualização sem mudanças, atualização somente de conteúdo, estabilidade do endereço em atualização de mesmo tamanho e crescimento de segmento.

O teste já está registrado no alvo CMake existente `spriteui_render_list_tests` / `SpriteUI.RenderList`; não foi necessário alterar CMake.

## 5. Revisão crítica da implementação

- **Compatibilidade:** assinatura e tipo de retorno não mudaram.
- **Ordem visual:** os segmentos continuam concatenados na ordem dos nós.
- **Falha do builder:** o staging impede publicação parcial por exceção do builder.
- **Overflow:** o total preparado e o total final são limitados por `max_size()`.
- **Custo de mudança estrutural:** ainda O(N), pois a saída pública é contígua. Para eliminar também essa cópia seria necessário mudar o contrato do consumidor para spans/blocos, uma alteração maior que fica fora desta etapa.
- **Concorrência:** a classe continua não thread-safe; builders e chamadas concorrentes não devem acessar a mesma instância simultaneamente.
- **Lifetime:** referências para elementos do vetor podem ser invalidadas em uma atualização estrutural, como já ocorria com o vetor anterior. Atualizações de mesmo tamanho mantêm a capacidade e os endereços dos elementos.

## 6. Validação executada e pendente

**Executado nesta sessão:**
- Inspeção do header original, testes existentes e registro CMake.
- Revisão estática do novo fluxo de staging/commit.
- Inspeção posterior dos arquivos gravados na branch.

**Não executado nesta sessão:**
- Compilação CMake/C++.
- CTest ou execução do binário `spriteui_render_list_tests`.
- ASan/UBSan/TSan.
- Benchmark de alocações e tempo com milhares de nós.
- Teste visual em SDL/Diligent.

O acesso utilizado nesta etapa permite inspecionar e versionar arquivos no GitHub, mas não oferece um ambiente de compilação local do repositório. Portanto, esta etapa **não deve ser considerada validada em runtime**.

### Comandos de validação recomendados no checkout

```bash
cmake --preset dev
cmake --build --preset dev --target spriteui_render_list_tests
ctest --test-dir build -R SpriteUI.RenderList --output-on-failure
```

Os nomes e caminhos devem ser confirmados com o preset escolhido no ambiente local.

## 7. Arquivos modificados

- `third_party/SpriteUi/include/SpriteUi/Rendering/RenderListCache.hpp`
- `third_party/SpriteUi/tests/test_render_list.cpp`

## 8. Próxima etapa

Etapa 2 — auditar a árvore de layout e seus mutadores para introduzir invalidação incremental de measure/arrange sem alterar resultados existentes. Antes de alterar o algoritmo, mapear os consumidores de `LayoutTree::Layout`, os pontos de mutação e as regras de `LayoutEngine`.

## 9. Segunda revisão crítica

A revisão posterior aos commits verificou novamente o diff entre a branch e a baseline:

- A branch está adiante da `main` e não está atrás dela.
- O diff desta etapa está restrito ao cache, ao teste correspondente e à documentação de engenharia.
- O teste de atualização estrutural cobre crescimento, esvaziamento de segmento e preservação da ordem dos segmentos limpos.
- O teste de exceção cobre staging parcial e retry.
- O caminho sem mudança de contagem mantém a lista contígua no mesmo armazenamento.
- O caminho estrutural constrói o vetor substituto antes de fazer `swap`.

**Risco residual explicitamente mantido:** a reconstrução estrutural ainda copia os segmentos limpos uma vez. Isso é consequência do contrato de retorno como vetor contíguo e não foi escondido como se tivesse sido eliminado. A classe também não oferece sincronização para chamadas concorrentes.

A revisão foi estática. Sem compilação, não é possível confirmar compatibilidade efetiva com todos os compiladores nem considerar os testes aprovados.

## 10. Identificação da entrega

- **Branch:** `work/architecture-improvements-2026-11`
- **Baseline:** `1a3c1d23ab672153a3aa2c2fc049bf331643baa6`
- **HEAD após a documentação da etapa:** `ea8a8e89adf749766f049158ae0e51df81efca39`
- **Estado de main:** permanece no SHA da baseline durante esta etapa.
