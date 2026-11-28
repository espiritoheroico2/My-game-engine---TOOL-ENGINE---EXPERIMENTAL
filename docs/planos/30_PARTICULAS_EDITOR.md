# 30 — Partículas v2 no editor (P-B)

Integra a biblioteca `engine/particles` (planos 17 e 28) ao editor SpriteUI como objeto colocável na cena, com render em tela no `GpuRenderer`. O sistema antigo (`ParticleComp`, `fx.hpp`) continua intacto e convive na mesma cena.

## 1. O que foi feito

| # | Requisito | Entrega |
|---|---|---|
| 1 | Objeto "Particles" 3D/2D com play/pause/reset e efeito JSON | Componente `ParticleFxComp` (`apps/editor/components.hpp`). Botões Play, Pause, Stop and clear (limpa tudo) e Burst no Inspector; origem = posição do objeto no mundo (o eixo do cone gira com o objeto). Formas volumétricas: ponto, esfera, caixa, base de cone, disco, linha. Arquivo de efeito em `<projeto>/Effects/*.json` (Load/Save). Modo 2D = quads no plano XY do objeto. Entra por Inspector > Resources > Particle FX (v2). |
| 2 | Presets | `engine/particles/include/tool/particles/presets.hpp`: **smoke** (caixa larga + turbulência + empuxo, dispersão tipo CS2), **fire** (cone, empuxo forte, shader bloom) e **fluid** (jato de água: gravidade + arraste). Embutidos no código e gravados em `Effects/` na primeira vez (nunca sobrescreve edição do usuário). |
| 3 | Render em tela sem sobrepor objetos | Passe `Particles` no `GpuRenderer` (depois do opaco e do transparente): **teste de profundidade ligado, escrita desligada**; alfa ordenado de trás para frente (ordenação aproximada O(n) em 1024 baldes, `sortQuadsBackToFront`); **soft particles** com a profundidade linear do prepasso (`GBuf.w`). Se o SSAO está desligado, o prepasso roda só quando há partículas com `soft > 0`. |
| 4 | Shaders | `assets/shaders/hlsl/particles.hlsl` (VS instanciado lendo `StructuredBuffer<QuadInstance>`; `PSAlpha`, `PSAdditive`, `PSGlow`). Novo `PipelineDesc::blendAdditive` no RHI (Diligent: ONE/ONE). **Bloom**: reaproveita o bloom que o renderer já tinha (alvo `glow_` RGBA16F -> limiar -> blur -> soma); as partículas com shader `bloom` somam `cor * emissive` em `glow_`. |
| 5 | Inspector | Seção "Particle FX" com subgrupos Emitter / Motion / Over lifetime / Rendering: forma, taxa, raio, caixa, vida, tamanho, cor inicial, modo e velocidade, direção, cone, gravidade, arraste, turbulência (intensidade, escala, evolução, taxa de amostragem), empuxo (força e resfriamento), tamanho/cor/opacidade ao longo da vida, shader, emissive, soft distance, tamanho do pool. **Tooltips em todos os campos novos** (`inspector_tips.hpp`) com tradução pt-BR e es com acentos (`assets/lang/pt-BR.json` e `es.json`). |
| 6 | Qualidade da turbulência | `Module::every` (JSON `"every"`, 1 a 64): quadros entre atualizações da turbulência. **Padrão = 1 (todo quadro), igual ao comportamento anterior** (teste prova resultado idêntico). Com N > 1 cada chunk tem fase própria `(quadro + chunk) % N` e usa `dt * N`, espalhando o custo. Campo "Turbulence update every N frames" no Inspector. O padrão não foi mudado. |
| 7 | Cenas antigas | `ParticleComp` e a chave `"particles"` da cena continuam carregando como antes (convivência, sem migração). O componente novo vai na chave `"particlesFx"` com o efeito completo embutido na cena. |

Outros detalhes:

- `ParticleSystem::setEffect` permite editar o efeito ao vivo (as partículas vivas continuam).
- `effectToJson` (ida e volta do JSON) e campos novos do efeito: `soft`, `every`.
- Sem GPU (renderer por software): fallback simples em `view_overlays.hpp` (bolinhas, no máximo ~3000 por objeto, sem profundidade).
- Flags de teste/medição no editor: `--fx-bench N` e `--fx-norender` (ver seção 3).

## 2. Testes

