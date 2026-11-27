# 15 - Recursos de referência e Level maker

## 1. Para que serve este documento

Este documento é uma lista de conferência (checklist) dos recursos que engines maduras, como Unity e Unreal, costumam oferecer e que a Tool Engine ainda não tem, ou tem só em parte. A comparação usa apenas funcionalidades publicamente conhecidas. Onde há dúvida sobre o nome exato ou o comportamento de um recurso nessas engines, o texto diz "verificar na documentação oficial".

Prioridades:

- **P0**: bloqueia fazer um jogo de verdade (sem isso o jogo não fica pronto).
- **P1**: importante; o jogo sai sem isso, mas com muito mais custo ou qualidade menor.
- **P2**: bom ter depois.

Situação na Tool Engine: "existe", "parcial" ou "não existe". Foi verificada com busca no repositório (grep) em `engine/` e `apps/editor/`. Quando a busca não achou nada, o item é "não existe", mas vale conferir antes de começar a implementar, porque o nome do recurso pode ser diferente.

Estimativas de esforço: em semanas, para **um desenvolvedor trabalhando com agentes**. São números aproximados, de ordem de grandeza, sem medição. Servem só para ordenar o trabalho, não para prometer prazo.

Documentos relacionados: `00_ROADMAP.md`, `10_FISICA.md`, `08_PARTICULAS.md`, `07_UI_DE_JOGO.md`, `13_FUNCIONALIDADES_FUTURAS.md`.

---

## 2. Tabela de lacunas por área

### 2.1 Edição de cena e de nível

| Recurso | O que faz | Por que importa | Situação na Tool Engine | Prio. | Esforço |
|---|---|---|---|---|---|
| Prefab | Objeto reutilizável salvo em arquivo; instâncias na cena herdam dele | Sem isso, cada inimigo, porta ou item é copiado à mão e muda um por um | Não existe (grep "prefab" sem resultado). Cena é `.scene.json` (`apps/editor/assets_model.hpp`, `components.hpp`); há hierarquia via `Parent` | P0 | 3-4 |
| Prefab variants e overrides | Prefab derivado de outro, com campos sobrescritos | Variações (inimigo forte/fraco) sem duplicar dados | Não existe | P1 | 2-3 |
| Level streaming / cenas aditivas | Carregar e descarregar partes do mundo | Mundos grandes e tempo de carga | Não existe. Há chunks de voxel (`VoxelChunk`), mas sem carga por distância | P1 | 3-4 |
| Terreno (heightmap, pintura de textura, vegetação) | Superfície grande editável com pincel | Quase todo jogo 3D aberto precisa | Parcial: `makeVoxel(..., terrain)` e `fillTerrain` geram terreno de teste em blocos (`apps/editor/editor_state.hpp`); não há heightmap nem pincel | P1 | 4-6 |
| Snapping (grade, vértice, superfície) | Alinhar objetos ao mover/rotacionar/escalar | Montar nível sem desalinhar | Parcial: `snapToContact` em `apps/editor/viewport_tools.hpp`; sem passo de grade configurável conferido | P0 | 1-2 |
| Multisseleção e edição em lote | Selecionar vários objetos e editar juntos | Produtividade básica de level design | Parcial: seleção por raio em `viewport_tools.hpp`; multisseleção na hierarquia deve ser conferida em `hierarchy_panel.hpp` | P0 | 1-2 |
| Histórico de desfazer visual | Lista de ações com navegação | Ver e voltar a um ponto específico | Parcial: undo por instantâneo de cena (`UndoState` em `apps/editor/undo.hpp`); sem painel de histórico | P2 | 1 |
| Gizmos e ferramentas de cena | Mover/girar/escalar com alças | Base da edição visual | Existe em parte (viewport do editor); conferir cobertura | P0 | verificar |

### 2.2 2D

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Tilemap | Pintar mapas 2D com tiles em grade | Base de jogos 2D | Não existe (grep "tilemap" sem resultado) | P1 | 3-4 |
| Sprite sheets e fatiamento | Dividir uma imagem em quadros e animar | Animação 2D sem um arquivo por quadro | Não existe (grep "spritesheet" sem resultado). SpriteUI é a UI do editor, não sprites de jogo | P1 | 2 |
| Colliders 2D | Física e colisão em plano | Plataforma e top-down | Não existe. Física atual é 3D (`engine/physics`, newtoon) | P1 | 2-3 (reusar o 3D travando eixo é uma opção) |
| Câmera pixel perfect | Câmera que escala em múltiplos inteiros | Pixel art sem borrão | Não existe | P2 | 1 |

