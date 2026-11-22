# Pendências do editor

Atualizado em 2026-11-22. Branch de trabalho: `main`.

## Estado da rodada (2026-11-21)

- [x] Appearance: os sliders foram observados em uso; raio dos botoes `6 px`, raio das janelas `14 px`
      e texto `80%`. Apos reabrir o editor, `editor_settings.json` continha os mesmos tres valores.
- [~] Coding: a captura em execucao mostrou a lista a esquerda, editor a direita, tamanho `12` e o botao
      `Hide list`. Nenhum arquivo estava aberto, entao os numeros de linha ainda precisam de verificacao visual.
- [x] O tamanho do texto agora escala Settings, Scenes, Assets e Inspector SpriteUI.
- [x] O raio dos botoes agora chega aos botoes de Settings, Scenes e Assets.
- [x] A secao antes chamada `Settings` passou a se chamar `API/Graphics`.
- [~] Hierarchy agora recebe a escala global do texto; falta conferir visualmente com o slider em mais de um valor.
- [~] Cursor: o I-beam fica restrito aos campos editaveis/selecionaveis da janela em foco e a seta e restaurada fora deles.
      Os handlers de selecao continuam iniciando no evento do botao esquerdo; falta conferir arraste em execucao.
- [~] Inspector: a moldura opaca estava sendo desenhada por cima do conteudo SpriteUI. O corpo da moldura ficou
      transparente; teste cobre a preparacao dos campos Nome e Transform. Falta confirmar visualmente ao reabrir.
- [x] `test ECS: 100` agora inclui um teste com raio de simulacao ativo. Corpos criados durante o Play passaram a
      adicionar tambem seu estado `culled`, mantendo os vetores de fisica alinhados e evitando acesso fora dos limites.
- [ ] Lighting, Physics, Game, Optimizing e Language ainda precisam ser portados para controles SpriteUI;
      as implementacoes ImGui existentes nao foram removidas nesta rodada.
- [ ] Nenhuma janela foi removida do ImGui nesta rodada: cada remocao continua condicionada a aprovacao
      visual propria.
- [ ] A mensagem de `1763d6c` continua incorreta: `git show --stat 1763d6c` nao lista arquivos em `docs/`.
      O historico nao foi reescrito; aguarda confirmacao antes de qualquer alteracao em `main`.

## 1. Configurações (SpriteUI)

Sintoma: na janela Settings só as seções Appearance e Settings têm conteúdo. Game, Lighting, Optimizing e
Language mostram "Not available in this version yet". A versão ImGui tinha todos os controles.

- [ ] **Lighting**: portar os controles de `drawLightingTab` (`main.cpp`). Inclui Sun (cor, intensidade,
      azimute, elevação, ambiente, sombra), culling, distância de desenho, Light Prism (qualidade e custom),
      orçamento de luzes e sombras em cascata e pontuais.
- [ ] **Game**: portar os controles da aba Game da versão ImGui.
- [ ] **Optimizing**: cache de malhas, modo ocioso e escala da interface (limites em `settings_model.hpp`).
- [ ] **Language**: lista de pacotes (rádio), recarregar pacotes, exportar modelo. Ver `drawLanguageTab`.
- Cada campo novo precisa de `sc::Field`, um setter em `main.cpp` e um caso em `settings_changes.hpp`.

## 2. Tema (paleta)

Sintoma: trocar o tema em Appearance não muda o editor por completo.

- [x] Molduras SpriteUI leem `palette::active()` a cada quadro (`window_palette.hpp`, `framed_window.hpp`).
- [x] Painéis que copiavam cores no construtor agora atualizam a cada quadro (Inspector, Status bar, Menu,
      Hierarchy, Help, Scene, Game, Profiler, Editor info, Scenes, Assets).
- [ ] Painéis com cores fixas próprias: Console, Profiler (corpo), Assets (corpo), Scenes (fundo do corpo),
      Settings (corpo), Coding. Trocar pelos tokens de `palette::active()`.
- [ ] Unificar com o tema ImGui (`ed.theme.setPreset`): hoje são duas listas. Enquanto o ImGui existir, a troca
      de paleta não muda as janelas ImGui.
- [ ] Decidir se o padrão continua "Azul escuro" (visual atual das molduras mudou para azul).

## 3. Remoção do ImGui

Ordem (cada janela só sai do ImGui depois da aprovação visual):

1. [x] Código morto da Scene, Game e Hierarchy (`3d688ef`).
2. [x] Janela de script em SpriteUI (`0b18486`). Falta verificar teclado, mouse e roda em execução real.
3. [~] **Coding**: o corpo em `coding_panel.hpp` ja usa SpriteUI, sem chamadas ImGui; falta a aprovacao visual
       do editor com um arquivo aberto e os numeros de linha visiveis.
