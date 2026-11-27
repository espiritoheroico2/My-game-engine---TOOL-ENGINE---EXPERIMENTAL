# 23 - Revisao (Opus) 2 do ECS ("test ecs" / "test ecsf") - so leitura, nada compilado/executado

a) Selftest: correcao LEGITIMA. renderScene (editor_state.hpp ~1035) faz `++stamp; casterHashFrameValid_ = false; active = true`; o helper
   antigo so avancava o carimbo, entao `casterHash(camA)` com a mesma camera devolvia o valor do "quadro" anterior. O novo cacheOn imita o
   inicio de renderScene; nenhum check foi relaxado. Para confirmar que nao esconde bug: (1) apos mover, comparar `h1` com o hash SEM cache
   (cacheOff + casterHash) - igualdade prova que o cache nao muda o resultado; (2) check negativo: mover o caster DENTRO do mesmo quadro e
   documentar que o hash fica velho (comportamento esperado, mas hoje sem teste nem assert); (3) `sameCam` compara so `eye`: ok porque o LOD
   usa o olho, mas registre isso no comentario ou compare a camera inteira. Pendente da rev. 21: teste sol + pontual (as duas assinaturas).

b) Sustenta: com 3 execucoes/mediana, o custo cresce ~linear de 10k a 100k e tres zonas (sombra-assin., oclusao, submit) somam ~95% do
   quadro movers a 100k (211 de 221 ms). NAO sustenta: (1) o -5%/-3% das 3 mudancas: menor que o desvio de 50k (4,7-6,0%), binario mistura
   physics/rhi/settings, e o cache de assets nem e exercitado (esferas sem textura) - e "medir o facil"; (2) a extrapolacao de ~2,2 s a 1M:
   o submit e superlinear (39,3 -> 90,6 ms = 2,3x para 2x N; oclusao 2,2x), o heap de instancias estoura antes de 1M e o broadphase nao foi
   medido acima de 100k; diga "ao menos 2,2 s", nao "~2,2 s"; (3) comparar ecs x ecsf por zona: as cenas diferem (oclusao 65,9 x 19,2 ms
   porque os corpos se amontoam), entao fisica deve ser lida so pela zona Fisica. Isolar sem stash: `git worktree add` em pasta separada
   (nao toca a arvore suja) com o commit/patch base, compilar dois binarios e rodar ABAB intercalado na mesma sessao termica; ou flag de
   bench temporaria por caminho (ex.: --bench-no-asset-cache, --bench-draw-sort-legacy) removida apos a medicao. Cache de assets exige
   variante de cena com textura/material; forgetTexture se mede em RAM (recarregar textura N vezes, RAM deve ficar estavel).

c) RAM: os proprios numeros dao MAIS que o relatorio: movers (1658-1480)/90k = ~2,0 KB/ent; ecsf (1867-1503)/90k = ~4,0 KB/ent; 1M seria
   ~2 GB / ~4 GB alem da base. Working set e ruim (paginas, vetores que dobram de capacidade): medir PeakPagefileUsage/PrivateUsage pico e
   reportar por degrau. Meta honesta para 1M: render <= ~600 B/ent (<= 0,6 GB alem da base) e fisica <= ~1,5 KB/ent extra (<= 2,1 GB total
   ecsf). Caminho: id u32 no MeshRenderer (~650 -> ~124 B, ~-0,5 GB a 1M); buffer persistente sem copia de Draw/Material por quadro (vetores
   transientes de 160 B+ por entidade somem); listas de oclusao/casters com indices u32 em vez de Entity+ponteiros; cache worldMat 72 B
   vira a matriz unica do Transform por versao (nao duplicar). Publicar orcamento por componente (sizeof x N) ao lado da RAM medida.

d) Ordem: a oclusao por CPU NAO e o maior gargalo a 100k: GPU submit 90,6 (e superlinear) > oclusao 65,9 > sombra-assin. 54,9 (movers);
   em ecsf a oclusao e 19,2. HiZ continua por ultimo. Transform por versao primeiro esta certo, MAS por outro motivo: em movers todo objeto
   muda todo quadro, entao "O(alterados)" = O(N); o ganho vem de trocar o hash por um contador global de versao de casters (O(1)) e de ser
   a base para sujar slots do buffer persistente. Depois id u32, buffer persistente, HiZ. Desvio: o agente fez "ordenar indices" (item 4)
   antes do item 2 da rev. 21 e nao fez a comparacao de imagem do item 1.

e) Proximo degrau unico: Transform por versao (escrita so por API que incrementa versao da entidade e um contador global de casters;
   worldMat calculada uma vez por quadro, compartilhada entre vistas; assinatura de casters = contador global + sets de criacao/destruicao).
   Aceite: selftest novo (parado = assinatura estavel; mover 1 caster muda as duas assinaturas; escrita direta fora da API detectada por
   assert em debug); hash de framebuffer igual em 60 quadros Scene/Game antes/depois; 3 exec./mediana/desvio a 10k/50k/100k ecs e ecsf
   + variante parada; sombra-assin. a 100k <= 2 ms (de 54,9); quadro movers 100k cai >= 45 ms; RAM privada pico <= +16 B/ent.

f) Veredito: SIM, COM RESSALVAS. (1) RAM subestimada (~2/~4 KB, nao 1,5/3,6) e medida por working set; (2) 3 mudancas sem medicao isolada
   e cache de assets nao exercitado; (3) extrapolacao 1M linear apesar do submit superlinear; (4) 50k com desvio > 3%; (5) sem comparacao
   de imagem nem teste sol + pontual; (6) ordem da rev. 21 nao seguida (item 4 antes do 2); (7) binario com mudancas nao-ECS.
