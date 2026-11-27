# 17 — Sistema de partículas (planejamento)

Status: **planejamento** (nada implementado ainda). Ver também `13_FUNCIONALIDADES_FUTURAS.md` e `12_OTIMIZACAO_FISICA_ECS.md`.

## 1. Requisitos (do pedido do usuário)

1. Sistema **remodelado**, extremamente otimizado e complexo, substituindo o atual.
2. Simular **fluidos, fumaça, nuvens e faíscas**.
3. Receber **malhas** (partícula = objeto 3D) e **shaders**.
4. **Biblioteca nativa** de partículas, com **ECS** e **data-driven** (o comportamento vem de dados, não de código).
5. Emitir **milhões de partículas** sem gargalo no jogo.
6. Começar com **partículas de malha simples**; para isso a engine precisa de **Material** e de **shaders** como recursos.
7. **Física opcional e modular**: a partícula pode colidir, mas o sistema funciona sem física, e física não pode custar nada quando desligada.

## 2. Estado atual (base de partida)

- Um `ParticleComp` por objeto (`apps/editor/components.hpp`), com limite de 5.000 partículas por emissor (clamp no carregamento).
- Simulação na CPU, por emissor, em `apps/editor/fx.hpp` (`Editor::updateParticles`), usando `Editor::Particle` (posição, velocidade, idade, vida, fase, deslocamento de nascimento).
- Sem ECS próprio, sem dados por asset, sem malhas, sem materiais por partícula, sem GPU.
- Presets (fogo, fumaça) em `extra_ui.hpp` (`applySmokePreset`).

O atual serve de referência de comportamento, mas não de arquitetura.

## 3. Arquitetura proposta

Biblioteca nova: **`engine/particles/`** (namespace `tool::particles`), independente do editor. O editor só cria emissores e desenha.

### 3.1 Dados (SoA em chunks)

- Cada **emissor** é uma entidade ECS com `EmitterComp` (referência ao asset de efeito, estado, contadores).
- As partículas ficam em **pools SoA** (um array por campo), divididos em **chunks** de 4.096 partículas, para o paralelismo e o culling por chunk.
- Campos por partícula (aprox. 44 B): posição (3 f32), velocidade (3 f32), idade, vida, tamanho, cor (RGBA8 em u32), semente (u32). Campos opcionais (rotação, escala de malha, trilha) entram só se o efeito pedir.
- Pool com **lista de livres** (ou compactação por chunk): sem realocar no quadro.
- Um milhão de partículas com os campos base: ~44 MB. Meta de memória: ≤ 64 B por partícula com campos opcionais.

### 3.2 Efeito como dado (data-driven)

- Um **efeito** é um asset JSON (`<projeto>/Effects/<nome>.json`), com uma lista de **módulos** em ordem:
  - **Emissão**: taxa contínua, rajadas (bursts), formato (ponto, esfera, caixa, superfície de malha).
  - **Inicialização**: vida, velocidade (cone, esfera), tamanho, cor, rotação, seleção de malha/material.
  - **Atualização**: gravidade, arraste (drag), turbulência (ruído de gradiente / curl noise), cor e tamanho ao longo da vida, animação de sprite.
  - **Colisão** (opcional, módulo de física, ver §5).
  - **Renderização**: billboard, malha instanciada, faixa/trilha (ribbon).
- Módulos são registrados por nome (tabela de fábricas). Um efeito novo é só um JSON; um módulo novo é código, registrado uma vez.

### 3.3 Execução

- **CPU (baseline, sempre disponível)**: `tool::parallelFor` sobre os chunks (pool já existente). Cada chunk é atualizado por inteiro, com laços simples sobre arrays (auto-vetorizável).
- **GPU (fase 3)**: o estado do pool vai para buffers de armazenamento (storage/UAV). Emissão por contador atômico (append), atualização em compute, desenho por **draw indireto** (`drawIndexedIndirect`, já disponível no RHI desde o teste de culling). Mesmo modelo de dados, outro executor.
- A escolha é por efeito (`"exec": "cpu" | "gpu"`), com fallback para CPU quando o backend não tem compute.

### 3.4 Tipos de efeito (presets de partícula)

| Efeito | Comportamento | Módulos principais |
|---|---|---|
| Faíscas | vida curta, velocidade alta, arraste, cor quente → escura | emissão em rajada, gravidade, cor/tamanho ao longo da vida, trilha opcional |
| Fumaça | vida média, empuxo, turbulência, cresce e some | turbulência, empuxo (gravidade negativa), tamanho ao longo da vida, alfa suave |
| Nuvens | campo lento, grandes, pouco alfa | turbulência de baixa frequência, emissão em volume |
| Fluido | partículas densas que se atraem e escoam | vizinhança (grade uniforme), pressão simplificada (SPH leve), colisão com planos e caixas |
| Malha simples | cubos/esferas com material | malha instanciada, rotação, escala |

