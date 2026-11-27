# 21 - Revisao (Opus) do degrau 1 de escala ECS ("test ecs" / "test ecsf")

Data: 2026-11-27. Revisao so de leitura: docs/planos/19_ESCALA_ECS.md, docs/estudos/RENDER_FILA_MEMORIA.md e o diff de
apps/editor/editor_hier.hpp, apps/editor/editor_state.hpp, apps/editor/main.cpp. Nada foi compilado, executado ou alterado no codigo.

## 1. Validade das medicoes

- O metodo declarado (120 quadros medidos, 60 de aquecimento, dt fixo 1/60, 1280x720, Vulkan, copia do projeto) e adequado, mas a tabela
  tem **uma unica execucao por celula**, sem variancia. Sinal de ruido: a zona Fisica caiu de 31,6 para 28,6 ms (-10%) no ecsf sem nenhuma
  mudanca em engine/physics. Ou seja, o ruido entre execucoes pode ser da ordem de 10%, e parte dos -17%/-20% pode ser ruido.
- A queda de "sombra: assinatura" (34,9 -> 25,6) nao separa as duas causas. Se a cena de teste nao tem luz pontual com sombra,
  `pointShadowSignature` nem roda (`pointShadows ? ... : 0`, editor_state.hpp ~1058) e o compartilhamento do hash nao rende nada: todo
  ganho viria do cache plano. Precisa medir as duas mudancas isoladas, ou ao menos dizer quantas luzes com sombra a cena tem.
- 10k nao foi medido depois da mudanca; so 50k. Falta tambem 100k (o estudo base tem 146 ms a 100k, com 78 ms de oclusao).
- Numeros a conferir: o baseline de 10k ecsf (43,8 ms) nao bate com o 18 ms a 10k do RENDER_MILHOES.md (cena/projeto diferentes?).
  E as zonas se sobrepoem ("Update" 35 ms contem a fisica 31,6 ms; "Scene prep" contem a assinatura): nao se pode somar colunas.
- Comparacao justa exige: (a) mesma copia do projeto restaurada antes de cada execucao (o spawner e a fisica dependem do estado inicial
  e da semente); (b) mesmo binario de base: a arvore tem mudancas nao relacionadas (rhi.hpp, diligent_device.cpp, settings_*, linha
  `gpuRayTracing` em main.cpp). Registrar o hash do commit + diff de cada binario medido; (c) 3 execucoes, reportar mediana e min-max;
  (d) mesma condicao termica e de cache de disco (uma execucao descartada antes); (e) RAM (pico de working set), que ainda nao foi medida.

## 2. Qualidade das mudancas

- Cache plano de worldMat: preserva o resultado. A conta e a mesma; a validade e (stamp do quadro de preparo == atual) e (geracao confere),
  o que equivale ao antigo `unordered_map` limpo por vista. Slots novos nascem com stamp 0 e o stamp atual e sempre >= 1, entao nunca
  dao falso acerto. A volta do contador (2^32) limpa o vetor. A referencia `s` sai de escopo antes do laco e `out` e tomado depois: ok.
- Invalidacao entre vistas: cada `renderScene` faz ++stamp, entao Scene e Game nunca compartilham matriz (correto, porque
  `hiddenInView` e `lodMeshOf(e, mr, cam)` dependem da vista). Custo: a matriz e recalculada uma vez por vista por quadro (oportunidade, nao bug).
- Dados mudando no meio do quadro: mesmo risco de antes (o mapa antigo tambem so era limpo por vista). Se algo dentro do preparo mexer em
  Transform (gizmo, script, sincronizacao de fisica), o cache devolve a matriz velha. Nao e regressao, mas nao esta coberto por teste.
- casterHash: o sol e as pontuais usam a mesma `cam` e o mesmo filtro, entao o hash dos casters e o mesmo; agora entra como um u64 no
  hash externo (hash de hash) em vez de entrar byte a byte no fluxo. A deteccao de mudanca continua igual salvo colisao de 64 bits (FNV
  com mistura por palavra ja era fraco antes; nao piorou). Risco real: se algum dia uma das assinaturas for chamada com outra camera
  (ex.: shadow de uma vista secundaria dentro do mesmo renderScene), o valor em cache estara errado. Recomendo guardar a camera/vi
  junto com `casterHashFrameValid_` ou um assert.
- Reentrancia: se `renderScene` for chamado dentro de outro (sonda, miniatura), o interno avanca o stamp e o guard desliga o cache no fim,
  invalidando o externo. Ja existia antes; vale um assert de nao reentrancia.