- `tool_particles_tests` (Particles.Core): 21 testes, 0 falhas (4 novos: presets e ida e volta do JSON, `soft`/`every` no JSON, padrão da turbulência inalterado, ordenação de quads).
- Selftest do editor (`tool_editor --selftest`): bloco novo "particle fx": preset de fumaça carrega, arquivos em `Effects/`, `every = 1` por padrão, N quadros geram partículas vivas, um quad por partícula viva, ordenação de trás para frente, Pause congela, Play retoma, **Stop limpa e não há quads**, **JSON inválido rejeitado com aviso** (módulo, shader, forma, `soft` negativo, texto malformado, arquivo ruim, caminho fora do projeto, arquivo inexistente), arquivo válido carrega, cena salva/carrega o componente, **cena antiga com `ParticleComp` abre como antes**, taxa de amostragem determinística.

## 3. Medição (Vulkan, RX 6500 XT, Release, 1280x720, vista Game a cada quadro)

Comando (em `Projeto Final/`): `ToolEditor.exe --project <pasta-temp> --instances 100 [--fx-bench 100000 [--fx-norender]] --api vulkan --bench-dt 0.016667 --bench-frames 300 --bench-warmup 60`.
Cena: 100 cubos + 1 emissor v2 com o preset de fumaça (shader alpha, soft particles, turbulência a cada quadro), 100 mil partículas vivas e constantes (rajada única, vida longa, tamanho 0,15 a 0,25 m, caixa de 20 x 8 x 16 m na frente da câmera). Média de 300 quadros (uma execução cada).

| Caso | Quadro médio | p95 | FPS |
|---|---|---|---|
| Sem partículas (só a cena) | 3,38 ms | 3,60 ms | 296 |
| 100 mil vivas, simulação na CPU, **sem** passe de partículas (`--fx-norender`) | 6,89 ms | 7,93 ms | 145 |
| 100 mil vivas, **com** passe de partículas (100 mil quads, alfa ordenado, soft) | 8,77 ms | 10,90 ms | 114 |

Leitura: a simulação (CPU, turbulência incluída) custa ~3,5 ms por quadro; geração dos quads + ordenação + upload + desenho (passe de partículas) somam ~1,9 ms. Uma comparação de imagem (`TOOL_BENCH_IMGDUMP`) confirma que o passe altera os pixels (2,18 milhões de bytes diferentes com 20 mil partículas). Não medi o tempo de GPU isolado (o renderer só tem tempo de CPU por passe); o custo acima é de quadro inteiro.

## 4. O que NÃO foi feito e limites

- **OpenGL (GLES)**: o passe de partículas não existe (o renderer usa GLSL pré-gerado e não há `particles.430.glsl`). Vulkan e D3D12 têm. Em OpenGL as partículas v2 somem da vista 3D (sem erro).
- **Bloom**: reaproveitado, não reescrito. O brilho das partículas só aparece com Bloom ligado na iluminação da cena (Settings), porque o halo vem do alvo `glow_` desse bloom. Com Bloom desligado o shader `bloom` vira um aditivo comum (cor sem ganho).
- **Soft particles**: usam o `GBuf.w` do prepasso (só geometria que entra no prepasso: objetos opacos da fila de prepasso). Chunks de voxel e geometria fora do frustum da CPU também entram conforme o prepasso já fazia. O ceu (w = 0) não apaga partícula.
- **Profundidade**: o teste usa o depth do passe opaco/transparente; partículas **não** escrevem profundidade, então partículas alfa só se ordenam entre si pela ordenação aproximada (não é exata: baldes lineares; partículas de objetos diferentes não se ordenam entre objetos).
- **Ordenação 2D**: no modo 2D os quads saem na ordem de emissão (sem ordenação).
- **Simulação na CPU**: 100 mil partículas custam a atualização do sistema (turbulência é o maior custo) mais a geração e o upload dos quads (32 B cada). Para mais que ~200 mil a 60 FPS use `every` > 1 ou menos partículas. Compute na GPU não faz parte desta etapa.
- **Direção vs. rotação do objeto**: o eixo do cone/velocidade (`init.direction`) acompanha a rotação do objeto; a caixa de emissão e a gravidade ficam alinhadas ao mundo (escala do objeto é ignorada).
- **Scripts**: `addComponent(obj, "ParticleFxComp")` ainda não existe (só `ParticleComp`); pelo código use o Inspector. Os comandos `particles_on/off` do Narrative só valem para o sistema antigo.
- **Texturas**: o sprite é um disco suave procedural (sem atlas/flipbook); `material` do quad ainda não é usado pelo shader.
- **Undo**: as edições do Inspector entram no histórico como as outras seções (mesma regra de fim de interação); o estado de execução (partículas vivas) não é desfeito.
