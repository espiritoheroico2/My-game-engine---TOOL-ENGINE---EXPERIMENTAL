# CODAR.MD — o que você precisa testar (branch `ui-settings-router`)

Data: 2026-11-19
Branch: `ui-settings-router` (base: `main` em `908cbaf`). Não faça merge no `main` antes dos testes abaixo.

Este arquivo substitui o CODAR.MD antigo, que foi removido no commit `908cbaf` ("remove old docs").
O roteiro geral continua em `docs/ROADMAP.md`. Aqui ficam só o que mudou e o que você precisa testar.

---

## 1. O que mudou

### Já no `main` (não precisa testar de novo, mas vale rever)
| Commit | O que faz |
|---|---|
| `a98825e` | SpriteUI: camada no clique, arraste só pelo grip (14 px à esquerda do título), limiar de 16 px para sair do dock |
| `21b15ac` | SpriteUI: `UIEventRouter::TopmostAt`, consulta do alvo mais alto sem estado |
| `89254f8` | Editor: `PanelRouter` (regra de camada da SpriteUI para o editor), teste `Editor.PanelRouter` |
| `e16b7f2` | Editor: captura de exceções no topo (grava `crash_report.txt`), spawner do teste ECS para ao faltar memória |
| `1048fa7` | Build MSVC: desliga o aviso C4324 (relleno de alinhamento intencional) |

### Só nesta branch
| Commit | O que faz |
|---|---|
| `f2cdd24` | Menu e Settings passam pelo `PanelRouter`. Com a lista de um menu aberta, o fundo de captura bloqueia cliques nas janelas de trás. Pontos cobertos por painel SpriteUI não são repassados ao ImGui |
| `a8b1676` | Texto de aviso da GPU no Profiler aponta para `docs/ROADMAP.md` (o `CODAR.MD` antigo não existe mais) |
| (este commit) | Este `CODAR.MD` |

---

## 2. Preparar o build (Windows)

1. Atualize o repositório e troque de branch:
   ```
   git fetch origin
   git checkout ui-settings-router
   git pull
   ```
2. Compile com `build_editor.bat` (ver `docs/BUILD.md`).
3. Rode os testes automáticos (passo 3).
4. Abra `Projeto Final\ToolEditor.exe`.

Se o build falhar, envie o `build_log.txt` inteiro. Não tente corrigir sozinho.

---

## 3. Testes automáticos

Rode na pasta de build usada:
```
ctest --output-on-failure
```

Resultado esperado: **35 testes, 100%** (rodado em 2026-11-19: 35/35). Os que tocam nesta branch:
- `Editor.PanelRouter` (roteamento: menu bloqueia Settings; fora das áreas, nada muda)
- `Editor.WindowManager` (30 verificações: camada, popup, grip, limiar)
- `SpriteUI.LayersAndInput` (no build da SpriteUI sozinha: 39 testes passam)

Se algum falhar, envie a saída completa do `ctest --output-on-failure`.

---

## 4. Testes visuais (você faz; eu não consigo ver a tela)

Para cada teste, marque **OK**, **FALHOU** ou **NÃO TESTADO**. Em caso de falha, anexe uma captura de tela.

### T-1. Menu aberto não deixa clique passar (principal desta branch)
1. Abra **Configurações** (Settings) para que a janela fique visível.
2. Clique no menu principal para abrir uma lista (por exemplo, Arquivo).
3. Clique numa área da janela de Configurações que fica **embaixo** da lista aberta (uma aba, um botão de rádio, um controle).
4. **Esperado:** nada muda nas Configurações. A lista continua aberta.
5. Clique numa opção da lista.
6. **Esperado:** a opção é executada, e a lista fecha.

### T-2. Clique fora da lista fecha a lista e não atinge nada atrás
1. Abra uma lista do menu.
2. Clique numa área vazia da cena ou das Configurações, longe da lista.
3. **Esperado:** a lista fecha. O clique não ativa botão, aba ou controle que estava embaixo.

### T-3. Configurações continuam funcionando (regressão)
1. Troque de aba (Gráficos, Iluminação, Idioma etc.).
2. Mude o slider de escala de renderização, marque e desmarque uma caixa, abra e feche um dropdown.
3. Troque a API de renderização no botão de rádio (anote a API escolhida e confirme o status "Switching to ...").
4. **Esperado:** todos os controles respondem, e nada fica preso.

### T-4. Atalhos de teclado (regressão)
1. Use Delete e F2 num objeto selecionado.
2. **Esperado:** o teclado segue funcionando como antes, inclusive com a lista do menu aberta.

