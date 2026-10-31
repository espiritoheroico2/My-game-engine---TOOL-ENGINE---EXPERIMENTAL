# Revisão de segurança de arquivos (v0.4)

Princípio: **todo arquivo do projeto é entrada não confiável** (pode ter sido baixado, editado à mão ou corrompido).

| Entrada | Proteção | Onde |
|---|---|---|
| Caminhos de asset/script em cenas | relativos, sem `..`, sem absoluto/drive; scripts só em `Assets/*.js` | `Project::resolve`, `scriptPathOk` |
| Nomes de arquivo/pasta digitados | `safeName` (sem separadores, `: * ? " < > \|`, controle) | `project.hpp` |
| `.scene.json` | tamanho máx. 8 MB, 100 000 entidades, enums validados, **todos os números de render/pós-fx limitados a faixas seguras** | `loadSceneFile`, `loadRenderSettings`, `ser::readPostFx` |
| `project.json` | 1 MB, ≤ 64 ligações, alvo só entre tags pré-definidas, nomes ≤ 40 | `ProjectSettings` |
| Scripts `.tool.js` | ≤ 256 KB ao ler/gravar; **nunca executados** nesta versão | `script.hpp` |
| Apagar objeto | só apaga o script se for o texto inicial intocado e não compartilhado | `Editor::releaseScript` |
| Renomear tag | recusa vazio/longo/duplicado/pré-definida; atualiza outras cenas com parse JSON (não busca textual) | `TagRegistry::rename`, `renameTagInSceneFiles` |
| Modelos/áudio | importadores com limites e fuzzers (`fuzz/`) | `engine/assets`, `engine/audio` |
| Shaders | carregados só de `assets/shaders` (caminho fixo no código) | `GpuRenderer::init` |

Limpeza: nenhum arquivo gerado (logs, `bin/`, `out/`, configurações do usuário, `Project/`) faz parte do código-fonte; ver `.gitignore`.
Risco conhecido: gravação de cena não é atômica (escreve direto no arquivo). Melhoria futura: gravar em `.tmp` e renomear.