### 2.3 Interface (UI de jogo)

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Âncoras e layout básico | Posicionar relativo ao canvas | UI em várias resoluções | Existe: `CanvasComp` com âncora e referência 1920x1080 (`apps/editor/components.hpp`, `engine/canvas`) | - | - |
| Layout groups (horizontal, vertical, grade) | Organizar filhos automaticamente | Listas, inventários, menus | Não confirmado; o canvas posiciona elementos em frações do canvas. Tratar como "não existe" até conferir | P1 | 2 |
| Texto rico | Negrito, cor e ícone no meio do texto | Diálogos e tutoriais | Não existe (grep "rich_text" sem resultado) | P1 | 2 |
| Localização | Trocar idioma de textos | Publicar em vários países | Parcial: `apps/editor/i18n.hpp` e `assets/lang/*.json` cobrem o **editor**; falta o mesmo para textos do **jogo** | P1 | 2 |
| Tooltips | Dica ao passar o mouse | Usabilidade | Parcial: há em `window_manager.hpp` (editor); não é recurso do canvas do jogo | P2 | 1 |
| Data binding | Ligar valor de variável a elemento da UI | Barra de vida sem código manual | Não existe | P1 | 2-3 |

### 2.4 Assets

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Importadores FBX / glTF | Ler modelos de fora | Sem isso não entra arte pronta | Existe: `engine/assets/include/tool/assets/fbx.hpp`, `gltf.hpp`; **sem** animação, esqueleto e texturas embutidas (avisos em `model.hpp`) | P0 | 2-3 (esqueleto e skin) |
| Configurações de importação de textura | Compressão, mipmaps, sRGB, filtro por asset | Memória e qualidade | Não existe (conferir `docs/planos/09_TEXTURAS_E_MATERIAIS.md`) | P1 | 2 |
| Dependências entre assets | Saber o que usa o quê | Exportar só o necessário, apagar sem quebrar | Não existe | P1 | 2 |
| Navegador de assets com miniaturas | Ver imagens/modelos em grade | Achar assets rápido | Parcial: `assets_panel.hpp`, `assets_model.hpp`; grep "thumbnail" sem resultado | P1 | 2 |
| IDs estáveis (GUID) e arquivos `.meta` | Referência que sobrevive a renomear | Evitar links quebrados | Não existe (referência por caminho/nome; conferir) | P1 | 2-3 |
| Versionamento de assets | Histórico e migração de formato | Projetos antigos continuam abrindo | Não existe como recurso; formato de cena tem campos validados (`ser::clampNum`) | P2 | 2 |

### 2.5 Renderização

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Materiais PBR | Metálico/rugosidade/emissão | Aparência moderna | Existe: shaders `pbr*.hlsl`/glsl, campos em `components.hpp` | - | - |
| Shader graph | Criar shader por nós | Artista cria efeitos sem código | Não existe | P2 | 6-10 |
| Pilha de pós-processamento | Volume com bloom, SSAO, tonemap, cor, etc. | Acabamento visual | Parcial: bloom (`bloom.hpp`, `bloom.hlsl`), SSAO (`ssao.hlsl`), tonemap (`Tonemap.hlsli`); falta pilha/volumes configuráveis | P1 | 2-3 |
| LOD | Malha mais simples à distância | Desempenho | Parcial: `engine/render/include/tool/render/mesh_simplify.hpp` e menção a LOD no profiler; sem grupo de LOD por objeto conferido | P1 | 2-3 |
| Reflection probes | Reflexo local capturado | Metal e vidro coerentes | Não existe. Há IBL em `Ibl.hlsli` | P2 | 3 |
| Skybox | Fundo do céu | Cenário | Parcial: 6 imagens (`apps/editor/skybox.hpp`); sem céu procedural ou HDRI | P1 | 1-2 |
| Iluminação global / lightmaps | Luz indireta pré-calculada | Interiores bonitos e baratos | Não existe (grep "lightmap" sem resultado). Há luzes em cluster (`lightprism`), sombras e SSAO | P2 | 8+ |
| Decals | Projetar marcas em superfícies | Buracos de bala, sujeira | Não existe | P2 | 2-3 |
| VFX graph | Efeitos por nós em GPU | Efeitos avançados | Parcial: `ParticleComp` simula na CPU (`08_PARTICULAS.md`); sem grafo | P2 | 6+ |
| Oclusão e culling avançado | Não desenhar o que está escondido | Cenas grandes | Parcial (culling por objeto citado no profiler); verificar | P1 | verificar |

