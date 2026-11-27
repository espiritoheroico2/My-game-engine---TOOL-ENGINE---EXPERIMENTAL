# 24 - Revisao (Opus) 3 do ECS - PASSO 2 (assinatura por valor + matriz mundial persistente) - so leitura, nada compilado/executado

a) Ganho: o TOTAL se sustenta (A/B intercalado, mesmo binario, 3 exec., desvio < 3% nas 18 celulas, -70 ms >> ruido). O que NAO se sustenta
   e a atribuicao: A liga 3 chaves juntas (LEGACY_SIG + LEGACY_SORT + NO_MEMO), entao "-70 ms vem da assinatura/matriz" nao esta provado
   (o proprio agente cita GPU submit 109 -> 84 e oclusao 29,6 -> 11,9 como zonas vizinhas). A zona sombra-assin. 50 -> 2,8 ms e atribuivel
   (e medida dentro da propria funcao). Falta: matriz 2^3 de chaves (ou pelo menos uma chave por vez vs padrao) a 100k movers/parada, ABAB,
   3 exec.; e a cena com textura/material para NO_MEMO, senao o cache de assets continua "medido no facil" (pendencia da rev. 23 b1).

b) Nao bloquear por 2,8 vs 2 ms em si; bloquear pelo que o numero revela: o desvio de projeto troca O(1) por O(N). Assinatura por valor
   varre Transform+Parent+MeshRenderer TODO quadro, inclusive parado (0,79 ms a 100k = ~8 ms a 1M so para descobrir "nada mudou"; movers
   ~28 ms a 1M). A rev. 23 pediu contador global de versao justamente por escala; o valor e otimo como VERIFICADOR (debug/selftest), nao
   como mecanismo. RAM +290 B/ent (18x o orcamento) sem causa e bloqueante: o passo 3 existe para reduzir RAM, e sem saber de onde vem o
   +29 MB nao da para medir o ganho do id u32. Minimo para liberar o passo 3: (1) causa do +290 B/ent identificada (por chave isolada, ver
   d) e corrigida ou orcada por componente (sizeof x N, incl. renderWorldMatCache_ 72 B + crescimento 1,5x do resize sob demanda);
   (2) corrida de imagem (c) explicada; (3) plano escrito de versao por API com o hash por valor mantido como assert de debug.
   O alvo <= 2 ms pode ir junto com a versao por API (ela o resolve por construcao), mas nao pode ser dado como "quase ok".

c) NAO e aceitavel como ruido: A-A = 0 e B-B = 3 bytes prova nao-determinismo introduzido por B. Suspeito concreto no codigo: worldMat()
   sob demanda (editor_hier.hpp ~54-76) faz resize() de renderWorldMatCache_ e grava m/gen/stamp sem atomico/barreira; se for chamado de
   dentro de parallelFor (oclusao, casters, submit), ha (1) realocacao do vetor enquanto outra thread le = UB, e (2) leitor ve stamp novo
   antes de m completo (escrita rasgada; o compilador pode reordenar). Filhos de netos/niveis >= 3 caem nesse caminho, e o quadro mais
   rapido muda a intercalacao - casa com "depende de tempo". Provar: (i) rodar B com TSan/ou build com mutex no caminho sob demanda e
   ver se B-B vira 0; (ii) forcar parallelFor com 1 thread (B-B deve virar 0 se for corrida); (iii) assert "renderWorldMatCache_ nao muda
   de tamanho com cache ativo" + contagem de acertos sob demanda por thread; (iv) hash de framebuffer em 60 quadros (nao so o ultimo), lido
   apos fence/WaitIdle, para descartar leitura antes do fim do quadro. Correcao arquitetural: pre-calcular TODOS os niveis (por
   profundidade, em paralelo) em beginWorldMatFrame e deixar worldMat() so-leitura durante o render.

d) Remover antes do passo 3, mas DEPOIS de usa-las para o isolamento que faltou (a + b1): elas permitem exatamente variar uma coisa por vez,
   o que nao foi feito. Chaves de ambiente lidas em static const no caminho quente, sem nome de bench e sem data de remocao, viram divida.
   Sem elas: `git worktree add` com o patch base (custo de rebuild do Diligent e aceitavel uma vez; ou apontar o worktree para o mesmo
   build de deps via cache/prefix), ou um alvo de bench separado (#ifdef TOOL_BENCH_AB so nesse alvo), nunca no binario do editor.

e) Ordem corrigida: 1) corrida de imagem (correcao, nao desempenho - nada mede direito com UB); 2) causa do +290 B/ent; 3) versao por API
   (contador global de casters + versao por entidade, hash por valor como assert de debug) -> resolve sombra <= 2 ms em O(1);
   4) id u32; 5) buffer persistente (usa as versoes para sujar slots); 6) HiZ. "Reduzir a assinatura a <= 2 ms" otimizando o hash por valor
   e o caminho errado: so empurra o O(N) para frente.

f) Veredito: SIM, COM RESSALVAS (nao libera o passo 3 ainda). Ressalvas: (1) desvio de projeto O(1) -> O(N) contrario ao objetivo de 1M;
   (2) sombra-assin. <= 2 ms nao atingido (movers 2,8; ecsf 2,08); (3) RAM +290 B/ent vs +16, causa nao investigada; (4) nao-determinismo
   de imagem introduzido por B, nao investigado (provavel corrida no worldMat sob demanda); (5) atribuicao do -70 ms nao isolada por chave;
   (6) cache de assets ainda sem cena que o exercite; (7) chaves TOOL_AB_* no codigo de producao; (8) pendencias da rev. 23: teste sol +
   pontual, check "escrita direta detectada" via API/assert, hash de imagem em 60 quadros; (9) limites declarados (LOD da esfera fora da
   assinatura da pontual; modelo novo so na sombra um quadro depois) sao bugs visiveis, nao "limites" - precisam de teste e correcao.
