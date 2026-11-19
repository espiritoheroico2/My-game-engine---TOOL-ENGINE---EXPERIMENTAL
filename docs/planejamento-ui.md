# Planejamento da UI (SpriteUI): layout, estilo e ordem das janelas

Referências visuais: `docs/referencias/ui/01-settings-e-inspector.png` e `02-editor-completo.png`.
Processo de cada janela: `docs/trocarjanelas.md`. Linguagem visual de referência: `docs/design-janelas.md`.

## 1. Princípios (decididos pelo João Vitor)

1. **Layout, não formato.** As imagens mostram onde os elementos ficam. Não copiamos cantos arredondados.
2. **Janelas quadradas e estilo da SpriteUI.** Só o layout dos elementos muda.
3. **Paleta configurável:** Configurações > Style/Appearance. O azul escuro das referências é o tom padrão.
4. **Menus que se recolhem:** cada seção de uma janela tem cabeçalho clicável com seta (▸ recolhido, ▾ aberto).
5. **Botões marcados:** ação principal com preenchimento forte; ação secundária com contorno; ação perigosa em
   vermelho. Nenhum botão com cara de rótulo.
6. **Inspector sem bagunça:** rótulos em coluna fixa à esquerda, campos em grade, uma seção por tema.
7. **Game com três botões:** Play, Pause e Stop (ver seção 4).

## 2. Padrão de layout (todas as janelas)

```
+--------------------------------------------------------------+
| [grip] TÍTULO (negrito)                       [?]  [_]  [X] |  cabeçalho
+--------------------------------------------------------------+
| ▾ Seção A                                                    |  seção recolhível
|   Rótulo      [ campo ..................... ]                 |  linha: rótulo fixo + campo
|   Rótulo      [ campo ..... ]  [ botão ]                     |
| ▸ Seção B (recolhida)                                        |
|                                                              |
+--------------------------------------------------------------+
| [ AÇÃO PRINCIPAL ]  [ secundária ]           estatísticas    |  rodapé fixo
+--------------------------------------------------------------+
```

Regras de medida (valores iniciais, ajustar no visual):
- Altura de linha de campo: 28 px; espaço entre linhas: 6 px.
- Rótulo: coluna de 110 px, alinhado à esquerda.
- Seção: cabeçalho de 26 px, seta à esquerda, recolhe o conteúdo.
- Botão: altura 28 px; principal com preenchimento do tema; secundário com borda do tema.
- Rolagem vertical só quando o conteúdo passa da janela.

## 3. Esquema das janelas

Cada janela descreve: seções, elementos, ações (o que a view envia ao host) e fonte de dados.

### 3.1 Editor info (piloto, em andamento)
- Seções: Projeto (Product name, Version, Description), Autor (Creator, Studio, Country, City, Website), Copyright.
- Rodapé: Save info (principal), status e lista de faltantes.
- Ações: `Save`, `Close`. Dados: `editor::info::Model`.
- Estado: moldura pronta; falta layout em seções recolhíveis e rótulos na coluna fixa.

### 3.2 Settings (prioridade 1: está vazio)
- Painel esquerdo: categorias como botões marcados com **+** (Graphics, Physics, Lighting, Optimization,
  Sound, Export, Multiplayer, Library). Cada categoria abre uma seção recolhível.
- Nova categoria **Style / Appearance**: paleta do tema (fundo, botão, ação, texto, ciano do scroll), tamanho da fonte.
- Ações: `ApplyTheme`, `SetOption`. Dados: `settings_bridge.hpp` (já existe; investigar por que o conteúdo sai vazio).
- Critério de aceite: cada categoria mostra seus itens; trocar a cor do tema muda a janela na hora.

