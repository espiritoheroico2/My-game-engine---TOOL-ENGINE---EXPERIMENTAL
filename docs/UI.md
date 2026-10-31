# Editor: idiomas e temas (v0.2.1)

## Configurações (Arquivo ▸ Configurações, ou menu Opções)
Janela ancorável com abas **Aparência**, **Idioma** e **Gráficos** (a antiga janela de API gráfica agora é uma aba).

### Aparência
- Temas prontos: Escuro, Claro, Meia-noite, Floresta, Brasa, Alto contraste.
- 5 cores-base (fundo, superfície, texto, destaque, borda) geram a paleta inteira; "Cores avançadas" edita cada uma das ~55 cores do ImGui, com filtro.
- Arredondamento e escala da UI.
- "Salvar tema" grava `assets/themes/<nome>.json`; qualquer JSON nessa pasta aparece em "Temas do usuário". A escolha atual persiste em `editor_theme.json`.

### Idioma
- Pacotes são JSON em `assets/lang/` (incluídos: pt-BR, en, es). Formato:
  `{"code":"en","name":"English","strings":{"Hierarquia":"Hierarchy", ...}}`
- A chave é o texto-base em português sem acento; frase sem tradução aparece em português (nada quebra).
- "Exportar modelo" grava `assets/lang/modelo.json` com todas as frases que a UI já usou; traduza, renomeie `code`/`name` e salve na pasta; "Recarregar pacotes" aplica sem reiniciar.
- Títulos de janela usam `Texto###id`: trocar de idioma não bagunça o layout salvo.
- Fonte: `assets/fonts/ui.ttf` (opcional) ou fonte do sistema (Segoe UI / DejaVu); cobre latim, grego e cirílico. Idiomas CJK precisam de uma fonte própria em `assets/fonts/ui.ttf` (suporte a fonte por pacote ainda não existe).

## Outras melhorias
Estilo com mais respiro e cantos arredondados, barra de status (API, FPS, entidades, erros, idioma), menu Opções, abas com realce, fonte nítida.

## Linha de comando (testes/capturas)
`tool_editor --lang en --theme Brasa --tab 1 --api vulkan`

## Testes
`ctest --test-dir build/beta` inclui `ui_editor` (carga de pacotes, fallback, formatação, ciclo salvar/carregar tema).
Não testado: persistência com a janela aberta em máquina com tela real (só validei salvar/carregar por teste e as capturas em Xvfb).
