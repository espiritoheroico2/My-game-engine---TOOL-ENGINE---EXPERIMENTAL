# Índice da documentação

Este índice diz o que fazer primeiro. Atualizado em 2026-11-22.

## Estrutura e nomes

| Pasta | Conteúdo | Padrão de nome |
|-------|----------|----------------|
| `planos/` | Trabalho a fazer, em ordem de execução | `NN_NOME_EM_MAIUSCULAS.md` (número = ordem) |
| `planos/00_*` | Painéis de acompanhamento (não são etapas) | `00_NOME.md` |
| `estudos/` | Análises e decisões ainda abertas, sem número | `NOME_EM_MAIUSCULAS.md` |
| `referencia/` | Guias e histórico que não mudam com frequência | `NOME_EM_MAIUSCULAS.md` |
| `imagens/` | Capturas de tela | `minusculas_com_underscore.png` |

Regra: nomes em maiúsculas com `_`, sem hífen e sem acento. Um documento fica em uma pasta só.

## Ordem de prioridade e de implementação

| Ordem | Documento | Prioridade | Por quê | Depende de |
|-------|-----------|------------|---------|------------|
| 01 | `planos/01_FALHAS_E_AVISOS_ENGINE.md` | **Crítica** | Acesso inválido, corrupção e travamento em uso normal. Atrapalham qualquer teste. | Nada |
| 02 | `planos/02_RENDER_DRAW_CALLS.md` | **Alta** | Redução de draw calls e pipeline de render. Etapas 0–8 do plano. | 01 (estabilidade); validação em GPU |
| 03 | `planos/03_DESEMPENHO_EDITOR.md` | **Alta** | Stutters do editor medidos no Profiler. | 02 (parte do render) |
| 04 | `planos/04_PENDENCIAS_EDITOR.md` | **Alta** | Lista de pendências do editor com decisões que dependem de você. | Decisões do usuário |
| 05 | `planos/05_REMOVER_IMGUI.md` | **Média** | Processo para trocar as janelas ImGui por SpriteUI, uma por uma. | 06 (usa SpriteUI) |
| 06 | `planos/06_SPRITEUI.md` | **Média** | Migração da UI para SpriteUI. | Nada |
| 07 | `planos/07_UI_DE_JOGO.md` | **Média** | Componente de UI dentro do jogo. | 06 |
| 08 | `planos/08_PARTICULAS.md` | **Baixa** | Sistema de partículas. | 02 |
| 09 | `planos/09_TEXTURAS_E_MATERIAIS.md` | **Baixa** | Texturas e materiais no renderer (hoje só cor). | 02 |
| 10 | `planos/10_FISICA.md` | **Baixa** | Física Newtoon: a parte de editor está pronta, o resto está fora desta versão. | Nada |
| 11 | `planos/11_USABILIDADE_SPRITEUI.md` | **Alta** | Análise de usabilidade do SpriteUI: Scenes sem ação, +/- pequenos, combos, hover e tema lento. | 06 |

Painéis de acompanhamento: `planos/00_ROADMAP.md` (visão geral) e `planos/00_CHECKLIST.md` (estado de cada item).

## Estudos (decisões abertas)

Estes documentos não são etapas. Servem para decidir se e quando algo entra no projeto.

| Documento | Decisão a tomar |
|-----------|-----------------|
| `estudos/AUDITORIA_SHADERS.md` | Corrigir divergências GLSL/HLSL e o limite de buffers do Vulkan. Antes da etapa 02. |
| `estudos/MATRIZ_APIS_SHADER.md` | Quais APIs gráficas validar. Antes da etapa 02. |
| `estudos/AUDITORIA_DESEMPENHO.md` | Achados de desempenho do núcleo. Base do plano 03. |
| `estudos/ESCALONAMENTO_RENDER.md` | Escala e antisserrilhado por software. Base para `AMD_FSR.md`. |
| `estudos/AMD_FSR.md` | Entrar ou não com FSR. |
| `estudos/NVIDIA_NTC.md` | Viabilidade de compressão neural de textura. Depende de 09. |
| `estudos/DIRECTX12_DXR.md` | Ray tracing em DX12. |
| `estudos/SEGURANCA_ARQUIVOS.md` | Revisão de segurança de arquivos (v0.4). Revisar antes de liberar mods. |
| `estudos/JS_VS_CPP.md` e `estudos/RUNTIME_CODIGO.md` | Runtime de script. Decidir antes de mexer no ToolObject. |
| `estudos/BIBLIOTECAS_TERCEIROS.md` | Bibliotecas candidatas, nenhuma incluída ainda. |
| `estudos/PESO_REPOSITORIO.md` | Peso do repositório e o que pode sair do histórico. |
| `estudos/REVISAO_ILUMINACAO.md` e `estudos/LIGHT_PRISM.md` | Estado da iluminação. Referência para o plano 02. |
| `estudos/DESIGN_JANELAS.md` | Referência visual das janelas do editor (cores estimadas, não medidas). |

## Referência (não tem prioridade)

- `referencia/COMPILAR.md`: como compilar.
- `referencia/CHANGELOG.md`: histórico de versões.
- `referencia/ECS.md`: API do ECS.
- `referencia/ERROS_SCRIPT.md`: códigos de erro de script.
- `referencia/IDIOMAS_E_TEMAS.md`: idiomas e temas do editor.
- `referencia/LEIA_ME_PROJETO_FINAL.txt`: copiado para a pasta final pelo CMake.

## Referências quebradas (arquivos citados que não existem)

Já existiam antes desta reorganização. Decidir se o documento volta, se a citação sai ou se aponta para outro arquivo:

- `docs/RENDER_PERF.md` (citado em `assets/lang/*.json`, `apps/editor/extra_ui.hpp`)
- `docs/sugestaoarquiteturanova.md` (citado no README e em `PENDENCIAS`)
- `docs/SHADOWS.md`, `docs/CLUSTERS.md`, `docs/IBL.md`, `docs/diagnostics.md`, `docs/PESQUISA_MILHARES_OBJETOS.md`
- `docs/ALPHA.md`, `docs/BETA.md`, `docs/BUILD_TIME.md`, `docs/ESTADO_ATUAL.md`, `docs/IMPLEMENTATION_STATUS.md`, `docs/NEXT_STEPS.md` (citados no `ROADMAP` antigo)