### T-5. Teste ECS (estabilidade)
1. No console, digite `test ECS: 10000`.
2. **Esperado:** o teste roda e o editor continua aberto.
3. Digite `test ECS: 1000000`.
4. **Esperado:** aparece um aviso com a memória estimada. Se faltar memória, o teste para com a mensagem "out of memory after N spheres", e o editor **não** fecha.
5. Se o editor fechar, envie o arquivo `crash_report.txt` (fica na pasta de execução) e o log do console.

### T-6. Aviso de GPU no Profiler
1. Abra o Profiler e procure a linha sobre GPU %.
2. **Esperado:** o texto termina com "see docs/ROADMAP.md, item 7".

### T-7. Capturas da nova janela Editor info (SpriteUI, ainda fora do menu)
1. Na pasta de build, rode `editor_info_capture` com a fonte e uma pasta de saída (ver o cabeçalho de `apps/editor/tools/editor_info_capture.cpp`). Ele grava três `.ppm`.
2. Converta para PNG com a ferramenta que você preferir, e confira: campos com fundo, lista de faltantes em amarelo, "Saved." em verde no cenário 3.
3. Print do editor inteiro (sem tela): `ToolEditor --project <pasta do projeto> --frames 120 --screenshot <arquivo.ppm>`. Grava o último quadro. Converta para PNG como acima.
4. Esta janela **não** substitui a ImGui ainda. A ImGui "Editor info" continua sendo a do menu. A remoção só acontece depois do seu OK.

### Não testar ainda (ainda não migrados nesta branch)
- Janela solta acima de janela encaixada, barra de título sempre visível, arraste só pelo grip, e docking das janelas do editor. Essas regras estão prontas na SpriteUI e no `WindowManager`, mas **os painéis do editor ainda não usam elas**. Teste depois da migração (passo 6).

---

## 5. Como me enviar o resultado

Envie uma lista com:
- Resultado de cada teste (T-1 a T-6) como OK, FALHOU ou NÃO TESTADO.
- Para cada falha: o que você fez, o que aconteceu, e uma captura de tela.
- Saída do `ctest --output-on-failure`.
- Arquivos `crash_report.txt` e log do console, se existirem.

---

## 6. Remover o ImGui: plano em etapas (não é uma etapa só)

O ImGui ainda desenha a maior parte do editor. Para medir o que resta, rode na raiz:
```
grep -c "ImGui::Begin" apps/editor/main.cpp
```

Ordem proposta (cada etapa só entra depois do teste visual da anterior):

1. **Configurações completas em SpriteUI**: abas Aparência, Configurações, Jogo, Iluminação, Otimização e Idioma. Já existe a ponte `settings_bridge.hpp`. Só remova o ImGui dessa janela quando a paridade estiver testada (T-3 sem falhas).
2. **Janela Console** (já tem `console_panel`). Teste: `test ECS`, filtros, Copiar, clique que seleciona objeto.
3. **Janela Profiler** (já tem `profiler_panel`). Teste: seção Priority em negrito, gráfico de frame time.
4. **Hierarchy e Inspector** (são as mais complexas). Teste: selecionar, renomear, arrastar para pai, adicionar componente.
5. **Assets, Projeto e Narrative**.
6. **Remover o ImGui de vez**: só depois de todas as etapas acima passarem nos testes visuais. Aí sai o `imgui` do build, do `main` e do `CMakeLists.txt`.

Para cada etapa, eu faço: modelo e teste automático primeiro, depois a janela SpriteUI com o roteador, depois você testa visualmente, e só então eu removo o ImGui daquela janela.

---

## 7. Erros nos documentos (corrigidos em `docs/ROADMAP.md`, seção 9)

- `docs/ROADMAP.md`: `test ECS: 500/10000` corrigido para `500/1000`. Confirme no teste T-5.
- `docs/ROADMAP.md`: o item do Settings dizia para integrar `settings_bridge.hpp`, mas a ponte ja esta integrada.
- As pendências T11, T12 e T13 agora estão no `docs/ROADMAP.md` (Fases A, C e D) e no `docs/CHECKLIST.md`.
- Não verifiquei se `HOW_TO_BUILD.txt` e `COMO_COMPILAR.txt` (citados em `docs/BUILD.md`) existem no repositório.
- `docs/CHANGELOG.md` e o relatório de 2026-11-18 divergem na contagem de arquivos GLSL (23 e 26). Conferir antes de citar.

---

## 9. Partículas 2D/3D, gizmos e debug (para você ajudar em paralelo)

O plano, a arquitetura proposta e os prompts prontos para cada peça estão em
`docs/PARTICULAS_E_DEBUG_PROMPTS.md`. Resumo: partículas 2D espaciais na SpriteUI, partículas 3D com Light Prism,
núcleo de simulação compartilhado, e classes `DebugDraw`, `Gizmos` e `Canvas2D` (estilo GameMaker/Turtle) para
remover o ImGui dos gizmos e da grade. As decisões em aberto estão na seção 5 desse documento.