### 2.6 Animação

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Animação esquelética | Ossos e skinning | Personagens | Não existe (importadores avisam "não suportado": animação, skin/ossos) | P0 | 4-6 |
| Máquina de estados de animação | Transições entre clipes | Controlar personagem | Não existe | P0 | 2-3 |
| Blend trees | Misturar clipes por parâmetro | Andar/correr suave | Não existe | P1 | 2 |
| Timeline / sequenciador | Cinemáticas e eventos no tempo | Cutscenes e tutoriais | Não existe (grep sem resultado) | P1 | 4-6 |
| IK | Pés e mãos ajustados ao ambiente | Qualidade de movimento | Não existe | P2 | 3-4 |
| Animação por propriedade (keyframes) | Animar posição, cor, etc. | Portas, plataformas | Não existe | P1 | 2-3 |

### 2.7 Áudio

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Mixer com barramentos | Volumes por grupo | Opções de som | Existe: `Mixer`, `BusId {Master, Music, Sfx, Voice}` em `engine/audio/include/tool/audio/audio.hpp` | - | - |
| Áudio espacializado | Som 3D com distância/pan | Imersão | Parcial: "3D simples" (`spatial`, `minDistance`, `maxDistance`, ouvinte no plano XZ); sem HRTF, oclusão ou doppler | P1 | 2 |
| Eventos de áudio e efeitos | Reverb, filtros, camadas por evento | Som dinâmico | Não existe | P2 | 3-4 |

### 2.8 Scripting

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Scripts de jogo | Lógica por script | Base do gameplay | Existe: `script.hpp`, `script_host.hpp`, `script_check.hpp`, `script_complete.hpp` | - | - |
| Script visual | Lógica por nós | Designers sem código | Não existe (só aparece em `help_content.hpp` como texto) | P2 | 8+ |
| Depurador (breakpoints, passo a passo) | Parar e inspecionar | Achar bug de lógica | Não existe (grep "debugger" sem resultado) | P1 | 3-4 |
| Hot reload | Recarregar código sem reabrir | Iterar rápido | Não existe (grep sem resultado); verificar se o host de script já recarrega arquivos | P1 | 1-2 |

### 2.9 Física

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Colliders e corpos rígidos | Colisão e dinâmica | Base | Existe: `engine/physics` (newtoon), `Collider` no editor | - | - |
| Triggers | Detectar sem empurrar | Zonas, coleta | Existe: `isTrigger` em `newtoon_types.hpp` | - | - |
| Raycast | Consulta por raio | Tiro, visão, seleção | Existe: `newtoon_bvh.hpp`, `newtoon_modules.hpp`; ver exposição ao script | P0 | verificar |
| Joints | Dobradiças, juntas | Portas, cordas | Parcial: `Joint` e `JointType {Fixed, Hinge, BallAndSocket}` no motor; editor/inspetor não confirmados | P1 | 2 |
| Character controller | Movimento de personagem com degraus e rampas | Quase todo jogo 3D | Não existe (grep "character_controller" sem resultado) | P0 | 2-3 |
| Camadas e matriz de colisão | Quem colide com quem | Organização | Parcial/não confirmado (grep "Layer" em `newtoon_types.hpp` só achou `isTrigger`); verificar | P0 | 1-2 |

### 2.10 Navegação e IA

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Navmesh (geração e consulta de caminho) | Malha navegável | IA anda sem atravessar paredes | Não existe | P0 (para jogos com NPC) | 4-6 |
| Agentes de navegação | Seguem o caminho, desviam | Inimigos, multidões | Não existe | P1 | 2-3 |
| Árvore de comportamento / FSM de IA | Decisão de NPC | Comportamento | Não existe (há `engine/narrative`, a conferir se cobre) | P1 | 3 |

### 2.11 Profiling

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Profiler CPU por fase | Tempo por etapa | Achar gargalo | Existe: `profiler.hpp`, `profiler_panel.hpp`, exportação CSV/MD | - | - |
| Timers de GPU | Tempo real de GPU por passe | Otimizar renderização | Parcial: fases "GPU render/submit" e "GPU readback" no profiler; sem timers de query (grep sem resultado) | P1 | 2 |
| Frame debugger | Ver desenho a desenho | Depurar render | Não existe (ferramenta externa de captura é alternativa; verificar na documentação oficial de cada engine o que oferecem) | P2 | 3-4 |
| Visão de memória | Uso por categoria | Orçamento de memória | Não existe | P1 | 2 |

