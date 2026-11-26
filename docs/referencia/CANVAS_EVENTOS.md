# Canvas e eventos (referência)

Status: **projeto aprovado, ainda não implementado**. Este documento descreve a API que os scripts vão usar para criar elementos de UI no canvas e reagir a eles. Quando a implementação estiver pronta, ele vira a referência de ajuda da engine.

## Ideia geral

- Um **canvas** é a tela de UI de uma entidade. Ele é dividido em **regiões** por pontos de divisão e linhas (feito no editor).
- Cada região tem um número. A numeração segue a ordem de criação. Quando regiões se fundem, a fusão fica com o menor número (fusão da 4 com a 7 = 4).
- Cada **elemento** (botão, texto, barra) recebe um **id único** no momento da criação. O id vale para o **jogo inteiro**, tem letras e números (formato `e` + base 36, ex.: `e1`, `ea`, `e10`) e nunca é reaproveitado durante a sessão.
- Cada elemento tem uma **ordem Z**: o de valor maior fica por cima dos outros. Elementos novos entram por cima de todos.
- Elementos e o jogo se comunicam por **eventos**, no estilo de eventos do C#: você se inscreve com uma função, e o evento chama todas as funções inscritas quando é disparado.

## Criar um elemento

```js
// start
const element = canvas.CreateElement(button, "Save Game", 0.05, 0.08, border.round, 5);
```

Parâmetros de `CreateElement(tipo, texto, x, y, estiloBorda, regiao)`:

| Parâmetro | Significado |
|---|---|
| `tipo` | Constante do tipo de elemento: `button`, `text`, `bar`. |
| `texto` | Texto exibido no elemento. |
| `x`, `y` | Posição do elemento dentro do canvas, em fração (0 = esquerda/topo, 1 = direita/base). |
| `estiloBorda` | Constante de borda: `border.round`, e outras. |
| `regiao` | Número da região onde o elemento fica. Usa a numeração salva no canvas. |

O retorno é um objeto de elemento com:

- `element.getID()` ou `element.id`: id único do elemento, em texto (ex.: `"e1"`).
- `element.z`: ordem Z do elemento. Maior fica por cima.
- `element.Pressed`: evento disparado quando o elemento é clicado.

## Ordem Z

- O editor mostra o campo **Ordem Z** em cada elemento (valor maior fica por cima).
- Elementos com a mesma ordem Z são desenhados na ordem da lista.
- Um clique acerta o elemento de cima, então o que está visível recebe o clique.

## Eventos

Cada evento tem três operações. Elas seguem a sintaxe do C#, com métodos no lugar de operadores, porque JavaScript não permite `+=` sobre funções:

| C# | Script | Efeito |
|---|---|---|
| `Evento += Funcao;` | `Evento.add(Funcao);` | inscreve a função |
| `Evento -= Funcao;` | `Evento.remove(Funcao);` | cancela a inscrição |
| `Evento?.Invoke(args);` | `Evento.trigger(args);` | chama todas as funções inscritas |

Exemplo completo:

```js
// start
const element = canvas.CreateElement(button, "Save Game", 0.05, 0.08, border.round, 5);
element.Pressed.add(SalvarJogo);
element.Pressed.add(Sincronizar);
element.Pressed.remove(SalvarJogo);

function SalvarJogo(sender, args) {
  debug.console("salvando " + sender);
}

// em qualquer lugar do script
Event.TrocarCena.trigger("cena game");
```

### Regras

- **Sem `if` para escolher o botão.** Cada elemento tem seu próprio evento. A função inscrita recebe o elemento que disparou como primeiro argumento (`sender`).
- **Um disparo chama cada função uma vez.** Se o `trigger` estiver dentro de um loop, as funções rodam a cada volta.
- **Ordem:** as funções rodam na ordem em que foram inscritas.
- **Inscrever a mesma função duas vezes** faz ela rodar duas vezes, como no C#. `remove` apaga uma inscrição.
- **Cancelar durante o disparo** não afeta o disparo atual. A lista é copiada antes de rodar.
- **Recursão:** se uma função dispara o próprio evento, o engine para depois de um limite de profundidade e avisa no console.

### Evento global

`Event.NomeDoEvento` é um evento global por nome. Use para avisar vários scripts ao mesmo tempo, por exemplo uma troca de cena. Um nome é criado na primeira vez em que é usado.

## Regiões em tempo de execução

As regiões vêm do editor e são salvas com o canvas. Os scripts conseguem usar o número da região, mas as operações de mostrar, esconder, adicionar ou remover todos os elementos de uma região ainda não estão definidas. Ver "Em aberto".

## Em aberto

1. Tipo do argumento de `trigger`: texto livre (como `"cena game"`) ou objeto com campos (como `{ cena: "game" }`).
2. Operações por região: mostrar, esconder, adicionar e remover elementos pelo número da região.
3. Ids carregados de arquivo: hoje, ids repetidos num arquivo editado à mão não são corrigidos. Só ids vazios ou fora do formato são trocados.

## Relação com o sistema antigo

O campo `action` do botão (`add_`, `sub_`, `reset_` sobre variáveis) continua funcionando. O novo sistema de eventos não o substitui de imediato.

## Próximo passo

Implementar primeiro o canvas (divisões, regiões e ids) e depois a API de scripts descrita aqui. Um jogo demo só entra depois que o canvas estiver funcionando.
