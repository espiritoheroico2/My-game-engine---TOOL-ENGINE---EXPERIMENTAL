# UIBuilder: projeto da ferramenta de layout

Estado: projeto. Nenhum código foi escrito ainda. Este documento define o formato do arquivo, o caminho
de leitura até a árvore de elementos, a regra de ajuste e as fases com critérios de aceite.

## 1. O que é

Um executável separado, que usa só a biblioteca SpriteUI (núcleo e, para abrir a janela, o adaptador SDL3
opcional que já existe). A ferramenta não depende da engine nem do ImGui.

Ela abre uma janela com uma grade. O usuário:

- clica com o botão direito na grade e escolhe, num menu, um dos tipos de elemento do mapa (janela, painel,
  botão, campo de texto e assim por diante);
- arrasta o elemento para posicionar, puxa as bordas ou os cantos para redimensionar;
- ajusta as propriedades (texto, raio, estilo) num painel lateral;
- salva e carrega um arquivo `.json`.

O arquivo é a fonte de verdade. A engine (fase 5) lê o mesmo arquivo e monta a mesma árvore.

Exemplo de uso: o designer monta a tela de "Iniciar partida" (uma janela com um botão "Iniciar partida" e
um campo "Nome do jogador") sem escrever código. A engine carrega o arquivo ao abrir essa tela.

## 2. Formato JSON proposto

Nível de cima: versão do formato, estilos por nome e a lista de janelas. Cada nó da árvore tem `id` (único
no arquivo), `type` (nome da classe em `include/SpriteUi/Elements`), `box` (âncoras e offsets em pixels
lógicos), `props` (propriedades do tipo), `style` (nome de um estilo da tabela) e `children`.

Exemplo curto, com uma janela, um botão e um campo de texto:

```json
{
  "formatVersion": 1,
  "styles": {
    "destaque": { "cornerRadius": 6 }
  },
  "windows": [
    {
      "id": "janela-inicio",
      "type": "Window",
      "box": {
        "anchorMin": [0.5, 0.5], "anchorMax": [0.5, 0.5],
        "offsetMin": [-160, -90], "offsetMax": [160, 90]
      },
      "props": { "title": "Nova partida", "minSize": [200, 140] },
      "children": [
        {
          "id": "campo-nome",
          "type": "TextField",
          "box": { "anchorMin": [0, 0], "anchorMax": [1, 0],
                   "offsetMin": [12, 48], "offsetMax": [-12, 80] },
          "props": { "placeholder": "Nome do jogador" }
        },
        {
          "id": "botao-iniciar",
          "type": "Button",
          "box": { "anchorMin": [0.5, 1], "anchorMax": [0.5, 1],
                   "offsetMin": [-70, -48], "offsetMax": [70, -12] },
          "props": { "label": "Iniciar partida" },
          "style": "destaque"
        }
      ]
    }
  ]
}
```

Regras do formato:

- `formatVersion` é obrigatório. Versão diferente da que a ferramenta conhece é recusada na leitura, sem
  mexer no arquivo.
- `box` segue o `Transform2D` da biblioteca: âncoras entre 0 e 1 dentro do pai, offsets em pixels lógicos.
  Escala de DPI não entra no arquivo; quem desenha aplica `globalScale` de `UISettingsConfig`.
- `style` é um nome. Se ele não existir em `styles`, a leitura falha com a mensagem do nó. Campos de estilo
  seguem os nomes das structs de estilo do elemento (`cornerRadius`, `borderWidth` e assim por diante).
- `props` aceita só os nomes que o tipo conhece. Nome desconhecido em `props` é erro de validação, não é
  ignorado.
- `Button` com `label` cria um rótulo filho no carregamento; o rótulo não aparece no arquivo.

Limites de validação: ids únicos; tipos existentes; raio, largura e altura finitos; `minSize` não negativo;
ids com até 64 caracteres; no máximo 10 000 nós por arquivo.

## 3. Do JSON à árvore em tempo de execução

O caminho tem quatro passos, e nenhum deles guarda ponteiro cru:

1. **Leitura para um modelo genérico.** O texto vira um valor JSON com ordem de chaves preservada. Ainda
   não há elemento nenhum. Esse passo reaproveita a ideia do leitor de `Config/UIConfig.cpp`, mas com um
   valor genérico, porque a ferramenta precisa guardar campos que não conhece (ver seção 4).
2. **Validação em estágio.** Todo o arquivo é conferido antes de qualquer mudança: versão, tipos, props,
   estilos, ids e limites. Se algo falha, a árvore atual fica intacta e a mensagem diz o nó e o campo, como
   `UIConfigManager::ValidateAndStage` faz hoje.
3. **Fábrica por tipo.** Uma tabela leva o nome do tipo a uma função que cria o elemento com
   `ElementTree::Create<T>(pai)`. Cada tipo também tem uma tabela de propriedades: nome, tipo esperado e
   função que aplica o valor. A fábrica não conhece o JSON, só recebe valores já validados.
4. **Mapa de ids para handles.** O carregador guarda `id -> UIHandle` (spec 2.1: índice mais geração).
   Quando a árvore muda, um handle antigo deixa de valer sozinho, então não há risco de apontar para
   memória liberada. Para acessar um elemento, a ferramenta chama `tree.Get(handle)` no momento do uso e
   nunca guarda o `Element*` entre quadros (a regra do `ElementTree` já diz isso).

Um exemplo de por que isso importa: o usuário apaga "botao-iniciar" e depois aperta Desfazer. O mapa de ids
é refeito a partir do arquivo ou do histórico, e o handle velho não aponta para o botão novo, porque a
geração do slot mudou.