- Memoria: o vetor tem 72 B x maior e.index e nunca encolhe (72 MB a 1 milhao). Aceitavel, mas registrar na tabela de RAM.
- Como testar regressao: (1) teste unitario: cena com hierarquia de 3 niveis, mover o pai entre duas vistas e conferir worldMat == calculo
  sem cache; (2) entidade destruida e recriada no mesmo indice (geracao nova) no mesmo quadro; (3) sombra: mover um unico caster com sol
  + 1 pontual e verificar que as DUAS assinaturas mudam e que, parado, as duas ficam estaveis; (4) comparar imagem (hash do framebuffer)
  de 60 quadros com dt fixo antes/depois, em Scene e Game.

## 3. O que viola o criterio e o que e arquitetura

- Arquitetura de verdade: trocar o mapa por vetor indexado + carimbo; calcular o hash uma vez. Nao esconde nada.
- Ainda e contorno, nao solucao: a assinatura de sombra continua O(N) por quadro (16 floats por objeto) so para decidir se reaproveita
  o mapa; em movers ela muda todo quadro, entao e custo puro. A solucao e rastreio de mudanca (versao/dirty por Transform), nao hash.
- Viola o criterio "medir so o que e facil": so 50k, uma execucao, sem RAM, sem 100k, fisica declarada fora de escopo sem numero de
  quanto ela pesa a 1 milhao. A fisica pode ficar fora do codigo, mas deve ficar dentro da tabela.
- Atencao: `--no-occlusion` existe no bench; nenhum numero de degrau deve usar essa flag nem raio de oclusor maior (`kOccluderMinPx`).

## 4. Caminho recomendado para 1 milhao (ordem por ganho e risco)

1. Infra de medicao (risco zero): 3 execucoes, mediana, RAM pico, commit registrado, 10k/50k/100k, zonas sem sobreposicao,
   e uma comparacao de imagem para provar "mesma qualidade". Medir: desvio entre execucoes < 3%.
2. Transform sistema unico por quadro: versao por entidade + matriz mundial calculada uma vez por quadro (SoA, compartilhada entre vistas
   e passes), assinatura de casters = agregado das versoes (O(alterados)). Medir: zona de assinatura e worldMat a 50k/100k parados e movers.
3. id u32 no MeshRenderer (model/texture/material/normalMap) com migracao de cena (arquivo continua com string; carga mapeia para id;
   roundtrip testado; UI/undo/prefab/scripts continuam por nome). Medir: RAM por entidade (~650 -> ~124 B esperado), GPU submit e Scene prep.
4. Ordenar indices em vez de `Draw` em `GpuRenderer::end()` + Draw enxuto (id de material em vez de copia de `Material`). Medir: GPU submit.
5. Buffer de instancias persistente (B2): slot estavel por entidade, faixas sujas via `updateBufferRange`, indice u32 por lote gerado pelo
   culling. Medir: bytes enviados por quadro (deve cair de 160 B x N para ~4 B x N ou so os sujos), e o estouro de heap a 1 milhao.
6. Oclusao HiZ na GPU (E7) em duas fases (conservadora, sem popping) substituindo a rasterizacao por software; classificacao de casters
   de luz pontual por estrutura espacial em vez de O(N x faces). Medir: zona de oclusao, popping (comparacao de imagem), quadro a 100k/1M.
- id u32 ANTES do buffer persistente: sim. (a) O buffer persistente precisa detectar "material/textura deste objeto mudou" para sujar o
  slot e trocar de lote; com strings isso e comparacao de 160 B por objeto, com id e uma comparacao de inteiro. (b) A chave de lote
  (malha, textura) vira inteiro. (c) Reduz a memoria fria que todo laco O(N) restante le (casters, oclusao, montagem). (d) Na medicao
  atual, a maior parte dos 47 ms de submit a 50k e CPU (lookups de string, copia de Draw), nao o upload de 8 MB. Risco: migracao de cena;
  mitigar mantendo string no disco e testando roundtrip.
- NAO fazer: amostrar casters ou hashear a cada k quadros; baixar resolucao de sombra/bench; aumentar `kOccluderMinPx`; usar
  `--no-occlusion` nos numeros; spawnar objetos fora da camera; cache de worldMat entre quadros sem rastreio de mudanca; subir lote acima
  de 4096 sem medir; tirar sombra de objetos distantes sem que isso seja uma configuracao visivel ao usuario.

## 5. Veredito

Sim, com ressalvas. A mudanca e correta e e arquitetura (nao esconde custo), mas o ganho nao esta provado com rigor.
Ressalvas: (1) uma execucao por celula e ruido visivel (fisica -10% sem mudanca); (2) nao separa cache plano x hash compartilhado e nao diz
se ha luz pontual com sombra; (3) falta 10k depois, 100k e RAM; (4) arvore com mudancas nao relacionadas no mesmo binario; (5) sem teste
especifico de invalidacao (hierarquia, geracao, duas assinaturas); (6) a assinatura O(N) continua: o problema real (rastreio de mudanca) segue aberto.
