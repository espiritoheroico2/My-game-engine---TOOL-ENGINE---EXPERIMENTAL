# UI de jogo e Partículas (v0.6)

## UI de jogo (componente `UiCanvas`)
- Um objeto **"Game UI (canvas)"** cria uma camada 2D sobre a imagem de uma câmera. Você pode ter **vários** canvases e escolher a câmera de cada um (vazio = todas as câmeras Principais).
- Elementos (até 16 por canvas): **Texto**, **Painel**, **Botão**, **Barra**. Posição/tamanho em fração da tela (0..1) → funciona em qualquer resolução.
- Texto aceita `{variavel}` (ex.: `Score: {score}`). **Barra** mostra uma variável (campo *Variable*) de 0 até *Full at*.
- **Botão**: ao clicar (durante o Play) envia o evento com o nome do campo *Event name*. Eventos embutidos: `add_<var>` soma 1, `sub_<var>` subtrai 1, `reset_<var>` zera.
  O mesmo evento também dispara um **gatilho do Narrative Controller** com esse nome.
- Variáveis: o Narrative e a UI compartilham as variáveis (`ed.vars`); o Narrative vê as mudanças da UI e vice-versa.
- A cena padrão já vem com um exemplo ("UI": texto, barra e botão "Add score").
- Implementação: ImGui (`ImDrawList`) por cima da imagem da câmera no painel Game (`fx.hpp`). Limite atual: só no editor/Play do editor; o jogo exportado precisará do mesmo desenhista (planejado com o exportador).

## Partículas (componente `ParticleComp`)
- Emissor CPU: taxa, vida, velocidade, abertura (cone em torno do eixo +Y do objeto), tamanho/cor início→fim, opacidade, quique no chão.
- Usa a **gravidade da física do mundo** (Settings > Physics, Newtoon) × *gravity scale*.
- Desenhadas como pontos suaves por cima da imagem (Scene e Game), **sem teste de profundidade** (atravessam objetos). Isso é rápido e simples; a versão com profundidade vem com o pipeline GPU.
- Ações do Narrative: `particles_on` / `particles_off` (argumento = nome do objeto).

## Narrative: novidades
- Gatilhos: **Start** (1x ao dar Play), **Update** (todo quadro), **Create** (campo *object*: quando um objeto com esse nome é criado durante o Play; vazio = qualquer).
- Blocos: **SavePoint** / **LoadPoint** (slot → `Project/Saves/<slot>.save.json`, escrita atômica; nome de slot validado).
- Ações: `log`, `spawn`, `destroy`, `ui_show`, `ui_hide`, `particles_on`, `particles_off`.
- **Código da cena:** toda cena (nova ou antiga ao abrir) ganha um grafo `Assets/Narrative/<cena>.narr.json` e um script `Assets/Scripts/Scenes/<cena>.scene.tool.js` que o chama. O Play executa o grafo nativamente.
