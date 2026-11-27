# Estudo: otimização de física para ganhar CPU

Escopo: `engine/physics/include/tool/physics/` (Newtoon) e a ligação com o editor (`apps/editor/play_runtime.hpp`, `editor_play.hpp`). Estudo de planejamento; nenhuma mudança de código aqui. Documentos de base: `../planos/12_OTIMIZACAO_FISICA_ECS.md` e `../planos/10_FISICA.md`.

## 1. Custo medido hoje

| Medição | Fonte e data | Resultado |
|---|---|---|
| Perfil de 3000 esferas, 60 quadros (callgrind, 2 núcleos) | `12_OTIMIZACAO_FISICA_ECS.md`, medido em 2026-11-24 | `SolveSingleContact` ~51%; `BVH::Query` ~26%; `PreSolve`+`Step`+`Prepare` ~10%; `MakeShape` e alocação ~2% |
| Tempo por quadro, 3000 esferas | mesma fonte | ~3,9 a 4,1 ms médio; pior quadro 7,6 a 24 ms; 17 corpos dormindo de 3000 |
| Tempo por quadro, 1000 esferas | mesma fonte | ~1,4 a 1,6 ms; 175 dormindo |
| 5000 esferas, renderizador CPU | `../referencia/CHANGELOG.md` | física (Update) em 29 a 30 ms; quadro total 485 ms (a física não era o gargalo naquela configuração) |
| Solver ~85% do passo com 5 mil esferas (5.002 corpos, Vulkan, Release) | medido em 26/11/2026 com `--ecs 5000 --bench-dt 0.016667` (fases de `PhysicsWorld::stats`) | Antes das correções do pool: solver 15,3 de 18,0 ms por passo. Depois da correção do pool de threads: solver 2,9–3,1 ms de 5,2–5,5 ms por passo. Ver `docs/estudos/RENDER_MILHOES.md` e o histórico do commit `95e598c`. |

Observação: o perfil de 3000 corpos (24/11, callgrind) dá o solver em ~51% e a broad-phase em ~26%. A medição de 26/11 com 5 mil corpos deu ~85% do passo no solver, antes de corrigir o pool de threads. A diferença vem do pool de threads (o solver faz ~140 chamadas paralelas por passo, cada uma custando centenas de µs). Depois da correção, o solver caiu para ~55% do passo. A etapa 0 ainda vale para ter a linha de base com contadores completos.

Dois experimentos já foram rejeitados e não devem ser repetidos sem fato novo: warm starting dos impulsos e broad-phase por travessia árvore contra árvore (detalhes em `12_OTIMIZACAO_FISICA_ECS.md`; este último mudou a ordem dos pares e, com isso, o resultado da simulação).

## 2. O que existe hoje no código

- **Passo fixo.** `PhysicsWorld::Simulate` acumula `dt` e executa `Step` a `fixedDt` (padrão 1/60 s, `stepHz` de 15 a 240 em `project.hpp`). Há limite de passos por quadro (`maxStepsPerFrame`); se estourar, o acumulador é zerado.
- **Sono.** Em `Step`, corpo dinâmico com velocidade linear e angular abaixo de `sleepThreshold` por mais de 0,5 s passa a `isSleeping` e para de ser integrado. Um corpo acordado que toca um corpo dormindo acorda este (`WakeUp`).
- **Passo ocioso.** Sem corpos dinâmicos ativos e sem cinemáticos em movimento, o passo pula a preparação de formas e a reconstrução da BVH (`10_FISICA.md`).
- **Raio de simulação** (`simRadius`, Settings > Physics; `updateSimulationRadiusParallel` em `play_runtime.hpp`). Em volta da câmera principal (`cams[0]`), corpos dinâmicos fora do raio são marcados como dormindo, têm velocidade zerada e saem da simulação (`setSimulating(false)`) e do desenho de sincronização. Volta a simular quando entram em 90% do raio (histerese). O teste é paralelo, em blocos de 512. Valor 0 desliga. Limite: só considera a distância à câmera, não a importância do corpo para o jogo, e corpos zerados perdem a velocidade.
- **Interpolação visual: peça existe, não está ligada.** `PlayRuntime` guarda `prevTf` e `capturePreviousTransforms()`, e `syncPhysicsToEcsParallel(ecsWorld, alpha)` interpola posição e faz slerp da rotação entre `prevTf` e `tf`. Mas `editor_play.hpp` chama `syncPhysicsToEcsParallel(w, 1.f)` e `capturePreviousTransforms` não tem chamadores (conferido por busca em `apps/`). Hoje a renderização usa a pose do último passo, sem interpolar.
- **Paralelismo.** Solver por cores de restrição (`ConstraintColorGraph`, até 16 cores), narrow-phase paralela e `tool::ParallelPool` persistente (`12_OTIMIZACAO_FISICA_ECS.md`).
- **Ilhas.** Não existe. Busca por `island` e `ilha` no código de física não retornou nada. Os corpos ficam em vetores globais (`tf`, `rb`, `col`) e o sono é por corpo.