### 2.12 Build e distribuição

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Exportação por plataforma | Gerar jogo para cada SO | Distribuir | Parcial: `apps/editor/export.hpp` e `export_panel.hpp` (Windows x64 e Linux x64) | P0 (terminar) | 1-2 |
| Empacotamento de assets | Pacote único comprimido | Distribuição e cópia limpa | Não confirmado (a exportação copia pasta); verificar | P1 | 2 |
| Sistema de plugins | Estender o editor/engine | Comunidade e reuso | Não existe (só menção em `help_content.hpp`) | P2 | 4-6 |
| Plataformas extras (web, mobile, console) | Mais mercados | Alcance | Não existe | P2 | 8+ cada |

### 2.13 Colaboração

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Integração com controle de versão | Ver status/diff/commit no editor | Trabalho em equipe | Não existe como recurso do editor (o repositório usa git por fora) | P2 | 2-3 |
| Merge de cenas | Combinar mudanças de duas pessoas | Equipe sem perder trabalho | Não existe. Formato texto JSON ajuda; ordem estável de entidades e IDs por objeto ainda são necessários | P2 | 3-4 |

### 2.14 Acessibilidade, entrada e documentação

| Recurso | O que faz | Por que importa | Situação | Prio. | Esforço |
|---|---|---|---|---|---|
| Remapeamento de entrada | Jogador troca teclas | Acessibilidade e conforto | Existe: `apps/editor/input_map.hpp` (salva/carrega `input_map.json`), `input_map_panel.hpp`, `engine/input`. Falta confirmar remapeamento **em tempo de execução no jogo exportado** | P1 | 1 |
| Suporte a gamepad | Controles | Jogo de console/PC | Verificar em `engine/input` | P1 | verificar |
| Opções de acessibilidade (escala de UI, daltonismo, legendas) | Jogo para mais pessoas | Alcance | Não existe | P2 | 2 |
| Documentação in-editor e exemplos | Ajuda e amostras | Aprendizado | Parcial: `help_content.hpp`, `docs/referencia/ERROS_SCRIPT.md`, `assets/samples` | P1 | 2 |
| Gerador de documentação da API de script | Referência automática | Menos dúvida | Não existe (conferir) | P2 | 1-2 |

---

## 3. Level maker

### 3.1 O que é

Ferramenta dentro do editor para **criar e editar a geometria 3D de um nível** diretamente na cena, sem sair para um programa de modelagem. Inclui:

- blocos, rampas, arcos e outros sólidos paramétricos;
- cortes e extrusões;
- edição por vértice, aresta e face;
- atribuição de UV;
- "greybox" rápido: montar o espaço jogável com formas simples para testar o fluxo antes da arte final.

### 3.2 Por que importa para o level designer

- Testa-se o ritmo e a escala do nível em minutos, não em dias.
- Não depende do artista de ambiente para o primeiro protótipo.
- Corrigir um corredor ou uma rampa é editar a malha ali mesmo, sem exportar e reimportar.
- Combina com o modo Play do editor: editar, jogar e voltar.

### 3.3 Escopo proposto da primeira versão (V1)

Dentro:

1. Primitivas: **caixa, rampa, cilindro e cunha** (wedge), com parâmetros (tamanho, lados do cilindro).
2. Seleção por **face** e operação **mover** e **extrudir face** ao longo da normal.
3. **Snapping à grade** (passo configurável, ligado por padrão).
4. **Exportar como malha** (para um modelo usável pelo restante da engine).
5. Desfazer/refazer integrado ao `undo.hpp`.
6. Colisão gerada da mesma malha (ver 3.6).

Fora da V1 (fica para depois): arcos, cortes (boolean), edição de aresta e vértice, UV manual, simetria, modificadores.

Estimativa para a V1: **4 a 6 semanas** (um dev com agentes; aproximado). Edição de vértice/aresta e UV: mais 3 a 4 semanas.

### 3.4 Formato de dados

Guardar como **malha editável no projeto**, não como malha "cozida":

- Estrutura proposta: lista de vértices, lista de faces (polígonos, não só triângulos) e, por face, índice de material e UV. Arestas são derivadas.
- Gravar como componente na cena (`.scene.json`, no mesmo estilo de `VoxelChunk` em `components.hpp`) ou como arquivo próprio do projeto referenciado pela cena. Para malhas grandes, arquivo próprio evita cena enorme e diffs ruins.
- A malha triangulada para desenhar é **cache** gerado a partir da malha editável, com contador de revisão (mesmo padrão do `revision` do `VoxelChunk`).
- Exportar para malha final é uma ação separada, que gera um modelo sem perder o original editável.

Motivo: uma malha cozida perde faces, grupos e parâmetros; editar de novo ficaria impossível ou destrutivo.

### 3.5 Relação com o sistema de voxel/chunk existente

