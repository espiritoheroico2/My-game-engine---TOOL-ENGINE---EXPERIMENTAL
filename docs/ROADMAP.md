# Roteiro consolidado do ToolEngine

Ultima atualizacao: 2026-11-18  
Versao de referencia: `0.7.19-beta (em andamento)`

Este documento e a fonte resumida para decidir o que fazer a seguir. Ele consolida os planos de
os antigos documentos operacionais de tarefas, estado atual, P1/P6 e proximos passos.
Ele tambem organiza os planos em `docs/CHECKLIST.md`, `docs/UI_SPRITE_PLAN.md`,
`docs/PERFORMANCE_PLAN.md`, `docs/RUNTIME.md`, `docs/SECURITY.md` e `docs/BUILD.md`.

Os documentos especializados continuam valendo para detalhes tecnicos e verificacoes. Em caso de
contradicao, esta pagina define o resumo da prioridade; o changelog define o que entrou em cada versao.

## 1. Estado da versao

### Funcionando e integrado

- Core C++20: ECS, scene, eventos, assets, physics, audio, narrative, modding e scripting estatico.
- Renderizacao CPU e GPU por Diligent: Software, Vulkan e OpenGL; D3D11/D3D12 existem como caminhos no codigo.
- Light Prism integrada como iluminacao CPU/GPU, com SSAO, sombras CSM e de luz pontual/spot.
- Editor com Hierarchy, Inspector, Scene, Game, Console, Profiler, Assets e tela inicial SpriteUI.
- Instanciamento P1 implementado no caminho principal de render, prepass, SSAO e sombras.
- GLSL commitado regenerado a partir do HLSL e verificado por um teste de bindings.
- Exportacao Windows x64 e Linux x64, modo jogador, projetos em pasta propria e captura de dados do Profiler.
- Texturas, emissao, transparencia por objeto, neblina, particulas, chunks opcionais e multi-material.

### Implementado, mas nao fechado por validacao

| Area | O que falta antes de declarar concluido |
|---|---|
| P1 instanciamento | Cena real em Vulkan/OpenGL, screenshot antes/depois, `DrawStats` e CSV com `--ecs 1000 --play` |
| Troca de API | Validacao visual da janela em uma sessao interativa e medicao da troca |
| SpriteUI | Validar em GPU real, migrar paineis ativos e verificar docking em janela real |
| OpenGL | Validar texturas, bloom, sombra pontual e neblina em uma maquina com GPU |
| D3D11/D3D12 | Compilar e executar em Windows com driver real |
| Profiler/export | Confirmar a aparencia final e abrir o Markdown exportado em uso real |
| ECS 500/1000 | Reproduzir a estabilidade interativa e medir ganho com render ativo |
| Texturas | Mipmaps/bilinear, sRGB e textura por parte ainda nao estao fechados |

### Nao implementar agora

- Rewrite completo do ImGui em uma unica etapa.
- Flecs ou substituicao integral do ECS.
- D3D12, ray tracing e Hi-Z antes de estabilizar P1 e validar o caminho principal.
- GPU resident rendering ou vegetacao voxel antes de P2/P3/P5 terem dados medidos.
- QuickJS como subprocesso ou backend com acesso externo ao projeto.
- Migracao de todo o projeto para C++, porque isso nao melhora os caminhos quentes atuais.

## 2. Ordem recomendada

### Fase A: fechar a base verificada

1. **P1 visual e benchmark**
   - Finalizar a validacao visual do instanciamento.
   - Executar Vulkan e OpenGL na mesma cena.
   - Registrar `DrawStats`, CSV e screenshots.
   - Resultado minimo: imagem equivalente, menos draw calls e nenhum erro de binding.
   - Detalhes: seção “Implementacao tecnica do P1” neste documento.

2. **Diagnosticar D3D12**
   - Capturar `ppCompilerOutput` em `createShader`.
   - Confirmar o campo na versao vendorizada do Diligent.
   - Copiar `dxcompiler.dll` e `dxil.dll` para o executavel.
   - Nao trocar o compilador nem o shader antes de obter o erro completo.
   - Trabalhar em uma maquina Windows com GPU.

