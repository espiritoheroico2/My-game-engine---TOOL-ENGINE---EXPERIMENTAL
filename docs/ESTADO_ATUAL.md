# Estado atual da engine (2026-11-18)

Este documento resume o que foi feito e o que falta. Detalhes de cada tarefa estão em `CODAR.MD`
(tarefas T1 a T10 e os testes que você faz) e em `docs/P1_IMPLEMENTACAO.md` (secao 12).

## 1. Feito

### Renderização
- **P1 (instanciamento):** agrupamento por malha e textura (`instance_batch.hpp`), buffer de instâncias no
  HLSL, passe principal, prepass e sombras por lote. Brilho e transparentes continuam por objeto.
- **Correção de conflito de shader:** `Instances` estava em `t15` (mesmo registrador do `IblBrdfLut`).
  Agora é `t17`.
- **GLSL 430 regenerado** a partir do HLSL. Estava desatualizado e sem instanciamento.
- **Culling por frustum:** módulo puro e testado (`frustum_cull.hpp`). Não está ligado ao renderizador (P2).

### Ferramentas e testes
- `tools/shaders/check_bindings.py`: detecta registrador repetido no HLSL (por variante `#ifdef`) e
  `binding` repetido no GLSL (por tipo de recurso). Roda no ctest como `Shaders.Bindings`.
- `tools/shaders/build_glsl.py`: regenera o GLSL; `--check` confirma que está em dia.
- ctest: 100% (32 testes) no build atual.

### Editor
- **Troca de API:** o pedido responde na hora (o seletor mostra a escolha e o status "Switching to ...").
  A troca pesada roda no início do quadro seguinte, e pedidos seguidos valem só o último.
- **Profiler:** seção "Priority" no topo com draw calls, objetos, triângulos, CPU %, tempo de CPU, tempo de GPU
  (zona), render ms, memória do processo, VRAM e atraso (média, p99, máximo, jitter). Esses itens saem em
  destaque (fonte maior e negrito simulado). O gráfico de frame time também fica em destaque.

### Documentos
- `CODAR.MD`: revisão com tarefas T7 a T10 e os testes que você faz.
- `docs/P1_IMPLEMENTACAO.md`: seção 12 com o estado real do P1 e o prompt da próxima etapa.
- `docs/PESQUISA_MILHARES_OBJETOS.md`: pesquisa e plano P1 a P6.

## 2. Não verificado aqui (precisa do seu build ou da sua GPU)

- Imagem igual entre Vulkan e OpenGL com instanciamento.
- Troca de API sem clique repetido e com a janela respondendo.
- D3D12: funciona ou não. O backend não está verificado (ver T7).
- Profiler: aparência final em negrito e tamanho.

## 3. Falta fazer (em ordem sugerida)

| Tarefa | O que é | Por quê |
|---|---|---|
| T7 | Diagnóstico do D3D12: capturar a saída do compilador de shader e copiar `dxcompiler.dll` | D3D12 não funciona hoje |
| T8 | Shaders nativos em HLSL e GLSL (sem tradução) | Pedido seu; o GLSL atual é gerado e tem ajustes manuais |
| T9 | Troca de API em etapas, uma por quadro | A troca ainda trava a janela por um tempo |
| T10 | Medir uso de GPU (timestamp queries no RHI) | "GPU %" não é medido hoje |
| P2 | Ligar o culling por frustum ao renderizador | Depende do teste visual de câmera |
| P3 a P6 | Culling em GPU, Hi-Z, LOD e vegetação distante | Dependem de GPU para validar |
| T1 a T6 | Janelas ainda em ImGui, Settings, docking, segurança | Ver `CODAR.MD` |

## 4. Erros pequenos encontrados nesta revisão

- `Instances` em `t15` conflitava com `IblBrdfLut` (o D3D12 rejeita registradores repetidos). Corrigido.
- O GLSL do repositório não correspondia ao HLSL atual. Regenerado.
- `P1_IMPLEMENTACAO.md` ainda citava `t15` como livre. Corrigido.
- Atenção: o `Shaders.Bindings` só enxerga `#ifdef` simples. Se usar `#if` com expressões, revise o checador.