## 4. Ajustes sem perder dados

A ferramenta lê e grava o mesmo arquivo. Para não apagar campos de versões futuras:

- Cada objeto do JSON guarda, além dos campos conhecidos, um bloco de **campos desconhecidos** com o valor
  original, na mesma ordem.
- Ao salvar, a ferramenta escreve os campos conhecidos e, em seguida, os desconhecidos, sem alterar o valor.
- Um campo que a ferramenta não reconhece nunca é descartado. Um campo conhecido só muda se o usuário
  mexeu nele.
- Um arquivo de versão mais nova que a da ferramenta não é salvo por ela: ela oferece "salvar como cópia".

Exemplo: uma versão futura do formato acrescenta `"tooltip": "Inicia a partida"` num botão. A ferramenta
antiga abre o arquivo, move o botão e salva. O `tooltip` continua no arquivo, com o mesmo texto.

## 5. Otimização de draw calls

**Lista de itens por quadro.** Cada elemento gera `UIRenderItem` numa lista única. O `RenderExtractor` ordena
a lista pela ordem de pintura e junta itens consecutivos que têm o mesmo atlas, material, recorte e flags
num único lote. Um lote é uma chamada de desenho. Por isso a ordem de pintura manda: itens do mesmo atlas
precisam estar lado a lado para virar um lote só.

**Agrupamento por atlas, recorte e shader.** Não é possível reordenar itens que não são vizinhos sem mudar o
resultado, porque a mistura de cor depende da ordem. O agrupamento, então, só aproveita a ordem que já existe:
o painel de propriedades e o texto de uma janela são pintados em sequência, e a ferramenta evita trocar de
recorte a cada item.

**Cache dos elementos estáticos.** Um elemento que não mudou não precisa gerar a lista de novo. O estado sujo
(`UIDirtyState`) marca o que mudou: `DirtyDrawData` para aparência, `DirtyTransform` e `DirtyArrange` para
posição. A `UIRefreshPolicy` decide quando reconstruir: `ShouldRebuild` olha o tempo e o estado sujo, e
`TryRebuild` registra a reconstrução e limpa os bits. Assim, a grade parada não gera nada por quadro; ao
arrastar, só o elemento arrastado é reconstruído.

**Cache do pacote.** Se nenhum bit sujo foi marcado no quadro inteiro, o `FramePacket` do quadro anterior é
reaproveitado. A ferramenta mostra o último pacote e só recalcula quando um bit aparece.

## 6. Fases

**Fase 1: leitura e escrita do JSON com validação.**
Modelo genérico com campos desconhecidos, validação em estágio, escrita canônica, testes de ida e volta.

**Fase 2: janela com grade e seleção.**
Janela do adaptador SDL3, grade desenhada com `MakeLine`, clique seleciona o elemento sob o ponteiro, borda
de seleção com `MakeSolidRect` e `AppendOutlineItems`.

**Fase 3: menu de botão direito e arrasto.**
Menu de contexto com os tipos do mapa (modelo `Widgets/Menu.hpp`), criação no ponto do clique, arrasto para
mover, bordas e cantos para redimensionar, com o mínimo de tamanho de cada tipo.

**Fase 4: propriedades.**
Painel lateral com `TextField`, `Slider`, `Toggle` e `Dropdown` ligados às propriedades do elemento
selecionado. Mudança passa pelo validador antes de entrar na árvore.

**Fase 5: integração com a engine.**
A engine carrega o mesmo arquivo pela fábrica e pelo mapa de ids. A integração segue o plano do
`TOOLENGINE_INTEGRATION_GUIDE.md`: o adaptador de janela e entrada da engine é reaproveitado, sem segundo
laço de eventos e sem ImGui.

## 7. Critérios de aceite

**Fase 1**
- Ida e volta: ler e salvar um arquivo sem mudanças não altera nenhum byte de valor.
- Campo desconhecido em qualquer nível sobrevive a leitura, edição e escrita.
- Arquivo com erro (tipo, prop ou estilo desconhecido, id repetido, versão errada) é recusado e a árvore
  atual não muda.
- Teste com um arquivo de cada erro, cada um com a mensagem do nó.

**Fase 2**
- A grade aparece com espaçamento configurável e a seleção acompanha o clique em qualquer elemento aninhado.
- Captura com fonte real e checagem de pixels da borda de seleção.

**Fase 3**
- O menu lista todos os tipos do mapa, e cada um cria um elemento válido no ponto do clique.
- Arrastar e redimensionar respeitam o mínimo; o elemento volta ao lugar com Esc durante o arrasto.
- Teste de geometria: arrastar além do mínimo para no mínimo, como a janela já faz.

**Fase 4**
- Editar uma propriedade e salvar grava o novo valor; desfazer volta o valor anterior no arquivo.
- Valor inválido no painel é recusado e mostrado no campo, sem entrar na árvore.

**Fase 5**
- Uma tela real da engine é montada a partir do arquivo e tem o mesmo tamanho e posição que na ferramenta.
- Nenhuma dependência nova no núcleo SpriteUI. O alvo da engine segue sem ImGui.
- Conta de draw calls da tela igual ou menor que a da versão montada à mão, medida pelo `ExtractionStats`.

## 8. Fora de escopo agora

- Editor de animação e de timeline (existe `ToonTimeline`, mas fica para depois).
- Importação de arquivos de layout de outras ferramentas.
- Edição de texto com IME dentro da ferramenta: a ferramenta usa o campo de texto do próprio SpriteUI.