### 3.3 Inspector
- Topo: NAME, TAG (coluna de rótulos fixa); à direita, miniatura do prefab com ações editar e desvincular.
- Seção **Transform** (sempre aberta): Position, Rotation, Scale com X/Y/Z em grade de 3 colunas.
- Seções de componentes recolhíveis: RigidBody, Script, Mesh Renderer.
- Rodapé: ADD (principal), REMOVE (secundário), contadores Instances, Memory, Scripts.
- Ações: `SetName`, `SetTag`, `SetTransform`, `AddComponent`, `RemoveComponent`. Dados: `inspector_panel.hpp`.

### 3.4 Hierarchy
- Topo: Create, Duplicate, Delete (botões marcados).
- Lista em árvore: seções "Scenario" e "Instances" recolhíveis; ícones de olho e cadeado por linha.
- Ações: `Select`, `Rename`, `Reparent`, `Create`, `Duplicate`, `Delete`. Dados: `hierarchy_panel.hpp`.
- Atenção: a mais complexa; migrar depois do Settings e do Inspector.

### 3.5 Scene e Game (abas) com barra de jogo
- Barra da cena: Grid, Gizmos, Culled, Post-fx (toggles marcados quando ativos).
- **Barra de jogo: Play, Pause, Stop** (3 botões no topo, centralizados, à direita do Play atual).
  - Play: inicia; vira "Resume" quando pausado.
  - Pause: congela a simulação; ativo só durante o Play.
  - Stop: para e volta o estado da cena ao que era antes do Play.
- Ações: `GamePlay`, `GamePause`, `GameStop`. Dados: estado de jogo do host (verificar se existe; senão criar).

### 3.6 Console
- Lista de mensagens com filtros (Info, Warning, Error) marcados.
- Botões: Clear, Copy. Campo de comando embaixo.
- Ações: `Clear`, `Copy`, `Run`. Dados: `console_panel.hpp`.

### 3.7 Profiler
- Gráfico de frame time; seções recolhíveis por zona (Priority em negrito).
- Ações: `Pause`, `Clear`. Dados: `profiler_panel.hpp`.

### 3.8 Assets / Projeto
- Barra: Import, New folder, New text file (botões marcados); alternância List / Icons.
- Busca no topo; grade ou lista de arquivos.
- Ações: `Import`, `Open`, `Rename`, `Delete`. Dados: `assets_panel`/`project_panel` (verificar nome).

### 3.9 Scenes
- Current scene; botões Save scene e New scene; lista "Scenes in project" recolhível.
- Ações: `SaveScene`, `NewScene`, `SwitchScene`. Dados: `scenes_panel` (verificar).

### 3.10 Export game, Input map, Script
- Mesma estrutura: seções recolhíveis, rodapé com a ação principal.
- Ordem interna: Export (mais simples), Input map, Script.

### 3.11 Narrative (por último)
- Editor de nós com ImNodes. Migrar só depois de todas as janelas acima e da remoção do ImGui do resto.

## 4. Ordem de trabalho

1. **Settings**: investigar o vazio, criar a categoria Style/Appearance, seções recolhíveis.
2. **Editor info**: aplicar o layout da seção 2 (piloto).
3. **Inspector**: layout da seção 3.3.
4. **Game bar**: Play/Pause/Stop (3.5). Depende de verificar o estado de jogo do host.
5. **Console** e **Profiler** (menores).
6. **Hierarchy**.
7. **Assets, Scenes, Export, Input map, Script**.
8. **Narrative**.
9. Remover ImGui do build.

Cada janela segue sete etapas (ver `docs/trocarjanelas.md`): modelo, esqueleto visual, testes de elementos,
backend, print, teste visual do João Vitor, remoção da antiga. Nenhuma janela ImGui é removida antes da aprovação.

## 5. Itens transversais

- **Paleta:** guardada nas Configurações; o padrão é o azul escuro das referências.
- **Draw calls:** unificar os pacotes da SpriteUI num envio por quadro (medido: 21,9 por quadro no modo software).
- **Cursor I-beam e seleção arrastando** nos campos: pendente (ver `docs/trocarjanelas.md`).
- **Ícone "?"** em cada janela: pendente; abre o Help na seção dela.
