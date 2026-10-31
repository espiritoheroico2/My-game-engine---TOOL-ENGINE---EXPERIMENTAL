# JavaScript vs. C++ — análise de viabilidade (v0.6)

**Pergunta:** manter JS nos scripts ou migrar tudo para C++ para ganhar desempenho?

## Estado real hoje
- **Nenhum JavaScript roda hoje.** Os arquivos `.tool.js` são criados/editados, mas ainda não há VM (ver `RUNTIME.md`).
- **Todo o caminho quente já é C++ nativo:** renderização (SoftRenderer/GpuRenderer), física (Newtoon), Tools (Jump/Move/Follow/Spin/OnCollision),
  partículas, UI, Controlador Narrativo (compilado para IR e executado nativamente) e pós-processamento.
- Portanto, trocar "JS por C++" **não acelera nada que existe hoje**: o JS nunca esteve no caminho de desempenho.

## O que a decisão realmente afeta
Só a linguagem do **código de jogo escrito pelo usuário** (lógica de gameplay específica).

| Opção | Velocidade | Custo para o usuário | Segurança / export | Veredito |
|---|---|---|---|---|
| QuickJS (JS embutido) | interpretador: ordens de grandeza mais lento que C++ em laços apertados, mas suficiente para "cola" de gameplay (dezenas de chamadas por quadro) | zero instalação, hot-reload instantâneo | sandbox simples (sem I/O por padrão) | **padrão** para scripts |
| C++ compilado pelo usuário | máxima | exige compilador + CMake na máquina do usuário; recarregar = recompilar; quebra o ideal "ferramenta leve" | código nativo arbitrário (risco, difícil de exportar p/ Web/Android) | só como **plugin opcional** (ModTool) |
| WASM (futuro) | próximo do nativo | toolchain própria (Rust/AssemblyScript/C++→wasm) | sandbox forte, portável | opção futura p/ scripts pesados |

## Decisão
1. **Não migrar tudo para C++.** Não há ganho (o JS não está no caminho quente) e o custo de usabilidade seria alto.
2. **Mover lógica comum para blocos nativos** (já feito): Tools, Narrative (Start/Update/Create/SavePoint), UI e Partículas cobrem o que mais se repete em jogos sem escrever código — e rodam em C++.
3. **Script = cola.** Regra prática: se algo roda por objeto por quadro em laço grande (ex.: IA de 1000 inimigos), vira **Tool nativo** ou módulo C++ (plugin), não script.
4. **Orçamento por quadro:** o Profiler já tem zonas; ao ligar a VM, medir o tempo de script por quadro e avisar quando passar de ~2 ms.
5. Revisar quando houver benchmarks reais com a VM ligada (item 1 do `NEXT_STEPS.md`).