Fluido é o mais caro: entra na fase 4, com grade de vizinhança e número de vizinhos limitado.

## 4. Materiais e shaders (pré-requisito)

Partícula com malha precisa de material. Esse é o primeiro passo, e serve também ao resto da engine:

- **Material** como asset JSON (`<projeto>/Materials/<nome>.json`): albedo (RGBA, com alfa), rugosidade, metálico, emissivo, textura, modo de mistura (opaco, alfa, aditivo).
- **Shader** como asset: referência a um par de shaders (vértice/pixel, ou compute) do pacote do projeto, com parâmetros declarados.
- Partículas guardam um **índice de material** (u16). Ao desenhar, agrupam-se por material: um draw indireto por (malha, material).
- Transparência: fumaça e nuvens usam ordenação aproximada por chunk (sem ordenar milhões de partículas); faíscas usam aditivo (ordem irrelevante).

## 5. Física (modular e opcional)

Regra: **física desligada custa zero**. O módulo não é compilado no caminho quando não há módulo de colisão no efeito.

- **Colisão analítica** (planos, caixas, esferas): barata, por partícula, na CPU ou na GPU. É o padrão para faíscas, fumaça e fluidos.
- **Colisão com a física Newtoon** (corpos reais): opcional, só para **detritos** (poucas dezenas a poucos milhares de corpos, com limite fixo). Cada detrito vira um corpo do Newtoon em um emissor de "corpos", sem competir com milhões de partículas.
- **Fluido com física** (SPH): é a única parte que usa física de forma intensa; fica atrás de flag e de limite de vizinhança.
- Sem física: o efeito não tem módulo de colisão, e o sistema não toca em nada da física.

## 6. Integração com o editor

- `ParticleComp` vira uma referência a um **efeito** (asset) + sobrescritas simples (taxa, cor, escala), para não quebrar cenas antigas (migração na leitura).
- Inspector: lista de módulos do efeito, com editor simples (campos por módulo).
- Console: `test particles: N` (N partículas em um emissor de teste, CPU ou GPU), para o benchmark de §7.
- Presets exibidos no menu de efeitos (fogo, fumaça, nuvens, faíscas, fluido, malha simples).
- Tooltips em todos os campos novos (requisito do usuário).

## 7. Fases e critérios de aceite

| Fase | Entrega | Critério de aceite |
|---|---|---|
| F0 | Materiais e shaders como assets (JSON), uso em malhas e partículas | cena com 2 materiais; selftest passa; ctest passa |
| F1 | Núcleo CPU: pools SoA, emissão, módulos base, billboards, efeitos JSON | 1 M partículas com update CPU em tempo medido e registrado; determinismo por semente; testes de pool (alocar, liberar, reaproveitar) |
| F2 | Malhas instanciadas com material; efeito "malha simples" | 100 k cubos com material; número de draws por material |
| F3 | Executor GPU: compute de atualização, emissão atômica, draw indireto | mesmo resultado que a CPU (contagem e posições dentro de tolerância); 10 M partículas com quadro medido |
| F4 | Efeitos: fumaça, nuvens (turbulência), faíscas (trilha), fluido (vizinhança) | cada efeito com preset e medição de quadro |
| F5 | Física modular: colisão analítica; corpos Newtoon para detritos com limite | física desligada: custo zero medido; com colisão: limite respeitado |
| F6 | Editor: editor de efeitos, presets, migração do ParticleComp, testes | selftest, ctest e bench documentados |

Medição: cada fase registra quadro médio, p95 e memória no bench (`ECS bench:` ou comando dedicado), sempre no mesmo hardware (RX 6500 XT, Vulkan, 8 núcleos, 1280x720).

## 8. Riscos

- **GL sem compute**: efeitos com `exec: gpu` caem para CPU. Documentar a diferença de desempenho.
- **Ordenação de transparência** em milhões de partículas: resolver por aproximação (chunks, aditivo) e não ordenar.
- **Fluido** pode consumir todo o orçamento; limitar vizinhos e número de partículas por efeito.
- **Migração do ParticleComp**: cenas antigas precisam abrir sem perda (teste de ida e volta).
- **Dependência de RHI**: o caminho GPU depende do que já foi testado só em Vulkan (D3D12, D3D11 e GL não testados).

## 9. Ordem recomendada

1. F0 (materiais e shaders): é pré-requisito de malhas e de partículas com material, e melhora a engine como um todo.
2. F1 e F2 em paralelo com F0 na parte de dados, sem bloquear.
3. F3 (GPU) só depois que F1 estiver medido, para decidir com números.
4. F4 a F6 em sequência.
