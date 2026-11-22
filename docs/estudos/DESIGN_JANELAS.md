# Design das janelas do editor (referência visual)

Fonte: mockups enviados pelo João Vitor em 2026-11-19 (duas imagens: Settings e Inspector; editor completo).
Cores em hexadecimal são **estimativas a olho**, não medidas. Confirmar antes de fixar no tema.

## 1. Linguagem visual (todas as janelas)

- **Corpo:** azul marinho sólido, quase preto-azulado (estimado `#0A0F9E` a `#0000A0`). Cantos arredondados (~8 px).
- **Borda:** contorno fino mais escuro ao redor de toda a janela.
- **Cabeçalho (aba):** faixa do topo com aba arredondada no canto superior esquerdo.
  - Quadrado branco pequeno (■) antes do nome.
  - Nome em **caixa-alta** e **negrito**, branco. Tamanho um pouco maior que o corpo.
  - Botão **×** logo depois do nome (fecha a janela).
  - Na aba ativa, a faixa de fundo fica mais clara que a das inativas.
- **Ícone de ajuda:** círculo branco com **?** no canto superior direito de cada janela. Clique abre o Help na seção dela.
- **Barra de rolagem:** trilho escuro, polegar **ciano** (estimado `#00E5CC`), arredondado.
- **Botões de categoria** (Settings): retângulo azul médio (estimado `#0A78C8`), cantos arredondados, texto
  branco à esquerda, **+** branco grande alinhado à direita. Ocupam a largura da janela com respiro.
- **Campos de texto:** retângulo branco com cantos arredondados, rótulo ao lado em branco (NAME, TAG).
- **Bloco destacado** (Transform): fundo azul claro (estimado `#8FB8FF`), rótulos escuros, campos brancos X/Y/Z.
- **Separadores:** linha branca fina horizontal entre seções.
- **Rótulo de seção:** texto branco em negrito (ex.: "Transform").
- **Rodapé de ações:** botões quadrados com **+** e **−** grandes, rótulos pequenos (ADD / REMOVE) embaixo.
  Estatísticas à direita em texto pequeno (Instances, Memory, Scripts).

## 2. Janela Settings (imagem 1)

- Lista vertical de categorias: Graphics, Physics, Lighting, Optimization, Sound, Export Settings, Multiplayer, Library.
- Cada categoria é um botão com **+** (expande a seção).
- Scroll à direita com polegar ciano.

## 3. Janela Inspector (imagem 1)

- Topo: campos NAME e TAG; à direita, miniatura do **Prefab** (ícone com lápis para editar e × para desvincular).
- Separador, depois bloco **Transform** com Position, Rotation e Scale, cada um com X, Y, Z.
- Cards de componentes (RigidBody, Script, Mesh Renderer), cada um com **+**.
- Rodapé: ADD / REMOVE e contadores (Instances, Memory, Scripts).

## 4. Editor completo (imagem 2)

- Barra de menu no topo: File, Edit, Window, Interface, Options; **Play** centralizado.
- Hierarquia à esquerda: fundo branco/cinza claro com texto escuro, lista em árvore (Game Manager, Main Camera, Ground, Global Light, Player com filhos), seções "Scenario" e "Instances", botões Create / Duplicate / Delete no topo.
- Centro: Scene e Game como abas; barra de ferramentas da cena (Play, Grid, Gizmos, Culled, Post-fx).
- Direita: Inspector em cima e Settings embaixo, empilhados.
- Inferior: abas File Manager, Narrative, Console, Profiler.
- Janelas a esquerda inferior: Scenes.

## 5. Diferenças para o que existe hoje

| Item | Hoje (SpriteUI, Editor info) | Mockup |
|---|---|---|
| Fundo do corpo | cinza-escuro neutro | azul marinho |
| Título | regular, sem caixa-alta | negrito, caixa-alta, quadrado branco antes |
| Botão × | só na moldura | × ao lado do nome, além do botão de fechar |
| Ícone "?" | não existe | círculo no canto superior direito |
| Botões | azul de ação, sem "+" | azul médio com "+" à direita |
| Campos | cinza escuro | brancos, cantos arredondados |
| Scroll | não há | trilho escuro com polegar ciano |
| Hierarquia | ImGui, fundo escuro | fundo claro, texto escuro |

A janela Editor info atual **não segue este design**. Eu a fiz com tema escuro neutro, o que não bate com o mockup.

## 6. Decisões a confirmar

1. Paleta exata (fundo, azul de botão, ciano do scroll, azul do bloco Transform). Preciso que você diga os hex ou aprove as estimativas.
2. Hierarquia: fundo claro (como no mockup) ou escuro (como no editor atual)?
3. O tema vale para o editor inteiro (todas as janelas) ou só para as janelas novas?
4. Tamanho do título: quanto maior que o corpo (sugestão: 1,3×)?