O que existe: `VoxelChunk` (16x16x16 blocos com paleta, `revision`) em `apps/editor/components.hpp`, `makeVoxel` e `chunkMeshesOf` em `apps/editor/editor_state.hpp`, e colunas estáticas de colisão em `editor_play.hpp`.

Reaproveitar:

- o padrão de **revisão para invalidar cache de malha**;
- a serialização na cena (carga com limites, `ser::clampNum`);
- o ciclo de colisão estática por chunk no Play.

Não reaproveitar como base:

- voxel é uma grade de células inteiras; rampas, cunhas, cilindros e ângulos livres **não cabem** nele sem perder a forma;
- o formato é um volume (cheio/vazio), enquanto o Level maker precisa de superfície com faces, UV e normais;
- misturar os dois no mesmo componente complicaria os dois.

Conclusão: **Level maker é um componente novo**, separado do voxel. Dá para reusar utilitários (revisão, cache, colisão estática) e manter o voxel como modo para terrenos em blocos. Uma ponte posterior (voxel para malha editável) é possível, mas fora da V1.

### 3.6 Ideias para melhorar o conceito

1. **Greybox que vira malha final**: o mesmo objeto guarda a malha editável e uma lista de substituição; quando o artista entrega o modelo final, ele troca a malha mantendo posição, colisão e referências do nível.
2. **Pilha de modificadores**: operações não destrutivas (array, espelhar, chanfro, solidificar) aplicadas sobre a malha base e reordenáveis. Evita refazer geometria ao mudar um parâmetro.
3. **Colisão cozida ao vivo**: ao editar, regerar a colisão estática em segundo plano (com a `revision`), para testar o nível em Play sem passo manual. Medir o custo antes (ver 3.7).
4. **Kits modulares alinhados à grade**: paredes, pisos e cantos com tamanho múltiplo da grade, para o level designer montar como peças. Guardar o kit como biblioteca de primitivas parametrizadas; depende de prefab (seção 2.1).
5. **Edição simétrica**: espelhar a edição em um eixo (ou radial) durante o trabalho; poupa tempo em corredores, salas e arcos.

### 3.7 O que medir primeiro

Antes de implementar a V1, medir (no profiler existente, `profiler.hpp`):

- tempo para gerar a malha triangulada de uma malha editável de 1.000, 10.000 e 50.000 faces;
- tempo para regerar colisão estática dessa malha (e se cabe no quadro ou precisa de thread);
- memória por malha editável e tamanho em JSON da cena;
- custo de seleção por raio em malha com muitas faces (`viewport_tools.hpp`), para decidir se precisa de BVH (já existe em `newtoon_bvh.hpp`, possível reuso);
- número de draw calls ao usar vários objetos Level maker na cena (ver `02_RENDER_DRAW_CALLS.md`).

---

## 4. Ordem sugerida

Os 8 itens para fazer a seguir, nesta ordem, e o motivo:

| # | Item | Por que agora | Esforço |
|---|---|---|---|
| 1 | **Prefab** (e overrides básicos) | Bloqueia reuso de qualquer objeto; é base do kit modular do Level maker e de inimigos/itens | 3-4 |
| 2 | **Character controller** | Sem personagem jogável não há jogo 3D testável; a física já existe e só falta o controlador | 2-3 |
| 3 | **Camadas de colisão e raycast exposto ao script** | Pequeno e destrava gameplay (tiro, visão, filtros) | 1-2 |
| 4 | **Snapping à grade e multisseleção** | Pré-requisito direto do Level maker e melhora toda edição de cena | 2-3 |
| 5 | **Level maker V1** (seção 3.3) | Ideia central do usuário; destrava greybox e montagem de níveis | 4-6 |
| 6 | **Animação esquelética + máquina de estados** | Maior lacuna para personagens; importadores já leem FBX/glTF mas descartam ossos | 6-9 |
| 7 | **Navmesh e agentes** | NPCs andando é requisito de muitos gêneros; depende de colisão estável (itens 2 e 3) | 6-9 |
| 8 | **Exportação e empacotamento completos** | Sem isso o jogo não chega a ninguém; já existe parcial, é terminar e testar | 2-3 |

Observações:

- A ordem põe primeiro o que destrava outros itens (prefab, física, snapping) e deixa para depois o que é grande e independente (animação, navmesh).
- Itens de acabamento visual (pilha de pós-processamento, skybox, LOD, decals) ficam logo atrás deste grupo; vale reavaliar a lista depois do Level maker V1.
- Todas as estimativas acima são aproximadas e devem ser revistas após a primeira medição real de cada item.
