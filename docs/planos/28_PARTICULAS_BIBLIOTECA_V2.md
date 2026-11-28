# 28 — Partículas: biblioteca v2 (P-A)

Extensão da biblioteca `engine/particles` (CPU) sem editor e sem desenho. Plano-base: `17_SISTEMA_DE_PARTICULAS.md`.

## 1. O que foi feito

- **Formas de emissão** (`emitter.hpp`): `point`, `sphere` (volume), `box` (volume, meias-medidas `box`), `cone` (disco de base de raio `radius`, 0 = vértice), `disc` (área, perpendicular a `init.direction`), `line` (comprimento `2*radius`). Mesma semente determinística (hash do emissor + índice global).
- **Modo de velocidade** (`init.velocity`): `cone` (eixo `direction`, meio ângulo `coneAngle`), `radial` (para longe da origem, a partir da posição sorteada), `random` (direção uniforme na esfera).
- **Campos novos por partícula** (`pool.hpp`): `temp` (temperatura simulada), `alpha` (alfa suave), `rot` (rotação do billboard). `ColorOverLife` e `SizeOverLife` já existiam e foram mantidos; novo `AlphaOverLife` (smoothstep).
- **Módulos novos** (`modules.hpp`): `turbulence` (ruído de gradiente 3D por hash, determinístico, sem alocação; 3 amostras deslocadas = vetor; campo evolui com o tempo simulado), `buoyancy` (`v.y += valor * temp * dt`, com `cooling` exponencial da temperatura), `alphaOverLife`.
- **Quads de render** (`quads.hpp`): `buildQuads(sistema, QuadView, QuadBatch, parallel)` gera um `QuadInstance` (32 B: centro, tamanho, rotação, cor RGBA8 com alfa já multiplicado pelo campo `alpha`, índice de material, temperatura) por partícula viva, em buffer contíguo; `chunkOffset` dá o trecho de cada chunk. `QuadView::billboard(right, up, forward)` (3D, eixos da câmera) e `QuadView::plane2D(z)` (2D, plano XY). `quadCorners` devolve os 4 cantos com rotação (para CPU/testes).
- **Parâmetros de render por efeito** (`effect.hpp`): `shader` (`alpha`/`additive`/`bloom`), `emissive` (>= 0), `material` (índice copiado para cada quad). `QuadBatch` expõe `shader` e `emissive`.

## 2. JSON (campos novos)

```json
{ "shader": "bloom", "emissive": 2.5, "material": 3,
  "emitter": { "shape": "box|sphere|cone|disc|line|point", "box": [1,2,3], "radius": 1 },
  "init": { "velocity": "cone|radial|random", "temperature": 1, "rotation": [0,360] },
  "modules": [ {"type":"turbulence","amplitude":2,"frequency":0.4,"scroll":0.3,"seed":9},
               {"type":"buoyancy","value":3,"cooling":0.5},
               {"type":"alphaOverLife","from":1,"to":0} ] }
```
Rejeitados (falha fechada): shader/forma/velocidade desconhecidos, `emissive` negativo ou > 1000, raio ou `box` negativos, `frequency <= 0`, `cooling < 0`. Mudança de comportamento: antes um `shape` desconhecido virava ponto; agora rejeita o efeito.

## 3. Testes (`tool_particles_tests`, Particles.Core)

17 testes, 0 falhas (8 antigos + caixa, esfera, cone, disco/linha/radial, turbulência determinística, empuxo, quads billboard, quads 2D, JSON v2).

## 4. Medição (Release, MSVC, 20 threads, 1 milhão vivas, `tool_particles_bench_v2`)

Efeito: caixa, turbulência + empuxo + arraste + cor/tamanho/alfa ao longo da vida; 3 execuções.

| Execução | update (ms/quadro) | quads 3D (ms) | quads 2D (ms) |
|---|---|---|---|
| 1 | 19.835 | 7.141 | 6.820 |
| 2 | 19.640 | 7.157 | 6.788 |
| 3 | 19.863 | 7.135 | 6.787 |
| **mediana** | **19.835** | **7.141** | **6.788** |

~998 mil vivas; buffer de quads 30,5 MB (32 B por quad). Serial (1 thread): update 179,6 ms, quads 10,7 ms. O custo do update é dominado pela turbulência (3 ruídos 3D por partícula por quadro); sem ela o núcleo é bem mais leve (ver bench anterior). Para 1M no editor a 60 FPS, o custo total (~27 ms) exige reduzir turbulência (menos amostras, ou aplicar a cada 2 quadros) ou menos partículas.

## 5. Falta para o editor (P-B)

Componente de partículas na cena (origem, efeito, play/pause/reset); inspector com formas, módulos, shader, emissive; render em tela: upload do `QuadBatch` como instance buffer, shader additive/alpha com teste de profundidade (sem escrita), passe de bloom usando `emissive`; ordenação por profundidade para `alpha`; presets (fumaça, fogo); possível LOD/redução de turbulência por distância.
