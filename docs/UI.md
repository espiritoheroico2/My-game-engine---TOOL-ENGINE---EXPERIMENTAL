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

## Estabilização da interface (0.7.19, em andamento)
- Os painéis SpriteUI usam um atlas TrueType de cobertura em modo bitmap. A resolução de rasterização acompanha o DPI da janela, com buckets de 24–256 px; o atlas passa a 2048² nos buckets de 64 px ou maiores. Não é um atlas MSDF.
- A janela Hierarchy tem menu contextual para focar, renomear, duplicar, excluir, ocultar/mostrar e travar/destravar objetos. O campo de renomear aceita posicionamento do caret, seleção por arraste, Ctrl+A/C/X/V e cursor de texto. No Inspector, arrastar dentro de um campo mantém a seleção de texto.
- A faixa de abas de Settings tem setas laterais além da rolagem pela roda. O layout redefinido coloca Settings abaixo do Inspector, em uma divisão vertical à direita.
- A barra flutuante da Scene pode ser movida pela alça de três traços. O slider de snap recebe o mouse antes da seleção de objetos na imagem.
- Os painéis Hierarchy, Inspector, Settings, Scene, Game, Console, Help e Profiler usam margem interna comum de 14 px. O Profiler desenha linhas-guia nos gráficos.
- A troca de API invalida buffers de readback e mapas de sombra específicos da GPU. A CPU continua exibindo a vista enquanto o novo backend prepara seu primeiro readback.
- O Console SpriteUI é renderizado na área de conteúdo de uma janela ImGui hospedeira, então participa do docking e redimensionamento padrão. Filtro e comando permitem posicionar o caret pelo clique, selecionar por arraste e usar atalhos de seleção/cópia/recorte/colagem; a busca do Help também suporta seleção por arraste. O cursor I-beam aparece sobre esses campos e sobre o renomear da Hierarchy.
- O renderer 2D do SDL tenta os backends disponíveis, exceto Vulkan, e usa software como fallback; isso evita uma segunda instância Vulkan junto com o backend Diligent configurado em Settings.
- A fonte ImGui usa 18 px com oversampling horizontal 3x e vertical 2x; os atlas SpriteUI são rasterizados conforme o DPI.

Esta entrega não conclui a remoção do ImGui: Narrative, Assets, Scenes e várias seções do Inspector/Settings ainda dependem dele.

## Linha de comando (testes/capturas)
`tool_editor --lang en --theme Brasa --tab 1 --api vulkan`

## Testes
`ctest --test-dir build/beta` inclui `ui_editor` (carga de pacotes, fallback, formatação, ciclo salvar/carregar tema).
Não testado: persistência com a janela aberta em máquina com tela real (só validei salvar/carregar por teste e as capturas em Xvfb).
