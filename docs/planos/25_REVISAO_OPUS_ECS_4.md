# 25 - Revisao (Opus) 4 do ECS - itens 1 e 2 da rev. 24 (so leitura, nada compilado/executado)

a) worldMat: correcao COMPLETA para o que foi pedido. Com o cache ativo (editor_hier.hpp:70-78) nao ha escrita nem resize; o erro cai em
   worldMatWalk (so leitura). Unicas escritas no cache: beginWorldMatFrame (passos 1-2 em parallelFor escrevem cada um o seu slot `i`;
   o passo 2 le slots de raizes ja fechados pela barreira do parallelFor 1; passo 3 serial). Nenhum parallelFor do editor chama worldMat
   (fx.hpp:105 e play_runtime.hpp:94/124/152 nao chamam; draws em editor_state.hpp:1315/1394 sao seriais). Ressalvas: (1) se algo
   escrever Transform/Parent DENTRO de renderScene apos beginWorldMatFrame, o cache devolve matriz velha sem aviso - falta assert de debug
   (no fim de renderScene recalcular denseSum e comparar com worldTxSig_/worldParSig_); (2) comentario obsoleto em fx.hpp:18
   ("worldMat nao e thread-safe"); (3) falta contador de erros de cache por quadro (o fallback silencioso pode esconder custo O(profundidade)).

b) Permutacao (gpu_renderer.hpp:437-459): correta. sortOrder_[j] = origem do destino j (gather); cada ciclo e percorrido uma vez,
   marcado com sortOrder_[j]=j, um Draw temporario. Estabilidade: vem do stable_sort de INDICES com a mesma chave -> mesma ordem que o
   stable_sort legado. Chaves iguais: o teste `sorted` usa `<` estrito, iguais contam como ordenados (igual ao estavel). n=0/1: nada move.
   mesh nulo derruba os dois caminhos igual (pre-existente). NAO ha teste unitario dele (test_instance_batch.cpp cobre outro modulo).
   Teste minimo: extrair a permutacao para funcao livre (template) e comparar com std::stable_sort em payload {chave, id}: n = 0,1,2,
   ja ordenado, inverso, todas iguais, um ciclo unico de n, e 1000 casos pseudoaleatorios com muitas chaves repetidas (ids na mesma ordem).

c) Hipotese "driver nao determinista" NAO e aceitavel como esta: o pixel e SEMPRE (786,368) e alterna ENTRE processos, nunca dentro de um.
   Isso aponta para estado por processo (ASLR: ordem de unordered_map/sort por ponteiro, ordem de threads, memoria nao inicializada) num
   elemento so da vista Scene (grade, gizmo, contorno, icones), nao para ruido do driver. Condicao minima antes do id u32: captura
   (RenderDoc pixel history ou dump por passada) de (786,368) nos dois estados, nomeando a passada/draw que diverge; e rodar com 1 thread
   (e/ou WARP) para separar CPU de GPU. Movers 10k: Game 0-6 bytes tambem varia - confirmar que o dt e fixo na medicao, senao a medida nao vale.

d) Item 2 bem fechado (causa por chave isolada, correcao e budget por componente registrados), faltando so o teste de (b). Item 1:
   a parte worldMat esta fechada; a corrida de imagem (pedido (2) da rev. 24, "explicada") NAO esta: foi refutada a causa, nao achada.
   Antes do item 3 faltam: teste da permutacao; assert de debug de (a)(1); diagnostico do pixel conforme (c).

e) Veredito: SIM, COM RESSALVAS, pode seguir para o item 3 (versao por API) - ele nao depende da imagem e o assert de debug com hash por
   valor (pedido (3) da rev. 24) ja cobre (a)(1). Ressalvas: 1) plano escrito do item 3 primeiro, mantendo o hash por valor como assert;
   2) teste unitario da permutacao junto; 3) pixel (786,368) segue ABERTO e BLOQUEIA o item 4 (id u32) ate cumprir (c); 4) nenhum numero
   de imagem "igual" entra no registro sem dizer se o pixel estava no estado A ou B; 5) corrigir comentario fx.hpp:18 e contar erros de cache.
