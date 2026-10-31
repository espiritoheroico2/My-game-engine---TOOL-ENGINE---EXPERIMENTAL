# Runtime de código — recomendação

**Objetivo:** scripts de objeto (`ToolObject`: start/update/OnDelete/DeleteAndReuse/onCollisionEnter…), leves, seguros e rápidos.

| Opção | Tamanho | Velocidade | Segurança/sandbox | Observação |
|---|---|---|---|---|
| **QuickJS** (MIT) | ~250 KB | média (interpretador) | boa: sem acesso a nada que você não exponha | JS moderno (classes, async); embutir é simples (C puro) |
| Lua / LuaJIT | ~200 KB | alta (JIT) | boa | JIT proibido em iOS/consoles; sintaxe diferente do JS já usado nos templates |
| WebAssembly (wasm3 / WAMR) | 100–300 KB | alta (AOT: muito alta) | **excelente** (sandbox por design) | Precisa de compilador (AssemblyScript/C#/C++) — fricção para iniciantes |
| C# (.NET/Mono) | grande | alta | média | Pesado para uma engine "menos de 1 GB e leve" |

## Recomendação
1. **Agora: QuickJS** atrás de uma interface `IScriptHost`. Combina com os arquivos `.tool.js` que já existem, é pequeno, permite orçamento de tempo por quadro (interrupt handler) e limite de memória.
2. API exposta mínima (ver tópico "Script API" na janela Help): objeto, física, Input, Audio, Scene. **Nunca** `fs`, rede ou `eval` de arquivos externos.
3. Os **Tools** nativos (Jump, Move, Follow…) continuam sendo o caminho de desempenho; script chama o mesmo código C++.
4. **Depois:** WebAssembly como segundo backend, para código de alto desempenho e **mods em sandbox** (compartilha a decisão do doc de design §29).
5. Desempenho: scripts só rodam em objetos com script habilitado; `update` é pulado se o método estiver vazio (detectado ao carregar).