4. [ ] Diálogo de projeto (`project_dialog.hpp`, 41 chamadas).
5. [ ] Exportar (`export_ui.hpp`, 41 chamadas).
6. [ ] Narrative Controller (`narrative_panel.hpp`, 55 chamadas).
7. [ ] `extra_ui.hpp` (236 chamadas): inventário função a função antes.
8. [ ] `fx.hpp` (16 chamadas).
9. [ ] Dependências de tipo ImGui: `theme.hpp`, `ui_contract.hpp`, `view_shapes.hpp`, `editor_scripts.hpp`,
       `ui_test.cpp`, e helpers de viewport com `ImDrawList` (`ViewXf`, `line3`, `snapToContact`, `pickByRay`,
       `rayFromScreen`), usados por `selftest.hpp`.
10. [ ] Backend do ImGui em `main.cpp` (contexto, input, `ImGui::Render`, DockBuilder residual).

## 4. Janelas e docking

- [ ] Desagrupar abas: hoje a aba precisa ser ativada e o grip da barra arrastado para fora. Decidir: um grip
      por aba, ou arrastar o nome da aba também desagrupa?
- [ ] Janela de script: verificar em execução real teclado, seleção por arrasto, Ctrl+S, Ctrl+Espaço,
      snippets e roda. Implementar o arraste da barra de rolagem (hoje só a roda).
- [ ] Build sem SpriteUI (`TOOL_WITH_SPRITEUI=OFF`) não desenha Scene nem Game. Remover os ramos `#else` ou
      manter a opção?

## 5. ECS e SpriteUI

- [ ] Ligar um painel piloto ao adaptador `apps/editor/spriteui_ecs.hpp` (já existe, com teste
      `Editor.SpriteUiEcs`) e migrar dados e callbacks sem trocar o resto do editor.

## 6. Documentação

Removidos nesta revisão: `PARTICULAS_E_DEBUG_PROMPTS.md`, `planejamento-ui.md`, `PENDENCIAS_REMOCAO_IMGUI.md`.

Candidatos a revisão (referenciados pouco ou com dados de versões antigas, por decisão sua):
`design-janelas.md`, `JS_VS_CPP.md`, `NTC.md`, `FSR.md`, `TEXTURES.md`, `RUNTIME.md`, `SECURITY.md`,
`SHADER_API_MATRIX.md`, `CHECKLIST.md`.

## 7. Plano incremental de renderização

Base: `docs/sugestaoarquiteturanova.md`. Adotar a proposta por etapas, sem substituir o `GpuRenderer` ou duplicar
o batching existente. Os nomes P0-P5 do documento são locais à proposta; mapear para as prioridades do
`ROADMAP.md` antes de reordenar o roadmap oficial.

- [x] **P0 parcial**: contar os `drawIndexed` do `GpuRenderer` por passe, vista Scene/Game e total; capturar o
      `DrawStats` do backend logo após cada `present()` e somar explicitamente as vistas; medir CPU por passe;
      mostrar no Profiler e exportar CSV junto ao Markdown. `gpu_ms` fica vazio: timestamps reais não existem ainda.
- [ ] Completar P0: validar os contadores locais contra o backend por vista e decidir como incluir uploads,
      mudanças de pipeline e tempos reais de GPU sem confundir CPU com GPU.
- [x] Bounds/cache: bounds invalidam por revisão; assinaturas de sombra ignoram casters inativos e incluem
      revisão/contagens da malha e transformação mundial dos chunks. A assinatura do BVH de RT também considera
      revisão da geometria. Autotestes cobrem revisão sem realocação, desativação e movimento de chunk.
- [x] Culling CPU de casters por frustum de cada cascata CSM e face Point/Spot; bounds de malha são calculados
      ao atualizar o buffer e transformados conservadoramente. CSM testa os quatro planos laterais para manter
      casters fora do intervalo de profundidade que ainda podem projetar sombra. Falta validação visual em GPU.
- [x] Cache Point/Spot por face: assinatura da luz, projeção, resolução e casters que intersectam a face. Faces
      sujas preservam as demais cores do atlas, limpam a região por um passe scissored e limpam a profundidade
      compartilhada antes de redesenhar. Erros síncronos de gravação mantêm a face inválida.
- [x] Profiler mostra, por vista e no total, faces reutilizadas/refeitas, primeiro uso, recriação do alvo,
      mudanças de luz/casters e falhas; CSV inclui as sete métricas e testes cobrem exibição, agregação e schema.
- [ ] Validar visualmente cache ligado/desligado e compilar o GLSL gerado; o RHI ainda não expõe um resultado
      de submissão assíncrona.
- [x] Passe de emissão GPU agrupa objetos emissivos por malha e usa instâncias (até 4096 por lote), mantendo
      transparência fora do agrupamento. Falta comparar visualmente o bloom em execução GPU.
- [~] Extraída a seleção por índices das filas opaca, prepass, emissores e casters de sombra para um builder
      CPU reutilizável. Planejamento de batches agora consome as filas; `plan.order` referencia posições nelas,
      enquanto os dados de instância são empacotados depois. A transparência continua no caminho ordenado atual
      (`tdraws_`), sem mudança de ordem. Falta comparar imagens e validar visualmente em execução GPU.
- [ ] Medir `Flush()` no Diligent antes de alterar submissão.
- [ ] Deixar compute/indirect e Hi-Z para depois do caminho CPU validado, com capacidades explícitas e fallback.
