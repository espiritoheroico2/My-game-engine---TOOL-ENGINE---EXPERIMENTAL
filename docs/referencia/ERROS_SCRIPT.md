# Erros de script (ToolObject, QuickJS)

Como o editor verifica e mostra erros dos scripts, e o que cada codigo quer dizer.

## Onde aparece

| Quando | Onde | O que mostra |
|---|---|---|
| Enquanto voce digita (0,4 s depois da ultima tecla) | Inspector > ToolObject Script > lista abaixo do codigo | Erros e avisos com a linha e o trecho da linha |
| Ao salvar o script | Inspector e Console | Cada problema com o nome do objeto e a linha |
| Ao dar Play | Console | Problemas de todos os scripts da cena, antes de rodar |
| Durante o Play | Console e Inspector (Script status) | Erro de execucao com funcao e linha |
| Console > "Check scripts" ou comando `scripts` | Console | Todos os objetos com script |

No Console, clique numa linha que comeca com `[Nome]` para selecionar o objeto. O filtro aceita texto
(ex.: `Cubo` ou `TOOL-S007`). Os botoes Info, Warnings e Errors escondem cada nivel.

## Codigos

| Codigo | Nivel | Significado | Exemplo de correcao |
|---|---|---|---|
| TOOL-S001 | erro | Sintaxe invalida: o script nao compila. A linha e a coluna vem do QuickJS. | Feche o parenteses, a chave ou a aspa indicada |
| TOOL-S002 | erro | Erro durante start, update ou OnEventTrigger. O script para; o resto do jogo continua. | Veja a linha no Console |
| TOOL-S003 | aviso | Nenhuma das funcoes de entrada existe: o objeto nao faz nada. | `export function update(obj, dt) { }` |
| TOOL-S004 | erro ou aviso | Nome, argumento ou tecla invalidos (funcao desconhecida, numero errado de argumentos, `console.log`, tecla `"w"`). | `setPosition("Cubo", x, y, z)` tem 4 argumentos |
| TOOL-S005 | aviso | Comando para um objeto que nao existe na cena (uma vez por Play). | Confira o nome na Hierarchy |
| TOOL-S006 | aviso | Valor invalido: NaN, infinito, fora da faixa ou campo do Transform nao numerico. A mudanca e ignorada. | `obj.transform.pos[1]` precisa ser numero |
| TOOL-S007 | erro | Propriedade que nao existe em `obj`. | `obj.position` -> `obj.transform.pos` |
| TOOL-S008 | erro ou aviso | Recurso nao suportado: `class`, `import`, `this`. | Use funcoes e o parametro `obj` |
| TOOL-S009 | erro | Erro interno do sistema de scripts. O editor continua; os scripts param. | Reinicie o Play e avise, com o texto do erro |

## try/catch

Um erro lancado pelo host (argumento invalido, por exemplo) e uma excecao JavaScript: se voce
usar try/catch, o script continua.

```
try {
  setPosition("Camera", "abc", 0, 0);
} catch (e) {
  debug.warn("camera nao movida: " + e.message);
}
```

Sem try/catch, o erro para o script desse objeto e aparece no Console com a linha.

## Funcoes de log para o script

- `debug.console(msg)`: informacao.
- `debug.warn(msg)`: aviso (amarelo). O script continua.
- `debug.error(msg)`: erro (vermelho). O script continua.

## Limites conhecidos

- A verificacao de regras (S004, S007, S008) e feita sobre o texto. Ela nao sabe o tipo das variaveis:
  `obj.transform.pos[0] = "abc"` passa na verificacao e cai no TOOL-S002 ou S006 durante o Play.
- Nomes de funcao declarados em outro arquivo nao existem: scripts nao importam codigo (ainda).
- O verificador roda no editor. No VS Code, a sintaxe tambem aparece (o VS Code le `toolengine.d.ts`
  para o autocomplete).