3. **Corrigir estabilidade do editor**
   - Reproduzir o crash de `test ECS: 500/10000`.
   - Capturar `crash_report.txt`, console e zonas do Profiler.
   - Tratar erros de memoria sem ocultar falhas.

4. **Unificar verificacao**
   - Depois de qualquer codigo novo, rodar pelo menos o alvo relevante, `tool_tests` e o
     `ctest` da configuracao usada.
   - Nunca marcar uma tarefa grafica como concluida sem teste visual em janela real.

### Fase B: estabilizar APIs e shaders

5. **T8 - shader nativo por backend**
   - HLSL continua para D3D11/D3D12/Vulkan.
   - Criar GLSL OpenGL 430 dedicado em vez de depender da traducao como caminho de producao.
   - Compartilhar nomes, semantica e registradores.
   - Fazer `check_bindings.py` validar HLSL e GLSL.

6. **T9 - troca de API por etapas**
   - Transformar a troca em uma maquina de estados por quadro.
   - Preservar a escolha solicitada pelo usuario.
   - Continuar em CPU somente apos falha controlada do backend.
   - Validar Vulkan, OpenGL, D3D11, D3D12 e Software.

7. **T10 - tempo real da GPU**
   - Adicionar timestamp queries ao RHI.
   - Medir cada passe, nao apenas uma zona aproximada da CPU.
   - Separar CPU, GPU, readback, apresentacao e espera ociosa.

8. **Zero-copy**
   - Depois de estabilizar a troca e o readback assincrono, avaliar apresentacao direta.
   - Usar como pre-requisito FSR e pos-processamento GPU.

### Fase C: migracao incremental da interface

9. **Settings SpriteUI**
   - Integrar `settings_bridge.hpp` ao painel ativo.
   - Completar Appearance, Settings, Game, Lighting, Optimizing e Language.
   - Nao remover os controles ImGui enquanto a paridade nao estiver testada.

10. **Migrar uma janela por vez**
   - Ordem sugerida: Assets/Scenes, Export/Editor info, Input Map, Script, Project dialog,
     Profiler/Console/Inspector/Scene/Game e, por ultimo, Narrative.
   - Cada janela deve passar por modelo/teste, paridade visual, fallback e remocao do ImGui.

11. **Integrar cache e Splitter**
   - Aplicar `TextLayoutCache` aos paineis reais.
   - Registrar benchmark antes/depois com a mesma cena e a mesma maquina.
   - Melhorar layout por medicao, nao por supostas draw calls.

### Fase D: fechar tarefas tecnicas pendentes

12. **D3D12, T7**
   - Capturar `ppCompilerOutput` no `createShader`.
   - Confirmar se o campo existe na versao vendorizada do Diligent.
   - Copiar `dxcompiler.dll` e `dxil.dll` ao lado do executavel.
   - Registrar o erro real do compilador; nao trocar de compilador antes dessa prova.

13. **D3D12, T11**
   - Compilar Diligent e projeto com o mesmo CRT e `_ITERATOR_DEBUG_LEVEL`.
   - Tratar os avisos MSVC relevantes: C4324, C4456, C4457, C4146, C4702 e C4701.
   - Criterio: zero LNK C4743 em build Windows limpo.

14. **Instabilidade, T12**
   - Executar `test ECS: 10000`, depois cargas maiores.
   - Capturar `crash_report.txt`, console e memoria estimada.
   - Proibir o spawner de continuar ao exceder memoria disponivel.

15. **Docking, T13**
   - Escolher um gerenciador unico: `WindowManager` ou `SpriteUI::DrawOrder/InputRouter`.
   - Ligar todas as regras antes de remover ImGui.
   - Validar popup, arraste pelo grip, limiar de dock e multicamada em janela real.

## 3. Plano de escala P2-P6