## 3. Propostas

### 3.1 Interpolação e frequência menor para corpos lentos ou dormindo

Duas ideias distintas, que não devem ser confundidas:

1. **Interpolar a renderização** entre passos fixos. Não reduz CPU de física por si só; permite rodar a física em passo fixo menor que o quadro sem tremor visual. Ligar `capturePreviousTransforms()` antes do passo e passar `alpha = acc_/fixedDt` ao sync. O pedaço já escrito cobre isso; falta expor o `alpha` do acumulador (`acc_` é privado em `PhysicsWorld`) e chamar a captura.
2. **Simular corpos lentos com frequência menor** (por exemplo, a cada 2 ou 4 passos). Isso sim economiza CPU, mas muda o resultado: corpos lentos em contato com corpos rápidos exigem o mesmo passo no solver, senão há penetração e energia ganha. Uso seguro: só para corpos sem contato com corpos rápidos, ou como nível de atividade (ver 3.2).

Ganho esperado: só medindo. A interpolação em si é custo extra pequeno (uma passagem paralela já existente).

### 3.2 Níveis de atividade por distância

Estender o `simRadius` de dois estados (simulando / congelado) para três ou quatro: perto (passo cheio), médio (um passo a cada N), longe (congelado, como hoje). Parâmetros: raios por nível, N por nível, histerese. Cuidados: um corpo no nível médio que encosta num corpo do nível perto precisa subir de nível; corpos congelados não devem perder a velocidade se o jogo espera continuidade (hoje a velocidade é zerada).

### 3.3 Ilhas ou grupos de Rigidbody em dados contíguos

Ideia: agrupar corpos que se tocam (grafo de contatos e juntas) em ilhas. Cada ilha guarda seus corpos em arrays contíguos (posição, velocidade, massa inversa), tem um solver próprio e dorme inteira quando todos os corpos estão parados.

Ganhos prováveis:
- Dormir a ilha inteira em vez de corpo a corpo (hoje com 3000 esferas só 17 dormem no perfil; uma pilha em movimento lento mantém todos acordados).
- Localidade de cache: o solver lê dados contíguos por ilha.
- Paralelismo natural: uma ilha por tarefa, sem a coloração por cores (que é o que garante hoje a ausência de corrida de dados no solver).

Custos e dúvidas:
- Montar as ilhas a cada passo (união-busca sobre os pares de contato) tem custo; precisa ser menor que o ganho.
- Um chão estático enorme toca quase tudo; corpos estáticos não devem unir ilhas (padrão da literatura de motores de física), senão existe uma ilha só.
- O formato atual é um vetor global indexado por `i` em todo o editor (`play.tf[i]`, `play.rb[i]`, `ents[i]`). Mudar para arrays por grupo exige uma camada de índice, ou manter os arrays globais e só reordená-los por ilha.
- Em cenas com poucos contatos (a medição `--ecs 500 --play`, 0,04 ms) não há ganho.

