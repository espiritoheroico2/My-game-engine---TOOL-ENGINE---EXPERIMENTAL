# 14 — Limpeza do repositório

Lista de arquivos que podem sair do repositório, com o motivo e o estado. Nada foi apagado ainda: a remoção foi bloqueada pelo sistema e precisa de aprovação explícita.

## Remover (gerados ou sobras, sem uso no build)

| Arquivo | Tamanho | Motivo | Ação proposta |
|---|---|---|---|
| `build_log.txt` | ~316 KB | Log de build. Já está no `.gitignore`, mas foi versionado no commit `95e598c`. | `git rm --cached build_log.txt` e apagar o arquivo |
| `ProfilerExport_20261124_223916_160.csv` e `.md` (raiz) | ~2,5 mil linhas | Exportação de Profiler na raiz. A cópia útil de referência fica em `docs/diag/`. | Apagar da raiz |
| `git add.txt` | 23 bytes | Saída de comando colada por engano (conteúdo: `third_party/LightPrism/`). | Apagar |

## Revisar antes de remover

| Caminho | Tamanho | Observação |
|---|---|---|
| `third_party/SDL3/` (código fonte inteiro) | vários MB | O SDL3 é compilado a partir desta cópia pelo CMake (`cmake/Dependencies.cmake`). Só dá para cortar backends de outras plataformas (Metal, Android, Linux, Xbox) depois de uma compilação de teste em cada uma. Não é seguro cortar sem isso. |
| `third_party/SDL3/src/hidapi/windows/test/data/` | ~650 KB | Dados de teste do HIDAPI, não usados pelo build. Candidato forte. |
| `docs/imagens/ui_*.png` (capturas antigas) | ~0,8 MB | **Já removidas** nesta sessão: não condiziam com a interface atual. As capturas novas estão em `docs/imagens/` (play_game_inspector, coding_editor, play_scene_console, render_3d_sombras). |

## Como verificar antes de apagar algo

1. `git grep <nome>` para confirmar que nada referencia o arquivo.
2. Compilar `tool_editor` e rodar `--selftest` depois da remoção.