| Etapa | Acao | Pre-requisito | Aprovar somente com |
|---|---|---|---|
| P2 | Culling por frustum na CPU | Malha com centro/raio; P1 medido | Câmera sem objetos sumindo na borda |
| P3 | Compute culling + indirect draw | P2 estabilizado; RHI com dispatch/indirect | Mesma imagem e ganho medido em GPU real |
| P4 | Oclusao Hi-Z | P3 | Cena atras de paredes; aceitar uma latencia de quadro |
| P5 | LOD por distancia | P1-P3 medidos | Triangulos menores sem pop visual |
| P6 | Impostores ou voxels distantes | P1-P5 medidos | Comparacao visual e FPS em floresta densa |

Nao aplicar P3-P6 antes de P1-P2 terem sido medidos. Hi-Z pode causar atrasos de aparecimento em
movimentos rapidos; P5 exige simplificacao offline; P6 e especulativo ate haver uma cena de referencia.

### Implementacao tecnica do P1

- O lote deve agrupar por malha e textura/material.
- Um `StructuredBuffer` envia matriz e dados por instancia uma vez por quadro.
- `BatchBuffer` guarda o indice inicial e cabe em um UBO pequeno.
- O Vertex Shader usa `batchFirst + SV_InstanceID`.
- Nao agrupar transparentes nem bloom nesta fase, pois precisam de estado/ordem diferentes.
- Sombras e prepass podem ser agrupados separadamente, mas somente apos validar o passe principal.
- Critérios obrigatórios: imagem equivalente, draw calls perto do numero de lotes, menos buffer
  uploads e nenhuma falha de binding.

### Implementacao tecnica P2-P6

- P2 precisa de centro e raio calculados na carga da malha e transformados pela matriz do mundo.
- Sombras nao devem usar o frustum da câmera; objetos fora da tela ainda podem projetar sombra.
- P3 exige `dispatch`, buffer de contagem e `drawIndexedIndirect` no RHI.
- P4 usa a depth piramide do quadro anterior e aceita uma latencia de um quadro.
- P5 cria lotes separados por malha simplificada e escolhe pelo raio/distancia.
- P6 usa impostores/voxels somente apos P1-P5 terem sido medidos em cena real.

## 4. Performance por beneficio

### Referencia atual do SpriteUI

Benchmark CPU em Release, executado em Windows com MSVC:

| Fase | Tempo medio |
|---|---:|
| Layout | 0.4352 ms/quadro |
| Texto | 0.1277 ms/quadro |
| Extracao | 0.2998 ms/quadro |
| Assembleia | 0.3806 ms/quadro |
| Input | 0.0035 ms/quadro |
| Total | 1.2953 ms/quadro (772 quadros/s) |

Cenario: 20 janelas, 200 botoes e labels TrueType; 1000 quadros medidos apos 20 de aquecimento.
Os numeros dependem da maquina e não comparam uma versão anterior nem o Editor integrado.
Antes de afirmar ganho, integrar `TextLayoutCache` aos paineis ativos e repetir a mesma medicao.

Ordem tecnica recomendada:

1. P1-P2: reduzir escritas de buffer e objetos fora da tela.
2. Readback assincrono e, depois, apresentacao direta.
3. Filtros/texturas: mipmaps, sRGB e filtragem bilinear.
4. `World::each` sem copia e indice por tag.
5. Hierarquia de muitos filhos medida com o Profiler.
6. Contexto QuickJS por arquivo.
7. Compute, indirect draw, Hi-Z e LOD.

Nao otimizar o ECS com SoA ou execution paralelo apenas por possivel ganho. Primeiro medir o caminho
real com a cena benchmark e manter callbacks seguros que possam criar/destruir entidades.

## 5. Scripts e runtime