Este é o item de maior risco e de maior possível retorno; fica por último.

## 4. Riscos

| Risco | Onde aparece | Mitigação |
|---|---|---|
| Determinismo | A ordem de resolução dos contatos muda o resultado (já observado no experimento da travessia dupla). Ilhas em paralelo mudam a ordem. | Saída com ordem fixa por ilha; mesmo resultado com 1 ou N threads, igual ao teste que já existe para a travessia dupla. Teste de hash do estado após N quadros. |
| Estabilidade | Frequência menor para corpos lentos pode causar penetração e empilhamento instável. | Só aplicar a corpos sem contato com corpos rápidos; métrica de altura média e velocidade média (as mesmas da tabela de 12_OTIMIZACAO) como critério. |
| Ordem de resolução | O solver depende da ordem dos pares; mudar a ordem muda o empilhamento. | Aceitar mudança só com critério de qualidade explícito, como foi feito no warm starting. |
| Acordar | Ilha dormindo precisa acordar quando algo externo toca, inclusive scripts (`AddImpulse`, teleporte). | Toda API que altera um corpo chama `WakeUp` na ilha, não só no corpo. |
| Estado visual | Corpos congelados pelo raio param de ser sincronizados com o Transform. | Já tratado hoje (`culled`); manter no novo modelo. |
| Medição | Máquina de desenvolvimento tem 2 núcleos; ganho com mais núcleos é desconhecido. | Registrar CPU e número de núcleos em cada medição; repetir na máquina do usuário. |

## 5. Plano em etapas com gate de medição

Regra comum: antes e depois na mesma máquina e com o mesmo benchmark (esferas caindo sobre o chão, 300 quadros, valores médio, p95 e pior quadro, corpos dormindo, velocidade média, altura média). Uma etapa só avança com o gate cumprido; se não cumprir, reverter e registrar como experimento rejeitado.

| Etapa | Trabalho | Gate |
|---|---|---|
| 0 | Repetir a medição de 3000 e 5000 esferas com o profiler por fase (`stats.solverMs`, `broadphaseMs` etc. já existem em `PhysicsWorld::stats`). Registrar data, CPU, núcleos e comando. Resolver a diferença entre ~51% e ~85%. | Tabela de linha de base com solver, broad-phase e demais fases por corpo. Sem isso, nenhuma outra etapa começa. |
| 1 | Ligar a interpolação visual (`capturePreviousTransforms` + `alpha`). Passo fixo menor opcional. | Sem mudança no resultado da simulação (hash do estado igual). Tremor visual medido por teste em 30 Hz de física e 144 Hz de tela. Custo extra da interpolação < 5% do tempo de física. |
| 2 | Níveis de atividade por distância (3 níveis). | Com 5000 corpos, 80% fora do raio: tempo de física cai proporcional ao número de corpos ativos (alvo a definir na etapa 0). Corpos no nível médio sem penetração acima do limite da linha de base. |
| 3 | Frequência menor para corpos lentos sem contato com rápidos. | Altura média e velocidade média dentro de uma faixa definida contra a linha de base; sem aumento de pior quadro. |
| 4 | Ilhas: só a detecção e o sono da ilha inteira (sem mudar o layout dos dados). | Corpos dormindo sobe de forma mensurável na pilha de teste; custo da detecção < ganho em ms. Resultado igual ao anterior quando nenhuma ilha dorme. |
| 5 | Ilhas: solver por ilha em arrays contíguos, uma ilha por tarefa. | Determinismo com 1 e N threads; tempo do solver menor que a coloração por cores em ao menos 2 cenas (pilha e corpos espalhados); sem regressão de ordem. |

Fora do escopo deste estudo: solver de juntas, física 2D, GPU.