- A recomendacao continua QuickJS como runtime padrao.
- Usar `IScriptHost` com um contexto por arquivo ou pool controlado.
- Expor somente objeto, fisica, Input, Audio e Scene.
- Nunca expor `fs`, rede, processo ou acesso arbitrario ao projeto.
- Definir limite de memoria e tempo por quadro.
- Tools nativos continuam sendo o caminho rapido para Jump, Move, Follow, Physics e Gameplay comum.
- C++ deve ser opcional para plugins/mods de alto desempenho, nao o substituto dos scripts.
- WASM pode ser estudado posteriormente para modding seguro e scripts portaveis.

Detalhes: `docs/RUNTIME.md` e `docs/JS_VS_CPP.md`.

## 6. Trabalho pendente por modulo

### Render

- Zero-copy e readback assincrono.
- FXAA/nitidez e bloom na GPU.
- Validacao opcional em Settings.
- Texturas por parte, mipmaps e sRGB.
- Cache do skybox.
- Grid/gizmos 2D adaptados a camera ortografica.
- Culling por oclusao.
- FSR com compute e vetores de movimento.
- Sombras pontuais ja implementadas; fechar paridade OpenGL e teste visual.
- Shaders nativos por API.

### 2D e UI

- Entidade Sprite e Sprite Editor.
- Migracao ImGui -> SpriteUI incremental.
- TextArea, Browse, fontes do projeto e imagens.
- DataGrid virtualizado e binding reativo.
- FreeType e, depois, HarfBuzz.
- UI no mundo em 3D.

### Particulas e fisica

- Profundidade ja implementada no caminho atual; fechar gravidade/quique e vento.
- Compute shader, indirect draw e emission por malha somente apos compute no RHI.
- Sub-emissores, fraturamento e audio espacial.
- Colisores exatos de cilindro, cone, capsula, roda e prismas.
- Colisao por chunk e selecao/undo no Inspector.

### Projeto e exportacao

- Editar nome e autor pela aba Project.
- Fazer Android, WebGL e UWP somente apos fechar os backends principais.
- Versao do executavel no Windows.
- Exportacao segura com manifesto e assinatura antes de expandir plataformas.
- Gravacao atomica de cenas em `.tmp` + rename.

### Seguranca e repositorio

- Nao executar scripts externos ou limpar limites de tamanho.
- Validar todos os numeros de render, path, package e shader.
- Mover implementacoes pesadas de headers para `.cpp` em modulos, com render primeiro.
- Fixar ou venderizar dependencias com licenca registrada.
- Reduzir artefatos de build fora do pacote; manter os dos terceiros necessarios.
- Atualizar nomes de versao em um unico lugar.

## 7. Criterios de aceite global

Nenhuma tarefa fica pronta sem:

1. Build relevante concluido.
2. Testes automatizados verdes.
3. Teste visual ou interativo registrado quando houver interface/GPU.
4. Medicao antes/depois na mesma configuracao para qualquer afirmacao de desempenho.
5. Changelog, checklist e este roteiro atualizados.
6. Itens nao verificados escritos explicitamente como pendentes, sem linguagem de sucesso.

## 8. Onde encontrar detalhes

| Tema | Documento |
|---|---|
| Estado por versao | `docs/CHANGELOG.md` |
| Checklist detalhada | `docs/CHECKLIST.md` |
| Tarefas e estado P1-P6 | Este documento |
| Pesquisa de milhoes de objetos | Seções P1-P6 deste documento |
| Performance | `docs/PERFORMANCE_PLAN.md`, `docs/PERFORMANCE_AUDIT.md` |
| SpriteUI | `docs/UI_SPRITE_PLAN.md`, `docs/UI.md`, `docs/UI_FX.md` |
| Shaders | `docs/SHADER_AUDIT.md`, `docs/SHADER_API_MATRIX.md` |
| Build | `docs/BUILD.md` |
| Scripts | `docs/RUNTIME.md`, `docs/JS_VS_CPP.md`, `docs/SCRIPT_ERRORS.md` |
| Seguranca | `docs/SECURITY.md` |
| Peso do repo | `docs/REPO_WEIGHT.md` |
